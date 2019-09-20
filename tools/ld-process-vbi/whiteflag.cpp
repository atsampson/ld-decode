/************************************************************************

    whiteflag.h

    ld-process-vbi - VBI and IEC NTSC specific processor for ld-decode
    Copyright (C) 2018-2019 Simon Inns

    This file is part of ld-decode-tools.

    ld-process-vbi is free software: you can redistribute it and/or
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

#include "whiteflag.h"

WhiteFlag::WhiteFlag(QObject *parent) : QObject(parent)
{

}

// Public method to read the white flag status from a field-line
bool WhiteFlag::getWhiteFlag(const quint16 *lineData, qint32 lineWidth, qint32 zcPoint)
{
    qint32 whiteCount = 0;
    for (qint32 x = 0; x < lineWidth; x++) {
        qint32 pixelValue = static_cast<qint32>(lineData[x]);
        if (pixelValue > zcPoint) whiteCount++;
    }

    // Mark the line as a white flag if at least 50% of the data is above the zc point
    if (whiteCount > (lineWidth / 2)) {
        qDebug() << "WhiteFlag::getWhiteFlag(): White-flag detected: White count was" << whiteCount << "out of" << lineWidth;
        return true;
    }

    // Not a white flag
    return false;
}
