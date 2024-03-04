#pragma once

#include "loopfilter-prim.h"
#include "sao.h"

// completely skip this file without SVE
#if defined(HAVE_SVE)

#include <arm_sve.h>

namespace X265_NS
{

#if !HIGH_BIT_DEPTH
typedef svuint8_t svpixel_t;
#else
typedef svuint16_t svpixel_t;
#endif

enum {
    SVE = 1,
    SVE2 = 2
};

/* get the sign of input variable (TODO: this is a dup, make common) */
static inline int8_t signOf(int x)
{
    return (x >> 31) | ((int)(
        (((uint32_t)-x))
        >> 31));
}

static inline int signOf2(const int a, const int b)
{
    // NOTE: don't reorder below compare, both ICL, VC, GCC optimize strong depends on order!
    int r = 0;
    if (a < b)
        r = -1;
    if (a > b)
        r = 1;
    return r;
}

static inline svint8_t signOf2_sve(svbool_t predicate, svuint8_t a, svuint8_t b)
{
    svbool_t gt = svcmpgt(predicate, a, b);
    svbool_t lt = svcmplt(predicate, a, b);

    svint8_t result;
    result = svdup_n_s8_z(        gt,  1);
    result = svdup_n_s8_m(result, lt, -1);
    return result;
}

static inline svint8_t signOf2_sve_narrowing(svbool_t predicate_16_low, svbool_t predicate_16_high,
                                             svuint16_t a_low, svuint16_t b_low,
                                             svuint16_t a_high, svuint16_t b_high)
{
    svbool_t gt_low = svcmpgt(predicate_16_low, a_low, b_low);
    svbool_t lt_low = svcmplt(predicate_16_low, a_low, b_low);

    svbool_t gt_high = svcmpgt(predicate_16_high, a_high, b_high);
    svbool_t lt_high = svcmplt(predicate_16_high, a_high, b_high);

    svbool_t gt = svuzp1_b8(gt_low, gt_high);
    svbool_t lt = svuzp1_b8(lt_low, lt_high);

    svint8_t result;
    result = svdup_n_s8_z(        gt,  1);
    result = svdup_n_s8_m(result, lt, -1);
    return result;
}

// These three functions are templated helpers for saoCuStatsE* which use a different
// implementation for SVE1 and SVE2.
template<int SVE_VERSION, typename STATS_TYPE>
STATS_TYPE saoCuStatsE_stats_initilializer();

template<int SVE_VERSION, typename STATS_TYPE>
int32_t saoCuStatsE_stats_final_sum(STATS_TYPE stat);

template<int SVE_VERSION, typename STATS_TYPE>
void saoCuStatsE_stats_sum(STATS_TYPE &edge_sums, uint32_t &edge_count,
                                                 int edge_type_mask, svint8_t edge_vals,
                                                 svint16_t diff_vals_0, svint16_t diff_vals_1,
                                                 svbool_t predicate);

// Specializations for the helper functions using SVE1
// The SVE1 implementation for the temporary stats aggregator uses only an int32_t
template <>
inline int32_t saoCuStatsE_stats_initilializer<SVE, int32_t>()
{
    return 0;
}

// Similarly, for SVE1, no special code is required to complete the final sum of the stats.
template <>
inline int32_t saoCuStatsE_stats_final_sum<SVE, int32_t>(int32_t stat)
{
    return stat;
}

// For SVE1
template<>
inline void saoCuStatsE_stats_sum<SVE, int32_t>(int32_t &edge_sums, uint32_t &edge_count,
                                                 int edge_type_mask, svint8_t edge_vals,
                                                 svint16_t diff_vals_low, svint16_t diff_vals_high,
                                                 svbool_t predicate)
{
    // this function builds a mask to only include diff_vals in positions where edge_vals == edge_type_mask
    // this should be hoisted by the compiler out of the loop
    svint8_t edge_comparator = svdup_s8(edge_type_mask);

    // this mask will be 0 in positions where edge_vals != edge_type_mask
    svbool_t edge_mask = svcmpeq_s8(predicate, edge_comparator, edge_vals);
    svbool_t edge_mask_low  = svunpklo(edge_mask);
    svbool_t edge_mask_high = svunpkhi(edge_mask);

    // count up the number of edge_vals == edge_type_mask
    edge_count += svcntp_b8(predicate, edge_mask);

    // accumlate diff_vals in positions where edge_vals == edge_type_mask using
    // add across vector
    edge_sums += svaddv_s16(edge_mask_low, diff_vals_low);
    edge_sums += svaddv_s16(edge_mask_high, diff_vals_high);
}

#if defined(HAVE_SVE2)
// Specializations for the helper functions using SVE2
// The SVE2 implementation for the temporary stats aggregator uses am svint32_t which
// is added up across vector in the final step
template<>
inline svint32_t saoCuStatsE_stats_initilializer<SVE2, svint32_t>()
{
    return svdup_s32(0);
}
template<>
inline int32_t saoCuStatsE_stats_final_sum<SVE2, svint32_t>(svint32_t stat)
{
    // add-across-vector for the final step
    // This type of add is expensive, so that's why we only use it at the end
    return svaddv_s32(svptrue_b8(), stat);
}
template<>
inline void saoCuStatsE_stats_sum<SVE2, svint32_t> (svint32_t &edge_sums, uint32_t &edge_count,
                                                 int edge_type_mask, svint8_t edge_vals,
                                                 svint16_t diff_vals_low, svint16_t diff_vals_high,
                                                 svbool_t predicate)
{
    // this function uses predication to include diff_vals in positions where edge_vals == edge_type_mask

    // this will be hoisted by the compiler out of the loop
    svuint8_t edge_comparator = svdup_s8(edge_type_mask);
    svint16_t zero = svdup_s16(0);

    // predicates to indicate positions where edge_vals == edge_type_mask
    svbool_t edge_mask = svcmpeq_s8(predicate, edge_comparator, edge_vals);

    // unpack the predicate mask for use on a 16-bit type
    svbool_t edge_mask_low  = svunpklo(edge_mask);
    svbool_t edge_mask_high = svunpkhi(edge_mask);

    // count up the number of edge_vals == edge_type_mask
    edge_count += svcntp_b8(predicate, edge_mask);

    // select only the diff_vals in positions where edge_vals == edge_type_mask
    diff_vals_low = svsel_s16(edge_mask_low, diff_vals_low, zero);
    diff_vals_high = svsel_s16(edge_mask_high, diff_vals_high, zero);

    // use accumulating add long pairwise to create partial sums of values from diff
    // This instruction is an SVE2 instruction which is less expensive than an
    // add-across-vector, so it's a cheaper way to keep partial running sums.
    edge_sums = svadalp_s32_m(svptrue_b8(), edge_sums, diff_vals_low);
    edge_sums = svadalp_s32_m(svptrue_b8(), edge_sums, diff_vals_high);
}
#endif

template <int SVE_VERSION, typename STATS_TYPE>
static inline void saoCuStatsE_stats_final_accumulate(
    int32_t *stats,
    int32_t *count,
    STATS_TYPE *tmp_stats_ptrs[5],
    uint32_t tmp_count[5]
    )
{
    // Manually unrolling this loop ensures that gcc 11.4 does not spill these values to the stack
    // and reload them. It also enables the compiler to shuffle the values in registers rather than
    // using more expensive indirection into memory. Finally it also enables the use of ldp (load pair)
    // and stp (store pair) for loading and storing the values in stats and count.
    stats[1] += saoCuStatsE_stats_final_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[0]);
    stats[2] += saoCuStatsE_stats_final_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[1]);
    stats[0] += saoCuStatsE_stats_final_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[2]);
    stats[3] += saoCuStatsE_stats_final_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[3]);
    stats[4] += saoCuStatsE_stats_final_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[4]);
    count[1] += tmp_count[0];
    count[2] += tmp_count[1];
    count[0] += tmp_count[2];
    count[3] += tmp_count[3];
    count[4] += tmp_count[4];
}

