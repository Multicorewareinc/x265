#ifndef _LOOPFILTER_NEON_H__
#define _LOOPFILTER_NEON_H__

#include "common.h"
#include "primitives.h"

#define PIXEL_MIN 0

namespace X265_NS
{

void setupLoopFilterPrimitives_neon(EncoderPrimitives &p);
#if defined(HAVE_SVE)
void setupSaoPrimitives_sve(EncoderPrimitives &p);
#endif
#if defined(HAVE_SVE2)
void setupSaoPrimitives_sve2(EncoderPrimitives &p);
#endif
};

#endif
