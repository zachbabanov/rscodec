#include "Common.h"

#include <thread>

namespace codec {


//------------------------------------------------------------------------------
// Runtime CPU Architecture Check
//
// Feature checks stolen shamelessly from
// https://github.com/jedisct1/libsodium/blob/master/src/libsodium/sodium/runtime.c

#if defined(HAVE_ANDROID_GETCPUFEATURES)
    #include <cpu-features.h>
#endif

#if defined(TRY_NEON)
# if defined(IOS) && defined(__ARM_NEON__)
    // Requires iPhone 5S or newer
# else
    // Remember to add LOCAL_STATIC_LIBRARIES := cpufeatures
    bool CpuHasNeon = false; // V6 / V7
    bool CpuHasNeon64 = false; // 64-bit
# endif
#endif


#if !defined(TARGET_MOBILE)

#ifdef _MSC_VER
    #include <intrin.h> // __cpuid
    #pragma warning(disable: 4752) // found Intel(R) Advanced Vector Extensions; consider using /arch:AVX
#endif

#ifdef TRY_AVX2
    bool CpuHasAVX2 = false;
#endif

bool CpuHasSSSE3 = false;

#define CPUID_EBX_AVX2    0x00000020
#define CPUID_ECX_SSSE3   0x00000200

static void _cpuid(unsigned int cpu_info[4U], const unsigned int cpu_info_type)
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64) || defined(_M_IX86))
    __cpuid((int *) cpu_info, cpu_info_type);
#else //if defined(HAVE_CPUID)
    cpu_info[0] = cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
# ifdef __i386__
    __asm__ __volatile__ ("pushfl; pushfl; "
                          "popl %0; "
                          "movl %0, %1; xorl %2, %0; "
                          "pushl %0; "
                          "popfl; pushfl; popl %0; popfl" :
                          "=&r" (cpu_info[0]), "=&r" (cpu_info[1]) :
                          "i" (0x200000));
    if (((cpu_info[0] ^ cpu_info[1]) & 0x200000) == 0) {
        return; /* LCOV_EXCL_LINE */
    }
# endif
# ifdef __i386__
    __asm__ __volatile__ ("xchgl %%ebx, %k1; cpuid; xchgl %%ebx, %k1" :
                          "=a" (cpu_info[0]), "=&r" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# elif defined(__x86_64__)
    __asm__ __volatile__ ("xchgq %%rbx, %q1; cpuid; xchgq %%rbx, %q1" :
                          "=a" (cpu_info[0]), "=&r" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# else
    __asm__ __volatile__ ("cpuid" :
                          "=a" (cpu_info[0]), "=b" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# endif
#endif
}

#elif defined(USE_SSE2NEON)
bool CpuHasSSSE3 = true;
#endif // defined(TARGET_MOBILE)


void InitializeCPUArch()
{
#if defined(TRY_NEON) && defined(HAVE_ANDROID_GETCPUFEATURES)
    AndroidCpuFamily family = android_getCpuFamily();
    if (family == ANDROID_CPU_FAMILY_ARM)
    {
        if (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON)
            CpuHasNeon = true;
    }
    else if (family == ANDROID_CPU_FAMILY_ARM64)
    {
        CpuHasNeon = true;
        if (android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD)
            CpuHasNeon64 = true;
    }
#endif

#if !defined(TARGET_MOBILE)
    unsigned int cpu_info[4];

    _cpuid(cpu_info, 1);
    CpuHasSSSE3 = ((cpu_info[2] & CPUID_ECX_SSSE3) != 0);

#if defined(TRY_AVX2)
    _cpuid(cpu_info, 7);
    CpuHasAVX2 = ((cpu_info[1] & CPUID_EBX_AVX2) != 0);
#endif // TRY_AVX2

#ifndef USE_SSSE3_OPT
    CpuHasSSSE3 = false;
#endif // USE_SSSE3_OPT
#ifndef USE_AVX2_OPT
    CpuHasAVX2 = false;
#endif // USE_AVX2_OPT

#endif // TARGET_MOBILE
}


//------------------------------------------------------------------------------
// XOR Memory

void xor_mem(
    void * RESTRICT vx, const void * RESTRICT vy,
    uint64_t bytes)
{
#if defined(TRY_AVX2)
    if (CpuHasAVX2)
    {
        M256 * RESTRICT x32 = reinterpret_cast<M256 *>(vx);
        const M256 * RESTRICT y32 = reinterpret_cast<const M256 *>(vy);
        while (bytes >= 128)
        {
            const M256 x0 = _mm256_xor_si256(_mm256_loadu_si256(x32),     _mm256_loadu_si256(y32));
            const M256 x1 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 1), _mm256_loadu_si256(y32 + 1));
            const M256 x2 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 2), _mm256_loadu_si256(y32 + 2));
            const M256 x3 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 3), _mm256_loadu_si256(y32 + 3));
            _mm256_storeu_si256(x32, x0);
            _mm256_storeu_si256(x32 + 1, x1);
            _mm256_storeu_si256(x32 + 2, x2);
            _mm256_storeu_si256(x32 + 3, x3);
            x32 += 4, y32 += 4;
            bytes -= 128;
        };
        if (bytes > 0)
        {
            const M256 x0 = _mm256_xor_si256(_mm256_loadu_si256(x32),     _mm256_loadu_si256(y32));
            const M256 x1 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 1), _mm256_loadu_si256(y32 + 1));
            _mm256_storeu_si256(x32, x0);
            _mm256_storeu_si256(x32 + 1, x1);
        }
        return;
    }
