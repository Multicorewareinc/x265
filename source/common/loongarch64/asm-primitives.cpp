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
    }
#endif /* HAVE_LASX */
}

}
