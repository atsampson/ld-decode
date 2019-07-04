/************************************************************************

    decoder.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
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

#include "decoder.h"

void Decoder::setVideoParameters(Decoder::Configuration &config, const LdDecodeMetaData::VideoParameters &videoParameters,
                                 qint32 firstActiveScanLine, qint32 lastActiveScanLine) {

    config.videoParameters = videoParameters;
    config.firstActiveScanLine = firstActiveScanLine;
    config.lastActiveScanLine = lastActiveScanLine;
    config.topPadLines = 0;
    config.bottomPadLines = 0;

    // Both width and height should be divisible by 8, as video codecs expect this.
    // Expand horizontal active region so the width is divisible by 8.
    qint32 outputWidth;
    while (true) {
        outputWidth = config.videoParameters.activeVideoEnd - config.videoParameters.activeVideoStart;
        if ((outputWidth % 8) == 0) {
            break;
        }

        // Add pixels to the right and left sides in turn, to keep the active area centred
        if ((outputWidth % 2) == 0) {
            config.videoParameters.activeVideoEnd++;
        } else {
            config.videoParameters.activeVideoStart--;
        }
    }

    // Insert empty padding lines so the height is divisible by 8
    qint32 outputHeight;
    while (true) {
        outputHeight = config.topPadLines + (config.lastActiveScanLine - config.firstActiveScanLine) + config.bottomPadLines;
        if ((outputHeight % 8) == 0) {
            break;
        }

        // Add lines to the bottom and top in turn, to keep the active area centred
        if ((outputHeight % 2) == 0) {
            config.bottomPadLines++;
        } else {
            config.topPadLines++;
        }
    }

    // Show output information to the user
    const qint32 frameHeight = (videoParameters.fieldHeight * 2) - 1;
    qInfo() << "Input video of" << config.videoParameters.fieldWidth << "x" << frameHeight <<
               "will be colourised and trimmed to" << outputWidth << "x" << outputHeight << "RGB 16-16-16 frames";
}

QByteArray Decoder::cropOutputFrame(const Decoder::Configuration &config,
                                    QByteArray evenFieldData, QByteArray oddFieldData) {
    const qint32 activeVideoStart = config.videoParameters.activeVideoStart;
    const qint32 activeVideoEnd = config.videoParameters.activeVideoEnd;
    const qint32 outputLineBytes = (activeVideoEnd - activeVideoStart) * 6;

    QByteArray croppedData;

    // Insert padding at the top
    if (config.topPadLines > 0) {
        croppedData.append(config.topPadLines * outputLineBytes, 0);
    }

    // Copy the active region from the decoded image
    for (qint32 y = config.firstActiveScanLine; y < config.lastActiveScanLine; y++) {
        const qint32 fieldLine = y / 2;
        const qint32 offset = (fieldLine * config.videoParameters.fieldWidth * 6) + (activeVideoStart * 6);
        if ((y % 2) == 0) {
            croppedData.append(evenFieldData.mid(offset, outputLineBytes));
        } else {
            croppedData.append(oddFieldData.mid(offset, outputLineBytes));
        }
    }

    // Insert padding at the bottom
    if (config.bottomPadLines > 0) {
        croppedData.append(config.bottomPadLines * outputLineBytes, 0);
    }

    return croppedData;
}
