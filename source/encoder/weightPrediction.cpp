/*****************************************************************************
 * Copyright (C) 2013-2020 MulticoreWare, Inc
 *
 * Author: Shazeb Nawaz Khan <shazeb@multicorewareinc.com>
 *         Steve Borho <steve@borho.org>
 *         Kavitha Sampas <kavitha@multicorewareinc.com>
 *         Min Chen <chenm003@163.com>
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
#include "frame.h"
#include "picyuv.h"
#include "lowres.h"
#include "slice.h"
#include "mv.h"
#include "bitstream.h"
#include "threading.h"

using namespace X265_NS;
namespace {
struct Cache
{
    const int * intraCost;
    int         numPredDir;
    int         csp;
    int         hshift;
    int         vshift;
    int         lowresWidthInCU;
    int         lowresHeightInCU;
};

int sliceHeaderCost(WeightParam *w, int lambda, int bChroma)
{
    /* 4 times higher, because chroma is analyzed at full resolution. */
    if (bChroma)
        lambda *= 4;
    int denomCost = bs_size_ue(w[0].log2WeightDenom) * (2 - bChroma);
    return lambda * (10 + denomCost + 2 * (bs_size_se(w[0].inputWeight) + bs_size_se(w[0].inputOffset)));
}

/* make a motion compensated copy of lowres ref into mcout with the same stride.
 * The borders of mcout are not extended */
void mcLuma(pixel* mcout, Lowres& ref, const MV * mvs)
{
    intptr_t stride = ref.lumaStride;
    const int mvshift = 1 << 2;
    const int cuSize = 8;
    MV mvmin, mvmax;

    int cu = 0;

    for (int y = 0; y < ref.lines; y += cuSize)
    {
        intptr_t pixoff = y * stride;
        mvmin.y = (int32_t)((-y - 8) * mvshift);
        mvmax.y = (int32_t)((ref.lines - y - 1 + 8) * mvshift);

        for (int x = 0; x < ref.width; x += cuSize, pixoff += cuSize, cu++)
        {
            ALIGN_VAR_16(pixel, buf8x8[8 * 8]);
            intptr_t bstride = 8;
            mvmin.x = (int32_t)((-x - 8) * mvshift);
            mvmax.x = (int32_t)((ref.width - x - 1 + 8) * mvshift);

            /* clip MV to available pixels */
            MV mv = mvs[cu];
            mv = mv.clipped(mvmin, mvmax);
            pixel *tmp = ref.lowresMC(pixoff, mv, buf8x8, bstride, 0);
            primitives.cu[BLOCK_8x8].copy_pp(mcout + pixoff, stride, tmp, bstride);
        }
    }
}

/* use lowres MVs from lookahead to generate a motion compensated chroma plane.
 * if a block had cheaper lowres cost as intra, we treat it as MV 0 */
void mcChroma(pixel *      mcout,
              pixel *      src,
              intptr_t     stride,
              const MV *   mvs,
              const Cache& cache,
              int          height,
              int          width)
{
    /* the motion vectors correspond to 8x8 lowres luma blocks, or 16x16 fullres
     * luma blocks. We have to adapt block size to chroma csp */
    int csp = cache.csp;
    int bw = 16 >> cache.hshift;
    int bh = 16 >> cache.vshift;
    const int mvshift = 1 << 2;
    MV mvmin, mvmax;

    for (int y = 0; y < height; y += bh)
    {
        /* note: lowres block count per row might be different from chroma block
         * count per row because of rounding issues, so be very careful with indexing
         * into the lowres structures */
        int cu = y * cache.lowresWidthInCU;
        intptr_t pixoff = y * stride;
        mvmin.y = (int32_t)((-y - 8) * mvshift);
        mvmax.y = (int32_t)((height - y - 1 + 8) * mvshift);

        for (int x = 0; x < width; x += bw, cu++, pixoff += bw)
        {
            if (x < cache.lowresWidthInCU && y < cache.lowresHeightInCU)
            {
                MV mv = mvs[cu]; // lowres MV
                mv <<= 1;        // fullres MV
                mv.x >>= cache.hshift;
                mv.y >>= cache.vshift;

                /* clip MV to available pixels */
                mvmin.x = (int32_t)((-x - 8) * mvshift);
                mvmax.x = (int32_t)((width - x - 1 + 8) * mvshift);
                mv = mv.clipped(mvmin, mvmax);

                intptr_t fpeloffset = (mv.y >> 2) * stride + (mv.x >> 2);
                pixel *temp = src + pixoff + fpeloffset;

                int xFrac = mv.x & 7;
                int yFrac = mv.y & 7;
                if (!(yFrac | xFrac))
                {
                    primitives.chroma[csp].pu[LUMA_16x16].copy_pp(mcout + pixoff, stride, temp, stride);
                }
                else if (!yFrac)
                {
                    primitives.chroma[csp].pu[LUMA_16x16].filter_hpp(temp, stride, mcout + pixoff, stride, xFrac);
                }
                else if (!xFrac)
                {
                    primitives.chroma[csp].pu[LUMA_16x16].filter_vpp(temp, stride, mcout + pixoff, stride, yFrac);
                }
                else
                {
                    ALIGN_VAR_16(int16_t, immed[16 * (16 + NTAPS_CHROMA - 1)]);
                    primitives.chroma[csp].pu[LUMA_16x16].filter_hps(temp, stride, immed, bw, xFrac, 1);
                    primitives.chroma[csp].pu[LUMA_16x16].filter_vsp(immed + ((NTAPS_CHROMA >> 1) - 1) * bw, bw, mcout + pixoff, stride, yFrac);
                }
            }
            else
            {
                primitives.chroma[csp].pu[LUMA_16x16].copy_pp(mcout + pixoff, stride, src + pixoff, stride);
            }
        }
    }
}

