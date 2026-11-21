#include "common.h"
#include "primitives.h"


#if HAVE_NEON
#include "arm64-utils.h"
#include "mem-neon.h"
#include <arm_neon.h>

using namespace X265_NS;

namespace
{


template<int tuSize>
void intraFilter_neon(const pixel* samples, pixel* filtered) /* 1:2:1 filtering of left and top reference samples */
{
    const int tuSize2 = tuSize << 1;
    pixel topLeft = samples[0], topLast = samples[tuSize2], leftLast = samples[tuSize2 + tuSize2];

    uint16x8_t two_vec = vdupq_n_u16(2);
#if !HIGH_BIT_DEPTH
    {
        // Avoid reading samples[-1]. build it from the loaded sample1 instead.
        // The first value is unused since filtered[0] is overwritten below anyway.
        {
            uint8x8_t raw1 = vld1_u8(&samples[0]);
            uint16x8_t sample1 = vmovl_u8(raw1);
            uint16x8_t sample2 = vmovl_u8(vext_u8(raw1, raw1, 7));
            uint16x8_t sample3 = vmovl_u8(vld1_u8(&samples[1]));

            uint16x8_t result1 = vaddq_u16(vshlq_n_u16(sample1,1), sample2 );
            uint16x8_t result2 = vaddq_u16(sample3, two_vec);
            uint16x8_t result3 = vaddq_u16(result1,result2);
            vst1_u8(&filtered[0] , vmovn_u16(vshrq_n_u16(result3, 2)));
        }
        for(int i = 8; i < tuSize2 + tuSize2; i+=8)
         {
            uint16x8_t sample1 = vmovl_u8(vld1_u8(&samples[i]));
            uint16x8_t sample2 = vmovl_u8(vld1_u8(&samples[i-1]));
            uint16x8_t sample3 = vmovl_u8(vld1_u8(&samples[i+1]));

            uint16x8_t result1 = vaddq_u16(vshlq_n_u16(sample1,1), sample2 );
            uint16x8_t result2 = vaddq_u16(sample3, two_vec);
            uint16x8_t result3 = vaddq_u16(result1,result2);
            vst1_u8(&filtered[i] , vmovn_u16(vshrq_n_u16(result3, 2)));
        }
    }
#else
    {
        {
            uint16x8_t sample1 = vld1q_u16(&samples[0]);
            uint16x8_t sample2 = vextq_u16(sample1, sample1, 7);
            uint16x8_t sample3 = vld1q_u16(&samples[1]);

            uint16x8_t result1 = vaddq_u16(vshlq_n_u16(sample1,1), sample2 );
            uint16x8_t result2 = vaddq_u16(sample3, two_vec);
            uint16x8_t result3 = vaddq_u16(result1,result2);
            vst1q_u16(&filtered[0] , vshrq_n_u16(result3, 2));
        }
        for(int i = 8; i < tuSize2 + tuSize2; i+=8)
        {
            uint16x8_t sample1 = vld1q_u16(&samples[i]);
            uint16x8_t sample2 = vld1q_u16(&samples[i-1]);
            uint16x8_t sample3 = vld1q_u16(&samples[i+1]);

            uint16x8_t result1 = vaddq_u16(vshlq_n_u16(sample1,1), sample2 );
            uint16x8_t result2 = vaddq_u16(sample3, two_vec);
            uint16x8_t result3 = vaddq_u16(result1,result2);
            vst1q_u16(&filtered[i] , vshrq_n_u16(result3, 2));
        }
    }
#endif
    // filtering top
    filtered[tuSize2] = topLast;

    // filtering top-left
    filtered[0] = ((topLeft << 1) + samples[1] + samples[tuSize2 + 1] + 2) >> 2;

    // filtering left
    filtered[tuSize2 + 1] = ((samples[tuSize2 + 1] << 1) + topLeft + samples[tuSize2 + 2] + 2) >> 2;
    filtered[tuSize2 + tuSize2] = leftLast;
}

template<int width, bool bTranspose, bool neighboursSwapped = false>
void intra_pred_ang_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix0, int dirMode, int bFilter)
{
    int width2 = width << 1;
    // Flip the neighbours in the horizontal case.
    int horMode = dirMode < 18;
    pixel neighbourBuf[129];
    const pixel *srcPix = srcPix0;

    if (horMode && !neighboursSwapped)
    {
        neighbourBuf[0] = srcPix[0];
        //for (int i = 0; i < width << 1; i++)
        //{
        //    neighbourBuf[1 + i] = srcPix[width2 + 1 + i];
        //    neighbourBuf[width2 + 1 + i] = srcPix[1 + i];
        //}
        memcpy(&neighbourBuf[1], &srcPix[width2 + 1], sizeof(pixel) * (width << 1));
        memcpy(&neighbourBuf[width2 + 1], &srcPix[1], sizeof(pixel) * (width << 1));
        srcPix = neighbourBuf;
    }

    // Intra prediction angle and inverse angle tables.
    const int8_t angleTable[17] = { -32, -26, -21, -17, -13, -9, -5, -2, 0, 2, 5, 9, 13, 17, 21, 26, 32 };
    const int16_t invAngleTable[8] = { 4096, 1638, 910, 630, 482, 390, 315, 256 };

    // Get the prediction angle.
    int angleOffset = horMode ? 10 - dirMode : dirMode - 26;
    int angle = angleTable[8 + angleOffset];

    // Vertical Prediction.
    if (!angle)
    {
        for (int y = 0; y < width; y++)
        {
            memcpy(&dst[y * dstStride], srcPix + 1, sizeof(pixel)*width);
        }
        if (bFilter)
        {
            int topLeft = srcPix[0], top = srcPix[1];
            for (int y = 0; y < width; y++)
            {
                dst[y * dstStride] = x265_clip((int16_t)(top + ((srcPix[width2 + 1 + y] - topLeft) >> 1)));
            }
        }
    }
    else // Angular prediction.
    {
        // Get the reference pixels. The reference base is the first pixel to the top (neighbourBuf[1]).
        pixel refBuf[64];
        const pixel *ref;

        // Use the projected left neighbours and the top neighbours.
        if (angle < 0)
        {
            // Number of neighbours projected.
            int nbProjected = -((width * angle) >> 5) - 1;
            pixel *ref_pix = refBuf + nbProjected + 1;

            // Project the neighbours.
            int invAngle = invAngleTable[- angleOffset - 1];
            int invAngleSum = 128;
            for (int i = 0; i < nbProjected; i++)
            {
                invAngleSum += invAngle;
                ref_pix[- 2 - i] = srcPix[width2 + (invAngleSum >> 8)];
            }

            // Copy the top-left and top pixels.
            //for (int i = 0; i < width + 1; i++)
            //ref_pix[-1 + i] = srcPix[i];

            memcpy(&ref_pix[-1], srcPix, (width + 1)*sizeof(pixel));
            ref = ref_pix;
        }
        else // Use the top and top-right neighbours.
        {
            ref = srcPix + 1;
        }

        // Pass every row.
        int angleSum = 0;
        for (int y = 0; y < width; y++)
        {
            angleSum += angle;
            int offset = angleSum >> 5;
            int fraction = angleSum & 31;

            if (fraction) // Interpolate
            {
                if (width >= 8 && sizeof(pixel) == 1)
                {
                    // We have to cast to the 'real' type so that this block
                    // will compile for both low and high bitdepth.
                    const uint8_t *ref_u8 = (const uint8_t *)ref + offset;
                    uint8_t *dst_u8 = (uint8_t *)dst;

                    // f0 and f1 are unsigned (fraction is in range [0, 31]).
                    const uint8x8_t f0 = vdup_n_u8(32 - fraction);
                    const uint8x8_t f1 = vdup_n_u8(fraction);
                    for (int x = 0; x < width; x += 8)
                    {
                        uint8x8_t in0 = vld1_u8(ref_u8 + x);
                        uint8x8_t in1 = vld1_u8(ref_u8 + x + 1);
                        uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), in0, f0);
                        lo = vmlal_u8(lo, in1, f1);
                        uint8x8_t res = vshrn_n_u16(lo, 5);
                        vst1_u8(dst_u8 + y * dstStride + x, res);
                    }
                }
                else if (width >= 4 && sizeof(pixel) == 2)
                {
                    // We have to cast to the 'real' type so that this block
                    // will compile for both low and high bitdepth.
                    const uint16_t *ref_u16 = (const uint16_t *)ref + offset;
                    uint16_t *dst_u16 = (uint16_t *)dst;

                    // f0 and f1 are unsigned (fraction is in range [0, 31]).
                    const uint16x4_t f0 = vdup_n_u16(32 - fraction);
                    const uint16x4_t f1 = vdup_n_u16(fraction);
                    for (int x = 0; x < width; x += 4)
                    {
                        uint16x4_t in0 = vld1_u16(ref_u16 + x);
                        uint16x4_t in1 = vld1_u16(ref_u16 + x + 1);
                        uint32x4_t lo = vmlal_u16(vdupq_n_u32(16), in0, f0);
                        lo = vmlal_u16(lo, in1, f1);
                        uint16x4_t res = vshrn_n_u32(lo, 5);
                        vst1_u16(dst_u16 + y * dstStride + x, res);
                    }
                }
                else
                {
                    for (int x = 0; x < width; x++)
                    {
                        dst[y * dstStride + x] = (pixel)(((32 - fraction) * ref[offset + x] + fraction * ref[offset + x + 1] + 16) >> 5);
                    }
                }
            }
            else // Copy.
            {
                memcpy(&dst[y * dstStride], &ref[offset], sizeof(pixel)*width);
            }
        }
    }

    // Flip for horizontal.
    if (horMode && bTranspose)
    {
        if (width == 8)
        {
            transpose8x8(dst, dst, dstStride, dstStride);
        }
        else if (width == 16)
        {
            transpose16x16(dst, dst, dstStride, dstStride);
        }
        else if (width == 32)
        {
            transpose32x32(dst, dst, dstStride, dstStride);
        }
        else
        {
            for (int y = 0; y < width - 1; y++)
            {
                for (int x = y + 1; x < width; x++)
                {
                    pixel tmp              = dst[y * dstStride + x];
                    dst[y * dstStride + x] = dst[x * dstStride + y];
                    dst[x * dstStride + y] = tmp;
                }
            }
        }
    }
}

