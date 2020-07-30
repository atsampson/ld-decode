/************************************************************************

    comb.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018 Chad Page
    Copyright (C) 2018-2019 Simon Inns
    Copyright (C) 2020 Adam Sampson

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

#include "comb.h"

#include "deemp.h"

#include <QScopedPointer>

// Definitions of static constexpr data members, for compatibility with
// pre-C++17 compilers
constexpr qint32 Comb::MAX_WIDTH;
constexpr qint32 Comb::MAX_HEIGHT;

// Public methods -----------------------------------------------------------------------------------------------------

Comb::Comb()
    : configurationSet(false)
{
}

qint32 Comb::Configuration::getLookBehind() const {
    // XXX Always uses lookahead even when not in 3D mode -- fix
    return 1;
}

qint32 Comb::Configuration::getLookAhead() const {
    return 1;
}

// Return the current configuration
const Comb::Configuration &Comb::getConfiguration() const {
    return configuration;
}

// Set the comb filter configuration parameters
void Comb::updateConfiguration(const LdDecodeMetaData::VideoParameters &_videoParameters, const Comb::Configuration &_configuration)
{
    // Copy the configuration parameters
    videoParameters = _videoParameters;
    configuration = _configuration;

    // Range check the frame dimensions
    if (videoParameters.fieldWidth > MAX_WIDTH) qCritical() << "Comb::Comb(): Frame width exceeds allowed maximum!";
    if (((videoParameters.fieldHeight * 2) - 1) > MAX_HEIGHT) qCritical() << "Comb::Comb(): Frame height exceeds allowed maximum!";

    // Range check the video start
    if (videoParameters.activeVideoStart < 16) qCritical() << "Comb::Comb(): activeVideoStart must be > 16!";

    configurationSet = true;
}

void Comb::decodeFrames(const QVector<SourceField> &inputFields, qint32 startIndex, qint32 endIndex,
                        QVector<RGBFrame> &outputFrames)
{
    assert(configurationSet);
    assert((outputFrames.size() * 2) == (endIndex - startIndex));

    // Buffers for the next, current and previous frame.
    // Because we only need three of these, we allocate them upfront then
    // rotate the pointers below.
    QScopedPointer<FrameBuffer> nextFrameBuffer, currentFrameBuffer, previousFrameBuffer;
    nextFrameBuffer.reset(new FrameBuffer(videoParameters, configuration));
    currentFrameBuffer.reset(new FrameBuffer(videoParameters, configuration));
    previousFrameBuffer.reset(new FrameBuffer(videoParameters, configuration));

    // Decode each pair of fields into a frame
    for (qint32 fieldIndex = startIndex - 4; fieldIndex < endIndex; fieldIndex += 2) {
        const qint32 frameIndex = (fieldIndex - startIndex) / 2;

        // Rotate the buffers
        {
            QScopedPointer<FrameBuffer> recycle(previousFrameBuffer.take());
            previousFrameBuffer.reset(currentFrameBuffer.take());
            currentFrameBuffer.reset(nextFrameBuffer.take());
            nextFrameBuffer.reset(recycle.take());
        }

        // Load fields into the buffer
        nextFrameBuffer->loadFields(inputFields[fieldIndex + 2], inputFields[fieldIndex + 3]);

        // Extract chroma using 1D filter
        nextFrameBuffer->split1D();

#if 1
        // Extract chroma using 3D filter with only 2D sources
if (configuration.adaptive) {
        nextFrameBuffer->split2D();
} else {
        nextFrameBuffer->split3D(*nextFrameBuffer, *nextFrameBuffer, true); // XXX ugly!
}
        nextFrameBuffer->splitIQ(true);
        nextFrameBuffer->adjustY(true);
#endif

        // XXX Maybe only look at chroma for low-detail areas, or something...

        if (fieldIndex < startIndex) {
            // This is a look-behind frame; no further decoding needed.
            continue;
        }

        if (configuration.dimensions == 2) {
            // Extract chroma using 2D filter
            currentFrameBuffer->split2D();
        } else if (configuration.dimensions == 3) {
            // Extract chroma using 3D filter
            currentFrameBuffer->split3D(*previousFrameBuffer, *nextFrameBuffer, false);
        }

        // Demodulate chroma giving I/Q
        currentFrameBuffer->splitIQ(false);

        // Extract Y from baseband and I/Q
        currentFrameBuffer->adjustY(false);

        // Post-filter I/Q
        if (configuration.colorlpf) currentFrameBuffer->filterIQ();

        // Apply noise reduction
        currentFrameBuffer->doYNR();
        currentFrameBuffer->doCNR();

        // Convert the YIQ result to RGB
        outputFrames[frameIndex] = currentFrameBuffer->yiqToRgbFrame();

        // Overlay the map if required
        if (configuration.dimensions == 3 && configuration.showMap) {
            currentFrameBuffer->overlayMap(outputFrames[frameIndex]);
        }
    }
}

// Private methods ----------------------------------------------------------------------------------------------------

Comb::FrameBuffer::FrameBuffer(const LdDecodeMetaData::VideoParameters &videoParameters_,
                               const Configuration &configuration_)
    : videoParameters(videoParameters_), configuration(configuration_)
{
    // Set the frame height
    frameHeight = ((videoParameters.fieldHeight * 2) - 1);

    // Set the IRE scale
    irescale = (videoParameters.white16bIre - videoParameters.black16bIre) / 100;
}

/* 
 * The color burst frequency is 227.5 cycles per line, so it flips 180 degrees for each line.
 * 
 * The color burst *signal* is at 180 degrees, which is a greenish yellow.
 *
 * When SCH phase is 0 (properly aligned) the color burst is in phase with the leading edge of the HSYNC pulse.
 *
 * Per RS-170 note 6, Fields 1 and 4 have positive/rising burst phase at that point on even (1-based!) lines.
 * The color burst signal should begin exactly 19 cycles later.
 *
 * getLinePhase returns true if the color burst is rising at the leading edge.
 */