#endif // TRY_AVX2

    M128 * RESTRICT x16 = reinterpret_cast<M128 *>(vx);
    const M128 * RESTRICT y16 = reinterpret_cast<const M128 *>(vy);
    do
    {
        const M128 x0 = _mm_xor_si128(_mm_loadu_si128(x16),     _mm_loadu_si128(y16));
        const M128 x1 = _mm_xor_si128(_mm_loadu_si128(x16 + 1), _mm_loadu_si128(y16 + 1));
        const M128 x2 = _mm_xor_si128(_mm_loadu_si128(x16 + 2), _mm_loadu_si128(y16 + 2));
        const M128 x3 = _mm_xor_si128(_mm_loadu_si128(x16 + 3), _mm_loadu_si128(y16 + 3));
        _mm_storeu_si128(x16, x0);
        _mm_storeu_si128(x16 + 1, x1);
        _mm_storeu_si128(x16 + 2, x2);
        _mm_storeu_si128(x16 + 3, x3);
        x16 += 4, y16 += 4;
        bytes -= 64;
    } while (bytes > 0);
}

#ifdef M1_OPT

void xor_mem_2to1(
    void * RESTRICT x,
    const void * RESTRICT y,
    const void * RESTRICT z,
    uint64_t bytes)
{
#if defined(TRY_AVX2)
    if (CpuHasAVX2)
    {
        M256 * RESTRICT x32 = reinterpret_cast<M256 *>(x);
        const M256 * RESTRICT y32 = reinterpret_cast<const M256 *>(y);
        const M256 * RESTRICT z32 = reinterpret_cast<const M256 *>(z);
        while (bytes >= 128)
        {
            M256 x0 = _mm256_xor_si256(_mm256_loadu_si256(x32), _mm256_loadu_si256(y32));
            x0 = _mm256_xor_si256(x0, _mm256_loadu_si256(z32));
            M256 x1 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 1), _mm256_loadu_si256(y32 + 1));
            x1 = _mm256_xor_si256(x1, _mm256_loadu_si256(z32 + 1));
            M256 x2 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 2), _mm256_loadu_si256(y32 + 2));
            x2 = _mm256_xor_si256(x2, _mm256_loadu_si256(z32 + 2));
            M256 x3 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 3), _mm256_loadu_si256(y32 + 3));
            x3 = _mm256_xor_si256(x3, _mm256_loadu_si256(z32 + 3));
            _mm256_storeu_si256(x32, x0);
            _mm256_storeu_si256(x32 + 1, x1);
            _mm256_storeu_si256(x32 + 2, x2);
            _mm256_storeu_si256(x32 + 3, x3);
            x32 += 4, y32 += 4, z32 += 4;
            bytes -= 128;
        };

        if (bytes > 0)
        {
            M256 x0 = _mm256_xor_si256(_mm256_loadu_si256(x32),     _mm256_loadu_si256(y32));
            x0 = _mm256_xor_si256(x0, _mm256_loadu_si256(z32));
            M256 x1 = _mm256_xor_si256(_mm256_loadu_si256(x32 + 1), _mm256_loadu_si256(y32 + 1));
            x1 = _mm256_xor_si256(x1, _mm256_loadu_si256(z32 + 1));
            _mm256_storeu_si256(x32, x0);
            _mm256_storeu_si256(x32 + 1, x1);
        }

        return;
    }
