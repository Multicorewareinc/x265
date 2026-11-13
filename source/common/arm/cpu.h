/*****************************************************************************
 * Copyright (C) 2013-2020 MulticoreWare, Inc
 *
 * Authors: Loren Merritt <lorenm@u.washington.edu>
 *          Laurent Aimar <fenrir@via.ecp.fr>
 *          Fiona Glaser <fiona@x264.com>
 *          Steve Borho <steve@borho.org>
 *          Hongbin Liu <liuhongbin1@huawei.com>
 *          Yimeng Su <yimeng.su@huawei.com>
 *          Josh Dekker <josh@itanimul.li>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#ifndef X265_COMMON_ARM_CPU_H
#define X265_COMMON_ARM_CPU_H

#include "x265.h"

#if HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
#include <sys/auxv.h>
#endif

extern "C" {
/* cpu-a.S */
void PFX(cpu_neon_test)(void);
int PFX(cpu_fast_neon_mrc_test)(void);
}

#if !defined(HAVE_NEON) && !(HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO)
#include <signal.h>
#include <setjmp.h>

static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump = 0;

static inline void sigill_handler(int sig)
{
    if (!canjump)
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }

    canjump = 0;
    siglongjmp(jmpbuf, 1);
}
#endif // if !defined(HAVE_NEON) && !(HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO)

#define X265_ARM_HWCAP_NEON (1U << 12)

static inline uint32_t arm_cpu_detect()
{
    uint32_t flags = 0;

#if HAVE_ARMV6
    flags |= X265_CPU_ARMV6;

#if HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    unsigned long hwcap = x265_getauxval(AT_HWCAP);

    if (hwcap & X265_ARM_HWCAP_NEON)
        flags |= X265_CPU_NEON;
#else
    // don't do this hack if compiled with -mfpu=neon
#if !HAVE_NEON
    static void (* oldsig)(int);
    oldsig = signal(SIGILL, sigill_handler);
    if (sigsetjmp(jmpbuf, 1))
    {
        signal(SIGILL, oldsig);
        return flags;
    }

    canjump = 1;
    PFX(cpu_neon_test)();
    canjump = 0;
    signal(SIGILL, oldsig);
#endif // if !HAVE_NEON

    flags |= X265_CPU_NEON;
#endif // if HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO

    // fast neon -> arm (Cortex-A9) detection relies on user access to the
    // cycle counter; this assumes ARMv7 performance counters.
    // NEON requires at least ARMv7, ARMv8 may require changes here, but
    // hopefully this hacky detection method will have been replaced by then.
    // Note that there is potential for a race condition if another program or
    // x264 instance disables or reinits the counters while x264 is using them,
    // which may result in incorrect detection and the counters stuck enabled.
    // right now Apple does not seem to support performance counters for this test
#ifndef __MACH__
    flags |= PFX(cpu_fast_neon_mrc_test)() ? X265_CPU_FAST_NEON_MRC : 0;
#endif
    // TODO: write dual issue test? currently it's A8 (dual issue) vs. A9 (fast mrc)
#endif // if HAVE_ARMV6

    return flags;
}

#endif // ifndef X265_COMMON_ARM_CPU_H
