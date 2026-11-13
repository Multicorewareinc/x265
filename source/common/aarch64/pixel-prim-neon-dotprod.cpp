/*****************************************************************************
 * Copyright (C) 2025 MulticoreWare, Inc
 *
 * Authors: Li Zhang <li.zhang2@arm.com>
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

#include "pixel-prim.h"
#include "mem-neon.h"

#include <arm_neon.h>

namespace
{
#if !HIGH_BIT_DEPTH
template<int size>
void ssimDist_neon_dotprod(const pixel *fenc, uint32_t fStride, const pixel *recon,
                           intptr_t rstride, uint64_t *ssBlock, int shift, uint64_t *ac_k)
{
    // shift is always zero for 8-bit input.
    if (size >= 16)
    {
        uint32x4_t sum_src[2] = { vdupq_n_u32(0), vdupq_n_u32(0) };
        uint32x4_t sum_diff[2] = { vdupq_n_u32(0), vdupq_n_u32(0) };

        for (int h = 0; h < size; h += 2)
        {
            for (int w = 0; w + 16 <= size; w += 16)
            {
                uint8x16_t src[2], rec[2];
                load_u8x16xn<2>(fenc + w, fStride, src);
                load_u8x16xn<2>(recon + w, rstride, rec);

                uint8x16_t diff0 = vabdq_u8(src[0], rec[0]);
                uint8x16_t diff1 = vabdq_u8(src[1], rec[1]);

                sum_src[0] = vdotq_u32(sum_src[0], src[0], src[0]);
                sum_src[1] = vdotq_u32(sum_src[1], src[1], src[1]);
                sum_diff[0] = vdotq_u32(sum_diff[0], diff0, diff0);
                sum_diff[1] = vdotq_u32(sum_diff[1], diff1, diff1);
            }

            fenc += 2 * fStride;
            recon += 2 * rstride;
        }

        sum_src[0] = vaddq_u32(sum_src[0], sum_src[1]);
        sum_diff[0] = vaddq_u32(sum_diff[0], sum_diff[1]);

        *ac_k = vaddlvq_u32(sum_src[0]);
        *ssBlock = vaddlvq_u32(sum_diff[0]);
    }
    if (size == 8)
    {
        uint32x2_t sum_src = vdup_n_u32(0);
        uint32x2_t sum_diff = vdup_n_u32(0);

        for (int h = 0; h < size; ++h)
        {
            uint8x8_t src = vld1_u8(fenc);
            uint8x8_t rec = vld1_u8(recon);
            uint8x8_t diff = vabd_u8(src, rec);

            sum_src = vdot_u32(sum_src, src, src);
            sum_diff = vdot_u32(sum_diff, diff, diff);

            fenc += fStride;
            recon += rstride;
        }

        *ac_k = vaddlv_u32(sum_src);
        *ssBlock = vaddlv_u32(sum_diff);
    }
    if (size == 4)
    {
        uint32x2_t sum_src = vdup_n_u32(0);
        uint32x2_t sum_diff = vdup_n_u32(0);

        for (int h = 0; h < size; h += 2)
        {
            uint8x8_t src = load_u8x4x2(fenc, fStride);
            uint8x8_t rec = load_u8x4x2(recon, rstride);
            uint8x8_t diff = vabd_u8(src, rec);

            sum_src = vdot_u32(sum_src, src, src);
            sum_diff = vdot_u32(sum_diff, diff, diff);

            fenc += 2 * fStride;
            recon += 2 * rstride;
        }

        *ac_k = vaddlv_u32(sum_src);
        *ssBlock = vaddlv_u32(sum_diff);
    }
}

template<int size>
void normFact_neon_dotprod(const pixel *src, uint32_t blockSize, int shift, uint64_t *z_k)
{
    // shift is always zero for 8-bit input.
    if (size >= 16)
    {
        uint32x4_t sum[2] = { vdupq_n_u32(0), vdupq_n_u32(0) };

        for (int h = 0; h < size; h += 2)
        {
            for (int w = 0; w + 16 <= size; w += 16)
            {
                uint8x16_t s[2];
                load_u8x16xn<2>(src + w, blockSize, s);

                sum[0] = vdotq_u32(sum[0], s[0], s[0]);
                sum[1] = vdotq_u32(sum[1], s[1], s[1]);
            }

            src += 2 * blockSize;
        }

        sum[0] = vaddq_u32(sum[0], sum[1]);

        *z_k = vaddlvq_u32(sum[0]);
    }
    if (size == 8)
    {
        uint32x2_t sum = vdup_n_u32(0);

        for (int h = 0; h < size; ++h)
        {
            uint8x8_t s = vld1_u8(src);

            sum = vdot_u32(sum, s, s);

            src += blockSize;
        }

        *z_k = vaddlv_u32(sum);
    }
}

template<int size>
uint64_t pixel_var_neon_dotprod(const uint8_t *pix, intptr_t i_stride)
{
    if (size >= 16)
    {
        uint32x4_t sum[2] = { vdupq_n_u32(0), vdupq_n_u32(0) };
        uint32x4_t sqr[2] = { vdupq_n_u32(0), vdupq_n_u32(0) };

        for (int h = 0; h < size; h += 2)
        {
            for (int w = 0; w + 16 <= size; w += 16)
            {
                uint8x16_t s[2];
                load_u8x16xn<2>(pix + w, i_stride, s);

                sum[0] = vdotq_u32(sum[0], s[0], vdupq_n_u8(1));
                sum[1] = vdotq_u32(sum[1], s[1], vdupq_n_u8(1));

                sqr[0] = vdotq_u32(sqr[0], s[0], s[0]);
                sqr[1] = vdotq_u32(sqr[1], s[1], s[1]);
            }

            pix += 2 * i_stride;
        }

        sum[0] = vaddq_u32(sum[0], sum[1]);
        sqr[0] = vaddq_u32(sqr[0], sqr[1]);

        return vaddvq_u32(sum[0]) + (vaddlvq_u32(sqr[0]) << 32);
    }
    if (size == 8)
    {
        uint16x8_t sum = vdupq_n_u16(0);
        uint32x2_t sqr = vdup_n_u32(0);

        for (int h = 0; h < size; ++h)
        {
            uint8x8_t s = vld1_u8(pix);

            sum = vaddw_u8(sum, s);
            sqr = vdot_u32(sqr, s, s);

            pix += i_stride;
        }

        return vaddvq_u16(sum) + (vaddlv_u32(sqr) << 32);
    }
    if (size == 4) {
        uint16x8_t sum = vdupq_n_u16(0);
        uint32x2_t sqr = vdup_n_u32(0);

        for (int h = 0; h < size; h += 2)
        {
            uint8x8_t s = load_u8x4x2(pix, i_stride);

            sum = vaddw_u8(sum, s);
            sqr = vdot_u32(sqr, s, s);

            pix += 2 * i_stride;
        }

        return vaddvq_u16(sum) + (vaddlv_u32(sqr) << 32);
    }
}
#endif // !HIGH_BIT_DEPTH
}

namespace X265_NS
{
#if HIGH_BIT_DEPTH
void setupPixelPrimitives_neon_dotprod(EncoderPrimitives &)
{
}
#else // !HIGH_BIT_DEPTH
void setupPixelPrimitives_neon_dotprod(EncoderPrimitives &p)
{
    p.cu[BLOCK_4x4].var   = pixel_var_neon_dotprod<4>;
    p.cu[BLOCK_8x8].var   = pixel_var_neon_dotprod<8>;
    p.cu[BLOCK_16x16].var = pixel_var_neon_dotprod<16>;
    p.cu[BLOCK_32x32].var = pixel_var_neon_dotprod<32>;
    p.cu[BLOCK_64x64].var = pixel_var_neon_dotprod<64>;

    p.cu[BLOCK_4x4].ssimDist = ssimDist_neon_dotprod<4>;
    p.cu[BLOCK_8x8].ssimDist = ssimDist_neon_dotprod<8>;
    p.cu[BLOCK_16x16].ssimDist = ssimDist_neon_dotprod<16>;
    p.cu[BLOCK_32x32].ssimDist = ssimDist_neon_dotprod<32>;
    p.cu[BLOCK_64x64].ssimDist = ssimDist_neon_dotprod<64>;

    p.cu[BLOCK_8x8].normFact = normFact_neon_dotprod<8>;
    p.cu[BLOCK_16x16].normFact = normFact_neon_dotprod<16>;
    p.cu[BLOCK_32x32].normFact = normFact_neon_dotprod<32>;
    p.cu[BLOCK_64x64].normFact = normFact_neon_dotprod<64>;
}
#endif // HIGH_BIT_DEPTH
}