#endif // TRY_AVX2

    M128 * RESTRICT x16 = reinterpret_cast<M128 *>(x);
    const M128 * RESTRICT y16 = reinterpret_cast<const M128 *>(y);
    const M128 * RESTRICT z16 = reinterpret_cast<const M128 *>(z);
    do
    {
        M128 x0 = _mm_xor_si128(_mm_loadu_si128(x16), _mm_loadu_si128(y16));
        x0 = _mm_xor_si128(x0, _mm_loadu_si128(z16));
        M128 x1 = _mm_xor_si128(_mm_loadu_si128(x16 + 1), _mm_loadu_si128(y16 + 1));
        x1 = _mm_xor_si128(x1, _mm_loadu_si128(z16 + 1));
        M128 x2 = _mm_xor_si128(_mm_loadu_si128(x16 + 2), _mm_loadu_si128(y16 + 2));
        x2 = _mm_xor_si128(x2, _mm_loadu_si128(z16 + 2));
        M128 x3 = _mm_xor_si128(_mm_loadu_si128(x16 + 3), _mm_loadu_si128(y16 + 3));
        x3 = _mm_xor_si128(x3, _mm_loadu_si128(z16 + 3));
        _mm_storeu_si128(x16, x0);
        _mm_storeu_si128(x16 + 1, x1);
        _mm_storeu_si128(x16 + 2, x2);
        _mm_storeu_si128(x16 + 3, x3);
        x16 += 4, y16 += 4, z16 += 4;
        bytes -= 64;
    } while (bytes > 0);
}

#endif // M1_OPT

#ifdef USE_VECTOR4_OPT

