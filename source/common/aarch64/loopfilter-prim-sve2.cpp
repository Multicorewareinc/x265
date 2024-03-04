#pragma GCC target ("arch=armv8.4-a+sve2")

#include "loopfilter-prim-sve.h"

// completely skip this file without SVE
#if defined(HAVE_SVE) && defined(HAVE_SVE2)
namespace X265_NS
{

void setupSaoPrimitives_sve2(EncoderPrimitives &p)
{
    p.saoCuStatsE0 = saoCuStatsE0_sve<SVE2, svint32_t>;
    p.saoCuStatsE1 = saoCuStatsE1_sve<SVE2, svint32_t>;
    p.saoCuStatsE2 = saoCuStatsE2_sve<SVE2, svint32_t>;
    p.saoCuStatsE3 = saoCuStatsE3_sve<SVE2, svint32_t>;
}

}  // end namespace X265_NS
#endif // end if defined(HAVE_SVE)
