/*****************************************************************************
 * pixel.h: loongarch pixel metrics
 *****************************************************************************
 * Copyright (C) 2013-2024 MulticoreWare, Inc
 *
 * Authors: jinbo <jinbo@loongson.cn>
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

#ifndef X265_LOONGARCH_PIXEL_H
#define X265_LOONGARCH_PIXEL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define FUNCDEF_PU(ret, name, cpu, ...) \
    ret PFX(name ## _4x4_   ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x8_   ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x16_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x32_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _64x64_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x4_   ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _4x8_   ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x8_  ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x16_  ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x32_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x16_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _64x32_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x64_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x12_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _12x16_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x4_  ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _4x16_  ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x24_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _24x32_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x8_  ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x32_  ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _64x48_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _48x64_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _64x16_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x64_ ## cpu)(__VA_ARGS__)

#define FUNCDEF_CHROMA_PU(ret, name, cpu, ...) \
    FUNCDEF_PU(ret, name, cpu, __VA_ARGS__); \
    ret PFX(name ## _2x2_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _4x2_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _2x4_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x2_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _2x8_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x6_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _6x8_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x12_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _12x8_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _6x16_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x6_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _2x16_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x2_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _4x12_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _12x4_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x12_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _12x32_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x4_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _4x32_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _32x48_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _48x32_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _16x24_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _24x16_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _8x64_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _64x8_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _64x24_ ## cpu)(__VA_ARGS__); \
    ret PFX(name ## _24x64_ ## cpu)(__VA_ARGS__);

#define DECL_PIXELS(cpu) \
    FUNCDEF_PU(void, pixel_avg, cpu, pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int); \
    FUNCDEF_CHROMA_PU(void, addAvg, cpu, const int16_t*, const int16_t*, pixel*, intptr_t, intptr_t, intptr_t); \
    FUNCDEF_CHROMA_PU(void, addAvg_aligned, cpu, const int16_t*, const int16_t*, pixel*, intptr_t, intptr_t, intptr_t);

DECL_PIXELS(lsx);

void x265_pixel_avg_32x8_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_32x16_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_32x24_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_32x32_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_32x64_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_48x64_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_64x16_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_64x32_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_64x48_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);
void x265_pixel_avg_64x64_lasx(pixel* dst, intptr_t dstride, const pixel* src0, intptr_t sstride0, const pixel* src1, intptr_t sstride1, int);

void x265_addAvg_24x32_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_24x64_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_32x8_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_32x16_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_32x24_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_32x32_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_32x48_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_32x64_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_48x64_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_64x16_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_64x32_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_64x48_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);
void x265_addAvg_64x64_lasx(const int16_t* src0, const int16_t* src1, pixel* dst, intptr_t src0Stride, intptr_t src1Stride, intptr_t dstStride);

#undef FUNCDEF_PU
#undef DECL_PIXELS
#undef FUNCDEF_CHROMA_PU

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // ifndef X265_LOONGARCH_PIXEL_H