#if !HIGH_BIT_DEPTH
template<int width>
void intra_pred_ang26_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                           int /*dirMode*/, int bFilter)
{
    const int width2 = width << 1;

    if (width == 4)
    {
        uint32_t top;
        memcpy(&top, srcPix + 1, 4);
        pixel *d = dst;
        for (int y = 0; y < 4; y++, d += dstStride)
            memcpy(d, &top, 4);

        if (bFilter)
        {
            const int topLeft = srcPix[0], t0 = srcPix[1];
            d = dst;
            for (int y = 0; y < 4; y++, d += dstStride)
                d[0] = (pixel)x265_clip((int16_t)(t0 + ((srcPix[width2 + 1 + y] - topLeft) >> 1)));
        }
    }
    else if (width == 8)
    {
        const uint8x8_t top = vld1_u8(srcPix + 1);
        pixel *d = dst;
        for (int y = 0; y < 8; y++, d += dstStride)
            vst1_u8(d, top);

        if (bFilter)
        {
            const int16x8_t tl = vdupq_n_s16(srcPix[0]);
            const int16x8_t t0 = vdupq_n_s16(srcPix[1]);
            const int16x8_t left =
                vreinterpretq_s16_u16(vmovl_u8(vld1_u8(srcPix + width2 + 1)));
            uint8_t col[8];
            vst1_u8(col, vqmovun_s16(vaddq_s16(t0, vhsubq_s16(left, tl))));
            d = dst;
            for (int y = 0; y < 8; y++, d += dstStride)
                d[0] = col[y];
        }
    }
    else if (width == 16)
    {
        const uint8x16_t top = vld1q_u8(srcPix + 1);
        pixel *d = dst;
        for (int y = 0; y < 16; y++, d += dstStride)
            vst1q_u8(d, top);

        if (bFilter)
        {
            const int16x8_t tl = vdupq_n_s16(srcPix[0]);
            const int16x8_t t0 = vdupq_n_s16(srcPix[1]);
            const int16x8_t left0 =
                vreinterpretq_s16_u16(vmovl_u8(vld1_u8(srcPix + width2 + 1)));
            const int16x8_t left1 =
                vreinterpretq_s16_u16(vmovl_u8(vld1_u8(srcPix + width2 + 9)));
            uint8_t col[16];
            vst1_u8(col, vqmovun_s16(vaddq_s16(t0, vhsubq_s16(left0, tl))));
            vst1_u8(col + 8, vqmovun_s16(vaddq_s16(t0, vhsubq_s16(left1, tl))));
            d = dst;
            for (int y = 0; y < 16; y++, d += dstStride)
                d[0] = col[y];
        }
    }
    else
    {
        const uint8x16_t top0 = vld1q_u8(srcPix + 1);
        const uint8x16_t top1 = vld1q_u8(srcPix + 17);
        pixel *d = dst;
        for (int y = 0; y < 32; y++, d += dstStride)
        {
            vst1q_u8(d, top0);
            vst1q_u8(d + 16, top1);
        }
    }
}

template<int width>
static void intra_pred_ang10_neon(pixel *dst, intptr_t dstStride,
                                  const pixel *srcPix, int, int bFilter)
{
    const int width2 = width << 1;
    const pixel *left = srcPix + width2 + 1;
    if (width == 8)
    {
        pixel *d = dst;
        for (int y = 0; y < 8; y++, d += dstStride)
            vst1_u8(d, vdup_n_u8(left[y]));
        if (bFilter)
        {
            const int16x8_t tl = vdupq_n_s16(srcPix[0]);
            const int16x8_t l0 = vdupq_n_s16(left[0]);
            const int16x8_t top =
                vreinterpretq_s16_u16(vmovl_u8(vld1_u8(srcPix + 1)));
            vst1_u8(dst, vqmovun_s16(vaddq_s16(l0, vshrq_n_s16(vsubq_s16(top, tl), 1))));
        }
    }
    else if (width == 16)
    {
        pixel *d = dst;
        for (int y = 0; y < 16; y++, d += dstStride)
            vst1q_u8(d, vdupq_n_u8(left[y]));
        if (bFilter)
        {
            const int16x8_t tl = vdupq_n_s16(srcPix[0]);
            const int16x8_t l0 = vdupq_n_s16(left[0]);
            const int16x8_t top0 =
                vreinterpretq_s16_u16(vmovl_u8(vld1_u8(srcPix + 1)));
            const int16x8_t top1 =
                vreinterpretq_s16_u16(vmovl_u8(vld1_u8(srcPix + 9)));
            vst1_u8(dst, vqmovun_s16(vaddq_s16(l0, vshrq_n_s16(vsubq_s16(top0, tl), 1))));
            vst1_u8(dst + 8, vqmovun_s16(vaddq_s16(l0, vshrq_n_s16(vsubq_s16(top1, tl), 1))));
        }
    }
    else
    {
        pixel *d = dst;
        for (int y = 0; y < 32; y++, d += dstStride)
        {
            const uint8x16_t row = vdupq_n_u8(left[y]);
            vst1q_u8(d, row);
            vst1q_u8(d + 16, row);
        }
    }
}

template<>
void intra_pred_ang10_neon<4>(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                              int /*dirMode*/, int bFilter)
{
    const int width = 4;
    const int width2 = width << 1;
    const pixel *left = srcPix + width2 + 1;
    pixel *d = dst;
    for (int y = 0; y < 4; y++, d += dstStride)
    {
        const uint32_t row = (uint32_t)left[y] * 0x01010101u;
        memcpy(d, &row, 4);
    }

    if (bFilter)
    {
        const int topLeft = srcPix[0], l0 = left[0];
        for (int x = 0; x < 4; x++)
            dst[x] = (pixel)x265_clip((int16_t)(l0 + ((srcPix[1 + x] - topLeft) >> 1)));
    }
}

template<int width>
static inline void intra_pred_copy_diagonal_neon(pixel *dst, intptr_t dstStride, const pixel *ref)
{
    pixel *d = dst;

    if (width == 4)
    {
        for (int y = 0; y < 4; y++, d += dstStride)
        {
            uint32_t v;
            memcpy(&v, ref + y, 4);
            memcpy(d, &v, 4);
        }
    }
    else if (width == 8)
    {
        for (int y = 0; y < 8; y++, d += dstStride)
            vst1_u8(d, vld1_u8(ref + y));
    }
    else if (width == 16)
    {
        for (int y = 0; y < 16; y++, d += dstStride)
            vst1q_u8(d, vld1q_u8(ref + y));
    }
    else
    {
        for (int y = 0; y < 32; y++, d += dstStride)
        {
            vst1q_u8(d, vld1q_u8(ref + y));
            vst1q_u8(d + 16, vld1q_u8(ref + y + 16));
        }
    }
}

template<int width>
void intra_pred_ang34_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                           int /*dirMode*/, int /*bFilter*/)
{
    intra_pred_copy_diagonal_neon<width>(dst, dstStride, srcPix + 2);
}

template<int width>
void intra_pred_ang2_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                          int /*dirMode*/, int /*bFilter*/)
{
    intra_pred_copy_diagonal_neon<width>(dst, dstStride, srcPix + 2 * width + 2);
}

static const int8_t s_angleTable[17] =
{
    -32, -26, -21, -17, -13, -9, -5, -2, 0, 2, 5, 9, 13, 17, 21, 26, 32
};
static const int16_t s_invAngleTable[8] =
{
    4096, 1638, 910, 630, 482, 390, 315, 256
};

template<int width>
void intra_pred_ang9_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix0,
                          int /*dirMode*/, int /*bFilter*/)
{
    const int width2 = width << 1;
    const uint8_t *left = srcPix0 + width2 + 1;

    ALIGN_VAR_16(uint8_t, f0arr[32]);
    ALIGN_VAR_16(uint8_t, f1arr[32]);
    for (int x = 0; x < width; x++)
    {
        const int frac = ((x + 1) * 2) & 31;
        f0arr[x] = (uint8_t)(32 - frac);
        f1arr[x] = (uint8_t)frac;
    }

    pixel *d = dst;
    if (width == 8)
    {
        const uint8x8_t f0 = vld1_u8(f0arr);
        const uint8x8_t f1 = vld1_u8(f1arr);
        for (int y = 0; y < 8; y++, d += dstStride)
        {
            const uint8x8_t in0 = vdup_n_u8(left[y]);
            const uint8x8_t in1 = vdup_n_u8(left[y + 1]);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), in0, f0);
            lo = vmlal_u8(lo, in1, f1);
            vst1_u8(d, vshrn_n_u16(lo, 5));
        }
    }
    else if (width == 16)
    {
        /* x=0..14 offset 0; x=15 offset 1. */
        const uint8x16_t f0 = vld1q_u8(f0arr);
        const uint8x16_t f1 = vld1q_u8(f1arr);
        for (int y = 0; y < 16; y++, d += dstStride)
        {
            const uint8x16_t in0 = vdupq_n_u8(left[y]);
            const uint8x16_t in1 = vdupq_n_u8(left[y + 1]);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), vget_low_u8(f0));
            lo = vmlal_u8(lo, vget_low_u8(in1), vget_low_u8(f1));
            uint16x8_t hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0), vget_high_u8(f0));
            hi = vmlal_u8(hi, vget_high_u8(in1), vget_high_u8(f1));
            vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
            d[15] = left[y + 1];
        }
    }
    else
    {
        /* x=0..14 offset 0; x=15..30 offset 1; x=31 offset 2. */
        const uint8x16_t f0a = vld1q_u8(f0arr);
        const uint8x16_t f1a = vld1q_u8(f1arr);
        const uint8x16_t f0b = vld1q_u8(f0arr + 16);
        const uint8x16_t f1b = vld1q_u8(f1arr + 16);
        for (int y = 0; y < 32; y++, d += dstStride)
        {
            const uint8x16_t in0a = vdupq_n_u8(left[y]);
            const uint8x16_t in1a = vdupq_n_u8(left[y + 1]);
            const uint8x16_t in0b = vdupq_n_u8(left[y + 1]);
            const uint8x16_t in1b = vdupq_n_u8(left[y + 2]);

            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0a), vget_low_u8(f0a));
            lo = vmlal_u8(lo, vget_low_u8(in1a), vget_low_u8(f1a));
            uint16x8_t hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0a), vget_high_u8(f0a));
            hi = vmlal_u8(hi, vget_high_u8(in1a), vget_high_u8(f1a));
            vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
            d[15] = left[y + 1];

            lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0b), vget_low_u8(f0b));
            lo = vmlal_u8(lo, vget_low_u8(in1b), vget_low_u8(f1b));
            hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0b), vget_high_u8(f0b));
            hi = vmlal_u8(hi, vget_high_u8(in1b), vget_high_u8(f1b));
            vst1q_u8(d + 16, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
            d[31] = left[y + 2];
        }
    }
}

