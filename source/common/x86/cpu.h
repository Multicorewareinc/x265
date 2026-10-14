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

#ifndef X265_COMMON_X86_CPU_H
#define X265_COMMON_X86_CPU_H

#include "x265.h"

static bool enable512 = false;

extern "C" {
/* cpu-a.asm */
int PFX(cpu_cpuid_test)(void);
void PFX(cpu_cpuid)(uint32_t op, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);
uint64_t PFX(cpu_xgetbv)(int xcr);
}

#if defined(_MSC_VER)
#pragma warning(disable: 4309) // truncation of constant value
#endif

bool detect512()
{
    return enable512;
}

static inline uint32_t x86_cpu_detect(bool benableavx512)
{
    uint32_t flags = 0;
    uint32_t eax, ebx, ecx, edx;
    uint32_t vendor[4] = { 0 };
    uint32_t max_extended_cap, max_basic_cap;
    uint64_t xcr0 = 0;

#if !X86_64
    if (!PFX(cpu_cpuid_test)())
        return 0;
#endif

    PFX(cpu_cpuid)(0, &max_basic_cap, vendor + 0, vendor + 2, vendor + 1);
    if (max_basic_cap == 0)
        return 0;

    PFX(cpu_cpuid)(1, &eax, &ebx, &ecx, &edx);
    if (edx & 0x00800000)
        flags |= X265_CPU_MMX;
    else
        return flags;
    if (edx & 0x02000000)
        flags |= X265_CPU_MMX2 | X265_CPU_SSE;
    if (edx & 0x04000000)
        flags |= X265_CPU_SSE2;
    if (ecx & 0x00000001)
        flags |= X265_CPU_SSE3;
    if (ecx & 0x00000200)
        flags |= X265_CPU_SSSE3 | X265_CPU_SSE2_IS_FAST;
    if (ecx & 0x00080000)
        flags |= X265_CPU_SSE4;
    if (ecx & 0x00100000)
        flags |= X265_CPU_SSE42;

    if (ecx & 0x08000000) /* XGETBV supported and XSAVE enabled by OS */
    {
        /* Check for OS support */
        xcr0 = PFX(cpu_xgetbv)(0);
        if ((xcr0 & 0x6) == 0x6) /* XMM/YMM state */
        {
            if (ecx & 0x10000000)
                flags |= X265_CPU_AVX;
            if (ecx & 0x00001000)
                flags |= X265_CPU_FMA3;
        }
    }

    if (max_basic_cap >= 7)
    {
        PFX(cpu_cpuid)(7, &eax, &ebx, &ecx, &edx);
        /* AVX2 requires OS support, but BMI1/2 don't. */
        if (ebx & 0x00000008)
            flags |= X265_CPU_BMI1;
        if (ebx & 0x00000100)
            flags |= X265_CPU_BMI2;

        if ((xcr0 & 0x6) == 0x6) /* XMM/YMM state */
        {
            if (ebx & 0x00000020)
                flags |= X265_CPU_AVX2;
            if (benableavx512)
            {
                if ((xcr0 & 0xE0) == 0xE0) /* OPMASK/ZMM state */
                {
                    if ((ebx & 0xD0030000) == 0xD0030000)
                    {
                        flags |= X265_CPU_AVX512;
                        enable512 = true;
                    }
                }
            }
        }
    }

    PFX(cpu_cpuid)(0x80000000, &eax, &ebx, &ecx, &edx);
    max_extended_cap = eax;

    if (max_extended_cap >= 0x80000001)
    {
        PFX(cpu_cpuid)(0x80000001, &eax, &ebx, &ecx, &edx);

        if (ecx & 0x00000020)
            flags |= X265_CPU_LZCNT; /* Supported by Intel chips starting with Haswell */
        if (ecx & 0x00000040) /* SSE4a, AMD only */
        {
            int family = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff);
            flags |= X265_CPU_SSE2_IS_FAST;      /* Phenom and later CPUs have fast SSE units */
            if (family == 0x14)
            {
                flags &= ~X265_CPU_SSE2_IS_FAST; /* SSSE3 doesn't imply fast SSE anymore... */
                flags |= X265_CPU_SSE2_IS_SLOW;  /* Bobcat has 64-bit SIMD units */
                flags |= X265_CPU_SLOW_PALIGNR;  /* palignr is insanely slow on Bobcat */
            }
            if (family == 0x16)
            {
                flags |= X265_CPU_SLOW_PSHUFB; /* Jaguar's pshufb isn't that slow, but it's slow enough
                                                * compared to alternate instruction sequences that this
                                                * is equal or faster on almost all such functions. */
            }
        }

        if (flags & X265_CPU_AVX)
        {
            if (ecx & 0x00000800) /* XOP */
                flags |= X265_CPU_XOP;
            if (ecx & 0x00010000) /* FMA4 */
                flags |= X265_CPU_FMA4;
        }

        if (!strcmp((char*)vendor, "AuthenticAMD"))
        {
            if (edx & 0x00400000)
                flags |= X265_CPU_MMX2;
            if ((flags & X265_CPU_SSE2) && !(flags & X265_CPU_SSE2_IS_FAST))
                flags |= X265_CPU_SSE2_IS_SLOW; /* AMD CPUs come in two types: terrible at SSE and great at it */
        }
    }

    if (!strcmp((char*)vendor, "GenuineIntel"))
    {
        PFX(cpu_cpuid)(1, &eax, &ebx, &ecx, &edx);
        int family = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff);
        int model  = ((eax >> 4) & 0xf) + ((eax >> 12) & 0xf0);
        if (family == 6)
        {
            /* Detect Atom CPU */
            if (model == 28)
            {
                flags |= X265_CPU_SLOW_ATOM;
                flags |= X265_CPU_SLOW_PSHUFB;
            }

            /* Conroe has a slow shuffle unit. Check the model number to make sure not
             * to include crippled low-end Penryns and Nehalems that don't have SSE4. */
            else if ((flags & X265_CPU_SSSE3) && !(flags & X265_CPU_SSE4) && model < 23)
                flags |= X265_CPU_SLOW_SHUFFLE;
        }
    }

    if ((!strcmp((char*)vendor, "GenuineIntel") || !strcmp((char*)vendor, "CyrixInstead")) && !(flags & X265_CPU_SSE42))
    {
        /* cacheline size is specified in 3 places, any of which may be missing */
        PFX(cpu_cpuid)(1, &eax, &ebx, &ecx, &edx);
        int cache = (ebx & 0xff00) >> 5; // cflush size
        if (!cache && max_extended_cap >= 0x80000006)
        {
            PFX(cpu_cpuid)(0x80000006, &eax, &ebx, &ecx, &edx);
            cache = ecx & 0xff; // cacheline size
        }
        if (!cache && max_basic_cap >= 2)
        {
            // Cache and TLB Information
            static const char cache32_ids[] = { '\x0a','\x0c','\x41','\x42','\x43','\x44','\x45','\x82','\x83','\x84','\x85','\0' };
            static const char cache64_ids[] = { '\x22','\x23','\x25','\x29','\x2c','\x46','\x47','\x49','\x60','\x66','\x67',
                                                '\x68','\x78','\x79','\x7a','\x7b','\x7c','\x7c','\x7f','\x86','\x87','\0' };
            uint32_t buf[4];
            int max, i = 0;
            do
            {
                PFX(cpu_cpuid)(2, buf + 0, buf + 1, buf + 2, buf + 3);
                max = buf[0] & 0xff;
                buf[0] &= ~0xff;
                for (int j = 0; j < 4; j++)
                {
                    if (!(buf[j] >> 31))
                        while (buf[j])
                        {
                            if (strchr(cache32_ids, buf[j] & 0xff))
                                cache = 32;
                            if (strchr(cache64_ids, buf[j] & 0xff))
                                cache = 64;
                            buf[j] >>= 8;
                        }
                }
            }
            while (++i < max);
        }

        if (cache == 32)
            flags |= X265_CPU_CACHELINE_32;
        else if (cache == 64)
            flags |= X265_CPU_CACHELINE_64;
    }

#if BROKEN_STACK_ALIGNMENT
    flags |= X265_CPU_STACK_MOD4;
#endif

    return flags;
}

#endif // ifndef X265_COMMON_X86_CPU_H