inline qint32 Comb::FrameBuffer::getFieldID(qint32 lineNumber) const
{
    bool isFirstField = ((lineNumber % 2) == 0);
    
    return isFirstField ? firstFieldPhaseID : secondFieldPhaseID;
}

// NOTE:  lineNumber is presumed to be starting at 1.  (This lines up with how splitIQ calls it)
inline bool Comb::FrameBuffer::getLinePhase(qint32 lineNumber) const
{
    qint32 fieldID = getFieldID(lineNumber);
    bool isPositivePhaseOnEvenLines = (fieldID == 1) || (fieldID == 4);    

    int fieldLine = (lineNumber / 2);
    bool isEvenLine = (fieldLine % 2) == 0;
    
    return isEvenLine ? isPositivePhaseOnEvenLines : !isPositivePhaseOnEvenLines;
}

// Interlace two source fields into the framebuffer.
void Comb::FrameBuffer::loadFields(const SourceField &firstField, const SourceField &secondField)
{
    // Interlace the input fields and place in the frame buffer
    qint32 fieldLine = 0;
    rawbuffer.clear();
    for (qint32 frameLine = 0; frameLine < frameHeight; frameLine += 2) {
        rawbuffer.append(firstField.data.mid(fieldLine * videoParameters.fieldWidth, videoParameters.fieldWidth));
        rawbuffer.append(secondField.data.mid(fieldLine * videoParameters.fieldWidth, videoParameters.fieldWidth));
        fieldLine++;
    }

    // Set the phase IDs for the frame
    firstFieldPhaseID = firstField.field.fieldPhaseID;
    secondFieldPhaseID = secondField.field.fieldPhaseID;
}