template<int width, int angle>
void intra_pred_ang_hor_pos_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix0,
                                 int /*dirMode*/, int /*bFilter*/)
{
    const int width2 = width << 1;
    const uint8_t *left = srcPix0 + width2 + 1;

    ALIGN_VAR_16(uint8_t, idx0[32]);
    ALIGN_VAR_16(uint8_t, idx1[32]);
    ALIGN_VAR_16(uint8_t, f0arr[32]);
    ALIGN_VAR_16(uint8_t, f1arr[32]);
    for (int x = 0; x < width; x++)
    {
        const int angleSum = (x + 1) * angle;
        const int off = angleSum >> 5;
        const int frac = angleSum & 31;
        idx0[x] = (uint8_t)off;
        idx1[x] = (uint8_t)(off + 1);
        f0arr[x] = (uint8_t)(32 - frac);
        f1arr[x] = (uint8_t)frac;
    }

    pixel *d = dst;
    if (width == 8)
    {
        const uint8x8_t i0 = vld1_u8(idx0);
        const uint8x8_t i1 = vld1_u8(idx1);
        const uint8x8_t f0 = vld1_u8(f0arr);
        const uint8x8_t f1 = vld1_u8(f1arr);
        for (int y = 0; y < 8; y++, d += dstStride)
        {
            // For these 8x8 angles, both lookup indices are in [0, 7].
            const uint8x16_t win = vcombine_u8(vld1_u8(left + y), vdup_n_u8(0));
            const uint8x8_t in0 = vqtbl1_u8(win, i0);
            const uint8x8_t in1 = vqtbl1_u8(win, i1);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), in0, f0);
            lo = vmlal_u8(lo, in1, f1);
            vst1_u8(d, vshrn_n_u16(lo, 5));
        }
    }
    else if (width == 16)
    {
        const uint8x16_t i0 = vld1q_u8(idx0);
        const uint8x16_t i1 = vld1q_u8(idx1);
        const uint8x16_t f0 = vld1q_u8(f0arr);
        const uint8x16_t f1 = vld1q_u8(f1arr);
        for (int y = 0; y < 16; y++, d += dstStride)
        {
            const uint8x16_t win = vld1q_u8(left + y);
            const uint8x16_t in0 = vqtbl1q_u8(win, i0);
            const uint8x16_t in1 = vqtbl1q_u8(win, i1);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), vget_low_u8(f0));
            lo = vmlal_u8(lo, vget_low_u8(in1), vget_low_u8(f1));
            uint16x8_t hi = vmlal_high_u8(vdupq_n_u16(16), in0, f0);
            hi = vmlal_high_u8(hi, in1, f1);
            vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
        }
    }
    else
    {
        const int maxOff = (width * angle) >> 5;
        const uint8x16_t i0a = vld1q_u8(idx0);
        const uint8x16_t i1a = vld1q_u8(idx1);
        const uint8x16_t f0a = vld1q_u8(f0arr);
        const uint8x16_t f1a = vld1q_u8(f1arr);
        const uint8x16_t i0b = vld1q_u8(idx0 + 16);
        const uint8x16_t i1b = vld1q_u8(idx1 + 16);
        const uint8x16_t f0b = vld1q_u8(f0arr + 16);
        const uint8x16_t f1b = vld1q_u8(f1arr + 16);
        for (int y = 0; y < 32; y++, d += dstStride)
        {
            uint8x16_t in0, in1;
            uint16x8_t lo, hi;
            if (maxOff + 2 <= 16)
            {
                const uint8x16_t win = vld1q_u8(left + y);
                in0 = vqtbl1q_u8(win, i0a);
                in1 = vqtbl1q_u8(win, i1a);
                lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), vget_low_u8(f0a));
                lo = vmlal_u8(lo, vget_low_u8(in1), vget_low_u8(f1a));
                hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0), vget_high_u8(f0a));
                hi = vmlal_u8(hi, vget_high_u8(in1), vget_high_u8(f1a));
                vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));

                in0 = vqtbl1q_u8(win, i0b);
                in1 = vqtbl1q_u8(win, i1b);
            }
            else
            {
                uint8x16x2_t win;
                win.val[0] = vld1q_u8(left + y);
                win.val[1] = vld1q_u8(left + y + 16);
                in0 = vqtbl2q_u8(win, i0a);
                in1 = vqtbl2q_u8(win, i1a);
                lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), vget_low_u8(f0a));
                lo = vmlal_u8(lo, vget_low_u8(in1), vget_low_u8(f1a));
                hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0), vget_high_u8(f0a));
                hi = vmlal_u8(hi, vget_high_u8(in1), vget_high_u8(f1a));
                vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));

                in0 = vqtbl2q_u8(win, i0b);
                in1 = vqtbl2q_u8(win, i1b);
            }
            lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), vget_low_u8(f0b));
            lo = vmlal_u8(lo, vget_low_u8(in1), vget_low_u8(f1b));
            hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0), vget_high_u8(f0b));
            hi = vmlal_u8(hi, vget_high_u8(in1), vget_high_u8(f1b));
            vst1q_u8(d + 16, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
        }
    }
}

template<int width>
void intra_pred_ang11_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix0,
                           int /*dirMode*/, int /*bFilter*/)
{
    const int width2 = width << 1;
    const pixel *left = srcPix0 + width2 + 1;
    pixel refBuf[33];
    refBuf[0] = srcPix0[0];
    memcpy(refBuf + 1, left, width * sizeof(pixel));
    const uint8_t *ref = refBuf;

    ALIGN_VAR_16(uint8_t, f0arr[32]);
    ALIGN_VAR_16(uint8_t, f1arr[32]);
    for (int x = 0; x < width; x++)
    {
        const int angleSum = (x + 1) * (-2);
        const int frac = angleSum & 31;
        f0arr[x] = (uint8_t)(32 - frac);
        f1arr[x] = (uint8_t)frac;
    }

    pixel *d = dst;
    if (width == 8)
    {
        const uint8x8_t f0 = vld1_u8(f0arr);
        const uint8x8_t f1 = vld1_u8(f1arr);
        for (int y = 0; y < 8; y++, d += dstStride)
        {
            const uint8x8_t in0 = vdup_n_u8(ref[y]);
            const uint8x8_t in1 = vdup_n_u8(ref[y + 1]);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), in0, f0);
            lo = vmlal_u8(lo, in1, f1);
            vst1_u8(d, vshrn_n_u16(lo, 5));
        }
    }
    else if (width == 16)
    {
        const uint8x16_t f0 = vld1q_u8(f0arr);
        const uint8x16_t f1 = vld1q_u8(f1arr);
        for (int y = 0; y < 16; y++, d += dstStride)
        {
            const uint8x16_t in0 = vdupq_n_u8(ref[y]);
            const uint8x16_t in1 = vdupq_n_u8(ref[y + 1]);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), vget_low_u8(f0));
            lo = vmlal_u8(lo, vget_low_u8(in1), vget_low_u8(f1));
            uint16x8_t hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0), vget_high_u8(f0));
            hi = vmlal_u8(hi, vget_high_u8(in1), vget_high_u8(f1));
            vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
        }
    }
    else
    {
        const uint8x16_t f0a = vld1q_u8(f0arr);
        const uint8x16_t f1a = vld1q_u8(f1arr);
        const uint8x16_t f0b = vld1q_u8(f0arr + 16);
        const uint8x16_t f1b = vld1q_u8(f1arr + 16);
        const uint8_t proj = srcPix0[16]; /* Projected sample at ref_pix[-2]. */
        for (int y = 0; y < 32; y++, d += dstStride)
        {
            const uint8x16_t in0a = vdupq_n_u8(ref[y]);
            const uint8x16_t in1a = vdupq_n_u8(ref[y + 1]);
            const uint8_t u0 = (y == 0) ? proj : ref[y - 1];
            const uint8x16_t in0b = vdupq_n_u8(u0);
            const uint8x16_t in1b = vdupq_n_u8(ref[y]);

            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0a), vget_low_u8(f0a));
            lo = vmlal_u8(lo, vget_low_u8(in1a), vget_low_u8(f1a));
            uint16x8_t hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0a), vget_high_u8(f0a));
            hi = vmlal_u8(hi, vget_high_u8(in1a), vget_high_u8(f1a));
            vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));

            lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0b), vget_low_u8(f0b));
            lo = vmlal_u8(lo, vget_low_u8(in1b), vget_low_u8(f1b));
            hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0b), vget_high_u8(f0b));
            hi = vmlal_u8(hi, vget_high_u8(in1b), vget_high_u8(f1b));
            vst1q_u8(d + 16, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
        }
    }
}

// Interpolate one 16-pixel row.
static inline void ang_pred_row_u8_neon(const uint8_t *r, int fraction, uint8_t *dst)
{
    if (!fraction)
    {
        memcpy(dst, r, 16);
        return;
    }
    const uint8x8_t f0 = vdup_n_u8((uint8_t)(32 - fraction));
    const uint8x8_t f1 = vdup_n_u8((uint8_t)fraction);
    uint8x16_t in0 = vld1q_u8(r);
    uint8x16_t in1 = vld1q_u8(r + 1);
    uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), f0);
    lo = vmlal_u8(lo, vget_low_u8(in1), f1);
    uint16x8_t hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0), f0);
    hi = vmlal_u8(hi, vget_high_u8(in1), f1);
    vst1q_u8(dst, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
}

template<int dirMode>
void intra_pred_ang_hor8_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix0,
                              int /*dirMode*/, int /*bFilter*/)
{
    const int angleOffset = 10 - dirMode;
    const int angle = s_angleTable[8 + angleOffset];
    const int minOffset = (8 * angle) >> 5;
    const int nbProjected = -minOffset - 1;

    // The table load below reads 16 bytes, including unused tail lanes.
    pixel refBuf[32] = {};
    pixel *ref_pix = refBuf + nbProjected + 1;
    int invAngle = s_invAngleTable[-angleOffset - 1];
    int invAngleSum = 128;
    for (int i = 0; i < nbProjected; i++)
    {
        invAngleSum += invAngle;
        ref_pix[-2 - i] = srcPix0[invAngleSum >> 8];
    }
    ref_pix[-1] = srcPix0[0];
    memcpy(ref_pix, srcPix0 + 17, 8 * sizeof(pixel));

    ALIGN_VAR_16(uint8_t, idxBase[8]);
    ALIGN_VAR_16(uint8_t, f0arr[8]);
    ALIGN_VAR_16(uint8_t, f1arr[8]);
    for (int x = 0; x < 8; x++)
    {
        const int angleSum = (x + 1) * angle;
        const int frac = angleSum & 31;
        idxBase[x] = (uint8_t)((angleSum >> 5) - minOffset);
        f0arr[x] = (uint8_t)(32 - frac);
        f1arr[x] = (uint8_t)frac;
    }

    const uint8x16_t tbl = vld1q_u8(ref_pix + minOffset);
    const uint8x8_t f0 = vld1_u8(f0arr);
    const uint8x8_t f1 = vld1_u8(f1arr);
    const uint8x8_t base = vld1_u8(idxBase);
    const uint8x16_t baseq = vcombine_u8(base, base);

    pixel *d = dst;
    for (int y = 0; y < 8; y++, d += dstStride)
    {
        const uint8x16_t idxq = vaddq_u8(baseq, vdupq_n_u8((uint8_t)y));
        uint8x16_t in0 = vqtbl1q_u8(tbl, idxq);
        uint8x16_t in1 = vqtbl1q_u8(tbl, vaddq_u8(idxq, vdupq_n_u8(1)));
        uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), f0);
        lo = vmlal_u8(lo, vget_low_u8(in1), f1);
        vst1_u8(d, vshrn_n_u16(lo, 5));
    }
}

