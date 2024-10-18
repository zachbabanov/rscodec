#include "codec.h"
#include "Common.h"

#ifdef HAS_FF8
    #include "FF8.h"
#endif // HAS_FF8
#ifdef HAS_FF16
    #include "FF16.h"
#endif // HAS_FF16

#include <string.h>

extern "C" {


//------------------------------------------------------------------------------
// Initialization API

static bool m_Initialized = false;

EXPORT int init_(int version)
{
    if (version != VERSION)
        return InvalidInput;

    codec::InitializeCPUArch();

#ifdef HAS_FF8
    if (!codec::ff8::Initialize())
        return Platform;
#endif // HAS_FF8

#ifdef HAS_FF16
    if (!codec::ff16::Initialize())
        return Platform;
#endif // HAS_FF16


    m_Initialized = true;
    return Success;
}

//------------------------------------------------------------------------------
// Result

EXPORT const char* result_string(Result result)
{
    switch (result)
    {
    case Success: return "Operation succeeded";
    case NeedMoreData: return "Not enough recovery data received";
    case TooMuchData: return "Buffer counts are too high";
    case InvalidSize: return "Buffer size must be a multiple of 64 bytes";
    case InvalidCounts: return "Invalid counts provided";
    case InvalidInput: return "A function parameter was invalid";
    case Platform: return "Platform is unsupported";
    case CallInitialize: return "Call codec_init() first";
    }
    return "Unknown";
}


//------------------------------------------------------------------------------
// Encoder API

EXPORT unsigned codec_encode_work_count(
    unsigned original_count,
    unsigned recovery_count)
{
    if (original_count == 1)
        return recovery_count;
    if (recovery_count == 1)
        return 1;
    return codec::NextPow2(recovery_count) * 2;
}

// recovery_data = parity of original_data (xor sum)
static void EncodeM1(
    uint64_t buffer_bytes,
    unsigned original_count,
    const void* const * const original_data,
    void* recovery_data)
{
    memcpy(recovery_data, original_data[0], buffer_bytes);

    codec::XORSummer summer;
    summer.Initialize(recovery_data);

    for (unsigned i = 1; i < original_count; ++i)
        summer.Add(original_data[i], buffer_bytes);

    summer.Finalize(buffer_bytes);
}

EXPORT Result encode(
    uint64_t buffer_bytes,                    // Number of bytes in each data buffer
    unsigned original_count,                  // Number of original_data[] buffer pointers
    unsigned recovery_count,                  // Number of recovery_data[] buffer pointers
    unsigned work_count,                      // Number of work_data[] buffer pointers, from codec_encode_work_count()
    const void* const * const original_data,  // Array of pointers to original data buffers
    void** work_data)                         // Array of work buffers
{
    if (buffer_bytes <= 0 || buffer_bytes % 64 != 0)
        return InvalidSize;

    if (recovery_count <= 0 || recovery_count > original_count)
        return InvalidCounts;

    if (!original_data || !work_data)
        return InvalidInput;

    if (!m_Initialized)
        return CallInitialize;

    // Handle k = 1 case
    if (original_count == 1)
    {
        for (unsigned i = 0; i < recovery_count; ++i)
            memcpy(work_data[i], original_data[i], buffer_bytes);
        return Success;
    }

    // Handle m = 1 case
    if (recovery_count == 1)
    {
        EncodeM1(
            buffer_bytes,
            original_count,
            original_data,
            work_data[0]);
        return Success;
    }

    const unsigned m = codec::NextPow2(recovery_count);
    const unsigned n = codec::NextPow2(m + original_count);

    if (work_count != m * 2)
        return InvalidCounts;

#ifdef HAS_FF8
    if (n <= codec::ff8::kOrder)
    {
        codec::ff8::ReedSolomonEncode(
            buffer_bytes,
            original_count,
            recovery_count,
            m,
            original_data,
            work_data);
    }
    else
#endif // HAS_FF8
#ifdef HAS_FF16
    if (n <= codec::ff16::kOrder)
    {
        codec::ff16::ReedSolomonEncode(
            buffer_bytes,
            original_count,
            recovery_count,
            m,
            original_data,
            work_data);
    }
    else
#endif // HAS_FF16
        return TooMuchData;

    return Success;
}


//------------------------------------------------------------------------------
// Decoder API

EXPORT unsigned codec_decode_work_count(
    unsigned original_count,
    unsigned recovery_count)
{
    if (original_count == 1 || recovery_count == 1)
        return original_count;
    const unsigned m = codec::NextPow2(recovery_count);
    const unsigned n = codec::NextPow2(m + original_count);
    return n;
}

static void DecodeM1(
    uint64_t buffer_bytes,
    unsigned original_count,
    const void* const * original_data,
    const void* recovery_data,
    void* work_data)
{
    memcpy(work_data, recovery_data, buffer_bytes);

    codec::XORSummer summer;
    summer.Initialize(work_data);

    for (unsigned i = 0; i < original_count; ++i)
        if (original_data[i])
            summer.Add(original_data[i], buffer_bytes);

    summer.Finalize(buffer_bytes);
}

EXPORT Result decode(
    uint64_t buffer_bytes,                    // Number of bytes in each data buffer
    unsigned original_count,                  // Number of original_data[] buffer pointers
    unsigned recovery_count,                  // Number of recovery_data[] buffer pointers
    unsigned work_count,                      // Number of buffer pointers in work_data[]
    const void* const * const original_data,  // Array of original data buffers
    const void* const * const recovery_data,  // Array of recovery data buffers
    void** work_data)                         // Array of work data buffers
{
    if (buffer_bytes <= 0 || buffer_bytes % 64 != 0)
        return InvalidSize;

    if (recovery_count <= 0 || recovery_count > original_count)
        return InvalidCounts;

    if (!original_data || !recovery_data || !work_data)
        return InvalidInput;

    if (!m_Initialized)
        return CallInitialize;

    // Check if not enough recovery data arrived
    unsigned original_loss_count = 0;
    unsigned original_loss_i = 0;
    for (unsigned i = 0; i < original_count; ++i)
    {
        if (!original_data[i])
        {
            ++original_loss_count;
            original_loss_i = i;
        }
    }
    unsigned recovery_got_count = 0;
    unsigned recovery_got_i = 0;
    for (unsigned i = 0; i < recovery_count; ++i)
    {
        if (recovery_data[i])
        {
            ++recovery_got_count;
            recovery_got_i = i;
        }
    }
    if (recovery_got_count < original_loss_count)
        return NeedMoreData;

    // Handle k = 1 case
    if (original_count == 1)
    {
        memcpy(work_data[0], recovery_data[recovery_got_i], buffer_bytes);
        return Success;
    }
    
    // Handle case original_loss_count = 0
    if (original_loss_count == 0)
    {
        for(unsigned i = 0; i < original_count; i++)
            memcpy(work_data[i], original_data[i], buffer_bytes);
        return Success;
    }

    // Handle m = 1 case
    if (recovery_count == 1)
    {
        DecodeM1(
            buffer_bytes,
            original_count,
            original_data,
            recovery_data[0],
            work_data[original_loss_i]);
        return Success;
    }

    const unsigned m = codec::NextPow2(recovery_count);
    const unsigned n = codec::NextPow2(m + original_count);

    if (work_count != n)
        return InvalidCounts;

#ifdef HAS_FF8
    if (n <= codec::ff8::kOrder)
    {
        codec::ff8::ReedSolomonDecode(
            buffer_bytes,
            original_count,
            recovery_count,
            m,
            n,
            original_data,
            recovery_data,
            work_data);
    }
    else
#endif // HAS_FF8
#ifdef HAS_FF16
    if (n <= codec::ff16::kOrder)
    {
        codec::ff16::ReedSolomonDecode(
            buffer_bytes,
            original_count,
            recovery_count,
            m,
            n,
            original_data,
            recovery_data,
            work_data);
    }
    else
#endif // HAS_FF16
        return TooMuchData;

    return Success;
}


} // extern "C"
