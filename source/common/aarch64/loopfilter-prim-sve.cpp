#pragma GCC target ("arch=armv8.4-a+sve2")

#include "loopfilter-prim-sve.h"

// completely skip this file without SVE
#if defined(HAVE_SVE)
namespace X265_NS
{

void setupSaoPrimitives_sve(EncoderPrimitives &p)
{
    p.saoCuStatsE0 = saoCuStatsE0_sve<SVE, int32_t>;
    p.saoCuStatsE1 = saoCuStatsE1_sve<SVE, int32_t>;
    p.saoCuStatsE2 = saoCuStatsE2_sve<SVE, int32_t>;
    p.saoCuStatsE3 = saoCuStatsE3_sve<SVE, int32_t>;
}

}  // end namespace X265_NS
#endif // end if defined(HAVE_SVE)