template<int SVE_VERSION, typename STATS_TYPE>
void saoCuStatsE0_sve(const int16_t *diff,
                       const pixel *rec,
                       intptr_t stride,
                       int endX,
                       int endY,
                       int32_t *stats,
                       int32_t *count)
{
    uint32_t tmp_count[5] = {0};

    // Temporary storage for stats
    // Since the size of svint32_t isn't known at compile time, the compiler won't
    // allow us to use `svint32_t tmp_stats[5]`. Instead we create an array of pointers
    // to a fixed number of svint32_t objects on the stack.
    // This is all eliminated by the compiler at -03 and all of these values
    // are stored in registers.
    STATS_TYPE *tmp_stats_ptrs[5];
    STATS_TYPE tmp_stats_0 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[0] = &tmp_stats_0;
    STATS_TYPE tmp_stats_1 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[1] = &tmp_stats_1;
    STATS_TYPE tmp_stats_2 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[2] = &tmp_stats_2;
    STATS_TYPE tmp_stats_3 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[3] = &tmp_stats_3;
    STATS_TYPE tmp_stats_4 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[4] = &tmp_stats_4;

    X265_CHECK(endX <= MAX_CU_SIZE, "endX too big\n");

    // build a predicate with just the last lane set for use in the splice below
    const svbool_t pg_one = svrev_b8(svptrue_pat_b8(SV_VL1));
    const svint8_t two = svdup_s8(2);

    for (int y = 0; y < endY; y++)
    {
        // prepopulate the signRight_prev vector with rec[0]
        svint8_t signRight_prev = svdup_s8(0);
        signRight_prev = svdup_s8(signOf2(rec[-1], rec[0]));
        svint8_t signRight;

        // Build a predicate (mask) to enable only lanes which represent values of
        // x which x < endX. This loop will run through a wasted iteration if endX == 0,
        // but it won't have adverse effects because the predicate will disable all lanes.
        int x = 0;
        svbool_t predicate = svwhilelt_b8(x, endX);
        do
        {
            // Unpack the predicate mask for use on a 16-bit type
            svbool_t predicate_16_low  = svunpklo(predicate);
            svbool_t predicate_16_high = svunpkhi(predicate);

            // Load rec[x] and rec[x+1]
            // Even though these instructions load overlapping data, it's less expensive
            // to use the load pipelines to get the data offset in this manner than it is
            // to load the data and use the vector pipelines to offset by one. This is because
            // the vector pipelines are already busy with other work.
#if !HIGH_BIT_DEPTH
            svpixel_t rec_x       = svld1(predicate, &rec[x]);
            svpixel_t rec_x_plus1 = svld1(predicate, &rec[x+1]);
            signRight = signOf2_sve(predicate, rec_x, rec_x_plus1);
#else

            svpixel_t rec_x_low        = svld1(predicate_16_low,  &rec[x]);
            svpixel_t rec_x_plus1_low  = svld1(predicate_16_low,  &rec[x+1]);
            svpixel_t rec_x_high       = svld1(predicate_16_high, &rec[x+svcnth()]);
            svpixel_t rec_x_plus1_high = svld1(predicate_16_high, &rec[x+1+svcnth()]);
            signRight = signOf2_sve_narrowing(predicate_16_low, predicate_16_high,
                                              rec_x_low, rec_x_plus1_low, rec_x_high, rec_x_plus1_high);
#endif

            // Splice together the signRight_prev and signRight vectors using one value from signRight_prev
            // at the beginning and the remaining values from signRight.
            svint8_t signRightShift = svdup_s8(0);
            signRightShift = svsplice(pg_one, signRight_prev, signRight);

            // signLeft = -signRight (previous iteration)
            svint8_t signLeft = svneg_x(predicate, signRightShift);
            // edge_type = signRight + signLeft + 2
            svint8_t edge_type = svadd_x(predicate, svadd_x(predicate, signRight, signLeft), two);

            // save the signRight_prev value for the next iteration
            signRight_prev = signRight;

            // Load diff[x] and diff[x+vector_width_int16]
            svint16_t diff_vals_low = svld1_s16(predicate_16_low, &diff[x]);
            svint16_t diff_vals_high = svld1_s16(predicate_16_high, &diff[x+svcnth()]);

            // Sum up the stats and counts. This loop is completely unrolled and the function call inlined.
            // This function is the main different between the SVE1 and SVE2 implementations. SVE2 includes
            // a add-pairwise-long instruction which is cheaper than the add-accross-vector instruction so it
            // gives an extra bump in performance.
            for (int i = 0; i < 5; i++)
            {
                saoCuStatsE_stats_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[i], tmp_count[i], i, edge_type,
                                                              diff_vals_low, diff_vals_high, predicate);
            }

            // increment x by the vector width, compute a new predicate (mask), and check for loop termination
            x += svcntb();
            predicate = svwhilelt_b8(x, endX);
        } while (svptest_any(svptrue_b8(), predicate));

        diff += MAX_CU_SIZE;
        rec += stride;
    }

    // Accumulate the stats and counts
    saoCuStatsE_stats_final_accumulate<SVE_VERSION, STATS_TYPE>(stats, count, tmp_stats_ptrs, tmp_count);
}