template<int dirMode>
void intra_pred_ang_hor16_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix0,
                               int /*dirMode*/, int /*bFilter*/)
{
    const int angleOffset = 10 - dirMode;
    const int angle = s_angleTable[8 + angleOffset];
    const int minOffset = (16 * angle) >> 5;
    const int nbProjected = -minOffset - 1;

    pixel refBuf[64];
    pixel *ref_pix = refBuf + nbProjected + 1;
    int invAngle = s_invAngleTable[-angleOffset - 1];
    int invAngleSum = 128;
    for (int i = 0; i < nbProjected; i++)
    {
        invAngleSum += invAngle;
        ref_pix[-2 - i] = srcPix0[invAngleSum >> 8];
    }
    ref_pix[-1] = srcPix0[0];
    memcpy(ref_pix, srcPix0 + 33, 16 * sizeof(pixel));

    ALIGN_VAR_32(pixel, tmp[16 * 16]);
    int angleSum = 0;
    for (int x = 0; x < 16; x++)
    {
        angleSum += angle;
        ang_pred_row_u8_neon(ref_pix + (angleSum >> 5), angleSum & 31,
                             tmp + x * 16);
    }
    transpose16x16(dst, tmp, dstStride, 16);
}

// Supported block widths: 8, 16, 32.
template<int width>
void intra_pred_ang18_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                           int /*dirMode*/, int /*bFilter*/)
{
    const int width2 = width << 1;
    pixel refBuf[2 * width + 2];
    pixel *ref_pix = refBuf + width;

    /* invAngle = 256: (128 + 256 * (i + 1)) >> 8 == i + 1. */
    for (int i = 0; i < width - 1; i++)
        ref_pix[-2 - i] = srcPix[width2 + 1 + i];
    memcpy(&ref_pix[-1], srcPix, (width + 1) * sizeof(pixel));

    const uint8_t *ref = ref_pix;
    pixel *d = dst;
    for (int y = 0; y < width; y++, d += dstStride)
    {
        const uint8_t *r = ref - (y + 1);
        if (width == 8)
            vst1_u8(d, vld1_u8(r));
        else if (width == 16)
            vst1q_u8(d, vld1q_u8(r));
        else
        {
            vst1q_u8(d, vld1q_u8(r));
            vst1q_u8(d + 16, vld1q_u8(r + 16));
        }
    }
}

// Calculate each 16x16 row's weights directly without temporary arrays.
template<int angle>
static void intra_pred_ang_vpos16_direct_neon(pixel *dst, intptr_t stride, const pixel *srcPix,
                                              int /*dirMode*/, int /*bFilter*/)
{
    const uint8_t *ref = srcPix + 1;
    int position = 0;
    for (int y = 0; y < 16; y++, dst += stride)
    {
        position += angle;
        const int fraction = position & 31;
        const uint8_t *r = ref + (position >> 5);
        const uint8x16_t a = vld1q_u8(r);
        const uint8x16_t b = vld1q_u8(r + 1);
        const uint8x8_t w0 = vdup_n_u8((uint8_t)(32 - fraction));
        const uint8x8_t w1 = vdup_n_u8((uint8_t)fraction);
        uint16x8_t lo = vmull_u8(vget_low_u8(a), w0);
        lo = vmlal_u8(lo, vget_low_u8(b), w1);
        uint16x8_t hi = vmull_u8(vget_high_u8(a), w0);
        hi = vmlal_u8(hi, vget_high_u8(b), w1);
        vst1q_u8(dst, vcombine_u8(vrshrn_n_u16(lo, 5), vrshrn_n_u16(hi, 5)));
    }
}

// Supported block width: 8.
template<int width, int angle>
void intra_pred_ang_vpos_t_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                                int /*dirMode*/, int /*bFilter*/)
{

    const uint8_t *ref = srcPix + 1;
    ALIGN_VAR_16(uint8_t, f0arr[8]);
    ALIGN_VAR_16(uint8_t, f1arr[8]);
    for (int y = 0; y < width; y++)
    {
        const int frac = ((y + 1) * angle) & 31;
        f0arr[y] = (uint8_t)(32 - frac);
        f1arr[y] = (uint8_t)frac;
    }

    pixel *d = dst;
    int angleSum = 0;
    for (int y = 0; y < width; y++, d += dstStride)
    {
        angleSum += angle;
        const int off = angleSum >> 5;
        const uint8x8_t f0 = vdup_n_u8(f0arr[y]);
        const uint8x8_t f1 = vdup_n_u8(f1arr[y]);
        const uint8_t *r = ref + off;

        uint8x8_t in0 = vld1_u8(r);
        uint8x8_t in1 = vld1_u8(r + 1);
        uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), in0, f0);
        lo = vmlal_u8(lo, in1, f1);
        vst1_u8(d, vshrn_n_u16(lo, 5));
    }
}

// Mode 25 at 16x16 has offset -1 for every row and needs no projected samples.
static void intra_pred_ang25_16_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                                     int /*dirMode*/, int /*bFilter*/)
{
    const uint8x16_t a = vld1q_u8(srcPix);
    const uint8x16_t b = vld1q_u8(srcPix + 1);
    for (int y = 0; y < 15; y++, dst += dstStride)
    {
        const uint8x8_t w0 = vdup_n_u8((uint8_t)(2 * (y + 1)));
        const uint8x8_t w1 = vdup_n_u8((uint8_t)(32 - 2 * (y + 1)));
        uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(a), w0);
        lo = vmlal_u8(lo, vget_low_u8(b), w1);
        uint16x8_t hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(a), w0);
        hi = vmlal_u8(hi, vget_high_u8(b), w1);
        vst1q_u8(dst, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
    }
    vst1q_u8(dst, a);
}

// Supported block widths: 8, 16, 32.
template<int width, int angle, int invAngle>
void intra_pred_ang_vneg_t_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                                int /*dirMode*/, int /*bFilter*/)
{
    if (width == 32)
    {
        // Calculate weights directly for each row of a 32x32 block.
        pixel refBuf[66];
        pixel *ref = refBuf - angle;
        int invAngleSum = 128;
        for (int i = 0; i < -angle - 1; i++)
        {
            invAngleSum += invAngle;
            ref[-2 - i] = srcPix[64 + (invAngleSum >> 8)];
        }
        memcpy(ref - 1, srcPix, 33 * sizeof(pixel));

        int angleSum = 0;
        for (int y = 0; y < 31; y++, dst += dstStride)
        {
            angleSum += angle;
            const int frac = angleSum & 31;
            const uint8x8_t f0 = vdup_n_u8(32 - frac);
            const uint8x8_t f1 = vdup_n_u8(frac);
            const uint8_t *r = ref + (angleSum >> 5);
            const uint8x16_t a0 = vld1q_u8(r);
            const uint8x16_t a1 = vld1q_u8(r + 1);
            const uint8x16_t b0 = vld1q_u8(r + 16);
            const uint8x16_t b1 = vld1q_u8(r + 17);
            uint16x8_t lo = vmull_u8(vget_low_u8(a0), f0);
            uint16x8_t hi = vmull_u8(vget_high_u8(a0), f0);
            lo = vmlal_u8(lo, vget_low_u8(a1), f1);
            hi = vmlal_u8(hi, vget_high_u8(a1), f1);
            vst1q_u8(dst, vcombine_u8(vrshrn_n_u16(lo, 5), vrshrn_n_u16(hi, 5)));
            lo = vmull_u8(vget_low_u8(b0), f0);
            hi = vmull_u8(vget_high_u8(b0), f0);
            lo = vmlal_u8(lo, vget_low_u8(b1), f1);
            hi = vmlal_u8(hi, vget_high_u8(b1), f1);
            vst1q_u8(dst + 16, vcombine_u8(vrshrn_n_u16(lo, 5), vrshrn_n_u16(hi, 5)));
        }
        // At row 31 the fractional offset is zero.
        vst1q_u8(dst, vld1q_u8(ref + angle));
        vst1q_u8(dst + 16, vld1q_u8(ref + angle + 16));
        return;
    }
    const int width2 = width << 1;
    pixel refBuf[2 * width + 2];
    const int nbProjected = -((width * angle) >> 5) - 1;
    pixel *ref_pix = refBuf + nbProjected + 1;
    int invAngleSum = 128;
    for (int i = 0; i < nbProjected; i++)
    {
        invAngleSum += invAngle;
        ref_pix[-2 - i] = srcPix[width2 + (invAngleSum >> 8)];
    }
    memcpy(&ref_pix[-1], srcPix, (width + 1) * sizeof(pixel));
    const uint8_t *ref = ref_pix;

    ALIGN_VAR_16(uint8_t, f0arr[32]);
    ALIGN_VAR_16(uint8_t, f1arr[32]);
    for (int y = 0; y < width; y++)
    {
        const int frac = ((y + 1) * angle) & 31;
        f0arr[y] = (uint8_t)(32 - frac);
        f1arr[y] = (uint8_t)frac;
    }

    pixel *d = dst;
    int angleSum = 0;
    for (int y = 0; y < width; y++, d += dstStride)
    {
        angleSum += angle;
        const int off = angleSum >> 5;
        const uint8x8_t f0 = vdup_n_u8(f0arr[y]);
        const uint8x8_t f1 = vdup_n_u8(f1arr[y]);
        const uint8_t *r = ref + off;

        if (width == 8)
        {
            uint8x8_t in0 = vld1_u8(r);
            uint8x8_t in1 = vld1_u8(r + 1);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), in0, f0);
            lo = vmlal_u8(lo, in1, f1);
            vst1_u8(d, vshrn_n_u16(lo, 5));
        }
        else
        {
            uint8x16_t in0 = vld1q_u8(r);
            uint8x16_t in1 = vld1q_u8(r + 1);
            uint16x8_t lo = vmlal_u8(vdupq_n_u16(16), vget_low_u8(in0), f0);
            lo = vmlal_u8(lo, vget_low_u8(in1), f1);
            uint16x8_t hi = vmlal_u8(vdupq_n_u16(16), vget_high_u8(in0), f0);
            hi = vmlal_u8(hi, vget_high_u8(in1), f1);
            vst1q_u8(d, vcombine_u8(vshrn_n_u16(lo, 5), vshrn_n_u16(hi, 5)));
        }
    }
}
#endif

#endif

#if !HIGH_BIT_DEPTH
static inline uint8x16_t intra_interp_u8x16_neon(
    uint8x16_t a,
    uint8x16_t b,
    uint8x16_t inv,
    uint8x16_t frac)
{
    uint16x8_t lo = vmull_u8(vget_low_u8(a), vget_low_u8(inv));
    uint16x8_t hi = vmull_high_u8(a, inv);
    lo = vmlal_u8(lo, vget_low_u8(b), vget_low_u8(frac));
    hi = vmlal_high_u8(hi, b, frac);
    return vcombine_u8(vrshrn_n_u16(lo, 5), vrshrn_n_u16(hi, 5));
}

