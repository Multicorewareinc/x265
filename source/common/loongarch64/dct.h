/*****************************************************************************
 * Copyright (C) 2024 MulticoreWare, Inc
 *
 * Authors: Peng Zhou <zhoupeng@loongson.cn>
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

#ifndef X265_LOONGARCH_DCT_H
#define X265_LOONGARCH_DCT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void PFX(dct32_lsx)(const int16_t* src, int16_t* dst, intptr_t srcStride);
void PFX(dct16_lsx)(const int16_t* src, int16_t* dst, intptr_t srcStride);
void PFX(dct4_lsx)(const int16_t* src, int16_t* dst, intptr_t srcStride);
void PFX(dct8_lsx)(const int16_t* src, int16_t* dst, intptr_t srcStride);
void PFX(idct4_lsx)(const int16_t* src, int16_t* dst, intptr_t dstStride);
void PFX(idct8_lsx)(const int16_t* src, int16_t* dst, intptr_t dstStride);
void PFX(idct16_lsx)(const int16_t* src, int16_t* dst, intptr_t dstStride);
void PFX(idct32_lsx)(const int16_t* src, int16_t* dst, intptr_t dstStride);
void PFX(dst4_lsx)(const int16_t* src, int16_t* dst, intptr_t srcStride);
void PFX(idst4_lsx)(const int16_t* src, int16_t* dst, intptr_t dstStride);
int PFX(scanPosLast_lsx)(const uint16_t *scan, const coeff_t *coeff,
                         uint16_t *coeffSign, uint16_t *coeffFlag, uint8_t *coeffNum,
                         int numSig, const uint16_t* scanCG4x4, const int trSize);
uint32_t PFX(costCoeffNxN_lsx)(const uint16_t *scan, const coeff_t *coeff, intptr_t trSize,
                               uint16_t *absCoeff, const uint8_t *tabSigCtx, uint32_t scanFlagMask,
                               uint8_t *baseCtx, int offset, int scanPosSigOff, int subPosBase);

void PFX(dct16_lasx)(const int16_t* src, int16_t* dst, intptr_t srcStride);
void PFX(dct32_lasx)(const int16_t* src, int16_t* dst, intptr_t srcStride);
void PFX(idct16_lasx)(const int16_t* src, int16_t* dst, intptr_t dstStride);
void PFX(idct32_lasx)(const int16_t* src, int16_t* dst, intptr_t dstStride);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif // ifndef X265_LOONGARCH_DCT_H
