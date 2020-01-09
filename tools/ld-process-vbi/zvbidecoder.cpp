/************************************************************************

    zvbi.cpp

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

#include "zvbidecoder.h"

#include "vbilinedecoder.h"

#include <QtDebug>
#include <cstring>

ZvbiDecoder::~ZvbiDecoder()
{
    // Free the decoder, if allocated
    if (decoder != nullptr) {
        vbi_raw_decoder_destroy(decoder);
        delete decoder;
    }
}

// Process one field using ZVBI, writing the results into fieldMetadata
void ZvbiDecoder::process(const QByteArray &fieldData, LdDecodeMetaData::Field &fieldMetadata,
                          const LdDecodeMetaData::VideoParameters &videoParameters)
{
    // The number of lines in the (partial) field
    constexpr qint32 numLines = (VbiLineDecoder::endFieldLine - VbiLineDecoder::startFieldLine) + 1;

    // The number of samples in the (partial) field
    const qint32 numSamples = videoParameters.fieldWidth * numLines;

    // Initialise the decoder if we haven't already done so
    if (decoder == nullptr) {
        // Construct the decoder
        decoder = new vbi_raw_decoder;
        vbi_raw_decoder_init(decoder);

        // Set the decoder's input parameters.
        //
        // To enable Teletext decoding, ZVBI wants to know that it has access
        // to two fields' worth of input data, but it doesn't mind if only some
        // of the lines are filled in -- so we construct a double-size buffer,
        // and only fill half of it depending on which field we've got right
        // now.
        //
        // ZVBI 0.2 doesn't support 16bpp monochrome input, but it does support
        // YUV420p (ignoring the chroma samples), so we convert to 8bpp.
        decoder->scanning = videoParameters.isSourcePal ? 625 : 525;
        decoder->sampling_format = VBI_PIXFMT_YUV420;
        decoder->sampling_rate = videoParameters.sampleRate;
        decoder->bytes_per_line = videoParameters.fieldWidth;
        decoder->offset = 0;
        decoder->start[0] = VbiLineDecoder::startFieldLine;
        decoder->count[0] = numLines;
        decoder->start[1] = VbiLineDecoder::startFieldLine + (videoParameters.isSourcePal ? 312 : 263);
        decoder->count[1] = numLines;
        decoder->interlaced = FALSE;
        decoder->synchronous = TRUE;

        // Enable the services we want
        // XXX WSS?
        unsigned int wantServices;
        if (videoParameters.isSourcePal) {
            wantServices = VBI_SLICED_TELETEXT_B_625;
        } else {
            wantServices = VBI_SLICED_CAPTION_525;
        }
        unsigned int enabledServices = vbi_raw_decoder_add_services(decoder, wantServices, 1);
        if (enabledServices != wantServices) {
            qWarning() << "ZvbiDecoder::process(): Tried to enable services" << wantServices << "but only managed" << enabledServices;
        }

        // Resize the buffer
        linesBuf.resize(2 * numSamples);
    }

    // Copy the field into the appropriate half of linesBuf
    // XXX Is it worth oversampling using a better filter here? (ZVBI only does linear interpolation)
    linesBuf.fill(0);
    quint8 *outPtr = linesBuf.data();
    if (!fieldMetadata.isFirstField) {
        outPtr += numSamples;
    }
    const quint16 *inPtr = reinterpret_cast<const quint16 *>(fieldData.data());
    for (qint32 i = 0; i < numSamples; i++) {
        // Exclude values outside the black-white range (e.g. sync pulses), to
        // avoid confusing the slicer's automatic level adjustment
        quint16 value = qBound(quint16(videoParameters.black16bIre), *inPtr++, quint16(videoParameters.white16bIre));
        *outPtr++ = static_cast<quint8>(value >> 8);
    }

    // Decode the field
    vbi_sliced sliced[numLines];
    qint32 numSliced = vbi_raw_decode(decoder, reinterpret_cast<uint8_t *>(linesBuf.data()), sliced);
    qDebug() << "ZvbiDecoder::process(): Decoded" << numSliced << "sliced lines from" << (fieldMetadata.isFirstField ? "first" : "second") << "field";

    // Convert the vbi_sliced structs into SlicedVbi objects, and store in the metadata
    fieldMetadata.slicedVbi.clear();
    for (qint32 i = 0; i < numSliced; i++) {
        LdDecodeMetaData::SlicedVbi slicedVbi;

        slicedVbi.id = sliced[i].id;
        slicedVbi.line = sliced[i].line;
        int dataBytes = (vbi_sliced_payload_bits(sliced[i].id) + 7) / 8;
        slicedVbi.data.clear();
        slicedVbi.data.append(reinterpret_cast<const char *>(sliced[i].data), dataBytes);

        fieldMetadata.slicedVbi.push_back(slicedVbi);
    }
}