void xor_mem4(
    void * RESTRICT vx_0, const void * RESTRICT vy_0,
    void * RESTRICT vx_1, const void * RESTRICT vy_1,
    void * RESTRICT vx_2, const void * RESTRICT vy_2,
    void * RESTRICT vx_3, const void * RESTRICT vy_3,
    uint64_t bytes)
{
#if defined(TRY_AVX2)
    if (CpuHasAVX2)
    {
        M256 * RESTRICT       x32_0 = reinterpret_cast<M256 *>      (vx_0);
        const M256 * RESTRICT y32_0 = reinterpret_cast<const M256 *>(vy_0);
        M256 * RESTRICT       x32_1 = reinterpret_cast<M256 *>      (vx_1);
        const M256 * RESTRICT y32_1 = reinterpret_cast<const M256 *>(vy_1);
        M256 * RESTRICT       x32_2 = reinterpret_cast<M256 *>      (vx_2);
        const M256 * RESTRICT y32_2 = reinterpret_cast<const M256 *>(vy_2);
        M256 * RESTRICT       x32_3 = reinterpret_cast<M256 *>      (vx_3);
        const M256 * RESTRICT y32_3 = reinterpret_cast<const M256 *>(vy_3);
        while (bytes >= 128)
        {
            const M256 x0_0 = _mm256_xor_si256(_mm256_loadu_si256(x32_0),     _mm256_loadu_si256(y32_0));
            const M256 x1_0 = _mm256_xor_si256(_mm256_loadu_si256(x32_0 + 1), _mm256_loadu_si256(y32_0 + 1));
            const M256 x2_0 = _mm256_xor_si256(_mm256_loadu_si256(x32_0 + 2), _mm256_loadu_si256(y32_0 + 2));
            const M256 x3_0 = _mm256_xor_si256(_mm256_loadu_si256(x32_0 + 3), _mm256_loadu_si256(y32_0 + 3));
            _mm256_storeu_si256(x32_0, x0_0);
            _mm256_storeu_si256(x32_0 + 1, x1_0);
            _mm256_storeu_si256(x32_0 + 2, x2_0);
            _mm256_storeu_si256(x32_0 + 3, x3_0);
            x32_0 += 4, y32_0 += 4;
            const M256 x0_1 = _mm256_xor_si256(_mm256_loadu_si256(x32_1),     _mm256_loadu_si256(y32_1));
            const M256 x1_1 = _mm256_xor_si256(_mm256_loadu_si256(x32_1 + 1), _mm256_loadu_si256(y32_1 + 1));
            const M256 x2_1 = _mm256_xor_si256(_mm256_loadu_si256(x32_1 + 2), _mm256_loadu_si256(y32_1 + 2));
            const M256 x3_1 = _mm256_xor_si256(_mm256_loadu_si256(x32_1 + 3), _mm256_loadu_si256(y32_1 + 3));
            _mm256_storeu_si256(x32_1, x0_1);
            _mm256_storeu_si256(x32_1 + 1, x1_1);
            _mm256_storeu_si256(x32_1 + 2, x2_1);
            _mm256_storeu_si256(x32_1 + 3, x3_1);
            x32_1 += 4, y32_1 += 4;
            const M256 x0_2 = _mm256_xor_si256(_mm256_loadu_si256(x32_2),     _mm256_loadu_si256(y32_2));
            const M256 x1_2 = _mm256_xor_si256(_mm256_loadu_si256(x32_2 + 1), _mm256_loadu_si256(y32_2 + 1));
            const M256 x2_2 = _mm256_xor_si256(_mm256_loadu_si256(x32_2 + 2), _mm256_loadu_si256(y32_2 + 2));
            const M256 x3_2 = _mm256_xor_si256(_mm256_loadu_si256(x32_2 + 3), _mm256_loadu_si256(y32_2 + 3));
            _mm256_storeu_si256(x32_2, x0_2);
            _mm256_storeu_si256(x32_2 + 1, x1_2);
            _mm256_storeu_si256(x32_2 + 2, x2_2);
            _mm256_storeu_si256(x32_2 + 3, x3_2);
            x32_2 += 4, y32_2 += 4;
            const M256 x0_3 = _mm256_xor_si256(_mm256_loadu_si256(x32_3),     _mm256_loadu_si256(y32_3));
            const M256 x1_3 = _mm256_xor_si256(_mm256_loadu_si256(x32_3 + 1), _mm256_loadu_si256(y32_3 + 1));
            const M256 x2_3 = _mm256_xor_si256(_mm256_loadu_si256(x32_3 + 2), _mm256_loadu_si256(y32_3 + 2));
            const M256 x3_3 = _mm256_xor_si256(_mm256_loadu_si256(x32_3 + 3), _mm256_loadu_si256(y32_3 + 3));
            _mm256_storeu_si256(x32_3,     x0_3);
            _mm256_storeu_si256(x32_3 + 1, x1_3);
            _mm256_storeu_si256(x32_3 + 2, x2_3);
            _mm256_storeu_si256(x32_3 + 3, x3_3);
            x32_3 += 4, y32_3 += 4;
            bytes -= 128;
        }
        if (bytes > 0)
        {
            const M256 x0_0 = _mm256_xor_si256(_mm256_loadu_si256(x32_0),     _mm256_loadu_si256(y32_0));
            const M256 x1_0 = _mm256_xor_si256(_mm256_loadu_si256(x32_0 + 1), _mm256_loadu_si256(y32_0 + 1));
            const M256 x0_1 = _mm256_xor_si256(_mm256_loadu_si256(x32_1),     _mm256_loadu_si256(y32_1));
            const M256 x1_1 = _mm256_xor_si256(_mm256_loadu_si256(x32_1 + 1), _mm256_loadu_si256(y32_1 + 1));
            _mm256_storeu_si256(x32_0, x0_0);
            _mm256_storeu_si256(x32_0 + 1, x1_0);
            _mm256_storeu_si256(x32_1, x0_1);
            _mm256_storeu_si256(x32_1 + 1, x1_1);
            const M256 x0_2 = _mm256_xor_si256(_mm256_loadu_si256(x32_2),     _mm256_loadu_si256(y32_2));
            const M256 x1_2 = _mm256_xor_si256(_mm256_loadu_si256(x32_2 + 1), _mm256_loadu_si256(y32_2 + 1));
            const M256 x0_3 = _mm256_xor_si256(_mm256_loadu_si256(x32_3),     _mm256_loadu_si256(y32_3));
            const M256 x1_3 = _mm256_xor_si256(_mm256_loadu_si256(x32_3 + 1), _mm256_loadu_si256(y32_3 + 1));
            _mm256_storeu_si256(x32_2,     x0_2);
            _mm256_storeu_si256(x32_2 + 1, x1_2);
            _mm256_storeu_si256(x32_3,     x0_3);
            _mm256_storeu_si256(x32_3 + 1, x1_3);
        }
        return;
    }
#endif // TRY_AVX2
    M128 * RESTRICT       x16_0 = reinterpret_cast<M128 *>      (vx_0);
    const M128 * RESTRICT y16_0 = reinterpret_cast<const M128 *>(vy_0);
    M128 * RESTRICT       x16_1 = reinterpret_cast<M128 *>      (vx_1);
    const M128 * RESTRICT y16_1 = reinterpret_cast<const M128 *>(vy_1);
    M128 * RESTRICT       x16_2 = reinterpret_cast<M128 *>      (vx_2);
    const M128 * RESTRICT y16_2 = reinterpret_cast<const M128 *>(vy_2);
    M128 * RESTRICT       x16_3 = reinterpret_cast<M128 *>      (vx_3);
    const M128 * RESTRICT y16_3 = reinterpret_cast<const M128 *>(vy_3);
    do
    {
        const M128 x0_0 = _mm_xor_si128(_mm_loadu_si128(x16_0),     _mm_loadu_si128(y16_0));
        const M128 x1_0 = _mm_xor_si128(_mm_loadu_si128(x16_0 + 1), _mm_loadu_si128(y16_0 + 1));
        const M128 x2_0 = _mm_xor_si128(_mm_loadu_si128(x16_0 + 2), _mm_loadu_si128(y16_0 + 2));
        const M128 x3_0 = _mm_xor_si128(_mm_loadu_si128(x16_0 + 3), _mm_loadu_si128(y16_0 + 3));
        _mm_storeu_si128(x16_0, x0_0);
        _mm_storeu_si128(x16_0 + 1, x1_0);
        _mm_storeu_si128(x16_0 + 2, x2_0);
        _mm_storeu_si128(x16_0 + 3, x3_0);
        x16_0 += 4, y16_0 += 4;
        const M128 x0_1 = _mm_xor_si128(_mm_loadu_si128(x16_1),     _mm_loadu_si128(y16_1));
        const M128 x1_1 = _mm_xor_si128(_mm_loadu_si128(x16_1 + 1), _mm_loadu_si128(y16_1 + 1));
        const M128 x2_1 = _mm_xor_si128(_mm_loadu_si128(x16_1 + 2), _mm_loadu_si128(y16_1 + 2));
        const M128 x3_1 = _mm_xor_si128(_mm_loadu_si128(x16_1 + 3), _mm_loadu_si128(y16_1 + 3));
        _mm_storeu_si128(x16_1, x0_1);
        _mm_storeu_si128(x16_1 + 1, x1_1);
        _mm_storeu_si128(x16_1 + 2, x2_1);
        _mm_storeu_si128(x16_1 + 3, x3_1);
        x16_1 += 4, y16_1 += 4;
        const M128 x0_2 = _mm_xor_si128(_mm_loadu_si128(x16_2),     _mm_loadu_si128(y16_2));
        const M128 x1_2 = _mm_xor_si128(_mm_loadu_si128(x16_2 + 1), _mm_loadu_si128(y16_2 + 1));
        const M128 x2_2 = _mm_xor_si128(_mm_loadu_si128(x16_2 + 2), _mm_loadu_si128(y16_2 + 2));
        const M128 x3_2 = _mm_xor_si128(_mm_loadu_si128(x16_2 + 3), _mm_loadu_si128(y16_2 + 3));
        _mm_storeu_si128(x16_2, x0_2);
        _mm_storeu_si128(x16_2 + 1, x1_2);
        _mm_storeu_si128(x16_2 + 2, x2_2);
        _mm_storeu_si128(x16_2 + 3, x3_2);
        x16_2 += 4, y16_2 += 4;
        const M128 x0_3 = _mm_xor_si128(_mm_loadu_si128(x16_3),     _mm_loadu_si128(y16_3));
        const M128 x1_3 = _mm_xor_si128(_mm_loadu_si128(x16_3 + 1), _mm_loadu_si128(y16_3 + 1));
        const M128 x2_3 = _mm_xor_si128(_mm_loadu_si128(x16_3 + 2), _mm_loadu_si128(y16_3 + 2));
        const M128 x3_3 = _mm_xor_si128(_mm_loadu_si128(x16_3 + 3), _mm_loadu_si128(y16_3 + 3));
        _mm_storeu_si128(x16_3,     x0_3);
        _mm_storeu_si128(x16_3 + 1, x1_3);
        _mm_storeu_si128(x16_3 + 2, x2_3);
        _mm_storeu_si128(x16_3 + 3, x3_3);
        x16_3 += 4, y16_3 += 4;
        bytes -= 64;
    } while (bytes > 0);
}

