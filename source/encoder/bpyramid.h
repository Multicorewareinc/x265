/*****************************************************************************
 * Copyright (C) 2013-2020 MulticoreWare, Inc
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

#ifndef X265_BPYRAMID_H
#define X265_BPYRAMID_H 1

#include "common.h"

namespace X265_NS {
// encoder private namespace

/* One B frame of a hierarchical B-pyramid (--b-pyramid 2) mini-GOP */
struct BPyramidNode
{
    int  b;      /* the frame */
    int  p0, p1; /* the anchors or B-refs it is predicted from */
    int  layer;  /* pyramid layer, 1 at the top */
    bool bRef;   /* a B-ref, splitting [p0, p1] at b */
    int  end;    /* plan index past the frames that reference it (its subtree); next index if !bRef */
};

/* Enough nodes for any mini-GOP of the lookahead */
#define X265_BPYRAMID_MAX_NODES (X265_LOOKAHEAD_MAX + 1)

static inline int bPyramidPlanRange(int p0, int p1, int layer, BPyramidNode* plan, int count)
{
    if (p1 - p0 > 3)
    {
        const int k = count++;
        const int mid = p0 + (p1 - p0) / 2;
        plan[k] = { mid, p0, p1, layer, true, 0 };
        count = bPyramidPlanRange(p0, mid, layer + 1, plan, count);
        count = bPyramidPlanRange(mid, p1, layer + 1, plan, count);
        plan[k].end = count;
    }
    else
    {
        for (int b = p0 + 1; b < p1; b++, count++)
            plan[count] = { b, p0, p1, layer, false, count + 1 };
    }
    return count;
}

/* The hierarchical B-pyramid of the B frames between anchors p0 and p1, in coding order: a run of
 * more than 2 B frames gets a B-ref in its middle, which is coded first, predicted from the run's
 * anchors and splits the run in two, recursively. The B frames of a run of 2 or less reference its
 * anchors. B-ref placement, coding order, layers, references and the lookahead / CU-tree walks
 * all follow this plan. Returns the number of B frames (p1 - p0 - 1). */
static inline int bPyramidPlan(int p0, int p1, BPyramidNode* plan)
{
    return p1 - p0 > 1 ? bPyramidPlanRange(p0, p1, 1, plan, 0) : 0;
}

/* L0 reference of a node of the plan between p0 and p1 when p0 may be unusable (the leading
 * pictures of an IDR): the frames referencing p0 itself are then predicted from L1 only */
static inline int bPyramidRef0(const BPyramidNode& node, int p0, bool bP0Avail)
{
    return bP0Avail || node.p0 != p0 ? node.p0 : node.b;
}

/* Deepest pyramid layer of a mini-GOP of this length (POC distance between its anchors):
 * 2 or 3 tops out at layer 1, 4 to 7 at layer 2, 8 to 15 at layer 3 */
static inline int bPyramidMaxLayer(int span)
{
    BPyramidNode plan[X265_BPYRAMID_MAX_NODES];
    const int count = bPyramidPlan(0, X265_MIN(span, X265_BPYRAMID_MAX_NODES), plan);
    int maxLayer = 1;
    for (int k = 0; k < count; k++)
        maxLayer = X265_MAX(maxLayer, plan[k].layer);
    return maxLayer;
}

/* Pyramid layer of frame b (0 < b < span) of a mini-GOP of this length */
static inline int bPyramidLayerOf(int span, int b)
{
    BPyramidNode plan[X265_BPYRAMID_MAX_NODES];
    const int count = bPyramidPlan(0, X265_MIN(span, X265_BPYRAMID_MAX_NODES), plan);
    for (int k = 0; k < count; k++)
        if (plan[k].b == b)
            return plan[k].layer;
    return 1;
}

}

#endif // ifndef X265_BPYRAMID_H
