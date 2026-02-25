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

namespace X265_NS {
// private x265 namespace


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

    }
#endif /* HAVE_LSX */

#if HAVE_LASX
    if (cpuMask & X265_CPU_LASX)
    {
        // loopfilter
        p.sign = PFX(calSign_lasx);
    }
#endif /* HAVE_LASX */
}

}
