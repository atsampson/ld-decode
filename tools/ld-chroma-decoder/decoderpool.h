/************************************************************************

    decoderpool.h

    ld-chroma-decoder - Colourisation filter for ld-decode
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

#ifndef DECODERPOOL_H
#define DECODERPOOL_H

#include <QObject>
#include <QAtomicInt>
#include <QByteArray>
#include <QElapsedTimer>
#include <QMap>
#include <QMutex>
#include <QThread>

#include "lddecodemetadata.h"
#include "sourcevideo.h"

#include "decoder.h"

class DecoderPool : public QObject
{
    Q_OBJECT
public:
    explicit DecoderPool(Decoder &decoderParam, QString inputFileNameParam,
                         LdDecodeMetaData &ldDecodeMetaDataParam, QString outputFileNameParam,
                         qint32 startFrameParam, qint32 lengthParam, qint32 maxThreadsParam,
                         QObject *parent = nullptr);
    bool process();

    // Member functions used by worker threads
    bool getInputFrame(qint32& frameNumber, QByteArray& firstFieldData, QByteArray& secondFieldData,
                       qint32& firstFieldPhaseID, qint32& secondFieldPhaseID, qreal& burstMedianIre);
    bool putOutputFrame(qint32 frameNumber, QByteArray& rgbOutput);

signals:

public slots:

private slots:

private:
    // Parameters
    Decoder& decoder;
    QString inputFileName;
    QString outputFileName;
    qint32 startFrame;
    qint32 length;
    qint32 maxThreads;

    // Atomic abort flag shared by worker threads; workers watch this, and shut
    // down as soon as possible if it becomes true
    QAtomicInt abort;

    // Input stream information (all guarded by inputMutex while threads are running)
    QMutex inputMutex;
    qint32 inputFrameNumber;
    qint32 lastFrameNumber;
    LdDecodeMetaData &ldDecodeMetaData;
    SourceVideo sourceVideo;

    // Output stream information (all guarded by outputMutex while threads are running)
    QMutex outputMutex;
    qint32 outputFrameNumber;
    QMap<qint32, QByteArray> pendingOutputFrames;
    QFile targetVideo;
    QElapsedTimer totalTimer;
};

#endif // DECODERPOOL_H