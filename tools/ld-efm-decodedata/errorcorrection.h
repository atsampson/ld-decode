/************************************************************************

    errorcorrection.h

    ld-efm-decodedata - EFM data decoder for ld-decode
    Copyright (C) 2019 Simon Inns

    This file is part of ld-decode-tools.

    ld-efm-decodedata is free software: you can redistribute it and/or
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

#ifndef ERRORCORRECTION_H
#define ERRORCORRECTION_H

#include <QCoreApplication>
#include <QDebug>

// Include the rscode C library
extern "C" {
  #include "rscode-1.3/ecc.h"
}

class ErrorCorrection
{
public:
    ErrorCorrection();
    void checkP(qint32 data[]);
    void checkQ(qint32 data[]);

private:
    void test(void);
    void addByteError(int err, int loc, unsigned char *dst);
};

#endif // ERRORCORRECTION_H
