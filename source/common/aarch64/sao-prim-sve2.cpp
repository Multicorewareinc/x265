/*****************************************************************************
 * Copyright (C) 2024 MulticoreWare, Inc
 *
 * Authors: Hari Limaye <hari.limaye@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#include "sao-prim.h"
#include "neon-sve-bridge.h"
#include "sao.h"

static const int8_t kEoHistIdx[16] = {
    0, -2, -1, 1, 2, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3
};

static inline uint8x16_t sve_count(int8x16_t in, const svint8_t svidx)
{
    // We do not care about initialising the values in the rest of the vector,
    // for VL > 128, as HISTSEG counts matching elements in 128-bit segments.
    return svget_neonq_u8(
        svhistseg_s8(svidx, svset_neonq_s8(svundef_s8(), in)));
}

struct EoPartMask
{
    int16x8_t lo, hi;
};

static inline EoPartMask make_eo_part_mask(int endX)
{
    const int xPart = endX & ~15;
    int8x16_t pv = x265_sve_mask(xPart, endX, vdupq_n_s8(-1));
    int8x16_t pm8 = vreinterpretq_s8_u8(vceqq_s8(pv, vdupq_n_s8(-1)));
    EoPartMask m;
    m.lo = vreinterpretq_s16_s8(vzip1q_s8(pm8, pm8));
    m.hi = vreinterpretq_s16_s8(vzip2q_s8(pm8, pm8));
    return m;
}

/*
 * Compute Edge Offset statistics (stats array).
 * To save some instructions compute stats as negative values - since output of
 * Neon comparison instructions for a matched condition is all 1s (-1).
 */
static inline void compute_eo_stats(const int8x16_t edge_type,
                                          const int16_t *diff, int64x2_t *stats,
                                          int64x2_t &total_acc,
                                          const int16x8_t tmask_lo,
                                          const int16x8_t tmask_hi)
{
    // Create a mask for each edge type.
    int8x16_t mask0 = vreinterpretq_s8_u8(vceqq_s8(edge_type, vdupq_n_s8(-2)));
    int8x16_t mask1 = vreinterpretq_s8_u8(vceqq_s8(edge_type, vdupq_n_s8(-1)));
    int8x16_t mask3 = vreinterpretq_s8_u8(vceqq_s8(edge_type, vdupq_n_s8(1)));
    int8x16_t mask4 = vreinterpretq_s8_u8(vceqq_s8(edge_type, vdupq_n_s8(2)));

    // Widen the masks to 16-bit.
    int16x8_t mask0_lo = vreinterpretq_s16_s8(vzip1q_s8(mask0, mask0));
    int16x8_t mask0_hi = vreinterpretq_s16_s8(vzip2q_s8(mask0, mask0));
    int16x8_t mask1_lo = vreinterpretq_s16_s8(vzip1q_s8(mask1, mask1));
    int16x8_t mask1_hi = vreinterpretq_s16_s8(vzip2q_s8(mask1, mask1));
    int16x8_t mask3_lo = vreinterpretq_s16_s8(vzip1q_s8(mask3, mask3));
    int16x8_t mask3_hi = vreinterpretq_s16_s8(vzip2q_s8(mask3, mask3));
    int16x8_t mask4_lo = vreinterpretq_s16_s8(vzip1q_s8(mask4, mask4));
    int16x8_t mask4_hi = vreinterpretq_s16_s8(vzip2q_s8(mask4, mask4));

    int16x8_t diff_lo = vld1q_s16(diff);
    int16x8_t diff_hi = vld1q_s16(diff + 8);

    // Compute negative stats for each edge type.
    stats[0] = x265_sdotq_s16(stats[0], diff_lo, mask0_lo);
    stats[0] = x265_sdotq_s16(stats[0], diff_hi, mask0_hi);
    stats[1] = x265_sdotq_s16(stats[1], diff_lo, mask1_lo);
    stats[1] = x265_sdotq_s16(stats[1], diff_hi, mask1_hi);
    stats[3] = x265_sdotq_s16(stats[3], diff_lo, mask3_lo);
    stats[3] = x265_sdotq_s16(stats[3], diff_hi, mask3_hi);
    stats[4] = x265_sdotq_s16(stats[4], diff_lo, mask4_lo);
    stats[4] = x265_sdotq_s16(stats[4], diff_hi, mask4_hi);

    total_acc = x265_sdotq_s16(total_acc, diff_lo, tmask_lo);
    total_acc = x265_sdotq_s16(total_acc, diff_hi, tmask_hi);
}