template<int SVE_VERSION, typename STATS_TYPE>
void saoCuStatsE1_sve(const int16_t *diff,
                      const pixel *rec,
                      intptr_t stride,
                      int8_t *upBuff1,
                      int endX, int endY,
                      int32_t *stats, int32_t *count)
{
    // This function is very similar to saoCuStatsE0_sve and most of the comments in that function
    // apply here as well, so the comments are not duplicated here.

    uint32_t tmp_count[5] = {0};

    STATS_TYPE *tmp_stats_ptrs[5];
    STATS_TYPE tmp_stats_0 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[0] = &tmp_stats_0;
    STATS_TYPE tmp_stats_1 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[1] = &tmp_stats_1;
    STATS_TYPE tmp_stats_2 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[2] = &tmp_stats_2;
    STATS_TYPE tmp_stats_3 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[3] = &tmp_stats_3;
    STATS_TYPE tmp_stats_4 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[4] = &tmp_stats_4;

    X265_CHECK(endX <= MAX_CU_SIZE, "endX check failure\n");
    X265_CHECK(endY <= MAX_CU_SIZE, "endY check failure\n");

    const svint8_t two = svdup_s8(2);

    for (int y = 0; y < endY; y++)
    {
        int x = 0;
        svbool_t predicate = svwhilelt_b8(x, endX);
        do
        {
            // Unpack the predicate mask for use on a 16-bit type
            svbool_t predicate_16_low  = svunpklo(predicate);
            svbool_t predicate_16_high = svunpkhi(predicate);

#if !HIGH_BIT_DEPTH
            svpixel_t rec_x                        = svld1(predicate, &rec[x]);
            svpixel_t rec_x_plus_stride            = svld1(predicate, &rec[x+stride]);
            svint8_t signDown = signOf2_sve(predicate, rec_x, rec_x_plus_stride);
#else
            svpixel_t rec_x_low                    = svld1(predicate_16_low, &rec[x]);
            svpixel_t rec_x_high                   = svld1(predicate_16_high, &rec[x+svcnth()]);
            svpixel_t rec_x_plus_stride_low        = svld1(predicate_16_low, &rec[x+stride]);
            svpixel_t rec_x_plus_stride_high       = svld1(predicate_16_high, &rec[x+stride+svcnth()]);
            svint8_t signDown = signOf2_sve_narrowing(predicate_16_low, predicate_16_high,
                                                      rec_x_low, rec_x_plus_stride_low, rec_x_high, rec_x_plus_stride_high);
#endif
            svint8_t upBuff1_x          = svld1_s8(predicate, &upBuff1[x]);

            svint8_t edge_type = svadd_x(predicate, svadd_x(predicate, signDown, upBuff1_x), two);
            upBuff1_x = svneg_x(predicate, signDown);
            svst1_s8(predicate, &upBuff1[x], upBuff1_x);

            // Load diff[x] and diff[x+vector_width_int16]
            svint16_t diff_vals_low = svld1_s16(predicate_16_low, &diff[x]);
            svint16_t diff_vals_high = svld1_s16(predicate_16_high, &diff[x+svcnth()]);

            for (int i = 0; i < 5; i++)
            {
                saoCuStatsE_stats_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[i], tmp_count[i], i, edge_type,
                                              diff_vals_low, diff_vals_high, predicate);
            }

            x += svcntb();
            predicate = svwhilelt_b8(x, endX);
        } while (svptest_any(svptrue_b8(), predicate));

        diff += MAX_CU_SIZE;
        rec += stride;
    }

    saoCuStatsE_stats_final_accumulate<SVE_VERSION, STATS_TYPE>(stats, count, tmp_stats_ptrs, tmp_count);
}