// Predict all 16 samples together for positive 4x4 angles.
// Supported angles: 2, 5, 9, 13, 17, 21, 26.
template<int angle>
static inline void intra_pred_ang_vpos4_neon(pixel *dst, const pixel *ref)
{

#define POS4_IDX(y) ((y) * angle >> 5), (((y) * angle >> 5) + 1), (((y) * angle >> 5) + 2), (((y) * angle >> 5) + 3)
#define POS4_NEXT(y) (((y) * angle >> 5) + 1), (((y) * angle >> 5) + 2), (((y) * angle >> 5) + 3), (((y) * angle >> 5) + 4)
#define POS4_FRAC(y) (((y) * angle) & 31), (((y) * angle) & 31), (((y) * angle) & 31), (((y) * angle) & 31)
    static const uint8_t indices[16] = { POS4_IDX(1), POS4_IDX(2), POS4_IDX(3), POS4_IDX(4) };
    static const uint8_t nextIndices[16] = { POS4_NEXT(1), POS4_NEXT(2), POS4_NEXT(3), POS4_NEXT(4) };
    static const uint8_t fractions[16] = { POS4_FRAC(1), POS4_FRAC(2), POS4_FRAC(3), POS4_FRAC(4) };
#undef POS4_IDX
#undef POS4_NEXT
#undef POS4_FRAC
    const uint8x16_t table = vcombine_u8(vld1_u8(ref), vdup_n_u8(0));
    const uint8x16_t idx = vld1q_u8(indices);
    const uint8x16_t frac = vld1q_u8(fractions);
    const uint8x16_t a = vqtbl1q_u8(table, idx);
    const uint8x16_t b = vqtbl1q_u8(table, vld1q_u8(nextIndices));
    const uint8x16_t inv = vsubq_u8(vdupq_n_u8(32), frac);
    vst1q_u8(dst, intra_interp_u8x16_neon(a, b, inv, frac));
}

// The all-angles output stores horizontal predictions in vertical layout.
// Supported angles: -2, -5, -9, -13, -17, -21, -26, with matching inverse angles.
template<int angle, int invAngle>
static inline void intra_pred_ang_vneg4_neon(pixel *dst, const pixel *src, bool horizontal)
{

    const int projected = -((4 * angle) >> 5) - 1;
    uint8_t storage[16] = {};
    uint8_t *ref = storage + projected + 1;
    int invSum = 128;
    for (int i = 0; i < projected; i++)
    {
        invSum += invAngle;
        ref[-2 - i] = src[(horizontal ? 0 : 8) + (invSum >> 8)];
    }
    ref[-1] = src[0];
    memcpy(ref, src + (horizontal ? 9 : 1), 4);
#define NEG4_IDX(y) (((y) * angle >> 5) + projected + 1), (((y) * angle >> 5) + projected + 2), (((y) * angle >> 5) + projected + 3), (((y) * angle >> 5) + projected + 4)
#define NEG4_NEXT(y) (((y) * angle >> 5) + projected + 2), (((y) * angle >> 5) + projected + 3), (((y) * angle >> 5) + projected + 4), (((y) * angle >> 5) + projected + 5)
#define NEG4_FRAC(y) (((y) * angle) & 31), (((y) * angle) & 31), (((y) * angle) & 31), (((y) * angle) & 31)
    const uint8_t indices[16] = { NEG4_IDX(1), NEG4_IDX(2), NEG4_IDX(3), NEG4_IDX(4) };
    const uint8_t nextIndices[16] = { NEG4_NEXT(1), NEG4_NEXT(2), NEG4_NEXT(3), NEG4_NEXT(4) };
    const uint8_t fractions[16] = { NEG4_FRAC(1), NEG4_FRAC(2), NEG4_FRAC(3), NEG4_FRAC(4) };
#undef NEG4_IDX
#undef NEG4_NEXT
#undef NEG4_FRAC
    const uint8x16_t table = vld1q_u8(storage);
    const uint8x16_t idx = vld1q_u8(indices);
    const uint8x16_t frac = vld1q_u8(fractions);
    const uint8x16_t a = vqtbl1q_u8(table, idx);
    const uint8x16_t b = vqtbl1q_u8(table, vld1q_u8(nextIndices));
    const uint8x16_t inv = vsubq_u8(vdupq_n_u8(32), frac);
    vst1q_u8(dst, intra_interp_u8x16_neon(a, b, inv, frac));
}

// Positive 32x32 angles use the same vertical layout for both reference directions.
// Supported angles: 2, 5, 9, 13, 17, 21, 26.
template<int angle>
static void intra_pred_ang_vpos32_neon(pixel *dst, intptr_t stride, const pixel *srcPix)
{

    const uint8_t *ref = srcPix + 1;
    for (int y = 0; y < 31; y++, dst += stride)
    {
        const int angleSum = (y + 1) * angle;
        const int fraction = angleSum & 31;
        const uint8_t *r = ref + (angleSum >> 5);
        const uint8x16_t a0 = vld1q_u8(r);
        const uint8x16_t a1 = vld1q_u8(r + 1);
        const uint8x16_t b0 = vld1q_u8(r + 16);
        const uint8x16_t b1 = vld1q_u8(r + 17);
        const uint8x16_t f0q = vdupq_n_u8(32 - fraction);
        const uint8x16_t f1q = vdupq_n_u8(fraction);
        vst1q_u8(dst, intra_interp_u8x16_neon(a0, a1, f0q, f1q));
        vst1q_u8(dst + 16, intra_interp_u8x16_neon(b0, b1, f0q, f1q));
    }
    // The last row has an integer offset for every angle.
    vst1q_u8(dst, vld1q_u8(ref + angle));
    vst1q_u8(dst + 16, vld1q_u8(ref + angle + 16));
}
#endif

// Supported block sizes: 4x4, 8x8, 16x16, 32x32.
template<int log2Size>
void all_angs_pred_neon(pixel *dest, pixel *refPix, pixel *filtPix, int bLuma)
{

    const int size = 1 << log2Size;
#if !HIGH_BIT_DEPTH
    pixel swapped[2][129];
    if (size == 8 || size == 16)
    {
        const pixel *refs[2] = { refPix, filtPix };
        for (int i = 0; i < 2; i++)
        {
            swapped[i][0] = refs[i][0];
            memcpy(swapped[i] + 1, refs[i] + 2 * size + 1, 2 * size * sizeof(pixel));
            memcpy(swapped[i] + 2 * size + 1, refs[i] + 1, 2 * size * sizeof(pixel));
        }
    }
    if (size == 32)
    {
        // All horizontal modes use the same exchanged reference neighbours.
        const pixel *refs[2] = { refPix, filtPix };
        for (int i = 0; i < 2; i++)
        {
            swapped[i][0] = refs[i][0];
            memcpy(swapped[i] + 1, refs[i] + 65, 64 * sizeof(pixel));
            memcpy(swapped[i] + 65, refs[i] + 1, 64 * sizeof(pixel));
        }
    }
#endif
    for (int mode = 2; mode <= 34; mode++)
    {
        pixel *srcPix  = (g_intraFilterFlags[mode] & size ? filtPix  : refPix);
        pixel *out = dest + ((mode - 2) << (log2Size * 2));

#if !HIGH_BIT_DEPTH
        if (size == 4)
        {
            const pixel *positive = srcPix + (mode < 18 ? 9 : 1);
            switch (mode)
            {
            case 3:
            case 33:
                intra_pred_ang_vpos4_neon<26>(out, positive);
                continue;

            case 4:
            case 32:
                intra_pred_ang_vpos4_neon<21>(out, positive);
                continue;

            case 5:
            case 31:
                intra_pred_ang_vpos4_neon<17>(out, positive);
                continue;

            case 6:
            case 30:
                intra_pred_ang_vpos4_neon<13>(out, positive);
                continue;

            case 7:
            case 29:
                intra_pred_ang_vpos4_neon<9>(out, positive);
                continue;

            case 8:
            case 28:
                intra_pred_ang_vpos4_neon<5>(out, positive);
                continue;

            case 9:
            case 27:
                intra_pred_ang_vpos4_neon<2>(out, positive);
                continue;

            case 11:
            case 25:
                intra_pred_ang_vneg4_neon<-2, 4096>(out, srcPix, mode < 18);
                continue;

            case 12:
            case 24:
                intra_pred_ang_vneg4_neon<-5, 1638>(out, srcPix, mode < 18);
                continue;

            case 13:
            case 23:
                intra_pred_ang_vneg4_neon<-9, 910>(out, srcPix, mode < 18);
                continue;

            case 14:
            case 22:
                intra_pred_ang_vneg4_neon<-13, 630>(out, srcPix, mode < 18);
                continue;

            case 15:
            case 21:
                intra_pred_ang_vneg4_neon<-17, 482>(out, srcPix, mode < 18);
                continue;

            case 16:
            case 20:
                intra_pred_ang_vneg4_neon<-21, 390>(out, srcPix, mode < 18);
                continue;

            case 17:
            case 19:
                intra_pred_ang_vneg4_neon<-26, 315>(out, srcPix, mode < 18);
                continue;
            }
        }
        if ((size == 8 || size == 16) && mode < 18)
        {
            const int filtered = !!(g_intraFilterFlags[mode] & size);
            intra_pred_ang_neon<size, false, true>(out, size, swapped[filtered], mode, bLuma);
            continue;
        }
        if (size == 32)
        {
            const int filtered = !!(g_intraFilterFlags[mode] & size);
            const pixel *positive = mode < 18 ? swapped[filtered] : srcPix;
            switch (mode)
            {
            case 3:
            case 33:
                intra_pred_ang_vpos32_neon<26>(out, size, positive);
                continue;

            case 4:
            case 32:
                intra_pred_ang_vpos32_neon<21>(out, size, positive);
                continue;

            case 5:
            case 31:
                intra_pred_ang_vpos32_neon<17>(out, size, positive);
                continue;

            case 6:
            case 30:
                intra_pred_ang_vpos32_neon<13>(out, size, positive);
                continue;

            case 7:
            case 29:
                intra_pred_ang_vpos32_neon<9>(out, size, positive);
                continue;

            case 8:
            case 28:
                intra_pred_ang_vpos32_neon<5>(out, size, positive);
                continue;

            case 9:
            case 27:
                intra_pred_ang_vpos32_neon<2>(out, size, positive);
                continue;
            }
            if (mode < 18)
            {
                intra_pred_ang_neon<32, false, true>(out, size, swapped[filtered], mode, bLuma);
                continue;
            }
            switch (mode)
            {
            case 19:
                intra_pred_ang_vneg_t_neon<32, -26, 315>(out, size, srcPix, mode, bLuma);
                continue;
            case 20:
                intra_pred_ang_vneg_t_neon<32, -21, 390>(out, size, srcPix, mode, bLuma);
                continue;
            case 21:
                intra_pred_ang_vneg_t_neon<32, -17, 482>(out, size, srcPix, mode, bLuma);
                continue;
            case 22:
                intra_pred_ang_vneg_t_neon<32, -13, 630>(out, size, srcPix, mode, bLuma);
                continue;
            case 23:
                intra_pred_ang_vneg_t_neon<32, -9, 910>(out, size, srcPix, mode, bLuma);
                continue;
            case 24:
                intra_pred_ang_vneg_t_neon<32, -5, 1638>(out, size, srcPix, mode, bLuma);
                continue;
            case 25:
                intra_pred_ang_vneg_t_neon<32, -2, 4096>(out, size, srcPix, mode, bLuma);
                continue;
            }
        }
#endif
        intra_pred_ang_neon<size, false>(out, size, srcPix, mode, bLuma);
    }
}

