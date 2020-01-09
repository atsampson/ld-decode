/************************************************************************

    zvbi.h

    ld-export-metadata - Export JSON metadata into other formats
    Copyright (C) 2020 Adam Sampson

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

#ifndef ZVBI_H
#define ZVBI_H

#include <QString>

#include "lddecodemetadata.h"

/*!
    Write Teletext/CC data in ZVBI sliced format.

    This is the output format of zvbi-capture, and can be read by zvbi-export.
    For the (rather obscure) details of the format, see read_loop_old_sliced
    in test/sliced.c in the ZVBI source.

    Returns true on success, false on failure.
*/
bool writeZvbiSliced(LdDecodeMetaData &metaData, const QString &fileName);

/*!
    Write Teletext data in T42 format.

    This is the format used by vhs-teletext's tools -- a sequence of raw
    42-byte Teletext lines.

    Returns true on success, false on failure.
*/
bool writeT42(LdDecodeMetaData &metaData, const QString &fileName);

#endif
