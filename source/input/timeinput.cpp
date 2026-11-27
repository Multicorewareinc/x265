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

#include "common.h"
#include "timeinput.h"

#include <iostream>
#include <cmath>
#include <cstring>
#include <climits>
#include <cstdlib>
#include <cctype>

/* prevent excessive numerators and denominators */
#define MAX_NUM_DEN_VAL (MAX_FRAME_RATE * 1000)

#define MKV_TIMECODE_HEADER_FMT  "# timecode format v"
#define MKV_TIMESTAMP_HEADER_FMT "# timestamp format v"

#if MAX_NUM_DEN_VAL > (1u << 20)
# error "risk of overflow in error scaling"
#endif

using namespace X265_NS;
using namespace std;

TimeInput* TimeInput::open(const char* fname)
{
    char line[384] = "\x0";
    const char *ret = NULL;
    FILE* f = strcmp(fname, "-") ? x265_fopen(fname, "rb") : NULL;
    if (f)
    {
        ret = fgets(line, sizeof(line), f);
        fclose(f);
    }
    if (!ret)
        return NULL;
    if (!memcmp(line, MKV_TIMESTAMP_HEADER_FMT, 11) || !memcmp(line, MKV_TIMECODE_HEADER_FMT, 10))
        return new TimestampInput(fname);
    return new TimescaleInput(fname);
}

TimeInput::TimeInput(const char* fname)
{
    this->ifs = NULL;
    this->version = 0;
    this->vuiTimescaleNum = 0;
    this->vuiTimescaleDenom = 0;

    if (!strcmp(fname, "-"))
    {
        x265_log(NULL, X265_LOG_ERROR, "timeinput: pipe for timing information not supported.\n");
        return;
    }
    this->ifs = x265_fopen(fname, "rb");
}

TimeInput::~TimeInput()
{
    if (this->ifs)
        fclose(this->ifs);
}

bool TimeInput::setTimescale(uint32_t timescaleNum, uint32_t timescaleDenom)
{
    if (this->isFail())
        return false;

    if (!timescaleNum || !timescaleDenom)
    {
        x265_log(NULL, X265_LOG_ERROR, "%s: invalid bitstream timescale %u/%u.\n",
                 this->getName(), timescaleNum, timescaleDenom);
        fclose(this->ifs);
        this->ifs = NULL;
        return false;
    }
    this->vuiTimescaleNum = timescaleNum;
    this->vuiTimescaleDenom = timescaleDenom;

    reduceFraction(this->vuiTimescaleNum, this->vuiTimescaleDenom);
    return this->finalizeSetTimescale();
}

void TimeInput::reduceFraction(uint64_t& n, uint64_t& d)
{
    if (!d || !n)
        return;

    uint64_t a = n, b = d;
    while (b > 0)
    {
        uint64_t c = b;
        b = a % b;
        a = c;
    }

    n /= a;
    d /= a;
}

/* x265 timescale file
 * specify a first frame POC of applicability, and the FPS. Every line shall be:
 * <poc> <fpsNum/fpsDen|integer>
 * The last specified framerate applies up to the next frame rate.
 * I.e poc such that poc_m <= poc < poc_n uses the framerate specified at poc_m.
 * The default framerate is the VUI timescale. */
TimescaleInput::TimescaleInput(const char* fname)
    : TimeInput(fname)
{
    this->errorInTicks = 0;
    this->rateRatioNum = 1;
    this->rateRatioDenom = 1;

    this->rateNum   = this->vuiTimescaleNum = 0;
    this->rateDenom = this->vuiTimescaleDenom = 0;

    if (!this->isFail())
        this->version = this->readHeader();
}

bool TimescaleInput::finalizeSetTimescale()
{
    this->rateNum   = this->vuiTimescaleNum;
    this->rateDenom = this->vuiTimescaleDenom;
    return true;
}

int TimescaleInput::readHeader()
{
    char buff[384];
    version = 0;

    fseek(ifs, 0, SEEK_SET);
    if (fgets(buff, sizeof(buff), ifs))
    {
        if (sscanf(buff, "# timescale format v%d", &version) == 1 && version != 1)
        {
            x265_log(NULL, X265_LOG_ERROR, "timescale: format v%d not supported.\n", version);
            version = 0;
        }
        else if (version != 1)
        {
            /* if not provided assume v1 and reset file progress */
            fseek(ifs, 0, SEEK_SET);
            version = 1;
        }
    }
    else
    {
        x265_log(NULL, X265_LOG_ERROR, "timescale: Cannot read file.\n", version);
    }

    if (version <= 0)
    {
        /* failed to parse, make it null so isFail returns true */
        fclose(ifs);
        ifs = NULL;
    }
    return version;
}