template<int log2Size>
void planar_pred_neon(pixel * dst, intptr_t dstStride, const pixel * srcPix, int /*dirMode*/, int /*bFilter*/)
{
    const int blkSize = 1 << log2Size;

    const pixel* above = srcPix + 1;
    const pixel* left = srcPix + (2 * blkSize + 1);

    switch (blkSize) {
    case 8:
    {
        const uint16_t log2SizePlusOne = log2Size + 1;
        uint16x8_t blkSizeVec = vdupq_n_u16(blkSize);
        uint16x8_t topRight = vdupq_n_u16(above[blkSize]);
        uint16_t bottomLeft = left[blkSize];
        uint16x8_t oneVec = vdupq_n_u16(1);
        uint16x8_t blkSizeSubOneVec = vdupq_n_u16(blkSize - 1);

        for (int y = 0; y < blkSize; y++) {
            // (blkSize - 1 - y)
            uint16x8_t vlkSizeYVec = vdupq_n_u16(blkSize - 1 - y);
            // (y + 1) * bottomLeft
            uint16x8_t bottomLeftYVec = vdupq_n_u16((y + 1) * bottomLeft);
            // left[y]
            uint16x8_t leftYVec = vdupq_n_u16(left[y]);

            for (int x = 0; x < blkSize; x += 8) {
                int idx = y * dstStride + x;
                uint16x8_t xvec = { (uint16_t)(x + 0), (uint16_t)(x + 1),
                                    (uint16_t)(x + 2), (uint16_t)(x + 3),
                                    (uint16_t)(x + 4), (uint16_t)(x + 5),
                                    (uint16_t)(x + 6), (uint16_t)(x + 7) };

                // (blkSize - 1 - y) * above[x]
                uint16x8_t aboveVec = { (uint16_t)(above[x + 0]),
                                        (uint16_t)(above[x + 1]),
                                        (uint16_t)(above[x + 2]),
                                        (uint16_t)(above[x + 3]),
                                        (uint16_t)(above[x + 4]),
                                        (uint16_t)(above[x + 5]),
                                        (uint16_t)(above[x + 6]),
                                        (uint16_t)(above[x + 7]) };

                aboveVec = vmulq_u16(aboveVec, vlkSizeYVec);

                // (blkSize - 1 - x) * left[y]
                uint16x8_t first = vsubq_u16(blkSizeSubOneVec, xvec);
                first = vmulq_u16(first, leftYVec);

                // (x + 1) * topRight
                uint16x8_t second = vaddq_u16(xvec, oneVec);
                second = vmulq_u16(second, topRight);

                uint16x8_t resVec = vaddq_u16(first, second);
                resVec = vaddq_u16(resVec, aboveVec);
                resVec = vaddq_u16(resVec, bottomLeftYVec);
                resVec = vaddq_u16(resVec, blkSizeVec);
                resVec = vshrq_n_u16(resVec, log2SizePlusOne);

                for (int i = 0; i < 8; i++)
                    dst[idx + i] = (pixel)resVec[i];
    }
}
        }
    break;
    case 4:
    case 32:
    case 16:
    {
        const uint32_t log2SizePlusOne = log2Size + 1;
        uint32x4_t blkSizeVec = vdupq_n_u32(blkSize);
        uint32x4_t topRight = vdupq_n_u32(above[blkSize]);
        uint32_t bottomLeft = left[blkSize];
        uint32x4_t oneVec = vdupq_n_u32(1);
        uint32x4_t blkSizeSubOneVec = vdupq_n_u32(blkSize - 1);

        for (int y = 0; y < blkSize; y++) {
            // (blkSize - 1 - y)
            uint32x4_t vlkSizeYVec = vdupq_n_u32(blkSize - 1 - y);
            // (y + 1) * bottomLeft
            uint32x4_t bottomLeftYVec = vdupq_n_u32((y + 1) * bottomLeft);
            // left[y]
            uint32x4_t leftYVec = vdupq_n_u32(left[y]);

            for (int x = 0; x < blkSize; x += 4) {
                int idx = y * dstStride + x;
                uint32x4_t xvec = { (uint32_t)(x + 0), (uint32_t)(x + 1),
                                    (uint32_t)(x + 2), (uint32_t)(x + 3) };

                // (blkSize - 1 - y) * above[x]
                uint32x4_t aboveVec = { (uint32_t)(above[x + 0]),
                                        (uint32_t)(above[x + 1]),
                                        (uint32_t)(above[x + 2]),
                                        (uint32_t)(above[x + 3]) };
                aboveVec = vmulq_u32(aboveVec, vlkSizeYVec);

                // (blkSize - 1 - x) * left[y]
                uint32x4_t first = vsubq_u32(blkSizeSubOneVec, xvec);
                first = vmulq_u32(first, leftYVec);

                // (x + 1) * topRight
                uint32x4_t second = vaddq_u32(xvec, oneVec);
                second = vmulq_u32(second, topRight);

                uint32x4_t resVec = vaddq_u32(first, second);
                resVec = vaddq_u32(resVec, aboveVec);
                resVec = vaddq_u32(resVec, bottomLeftYVec);
                resVec = vaddq_u32(resVec, blkSizeVec);
                resVec = vshrq_n_u32(resVec, log2SizePlusOne);

                for (int i = 0; i < 4; i++)
                    dst[idx + i] = (pixel)resVec[i];
            }
        }
    }
    break;
        }
}

#if !HIGH_BIT_DEPTH
void intra_pred_planar4_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                             int /*dirMode*/, int /*bFilter*/)
{
    const int log2Size = 2;
    const int blkSize = 1 << log2Size;

    uint8x16_t src = vld1q_u8(srcPix + 1);

    uint8x8_t above =
        vreinterpret_u8_u32(vdup_laneq_u32(vreinterpretq_u32_u8(src), 0));

    uint8x8_t topRight = vdup_laneq_u8(src, blkSize);
    uint8x8_t bottomLeft = vdup_laneq_u8(src, 3 * blkSize);

    const uint8_t c[2][16] =
    {
        {3, 2, 1, 0, 3, 2, 1, 0, 1, 2, 3, 4, 1, 2, 3, 4},
        {3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3}
    };

    const uint8x16_t c0 = vld1q_u8(c[0]);
    const uint8x16_t c1 = vld1q_u8(c[1]);

    uint16x8_t t = vmull_u8(topRight, vget_high_u8(c0));
    t = vmlal_u8(t, above, vget_low_u8(c1));
    t = vmlal_u8(t, bottomLeft, vget_high_u8(c1));

    uint8x8_t index02 = vcreate_u8(0x0A0A0A0A08080808);
    uint8x8_t left02 = vqtbl1_u8(src, index02);
    uint16x8_t t02 = vmlal_u8(t, left02, vget_low_u8(c0));
    uint8x8_t d02 = vrshrn_n_u16(t02, log2Size + 1);

    uint8x8_t index13 = vcreate_u8(0x0B0B0B0B09090909);
    uint8x8_t left13 = vqtbl1_u8(src, index13);
    uint16x8_t t13 = vmlal_u8(t, left13, vget_low_u8(c0));
    uint16x8_t sub_bottomLeft_above = vsubl_u8(bottomLeft, above);
    t13 = vaddq_u16(t13, sub_bottomLeft_above);
    uint8x8_t d13 = vrshrn_n_u16(t13, log2Size + 1);

    store_u8x4_strided_xN<2>(dst + 0 * dstStride, 2 * dstStride, &d02);
    store_u8x4_strided_xN<2>(dst + 1 * dstStride, 2 * dstStride, &d13);
}
#endif

#if !HIGH_BIT_DEPTH
void intra_pred_planar32_neon(pixel *dst, intptr_t dstStride, const pixel *srcPix,
                              int /*dirMode*/, int /*bFilter*/)
{
    const int log2Size = 5;
    const int blkSize = 1 << log2Size;

    const pixel *src0 = srcPix + 1;
    const pixel *src1 = srcPix + 2 * blkSize + 1;

    uint8x8_t above0 = vld1_u8(src0 + 0 * 8);
    uint8x8_t above1 = vld1_u8(src0 + 1 * 8);
    uint8x8_t above2 = vld1_u8(src0 + 2 * 8);
    uint8x8_t above3 = vld1_u8(src0 + 3 * 8);

    uint8x8_t topRight = vdup_n_u8(src0[blkSize]);
    uint8x8_t bottomLeft = vdup_n_u8(src1[blkSize]);

    const uint8_t c[2][32] =
    {
        {31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
         15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0},
        { 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
         17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}
    };

    // left constant
    const uint8x8_t l0 = vld1_u8(c[0] + 0 * 8);
    const uint8x8_t l1 = vld1_u8(c[0] + 1 * 8);
    const uint8x8_t l2 = vld1_u8(c[0] + 2 * 8);
    const uint8x8_t l3 = vld1_u8(c[0] + 3 * 8);

    // topRight constant
    const uint8x8_t tR0 = vld1_u8(c[1] + 0 * 8);
    const uint8x8_t tR1 = vld1_u8(c[1] + 1 * 8);
    const uint8x8_t tR2 = vld1_u8(c[1] + 2 * 8);
    const uint8x8_t tR3 = vld1_u8(c[1] + 3 * 8);

    const uint16x8_t offset = vdupq_n_u16(blkSize);
    const uint16x8_t offset_bottomLeft = vaddw_u8(offset, bottomLeft);

    const uint8x8_t c31 = vdup_n_u8(31);

    uint16x8_t t0 = vmlal_u8(offset_bottomLeft, topRight, tR0);
    t0 = vmlal_u8(t0, above0, c31);

    uint16x8_t t1 = vmlal_u8(offset_bottomLeft, topRight, tR1);
    t1 = vmlal_u8(t1, above1, c31);

    uint16x8_t t2 = vmlal_u8(offset_bottomLeft, topRight, tR2);
    t2 = vmlal_u8(t2, above2, c31);

    uint16x8_t t3 = vmlal_u8(offset_bottomLeft, topRight, tR3);
    t3 = vmlal_u8(t3, above3, c31);

    uint16x8_t sub_bottomLeft_above0 = vsubl_u8(bottomLeft, above0);
    uint16x8_t sub_bottomLeft_above1 = vsubl_u8(bottomLeft, above1);
    uint16x8_t sub_bottomLeft_above2 = vsubl_u8(bottomLeft, above2);
    uint16x8_t sub_bottomLeft_above3 = vsubl_u8(bottomLeft, above3);

    for (int y = 0; y < 32; y++)
    {
        uint8x8_t left = vdup_n_u8(src1[y]);

        uint16x8_t r0 = vmlal_u8(t0, left, l0);
        uint16x8_t r1 = vmlal_u8(t1, left, l1);
        uint16x8_t r2 = vmlal_u8(t2, left, l2);
        uint16x8_t r3 = vmlal_u8(t3, left, l3);

        uint8x8_t d[4];
        d[0] = vshrn_n_u16(r0, log2Size + 1);
        d[1] = vshrn_n_u16(r1, log2Size + 1);
        d[2] = vshrn_n_u16(r2, log2Size + 1);
        d[3] = vshrn_n_u16(r3, log2Size + 1);

        store_u8x8xn<4>(dst + y * dstStride, 8, d);

        t0 = vaddq_u16(t0, sub_bottomLeft_above0);
        t1 = vaddq_u16(t1, sub_bottomLeft_above1);
        t2 = vaddq_u16(t2, sub_bottomLeft_above2);
        t3 = vaddq_u16(t3, sub_bottomLeft_above3);
    }
}
#endif