/*
 * Reduce and store Edge Offset statistics (count and stats).
 */
static inline void reduce_eo_stats(int64x2_t *vstats, uint16x8_t vcount,
                                   int32_t *stats, int32_t *count)
{
    // s_eoTable maps edge types to memory in order: {2, 0, 1, 3, 4}.
    // We already have the count values in the correct order for the store,
    // so widen to 32-bit and accumulate to the destination.
    int32x4_t c0123 = vmovl_s16(vget_low_s16(vreinterpretq_s16_u16(vcount)));
    vst1q_s32(count, vaddq_s32(vld1q_s32(count), c0123));
    count[4] += vcount[4];

    int32x4_t s01 = vcombine_s32(vmovn_s64(vstats[2]), vmovn_s64(vstats[0]));
    int32x4_t s23 = vcombine_s32(vmovn_s64(vstats[1]), vmovn_s64(vstats[3]));
    int32x4_t s0123 = vpaddq_s32(s01, s23);
    // Subtract from current stats, as we calculate the negation.
    vst1q_s32(stats, vsubq_s32(vld1q_s32(stats), s0123));
    stats[4] -= vaddvq_s64(vstats[4]);
}

static inline void compute_eo_zero_stats(int64x2_t *vstats, int64x2_t total_acc)
{
    int64x2_t rest = vaddq_s64(vaddq_s64(vstats[0], vstats[1]),
                             vaddq_s64(vstats[3], vstats[4]));
    vstats[2] = vsubq_s64(total_acc, rest);
}

static inline void merge_eo_stats(int64x2_t *a, int64x2_t ta,
                                 int64x2_t *b, int64x2_t tb)
{
    a[0] = vaddq_s64(a[0], b[0]);
    a[1] = vaddq_s64(a[1], b[1]);
    a[3] = vaddq_s64(a[3], b[3]);
    a[4] = vaddq_s64(a[4], b[4]);
    int64x2_t total = vaddq_s64(ta, tb);
    int64x2_t rest = vaddq_s64(vaddq_s64(a[0], a[1]), vaddq_s64(a[3], a[4]));
    a[2] = vsubq_s64(total, rest);
}

