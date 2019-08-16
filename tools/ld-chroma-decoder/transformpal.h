/************************************************************************

    transformpal.h

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

#ifndef TRANSFORMPAL_H
#define TRANSFORMPAL_H

#include <QByteArray>
#include <QDebug>
#include <QObject>
#include <fftw3.h>

#include "lddecodemetadata.h"

class TransformPal {
public:
    TransformPal();
    ~TransformPal();

    // threshold is the similarity threshold for the filter (values from 0-1
    // are meaningful; 0.6 is pyctools-pal's default).
    // yTile/xTile are the FFT tile size.
    void updateConfiguration(const LdDecodeMetaData::VideoParameters &videoParameters,
                             double threshold, qint32 yTile, qint32 xTile);

    // Filter an input field.
    // Returns a pointer to an array of the same size (owned by this object)
    // containing the chroma signal.
    const double *filterField(qint32 firstFieldLine, qint32 lastFieldLine, const QByteArray &fieldData);

private:
    void freeConfiguration();
    void applyFilter();

    // Configuration parameters
    bool configurationSet;
    LdDecodeMetaData::VideoParameters videoParameters;
    double threshold;

    // Maximum field size, based on PAL
    static constexpr qint32 MAX_WIDTH = 1135;

    // Bounds on FFT tile size
    static constexpr qint32 MIN_YTILE = 1;
    static constexpr qint32 MAX_YTILE = 64;
    static constexpr qint32 MIN_XTILE = 1;
    static constexpr qint32 MAX_XTILE = 64;

    // FFT input and output sizes.
    // The input field is divided into tiles of xTile x yTile, with adjacent
    // tiles overlapping by half a tile.
    //
    // For the Transform PAL filter, the size needs to be chosen so that the
    // carrier frequencies land in the middle of a cell or on the boundary
    // between cells.
    qint32 yTile;
    qint32 xTile;

    // Each tile is converted to the frequency domain using forwardPlan, which
    // gives a complex result of size xComplex x yComplex (roughly half the
    // size of the input, because the input data was real, i.e. contained no
    // negative frequencies).
    qint32 yComplex;
    qint32 xComplex;

    // Window function applied before the FFT
    double windowFunction[MAX_YTILE][MAX_XTILE];

    // FFT input/output buffers
    double *fftReal;
    fftw_complex *fftComplexIn;
    fftw_complex *fftComplexOut;

    // FFT plans
    fftw_plan forwardPlan, inversePlan;

    // The combined result of all the FFT processing.
    // Inverse-FFT results are accumulated into this buffer.
    QVector<double> chromaBuf;
};

#endif
