/*****************************************************************************
 * Copyright (C) 2013-2024 MulticoreWare, Inc
 *
 * Authors: Hao Chen <chenhao@loongson.cn>
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
#include "primitives.h"
#include "x265.h"
#include "cpu.h"
#include "loopfilter.h"
#include "ipfilter8.h"
#include "dct.h"
#include "pixel.h"

#define ASSIGN2(func, fname) \
    func[ALIGNED] = PFX(fname); \
    func[NONALIGNED] = PFX(fname)

namespace X265_NS {
// private x265 namespace

#define LUMA(W, H, CPU) \
    p.pu[LUMA_ ## W ## x ## H].luma_vpp     = PFX(interp_8tap_vert_pp_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].luma_vps     = PFX(interp_8tap_vert_ps_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].luma_vsp     = PFX(interp_8tap_vert_sp_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].luma_vss     = PFX(interp_8tap_vert_ss_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].luma_hpp     = PFX(interp_8tap_horiz_pp_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].luma_hps     = PFX(interp_8tap_horiz_ps_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].luma_hvpp     = PFX(interp_8tap_hv_pp_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].convert_p2s[NONALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].convert_p2s[ALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU);

#define CHROMA_420(W, H, CPU) \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].filter_hpp = PFX(interp_4tap_horiz_pp_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].filter_hps = PFX(interp_4tap_horiz_ps_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].filter_vpp = PFX(interp_4tap_vert_pp_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].filter_vps = PFX(interp_4tap_vert_ps_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].filter_vsp = PFX(interp_4tap_vert_sp_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].filter_vss = PFX(interp_4tap_vert_ss_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].p2s[NONALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].p2s[ALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU);

#define CHROMA_422(W, H, CPU) \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].filter_hpp = PFX(interp_4tap_horiz_pp_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].filter_hps = PFX(interp_4tap_horiz_ps_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].filter_vpp = PFX(interp_4tap_vert_pp_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].filter_vps = PFX(interp_4tap_vert_ps_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].filter_vsp = PFX(interp_4tap_vert_sp_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].filter_vss = PFX(interp_4tap_vert_ss_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].p2s[NONALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].p2s[ALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU);

#define CHROMA_444(W, H, CPU) \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].filter_hpp = PFX(interp_4tap_horiz_pp_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].filter_hps = PFX(interp_4tap_horiz_ps_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].filter_vpp = PFX(interp_4tap_vert_pp_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].filter_vps = PFX(interp_4tap_vert_ps_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].filter_vsp = PFX(interp_4tap_vert_sp_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].filter_vss = PFX(interp_4tap_vert_ss_ ## W ## x ## H ## _ ##CPU);  \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].p2s[NONALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I444].pu[LUMA_ ## W ## x ## H].p2s[ALIGNED] = PFX(filterPixelToShort_ ## W ## x ## H ## _ ##CPU);

#define LUMA_PU(W, H, CPU) \
    p.pu[LUMA_ ## W ## x ## H].addAvg[NONALIGNED] = PFX(addAvg_ ## W ## x ## H ## _ ##CPU); \
    p.pu[LUMA_ ## W ## x ## H].addAvg[ALIGNED] = PFX(addAvg_ ## W ## x ## H ## _ ##CPU);

#define CHROMA_PU_420(W, H, CPU) \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].addAvg[NONALIGNED]  = PFX(addAvg_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I420].pu[CHROMA_420_ ## W ## x ## H].addAvg[ALIGNED]  = PFX(addAvg_ ## W ## x ## H ## _ ##CPU);

#define CHROMA_PU_422(W, H, CPU) \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].addAvg[NONALIGNED]  = PFX(addAvg_ ## W ## x ## H ## _ ##CPU); \
    p.chroma[X265_CSP_I422].pu[CHROMA_422_ ## W ## x ## H].addAvg[ALIGNED]  = PFX(addAvg_ ## W ## x ## H ## _ ##CPU);

void setupAssemblyPrimitives(EncoderPrimitives &p, int cpuMask)
{
#if HAVE_LSX
    if (cpuMask & X265_CPU_LSX)
    {
        // loopfilter
        p.saoCuOrgE0 = PFX(saoCuOrgE0_lsx);
        p.saoCuOrgE1 = PFX(saoCuOrgE1_lsx);
        p.saoCuOrgE1_2Rows = PFX(saoCuOrgE1_2Rows_lsx);
        p.saoCuOrgE2[0] = PFX(saoCuOrgE2_lsx);
        p.saoCuOrgE2[1] = PFX(saoCuOrgE2_lsx);
        p.saoCuOrgE3[0] = PFX(saoCuOrgE3_lsx);
        p.saoCuOrgE3[1] = PFX(saoCuOrgE3_lsx);
        p.saoCuOrgB0 = PFX(saoCuOrgB0_lsx);
        p.sign = PFX(calSign_lsx);
        p.pelFilterLumaStrong[0] = PFX(pelFilterLumaStrong_V_lsx);
        p.pelFilterLumaStrong[1] = PFX(pelFilterLumaStrong_H_lsx);
        p.pelFilterChroma[0] = PFX(pelFilterChroma_V_lsx);
        p.pelFilterChroma[1] = PFX(pelFilterChroma_H_lsx);
        p.saoCuStatsE0 = PFX(saoCuStatsE0_lsx);
        p.saoCuStatsE1 = PFX(saoCuStatsE1_lsx);
        p.saoCuStatsE2 = PFX(saoCuStatsE2_lsx);
        p.saoCuStatsE3 = PFX(saoCuStatsE3_lsx);
        p.saoCuStatsBO = PFX(saoCuStatsB0_lsx);

        // ipfilter
        LUMA(4,  4,  lsx)
        LUMA(4,  8,  lsx)
        LUMA(4,  16, lsx)
        LUMA(8,  4,  lsx)
        LUMA(8,  8,  lsx)
        LUMA(8,  16, lsx)
        LUMA(8,  32, lsx)
        LUMA(12, 16, lsx)
        LUMA(16, 4,  lsx)
        LUMA(16, 8,  lsx)
        LUMA(16, 12, lsx)
        LUMA(16, 16, lsx)
        LUMA(16, 32, lsx)
        LUMA(16, 64, lsx)
        LUMA(24, 32, lsx)
        LUMA(32, 8,  lsx)
        LUMA(32, 16, lsx)
        LUMA(32, 24, lsx)
        LUMA(32, 32, lsx)
        LUMA(32, 64, lsx)
        LUMA(48, 64, lsx)
        LUMA(64, 16, lsx)
        LUMA(64, 32, lsx)
        LUMA(64, 48, lsx)
        LUMA(64, 64, lsx)

        CHROMA_420(2,  4,  lsx)
        CHROMA_420(2,  8,  lsx)
        CHROMA_420(4,  2,  lsx)
        CHROMA_420(4,  4,  lsx)
        CHROMA_420(4,  8,  lsx)
        CHROMA_420(4,  16, lsx)
        CHROMA_420(6,  8,  lsx)
        CHROMA_420(8,  2,  lsx)
        CHROMA_420(8,  4,  lsx)
        CHROMA_420(8,  6,  lsx)
        CHROMA_420(8,  8,  lsx)
        CHROMA_420(8,  16, lsx)
        CHROMA_420(8,  32, lsx)
        CHROMA_420(12, 16, lsx)
        CHROMA_420(16, 4,  lsx)
        CHROMA_420(16, 8,  lsx)
        CHROMA_420(16, 12, lsx)
        CHROMA_420(16, 16, lsx)
        CHROMA_420(16, 32, lsx)
        CHROMA_420(24, 32, lsx)
        CHROMA_420(32, 8,  lsx)
        CHROMA_420(32, 16, lsx)
        CHROMA_420(32, 24, lsx)
        CHROMA_420(32, 32, lsx)

        CHROMA_422(2,  4,  lsx)
        CHROMA_422(2,  8,  lsx)
        CHROMA_422(2,  16, lsx)
        CHROMA_422(4,  4,  lsx)
        CHROMA_422(4,  8,  lsx)
        CHROMA_422(4,  16, lsx)
        CHROMA_422(4,  32, lsx)
        CHROMA_422(6,  16, lsx)
        CHROMA_422(8,  4,  lsx)
        CHROMA_422(8,  8,  lsx)
        CHROMA_422(8,  12, lsx)
        CHROMA_422(8,  16, lsx)
        CHROMA_422(8,  32, lsx)
        CHROMA_422(8,  64, lsx)
        CHROMA_422(12, 32, lsx)
        CHROMA_422(16, 8,  lsx)
        CHROMA_422(16, 16, lsx)
        CHROMA_422(16, 24, lsx)
        CHROMA_422(16, 32, lsx)
        CHROMA_422(16, 64, lsx)
        CHROMA_422(24, 64, lsx)
        CHROMA_422(32, 16, lsx)
        CHROMA_422(32, 32, lsx)
        CHROMA_422(32, 48, lsx)
        CHROMA_422(32, 64, lsx)

        CHROMA_444(4,  4,  lsx)
        CHROMA_444(4,  8,  lsx)
        CHROMA_444(4,  16, lsx)
        CHROMA_444(8,  4,  lsx)
        CHROMA_444(8,  8,  lsx)
        CHROMA_444(8,  16, lsx)
        CHROMA_444(8,  32, lsx)
        CHROMA_444(12, 16, lsx)
        CHROMA_444(16, 4,  lsx)
        CHROMA_444(16, 8,  lsx)
        CHROMA_444(16, 12, lsx)
        CHROMA_444(16, 16, lsx)
        CHROMA_444(16, 32, lsx)
        CHROMA_444(16, 64, lsx)
        CHROMA_444(24, 32, lsx)
        CHROMA_444(32, 8,  lsx)
        CHROMA_444(32, 16, lsx)
        CHROMA_444(32, 24, lsx)
        CHROMA_444(32, 32, lsx)
        CHROMA_444(32, 64, lsx)
        CHROMA_444(48, 64, lsx)
        CHROMA_444(64, 16, lsx)
        CHROMA_444(64, 32, lsx)
        CHROMA_444(64, 48, lsx)
        CHROMA_444(64, 64, lsx)

        //dct
        p.cu[BLOCK_32x32].dct  = PFX(dct32_lsx);
        p.cu[BLOCK_16x16].dct  = PFX(dct16_lsx);
        p.cu[BLOCK_8x8].dct    = PFX(dct8_lsx);
        p.cu[BLOCK_4x4].dct    = PFX(dct4_lsx);
        p.cu[BLOCK_4x4].idct   = PFX(idct4_lsx);
        p.cu[BLOCK_8x8].idct   = PFX(idct8_lsx);
        p.cu[BLOCK_16x16].idct = PFX(idct16_lsx);
        p.cu[BLOCK_32x32].idct = PFX(idct32_lsx);
        p.dst4x4               = PFX(dst4_lsx);
        p.idst4x4              = PFX(idst4_lsx);
        p.scanPosLast          = PFX(scanPosLast_lsx);
        p.costCoeffNxN         = PFX(costCoeffNxN_lsx);
        p.quant                = PFX(quant_lsx);
        p.dequant_normal       = PFX(dequant_normal_lsx);
        p.dequant_scaling      = PFX(dequant_scaling_lsx);
        p.nquant               = PFX(nquant_lsx);
        p.cu[BLOCK_32x32].count_nonzero = PFX(count_nonzero_32_lsx);
        p.cu[BLOCK_16x16].count_nonzero = PFX(count_nonzero_16_lsx);
        p.cu[BLOCK_8x8].count_nonzero   = PFX(count_nonzero_8_lsx);
        p.cu[BLOCK_4x4].count_nonzero   = PFX(count_nonzero_4_lsx);
        p.costC1C2Flag         = PFX(costC1C2Flag_lsx);
        p.costCoeffRemain      = PFX(costCoeffRemain_lsx);
        p.findPosFirstLast     = PFX(findPosFirstLast_lsx);
        p.denoiseDct           = PFX(denoiseDct_lsx);

        //pixelAvg_pp
        ASSIGN2(p.pu[LUMA_64x64].pixelavg_pp, pixel_avg_64x64_lsx);
        ASSIGN2(p.pu[LUMA_64x48].pixelavg_pp, pixel_avg_64x48_lsx);
        ASSIGN2(p.pu[LUMA_64x32].pixelavg_pp, pixel_avg_64x32_lsx);
        ASSIGN2(p.pu[LUMA_64x16].pixelavg_pp, pixel_avg_64x16_lsx);
        ASSIGN2(p.pu[LUMA_48x64].pixelavg_pp, pixel_avg_48x64_lsx);
        ASSIGN2(p.pu[LUMA_32x64].pixelavg_pp, pixel_avg_32x64_lsx);
        ASSIGN2(p.pu[LUMA_32x32].pixelavg_pp, pixel_avg_32x32_lsx);
        ASSIGN2(p.pu[LUMA_32x24].pixelavg_pp, pixel_avg_32x24_lsx);
        ASSIGN2(p.pu[LUMA_32x16].pixelavg_pp, pixel_avg_32x16_lsx);
        ASSIGN2(p.pu[LUMA_32x8].pixelavg_pp, pixel_avg_32x8_lsx);
        ASSIGN2(p.pu[LUMA_24x32].pixelavg_pp, pixel_avg_24x32_lsx);
        ASSIGN2(p.pu[LUMA_16x64].pixelavg_pp, pixel_avg_16x64_lsx);
        ASSIGN2(p.pu[LUMA_16x32].pixelavg_pp, pixel_avg_16x32_lsx);
        ASSIGN2(p.pu[LUMA_16x16].pixelavg_pp, pixel_avg_16x16_lsx);
        ASSIGN2(p.pu[LUMA_16x12].pixelavg_pp, pixel_avg_16x12_lsx);
        ASSIGN2(p.pu[LUMA_16x8].pixelavg_pp, pixel_avg_16x8_lsx);
        ASSIGN2(p.pu[LUMA_16x4].pixelavg_pp, pixel_avg_16x4_lsx);
        ASSIGN2(p.pu[LUMA_12x16].pixelavg_pp, pixel_avg_12x16_lsx);
        ASSIGN2(p.pu[LUMA_8x32].pixelavg_pp, pixel_avg_8x32_lsx);
        ASSIGN2(p.pu[LUMA_8x16].pixelavg_pp, pixel_avg_8x16_lsx);
        ASSIGN2(p.pu[LUMA_8x8].pixelavg_pp, pixel_avg_8x8_lsx);
        ASSIGN2(p.pu[LUMA_8x4].pixelavg_pp, pixel_avg_8x4_lsx);
        ASSIGN2(p.pu[LUMA_4x16].pixelavg_pp, pixel_avg_4x16_lsx);
        ASSIGN2(p.pu[LUMA_4x8].pixelavg_pp, pixel_avg_4x8_lsx);
        ASSIGN2(p.pu[LUMA_4x4].pixelavg_pp, pixel_avg_4x4_lsx);

        LUMA_PU(4,   4, lsx);
        LUMA_PU(8,   8, lsx);
        LUMA_PU(16, 16, lsx);
        LUMA_PU(32, 32, lsx);
        LUMA_PU(64, 64, lsx);
        LUMA_PU(4,   8, lsx);
        LUMA_PU(8,   4, lsx);
        LUMA_PU(16,  8, lsx);
        LUMA_PU(8,  16, lsx);
        LUMA_PU(16, 12, lsx);
        LUMA_PU(12, 16, lsx);
        LUMA_PU(16,  4, lsx);
        LUMA_PU(4,  16, lsx);
        LUMA_PU(32, 16, lsx);
        LUMA_PU(16, 32, lsx);
        LUMA_PU(32, 24, lsx);
        LUMA_PU(24, 32, lsx);
        LUMA_PU(32,  8, lsx);
        LUMA_PU(8,  32, lsx);
        LUMA_PU(64, 32, lsx);
        LUMA_PU(32, 64, lsx);
        LUMA_PU(64, 48, lsx);
        LUMA_PU(48, 64, lsx);
        LUMA_PU(64, 16, lsx);
        LUMA_PU(16, 64, lsx);

        CHROMA_PU_420(2,   2, lsx);
        CHROMA_PU_420(2,   4, lsx);
        CHROMA_PU_420(4,   4, lsx);
        CHROMA_PU_420(8,   8, lsx);
        CHROMA_PU_420(16, 16, lsx);
        CHROMA_PU_420(32, 32, lsx);
        CHROMA_PU_420(4,   2, lsx);
        CHROMA_PU_420(8,   4, lsx);
        CHROMA_PU_420(4,   8, lsx);
        CHROMA_PU_420(8,   6, lsx);
        CHROMA_PU_420(6,   8, lsx);
        CHROMA_PU_420(8,   2, lsx);
        CHROMA_PU_420(2,   8, lsx);
        CHROMA_PU_420(16,  8, lsx);
        CHROMA_PU_420(8,  16, lsx);
        CHROMA_PU_420(16, 12, lsx);
        CHROMA_PU_420(12, 16, lsx);
        CHROMA_PU_420(16,  4, lsx);
        CHROMA_PU_420(4,  16, lsx);
        CHROMA_PU_420(32, 16, lsx);
        CHROMA_PU_420(16, 32, lsx);
        CHROMA_PU_420(32, 24, lsx);
        CHROMA_PU_420(24, 32, lsx);
        CHROMA_PU_420(32,  8, lsx);
        CHROMA_PU_420(8,  32, lsx);

        CHROMA_PU_422(2,   4, lsx);
        CHROMA_PU_422(4,   8, lsx);
        CHROMA_PU_422(8,  16, lsx);
        CHROMA_PU_422(16, 32, lsx);
        CHROMA_PU_422(32, 64, lsx);
        CHROMA_PU_422(4,   4, lsx);
        CHROMA_PU_422(2,   8, lsx);
        CHROMA_PU_422(8,   8, lsx);
        CHROMA_PU_422(4,  16, lsx);
        CHROMA_PU_422(8,  12, lsx);
        CHROMA_PU_422(6,  16, lsx);
        CHROMA_PU_422(8,   4, lsx);
        CHROMA_PU_422(2,  16, lsx);
        CHROMA_PU_422(16, 16, lsx);
        CHROMA_PU_422(8,  32, lsx);
        CHROMA_PU_422(16, 24, lsx);
        CHROMA_PU_422(12, 32, lsx);
        CHROMA_PU_422(16,  8, lsx);
        CHROMA_PU_422(4,  32, lsx);
        CHROMA_PU_422(32, 32, lsx);
        CHROMA_PU_422(16, 64, lsx);
        CHROMA_PU_422(32, 48, lsx);
        CHROMA_PU_422(24, 64, lsx);
        CHROMA_PU_422(32, 16, lsx);
        CHROMA_PU_422(8,  64, lsx);
    }
#endif /* HAVE_LSX */