// Extract chroma into clpbuffer[0] using a 1D bandpass filter.
//
// The filter is [0.5, 0, -1.0, 0, 0.5], a gentle bandpass centred on fSC, with
// a gain of -2. So the output will contain all of the chroma signal, but also
// whatever luma components ended up in the same frequency range.
//
// This also acts as an alias removal pre-filter for the quadrature detector in
// splitIQ, so we use its result for split2D rather than the raw signal.
void Comb::FrameBuffer::split1D()
{
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get a pointer to the line's data
        const quint16 *line = rawbuffer.data() + (lineNumber * videoParameters.fieldWidth);

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            double tc1 = (((line[h + 2] + line[h - 2]) / 2) - line[h]);

            // Record the 1D C value
            clpbuffer[0].pixel[lineNumber][h] = tc1;

            // Compute luma based on this
            similarityBuffer[lineNumber][h] = YIQ(line[h] + (tc1 / 2), 0, 0);
        }
    }
}

// Extract chroma into clpbuffer[1] using a 2D 3-line adaptive filter.
//
// Because the phase of the chroma signal changes by 180 degrees from line to
// line, subtracting two adjacent lines that contain the same information will
// give you just the chroma signal. But real images don't necessarily contain
// the same information on every line.
//
// The "3-line adaptive" part means that we look at both surrounding lines to
// estimate how similar they are to this one. We can then compute the 2D chroma
// value as a blend of the two differences, weighted by similarity.
void Comb::FrameBuffer::split2D()
{
    // Dummy black line
    static constexpr double blackLine[MAX_WIDTH] = {0};

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get pointers to the surrounding lines of 1D chroma.
        // If a line we need is outside the active area, use blackLine instead.
        const double *previousLine = blackLine;
        if (lineNumber - 2 >= videoParameters.firstActiveFrameLine) {
            previousLine = clpbuffer[0].pixel[lineNumber - 2];
        }
        const double *currentLine = clpbuffer[0].pixel[lineNumber];
        const double *nextLine = blackLine;
        if (lineNumber + 2 < videoParameters.lastActiveFrameLine) {
            nextLine = clpbuffer[0].pixel[lineNumber + 2];
        }

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            double kp, kn;

            // Summing the differences of the *absolute* values of the 1D chroma samples
            // will give us a low value if the two lines are nearly in phase (strong Y)
            // or nearly 180 degrees out of phase (strong C) -- i.e. the two cases where
            // the 2D filter is probably usable. Also give a small bonus if
            // there's a large signal (we think).
            kp  = fabs(fabs(currentLine[h]) - fabs(previousLine[h]));
            kp += fabs(fabs(currentLine[h - 1]) - fabs(previousLine[h - 1]));
            kp -= (fabs(currentLine[h]) + fabs(previousLine[h - 1])) * .10;
            kn  = fabs(fabs(currentLine[h]) - fabs(nextLine[h]));
            kn += fabs(fabs(currentLine[h - 1]) - fabs(nextLine[h - 1]));
            kn -= (fabs(currentLine[h]) + fabs(nextLine[h - 1])) * .10;

            kp /= 2;
            kn /= 2;

            // Map the difference into a weighting 0-1.
            // 1 means in phase or unknown; 0 means out of phase (more than kRange difference).
            const double kRange = 45 * irescale;
            kp = qBound(0.0, 1 - (kp / kRange), 1.0);
            kn = qBound(0.0, 1 - (kn / kRange), 1.0);

            double sc = 1.0;

            if ((kn > 0) || (kp > 0)) {
                // At least one of the next/previous lines has a good phase relationship.

                // If one of them is much better than the other, only use that one
                if (kn > (3 * kp)) kp = 0;
                else if (kp > (3 * kn)) kn = 0;

                sc = (2.0 / (kn + kp));
                if (sc < 1.0) sc = 1.0;
            } else {
                // Neither line has a good phase relationship.

                // But are they similar to each other? If so, we can use both of them!
                if ((fabs(fabs(previousLine[h]) - fabs(nextLine[h])) - fabs((nextLine[h] + previousLine[h]) * .2)) <= 0) {
                    kn = kp = 1;
                }

                // Else kn = kp = 0, so we won't extract any chroma for this sample.
                // (Some NTSC decoders fall back to the 1D chroma in this situation.)
            }

            // Compute the weighted sum of differences, giving the 2D chroma value
            double tc1;
            tc1  = ((currentLine[h] - previousLine[h]) * kp * sc);
            tc1 += ((currentLine[h] - nextLine[h]) * kn * sc);
            tc1 /= 8;

            clpbuffer[1].pixel[lineNumber][h] = tc1;
        }
    }
}