/* Measure sum of 8x8 satd costs between source frame and reference
 * frame (potentially weighted, potentially motion compensated). We
 * always use source images for this analysis since reference recon
 * pixels have unreliable availability */
uint32_t weightCost(pixel *         fenc,
                    pixel *         ref,
                    pixel *         weightTemp,
                    intptr_t        stride,
                    const Cache &   cache,
                    int             width,
                    int             height,
                    WeightParam *   w,
                    bool            bLuma)
{
    if (w)
    {
        /* make a weighted copy of the reference plane */
        int offset = w->inputOffset * (1 << (X265_DEPTH - 8));
        int weight = w->inputWeight;
        int denom = w->log2WeightDenom;
        int round = denom ? 1 << (denom - 1) : 0;
        int correction = IF_INTERNAL_PREC - X265_DEPTH; /* intermediate interpolation depth */
        int pwidth = ((width + 31) >> 5) << 5;
        primitives.weight_pp(ref, weightTemp, stride, pwidth, height,
                             weight, round << correction, denom + correction, offset);
        ref = weightTemp;
    }

    uint32_t cost = 0;
    pixel *f = fenc, *r = ref;

    if (bLuma)
    {
        int cu = 0;
        for (int y = 0; y < height; y += 8, r += 8 * stride, f += 8 * stride)
        {
            for (int x = 0; x < width; x += 8, cu++)
            {
                int cmp = primitives.pu[LUMA_8x8].satd(r + x, stride, f + x, stride);
                cost += X265_MIN(cmp, cache.intraCost[cu]);
            }
        }
    }
    else if (cache.csp == X265_CSP_I444)
        for (int y = 0; y < height; y += 16, r += 16 * stride, f += 16 * stride)
            for (int x = 0; x < width; x += 16)
                cost += primitives.pu[LUMA_16x16].satd(r + x, stride, f + x, stride);
    else
        for (int y = 0; y < height; y += 8, r += 8 * stride, f += 8 * stride)
            for (int x = 0; x < width; x += 8)
                cost += primitives.pu[LUMA_8x8].satd(r + x, stride, f + x, stride);

    return cost;
}


void blockSatd(uint32_t* out, Lowres& fenc, pixel* ref, const WeightParam* w, pixel* tmp, const Cache& cache)
{
    intptr_t stride = fenc.lumaStride;
    int width = fenc.width, height = fenc.lines;
    if (w && w->wtPresent)
    {
        int denom = w->log2WeightDenom;
        int round = denom ? 1 << (denom - 1) : 0;
        int correction = IF_INTERNAL_PREC - X265_DEPTH;
        int pwidth = ((width + 31) >> 5) << 5;
        primitives.weight_pp(ref, tmp, stride, pwidth, height, w->inputWeight,
                             round << correction, denom + correction, w->inputOffset << (X265_DEPTH - 8));
        ref = tmp;
    }
    for (int y = 0, cu = 0; y < height; y += 8)
        for (int x = 0; x < width; x += 8, cu++)
            out[cu] = primitives.pu[LUMA_8x8].satd(ref + y * stride + x, stride, fenc.lowresPlane[0] + y * stride + x, stride);
    (void)cache;
}