template<int SVE_VERSION, typename STATS_TYPE>
void saoCuStatsE2_sve(const int16_t *diff,
                      const pixel *rec,
                      intptr_t stride,
                      int8_t *upBuff1,
                      int8_t *upBufft,
                      int endX, int endY,
                      int32_t *stats, int32_t *count)
{
    // This function is very similar to saoCuStatsE0_sve and most of the comments in that function
    // apply here as well, so the comments are not duplicated here.

    uint32_t tmp_count[5] = {0};

    STATS_TYPE *tmp_stats_ptrs[5];
    STATS_TYPE tmp_stats_0 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[0] = &tmp_stats_0;
    STATS_TYPE tmp_stats_1 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[1] = &tmp_stats_1;
    STATS_TYPE tmp_stats_2 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[2] = &tmp_stats_2;
    STATS_TYPE tmp_stats_3 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[3] = &tmp_stats_3;
    STATS_TYPE tmp_stats_4 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[4] = &tmp_stats_4;

    X265_CHECK(endX <= MAX_CU_SIZE, "endX too big\n");

    const svint8_t two = svdup_s8(2);

    for (int y = 0; y < endY; y++)
    {
        upBufft[0] = signOf(rec[stride] - rec[-1]);
        int x = 0;
        svbool_t predicate = svwhilelt_b8(x, endX);
        do
        {
            // Unpack the predicate mask for use on a 16-bit type
            svbool_t predicate_16_low  = svunpklo(predicate);
            svbool_t predicate_16_high = svunpkhi(predicate);

#if !HIGH_BIT_DEPTH
            svpixel_t rec_x                        = svld1(predicate, &rec[x]);
            svpixel_t rec_x_plus_stride            = svld1(predicate, &rec[x+stride+1]);
            svint8_t signDown = signOf2_sve(predicate, rec_x, rec_x_plus_stride);
#else
            svpixel_t rec_x_low                    = svld1(predicate_16_low, &rec[x]);
            svpixel_t rec_x_high                   = svld1(predicate_16_high, &rec[x+svcnth()]);
            svpixel_t rec_x_plus_stride_low        = svld1(predicate_16_low, &rec[x+stride+1]);
            svpixel_t rec_x_plus_stride_high       = svld1(predicate_16_high, &rec[x+stride+1+svcnth()]);
            svint8_t signDown = signOf2_sve_narrowing(predicate_16_low, predicate_16_high,
                                                      rec_x_low, rec_x_plus_stride_low, rec_x_high, rec_x_plus_stride_high);
#endif
            svint8_t upBuff1_x                     = svld1_s8(predicate, &upBuff1[x]);

            svint8_t edge_type = svadd_s8_x(predicate, svadd_s8_x(predicate, signDown, upBuff1_x), two);
            svint8_t upBufft_x = svneg_s8_x(predicate, signDown);
            svst1_s8(predicate, &upBufft[x+1], upBufft_x);

            svint16_t diff_vals_low  = svld1_s16(predicate_16_low, &diff[x]);
            svint16_t diff_vals_high = svld1_s16(predicate_16_high, &diff[x+svcnth()]);

            for (int i = 0; i < 5; i++)
            {
                saoCuStatsE_stats_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[i], tmp_count[i], i, edge_type,
                                              diff_vals_low, diff_vals_high, predicate);
            }

            x += svcntb();
            predicate = svwhilelt_b8(x, endX);
        } while (svptest_any(svptrue_b8(), predicate));

        std::swap(upBuff1, upBufft);

        diff += MAX_CU_SIZE;
        rec += stride;
    }

    saoCuStatsE_stats_final_accumulate<SVE_VERSION, STATS_TYPE>(stats, count, tmp_stats_ptrs, tmp_count);
}