namespace X265_NS {
void saoCuStatsE0_sve2(const int16_t *diff, const pixel *rec, intptr_t stride,
                       int endX, int endY, int32_t *stats, int32_t *count)
{
    // Separate buffers for each edge type, so that we can vectorise.
    int64x2_t tmp_stats[5] = { vdupq_n_s64(0), vdupq_n_s64(0), vdupq_n_s64(0),
                               vdupq_n_s64(0), vdupq_n_s64(0) };
    int64x2_t total_acc = vdupq_n_s64(0);
    uint16x8_t count_acc_u16 = vdupq_n_u16(0);
    const svint8_t svidx =
        svset_neonq_s8(svundef_s8(), vld1q_s8(kEoHistIdx));
    const EoPartMask pm = make_eo_part_mask(endX);
    const int16x8_t neg1 = vdupq_n_s16(-1);

    for (int y = 0; y < endY; y++)
    {
        uint8x16_t count_acc_u8 = vdupq_n_u8(0);

        // Calculate negated sign_left(x) directly, to save negation when
        // reusing sign_right(x) as sign_left(x + 1).
        int8x16_t neg_sign_left = vdupq_n_s8(x265_signOf(rec[-1] - rec[0]));
        for (int x = 0; x + 16 <= endX; x += 16)
        {
            int8x16_t sign_right = signOf_neon(rec + x, rec + x + 1);

            // neg_sign_left(x) = sign_right(x + 1), reusing one from previous
            // iteration.
            neg_sign_left = vextq_s8(neg_sign_left, sign_right, 15);

            // Subtract instead of add, as sign_left is negated.
            int8x16_t edge_type = vsubq_s8(sign_right, neg_sign_left);

            // For reuse in the next iteration.
            neg_sign_left = sign_right;

            count_acc_u8 = vaddq_u8(count_acc_u8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, tmp_stats, total_acc,
                                    neg1, neg1);
        }
        if (endX & 15)
        {
            const int x = endX & ~15;
            int8x16_t sign_right = signOf_neon(rec + x, rec + x + 1);
            neg_sign_left = vextq_s8(neg_sign_left, sign_right, 15);
            int8x16_t edge_type = vsubq_s8(sign_right, neg_sign_left);
            edge_type = x265_sve_mask(x, endX, edge_type);
            count_acc_u8 = vaddq_u8(count_acc_u8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, tmp_stats, total_acc,
                                    pm.lo, pm.hi);
        }

        // The width (endX) can be a maximum of 64, so we can safely
        // widen from 8-bit count accumulators after one inner loop iteration.
        // Technically the largest an accumulator could reach after one inner
        // loop iteration is 64, if every input value had the same edge type, so
        // we could complete two iterations (2 * 64 = 128) before widening.
        count_acc_u16 = vaddw_u8(count_acc_u16, vget_low_u8(count_acc_u8));

        diff += MAX_CU_SIZE;
        rec += stride;
    }

    compute_eo_zero_stats(tmp_stats, total_acc);
    reduce_eo_stats(tmp_stats, count_acc_u16, stats, count);
}

void saoCuStatsE1_sve2(const int16_t *diff, const pixel *rec, intptr_t stride,
                       int8_t *upBuff1, int endX, int endY, int32_t *stats,
                       int32_t *count)
{
    // Separate buffers for each edge type, so that we can vectorise.
    int64x2_t st0[5] = { vdupq_n_s64(0), vdupq_n_s64(0), vdupq_n_s64(0),
                         vdupq_n_s64(0), vdupq_n_s64(0) };
    int64x2_t st1[5] = { vdupq_n_s64(0), vdupq_n_s64(0), vdupq_n_s64(0),
                         vdupq_n_s64(0), vdupq_n_s64(0) };
    int64x2_t tot0 = vdupq_n_s64(0);
    int64x2_t tot1 = vdupq_n_s64(0);
    uint16x8_t count_acc_u16 = vdupq_n_u16(0);
    const svint8_t svidx = svset_neonq_s8(svundef_s8(), vld1q_s8(kEoHistIdx));
    const EoPartMask pm = make_eo_part_mask(endX);
    const int16x8_t neg1 = vdupq_n_s16(-1);

    // Negate upBuff1 (sign_up), so we can subtract and save repeated negations.
    for (int x = 0; x < endX; x += 16)
    {
        vst1q_s8(upBuff1 + x, vnegq_s8(vld1q_s8(upBuff1 + x)));
    }

    for (int y = 0; y + 2 <= endY; y += 2)
    {
        // Two rows contain at most 128 pixels, fitting in unsigned byte counts.
        uint8x16_t c8 = vdupq_n_u8(0);
        for (int x = 0; x + 16 <= endX; x += 16)
        {
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t dn0 = signOf_neon(rec + x, rec + x + stride);
            int8x16_t edge0 = vsubq_s8(dn0, sign_up);
            int8x16_t dn1 = signOf_neon(rec + x + stride, rec + x + 2 * stride);
            int8x16_t edge1 = vsubq_s8(dn1, dn0);
            vst1q_s8(upBuff1 + x, dn1);
            c8 = vaddq_u8(c8, sve_count(edge0, svidx));
            c8 = vaddq_u8(c8, sve_count(edge1, svidx));
            compute_eo_stats(edge0, diff + x, st0, tot0, neg1, neg1);
            compute_eo_stats(edge1, diff + x + MAX_CU_SIZE, st1, tot1, neg1, neg1);
        }
        if (endX & 15)
        {
            const int x = endX & ~15;
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t dn0 = signOf_neon(rec + x, rec + x + stride);
            int8x16_t edge0 = x265_sve_mask(x, endX, vsubq_s8(dn0, sign_up));
            int8x16_t dn1 = signOf_neon(rec + x + stride, rec + x + 2 * stride);
            int8x16_t edge1 = x265_sve_mask(x, endX, vsubq_s8(dn1, dn0));
            vst1q_s8(upBuff1 + x, dn1);
            c8 = vaddq_u8(c8, sve_count(edge0, svidx));
            c8 = vaddq_u8(c8, sve_count(edge1, svidx));
            compute_eo_stats(edge0, diff + x, st0, tot0, pm.lo, pm.hi);
            compute_eo_stats(edge1, diff + x + MAX_CU_SIZE, st1, tot1, pm.lo, pm.hi);
        }
        count_acc_u16 = vaddw_u8(count_acc_u16, vget_low_u8(c8));
        diff += 2 * MAX_CU_SIZE;
        rec += 2 * stride;
    }
    if (endY & 1)
    {
        uint8x16_t c8 = vdupq_n_u8(0);
        for (int x = 0; x + 16 <= endX; x += 16)
        {
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t sign_down = signOf_neon(rec + x, rec + x + stride);
            int8x16_t edge_type = vsubq_s8(sign_down, sign_up);
            vst1q_s8(upBuff1 + x, sign_down);
            c8 = vaddq_u8(c8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, st0, tot0, neg1, neg1);
        }
        if (endX & 15)
        {
            const int x = endX & ~15;
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t sign_down = signOf_neon(rec + x, rec + x + stride);
            int8x16_t edge_type = x265_sve_mask(x, endX, vsubq_s8(sign_down, sign_up));
            vst1q_s8(upBuff1 + x, sign_down);
            c8 = vaddq_u8(c8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, st0, tot0, pm.lo, pm.hi);
        }
        count_acc_u16 = vaddw_u8(count_acc_u16, vget_low_u8(c8));
    }

    merge_eo_stats(st0, tot0, st1, tot1);
    reduce_eo_stats(st0, count_acc_u16, stats, count);
}

void saoCuStatsE2_sve2(const int16_t *diff, const pixel *rec, intptr_t stride,
                       int8_t *upBuff1, int8_t *upBufft, int endX, int endY,
                       int32_t *stats, int32_t *count)
{
    // Separate buffers for each edge type, so that we can vectorise.
    int64x2_t tmp_stats[5] = { vdupq_n_s64(0), vdupq_n_s64(0), vdupq_n_s64(0),
                               vdupq_n_s64(0), vdupq_n_s64(0) };
    int64x2_t total_acc = vdupq_n_s64(0);
    uint16x8_t count_acc_u16 = vdupq_n_u16(0);
    const svint8_t svidx =
        svset_neonq_s8(svundef_s8(), vld1q_s8(kEoHistIdx));
    const EoPartMask pm = make_eo_part_mask(endX);
    const int16x8_t neg1 = vdupq_n_s16(-1);

    // Negate upBuff1 (sign_up) so we can subtract and save repeated negations.
    for (int x = 0; x < endX; x += 16)
    {
        vst1q_s8(upBuff1 + x, vnegq_s8(vld1q_s8(upBuff1 + x)));
    }

    for (int y = 0; y < endY; y++)
    {
        uint8x16_t count_acc_u8 = vdupq_n_u8(0);

        upBufft[0] = x265_signOf(rec[-1] - rec[stride]);
        for (int x = 0; x + 16 <= endX; x += 16)
        {
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t sign_down = signOf_neon(rec + x, rec + x + stride + 1);

            // Subtract instead of add, as sign_up is negated.
            int8x16_t edge_type = vsubq_s8(sign_down, sign_up);

            // For reuse in the next iteration.
            vst1q_s8(upBufft + x + 1, sign_down);

            count_acc_u8 = vaddq_u8(count_acc_u8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, tmp_stats, total_acc,
                                    neg1, neg1);
        }
        if (endX & 15)
        {
            const int x = endX & ~15;
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t sign_down = signOf_neon(rec + x, rec + x + stride + 1);
            int8x16_t edge_type = vsubq_s8(sign_down, sign_up);
            vst1q_s8(upBufft + x + 1, sign_down);
            edge_type = x265_sve_mask(x, endX, edge_type);
            count_acc_u8 = vaddq_u8(count_acc_u8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, tmp_stats, total_acc,
                                    pm.lo, pm.hi);
        }

        std::swap(upBuff1, upBufft);

        // The width (endX) can be a maximum of 64, so we can safely
        // widen from 8-bit count accumulators after one inner loop iteration.
        // Technically the largest an accumulator could reach after one inner
        // loop iteration is 64, if every input value had the same edge type, so
        // we could complete two iterations (2 * 64 = 128) before widening.
        count_acc_u16 = vaddw_u8(count_acc_u16, vget_low_u8(count_acc_u8));

        rec += stride;
        diff += MAX_CU_SIZE;
    }

    compute_eo_zero_stats(tmp_stats, total_acc);
    reduce_eo_stats(tmp_stats, count_acc_u16, stats, count);
}

void saoCuStatsE3_sve2(const int16_t *diff, const pixel *rec, intptr_t stride,
                       int8_t *upBuff1, int endX, int endY, int32_t *stats,
                       int32_t *count)
{
    // Separate buffers for each edge type, so that we can vectorise.
    int64x2_t tmp_stats[5] = { vdupq_n_s64(0), vdupq_n_s64(0), vdupq_n_s64(0),
                               vdupq_n_s64(0), vdupq_n_s64(0) };
    int64x2_t total_acc = vdupq_n_s64(0);
    uint16x8_t count_acc_u16 = vdupq_n_u16(0);
    const svint8_t svidx =
        svset_neonq_s8(svundef_s8(), vld1q_s8(kEoHistIdx));
    const EoPartMask pm = make_eo_part_mask(endX);
    const int16x8_t neg1 = vdupq_n_s16(-1);

    // Negate upBuff1 (sign_up) so we can subtract and save repeated negations.
    for (int x = 0; x < endX; x += 16)
    {
        vst1q_s8(upBuff1 + x, vnegq_s8(vld1q_s8(upBuff1 + x)));
    }

    for (int y = 0; y < endY; y++)
    {
        uint8x16_t count_acc_u8 = vdupq_n_u8(0);

        for (int x = 0; x + 16 <= endX; x += 16)
        {
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t sign_down = signOf_neon(rec + x, rec + x + stride - 1);

            // Subtract instead of add, as sign_up is negated.
            int8x16_t edge_type = vsubq_s8(sign_down, sign_up);

            // For reuse in the next iteration.
            vst1q_s8(upBuff1 + x - 1, sign_down);

            count_acc_u8 = vaddq_u8(count_acc_u8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, tmp_stats, total_acc,
                                    neg1, neg1);
        }
        if (endX & 15)
        {
            const int x = endX & ~15;
            int8x16_t sign_up = vld1q_s8(upBuff1 + x);
            int8x16_t sign_down = signOf_neon(rec + x, rec + x + stride - 1);
            int8x16_t edge_type = vsubq_s8(sign_down, sign_up);
            vst1q_s8(upBuff1 + x - 1, sign_down);
            edge_type = x265_sve_mask(x, endX, edge_type);
            count_acc_u8 = vaddq_u8(count_acc_u8, sve_count(edge_type, svidx));
            compute_eo_stats(edge_type, diff + x, tmp_stats, total_acc,
                                    pm.lo, pm.hi);
        }

        upBuff1[endX - 1] = x265_signOf(rec[endX] - rec[endX - 1 + stride]);

        // The width (endX) can be a maximum of 64, so we can safely
        // widen from 8-bit count accumulators after one inner loop iteration.
        // Technically the largest an accumulator could reach after one inner
        // loop iteration is 64, if every input value had the same edge type, so
        // we could complete two iterations (2 * 64 = 128) before widening.
        count_acc_u16 = vaddw_u8(count_acc_u16, vget_low_u8(count_acc_u8));

        rec += stride;
        diff += MAX_CU_SIZE;
    }

    compute_eo_zero_stats(tmp_stats, total_acc);
    reduce_eo_stats(tmp_stats, count_acc_u16, stats, count);
}

#if !HIGH_BIT_DEPTH
static const uint8_t kBoHistIdx0[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const uint8_t kBoHistIdx1[16] = {
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

static inline uint8x16_t bo_mask_band(int x, int endX, uint8x16_t band)
{
    svbool_t pg = svwhilelt_b8((uint64_t)x, (uint64_t)endX);
    return svget_neonq_u8(svsel_u8(pg, svset_neonq_u8(svundef_u8(), band),
                                    svdup_n_u8(255)));
}

static inline void bo_acc_histseg_counts(uint8x16_t band, uint8x16_t idx0,
                                         uint8x16_t idx1, int32_t *count)
{
    svuint8_t svband = svset_neonq_u8(svundef_u8(), band);
    uint8x16_t h0 = svget_neonq_u8(
        svhistseg_u8(svset_neonq_u8(svundef_u8(), idx0), svband));
    uint8x16_t h1 = svget_neonq_u8(
        svhistseg_u8(svset_neonq_u8(svundef_u8(), idx1), svband));

    uint16x8_t h0_lo = vmovl_u8(vget_low_u8(h0));
    uint16x8_t h0_hi = vmovl_u8(vget_high_u8(h0));
    uint16x8_t h1_lo = vmovl_u8(vget_low_u8(h1));
    uint16x8_t h1_hi = vmovl_u8(vget_high_u8(h1));

    vst1q_s32(count + 0,
              vaddq_s32(vld1q_s32(count + 0),
                        vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(h0_lo)))));
    vst1q_s32(count + 4,
              vaddq_s32(vld1q_s32(count + 4),
                        vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(h0_lo)))));
    vst1q_s32(count + 8,
              vaddq_s32(vld1q_s32(count + 8),
                        vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(h0_hi)))));
    vst1q_s32(count + 12,
              vaddq_s32(vld1q_s32(count + 12),
                        vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(h0_hi)))));
    vst1q_s32(count + 16,
              vaddq_s32(vld1q_s32(count + 16),
                        vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(h1_lo)))));
    vst1q_s32(count + 20,
              vaddq_s32(vld1q_s32(count + 20),
                        vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(h1_lo)))));
    vst1q_s32(count + 24,
              vaddq_s32(vld1q_s32(count + 24),
                        vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(h1_hi)))));
    vst1q_s32(count + 28,
              vaddq_s32(vld1q_s32(count + 28),
                        vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(h1_hi)))));
}