static void dcPredFilter(const pixel* above, const pixel* left, pixel* dst, intptr_t dststride, int size)
{
    // boundary pixels processing
    pixel topLeft = (pixel)((above[0] + left[0] + 2 * dst[0] + 2) >> 2);
    pixel * pdst = dst;

    switch (size) {
    case 32:
    case 16:
    case 8:
    {
        uint16x8_t vconst_3 = vdupq_n_u16(3);
        uint16x8_t vconst_2 = vdupq_n_u16(2);
        for (int x = 0; x < size; x += 8) {
            uint16x8_t vabo = { (uint16_t)(above[x + 0]),
                                (uint16_t)(above[x + 1]),
                                (uint16_t)(above[x + 2]),
                                (uint16_t)(above[x + 3]),
                                (uint16_t)(above[x + 4]),
                                (uint16_t)(above[x + 5]),
                                (uint16_t)(above[x + 6]),
                                (uint16_t)(above[x + 7]) };

            uint16x8_t vdst = { (uint16_t)(dst[x + 0]),
                                (uint16_t)(dst[x + 1]),
                                (uint16_t)(dst[x + 2]),
                                (uint16_t)(dst[x + 3]),
                                (uint16_t)(dst[x + 4]),
                                (uint16_t)(dst[x + 5]),
                                (uint16_t)(dst[x + 6]),
                                (uint16_t)(dst[x + 7]) };
            //  dst[x] = (pixel)((above[x] +  3 * dst[x] + 2) >> 2);
            vdst = vmulq_u16(vdst, vconst_3);
            vdst = vaddq_u16(vdst, vabo);
            vdst = vaddq_u16(vdst, vconst_2);
            vdst = vshrq_n_u16(vdst, 2);
            for (int i = 0; i < 8; i++)
                dst[x + i] = (pixel)(vdst[i]);
        }
        dst += dststride;
        for (int y = 1; y < size; y++)
        {
            *dst = (pixel)((left[y] + 3 * *dst + 2) >> 2);
            dst += dststride;
        }
    }
    break;
    case 4:
    {
        uint16x4_t vconst_3 = vdup_n_u16(3);
        uint16x4_t vconst_2 = vdup_n_u16(2);
        uint16x4_t vabo = { (uint16_t)(above[0]),
                            (uint16_t)(above[1]),
                            (uint16_t)(above[2]),
                            (uint16_t)(above[3]) };
        uint16x4_t vdstx = { (uint16_t)(dst[0]),
                             (uint16_t)(dst[1]),
                             (uint16_t)(dst[2]),
                             (uint16_t)(dst[3]) };
        vdstx = vmul_u16(vdstx, vconst_3);
        vdstx = vadd_u16(vdstx, vabo);
        vdstx = vadd_u16(vdstx, vconst_2);
        vdstx = vshr_n_u16(vdstx, 2);
        for (int i = 0; i < 4; i++)
            dst[i] = (pixel)(vdstx[i]);

        dst += dststride;
        for (int y = 1; y < size; y++)
        {
            *dst = (pixel)((left[y] + 3 * *dst + 2) >> 2);
            dst += dststride;
        }
    }
    break;
    }

    *pdst = topLeft;
}

template<int width>
void intra_pred_dc_neon(pixel* dst, intptr_t dstStride, const pixel* srcPix, int /*dirMode*/, int bFilter)
{
#if !HIGH_BIT_DEPTH
    if (width == 16 && bFilter)
    {
        const uint8x16_t top = vld1q_u8(srcPix + 1);
        const uint8x16_t left = vld1q_u8(srcPix + 33);
        const int dc = (16 + vaddlvq_u8(top) + vaddlvq_u8(left)) >> 5;
        const uint8x16_t fill = vdupq_n_u8((uint8_t)dc);
        const uint16x8_t bias = vdupq_n_u16((uint16_t)(3 * dc + 2));
        const uint8x8_t top0 = vshrn_n_u16(vaddw_u8(bias, vget_low_u8(top)), 2);
        const uint8x8_t top1 = vshrn_n_u16(vaddw_high_u8(bias, top), 2);
        const uint8x8_t left0 = vshrn_n_u16(vaddw_u8(bias, vget_low_u8(left)), 2);
        const uint8x8_t left1 = vshrn_n_u16(vaddw_high_u8(bias, left), 2);
        const uint8x16_t filteredLeft = vcombine_u8(left0, left1);
        const uint8_t corner = (uint8_t)((srcPix[1] + srcPix[33] + 2 * dc + 2) >> 2);
        vst1q_u8(dst, vsetq_lane_u8(corner, vcombine_u8(top0, top1), 0));

        // Each row is stored once, including its filtered left boundary pixel.
#define STORE_DC16_ROW(y) vst1q_u8(dst + (y) * dstStride, vcopyq_laneq_u8(fill, 0, filteredLeft, (y)))
        STORE_DC16_ROW(1);  STORE_DC16_ROW(2);  STORE_DC16_ROW(3);
        STORE_DC16_ROW(4);  STORE_DC16_ROW(5);  STORE_DC16_ROW(6);
        STORE_DC16_ROW(7);  STORE_DC16_ROW(8);  STORE_DC16_ROW(9);
        STORE_DC16_ROW(10); STORE_DC16_ROW(11); STORE_DC16_ROW(12);
        STORE_DC16_ROW(13); STORE_DC16_ROW(14); STORE_DC16_ROW(15);
#undef STORE_DC16_ROW
        return;
    }
    if (width == 8 && bFilter)
    {
        const uint8x8_t top = vld1_u8(srcPix + 1);
        const uint8x8_t left = vld1_u8(srcPix + 17);
        const int dc = (8 + vaddlv_u8(top) + vaddlv_u8(left)) >> 4;
        const uint8x8_t fill = vdup_n_u8((uint8_t)dc);
        const uint16x8_t bias = vdupq_n_u16((uint16_t)(3 * dc + 2));
        const uint8x8_t first = vshrn_n_u16(vaddw_u8(bias, top), 2);
        vst1_u8(dst, first);
        dst[0] = (pixel)((srcPix[1] + srcPix[17] + 2 * dc + 2) >> 2);
        for (int y = 1; y < 8; y++)
        {
            pixel *row = dst + y * dstStride;
            vst1_u8(row, fill);
            row[0] = (pixel)((srcPix[17 + y] + 3 * dc + 2) >> 2);
        }
        return;
    }
#endif
    int k, l;
    int dcVal = width;

    switch (width) {
    case 32:
    case 16:
    case 8:
    {
        for (int i = 0; i < width; i += 8) {
            uint16x8_t spa = { (uint16_t)(srcPix[i + 1]),
                               (uint16_t)(srcPix[i + 2]),
                               (uint16_t)(srcPix[i + 3]),
                               (uint16_t)(srcPix[i + 4]),
                               (uint16_t)(srcPix[i + 5]),
                               (uint16_t)(srcPix[i + 6]),
                               (uint16_t)(srcPix[i + 7]),
                               (uint16_t)(srcPix[i + 8]) };
            uint16x8_t spb = { (uint16_t)(srcPix[2 * width + i + 1]),
                               (uint16_t)(srcPix[2 * width + i + 2]),
                               (uint16_t)(srcPix[2 * width + i + 3]),
                               (uint16_t)(srcPix[2 * width + i + 4]),
                               (uint16_t)(srcPix[2 * width + i + 5]),
                               (uint16_t)(srcPix[2 * width + i + 6]),
                               (uint16_t)(srcPix[2 * width + i + 7]),
                               (uint16_t)(srcPix[2 * width + i + 8]) };
            uint16x8_t vsp = vaddq_u16(spa, spb);
            dcVal += vaddlvq_u16(vsp);
        }

        dcVal = dcVal / (width + width);
        for (k = 0; k < width; k++)
            for (l = 0; l < width; l += 8) {
                uint16x8_t vdv = vdupq_n_u16((pixel)dcVal);
                for (int n = 0; n < 8; n++)
                    dst[k * dstStride + l + n] = (pixel)(vdv[n]);
            }
    }
    break;
    case 4:
    {
        uint16x4_t spa = { (uint16_t)(srcPix[1]), (uint16_t)(srcPix[2]),
                           (uint16_t)(srcPix[3]), (uint16_t)(srcPix[4]) };
        uint16x4_t spb = { (uint16_t)(srcPix[2 * width + 1]),
                           (uint16_t)(srcPix[2 * width + 2]),
                           (uint16_t)(srcPix[2 * width + 3]),
                           (uint16_t)(srcPix[2 * width + 4]) };
        uint16x4_t vsp = vadd_u16(spa, spb);
        dcVal += vaddlv_u16(vsp);

        dcVal = dcVal / (width + width);
        for (k = 0; k < width; k++) {
            uint16x4_t vdv = vdup_n_u16((pixel)dcVal);
            for (int n = 0; n < 4; n++)
                dst[k * dstStride + n] = (pixel)(vdv[n]);
        }
    }
    break;
    }

    if (bFilter)
        dcPredFilter(srcPix + 1, srcPix + (2 * width + 1), dst, dstStride, width);
}
}

