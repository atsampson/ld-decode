/************************************************************************

    transformpal.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019 Adam Sampson

    Reusing code from pyctools-pal, which is:
    Copyright (C) 2014 Jim Easterbrook

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include "transformpal.h"

#include <QtMath>
#include <cassert>
#include <cmath>

/*!
    \class TransformPal

    Transform PAL filter, based on Jim Easterbrook's implementation in
    pyctools-pal. Given a composite signal, this extracts a chroma signal from
    it using frequency-domain processing.

    For a description of the algorithm with examples, see Transform PAL web
    site (http://www.jim-easterbrook.me.uk/pal/).

    Note that this is only a 2D implementation at the moment, which limits the
    quality of the output; it would be possible to extend it to 3D given access
    to multiple fields.
 */

TransformPal::TransformPal()
    : configurationSet(false)
{
}

TransformPal::~TransformPal()
{
    freeConfiguration();
}

void TransformPal::updateConfiguration(const LdDecodeMetaData::VideoParameters &_videoParameters,
                                       double _threshold, qint32 _yTile, qint32 _xTile)
{
    videoParameters = _videoParameters;
    threshold = _threshold;
    yTile = qBound(MIN_YTILE, _yTile, MAX_YTILE);
    xTile = qBound(MIN_XTILE, _xTile, MAX_XTILE);

    // Free anything we previously allocated
    freeConfiguration();

    // Compute the FFT result size
    yComplex = yTile;
    xComplex = (xTile / 2) + 1;

    // Compute the window function applied to the data blocks before the FFT to
    // reduce edge effects. A symmetrical raised-cosine function is chosen so
    // that the overlapping inverse-FFT blocks can be summed directly.
    for (qint32 y = 0; y < yTile; y++) {
        const double windowY = (1 - cos((2 * M_PI * (y + 0.5)) / yTile)) / 2;
        for (qint32 x = 0; x < xTile; x++) {
            const double windowX = (1 - cos((2 * M_PI * (x + 0.5)) / xTile)) / 2;
            windowFunction[y][x] = windowY * windowX;
        }
    }

    // Allocate buffers for FFTW. These must be allocated using FFTW's own
    // functions so they're properly aligned for SIMD operations.
    fftReal = fftw_alloc_real(yTile * xTile);
    fftComplexIn = fftw_alloc_complex(yComplex * xComplex);
    fftComplexOut = fftw_alloc_complex(yComplex * xComplex);

    // Plan FFTW operations
    forwardPlan = fftw_plan_dft_r2c_2d(yTile, xTile, fftReal, fftComplexIn, FFTW_MEASURE);
    inversePlan = fftw_plan_dft_c2r_2d(yTile, xTile, fftComplexOut, fftReal, FFTW_MEASURE);

    // Resize the chroma buffer
    chromaBuf.resize(videoParameters.fieldWidth * videoParameters.fieldHeight);

    configurationSet = true;
}

// Free data structures allocated during configuration
void TransformPal::freeConfiguration()
{
    if (!configurationSet) {
        return;
    }

    // Free FFTW plans and buffers
    fftw_destroy_plan(forwardPlan);
    fftw_destroy_plan(inversePlan);
    fftw_free(fftReal);
    fftw_free(fftComplexIn);
    fftw_free(fftComplexOut);

    configurationSet = false;
}

