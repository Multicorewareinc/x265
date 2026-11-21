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
    X265_CHECK(count >= 0 && count < X265_BPYRAMID_MAX_NODES, "bpyramid plan overflow\n");
    if (count < 0 || count >= X265_BPYRAMID_MAX_NODES)
        return count;
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
        for (int b = p0 + 1; b < p1 && count < X265_BPYRAMID_MAX_NODES; b++, count++)
            plan[count] = { b, p0, p1, layer, false, count + 1 };
    }
    return count;
}

inline constexpr BPyramidNode g_bPyramidPlans[][16] = {
    {
        {},
    },
    {
        {1, 0, 2, 1, false, 1},
    },
    {
        {1, 0, 3, 1, false, 1},
        {2, 0, 3, 1, false, 2},
    },
    {
        {2, 0, 4, 1, true, 3},
        {1, 0, 2, 2, false, 2},
        {3, 2, 4, 2, false, 3},
    },
    {
        {2, 0, 5, 1, true, 4},
        {1, 0, 2, 2, false, 2},
        {3, 2, 5, 2, false, 3},
        {4, 2, 5, 2, false, 4},
    },
    {
        {3, 0, 6, 1, true, 5},
        {1, 0, 3, 2, false, 2},
        {2, 0, 3, 2, false, 3},
        {4, 3, 6, 2, false, 4},
        {5, 3, 6, 2, false, 5},
    },
    {
        {3, 0, 7, 1, true, 6},
        {1, 0, 3, 2, false, 2},
        {2, 0, 3, 2, false, 3},
        {5, 3, 7, 2, true, 6},
        {4, 3, 5, 3, false, 5},
        {6, 5, 7, 3, false, 6},
    },
    {
        {4, 0, 8, 1, true, 7},
        {2, 0, 4, 2, true, 4},
        {1, 0, 2, 3, false, 3},
        {3, 2, 4, 3, false, 4},
        {6, 4, 8, 2, true, 7},
        {5, 4, 6, 3, false, 6},
        {7, 6, 8, 3, false, 7},
    },
    {
        {4, 0, 9, 1, true, 8},
        {2, 0, 4, 2, true, 4},
        {1, 0, 2, 3, false, 3},
        {3, 2, 4, 3, false, 4},
        {6, 4, 9, 2, true, 8},
        {5, 4, 6, 3, false, 6},
        {7, 6, 9, 3, false, 7},
        {8, 6, 9, 3, false, 8},
    },
    {
        {5, 0, 10, 1, true, 9},
        {2, 0, 5, 2, true, 5},
        {1, 0, 2, 3, false, 3},
        {3, 2, 5, 3, false, 4},
        {4, 2, 5, 3, false, 5},
        {7, 5, 10, 2, true, 9},
        {6, 5, 7, 3, false, 7},
        {8, 7, 10, 3, false, 8},
        {9, 7, 10, 3, false, 9},
    },
    {
        {5, 0, 11, 1, true, 10},
        {2, 0, 5, 2, true, 5},
        {1, 0, 2, 3, false, 3},
        {3, 2, 5, 3, false, 4},
        {4, 2, 5, 3, false, 5},
        {8, 5, 11, 2, true, 10},
        {6, 5, 8, 3, false, 7},
        {7, 5, 8, 3, false, 8},
        {9, 8, 11, 3, false, 9},
        {10, 8, 11, 3, false, 10},
    },
    {
        {6, 0, 12, 1, true, 11},
        {3, 0, 6, 2, true, 6},
        {1, 0, 3, 3, false, 3},
        {2, 0, 3, 3, false, 4},
        {4, 3, 6, 3, false, 5},
        {5, 3, 6, 3, false, 6},
        {9, 6, 12, 2, true, 11},
        {7, 6, 9, 3, false, 8},
        {8, 6, 9, 3, false, 9},
        {10, 9, 12, 3, false, 10},
        {11, 9, 12, 3, false, 11},
    },
    {
        {6, 0, 13, 1, true, 12},
        {3, 0, 6, 2, true, 6},
        {1, 0, 3, 3, false, 3},
        {2, 0, 3, 3, false, 4},
        {4, 3, 6, 3, false, 5},
        {5, 3, 6, 3, false, 6},
        {9, 6, 13, 2, true, 12},
        {7, 6, 9, 3, false, 8},
        {8, 6, 9, 3, false, 9},
        {11, 9, 13, 3, true, 12},
        {10, 9, 11, 4, false, 11},
        {12, 11, 13, 4, false, 12},
    },
    {
        {7, 0, 14, 1, true, 13},
        {3, 0, 7, 2, true, 7},
        {1, 0, 3, 3, false, 3},
        {2, 0, 3, 3, false, 4},
        {5, 3, 7, 3, true, 7},
        {4, 3, 5, 4, false, 6},
        {6, 5, 7, 4, false, 7},
        {10, 7, 14, 2, true, 13},
        {8, 7, 10, 3, false, 9},
        {9, 7, 10, 3, false, 10},
        {12, 10, 14, 3, true, 13},
        {11, 10, 12, 4, false, 12},
        {13, 12, 14, 4, false, 13},
    },
    {
        {7, 0, 15, 1, true, 14},
        {3, 0, 7, 2, true, 7},
        {1, 0, 3, 3, false, 3},
        {2, 0, 3, 3, false, 4},
        {5, 3, 7, 3, true, 7},
        {4, 3, 5, 4, false, 6},
        {6, 5, 7, 4, false, 7},
        {11, 7, 15, 2, true, 14},
        {9, 7, 11, 3, true, 11},
        {8, 7, 9, 4, false, 10},
        {10, 9, 11, 4, false, 11},
        {13, 11, 15, 3, true, 14},
        {12, 11, 13, 4, false, 13},
        {14, 13, 15, 4, false, 14},
    },
    {
        {8, 0, 16, 1, true, 15},
        {4, 0, 8, 2, true, 8},
        {2, 0, 4, 3, true, 5},
        {1, 0, 2, 4, false, 4},
        {3, 2, 4, 4, false, 5},
        {6, 4, 8, 3, true, 8},
        {5, 4, 6, 4, false, 7},
        {7, 6, 8, 4, false, 8},
        {12, 8, 16, 2, true, 15},
        {10, 8, 12, 3, true, 12},
        {9, 8, 10, 4, false, 11},
        {11, 10, 12, 4, false, 12},
        {14, 12, 16, 3, true, 15},
        {13, 12, 14, 4, false, 14},
        {15, 14, 16, 4, false, 15},
    },
    {
        {8, 0, 17, 1, true, 16},
        {4, 0, 8, 2, true, 8},
        {2, 0, 4, 3, true, 5},
        {1, 0, 2, 4, false, 4},
        {3, 2, 4, 4, false, 5},
        {6, 4, 8, 3, true, 8},
        {5, 4, 6, 4, false, 7},
        {7, 6, 8, 4, false, 8},
        {12, 8, 17, 2, true, 16},
        {10, 8, 12, 3, true, 12},
        {9, 8, 10, 4, false, 11},
        {11, 10, 12, 4, false, 12},
        {14, 12, 17, 3, true, 16},
        {13, 12, 14, 4, false, 14},
        {15, 14, 17, 4, false, 15},
        {16, 14, 17, 4, false, 16},
    },
};

