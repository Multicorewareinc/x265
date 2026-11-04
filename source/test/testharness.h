/*****************************************************************************
 * Copyright (C) 2013-2020 MulticoreWare, Inc
 *
 * Authors: Steve Borho <steve@borho.org>
 *          Min Chen <chenm003@163.com>
 *          Yimeng Su <yimeng.su@huawei.com>
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

#ifndef _TESTHARNESS_H_
#define _TESTHARNESS_H_ 1

#include "common.h"
#include "primitives.h"

#include <algorithm>
#include <chrono>
#include <utility>

#if X265_TESTBENCH_USE_COUNTER && X265_ARCH_X86 && defined(_MSC_VER)
#include <intrin.h>
#endif

#if _MSC_VER
#pragma warning(disable: 4324) // structure was padded due to __declspec(align())
#endif

#define PIXEL_MIN 0
#define SHORT_MAX  32767
#define SHORT_MIN -32767
#define UNSIGNED_SHORT_MAX 65535

using namespace X265_NS;

extern const char* lumaPartStr[NUM_PU_SIZES];
extern const char* const* chromaPartStr[X265_CSP_COUNT];

class TestHarness
{
public:

    TestHarness() {}

    virtual ~TestHarness() {}

    virtual bool testCorrectness(const EncoderPrimitives& ref, const EncoderPrimitives& opt) = 0;

    virtual void measureSpeed(const EncoderPrimitives& ref, const EncoderPrimitives& opt) = 0;

    virtual const char *getName() const = 0;

protected:

    /* Temporary variables for stack checks */
    int      m_ok;

    uint64_t m_rand;
};

struct BenchmarkClock
{
    enum CounterBoundary
    {
        BeginCounter,
        EndCounter
    };

#if X265_TESTBENCH_USE_COUNTER && \
    (X265_ARCH_X86 || X265_ARCH_ARM64 || X265_ARCH_RISCV64 || X265_ARCH_LOONGARCH64)
    typedef uint64_t time_point;

    static time_point now(CounterBoundary boundary)
    {
        (void)boundary;
#if X265_ARCH_X86
#if defined(_MSC_VER)
        _ReadWriteBarrier();
        const time_point counter = __rdtsc();
        _ReadWriteBarrier();
        return counter;
#else
        uint32_t low, high;
        asm volatile("rdtsc" : "=a" (low), "=d" (high) : : "memory");
        return (static_cast<time_point>(high) << 32) | low;
#endif
#elif X265_ARCH_ARM64
        time_point counter;
        if (boundary == BeginCounter)
        {
            asm volatile("isb\n\tmrs %0, cntvct_el0\n\tisb" : "=r" (counter) : : "memory");
        }
        else
        {
            asm volatile("isb\n\tmrs %0, cntvct_el0" : "=r" (counter) : : "memory");
        }
        return counter;
#elif X265_ARCH_RISCV64
        time_point counter;
        asm volatile("rdtime %0" : "=r" (counter) : : "memory");
        return counter;
#elif X265_ARCH_LOONGARCH64
        time_point counter, counterId;
        asm volatile("rdtime.d %0, %1" : "=r" (counter), "=r" (counterId) : : "memory");
        return counter;
#endif
    }

    static double elapsed(time_point start, time_point end)
    {
        return static_cast<double>(end - start);
    }
#else
    typedef std::chrono::steady_clock::time_point time_point;

    static time_point now(CounterBoundary)
    {
        return std::chrono::steady_clock::now();
    }

    static double elapsed(time_point start, time_point end)
    {
        return std::chrono::duration<double, std::nano>(end - start).count();
    }
#endif

    static const char* units()
    {
#if X265_TESTBENCH_USE_COUNTER && \
    (X265_ARCH_X86 || X265_ARCH_ARM64 || X265_ARCH_RISCV64 || X265_ARCH_LOONGARCH64)
#if X265_ARCH_X86
        return "raw RDTSC ticks/call";
#elif X265_ARCH_ARM64
        return "raw CNTVCT_EL0 ticks/call";
#elif X265_ARCH_RISCV64
        return "raw rdtime ticks/call";
#elif X265_ARCH_LOONGARCH64
        return "raw rdtime.d ticks/call";
#endif
#else
        return "ns/call";
#endif
    }
};