/* Lowres luma cost of predicting fenc from the nearest L0/L1 pair: per 8x8
 * block the cheapest of bi-average, uni-0, uni-1 and intra. r0/r1 are the
 * (already motion compensated and, if applicable, weighted) references. */
uint64_t biPairCost(Lowres& fenc, const pixel* r0, const pixel* r1, const Cache& cache)
{
    intptr_t stride = fenc.lumaStride;
    uint64_t cost = 0;
    ALIGN_VAR_16(pixel, avg[8 * 8]);
    for (int y = 0, cu = 0; y < fenc.lines; y += 8)
        for (int x = 0; x < fenc.width; x += 8, cu++)
        {
            intptr_t off = y * stride + x;
            const pixel* f = fenc.lowresPlane[0] + off;
            primitives.pu[LUMA_8x8].pixelavg_pp[NONALIGNED](avg, 8, r0 + off, stride, r1 + off, stride, 32);
            uint32_t c = primitives.pu[LUMA_8x8].satd(avg, 8, f, stride);
            c = X265_MIN(c, (uint32_t)primitives.pu[LUMA_8x8].satd(r0 + off, stride, f, stride));
            c = X265_MIN(c, (uint32_t)primitives.pu[LUMA_8x8].satd(r1 + off, stride, f, stride));
            cost += X265_MIN(c, (uint32_t)cache.intraCost[cu]);
        }
    return cost;
}

struct WPCand { int list, ref; bool bWeighted; uint32_t hdr; };


uint64_t wpBlockChoiceCost(const bool* on, const WPCand* cand, int numCand, const uint32_t* satdU, const uint32_t* satdW,
                           const uint32_t* bi, int i00, int i10, const int* intraCost, int numBlocks)
{
    uint64_t cost = 0;
    for (int i = 0; i < numCand; i++)
        if (on[i])
            cost += cand[i].hdr;
    int k = bi ? ((on[i00] ? 1 : 0) | (on[i10] ? 2 : 0)) : 0;
    for (int cu = 0; cu < numBlocks; cu++)
    {
        uint32_t c = (uint32_t)intraCost[cu];
        for (int i = 0; i < numCand; i++)
            c = X265_MIN(c, on[i] ? satdW[(size_t)i * numBlocks + cu] : satdU[(size_t)i * numBlocks + cu]);
        if (bi)
            c = X265_MIN(c, bi[(size_t)k * numBlocks + cu]);
        cost += c;
    }
    return cost;
}

/* Per 8x8 SATD of the average of two (already weighted) lowres references */
void blockSatdBi(uint32_t* out, Lowres& fenc, const pixel* r0, const pixel* r1)
{
    intptr_t stride = fenc.lumaStride;
    ALIGN_VAR_16(pixel, avg[8 * 8]);
    for (int y = 0, cu = 0; y < fenc.lines; y += 8)
        for (int x = 0; x < fenc.width; x += 8, cu++)
        {
            intptr_t off = y * stride + x;
            primitives.pu[LUMA_8x8].pixelavg_pp[NONALIGNED](avg, 8, r0 + off, stride, r1 + off, stride, 32);
            out[cu] = primitives.pu[LUMA_8x8].satd(avg, 8, fenc.lowresPlane[0] + off, stride);
        }
}
}