const double *TransformPal::filterField(qint32 firstFieldLine, qint32 lastFieldLine, const QByteArray &fieldData)
{
    assert(configurationSet);
    assert(!fieldData.isNull());

    const qint32 halfXTile = xTile / 2;
    const qint32 halfYTile = yTile / 2;

    // Pointers to the input and output data
    const quint16 *inputPtr = reinterpret_cast<const quint16 *>(fieldData.data());
    double *outputPtr = chromaBuf.data();

    // Clear chromaBuf
    chromaBuf.fill(0.0);

    // Iterate through the overlapping tile positions, covering the active area.
    // (See TransformThread member variable documentation for how the tiling works.)
    for (qint32 tileY = firstFieldLine - halfYTile; tileY < lastFieldLine; tileY += halfYTile) {
        for (qint32 tileX = videoParameters.activeVideoStart - halfXTile; tileX < videoParameters.activeVideoEnd; tileX += halfXTile) {
            // Work out what portion of this tile is inside the active area
            const qint32 startX = qMax(videoParameters.activeVideoStart - tileX, 0);
            const qint32 endX = qMin(videoParameters.activeVideoEnd - tileX, xTile);
            const qint32 startY = qMax(firstFieldLine - tileY, 0);
            const qint32 endY = qMin(lastFieldLine - tileY, yTile);

            // If we aren't going to fill in the whole tile, zero it first
            if (startX != 0 || endX != xTile || startY != 0 || endY != yTile) {
                for (qint32 i = 0; i < yTile * xTile; i++) {
                    fftReal[i] = 0.0;
                }
            }

            // Copy the input signal into fftReal, applying the window function
            for (qint32 y = startY; y < endY; y++) {
                const quint16 *b = inputPtr + ((tileY + y) * videoParameters.fieldWidth);
                for (qint32 x = startX; x < endX; x++) {
                    fftReal[(y * xTile) + x] = b[tileX + x] * windowFunction[y][x];
                }
            }

            // Convert time domain in fftReal to frequency domain in fftComplexIn
            fftw_execute(forwardPlan);

            // Apply the frequency-domain filter
            applyFilter();

            // Convert frequency domain in fftComplexOut back to time domain in fftReal
            fftw_execute(inversePlan);

            // Overlay the result, normalising the FFTW output, into chromaBuf
            const double fftNorm = yTile * xTile;
            for (qint32 y = startY; y < endY; y++) {
                double *b = outputPtr + ((tileY + y) * videoParameters.fieldWidth);
                for (qint32 x = startX; x < endX; x++) {
                    b[tileX + x] += fftReal[(y * xTile) + x] / fftNorm;
                }
            }
        }
    }

    return chromaBuf.data();
}

// Return the absolute value squared of an fftw_complex
static inline double fftwAbsSq(const fftw_complex &value) {
    return (value[0] * value[0]) + (value[1] * value[1]);
}

// Apply the frequency-domain filter
void TransformPal::applyFilter() {
    // Clear fftComplexOut. We discard values by default; the filter only
    // copies values that look like chroma.
    for (qint32 i = 0; i < xComplex * yComplex; i++) {
        fftComplexOut[i][0] = 0.0;
        fftComplexOut[i][1] = 0.0;
    }

    // This is a direct translation of transform_filter from pyctools-pal.
    // The main simplification is that we don't need to worry about
    // conjugates, because FFTW only returns half the result in the first
    // place. We've also only implemented "threshold" mode for now.
    //
    // The general idea is that a real modulated chroma signal will be
    // symmetrical around the U carrier, which is at fSC Hz and 72 c/aph -- and
    // because we're sampling at 4fSC, this is handily equivalent to being
    // symmetrical around the V carrier owing to wraparound. We look at every
    // point that might be a chroma signal, and only keep it if it's
    // sufficiently symmetrical with its reflection.

    const double threshold_sq = threshold * threshold;

    for (qint32 y = 0; y < yTile; y++) {
        // Reflect around 72 c/aph vertically.
        const qint32 y_ref = ((yTile / 2) + yTile - y) % yTile;

        // Input data for this line and its reflection
        const fftw_complex *bi = fftComplexIn + (y * xComplex);
        const fftw_complex *bi_ref = fftComplexIn + (y_ref * xComplex);

        // Output data for this line and its reflection
        fftw_complex *bo = fftComplexOut + (y * xComplex);
        fftw_complex *bo_ref = fftComplexOut + (y_ref * xComplex);

        // We only need to look at horizontal frequencies that might be chroma (0.5fSC to 2fSC).
        for (qint32 x = xTile / 8; x <= xTile / 4; x++) {
            // Reflect around 4fSC Hz horizontally.
            const qint32 x_ref = (xTile / 2) - x;

            const fftw_complex &in_val = bi[x];
            const fftw_complex &ref_val = bi_ref[x_ref];

            if (x == x_ref && y == y_ref) {
                // This point is its own reflection (i.e. it's a carrier). Keep it!
                bo[x][0] = in_val[0];
                bo[x][1] = in_val[1];
                continue;
            }

            // Compare the magnitudes of the two values.
            // (In fact, we compute the square of the magnitude, and square
            // both sides of the comparison below; this saves an expensive sqrt
            // operation.)
            // XXX Implement other comparison modes
            const double m_in_sq = fftwAbsSq(in_val);
            const double m_ref_sq = fftwAbsSq(ref_val);
            if (m_in_sq < m_ref_sq * threshold_sq || m_ref_sq < m_in_sq * threshold_sq) {
                // They're different. Probably not a chroma signal; throw it away.
                continue;
            }

            // They're similar. Keep it!
            bo[x][0] = in_val[0];
            bo[x][1] = in_val[1];
            bo_ref[x_ref][0] = ref_val[0];
            bo_ref[x_ref][1] = ref_val[1];
        }
    }
}