void saoCuStatsBO_sve2(const int16_t *diff, const pixel *rec, intptr_t stride,
                          int endX, int endY, int32_t *stats, int32_t *count)
{
    const int shift = X265_DEPTH - SAO::SAO_BO_BITS;
    const uint8x16_t idx0 = vld1q_u8(kBoHistIdx0);
    const uint8x16_t idx1 = vld1q_u8(kBoHistIdx1);

    ALIGN_VAR_16(int32_t, stats_bank[16][32]);
    ALIGN_VAR_16(int32_t, count_local[32]);
    memset(stats_bank, 0, sizeof(stats_bank));
    memset(count_local, 0, sizeof(count_local));

    for (int y = 0; y < endY; y++)
    {
        for (int x = 0; x + 16 <= endX; x += 16)
        {
            uint8x16_t band = vshrq_n_u8(vld1q_u8(rec + x), shift);
            bo_acc_histseg_counts(band, idx0, idx1, count_local);

            uint8_t bands[16];
            vst1q_u8(bands, band);
            const int16_t *d = diff + x;
            stats_bank[0][bands[0]] += d[0];
            stats_bank[1][bands[1]] += d[1];
            stats_bank[2][bands[2]] += d[2];
            stats_bank[3][bands[3]] += d[3];
            stats_bank[4][bands[4]] += d[4];
            stats_bank[5][bands[5]] += d[5];
            stats_bank[6][bands[6]] += d[6];
            stats_bank[7][bands[7]] += d[7];
            stats_bank[8][bands[8]] += d[8];
            stats_bank[9][bands[9]] += d[9];
            stats_bank[10][bands[10]] += d[10];
            stats_bank[11][bands[11]] += d[11];
            stats_bank[12][bands[12]] += d[12];
            stats_bank[13][bands[13]] += d[13];
            stats_bank[14][bands[14]] += d[14];
            stats_bank[15][bands[15]] += d[15];
        }

        if (endX & 15)
        {
            const int x = endX & ~15;
            uint8x16_t band =
                bo_mask_band(x, endX, vshrq_n_u8(vld1q_u8(rec + x), shift));
            bo_acc_histseg_counts(band, idx0, idx1, count_local);

            uint8_t bands[16];
            vst1q_u8(bands, band);
            const int16_t *d = diff + x;
            const int rem = endX - x;
            for (int i = 0; i < rem; i++)
                stats_bank[i][bands[i]] += d[i];
        }

        diff += MAX_CU_SIZE;
        rec += stride;
    }

    for (int i = 0; i < 32; i += 4)
    {
        int32x4_t s = vld1q_s32(stats_bank[0] + i);
        s = vaddq_s32(s, vld1q_s32(stats_bank[1] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[2] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[3] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[4] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[5] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[6] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[7] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[8] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[9] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[10] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[11] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[12] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[13] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[14] + i));
        s = vaddq_s32(s, vld1q_s32(stats_bank[15] + i));
        vst1q_s32(stats + i, vaddq_s32(vld1q_s32(stats + i), s));
        vst1q_s32(count + i,
                  vaddq_s32(vld1q_s32(count + i), vld1q_s32(count_local + i)));
    }
}
#endif

void setupSaoPrimitives_sve2(EncoderPrimitives &p)
{
#if !HIGH_BIT_DEPTH
    p.saoCuStatsBO = saoCuStatsBO_sve2;
#endif
    p.saoCuStatsE0 = saoCuStatsE0_sve2;
    p.saoCuStatsE1 = saoCuStatsE1_sve2;
    p.saoCuStatsE2 = saoCuStatsE2_sve2;
    p.saoCuStatsE3 = saoCuStatsE3_sve2;
}
} // namespace X265_NS