static const int s_bPyramidPlanCount = sizeof(g_bPyramidPlans) / sizeof(g_bPyramidPlans[0]);

/* The hierarchical B-pyramid of the B frames between anchors p0 and p1, in coding order: a run of
 * more than 2 B frames gets a B-ref in its middle, which is coded first, predicted from the run's
 * anchors and splits the run in two, recursively. The B frames of a run of 2 or less reference its
 * anchors. B-ref placement, coding order, layers, references and the lookahead / CU-tree walks
 * all follow this plan. Returns the number of B frames (p1 - p0 - 1). */
static inline int bPyramidPlan(int p0, int p1, BPyramidNode* plan)
{
    const int numB = p1 - p0 - 1;
    if (numB <= 0)
        return 0;
    if (numB >= s_bPyramidPlanCount)
        return bPyramidPlanRange(p0, p1, 1, plan, 0);

    for (int k = 0; k < numB; k++)
    {
        plan[k] = g_bPyramidPlans[numB][k];
        plan[k].b += p0;
        plan[k].p0 += p0;
        plan[k].p1 += p0;
    }
    return numB;
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
    if (span > 0 && span <= s_bPyramidPlanCount)
    {
        int maxLayer = 1;
        for (int k = 0; k < span - 1; k++)
            maxLayer = X265_MAX(maxLayer, g_bPyramidPlans[span - 1][k].layer);
        return maxLayer;
    }
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
    if (span > 0 && span <= s_bPyramidPlanCount)
    {
        for (int k = 0; k < span - 1; k++)
            if (g_bPyramidPlans[span - 1][k].b == b)
                return g_bPyramidPlans[span - 1][k].layer;
        return 1;
    }
    BPyramidNode plan[X265_BPYRAMID_MAX_NODES];
    const int count = bPyramidPlan(0, X265_MIN(span, X265_BPYRAMID_MAX_NODES), plan);
    for (int k = 0; k < count; k++)
        if (plan[k].b == b)
            return plan[k].layer;
    return 1;
}

}

#endif // ifndef X265_BPYRAMID_H
