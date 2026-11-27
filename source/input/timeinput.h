/*****************************************************************************
 * Copyright (C) 2026 MulticoreWare, Inc
 *
 * Authors: x265 contributors
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

#ifndef X265_TIMEINPUT_H
#define X265_TIMEINPUT_H

#include "common.h"
#include <cstdio>
#include <cstdint>

namespace X265_NS {
// x265 private namespace

class TimeInput
{
public:
    explicit TimeInput(const char* fname);
    virtual ~TimeInput();

    /* returns the reader for the format of fname. Check isFail() on the result. */
    static TimeInput* open(const char* fname);

    virtual const char *getName() const = 0;
    bool isFail() const { return !ifs; }

    /* Required configuration step to produce meaningful timing information.
     * Returns false if the reader failed or the timescale is invalid. */
    bool setTimescale(uint32_t timescaleNum, uint32_t timescaleDenom);

    /* Public API to get the duration of a frame.
     * Returns sourcePoc's duration in VUI clock ticks. POC must be monotonic. */
    uint32_t getFrameTicksDuration(const uint32_t sourcePoc) { return readNext(sourcePoc); }

protected:
    /* parse the file header, return the format version or <= 0 on failure */
    virtual int readHeader() = 0;
    /* find the duration of sourcePoc */
    virtual uint32_t readNext(uint32_t sourcePoc) = 0;

    static void reduceFraction(uint64_t& n, uint64_t& d);

    FILE *ifs;
    int version;

    /* bitstream timescale (VUI time_scale / num_units_in_tick) */
    uint64_t vuiTimescaleNum;
    uint64_t vuiTimescaleDenom;

private:
    /* format specific setup once the VUI timescale is known */
    virtual bool finalizeSetTimescale() = 0;
};

/* Reads an x265 timescale file: optional "# timescale format v1" header, then
 * one "<poc> <fps>" entry per line, where <fps> is either an integer or a fraction "num/denom".
 * A rate applies from its POC onwards until the next entry.
 * Has absolute precision given the timebase, in comparison to Matroska timestamps files. */
class TimescaleInput : public TimeInput
{
public:
    explicit TimescaleInput(const char* fname);

    const char *getName() const override { return "timescale"; }

protected:
    int readHeader() override;
    uint32_t readNext(uint32_t sourcePoc) override;

private:
    bool finalizeSetTimescale() override;

    void readNextRate(uint32_t sourcePoc);

    static int64_t rescaleError(int64_t err, int64_t oldDen, int64_t newDen);

    /* rounding residual, in units of 1/rateRatioDenom clock ticks */
    int64_t errorInTicks;

    /* latest frame rate parsed in the timescale file, for reference */
    uint64_t rateNum;
    uint64_t rateDenom;

    /* cached fps / vuiTimescale for error computation within a section */
    int64_t rateRatioNum;
    int64_t rateRatioDenom;
};

/* Reads a Matroska timestamp v2 file. It shall have a "# timestamp format v2" header, then
 * one "<pts>" entry per line, <pts> in millisecond, either an integer or a double.
 * if the file is consumed while there are still frames, the last valid duration is carried. */
class TimestampInput : public TimeInput
{
public:
    explicit TimestampInput(const char* fname);

    const char *getName() const override { return "timestamp"; }

protected:
    int readHeader() override;
    uint32_t readNext(uint32_t sourcePoc) override;

private:
    bool finalizeSetTimescale() override;

    double readNextTimestamp();

    double vuiTimebase;

    double errorInSecs;
    double timeShiftSecs;

    double lastTimestamp;
    double lastDuration;
};

}

#endif // ifndef X265_TIMEINPUT_H