// Extract chroma into clpbuffer[2] using an adaptive 3D filter.
//
// XXX write something about how this works
// XXX The adjustPenalty values -- having a little bit of bias towards using
// another line/frame produces a visibly less noisy picture -- but the bias
// needs to be pretty small
void Comb::FrameBuffer::split3D(const FrameBuffer &previousFrame, const FrameBuffer &nextFrame, bool force2D)
{
    Candidate candidates[8];

    auto& chromaBuffer = clpbuffer[force2D ? 1 : 2];

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            qint32 num = 0;

            // Look at nearby positions that have a 180 degree chroma phase difference from this sample.

            // XXX static constexpr
            const double LINE_BONUS = -2.0;
            const double FIELD_BONUS = LINE_BONUS - 2.0;
            const double FRAME_BONUS = FIELD_BONUS - 2.0;

#if 1
            // Don't use 1D on the first pass, since it often produces spurious colour
            if (!force2D) {
                // Same line, 2 samples left and right
                candidates[num++] = getCandidate(lineNumber, h, *this, lineNumber, h - 2, 0xff8080, 0);
                candidates[num++] = getCandidate(lineNumber, h, *this, lineNumber, h + 2, 0xff8080, 0);
            }
#endif

#if 1
            // Same field, 1 line up and down
            // XXX This is not as good as the existing 2D mode in some ways (and better in others!)
            candidates[num++] = getCandidate(lineNumber, h, *this, lineNumber - 2, h, 0x80ff80, LINE_BONUS);
            candidates[num++] = getCandidate(lineNumber, h, *this, lineNumber + 2, h, 0x80ff80, LINE_BONUS);
#endif
            const qint32 first3D = num;

            // If the next/previous field are available...
            if (!force2D) {
#if 1
                // Immediately adjacent lines in previous/next field
                if (getLinePhase(lineNumber) == getLinePhase(lineNumber - 1)) {
                    candidates[num++] = getCandidate(lineNumber, h, *this, lineNumber + 1, h, 0xffff80, FIELD_BONUS);
                    candidates[num++] = getCandidate(lineNumber, h, previousFrame, lineNumber - 1, h, 0xffff80, FIELD_BONUS);
                } else {
                    candidates[num++] = getCandidate(lineNumber, h, *this, lineNumber - 1, h, 0xffff80, FIELD_BONUS);
                    candidates[num++] = getCandidate(lineNumber, h, nextFrame, lineNumber + 1, h, 0xffff80, FIELD_BONUS);
                }
#endif

#if 1
                // Previous/next frame, same position
                candidates[num++] = getCandidate(lineNumber, h, previousFrame, lineNumber, h, 0x8080ff, FRAME_BONUS);
                candidates[num++] = getCandidate(lineNumber, h, nextFrame, lineNumber, h, 0xff80ff, FRAME_BONUS);
#endif
            }

            // Find the candidate with the lowest penalty
            qint32 best = 0;
            for (qint32 i = 1; i < num; i++) {
                if (candidates[i].penalty < candidates[best].penalty) best = i;
            }

            // If there are several candidates of the same type that are about equally good, use the mean of all of them
            static constexpr double MERGE_LIMIT = 2.0;
            double candidateSample = 0.0;
            qint32 numGood = 0;
            if (best >= first3D) {
                // but only for 2D...
                candidateSample = candidates[best].sample;
                numGood = 1;
            } else {
                for (qint32 i = 0; i < num; i++) {
                    if (candidates[i].penalty < (candidates[best].penalty + MERGE_LIMIT) && (candidates[i].shade == candidates[best].shade)) {
                        candidateSample += candidates[i].sample;
                        numGood++;
                    }
                }
                candidateSample /= numGood;
            }

#if 0
//            if (lineNumber == 158 && h >= 763 && h < 827) { // XXX
//            if (lineNumber == 483) { // frame 14756 GGV
            if (lineNumber == 386) {
                fprintf(stderr, "ref sample %f at y=%d x=%d\n", clpbuffer[0].pixel[lineNumber][h], lineNumber, h);
                for (qint32 i = 0; i < num; i++) {
                    fprintf(stderr, "  candidate %d - sample %f, penalty %f\n", i, candidates[i].sample, candidates[i].penalty);
                }
                double chroma = ((clpbuffer[0].pixel[lineNumber][h] / 2) - candidateSample) / 2;
                fprintf(stderr, "best=%d numGood=%d candidateSample=%f chroma=%f\n", best, numGood, candidateSample, chroma);
            }
#endif

            // This sample is Y + C; the candidate is (ideally) Y - C. So compute C as ((Y + C) - (Y - C)) / 2.
            chromaBuffer.pixel[lineNumber][h] = ((clpbuffer[0].pixel[lineNumber][h] / 2) - candidateSample) / 2;
            shades[lineNumber][h] = candidates[best].shade;

            if (configuration.adaptive && best < first3D) {
                // Use the split2D result
                chromaBuffer.pixel[lineNumber][h] = clpbuffer[1].pixel[lineNumber][h];
            }
        }
    }
}