namespace X265_NS {
void weightAnalyse(Slice& slice, Frame& frame, x265_param& param)
{
    WeightParam wp[2][MAX_NUM_REF][3];

    /* refGain = SATD(prediction without weight) − SATD(prediction with weight) − header cost of the weight
     * SATD reduction (net of header cost) achieved by each weighted reference, used to prioritise references when the HEVC weight-flag budget is exceeded
     * refGain measures how much the prediction error goes down because of this weight, minus the bits spent sending it. That's the net benefit of that one reference's weight.*/
    uint32_t    refGain[2][MAX_NUM_REF];
    PicYuv *fencPic = frame.m_fencPic;
    Lowres& fenc    = frame.m_lowres;

    Cache cache;

    memset(&cache, 0, sizeof(cache));
    memset(refGain, 0, sizeof(refGain));
    cache.intraCost = fenc.intraCost;
    cache.numPredDir = slice.isInterP() ? 1 : 2;
    cache.lowresWidthInCU = fenc.width >> 3;
    cache.lowresHeightInCU = fenc.lines >> 3;
    cache.csp = param.internalCsp;
    cache.hshift = CHROMA_H_SHIFT(cache.csp);
    cache.vshift = CHROMA_V_SHIFT(cache.csp);

    /* Use single allocation for motion compensated ref and weight buffers */
    pixel *mcbuf = X265_MALLOC(pixel, 2 * fencPic->m_stride * fencPic->m_picHeight);
    if (!mcbuf)
    {
        slice.disableWeights();
        return;
    }
    pixel *weightTemp = mcbuf + fencPic->m_stride * fencPic->m_picHeight;

    int lambda = (int)x265_lambda_tab[X265_LOOKAHEAD_QP];
    int curPoc = slice.m_poc;
    const float epsilon = 1.f / 128.f;
    const int numPlanes = param.internalCsp != X265_CSP_I400 ? 3 : 1;

    /* Legal weight range. HEVC codes delta_luma_weight / delta_chroma_weight in
     * [-128, 127] around 1 << denom, so at denom 7 a weight may go up to 255
     * (a gain of ~2x). The x264-inherited cap of 127 cannot express a gain > 1
     * at denom 7, which is needed whenever the current picture is brighter than
     * the reference (a future reference during a fade-out, a past reference
     * during a fade-in). Max weight 255 keeps (w << 6) within 16 bits, as the
     * weight_pp / weight_sp assembly requires. */
#define WP_MIN_WEIGHT(d) X265_MAX(0, (1 << (d)) - 128)
#define WP_MAX_WEIGHT(d) ((1 << (d)) + 127)

    int chromaDenom, lumaDenom, denom;
    chromaDenom = lumaDenom = 7;
    int numpixels[3];
    int w16 = ((fencPic->m_picWidth  + 15) >> 4) << 4;
    int h16 = ((fencPic->m_picHeight + 15) >> 4) << 4;
    numpixels[0] = w16 * h16;
    numpixels[1] = numpixels[2] = numpixels[0] >> (cache.hshift + cache.vshift);

    for (int list = 0; list < cache.numPredDir; list++)
    {
        /* every active reference index of the list is analysed */
        for (int ref = 0; ref < slice.m_numRefIdx[list]; ref++)
        {
            /* The first analysed reference (list 0, ref 0) establishes the slice-wide
             * luma and chroma denominators. All later references are searched with those denominators fixed. */
            const bool bFirstRef = !list && !ref;

            WeightParam *weights = wp[list][ref];
            Frame *refFrame = slice.m_refFrameList[list][ref];
            Lowres& refLowres = refFrame->m_lowres;
            int diffPoc = abs(curPoc - refFrame->m_poc);

            int mvDir = refFrame->m_poc > curPoc ? 1 : 0;

            /* prepare estimates */
            float guessScale[3], fencMean[3], refMean[3];
            for (int plane = 0; plane < numPlanes; plane++)
            {
                SET_WEIGHT(weights[plane], false, 1, 0, 0);
                uint64_t fencVar = fenc.wp_ssd[plane] + !refLowres.wp_ssd[plane];
                uint64_t refVar  = refLowres.wp_ssd[plane] + !refLowres.wp_ssd[plane];
                guessScale[plane] = sqrt((float)fencVar / refVar);
                fencMean[plane] = (float)fenc.wp_sum[plane] / (numpixels[plane]) / (1 << (X265_DEPTH - 8));
                refMean[plane]  = (float)refLowres.wp_sum[plane] / (numpixels[plane]) / (1 << (X265_DEPTH - 8));
            }

            while (bFirstRef && chromaDenom > 0)
            {
                float thresh = (float)WP_MAX_WEIGHT(chromaDenom) / (1 << chromaDenom);
                if (guessScale[1] < thresh && guessScale[2] < thresh)
                    break;
                chromaDenom--;
            }

            SET_WEIGHT(weights[1], false, 1 << chromaDenom, chromaDenom, 0);
            SET_WEIGHT(weights[2], false, 1 << chromaDenom, chromaDenom, 0);

            MV *mvs = NULL;

            for (int plane = 0; plane < numPlanes; plane++)
            {
                denom = plane ? chromaDenom : lumaDenom;
                if (plane && !weights[0].wtPresent)
                    break;

                /* Early termination */
                x265_emms();
                if (fabsf(refMean[plane] - fencMean[plane]) < 0.5f && fabsf(1.f - guessScale[plane]) < epsilon)
                {
                    SET_WEIGHT(weights[plane], 0, 1 << denom, denom, 0);
                    continue;
                }

                if (plane)
                {
                    int scale = x265_clip3(0, 511, (int)(guessScale[plane] * (1 << denom) + 0.5f));
                    if (scale > WP_MAX_WEIGHT(denom))
                        continue;
                    scale = X265_MAX(scale, WP_MIN_WEIGHT(denom));
                    weights[plane].inputWeight = scale;
                }
                else
                {
                    int guessW = (int)(guessScale[plane] * (1 << denom) + 0.5f);
                    SET_WEIGHT(weights[plane], false, x265_clip3(WP_MIN_WEIGHT(denom), WP_MAX_WEIGHT(denom), guessW), denom, 0);
                }

                int mindenom = weights[plane].log2WeightDenom;
                int minscale = weights[plane].inputWeight;
                int minoff = 0;

                if (!plane && diffPoc <= param.bframes + 1)
                {
                    mvs = fenc.lowresMvs[mvDir][diffPoc];

                    /* test whether this motion search was performed by lookahead */
                    if (mvs[0].x == 0x7FFF)
                        mvs = 0;
                    /* chroma borders are extended unconditionally at frame-input
                     * time in Encoder::encode(); no lazy extension needed here */
                }

                /* prepare inputs to weight analysis */
                pixel *orig;
                pixel *fref;
                intptr_t stride;
                int    width, height;
                switch (plane)
                {
                case 0:
                    orig = fenc.lowresPlane[0];
                    stride = fenc.lumaStride;
                    width = fenc.width;
                    height = fenc.lines;
                    fref = refLowres.lowresPlane[0];
                    if (mvs)
                    {
                        mcLuma(mcbuf, refLowres, mvs);
                        fref = mcbuf;
                    }
                    break;

                case 1:
                    orig = fencPic->m_picOrg[1];
                    stride = fencPic->m_strideC;
                    fref = refFrame->m_fencPic->m_picOrg[1];

                    width =  ((fencPic->m_picWidth  >> 4) << 4) >> cache.hshift;
                    height = ((fencPic->m_picHeight >> 4) << 4) >> cache.vshift;
                    if (mvs)
                    {
                        mcChroma(mcbuf, fref, stride, mvs, cache, height, width);
                        fref = mcbuf;
                    }
                    break;

                case 2:
                    orig = fencPic->m_picOrg[2];
                    stride = fencPic->m_strideC;
                    fref = refFrame->m_fencPic->m_picOrg[2];
                    width =  ((fencPic->m_picWidth  >> 4) << 4) >> cache.hshift;
                    height = ((fencPic->m_picHeight >> 4) << 4) >> cache.vshift;
                    if (mvs)
                    {
                        mcChroma(mcbuf, fref, stride, mvs, cache, height, width);
                        fref = mcbuf;
                    }
                    break;

                default:
                    slice.disableWeights();
                    X265_FREE(mcbuf);
                    return;
                }

                uint32_t origscore = weightCost(orig, fref, weightTemp, stride, cache, width, height, NULL, !plane);
                if (!origscore)
                {
                    SET_WEIGHT(weights[plane], 0, 1 << denom, denom, 0);
                    continue;
                }

                uint32_t minscore = origscore;
                bool bFound = false;

                static const int scaleDist = 4;
                static const int offsetDist = 2;

                const int minW = WP_MIN_WEIGHT(mindenom), maxW = WP_MAX_WEIGHT(mindenom);
                int startScale = x265_clip3(minW, maxW, minscale - scaleDist);
                int endScale   = x265_clip3(minW, maxW, minscale + scaleDist);
                for (int scale = startScale; scale <= endScale; scale++)
                {
                    int deltaWeight = scale - (1 << mindenom);
                    if (deltaWeight > 127 || deltaWeight <= -128)
                        continue;

                    x265_emms();
                    int curScale = scale;
                    int curOffset = (int)(fencMean[plane] - refMean[plane] * curScale / (1 << mindenom) + 0.5f);
                    if (curOffset < -128 || curOffset > 127)
                    {
                        /* Rescale considering the constraints on curOffset. We do it in this order
                         * because scale has a much wider range than offset (because of denom), so
                         * it should almost never need to be clamped. */
                        curOffset = x265_clip3(-128, 127, curOffset);
                        curScale = (int)((1 << mindenom) * (fencMean[plane] - curOffset) / refMean[plane] + 0.5f);
                        curScale = x265_clip3(minW, maxW, curScale);
                    }

                    int startOffset = x265_clip3(-128, 127, curOffset - offsetDist);
                    int endOffset   = x265_clip3(-128, 127, curOffset + offsetDist);
                    for (int off = startOffset; off <= endOffset; off++)
                    {
                        WeightParam wsp;
                        SET_WEIGHT(wsp, true, curScale, mindenom, off);
                        uint32_t s = weightCost(orig, fref, weightTemp, stride, cache, width, height, &wsp, !plane) +
                                     sliceHeaderCost(&wsp, lambda, !!plane);
                        COPY4_IF_LT(minscore, s, minscale, curScale, minoff, off, bFound, true);

                        /* Don't check any more offsets if the previous one had a lower cost than the current one */
                        if (minoff == startOffset && off != startOffset)
                            break;
                    }
                }

                int predTemp = (128 - ((128 * minscale) >> (mindenom)));
                int deltaChromaTemp = minoff - predTemp;

                if (!bFound || (minscale == (1 << mindenom) && minoff == 0) || (float)minscore / origscore > 0.998f ||
                    (plane && (deltaChromaTemp < -512 || deltaChromaTemp > 511)) )
                {
                    SET_WEIGHT(weights[plane], false, 1 << denom, denom, 0);
                }
                else
                {
                    SET_WEIGHT(weights[plane], true, minscale, mindenom, minoff);
                    refGain[list][ref] += origscore - minscore;
                }
            }

            if (weights[0].wtPresent)
            {
                if (weights[1].wtPresent != weights[2].wtPresent)
                {
                    if (weights[1].wtPresent)
                        weights[2] = weights[1];
                    else
                        weights[1] = weights[2];
                }
            }

            if (bFirstRef)
            {
                lumaDenom = weights[0].log2WeightDenom;
                chromaDenom = weights[1].log2WeightDenom;
            }
        }
    }

    {
        int numBlocks = ((fenc.width + 7) >> 3) * ((fenc.lines + 7) >> 3);
        WPCand cand[2 * MAX_NUM_REF];
        int numCand = 0;
        bool anyWeight = false;
        for (int list = 0; list < cache.numPredDir; list++)
            for (int ref = 0; ref < slice.m_numRefIdx[list]; ref++)
            {
                WPCand& c = cand[numCand++];
                c.list = list; c.ref = ref;
                c.bWeighted = !!wp[list][ref][0].wtPresent;
                c.hdr = c.bWeighted ? (uint32_t)sliceHeaderCost(&wp[list][ref][0], lambda, 0) : 0;
                anyWeight |= c.bWeighted;
            }

        size_t planeSize = (size_t)fenc.lumaStride * (fenc.lines + 8);

        uint32_t* satd = anyWeight ? X265_MALLOC(uint32_t, (size_t)numBlocks * (2 * numCand + 4)) : NULL;
        pixel* pbuf = anyWeight ? X265_MALLOC(pixel, 6 * planeSize) : NULL;
        if (satd && pbuf)
        {
            memset(pbuf, 0, 6 * planeSize * sizeof(pixel));
            uint32_t* satdU = satd;
            uint32_t* satdW = satd + (size_t)numBlocks * numCand;
            uint32_t* bi = satd + (size_t)numBlocks * 2 * numCand;
            pixel* mc = pbuf;
            pixel* wtmp = pbuf + planeSize;     /* weighted copy */

            pixel* keep[2][2] = { { pbuf + 2 * planeSize, pbuf + 3 * planeSize }, { pbuf + 4 * planeSize, pbuf + 5 * planeSize } };
            bool bBi = cache.numPredDir == 2 && slice.m_numRefIdx[0] && slice.m_numRefIdx[1];
            pixel* ref0U[2] = { NULL, NULL };
            pixel* ref0W[2] = { NULL, NULL };

            for (int i = 0; i < numCand; i++)
            {
                Frame* rf = slice.m_refFrameList[cand[i].list][cand[i].ref];
                int d = abs(curPoc - rf->m_poc);
                MV* mvs = NULL;
                if (d <= param.bframes + 1)
                {
                    mvs = fenc.lowresMvs[rf->m_poc > curPoc ? 1 : 0][d];
                    if (mvs[0].x == 0x7FFF)
                        mvs = NULL;
                }
                pixel* src = rf->m_lowres.lowresPlane[0];
                bool bRef0 = bBi && cand[i].ref == 0;
                pixel* dst = mc;
                if (bRef0)
                    dst = keep[cand[i].list][0];
                if (mvs)
                {
                    mcLuma(dst, rf->m_lowres, mvs);
                    src = dst;
                }
                blockSatd(satdU + (size_t)i * numBlocks, fenc, src, NULL, NULL, cache);
                pixel* wdst = bRef0 ? keep[cand[i].list][1] : wtmp;
                if (cand[i].bWeighted)
                    blockSatd(satdW + (size_t)i * numBlocks, fenc, src, &wp[cand[i].list][cand[i].ref][0], wdst, cache);
                if (bRef0)
                {
                    ref0U[cand[i].list] = src;
                    ref0W[cand[i].list] = cand[i].bWeighted ? wdst : src;
                }
            }

            if (bBi && ref0U[0] && ref0U[1])
            {
                for (int k = 0; k < 4; k++)
                    blockSatdBi(bi + (size_t)k * numBlocks, fenc, (k & 1) ? ref0W[0] : ref0U[0], (k & 2) ? ref0W[1] : ref0U[1]);
            }
            else
                bBi = false;

            int i00 = -1, i10 = -1;
            for (int i = 0; i < numCand; i++)
            {
                if (cand[i].list == 0 && cand[i].ref == 0) i00 = i;
                if (cand[i].list == 1 && cand[i].ref == 0) i10 = i;
            }

            bool on[2 * MAX_NUM_REF];
            for (int i = 0; i < numCand; i++)
                on[i] = cand[i].bWeighted;

            /* 1. B slices: the nearest L0/L1 pair must predict better with its
             * weights than without (by 0.2%, the same margin as the per-reference
             * test), otherwise every weight of the slice is dropped. This is what
             * catches cross-dissolves: each weight looks good against its own
             * reference, but the unweighted average already follows the blend. */
            if (bBi && (on[i00] || on[i10]))
            {
                uint64_t costW = biPairCost(fenc, ref0W[0], ref0W[1], cache);
                uint64_t costU = biPairCost(fenc, ref0U[0], ref0U[1], cache);
                if (costW * 1000 > costU * 998)
                {
                    if (param.logLevel >= X265_LOG_FULL)
                        x265_log(&param, X265_LOG_FULL, "poc: %d all weights dropped by bi-pair check (%llu vs %llu unweighted)\n",
                                 slice.m_poc, (unsigned long long)costW, (unsigned long long)costU);
                    for (int i = 0; i < numCand; i++)
                        on[i] = false;
                }
            }

            /* 2. greedy block-choice check for the remaining weights */

            uint64_t best = wpBlockChoiceCost(on, cand, numCand, satdU, satdW, bBi ? bi : NULL, i00, i10, cache.intraCost, numBlocks);
            bool changed = true;
            while (changed)
            {
                changed = false;
                int bestDrop = -1;
                uint64_t bestCost = best;
                for (int i = 0; i < numCand; i++)
                {
                    if (!on[i])
                        continue;
                    on[i] = false;
                    uint64_t c = wpBlockChoiceCost(on, cand, numCand, satdU, satdW, bBi ? bi : NULL, i00, i10, cache.intraCost, numBlocks);
                    on[i] = true;
                    if (c <= bestCost)
                    {
                        bestCost = c;
                        bestDrop = i;
                    }
                }
                if (bestDrop >= 0)
                {
                    on[bestDrop] = false;
                    best = bestCost;
                    changed = true;
                }
            }

            for (int i = 0; i < numCand; i++)
            {
                if (cand[i].bWeighted && !on[i])
                {
                    if (param.logLevel >= X265_LOG_FULL)
                        x265_log(&param, X265_LOG_FULL, "poc: %d L%d:R%d weight dropped by block-choice check\n",
                                 slice.m_poc, cand[i].list, cand[i].ref);
                    SET_WEIGHT(wp[cand[i].list][cand[i].ref][0], false, 1 << lumaDenom, lumaDenom, 0);
                    SET_WEIGHT(wp[cand[i].list][cand[i].ref][1], false, 1 << chromaDenom, chromaDenom, 0);
                    SET_WEIGHT(wp[cand[i].list][cand[i].ref][2], false, 1 << chromaDenom, chromaDenom, 0);
                }
            }
        }
        X265_FREE(satd);
        X265_FREE(pbuf);
    }

    X265_FREE(mcbuf);

    {
        /*  The sum of luma_weight_flag + 2 * chroma_weight_flag
         * over all reference indices of both lists shall not exceed 24. Drop the
         * weighted references with the smallest measured gain until it fits. */
        for (;;)
        {
            int numFlags = 0, minList = -1, minRef = -1;
            uint32_t minGain = UINT32_MAX;
            for (int list = 0; list < cache.numPredDir; list++)
            {
                for (int ref = 0; ref < slice.m_numRefIdx[list]; ref++)
                {
                    WeightParam *w = wp[list][ref];
                    if (!w[0].wtPresent)
                        continue;
                    numFlags += 1 + (numPlanes > 1 && w[1].wtPresent ? 2 : 0);
                    if (refGain[list][ref] < minGain)
                    {
                        minGain = refGain[list][ref];
                        minList = list;
                        minRef = ref;
                    }
                }
            }
            if (numFlags <= 24 || minList < 0)
                break;
            SET_WEIGHT(wp[minList][minRef][0], false, 1 << lumaDenom, lumaDenom, 0);
            SET_WEIGHT(wp[minList][minRef][1], false, 1 << chromaDenom, chromaDenom, 0);
            SET_WEIGHT(wp[minList][minRef][2], false, 1 << chromaDenom, chromaDenom, 0);
        }

        int shift = lumaDenom;
        bool bAnyLuma = false;
        for (int list = 0; list < cache.numPredDir; list++)
        {
            for (int ref = 0; ref < slice.m_numRefIdx[list]; ref++)
            {
                WeightParam &w = wp[list][ref][0];
                if (!w.wtPresent)
                    continue;
                bAnyLuma = true;
                if (w.inputWeight)
                {
                    unsigned long idx;
                    BSF(idx, (uint32_t)w.inputWeight);
                    shift = X265_MIN(shift, (int)idx);
                }
            }
        }
        if (bAnyLuma && shift > 0)
        {
            lumaDenom -= shift;
            for (int list = 0; list < cache.numPredDir; list++)
            {
                for (int ref = 0; ref < slice.m_numRefIdx[list]; ref++)
                {
                    WeightParam &w = wp[list][ref][0];
                    if (w.wtPresent)
                    {
                        w.inputWeight >>= shift;
                        w.log2WeightDenom = lumaDenom;
                    }
                    else
                        SET_WEIGHT(w, false, 1 << lumaDenom, lumaDenom, 0);
                }
            }
        }
    }

#undef WP_MIN_WEIGHT
#undef WP_MAX_WEIGHT

    memcpy(slice.m_weightPredTable, wp, sizeof(WeightParam) * 2 * MAX_NUM_REF * 3);

    if (param.logLevel >= X265_LOG_FULL)
    {
        char buf[1024];
        int p = 0;
        bool bWeighted = false;

        p = snprintf(buf, sizeof(buf), "poc: %d weights:", slice.m_poc);
        int numPredDir = slice.isInterP() ? 1 : 2;
        for (int list = 0; list < numPredDir; list++)
        {
            for (int ref = 0; ref < slice.m_numRefIdx[list] && p < (int)sizeof(buf); ref++)
            {
                WeightParam* w = &wp[list][ref][0];
                if (w[0].wtPresent || w[1].wtPresent || w[2].wtPresent)
                {
                    bWeighted = true;
                    p += snprintf(buf + p, sizeof(buf) - p, " [L%d:R%d(poc %d) ", list, ref, slice.m_refPOCList[list][ref]);
                    if (w[0].wtPresent)
                        p += snprintf(buf + p, sizeof(buf) - p, "Y{%d/%d%+d}", w[0].inputWeight, 1 << w[0].log2WeightDenom, w[0].inputOffset);
                    if (w[1].wtPresent)
                        p += snprintf(buf + p, sizeof(buf) - p, "U{%d/%d%+d}", w[1].inputWeight, 1 << w[1].log2WeightDenom, w[1].inputOffset);
                    if (w[2].wtPresent)
                        p += snprintf(buf + p, sizeof(buf) - p, "V{%d/%d%+d}", w[2].inputWeight, 1 << w[2].log2WeightDenom, w[2].inputOffset);
                    p += snprintf(buf + p, sizeof(buf) - p, "]");
                }
            }
        }

        if (bWeighted)
        {
            if (p < 80) // pad with spaces to ensure progress line overwritten
                snprintf(buf + p, sizeof(buf) - p, "%*s", 80 - p, " ");
            x265_log(&param, X265_LOG_FULL, "%s\n", buf);
        }
    }
}
}