#endif // USE_VECTOR4_OPT

void VectorXOR_Threads(
    const uint64_t bytes,
    unsigned count,
    void** x,
    void** y)
{
#ifdef USE_VECTOR4_OPT
    if (count >= 4)
    {
        int i_end = count - 4;
#pragma omp parallel for
        for (int i = 0; i <= i_end; i += 4)
        {
            xor_mem4(
                x[i + 0], y[i + 0],
                x[i + 1], y[i + 1],
                x[i + 2], y[i + 2],
                x[i + 3], y[i + 3],
                bytes);
        }
        count %= 4;
        i_end -= count;
        x += i_end;
        y += i_end;
    }
#endif // USE_VECTOR4_OPT

    for (unsigned i = 0; i < count; ++i)
        xor_mem(x[i], y[i], bytes);
}
void VectorXOR(
    const uint64_t bytes,
    unsigned count,
    void** x,
    void** y)
{
#ifdef USE_VECTOR4_OPT
    if (count >= 4)
    {
        int i_end = count - 4;
        for (int i = 0; i <= i_end; i += 4)
        {
            xor_mem4(
                x[i + 0], y[i + 0],
                x[i + 1], y[i + 1],
                x[i + 2], y[i + 2],
                x[i + 3], y[i + 3],
                bytes);
        }
        count %= 4;
        i_end -= count;
        x += i_end;
        y += i_end;
    }
#endif // USE_VECTOR4_OPT

    for (unsigned i = 0; i < count; ++i)
        xor_mem(x[i], y[i], bytes);
}


} // namespace codec