Comb::FrameBuffer::Candidate Comb::FrameBuffer::getCandidate(qint32 refLineNumber, qint32 refH,
                                                             const FrameBuffer &frameBuffer, qint32 lineNumber, qint32 h,
                                                             quint32 shade, double adjustPenalty)
{
    Candidate result;
    result.sample = frameBuffer.clpbuffer[0].pixel[lineNumber][h] / 2;
    result.shade = shade;

    // If the candidate is outside the active region (vertically), it's not viable
    if (lineNumber < videoParameters.firstActiveFrameLine || lineNumber >= videoParameters.lastActiveFrameLine) {
        result.penalty = 1000.0;
        return result;
    }

    // The target sample should have 180 degrees phase difference from the reference.
    // If it doesn't (e.g. because it's a blank frame or the player skipped), it's not viable.
    const qint32 wantPhase = (2 + (getLinePhase(refLineNumber) ? 2 : 0) + refH) % 4;
    const qint32 havePhase = ((frameBuffer.getLinePhase(lineNumber) ? 2 : 0) + h) % 4;
    if (wantPhase != havePhase) {
        result.penalty = 1000.0;
        return result;
    }

    // Penalty is based on mean difference in IRE over surrounding three luma samples
    double yPenalty = 0.0;
#if 1
    for (qint32 offset = -1; offset < 2; offset++) {
        const double refY = similarityBuffer[refLineNumber][refH + offset].y;
        const double candidateY = frameBuffer.similarityBuffer[lineNumber][h + offset].y;
        yPenalty += fabs(refY - candidateY);
    }
#endif

    // ... and chroma
    // XXX Maybe do this based on hue/saturation difference rather than I/Q?
    double iqPenalty = 0.0;
#if 1
    for (qint32 offset = -1; offset < 2; offset++) {
        const double refI = similarityBuffer[refLineNumber][refH + offset].i;
        const double candidateI = frameBuffer.similarityBuffer[lineNumber][h + offset].i;
        iqPenalty += fabs(refI - candidateI);

        const double refQ = similarityBuffer[refLineNumber][refH + offset].q;
        const double candidateQ = frameBuffer.similarityBuffer[lineNumber][h + offset].q;
        iqPenalty += fabs(refQ - candidateQ);
    }
    // Weaken this relative to luma, to avoid spurious colour in the 2D result from showing through
    iqPenalty *= 0.3;
#endif

    result.penalty = (yPenalty / 3 / irescale) + (iqPenalty / 6 / irescale) + adjustPenalty;
    return result;
}

