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
#include "intrapred.h"

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

        // intra_pred
        p.cu[BLOCK_4x4].intra_filter = PFX(intra_filter_4x4_lsx);
        p.cu[BLOCK_8x8].intra_filter = PFX(intra_filter_8x8_lsx);
        p.cu[BLOCK_16x16].intra_filter = PFX(intra_filter_16x16_lsx);
        p.cu[BLOCK_32x32].intra_filter = PFX(intra_filter_32x32_lsx);

        p.cu[BLOCK_4x4].intra_pred[PLANAR_IDX]   = PFX(intra_pred_planar_4x4_lsx);
        p.cu[BLOCK_8x8].intra_pred[PLANAR_IDX]   = PFX(intra_pred_planar_8x8_lsx);
        p.cu[BLOCK_16x16].intra_pred[PLANAR_IDX] = PFX(intra_pred_planar_16x16_lsx);
        p.cu[BLOCK_32x32].intra_pred[PLANAR_IDX] = PFX(intra_pred_planar_32x32_lsx);

        p.cu[BLOCK_4x4].intra_pred[2] = PFX(intra_pred_ang4_2_lsx);
        p.cu[BLOCK_4x4].intra_pred[3] = PFX(intra_pred_ang4_3_lsx);
        p.cu[BLOCK_4x4].intra_pred[4] = PFX(intra_pred_ang4_4_lsx);
        p.cu[BLOCK_4x4].intra_pred[5] = PFX(intra_pred_ang4_5_lsx);
        p.cu[BLOCK_4x4].intra_pred[6] = PFX(intra_pred_ang4_6_lsx);
        p.cu[BLOCK_4x4].intra_pred[7] = PFX(intra_pred_ang4_7_lsx);
        p.cu[BLOCK_4x4].intra_pred[8] = PFX(intra_pred_ang4_8_lsx);
        p.cu[BLOCK_4x4].intra_pred[9] = PFX(intra_pred_ang4_9_lsx);
        p.cu[BLOCK_4x4].intra_pred[10] = PFX(intra_pred_ang4_10_lsx);
        p.cu[BLOCK_4x4].intra_pred[11] = PFX(intra_pred_ang4_11_lsx);
        p.cu[BLOCK_4x4].intra_pred[12] = PFX(intra_pred_ang4_12_lsx);
        p.cu[BLOCK_4x4].intra_pred[13] = PFX(intra_pred_ang4_13_lsx);
        p.cu[BLOCK_4x4].intra_pred[14] = PFX(intra_pred_ang4_14_lsx);
        p.cu[BLOCK_4x4].intra_pred[15] = PFX(intra_pred_ang4_15_lsx);
        p.cu[BLOCK_4x4].intra_pred[16] = PFX(intra_pred_ang4_16_lsx);
        p.cu[BLOCK_4x4].intra_pred[17] = PFX(intra_pred_ang4_17_lsx);
        p.cu[BLOCK_4x4].intra_pred[18] = PFX(intra_pred_ang4_18_lsx);
        p.cu[BLOCK_4x4].intra_pred[19] = PFX(intra_pred_ang4_19_lsx);
        p.cu[BLOCK_4x4].intra_pred[20] = PFX(intra_pred_ang4_20_lsx);
        p.cu[BLOCK_4x4].intra_pred[21] = PFX(intra_pred_ang4_21_lsx);
        p.cu[BLOCK_4x4].intra_pred[22] = PFX(intra_pred_ang4_22_lsx);
        p.cu[BLOCK_4x4].intra_pred[23] = PFX(intra_pred_ang4_23_lsx);
        p.cu[BLOCK_4x4].intra_pred[24] = PFX(intra_pred_ang4_24_lsx);
        p.cu[BLOCK_4x4].intra_pred[25] = PFX(intra_pred_ang4_25_lsx);
        p.cu[BLOCK_4x4].intra_pred[26] = PFX(intra_pred_ang4_26_lsx);
        p.cu[BLOCK_4x4].intra_pred[27] = PFX(intra_pred_ang4_27_lsx);
        p.cu[BLOCK_4x4].intra_pred[28] = PFX(intra_pred_ang4_28_lsx);
        p.cu[BLOCK_4x4].intra_pred[29] = PFX(intra_pred_ang4_29_lsx);
        p.cu[BLOCK_4x4].intra_pred[30] = PFX(intra_pred_ang4_30_lsx);
        p.cu[BLOCK_4x4].intra_pred[31] = PFX(intra_pred_ang4_31_lsx);
        p.cu[BLOCK_4x4].intra_pred[32] = PFX(intra_pred_ang4_32_lsx);
        p.cu[BLOCK_4x4].intra_pred[33] = PFX(intra_pred_ang4_33_lsx);
        p.cu[BLOCK_4x4].intra_pred[34] = PFX(intra_pred_ang4_34_lsx);

        p.cu[BLOCK_8x8].intra_pred[2] = PFX(intra_pred_ang8_2_lsx);
        p.cu[BLOCK_8x8].intra_pred[3] = PFX(intra_pred_ang8_3_lsx);
        p.cu[BLOCK_8x8].intra_pred[4] = PFX(intra_pred_ang8_4_lsx);
        p.cu[BLOCK_8x8].intra_pred[5] = PFX(intra_pred_ang8_5_lsx);
        p.cu[BLOCK_8x8].intra_pred[6] = PFX(intra_pred_ang8_6_lsx);
        p.cu[BLOCK_8x8].intra_pred[7] = PFX(intra_pred_ang8_7_lsx);
        p.cu[BLOCK_8x8].intra_pred[8] = PFX(intra_pred_ang8_8_lsx);
        p.cu[BLOCK_8x8].intra_pred[9] = PFX(intra_pred_ang8_9_lsx);
        p.cu[BLOCK_8x8].intra_pred[10] = PFX(intra_pred_ang8_10_lsx);
        p.cu[BLOCK_8x8].intra_pred[11] = PFX(intra_pred_ang8_11_lsx);
        p.cu[BLOCK_8x8].intra_pred[12] = PFX(intra_pred_ang8_12_lsx);
        p.cu[BLOCK_8x8].intra_pred[13] = PFX(intra_pred_ang8_13_lsx);
        p.cu[BLOCK_8x8].intra_pred[14] = PFX(intra_pred_ang8_14_lsx);
        p.cu[BLOCK_8x8].intra_pred[15] = PFX(intra_pred_ang8_15_lsx);
        p.cu[BLOCK_8x8].intra_pred[16] = PFX(intra_pred_ang8_16_lsx);
        p.cu[BLOCK_8x8].intra_pred[17] = PFX(intra_pred_ang8_17_lsx);
        p.cu[BLOCK_8x8].intra_pred[18] = PFX(intra_pred_ang8_18_lsx);
        p.cu[BLOCK_8x8].intra_pred[19] = PFX(intra_pred_ang8_17_lsx);
        p.cu[BLOCK_8x8].intra_pred[20] = PFX(intra_pred_ang8_16_lsx);
        p.cu[BLOCK_8x8].intra_pred[21] = PFX(intra_pred_ang8_15_lsx);
        p.cu[BLOCK_8x8].intra_pred[22] = PFX(intra_pred_ang8_14_lsx);
        p.cu[BLOCK_8x8].intra_pred[23] = PFX(intra_pred_ang8_13_lsx);
        p.cu[BLOCK_8x8].intra_pred[24] = PFX(intra_pred_ang8_12_lsx);
        p.cu[BLOCK_8x8].intra_pred[25] = PFX(intra_pred_ang8_11_lsx);
        p.cu[BLOCK_8x8].intra_pred[26] = PFX(intra_pred_ang8_26_lsx);
        p.cu[BLOCK_8x8].intra_pred[27] = PFX(intra_pred_ang8_9_lsx);
        p.cu[BLOCK_8x8].intra_pred[28] = PFX(intra_pred_ang8_8_lsx);
        p.cu[BLOCK_8x8].intra_pred[29] = PFX(intra_pred_ang8_7_lsx);
        p.cu[BLOCK_8x8].intra_pred[30] = PFX(intra_pred_ang8_6_lsx);
        p.cu[BLOCK_8x8].intra_pred[31] = PFX(intra_pred_ang8_5_lsx);
        p.cu[BLOCK_8x8].intra_pred[32] = PFX(intra_pred_ang8_4_lsx);
        p.cu[BLOCK_8x8].intra_pred[33] = PFX(intra_pred_ang8_3_lsx);
        p.cu[BLOCK_8x8].intra_pred[34] = PFX(intra_pred_ang8_34_lsx);

        p.cu[BLOCK_16x16].intra_pred[2] = PFX(intra_pred_ang16_2_lsx);
        p.cu[BLOCK_16x16].intra_pred[3] = PFX(intra_pred_ang16_3_lsx);
        p.cu[BLOCK_16x16].intra_pred[4] = PFX(intra_pred_ang16_4_lsx);
        p.cu[BLOCK_16x16].intra_pred[5] = PFX(intra_pred_ang16_5_lsx);
        p.cu[BLOCK_16x16].intra_pred[6] = PFX(intra_pred_ang16_6_lsx);
        p.cu[BLOCK_16x16].intra_pred[7] = PFX(intra_pred_ang16_7_lsx);
        p.cu[BLOCK_16x16].intra_pred[8] = PFX(intra_pred_ang16_8_lsx);
        p.cu[BLOCK_16x16].intra_pred[9] = PFX(intra_pred_ang16_9_lsx);
        p.cu[BLOCK_16x16].intra_pred[10] = PFX(intra_pred_ang16_10_lsx);
        p.cu[BLOCK_16x16].intra_pred[11] = PFX(intra_pred_ang16_11_lsx);
        p.cu[BLOCK_16x16].intra_pred[12] = PFX(intra_pred_ang16_12_lsx);
        p.cu[BLOCK_16x16].intra_pred[13] = PFX(intra_pred_ang16_13_lsx);
        p.cu[BLOCK_16x16].intra_pred[14] = PFX(intra_pred_ang16_14_lsx);
        p.cu[BLOCK_16x16].intra_pred[15] = PFX(intra_pred_ang16_15_lsx);
        p.cu[BLOCK_16x16].intra_pred[16] = PFX(intra_pred_ang16_16_lsx);
        p.cu[BLOCK_16x16].intra_pred[17] = PFX(intra_pred_ang16_17_lsx);
        p.cu[BLOCK_16x16].intra_pred[18] = PFX(intra_pred_ang16_18_lsx);
        p.cu[BLOCK_16x16].intra_pred[19] = PFX(intra_pred_ang16_19_lsx);
        p.cu[BLOCK_16x16].intra_pred[20] = PFX(intra_pred_ang16_20_lsx);
        p.cu[BLOCK_16x16].intra_pred[21] = PFX(intra_pred_ang16_21_lsx);
        p.cu[BLOCK_16x16].intra_pred[22] = PFX(intra_pred_ang16_22_lsx);
        p.cu[BLOCK_16x16].intra_pred[23] = PFX(intra_pred_ang16_23_lsx);
        p.cu[BLOCK_16x16].intra_pred[24] = PFX(intra_pred_ang16_24_lsx);
        p.cu[BLOCK_16x16].intra_pred[25] = PFX(intra_pred_ang16_25_lsx);
        p.cu[BLOCK_16x16].intra_pred[26] = PFX(intra_pred_ang16_26_lsx);
        p.cu[BLOCK_16x16].intra_pred[27] = PFX(intra_pred_ang16_27_lsx);
        p.cu[BLOCK_16x16].intra_pred[28] = PFX(intra_pred_ang16_28_lsx);
        p.cu[BLOCK_16x16].intra_pred[29] = PFX(intra_pred_ang16_29_lsx);
        p.cu[BLOCK_16x16].intra_pred[30] = PFX(intra_pred_ang16_30_lsx);
        p.cu[BLOCK_16x16].intra_pred[31] = PFX(intra_pred_ang16_31_lsx);
        p.cu[BLOCK_16x16].intra_pred[32] = PFX(intra_pred_ang16_32_lsx);
        p.cu[BLOCK_16x16].intra_pred[33] = PFX(intra_pred_ang16_33_lsx);
        p.cu[BLOCK_16x16].intra_pred[34] = PFX(intra_pred_ang16_2_lsx);

        p.cu[BLOCK_32x32].intra_pred[2] = PFX(intra_pred_ang32_2_lsx);
        p.cu[BLOCK_32x32].intra_pred[3] = PFX(intra_pred_ang32_3_lsx);
        p.cu[BLOCK_32x32].intra_pred[4] = PFX(intra_pred_ang32_4_lsx);
        p.cu[BLOCK_32x32].intra_pred[5] = PFX(intra_pred_ang32_5_lsx);
        p.cu[BLOCK_32x32].intra_pred[6] = PFX(intra_pred_ang32_6_lsx);
        p.cu[BLOCK_32x32].intra_pred[7] = PFX(intra_pred_ang32_7_lsx);
        p.cu[BLOCK_32x32].intra_pred[8] = PFX(intra_pred_ang32_8_lsx);
        p.cu[BLOCK_32x32].intra_pred[9] = PFX(intra_pred_ang32_9_lsx);
        p.cu[BLOCK_32x32].intra_pred[10] = PFX(intra_pred_ang32_10_lsx);
        p.cu[BLOCK_32x32].intra_pred[11] = PFX(intra_pred_ang32_11_lsx);
        p.cu[BLOCK_32x32].intra_pred[12] = PFX(intra_pred_ang32_12_lsx);
        p.cu[BLOCK_32x32].intra_pred[13] = PFX(intra_pred_ang32_13_lsx);
        p.cu[BLOCK_32x32].intra_pred[14] = PFX(intra_pred_ang32_14_lsx);
        p.cu[BLOCK_32x32].intra_pred[15] = PFX(intra_pred_ang32_15_lsx);
        p.cu[BLOCK_32x32].intra_pred[16] = PFX(intra_pred_ang32_16_lsx);
        p.cu[BLOCK_32x32].intra_pred[17] = PFX(intra_pred_ang32_17_lsx);
        p.cu[BLOCK_32x32].intra_pred[18] = PFX(intra_pred_ang32_18_lsx);
        p.cu[BLOCK_32x32].intra_pred[19] = PFX(intra_pred_ang32_19_lsx);
        p.cu[BLOCK_32x32].intra_pred[20] = PFX(intra_pred_ang32_20_lsx);
        p.cu[BLOCK_32x32].intra_pred[21] = PFX(intra_pred_ang32_21_lsx);
        p.cu[BLOCK_32x32].intra_pred[22] = PFX(intra_pred_ang32_22_lsx);
        p.cu[BLOCK_32x32].intra_pred[23] = PFX(intra_pred_ang32_23_lsx);
        p.cu[BLOCK_32x32].intra_pred[24] = PFX(intra_pred_ang32_24_lsx);
        p.cu[BLOCK_32x32].intra_pred[25] = PFX(intra_pred_ang32_25_lsx);
        p.cu[BLOCK_32x32].intra_pred[26] = PFX(intra_pred_ang32_26_lsx);
        p.cu[BLOCK_32x32].intra_pred[27] = PFX(intra_pred_ang32_27_lsx);
        p.cu[BLOCK_32x32].intra_pred[28] = PFX(intra_pred_ang32_28_lsx);
        p.cu[BLOCK_32x32].intra_pred[29] = PFX(intra_pred_ang32_29_lsx);
        p.cu[BLOCK_32x32].intra_pred[30] = PFX(intra_pred_ang32_30_lsx);
        p.cu[BLOCK_32x32].intra_pred[31] = PFX(intra_pred_ang32_31_lsx);
        p.cu[BLOCK_32x32].intra_pred[32] = PFX(intra_pred_ang32_32_lsx);
        p.cu[BLOCK_32x32].intra_pred[33] = PFX(intra_pred_ang32_33_lsx);
        p.cu[BLOCK_32x32].intra_pred[34] = PFX(intra_pred_ang32_34_lsx);

        //intra_pred_dc
        p.cu[BLOCK_4x4].intra_pred[DC_IDX] = PFX(intra_pred_dc4_lsx);
        p.cu[BLOCK_8x8].intra_pred[DC_IDX] = PFX(intra_pred_dc8_lsx);
        p.cu[BLOCK_16x16].intra_pred[DC_IDX] = PFX(intra_pred_dc16_lsx);
        p.cu[BLOCK_32x32].intra_pred[DC_IDX] = PFX(intra_pred_dc32_lsx);
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

        // intra_filter
        p.cu[BLOCK_4x4].intra_filter = PFX(intra_filter_4x4_lasx);
        p.cu[BLOCK_8x8].intra_filter = PFX(intra_filter_8x8_lasx);
        p.cu[BLOCK_16x16].intra_filter = PFX(intra_filter_16x16_lasx);
        p.cu[BLOCK_32x32].intra_filter = PFX(intra_filter_32x32_lasx);

        //intra_pred
        p.cu[BLOCK_8x8].intra_pred[3] = PFX(intra_pred_ang8_3_lasx);
        p.cu[BLOCK_8x8].intra_pred[4] = PFX(intra_pred_ang8_4_lasx);
        p.cu[BLOCK_8x8].intra_pred[5] = PFX(intra_pred_ang8_5_lasx);
        p.cu[BLOCK_8x8].intra_pred[6] = PFX(intra_pred_ang8_6_lasx);
        p.cu[BLOCK_8x8].intra_pred[7] = PFX(intra_pred_ang8_7_lasx);
        p.cu[BLOCK_8x8].intra_pred[8] = PFX(intra_pred_ang8_8_lasx);
        p.cu[BLOCK_8x8].intra_pred[9] = PFX(intra_pred_ang8_9_lasx);
        p.cu[BLOCK_8x8].intra_pred[11] = PFX(intra_pred_ang8_11_lasx);
        p.cu[BLOCK_8x8].intra_pred[12] = PFX(intra_pred_ang8_12_lasx);
        p.cu[BLOCK_8x8].intra_pred[13] = PFX(intra_pred_ang8_13_lasx);
        p.cu[BLOCK_8x8].intra_pred[14] = PFX(intra_pred_ang8_14_lasx);
        p.cu[BLOCK_8x8].intra_pred[15] = PFX(intra_pred_ang8_15_lasx);
        p.cu[BLOCK_8x8].intra_pred[16] = PFX(intra_pred_ang8_16_lasx);
        p.cu[BLOCK_8x8].intra_pred[17] = PFX(intra_pred_ang8_17_lasx);
        p.cu[BLOCK_8x8].intra_pred[19] = PFX(intra_pred_ang8_19_lasx);
        p.cu[BLOCK_8x8].intra_pred[20] = PFX(intra_pred_ang8_20_lasx);
        p.cu[BLOCK_8x8].intra_pred[21] = PFX(intra_pred_ang8_21_lasx);
        p.cu[BLOCK_8x8].intra_pred[22] = PFX(intra_pred_ang8_22_lasx);
        p.cu[BLOCK_8x8].intra_pred[23] = PFX(intra_pred_ang8_23_lasx);
        p.cu[BLOCK_8x8].intra_pred[24] = PFX(intra_pred_ang8_24_lasx);
        p.cu[BLOCK_8x8].intra_pred[25] = PFX(intra_pred_ang8_25_lasx);
        p.cu[BLOCK_8x8].intra_pred[27] = PFX(intra_pred_ang8_27_lasx);
        p.cu[BLOCK_8x8].intra_pred[28] = PFX(intra_pred_ang8_28_lasx);
        p.cu[BLOCK_8x8].intra_pred[29] = PFX(intra_pred_ang8_29_lasx);
        p.cu[BLOCK_8x8].intra_pred[30] = PFX(intra_pred_ang8_30_lasx);
        p.cu[BLOCK_8x8].intra_pred[31] = PFX(intra_pred_ang8_31_lasx);
        p.cu[BLOCK_8x8].intra_pred[32] = PFX(intra_pred_ang8_32_lasx);
        p.cu[BLOCK_8x8].intra_pred[33] = PFX(intra_pred_ang8_33_lasx);
        p.cu[BLOCK_16x16].intra_pred[3] = PFX(intra_pred_ang16_3_lasx);
        p.cu[BLOCK_16x16].intra_pred[4] = PFX(intra_pred_ang16_4_lasx);
        p.cu[BLOCK_16x16].intra_pred[5] = PFX(intra_pred_ang16_5_lasx);
        p.cu[BLOCK_16x16].intra_pred[6] = PFX(intra_pred_ang16_6_lasx);
        p.cu[BLOCK_16x16].intra_pred[7] = PFX(intra_pred_ang16_7_lasx);
        p.cu[BLOCK_16x16].intra_pred[8] = PFX(intra_pred_ang16_8_lasx);
        p.cu[BLOCK_16x16].intra_pred[9] = PFX(intra_pred_ang16_9_lasx);
        p.cu[BLOCK_16x16].intra_pred[11] = PFX(intra_pred_ang16_11_lasx);
        p.cu[BLOCK_16x16].intra_pred[12] = PFX(intra_pred_ang16_12_lasx);
        p.cu[BLOCK_16x16].intra_pred[13] = PFX(intra_pred_ang16_13_lasx);
        p.cu[BLOCK_16x16].intra_pred[14] = PFX(intra_pred_ang16_14_lasx);
        p.cu[BLOCK_16x16].intra_pred[15] = PFX(intra_pred_ang16_15_lasx);
        p.cu[BLOCK_16x16].intra_pred[16] = PFX(intra_pred_ang16_16_lasx);
        p.cu[BLOCK_16x16].intra_pred[17] = PFX(intra_pred_ang16_17_lasx);
    }
#endif /* HAVE_LASX */
}

}
