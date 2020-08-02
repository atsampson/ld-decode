/************************************************************************

    yiq.h

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

#ifndef YIQ_H
#define YIQ_H

#include <QCoreApplication>

class YIQ
{
public:
    double y, i, q;

    YIQ(double y = 0.0, double i = 0.0, double q = 0.0);
    YIQ operator*=(double x) const;
    YIQ operator+=(const YIQ &p) const;

private:

};

#endif // YIQ_H