template<int SVE_VERSION, typename STATS_TYPE>
void saoCuStatsE3_sve(const int16_t *diff,
                      const pixel *rec,
                      intptr_t stride,
                      int8_t *upBuff1,
                      int endX, int endY,
                      int32_t *stats, int32_t *count)
{
    // This function is very similar to saoCuStatsE0_sve and most of the comments in that function
    // apply here as well, so the comments are not duplicated here.

    uint32_t tmp_count[5] = {0};

    STATS_TYPE *tmp_stats_ptrs[5];
    STATS_TYPE tmp_stats_0 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[0] = &tmp_stats_0;
    STATS_TYPE tmp_stats_1 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[1] = &tmp_stats_1;
    STATS_TYPE tmp_stats_2 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[2] = &tmp_stats_2;
    STATS_TYPE tmp_stats_3 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[3] = &tmp_stats_3;
    STATS_TYPE tmp_stats_4 = saoCuStatsE_stats_initilializer<SVE_VERSION, STATS_TYPE>(); tmp_stats_ptrs[4] = &tmp_stats_4;

    X265_CHECK(endX <= MAX_CU_SIZE, "endX check failure\n");
    X265_CHECK(endY <= MAX_CU_SIZE, "endY check failure\n");

    const svint8_t two = svdup_s8(2);

    for (int y = 0; y < endY; y++)
    {
        int x = 0;
        svbool_t predicate = svwhilelt_b8(x, endX);
        do
        {
            // Unpack the predicate mask for use on a 16-bit type
            svbool_t predicate_16_low  = svunpklo(predicate);
            svbool_t predicate_16_high = svunpkhi(predicate);

#if !HIGH_BIT_DEPTH
            svpixel_t rec_x                        = svld1(predicate, &rec[x]);
            svpixel_t rec_x_plus_stride            = svld1(predicate, &rec[x+stride-1]);
            svint8_t signDown = signOf2_sve(predicate, rec_x, rec_x_plus_stride);
#else
            svpixel_t rec_x_low                    = svld1(predicate_16_low, &rec[x]);
            svpixel_t rec_x_high                   = svld1(predicate_16_high, &rec[x+svcnth()]);
            svpixel_t rec_x_plus_stride_low        = svld1(predicate_16_low, &rec[x+stride-1]);
            svpixel_t rec_x_plus_stride_high       = svld1(predicate_16_high, &rec[x+stride-1+svcnth()]);
            svint8_t signDown = signOf2_sve_narrowing(predicate_16_low, predicate_16_high,
                                                      rec_x_low, rec_x_plus_stride_low, rec_x_high, rec_x_plus_stride_high);
#endif
            svint8_t upBuff1_x                     = svld1_s8(predicate, &upBuff1[x]);

            svint8_t edge_type = svadd_s8_x(predicate, svadd_s8_x(predicate, signDown, upBuff1_x), two);
            upBuff1_x = svneg_s8_x(predicate, signDown);
            svst1_s8(predicate, &upBuff1[x-1], upBuff1_x);

            svint16_t diff_vals_0 = svld1_s16(predicate_16_low, &diff[x]);
            svint16_t diff_vals_1 = svld1_s16(predicate_16_high, &diff[x+svcnth()]);

            for (int i = 0; i < 5; i++)
            {
                saoCuStatsE_stats_sum<SVE_VERSION, STATS_TYPE>(*tmp_stats_ptrs[i], tmp_count[i], i, edge_type,
                                              diff_vals_0, diff_vals_1, predicate);
            }

            x += svcntb();
            predicate = svwhilelt_b8(x, endX);
        } while (svptest_any(svptrue_b8(), predicate));

        upBuff1[endX - 1] = signOf(rec[endX - 1 + stride] - rec[endX]);

        diff += MAX_CU_SIZE;
        rec += stride;
    }

    saoCuStatsE_stats_final_accumulate<SVE_VERSION, STATS_TYPE>(stats, count, tmp_stats_ptrs, tmp_count);
}

} // end namespace X265_NS
#endif // end if defined(HAVE_SVE)