// Spilt the I and Q
void Comb::FrameBuffer::splitIQ(bool force2D)
{
    // Clear the target frame YIQ buffer
    for (qint32 lineNumber = 0; lineNumber < MAX_HEIGHT; lineNumber++) {
        for (qint32 h = 0; h < MAX_WIDTH; h++) {
            yiqBuffer[lineNumber][h] = YIQ();
        }
    }

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get a pointer to the line's data
        const quint16 *line = rawbuffer.data() + (lineNumber * videoParameters.fieldWidth);
        bool linePhase = getLinePhase(lineNumber);

        double si = 0, sq = 0;
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            qint32 phase = h % 4;

            double cavg;

            switch (force2D ? 2 : configuration.dimensions) {
            case 1:
                cavg = clpbuffer[0].pixel[lineNumber][h] / 2;
                break;
            case 2:
                cavg = clpbuffer[1].pixel[lineNumber][h];
                break;
            default:
                cavg = clpbuffer[2].pixel[lineNumber][h];
                break;
            }

            if (!linePhase) cavg = -cavg;

            switch (phase) {
                case 0: sq = cavg; break;
                case 1: si = -cavg; break;
                case 2: sq = -cavg; break;
                case 3: si = cavg; break;
                default: break;
            }

            yiqBuffer[lineNumber][h].y = line[h];
            yiqBuffer[lineNumber][h].i = si;
            yiqBuffer[lineNumber][h].q = sq;
        }
    }
}

// Filter the IQ from the input YIQ buffer
void Comb::FrameBuffer::filterIQ()
{
    auto iFilter(f_colorlpi);
    auto qFilter(configuration.colorlpf_hq ? f_colorlpi : f_colorlpq);

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        iFilter.clear();
        qFilter.clear();

        qint32 qoffset = configuration.colorlpf_hq ? f_colorlpi_offset : f_colorlpq_offset;

        double filti = 0, filtq = 0;

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            qint32 phase = h % 4;

            switch (phase) {
                case 0: filti = iFilter.feed(yiqBuffer[lineNumber][h].i); break;
                case 1: filtq = qFilter.feed(yiqBuffer[lineNumber][h].q); break;
                case 2: filti = iFilter.feed(yiqBuffer[lineNumber][h].i); break;
                case 3: filtq = qFilter.feed(yiqBuffer[lineNumber][h].q); break;
                default: break;
            }

            yiqBuffer[lineNumber][h - qoffset].i = filti;
            yiqBuffer[lineNumber][h - qoffset].q = filtq;
        }
    }
}

// Remove the colour data from the baseband (Y)
void Comb::FrameBuffer::adjustY(bool force2D)
{
    // remove color data from baseband (Y)
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        bool linePhase = getLinePhase(lineNumber);

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            double comp = 0;
            qint32 phase = h % 4;

            YIQ y = yiqBuffer[lineNumber][h];

            switch (phase) {
                case 0: comp = -y.q; break;
                case 1: comp = y.i; break;
                case 2: comp = y.q; break;
                case 3: comp = -y.i; break;
                default: break;
            }

            if (linePhase) comp = -comp;
            y.y += comp;

            yiqBuffer[lineNumber][h] = y;

            if (force2D) similarityBuffer[lineNumber][h] = y;
        }
    }
}

/*
 * This applies an FIR coring filter to both I and Q color channels.  It's a simple (crude?) NR technique used
 * by LD players, but effective especially on the Y/luma channel.
 *
 * A coring filter removes high frequency components (.4mhz chroma, 2.8mhz luma) of a signal up to a certain point,
 * which removes small high frequency noise.
 */

void Comb::FrameBuffer::doCNR()
{
    if (configuration.cNRLevel == 0) return;

    // High-pass filters for I/Q
    auto iFilter(f_nrc);
    auto qFilter(f_nrc);

    // nr_c is the coring level
    double nr_c = configuration.cNRLevel * irescale;

    QVector<YIQ> hplinef;
    hplinef.resize(videoParameters.fieldWidth + 32);

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Filters not cleared from previous line

        for (qint32 h = videoParameters.activeVideoStart; h <= videoParameters.activeVideoEnd; h++) {
            hplinef[h].i = iFilter.feed(yiqBuffer[lineNumber][h].i);
            hplinef[h].q = qFilter.feed(yiqBuffer[lineNumber][h].q);
        }

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            // Offset by 12 to cover the filter delay
            double ai = hplinef[h + 12].i;
            double aq = hplinef[h + 12].q;

            if (fabs(ai) > nr_c) {
                ai = (ai > 0) ? nr_c : -nr_c;
            }

            if (fabs(aq) > nr_c) {
                aq = (aq > 0) ? nr_c : -nr_c;
            }

            yiqBuffer[lineNumber][h].i -= ai;
            yiqBuffer[lineNumber][h].q -= aq;
        }
    }
}

