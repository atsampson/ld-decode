/************************************************************************

    zvbi.cpp

    ld-export-metadata - Export JSON metadata into other formats
    Copyright (C) 2019-2020 Adam Sampson

    This file is part of ld-decode-tools.

    ld-export-metadata is free software: you can redistribute it and/or
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

#include "zvbi.h"

#include <QtGlobal>
#include <QVector>
#include <fstream>
extern "C" {
#include <libzvbi.h>
}

bool writeZvbiSliced(LdDecodeMetaData &metaData, const QString &fileName)
{
    const auto videoParameters = metaData.getVideoParameters();

    // Open the output file
    std::ofstream file(fileName.toStdString(), std::ios::binary);
    if (file.fail()) {
        qDebug("writeZvbiSliced: Could not open file for output");
        return false;
    }

    // ZVBI is frame-oriented, so iterate through the frames in the input
    const qint32 numFrames = metaData.getNumberOfFrames();
    double lastFrameTime = 0.0;
    for (qint32 frameNumber = 1; frameNumber <= numFrames; frameNumber++) {
        const qint32 firstFieldNumber = metaData.getFirstFieldNumber(frameNumber);
        const qint32 secondFieldNumber = metaData.getSecondFieldNumber(frameNumber);

        // Collect the VBI from both fields
        QVector<LdDecodeMetaData::SlicedVbi> lines;
        lines.append(metaData.getFieldSlicedVbi(firstFieldNumber));
        lines.append(metaData.getFieldSlicedVbi(secondFieldNumber));

        // If we didn't collect any, nothing to do
        if (lines.empty()) {
            continue;
        }

        // The time in seconds since the last frame (newline-terminated string!)
        double frameTime = (frameNumber - 1) / (videoParameters.isSourcePal ? 25.0 : (3000.0 / 1001.0));
        QString timeDelta;
        timeDelta.setNum(frameTime - lastFrameTime);
        timeDelta.append("\n");
        file << timeDelta.toStdString();
        lastFrameTime = frameTime;

        // The number of lines (one byte)
        quint8 numLines = lines.size();
        file.write(reinterpret_cast<const char *>(&numLines), 1);

        for (const auto &line: lines) {
            quint8 header[3];

            // The service index (one byte)
            if (line.id == VBI_SLICED_TELETEXT_B) {
                header[0] = 0;
            } else if (line.id == VBI_SLICED_CAPTION_525) {
                header[0] = 7;
            } else {
                qCritical() << "writeZvbiSliced: Unknown sliced VBI line id:" << line.id;
                return false;
            }

            // The line number (two bytes, little-endian)
            header[1] = line.line & 0xFF;
            header[2] = (line.line >> 8) & 0xFF;

            // Write the header and data
            file.write(reinterpret_cast<const char *>(header), sizeof header);
            file.write(reinterpret_cast<const char *>(line.data.data()), line.data.size());
        }
    }

    // Close the file and check writing succeeded
    file.close();
    if (file.fail()) {
        qDebug("writeZvbiSliced: Writing to output file failed");
        return false;
    }

    return true;
}

bool writeT42(LdDecodeMetaData &metaData, const QString &fileName)
{
    // Open the output file
    std::ofstream file(fileName.toStdString(), std::ios::binary);
    if (file.fail()) {
        qDebug("writeT42: Could not open file for output");
        return false;
    }

    // Iterate through the fields
    qint32 numFields = metaData.getNumberOfFields();
    for (qint32 fieldNumber = 1; fieldNumber <= numFields; fieldNumber++) {
        // Find Teletext lines in this field
        for (const auto &line: metaData.getFieldSlicedVbi(fieldNumber)) {
            if (line.id != VBI_SLICED_TELETEXT_B) {
                // Not Teletext
                continue;
            }

            // Write the data
            file.write(reinterpret_cast<const char *>(line.data.data()), line.data.size());
        }
    }

    // Close the file and check writing succeeded
    file.close();
    if (file.fail()) {
        qDebug("writeT42: Writing to output file failed");
        return false;
    }

    return true;
}
