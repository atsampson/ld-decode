/************************************************************************

    zvbi.h

    ld-process-vbi - VBI and IEC NTSC specific processor for ld-decode
    Copyright (C) 2020 Adam Sampson

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

#ifndef ZVBI_H
#define ZVBI_H

#include "lddecodemetadata.h"

#include <QtGlobal>
#include <QByteArray>
#include <QVector>

extern "C" {
#include <libzvbi.h>
}

class ZvbiDecoder
{
public:
    ZvbiDecoder() = default;
    virtual ~ZvbiDecoder();

    // Don't allow copying or assignment
    ZvbiDecoder(const ZvbiDecoder &) = delete;
    ZvbiDecoder &operator=(const ZvbiDecoder &) = delete;

    void process(const QByteArray &fieldData, LdDecodeMetaData::Field &fieldMetadata,
                 const LdDecodeMetaData::VideoParameters &videoParameters);

private:
    // Decoder for both fields
    vbi_raw_decoder *decoder = nullptr;

    // Buffer for converted pixels
    QVector<quint8> linesBuf;
};

#endif // ZVBI_H