namespace X265_NS
{
// x265 private namespace
extern "C" void PFX(intra_pred_planar8_neon)(pixel* dst, intptr_t dstStride, const pixel* srcPix, int dirMode, int bFilter);
extern "C" void PFX(intra_pred_planar16_neon)(pixel* dst, intptr_t dstStride, const pixel* srcPix, int dirMode, int bFilter);

void setupIntraPrimitives_neon(EncoderPrimitives &p)
{
    p.cu[BLOCK_4x4].intra_filter = intraFilter_neon<4>;
    p.cu[BLOCK_8x8].intra_filter = intraFilter_neon<8>;
    p.cu[BLOCK_16x16].intra_filter = intraFilter_neon<16>;
    p.cu[BLOCK_32x32].intra_filter = intraFilter_neon<32>;

    for (int i = 2; i < NUM_INTRA_MODE; i++)
    {
        p.cu[BLOCK_8x8].intra_pred[i] = intra_pred_ang_neon<8, true>;
        p.cu[BLOCK_16x16].intra_pred[i] = intra_pred_ang_neon<16, true>;
        p.cu[BLOCK_32x32].intra_pred[i] = intra_pred_ang_neon<32, true>;
    }
    p.cu[BLOCK_4x4].intra_pred[2] = intra_pred_ang_neon<4, true>;
    p.cu[BLOCK_4x4].intra_pred[10] = intra_pred_ang_neon<4, true>;
    p.cu[BLOCK_4x4].intra_pred[18] = intra_pred_ang_neon<4, true>;
    p.cu[BLOCK_4x4].intra_pred[26] = intra_pred_ang_neon<4, true>;
    p.cu[BLOCK_4x4].intra_pred[34] = intra_pred_ang_neon<4, true>;

    p.cu[BLOCK_4x4].intra_pred_allangs = all_angs_pred_neon<2>;
    p.cu[BLOCK_8x8].intra_pred_allangs = all_angs_pred_neon<3>;
    p.cu[BLOCK_16x16].intra_pred_allangs = all_angs_pred_neon<4>;
    p.cu[BLOCK_32x32].intra_pred_allangs = all_angs_pred_neon<5>;

#if !HIGH_BIT_DEPTH
    p.cu[BLOCK_4x4].intra_pred[2] = intra_pred_ang2_neon<4>;
    p.cu[BLOCK_4x4].intra_pred[10] = intra_pred_ang10_neon<4>;
    p.cu[BLOCK_4x4].intra_pred[26] = intra_pred_ang26_neon<4>;
    p.cu[BLOCK_4x4].intra_pred[34] = intra_pred_ang34_neon<4>;

    p.cu[BLOCK_8x8].intra_pred[2] = intra_pred_ang2_neon<8>;
    p.cu[BLOCK_8x8].intra_pred[3] = intra_pred_ang_hor_pos_neon<8, 26>;
    p.cu[BLOCK_8x8].intra_pred[4] = intra_pred_ang_hor_pos_neon<8, 21>;
    p.cu[BLOCK_8x8].intra_pred[5] = intra_pred_ang_hor_pos_neon<8, 17>;
    p.cu[BLOCK_8x8].intra_pred[6] = intra_pred_ang_hor_pos_neon<8, 13>;
    p.cu[BLOCK_8x8].intra_pred[7] = intra_pred_ang_hor_pos_neon<8, 9>;
    p.cu[BLOCK_8x8].intra_pred[8] = intra_pred_ang_hor_pos_neon<8, 5>;
    p.cu[BLOCK_8x8].intra_pred[9] = intra_pred_ang9_neon<8>;
    p.cu[BLOCK_8x8].intra_pred[10] = intra_pred_ang10_neon<8>;
    p.cu[BLOCK_8x8].intra_pred[11] = intra_pred_ang11_neon<8>;
    p.cu[BLOCK_8x8].intra_pred[12] = intra_pred_ang_hor8_neon<12>;
    p.cu[BLOCK_8x8].intra_pred[13] = intra_pred_ang_hor8_neon<13>;
    p.cu[BLOCK_8x8].intra_pred[14] = intra_pred_ang_hor8_neon<14>;
    p.cu[BLOCK_8x8].intra_pred[15] = intra_pred_ang_hor8_neon<15>;
    p.cu[BLOCK_8x8].intra_pred[16] = intra_pred_ang_hor8_neon<16>;
    p.cu[BLOCK_8x8].intra_pred[17] = intra_pred_ang_hor8_neon<17>;
    p.cu[BLOCK_8x8].intra_pred[18] = intra_pred_ang18_neon<8>;
    p.cu[BLOCK_8x8].intra_pred[24] = intra_pred_ang_vneg_t_neon<8, -5, 1638>;
    p.cu[BLOCK_8x8].intra_pred[25] = intra_pred_ang_vneg_t_neon<8, -2, 4096>;
    p.cu[BLOCK_8x8].intra_pred[26] = intra_pred_ang26_neon<8>;
    p.cu[BLOCK_8x8].intra_pred[27] = intra_pred_ang_vpos_t_neon<8, 2>;
    p.cu[BLOCK_8x8].intra_pred[28] = intra_pred_ang_vpos_t_neon<8, 5>;
    p.cu[BLOCK_8x8].intra_pred[29] = intra_pred_ang_vpos_t_neon<8, 9>;
    p.cu[BLOCK_8x8].intra_pred[30] = intra_pred_ang_vpos_t_neon<8, 13>;
    p.cu[BLOCK_8x8].intra_pred[31] = intra_pred_ang_vpos_t_neon<8, 17>;
    p.cu[BLOCK_8x8].intra_pred[32] = intra_pred_ang_vpos_t_neon<8, 21>;
    p.cu[BLOCK_8x8].intra_pred[33] = intra_pred_ang_vpos_t_neon<8, 26>;
    p.cu[BLOCK_8x8].intra_pred[34] = intra_pred_ang34_neon<8>;

    p.cu[BLOCK_16x16].intra_pred[2] = intra_pred_ang2_neon<16>;
    p.cu[BLOCK_16x16].intra_pred[3] = intra_pred_ang_hor_pos_neon<16, 26>;
    p.cu[BLOCK_16x16].intra_pred[4] = intra_pred_ang_hor_pos_neon<16, 21>;
    p.cu[BLOCK_16x16].intra_pred[5] = intra_pred_ang_hor_pos_neon<16, 17>;
    p.cu[BLOCK_16x16].intra_pred[6] = intra_pred_ang_hor_pos_neon<16, 13>;
    p.cu[BLOCK_16x16].intra_pred[7] = intra_pred_ang_hor_pos_neon<16, 9>;
    p.cu[BLOCK_16x16].intra_pred[8] = intra_pred_ang_hor_pos_neon<16, 5>;
    p.cu[BLOCK_16x16].intra_pred[9] = intra_pred_ang9_neon<16>;
    p.cu[BLOCK_16x16].intra_pred[10] = intra_pred_ang10_neon<16>;
    p.cu[BLOCK_16x16].intra_pred[11] = intra_pred_ang11_neon<16>;
    p.cu[BLOCK_16x16].intra_pred[12] = intra_pred_ang_hor16_neon<12>;
    p.cu[BLOCK_16x16].intra_pred[13] = intra_pred_ang_hor16_neon<13>;
    p.cu[BLOCK_16x16].intra_pred[14] = intra_pred_ang_hor16_neon<14>;
    p.cu[BLOCK_16x16].intra_pred[15] = intra_pred_ang_hor16_neon<15>;
    p.cu[BLOCK_16x16].intra_pred[16] = intra_pred_ang_hor16_neon<16>;
    p.cu[BLOCK_16x16].intra_pred[17] = intra_pred_ang_hor16_neon<17>;
    p.cu[BLOCK_16x16].intra_pred[18] = intra_pred_ang18_neon<16>;
    p.cu[BLOCK_16x16].intra_pred[19] = intra_pred_ang_vneg_t_neon<16, -26, 315>;
    p.cu[BLOCK_16x16].intra_pred[20] = intra_pred_ang_vneg_t_neon<16, -21, 390>;
    p.cu[BLOCK_16x16].intra_pred[21] = intra_pred_ang_vneg_t_neon<16, -17, 482>;
    p.cu[BLOCK_16x16].intra_pred[22] = intra_pred_ang_vneg_t_neon<16, -13, 630>;
    p.cu[BLOCK_16x16].intra_pred[23] = intra_pred_ang_vneg_t_neon<16, -9, 910>;
    p.cu[BLOCK_16x16].intra_pred[24] = intra_pred_ang_vneg_t_neon<16, -5, 1638>;
    p.cu[BLOCK_16x16].intra_pred[25] = intra_pred_ang25_16_neon;
    p.cu[BLOCK_16x16].intra_pred[26] = intra_pred_ang26_neon<16>;
    p.cu[BLOCK_16x16].intra_pred[28] = intra_pred_ang_vpos16_direct_neon<5>;
    p.cu[BLOCK_16x16].intra_pred[29] = intra_pred_ang_vpos16_direct_neon<9>;
    p.cu[BLOCK_16x16].intra_pred[30] = intra_pred_ang_vpos16_direct_neon<13>;
    p.cu[BLOCK_16x16].intra_pred[31] = intra_pred_ang_vpos16_direct_neon<17>;
    p.cu[BLOCK_16x16].intra_pred[32] = intra_pred_ang_vpos16_direct_neon<21>;
    p.cu[BLOCK_16x16].intra_pred[34] = intra_pred_ang34_neon<16>;

    p.cu[BLOCK_32x32].intra_pred[2] = intra_pred_ang2_neon<32>;
    p.cu[BLOCK_32x32].intra_pred[3] = intra_pred_ang_hor_pos_neon<32, 26>;
    p.cu[BLOCK_32x32].intra_pred[4] = intra_pred_ang_hor_pos_neon<32, 21>;
    p.cu[BLOCK_32x32].intra_pred[5] = intra_pred_ang_hor_pos_neon<32, 17>;
    p.cu[BLOCK_32x32].intra_pred[6] = intra_pred_ang_hor_pos_neon<32, 13>;
    p.cu[BLOCK_32x32].intra_pred[7] = intra_pred_ang_hor_pos_neon<32, 9>;
    p.cu[BLOCK_32x32].intra_pred[8] = intra_pred_ang_hor_pos_neon<32, 5>;
    p.cu[BLOCK_32x32].intra_pred[9] = intra_pred_ang9_neon<32>;
    p.cu[BLOCK_32x32].intra_pred[10] = intra_pred_ang10_neon<32>;
    p.cu[BLOCK_32x32].intra_pred[11] = intra_pred_ang11_neon<32>;
    p.cu[BLOCK_32x32].intra_pred[18] = intra_pred_ang18_neon<32>;
    p.cu[BLOCK_32x32].intra_pred[19] = intra_pred_ang_vneg_t_neon<32, -26, 315>;
    p.cu[BLOCK_32x32].intra_pred[20] = intra_pred_ang_vneg_t_neon<32, -21, 390>;
    p.cu[BLOCK_32x32].intra_pred[21] = intra_pred_ang_vneg_t_neon<32, -17, 482>;
    p.cu[BLOCK_32x32].intra_pred[22] = intra_pred_ang_vneg_t_neon<32, -13, 630>;
    p.cu[BLOCK_32x32].intra_pred[23] = intra_pred_ang_vneg_t_neon<32, -9, 910>;
    p.cu[BLOCK_32x32].intra_pred[24] = intra_pred_ang_vneg_t_neon<32, -5, 1638>;
    p.cu[BLOCK_32x32].intra_pred[25] = intra_pred_ang_vneg_t_neon<32, -2, 4096>;
    p.cu[BLOCK_32x32].intra_pred[26] = intra_pred_ang26_neon<32>;
    p.cu[BLOCK_32x32].intra_pred[34] = intra_pred_ang34_neon<32>;

    p.cu[BLOCK_4x4].intra_pred[PLANAR_IDX] = intra_pred_planar4_neon;
    p.cu[BLOCK_8x8].intra_pred[PLANAR_IDX] = PFX(intra_pred_planar8_neon);
    p.cu[BLOCK_16x16].intra_pred[PLANAR_IDX] = PFX(intra_pred_planar16_neon);
    p.cu[BLOCK_32x32].intra_pred[PLANAR_IDX] = intra_pred_planar32_neon;
#endif

    p.cu[BLOCK_4x4].intra_pred[DC_IDX] = intra_pred_dc_neon<4>;
    p.cu[BLOCK_8x8].intra_pred[DC_IDX] = intra_pred_dc_neon<8>;
    p.cu[BLOCK_16x16].intra_pred[DC_IDX] = intra_pred_dc_neon<16>;
    p.cu[BLOCK_32x32].intra_pred[DC_IDX] = intra_pred_dc_neon<32>;
}
}