void TimescaleInput::readNextRate(uint32_t sourcePoc)
{
    if (!ifs)
        return;

    char buff[384];
    bool bChanged = false;
    uint64_t newNum = rateNum, newDenom = rateDenom;

    while (1)
    {
        long filePos = ftell(ifs);
        if (!fgets(buff, sizeof(buff), ifs))
            break;

        int c = 0;
        if (!strchr(buff, '\n') && !feof(ifs))
        {
            while ((c = fgetc(ifs)) != '\0')
            {
                if (c == '\n' || c == EOF)
                    break;
            }
        }

        uint32_t poc;
        uint32_t fpsNum, fpsDen = 1;

        /* blank lines and '#' comments are skipped silently */
        const char *line = buff;
        while (isspace((unsigned char)*line))
            line++;
        if (!*line || *line == '#')
            continue;

        /* "poc num" or "poc num/den" */
        int n = 0;
        if (sscanf(line, "%u %u%n", &poc, &fpsNum, &n) < 2)
        {
            x265_log(NULL, X265_LOG_WARNING, "timescale: cannot parse line: %s", buff);
            continue;
        }
        const char *rest = line + n;
        if (*rest == '/')
        {
            n = 0;
            if (sscanf(++rest, "%u%n", &fpsDen, &n) < 1)
                fpsDen = 0; /* rejected below */
            rest += n;
        }

        while (isspace((unsigned char)*rest))
            rest++;
        if (*rest)
        {
            x265_log(NULL, X265_LOG_WARNING, "timescale: cannot parse line: %s", buff);
            continue;
        }

        if (poc > sourcePoc)
        {
            /* entry for a future frame: rewind so it is read again later */
            fseek(ifs, filePos, SEEK_SET);
            break;
        }

        if (!fpsNum || !fpsDen || fpsNum > MAX_NUM_DEN_VAL || fpsDen > MAX_NUM_DEN_VAL)
        {
            x265_log(NULL, X265_LOG_WARNING, "timescale: invalid rate %u/%u at poc %u, ignored\n",
                     fpsNum, fpsDen, poc);
            continue;
        }

        /* guard against zero-tick (current fps cannot exceed timebase) */
        if ((uint64_t)fpsNum * vuiTimescaleDenom > vuiTimescaleNum * (uint64_t)fpsDen)
        {
            x265_log(NULL, X265_LOG_WARNING,
                     "timescale: rate %u/%u at poc %u exceeds bitstream %llu/%llu, clamped\n",
                     fpsNum, fpsDen, poc, (uint64_t)vuiTimescaleNum, (uint64_t)vuiTimescaleDenom);
            newNum = vuiTimescaleNum;
            newDenom = vuiTimescaleDenom;
        }
        else
        {
            newNum = fpsNum;
            newDenom = fpsDen;
        }
        /* it's tempting to break but if there's a gap
         * in sourcePoc then we need to continue */
        bChanged = true;
    }

    if (bChanged)
    {
        reduceFraction(newNum, newDenom);
        rateNum = newNum;
        rateDenom = newDenom;

        uint64_t a = vuiTimescaleNum * rateDenom;
        uint64_t b = vuiTimescaleDenom * rateNum;
        reduceFraction(a, b);

        errorInTicks = rescaleError(errorInTicks, rateRatioDenom, (int64_t)b);
        rateRatioNum = (int64_t)a;
        rateRatioDenom = (int64_t)b;
    }
}

int64_t TimescaleInput::rescaleError(int64_t err, int64_t oldDen, int64_t newDen)
{
    if (!err || oldDen == newDen)
        return err;

    /* round away from zero */
    int64_t p = err * newDen, r;
    r = (p >= 0 ? (p + oldDen / 2) : (p - oldDen / 2)) / oldDen;

    /* clamp to +/- half a tick away to avoid zero duration */
    return x265_clip3<int64_t>(-(newDen >> 1), newDen - (newDen >> 1) - 1, r);
}

