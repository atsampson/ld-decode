/************************************************************************

    palcolour.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018-2019 Simon Inns
    Copyright (C) 2019 Adam Sampson

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

#ifndef PALCOLOUR_H
#define PALCOLOUR_H

#include <QDebug>
#include <QObject>
#include <QScopedPointer>
#include <QVector>
#include <QtMath>

#include "lddecodemetadata.h"

#include "sourcefield.h"
#include "transformpal.h"

class PalColour : public QObject
{
    Q_OBJECT

public:
    explicit PalColour(QObject *parent = nullptr);

    // Specify which filter to use to separate luma and chroma information.
    enum ChromaFilterMode {
        // PALColour's 2D FIR filter
        palColourFilter = 0,
        // 2D Transform PAL frequency-domain filter
        transform2DFilter,
        // 3D Transform PAL frequency-domain filter
        transform3DFilter
    };

    struct Configuration {
        bool blackAndWhite = false;
        // This value is chosen to compensate for typical LaserDisc characteristics
        double chromaGain = 0.735;
        ChromaFilterMode chromaFilter = palColourFilter;
        TransformPal::TransformMode transformMode = TransformPal::thresholdMode;
        double transformThreshold = 0.4;
        bool showFFTs = false;
        qint32 showPositionX = 200;
        qint32 showPositionY = 200;

        // Interlaced line 44 is PAL line 23 (the first active half-line)
        qint32 firstActiveLine = 44;
        // Interlaced line 619 is PAL line 623 (the last active half-line)
        qint32 lastActiveLine = 620;

        qint32 getLookBehind() const;
        qint32 getLookAhead() const;
    };

    const Configuration &getConfiguration() const;
    void updateConfiguration(const LdDecodeMetaData::VideoParameters &videoParameters,
                             const Configuration &configuration);

    // Decode two fields to produce an interlaced frame.
    QByteArray decodeFrame(const SourceField &firstField, const SourceField &secondField);

    // Decode a sequence of fields into a sequence of interlaced frames
    void decodeFrames(const QVector<SourceField> &inputFields, qint32 startIndex, qint32 endIndex,
                      QVector<QByteArray> &outputFrames);

    // Maximum frame size, based on PAL
    static constexpr qint32 MAX_WIDTH = 1135;

private:
    // Information about a line we're decoding.
    struct LineInfo {
        qint32 number;
        double bp, bq;
        double Vsw;
        double burstAmplitude;
    };

    void buildLookUpTables();
    void detectBursts(const SourceField &inputField, QVector<LineInfo> &lines);
    void decodeField(const SourceField &inputField, const double *chromaData, const QVector<LineInfo> &lines,
                     double chromaGain, QByteArray &outputFrame);
    void detectBurst(LineInfo &line, const quint16 *inputData);
    template <typename ChromaSample, bool PREFILTERED_CHROMA>
    void decodeLine(const SourceField &inputField, const ChromaSample *chromaData, const LineInfo &line, double chromaGain,
                    QByteArray &outputFrame);

    // Configuration parameters
    bool configurationSet;
    Configuration configuration;
    LdDecodeMetaData::VideoParameters videoParameters;

    // Transform PAL filter
    QScopedPointer<TransformPal> transformPal;

    // The subcarrier reference signal
    double sine[MAX_WIDTH], cosine[MAX_WIDTH];

    // Coefficients for the three 2D chroma low-pass filters. There are
    // separate filters for U and V, but only the signs differ, so they can
    // share a set of coefficients.
    //
    // The filters are horizontally and vertically symmetrical, so each 2D
    // array represents one quarter of a filter. The zeroth horizontal element
    // is included in the sum twice, so the coefficient is halved to
    // compensate. Each filter is (2 * FILTER_SIZE) + 1 elements wide.
    static constexpr qint32 FILTER_SIZE = 7;
    double cfilt[FILTER_SIZE + 1][4];
    double yfilt[FILTER_SIZE + 1][2];
};

#endif // PALCOLOUR_H
