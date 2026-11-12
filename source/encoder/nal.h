/*****************************************************************************
* Copyright (C) 2013-2020 MulticoreWare, Inc
*
* Authors: Steve Borho <steve@borho.org>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
*
* This program is also available under a commercial proprietary license.
* For more information, contact us at license @ x265.com.
*****************************************************************************/

#ifndef X265_NAL_H
#define X265_NAL_H

#include "common.h"
#include "x265.h"

namespace X265_NS {
// private namespace

class Bitstream;

class NALList
{
public:
#if ENABLE_MULTIVIEW || ENABLE_ALPHA
    static const int MAX_NAL_UNITS = 32;
#else
    static const int MAX_NAL_UNITS = 16;
#endif

public:

    x265_nal    m_nal[MAX_NAL_UNITS];
    uint32_t    m_numNal;

    uint8_t*    m_buffer;
    uint32_t    m_occupancy;
    uint32_t    m_allocSize;

    uint8_t*    m_extraBuffer;
    uint32_t    m_extraOccupancy;
    uint32_t    m_extraAllocSize;
    bool        m_annexB;

    NALList();
    ~NALList() { X265_FREE(m_buffer); X265_FREE(m_extraBuffer); }

    void takeContents(NALList& other);

    void serialize(NalUnitType nalUnitType, const Bitstream& bs, int layerId = 0, uint8_t temporalID = 1);

    uint32_t serializeSubstreams(uint32_t* streamSizeBytes, uint32_t streamCount, const Bitstream* streams);

    const x265_nal* begin() const { return m_nal; }
    const x265_nal* end() const   { return m_nal + m_numNal; }
    const x265_nal* back() const  { return m_numNal ? m_nal + m_numNal - 1 : NULL; }

    uint32_t getNaluBytesLength(const x265_nal& nal) const
    {
        const uint32_t startCodeLength = getStartCodeLength(nal);
        X265_CHECK(nal.sizeBytes > startCodeLength, "NALU payload < header\n");
        return nal.sizeBytes - startCodeLength;
    }

private:
    uint8_t getStartCodeLength(const x265_nal& nal) const { return (m_annexB && nal.payload[2] == 1) ? 3 : 4; }
    uint8_t getNextStartCodeLength(NalUnitType nalUnitType) const;
};

}

#endif // ifndef X265_NAL_H
