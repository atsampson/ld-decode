/************************************************************************

    opticalflow.cpp

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

#include "opticalflow.h"

OpticalFlow::OpticalFlow()
{
    fieldsProcessed = 0;
}

// Perform a dense optical flow analysis
// Input is a vector of 16-bit Y values for the NTSC field (910x263)
void OpticalFlow::denseOpticalFlow(const YiqBuffer &yiqBuffer, QVector<qreal> &kValues)
{
    // Convert the buffer of Y values into an OpenCV n-dimensional dense array (cv::Mat)
    cv::Mat currentFieldGrey = convertYtoMat(yiqBuffer);
    cv::Mat flow;

    kValues.resize(910 * 263);

    // We need to compare with the last-but-one field.
    // If we have no previous image, simply copy the current to previous (and set all K values to 1 for 2D)
    if (fieldsProcessed >= 2) {
        // Perform the OpenCV compute dense optical flow (Gunnar Farneback’s algorithm)
        cv::calcOpticalFlowFarneback(previousPreviousFieldGrey, currentFieldGrey, flow, 0.5, 4, 2, 3, 7, 1.5, 0);

        // Apply a wide blur to the flow map to prevent the 3D filter from acting on small spots of the image;
        // also helps a lot with sharp scene transitions and still-frame images due to the averaging effect
        // on pixel velocity.
        cv::GaussianBlur(flow, flow, cv::Size(21, 21), 0);

        // Convert to K values
        for (qint32 y = 0; y < 263; y++) {
            for (qint32 x = 0; x < 910; x++) {
                // Get the flow velocity at the current x, y point
                const cv::Point2f flowatxy = flow.at<cv::Point2f>(y, x);

                // Calculate the difference between x and y to get the relative velocity (in any direction)
                // Because we're working with a single field, the motion detection is inherently twice as
                // sensitive in the X direction than the Y
                qreal velocity = calculateDistance(static_cast<qreal>(flowatxy.y), static_cast<qreal>(flowatxy.x));

                kValues[(910 * y) + x] = clamp(velocity, 0.0, 1.0);
            }
        }
    } else kValues.fill(1);

    // Copy the current field to the previous field
    previousFieldGrey.copyTo(previousPreviousFieldGrey);
    currentFieldGrey.copyTo(previousFieldGrey);

    fieldsProcessed++;
}

// Method to convert a qreal vector field of Y values to an OpenCV n-dimensional dense array (cv::Mat)
cv::Mat OpticalFlow::convertYtoMat(const YiqBuffer &yiqBuffer)
{
    quint16 field[910 * 263];
    memset(field, 0, sizeof(field));

    // Firstly we have to convert the Y vector of real numbers into quint16 values for OpenCV
    for (qint32 line = 0; line < 263; line++) {
        for (qint32 pixel = 0; pixel < 910; pixel++) {
            field[(line * 910) + pixel] = static_cast<quint16>(yiqBuffer[line][pixel].y);
        }
    }

    // Return a Mat y * x in CV_16UC1 format
    return cv::Mat(263, 910, CV_16UC1, field).clone();
}

// This method calculates the distance between points where x is the difference between the x-coordinates
// and y is the difference between the y coordinates
qreal OpticalFlow::calculateDistance(qreal yDifference, qreal xDifference)
{
    return sqrt((yDifference * yDifference) + (xDifference * xDifference));
}