uint32_t TimescaleInput::readNext(uint32_t sourcePoc)
{
    this->readNextRate(sourcePoc);

    int64_t frameDuration = (rateRatioNum + errorInTicks + (rateRatioDenom >> 1)) / rateRatioDenom;
    errorInTicks += rateRatioNum - frameDuration * rateRatioDenom;

    /* if frame duration exceeds UINT_MAX ticks then something is very wrong: caller should handle this */
    return (uint32_t)X265_MIN(UINT_MAX, frameDuration);
}

/* MKV Timecode or Timestamp file format
 * specify a duration, in millisecond, for every frame, every line:
 * <integer|float>
 * if the file is truncated, the last duration is carried. */
TimestampInput::TimestampInput(const char* fname)
    : TimeInput(fname)
{
    this->errorInSecs = 0.;
    this->timeShiftSecs = 0.;
    this->lastDuration = 0.;
    this->vuiTimebase = 0.;

    /* this->lastTimestamp is set in readHeader */
    if (!this->isFail())
        this->version = this->readHeader();
}

int TimestampInput::readHeader()
{
    char buff[384];
    version = 0;

    fseek(ifs, 0, SEEK_SET);
    if (fgets(buff, sizeof(buff), ifs))
    {
        if ((sscanf(buff, MKV_TIMESTAMP_HEADER_FMT "%d", &version) != 1 &&
             sscanf(buff, MKV_TIMECODE_HEADER_FMT "%d",  &version) != 1) || version != 2)
        {
            x265_log(NULL, X265_LOG_ERROR, "timestamp: format v%d not supported.\n", version);
            version = 0;
        }
    }
    else
    {
        x265_log(NULL, X265_LOG_ERROR, "timestamp: Cannot read file.\n");
    }

    if (version <= 0)
    {
        /* failed to parse, make it null so isFail returns true */
        fclose(ifs);
        ifs = NULL;
    }
    else
    {
        /* timestamps v2 files always read one ahead to get the current duration  */
        lastTimestamp = readNextTimestamp() / 1000.;
    }
    return version;
}

double TimestampInput::readNextTimestamp()
{
    char buff[384];
    double ptsMs = 0;

    while (1)
    {
        if (!fgets(buff, sizeof(buff), ifs))
            break;
        int c = 0;
        if (!strchr(buff, '\n') && !feof(ifs))
        {
            while ((c = fgetc(ifs)) != '\0')
            {
                if (c == '\n' || c == EOF)
                    break;
            }
        }

        const char *line = buff;
        while (isspace((unsigned char)*line))
            line++;
        if (!*line || *line == '#')
            continue;

        if (sscanf(line, "%lf", &ptsMs) == 1 && ptsMs >= 0)
            break;
        x265_log(NULL, X265_LOG_WARNING, "timestamp: skipped unparsable line: %s", line);
    }
    return ptsMs;
}

uint32_t TimestampInput::readNext(uint32_t sourcePoc)
{
    double nextPts = readNextTimestamp() / 1000.0;
    nextPts = nextPts > 0 ? (nextPts + timeShiftSecs) : (lastTimestamp + lastDuration);

    double expectedDuration = nextPts - lastTimestamp;
    double duration = expectedDuration + errorInSecs;

    /* give leeway due to float approximations */
    if (duration < 0.95*vuiTimebase)
    {
        x265_log(NULL, X265_LOG_ERROR, "timestamp: duration of frame %u smaller than bitstream timebase %.3f < %.3f ms, clamping.\n",
                 sourcePoc, duration*1000, vuiTimebase*1000);

        /* push the entire timeline by the remainder, else the error could happen for the rest of the encode. */
        double shift = vuiTimebase - duration;
        timeShiftSecs += shift;
        nextPts += shift;
        duration = vuiTimebase;
    }

    uint32_t durationTicks = X265_MAX(1, (uint32_t)((duration / vuiTimebase) + 0.5));
    errorInSecs = duration - (durationTicks * vuiTimebase);
    /* store exact value if the timestamp file ends early so the tick error can be
     * corrected every other frame and the frame rate be guaranteed */
    lastDuration = expectedDuration;
    lastTimestamp = nextPts;

    /* if frame duration exceeds UINT_MAX ticks then something is very wrong: caller should handle this */
    return (uint32_t)X265_MIN(UINT_MAX, durationTicks);
}


bool TimestampInput::finalizeSetTimescale()
{
    this->vuiTimebase = (double)this->vuiTimescaleDenom / this->vuiTimescaleNum;
    this->lastDuration = this->vuiTimebase; /* arbitrary default */
    return true;
}
