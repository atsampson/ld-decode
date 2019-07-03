/************************************************************************

    comb.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018 Chad Page
    Copyright (C) 2018-2019 Simon Inns

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

#ifndef COMB_H
#define COMB_H

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QtMath>

#include "yiq.h"
#include "rgb.h"
#include "opticalflow.h"
#include "yiqbuffer.h"

// Fix required for Mac OS compilation - environment doesn't seem to set up
// the expected definitions properly
#ifndef M_PIl
#define M_PIl 0xc.90fdaa22168c235p-2L
#endif

class Comb
{
public:
    Comb();

    // Comb filter configuration parameters
    struct Configuration {
        bool blackAndWhite;
        bool colorlpf;
        bool colorlpf_hq;
        bool whitePoint100;
        bool use3D;
        bool showOpticalFlowMap;

        qint32 fieldWidth;
        qint32 fieldHeight;

        qint32 activeVideoStart;
        qint32 activeVideoEnd;

        qint32 firstVisibleFieldLine;

        qint32 blackIre;
        qint32 whiteIre;

        qreal cNRLevel;
        qreal yNRLevel;
    };

    Configuration getConfiguration();
    void setConfiguration(const Configuration &configurationParam);
    QByteArray process(QByteArray fieldInputBuffer, qreal burstMedianIre, qint32 fieldPhaseID);

protected:

private:
    // Comb-filter configuration parameters
    Configuration configuration;

    // IRE scaling
    qreal irescale;

    // Input field height
    qint32 fieldHeight;

    // Input field buffer definitions
    struct PixelLine {
        qreal pixel[263][910]; // 263 is the maximum allowed field lines, 910 is the maximum field width
    };

    struct FieldBuffer {
        QByteArray rawbuffer;

        QVector<PixelLine> clpbuffer; // Unfiltered chroma for the current phase (can be I or Q)
        QVector<qreal> kValues;
        YiqBuffer yiqBuffer; // YIQ values for the field

        qreal burstLevel; // The median colour burst amplitude for the field
        qint32 fieldPhaseID; // The phase of the field
    };

    // Optical flow processor
    OpticalFlow opticalFlow;

    // Previous two fields for 3D processing
    FieldBuffer previousFieldBuffer;
    FieldBuffer previousPreviousFieldBuffer;

    void postConfigurationTasks();

    inline qint32 GetFieldID(FieldBuffer *fieldBuffer, qint32 lineNumber);
    inline bool GetLinePhase(FieldBuffer *fieldBuffer, qint32 lineNumber);

    void split1D(FieldBuffer *fieldBuffer);
    void split2D(FieldBuffer *fieldBuffer);
    void split3D(FieldBuffer *currentField, FieldBuffer *previousField);

    void filterIQ(YiqBuffer &yiqBuffer);
    void splitIQ(FieldBuffer *fieldBuffer);

    void doCNR(YiqBuffer &yiqBuffer);
    void doYNR(YiqBuffer &yiqBuffer);

    QByteArray yiqToRgbField(const YiqBuffer &yiqBuffer, qreal burstLevel);
    void overlayOpticalFlowMap(const FieldBuffer &fieldBuffer, QByteArray &rgbOutputField);
    void adjustY(FieldBuffer *fieldBuffer, YiqBuffer &yiqBuffer);
};

#endif // COMB_H
