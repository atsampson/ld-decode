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
    framesProcessed = 0;
}

// Perform a dense optical flow analysis
// Input is a vector of 16-bit Y values for the NTSC frame
void OpticalFlow::denseOpticalFlow(const YiqBuffer &yiqBuffer, QVector<qreal> &kValues)
{
    const qint32 width = yiqBuffer[0].size();
    const qint32 height = yiqBuffer.size();

    // Convert the buffer of Y values into an OpenCV n-dimensional dense array (cv::Mat)
    cv::Mat currentFrameGrey = convertYtoMat(yiqBuffer);
    cv::Mat flow;

    kValues.resize(width * height);

    // If we have no previous image, simply copy the current to previous (and set all K values to 1 for 2D)
    if (framesProcessed > 0) {
        // Perform the OpenCV compute dense optical flow (Gunnar Farneback’s algorithm)
        cv::calcOpticalFlowFarneback(previousFrameGrey, currentFrameGrey, flow, 0.5, 4, 2, 3, 7, 1.5, 0);

        // Apply a wide blur to the flow map to prevent the 3D filter from acting on small spots of the image;
        // also helps a lot with sharp scene transitions and still-frame images due to the averaging effect
        // on pixel velocity.
        cv::GaussianBlur(flow, flow, cv::Size(21, 21), 0);

        // Convert to K values
        for (qint32 y = 0; y < height; y++) {
            for (qint32 x = 0; x < width; x++) {
                // Get the flow velocity at the current x, y point
                const cv::Point2f flowatxy = flow.at<cv::Point2f>(y, x);

                // Calculate the difference between x and y to get the relative velocity (in any direction)
                // We multiply the x velocity by 2 in order to make the motion detection twice as sensitive
                // in the X direction than the y
                qreal velocity = calculateDistance(static_cast<qreal>(flowatxy.y), static_cast<qreal>(flowatxy.x) * 2);

                kValues[(y * width) + x] = clamp(velocity, 0.0, 1.0);
            }
        }
    } else kValues.fill(1);

    // Copy the current frame to the previous frame
    currentFrameGrey.copyTo(previousFrameGrey);

    framesProcessed++;
}

// Method to convert a qreal vector frame of Y values to an OpenCV n-dimensional dense array (cv::Mat)
cv::Mat OpticalFlow::convertYtoMat(const YiqBuffer &yiqBuffer)
{
    const qint32 width = yiqBuffer[0].size();
    const qint32 height = yiqBuffer.size();

    // XXX VLA is C++14
    quint16 frame[width * height];

    // Firstly we have to convert the Y vector of real numbers into quint8 values for OpenCV
    for (qint32 line = 0; line < height; line++) {
        for (qint32 pixel = 0; pixel < width; pixel++) {
            frame[(line * width) + pixel] = static_cast<quint16>(yiqBuffer[line][pixel].y);
        }
    }

    // Return a Mat y * x in CV_16UC1 format
    return cv::Mat(height, width, CV_16UC1, frame).clone();
}

// This method calculates the distance between points where x is the difference between the x-coordinates
// and y is the difference between the y coordinates
qreal OpticalFlow::calculateDistance(qreal yDifference, qreal xDifference)
{
    return sqrt((yDifference * yDifference) + (xDifference * xDifference));
}