#if HAVE_LASX
    if (cpuMask & X265_CPU_LASX)
    {
        // loopfilter
        p.sign = PFX(calSign_lasx);

        // ipfilter
        LUMA(16, 4,  lasx)
        LUMA(16, 8,  lasx)
        LUMA(16, 12, lasx)
        LUMA(16, 16, lasx)
        LUMA(16, 32, lasx)
        LUMA(16, 64, lasx)
        LUMA(24, 32, lasx)
        LUMA(32, 8,  lasx)
        LUMA(32, 16, lasx)
        LUMA(32, 24, lasx)
        LUMA(32, 32, lasx)
        LUMA(32, 64, lasx)
        LUMA(48, 64, lasx)
        LUMA(64, 16, lasx)
        LUMA(64, 32, lasx)
        LUMA(64, 48, lasx)
        LUMA(64, 64, lasx)

        CHROMA_420(16, 4,  lasx)
        CHROMA_420(16, 8,  lasx)
        CHROMA_420(16, 12, lasx)
        CHROMA_420(16, 16, lasx)
        CHROMA_420(16, 32, lasx)
        CHROMA_420(24, 32, lasx)
        CHROMA_420(32, 8,  lasx)
        CHROMA_420(32, 16, lasx)
        CHROMA_420(32, 24, lasx)
        CHROMA_420(32, 32, lasx)

        CHROMA_422(16, 8,  lasx)
        CHROMA_422(16, 16, lasx)
        CHROMA_422(16, 24, lasx)
        CHROMA_422(16, 32, lasx)
        CHROMA_422(16, 64, lasx)
        CHROMA_422(24, 64, lasx)
        CHROMA_422(32, 16, lasx)
        CHROMA_422(32, 32, lasx)
        CHROMA_422(32, 48, lasx)
        CHROMA_422(32, 64, lasx)

        CHROMA_444(16, 4,  lasx)
        CHROMA_444(16, 8,  lasx)
        CHROMA_444(16, 12, lasx)
        CHROMA_444(16, 16, lasx)
        CHROMA_444(16, 32, lasx)
        CHROMA_444(16, 64, lasx)
        CHROMA_444(24, 32, lasx)
        CHROMA_444(32, 8,  lasx)
        CHROMA_444(32, 16, lasx)
        CHROMA_444(32, 24, lasx)
        CHROMA_444(32, 32, lasx)
        CHROMA_444(32, 64, lasx)
        CHROMA_444(48, 64, lasx)
        CHROMA_444(64, 16, lasx)
        CHROMA_444(64, 32, lasx)
        CHROMA_444(64, 48, lasx)
        CHROMA_444(64, 64, lasx)

        //dct
        p.cu[BLOCK_16x16].dct  = PFX(dct16_lasx);
        p.cu[BLOCK_32x32].dct  = PFX(dct32_lasx);
        p.cu[BLOCK_16x16].idct = PFX(idct16_lasx);
        p.cu[BLOCK_32x32].idct = PFX(idct32_lasx);
        p.quant                = PFX(quant_lasx);
        p.dequant_normal       = PFX(dequant_normal_lasx);
        p.dequant_scaling      = PFX(dequant_scaling_lasx);
        p.nquant               = PFX(nquant_lasx);
        p.cu[BLOCK_32x32].count_nonzero = PFX(count_nonzero_32_lasx);
        p.cu[BLOCK_16x16].count_nonzero = PFX(count_nonzero_16_lasx);
        p.cu[BLOCK_8x8].count_nonzero   = PFX(count_nonzero_8_lasx);
        p.cu[BLOCK_4x4].count_nonzero   = PFX(count_nonzero_4_lasx);
        p.findPosFirstLast     = PFX(findPosFirstLast_lasx);
        p.denoiseDct           = PFX(denoiseDct_lasx);

        //pixelAvg_pp
        ASSIGN2(p.pu[LUMA_64x64].pixelavg_pp, pixel_avg_64x64_lasx);
        ASSIGN2(p.pu[LUMA_64x48].pixelavg_pp, pixel_avg_64x48_lasx);
        ASSIGN2(p.pu[LUMA_64x32].pixelavg_pp, pixel_avg_64x32_lasx);
        ASSIGN2(p.pu[LUMA_64x16].pixelavg_pp, pixel_avg_64x16_lasx);
        ASSIGN2(p.pu[LUMA_48x64].pixelavg_pp, pixel_avg_48x64_lasx);
        ASSIGN2(p.pu[LUMA_32x64].pixelavg_pp, pixel_avg_32x64_lasx);
        ASSIGN2(p.pu[LUMA_32x32].pixelavg_pp, pixel_avg_32x32_lasx);
        ASSIGN2(p.pu[LUMA_32x24].pixelavg_pp, pixel_avg_32x24_lasx);
        ASSIGN2(p.pu[LUMA_32x16].pixelavg_pp, pixel_avg_32x16_lasx);
        ASSIGN2(p.pu[LUMA_32x8].pixelavg_pp, pixel_avg_32x8_lasx);

        //addAvg_pp
        ASSIGN2(p.pu[LUMA_24x32].addAvg, addAvg_24x32_lasx);
        ASSIGN2(p.pu[LUMA_32x8].addAvg, addAvg_32x8_lasx);
        ASSIGN2(p.pu[LUMA_32x16].addAvg, addAvg_32x16_lasx);
        ASSIGN2(p.pu[LUMA_32x24].addAvg, addAvg_32x24_lasx);
        ASSIGN2(p.pu[LUMA_32x32].addAvg, addAvg_32x32_lasx);
        ASSIGN2(p.pu[LUMA_32x64].addAvg, addAvg_32x64_lasx);
        ASSIGN2(p.pu[LUMA_48x64].addAvg, addAvg_48x64_lasx);
        ASSIGN2(p.pu[LUMA_64x16].addAvg, addAvg_64x16_lasx);
        ASSIGN2(p.pu[LUMA_64x32].addAvg, addAvg_64x32_lasx);
        ASSIGN2(p.pu[LUMA_64x48].addAvg, addAvg_64x48_lasx);
        ASSIGN2(p.pu[LUMA_64x64].addAvg, addAvg_64x64_lasx);
        ASSIGN2(p.chroma[X265_CSP_I420].pu[CHROMA_420_24x32].addAvg, addAvg_24x32_lasx);
        ASSIGN2(p.chroma[X265_CSP_I420].pu[CHROMA_420_32x8].addAvg, addAvg_32x8_lasx);
        ASSIGN2(p.chroma[X265_CSP_I420].pu[CHROMA_420_32x16].addAvg, addAvg_32x16_lasx);
        ASSIGN2(p.chroma[X265_CSP_I420].pu[CHROMA_420_32x24].addAvg, addAvg_32x24_lasx);
        ASSIGN2(p.chroma[X265_CSP_I420].pu[CHROMA_420_32x32].addAvg, addAvg_32x32_lasx);
        ASSIGN2(p.chroma[X265_CSP_I422].pu[CHROMA_422_24x64].addAvg, addAvg_24x64_lasx);
        ASSIGN2(p.chroma[X265_CSP_I422].pu[CHROMA_422_32x16].addAvg, addAvg_32x16_lasx);
        ASSIGN2(p.chroma[X265_CSP_I422].pu[CHROMA_422_32x32].addAvg, addAvg_32x32_lasx);
        ASSIGN2(p.chroma[X265_CSP_I422].pu[CHROMA_422_32x48].addAvg, addAvg_32x48_lasx);
        ASSIGN2(p.chroma[X265_CSP_I422].pu[CHROMA_422_32x64].addAvg, addAvg_32x64_lasx);
    }
#endif /* HAVE_LASX */
}

}