#if X265_ARCH_ARM64 && defined(__linux__) && defined(__GNUC__)
// Work around SVE primitives that clobber callee-saved SIMD registers.
#define BENCHMARK_CALL_ATTR __attribute__((noinline))
#define BENCHMARK_SAVE_REGISTERS() \
    asm volatile("" : : : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "memory")
#define BENCHMARK_CALL_BARRIER() asm volatile("" : : : "memory")
#else
#define BENCHMARK_CALL_ATTR inline
#define BENCHMARK_SAVE_REGISTERS()
#define BENCHMARK_CALL_BARRIER()
#endif

template<std::size_t... Indices, typename Func, typename... Args>
BENCHMARK_CALL_ATTR void callUnrolled(std::index_sequence<Indices...>, Func& func, Args&... args)
{
    BENCHMARK_SAVE_REGISTERS();
    ((static_cast<void>(Indices), static_cast<void>(func(args...))), ...);
    BENCHMARK_CALL_BARRIER();
}

// Keep the median of five filtered batch averages.
template<int Runs, int Unroll = 5, typename Func, typename... Args>
double measureTimePerCall(Func func, Args... args)
{
    static_assert(Unroll > 0, "Benchmark unroll count must be positive");
    static_assert(Runs > 0 && Runs % Unroll == 0, "Benchmark runs must be positive and divisible by the unroll count");
    typedef BenchmarkClock Clock;
    double samples[5];
    int sampleCount = 0;

    callUnrolled(std::make_index_sequence<1>{}, func, args...);
    x265_emms();
    for (int sample = 0; sample < 5; sample++)
    {
        double total = 0;
        double acceptedCalls = 0;
        for (int group = 0; group < Runs / Unroll; group++)
        {
            const Clock::time_point start = Clock::now(Clock::BeginCounter);
            double calls = 0;
            double elapsed = 0;
            for (int attempt = 0; attempt < 64 && elapsed <= 0; attempt++)
            {
                callUnrolled(std::make_index_sequence<Unroll>{}, func, args...);
                const Clock::time_point end = Clock::now(Clock::EndCounter);
                x265_emms();
                calls += Unroll;
                elapsed = Clock::elapsed(start, end);
            }
            if (elapsed <= 0)
            {
                continue;
            }

            // Reject groups over 4x the running per-call mean.
            if (acceptedCalls == 0 || elapsed * acceptedCalls <= total * calls * 4)
            {
                total += elapsed;
                acceptedCalls += calls;
            }
        }
        if (acceptedCalls > 0)
        {
            samples[sampleCount++] = total / acceptedCalls;
        }
    }

    if (sampleCount == 0)
    {
        return 0;
    }

    std::sort(samples, samples + sampleCount);
    const int middle = sampleCount / 2;
    return sampleCount % 2 ? samples[middle] : (samples[middle - 1] + samples[middle]) / 2;
}

/* Runs is the calls per batch. Increase it to reduce variance.
 * Unroll is the calls per iteration; Runs must be divisible by Unroll.
 * Times are nanoseconds per call, or raw counter ticks per call with
 * TESTBENCH_USE_COUNTER; the ratio is reference / optimized. */
template<int Runs = 2000, int Unroll = 5, typename Opt, typename Ref, typename... Args>
void REPORT_SPEEDUP(Opt runOpt, Ref runRef, Args... args)
{
    const double optperf = measureTimePerCall<Runs, Unroll>(runOpt, args...);
    const double refperf = measureTimePerCall<Runs, Unroll>(runRef, args...);
    if (optperf <= 0 || refperf <= 0)
    {
        printf(" | \tn/a  | increase REPORT_SPEEDUP<Runs> (currently %d)\n", Runs);
        return;
    }
    printf(" | \t%3.2fx | ", refperf / optperf);
    printf("\t %-8.2lf | \t %-8.2lf\n", optperf, refperf);
}