void Comb::FrameBuffer::doYNR()
{
    if (configuration.yNRLevel == 0) return;

    // High-pass filter for Y
    auto yFilter(f_nr);

    // nr_y is the coring level
    double nr_y = configuration.yNRLevel * irescale;

    QVector<YIQ> hplinef;
    hplinef.resize(videoParameters.fieldWidth + 32);

    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Filter not cleared from previous line

        for (qint32 h = videoParameters.activeVideoStart; h <= videoParameters.activeVideoEnd; h++) {
            hplinef[h].y = yFilter.feed(yiqBuffer[lineNumber][h].y);
        }

        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            double a = hplinef[h + 12].y;

            if (fabs(a) > nr_y) {
                a = (a > 0) ? nr_y : -nr_y;
            }

            yiqBuffer[lineNumber][h].y -= a;
        }
    }
}

// Convert buffer from YIQ to RGB 16-16-16
RGBFrame Comb::FrameBuffer::yiqToRgbFrame()
{
    RGBFrame rgbOutputFrame;
    rgbOutputFrame.resize(videoParameters.fieldWidth * frameHeight * 3); // for RGB 16-16-16

    // Initialise the output frame
    rgbOutputFrame.fill(0);

    // Initialise YIQ to RGB converter
    RGB rgb(videoParameters.white16bIre, videoParameters.black16bIre, configuration.whitePoint75, configuration.chromaGain);

    // Perform YIQ to RGB conversion
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get a pointer to the line
        quint16 *linePointer = rgbOutputFrame.data() + (videoParameters.fieldWidth * 3 * lineNumber);

        // Offset the output by the activeVideoStart to keep the output frame
        // in the same x position as the input video frame
        qint32 o = (videoParameters.activeVideoStart * 3);

        // Fill the output line with the RGB values
        rgb.convertLine(&yiqBuffer[lineNumber][videoParameters.activeVideoStart],
                        &yiqBuffer[lineNumber][videoParameters.activeVideoEnd],
                        &linePointer[o]);
    }

    // Return the RGB frame data
    return rgbOutputFrame;
}

// Convert buffer from YIQ to RGB
void Comb::FrameBuffer::overlayMap(RGBFrame &rgbFrame)
{
    qDebug() << "Comb::FrameBuffer::overlayMap(): Overlaying map onto RGB output";

    // Overlay the map on the output RGB
    for (qint32 lineNumber = videoParameters.firstActiveFrameLine; lineNumber < videoParameters.lastActiveFrameLine; lineNumber++) {
        // Get a pointer to the line
        quint16 *linePointer = rgbFrame.data() + (videoParameters.fieldWidth * 3 * lineNumber);

        // Fill the output frame with the RGB values
        for (qint32 h = videoParameters.activeVideoStart; h < videoParameters.activeVideoEnd; h++) {
            quint32 shade = shades[lineNumber][h];

            double grey = similarityBuffer[lineNumber][h].y / 65535.0;
            qint32 red = grey * (((shade >> 16) & 0xff) << 8);
            qint32 green = grey * (((shade >> 8) & 0xff) << 8);
            qint32 blue = grey * ((shade & 0xff) << 8);

            if (red > 65535) red = 65535;
            if (green > 65535) green = 65535;
            if (blue > 65535) blue = 65535;

            linePointer[(h * 3)] = static_cast<quint16>(red);
            linePointer[(h * 3) + 1] = static_cast<quint16>(green);
            linePointer[(h * 3) + 2] = static_cast<quint16>(blue);
        }
    }
}
