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

#include "cpu.h"
#include "common.h"

#if HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
#include <sys/auxv.h>
#endif
#if MACOS || SYS_FREEBSD
#include <sys/types.h>
#include <sys/sysctl.h>
#endif
#if SYS_OPENBSD
#include <sys/param.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#endif

#if X265_ARCH_ARM && !defined(HAVE_NEON) && !(HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO)
#include <signal.h>
#include <setjmp.h>
static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump = 0;

static void sigill_handler(int sig)
{
    if (!canjump)
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }

    canjump = 0;
    siglongjmp(jmpbuf, 1);
}

#endif // if X265_ARCH_ARM

namespace X265_NS {
const cpu_name_t cpu_names[] =
{
#if X265_ARCH_X86
#define MMX2 X265_CPU_MMX | X265_CPU_MMX2
    { "MMX2",        MMX2 },
    { "MMXEXT",      MMX2 },
    { "SSE",         MMX2 | X265_CPU_SSE },
#define SSE2 MMX2 | X265_CPU_SSE | X265_CPU_SSE2
    { "SSE2Slow",    SSE2 | X265_CPU_SSE2_IS_SLOW },
    { "SSE2",        SSE2 },
    { "SSE2Fast",    SSE2 | X265_CPU_SSE2_IS_FAST },
    { "LZCNT", X265_CPU_LZCNT },
    { "SSE3",        SSE2 | X265_CPU_SSE3 },
    { "SSSE3",       SSE2 | X265_CPU_SSE3 | X265_CPU_SSSE3 },
    { "SSE4.1",      SSE2 | X265_CPU_SSE3 | X265_CPU_SSSE3 | X265_CPU_SSE4 },
    { "SSE4",        SSE2 | X265_CPU_SSE3 | X265_CPU_SSSE3 | X265_CPU_SSE4 },
    { "SSE4.2",      SSE2 | X265_CPU_SSE3 | X265_CPU_SSSE3 | X265_CPU_SSE4 | X265_CPU_SSE42 },
#define AVX SSE2 | X265_CPU_SSE3 | X265_CPU_SSSE3 | X265_CPU_SSE4 | X265_CPU_SSE42 | X265_CPU_AVX
    { "AVX",         AVX },
    { "XOP",         AVX | X265_CPU_XOP },
    { "FMA4",        AVX | X265_CPU_FMA4 },
    { "FMA3",        AVX | X265_CPU_FMA3 },
    { "BMI1",        AVX | X265_CPU_LZCNT | X265_CPU_BMI1 },
    { "BMI2",        AVX | X265_CPU_LZCNT | X265_CPU_BMI1 | X265_CPU_BMI2 },
#define AVX2 AVX | X265_CPU_FMA3 | X265_CPU_LZCNT | X265_CPU_BMI1 | X265_CPU_BMI2 | X265_CPU_AVX2
    { "AVX2", AVX2},
    { "AVX512", AVX2 | X265_CPU_AVX512 },
#undef AVX2
#undef AVX
#undef SSE2
#undef MMX2
    { "Cache32",         X265_CPU_CACHELINE_32 },
    { "Cache64",         X265_CPU_CACHELINE_64 },
    { "SlowAtom",        X265_CPU_SLOW_ATOM },
    { "SlowPshufb",      X265_CPU_SLOW_PSHUFB },
    { "SlowPalignr",     X265_CPU_SLOW_PALIGNR },
    { "SlowShuffle",     X265_CPU_SLOW_SHUFFLE },
    { "UnalignedStack",  X265_CPU_STACK_MOD4 },

#elif X265_ARCH_ARM
    { "ARMv6",           X265_CPU_ARMV6 },
    { "NEON",            X265_CPU_NEON },
    { "FastNeonMRC",     X265_CPU_FAST_NEON_MRC },

#elif X265_ARCH_ARM64
    { "NEON",            X265_CPU_NEON },
#if defined(HAVE_SVE)
    { "SVE",            X265_CPU_SVE },
#endif
#if defined(HAVE_SVE2)
    { "SVE2",            X265_CPU_SVE2 },
#endif
#if defined(HAVE_NEON_DOTPROD)
    { "Neon_DotProd",    X265_CPU_NEON_DOTPROD },
#endif
#if defined(HAVE_NEON_I8MM)
    { "Neon_I8MM",       X265_CPU_NEON_I8MM },
#endif
#if defined(HAVE_SVE2_BITPERM)
    { "SVE2_BitPerm",    X265_CPU_SVE2_BITPERM },
#endif
#elif X265_ARCH_POWER8
    { "Altivec",         X265_CPU_ALTIVEC },

#elif X265_ARCH_RISCV64
    { "RVV",           X265_CPU_RVV },
    { "Zbb",           X265_CPU_ZBB },
#elif X265_ARCH_LOONGARCH64
    { "LSX",             X265_CPU_LSX},
    { "LASX",            X265_CPU_LASX},

#endif // if X265_ARCH_X86
    { "", 0 },
};

unsigned long x265_getauxval(unsigned long type)
{
#if HAVE_GETAUXVAL
    return getauxval(type);
#elif HAVE_ELF_AUX_INFO
    unsigned long aux = 0;
    elf_aux_info(type, &aux, sizeof(aux));
    return aux;
#else
    (void)type;
    return 0;
#endif
}

#if X265_ARCH_X86
#include "x86/cpu.h"

uint32_t cpu_detect(bool benableavx512)
{
    uint32_t flags = 0;

#ifdef ENABLE_ASSEMBLY
    flags = x86_cpu_detect(benableavx512);
#endif

    return flags;
}

#elif X265_ARCH_ARM

extern "C" {
void PFX(cpu_neon_test)(void);
int PFX(cpu_fast_neon_mrc_test)(void);
}

#define X265_ARM_HWCAP_NEON (1U << 12)

uint32_t cpu_detect(bool benableavx512)
{
    int flags = 0;

#if HAVE_ARMV6 && ENABLE_ASSEMBLY
    flags |= X265_CPU_ARMV6;

#if HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO
    unsigned long hwcap = x265_getauxval(AT_HWCAP);

    if (hwcap & X265_ARM_HWCAP_NEON) flags |= X265_CPU_NEON;
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
#endif

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

#elif X265_ARCH_ARM64
#include "aarch64/cpu.h"

uint32_t cpu_detect(bool benableavx512)
{
    (void)benableavx512;
    uint32_t flags = 0;

#ifdef ENABLE_ASSEMBLY
    flags = aarch64_cpu_detect();
#endif

    return flags;
}

#elif X265_ARCH_RISCV64
#include "riscv64/cpu.h"

uint32_t cpu_detect(bool benableavx512)
{
    (void)benableavx512;
    uint32_t flags = 0;

#ifdef ENABLE_ASSEMBLY
    flags = riscv64_cpu_detect();
#endif

    return flags;
}

#elif X265_ARCH_LOONGARCH64
#include "loongarch64/cpu.h"

uint32_t cpu_detect( bool benableavx512 )
{
    (void)benableavx512;
    uint32_t flags = 0;

#ifdef ENABLE_ASSEMBLY
    flags = loongarch64_cpu_detect();
#endif

    return flags;
}

#elif X265_ARCH_POWER8
#include "ppc/cpu.h"

uint32_t cpu_detect(bool benableavx512)
{
    (void)benableavx512;
    uint32_t flags = 0;

    flags = ppc_cpu_detect();

    return flags;
}

#else // if X265_ARCH_POWER8

uint32_t cpu_detect(bool benableavx512)
{
    return 0;
}

#endif // if X265_ARCH_X86
}