extern "C" {
#if X265_ARCH_X86
int PFX(stack_pagealign)(int (*func)(), int align);

/* detect when callee-saved regs aren't saved
 * needs an explicit asm check because it only sometimes crashes in normal use. */
intptr_t PFX(checkasm_call)(intptr_t (*func)(), int *ok, ...);
float PFX(checkasm_call_float)(float (*func)(), int *ok, ...);
#elif (X265_ARCH_LOONGARCH64)
int PFX(stack_pagealign)(int (*func)(), int align);

/* detect when callee-saved regs aren't saved
 * needs an explicit asm check because it only sometimes crashes in normal use. */
intptr_t PFX(checkasm_call)(intptr_t (*func)(), int *ok, ...);
float PFX(checkasm_call_float)(float (*func)(), int *ok, ...);
#elif (X265_ARCH_ARM == 0 && X265_ARCH_ARM64 == 0 && X265_ARCH_RISCV64 == 0)
#define PFX(stack_pagealign)(func, align) func()
#endif

#if X86_64

/* Evil hack: detect incorrect assumptions that 32-bit ints are zero-extended to 64-bit.
 * This is done by clobbering the stack with junk around the stack pointer and calling the
 * assembly function through x265_checkasm_call with added dummy arguments which forces all
 * real arguments to be passed on the stack and not in registers. For 32-bit argument the
 * upper half of the 64-bit register location on the stack will now contain junk. Note that
 * this is dependent on compiler behavior and that interrupts etc. at the wrong time may
 * overwrite the junk written to the stack so there's no guarantee that it will always
 * detect all functions that assumes zero-extension.
 */
void PFX(checkasm_stack_clobber)(uint64_t clobber, ...);
#define checked(func, ...) ( \
        m_ok = 1, m_rand = (rand() & 0xffff) * 0x0001000100010001ULL, \
        PFX(checkasm_stack_clobber)(m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand), /* max_args+6 */ \
        PFX(checkasm_call)((intptr_t(*)())reinterpret_cast<void*>(func), &m_ok, 0, 0, 0, 0, __VA_ARGS__))

#define checked_float(func, ...) ( \
        m_ok = 1, m_rand = (rand() & 0xffff) * 0x0001000100010001ULL, \
        PFX(checkasm_stack_clobber)(m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand), /* max_args+6 */ \
        PFX(checkasm_call_float)((float(*)())reinterpret_cast<void*>(func), &m_ok, 0, 0, 0, 0, __VA_ARGS__))
#define reportfail() if (!m_ok) { fflush(stdout); fprintf(stderr, "stack clobber check failed at %s:%d", __FILE__, __LINE__); abort(); }
#elif ARCH_X86
#define checked(func, ...) PFX(checkasm_call)((intptr_t(*)())func, &m_ok, __VA_ARGS__);
#define checked_float(func, ...) PFX(checkasm_call_float)((float(*)())func, &m_ok, __VA_ARGS__);
#elif X265_ARCH_LOONGARCH64
void PFX(checkasm_stack_clobber)(uint64_t clobber, ...);
#define checked(func, ...) ( \
        m_ok = 1, m_rand = (rand() & 0xffff) * 0x0001000100010001ULL, \
        PFX(checkasm_stack_clobber)(m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand), /* max_args+8 */ \
        PFX(checkasm_call)((intptr_t(*)())func, &m_ok, 0, 0, 0, 0, 0, 0, __VA_ARGS__))

#define checked_float(func, ...) ( \
        m_ok = 1, m_rand = (rand() & 0xffff) * 0x0001000100010001ULL, \
        PFX(checkasm_stack_clobber)(m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, \
                                    m_rand, m_rand, m_rand, m_rand, m_rand, m_rand, m_rand), /* max_args+8 */ \
        PFX(checkasm_call_float)((float(*)())func, &m_ok, 0, 0, 0, 0, 0, 0, __VA_ARGS__))
#define reportfail() if (!m_ok) { fflush(stdout); fprintf(stderr, "stack clobber check failed at %s:%d", __FILE__, __LINE__); abort(); }

#else // if X86_64
#define checked(func, ...) func(__VA_ARGS__)
#define checked_float(func, ...) func(__VA_ARGS__)
#define reportfail()
#endif // if X86_64
}

#endif // ifndef _TESTHARNESS_H_
