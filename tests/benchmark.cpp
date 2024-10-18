#include "../Common.h"
#include "../FF8.h"
#include "../FF16.h"
#include "../codec.h"

#include <memory>
#include <vector>
#include <iostream>
#include <string>
using namespace std;

//#define TEST_DATA_ALL_SAME

struct TestParameters
{
#ifdef HAS_FF16
    unsigned original_count = 1000; // under 65536
    unsigned recovery_count = 100; // under 65536 - original_count
#else
    unsigned original_count = 100; // under 65536
    unsigned recovery_count = 10; // under 65536 - original_count
#endif
    unsigned buffer_bytes = 64000; // multiple of 64 bytes
    unsigned loss_count = 32768; // some fraction of original_count
    unsigned seed = 2;
};

static const unsigned kLargeTrialCount = 1;
static const unsigned kSmallTrialCount = 1;


//------------------------------------------------------------------------------
// Windows

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN

    #ifndef _WINSOCKAPI_
        #define DID_DEFINE_WINSOCKAPI
        #define _WINSOCKAPI_
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601 /* Windows 7+ */
    #endif

    #include <windows.h>
#endif

#ifdef DID_DEFINE_WINSOCKAPI
    #undef _WINSOCKAPI_
    #undef DID_DEFINE_WINSOCKAPI
#endif


//------------------------------------------------------------------------------
// Threads

static bool SetCurrentThreadPriority()
{
#ifdef _WIN32
    return 0 != ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#else
    // setpriority on mac os x
    return true;
#endif
}


//------------------------------------------------------------------------------
// Timing

#ifndef _WIN32
#include <sys/time.h>
#endif

static uint64_t GetTimeUsec()
{
#ifdef _WIN32
    LARGE_INTEGER timeStamp = {};
    if (!::QueryPerformanceCounter(&timeStamp))
        return 0;
    static double PerfFrequencyInverse = 0.;
    if (PerfFrequencyInverse == 0.)
    {
        LARGE_INTEGER freq = {};
        if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
            return 0;
        PerfFrequencyInverse = 1000000. / (double)freq.QuadPart;
    }
    return (uint64_t)(PerfFrequencyInverse * timeStamp.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000000 * tv.tv_sec + tv.tv_usec;
#endif // _WIN32
}


//------------------------------------------------------------------------------
// PCG PRNG
// From http://www.pcg-random.org/

class PCGRandom
{
public:
    inline void Seed(uint64_t y, uint64_t x = 0)
    {
        State = 0;
        Inc = (y << 1u) | 1u;
        Next();
        State += x;
        Next();
    }

    inline uint32_t Next()
    {
        const uint64_t oldstate = State;
        State = oldstate * UINT64_C(6364136223846793005) + Inc;
        const uint32_t xorshifted = (uint32_t)(((oldstate >> 18) ^ oldstate) >> 27);
        const uint32_t rot = oldstate >> 59;
        return (xorshifted >> rot) | (xorshifted << ((uint32_t)(-(int32_t)rot) & 31));
    }

    uint64_t State = 0, Inc = 0;
};


//------------------------------------------------------------------------------
// Self-Checking Packet

static void WriteRandomSelfCheckingPacket(PCGRandom& prng, void* packet, unsigned bytes)
{
    uint8_t* buffer = (uint8_t*)packet;
#ifdef TEST_DATA_ALL_SAME
    if (bytes != 0)
#else
    if (bytes < 16)
#endif
    {
        DEBUG_ASSERT(bytes >= 2);
        buffer[0] = (uint8_t)prng.Next();
        for (unsigned i = 1; i < bytes; ++i)
        {
            buffer[i] = buffer[0];
        }
    }
    else
    {
        uint32_t crc = bytes;
        *(uint32_t*)(buffer + 4) = bytes;
        for (unsigned i = 8; i < bytes; ++i)
        {
            uint8_t v = (uint8_t)prng.Next();
            buffer[i] = v;
            crc = (crc << 3) | (crc >> (32 - 3));
            crc += v;
        }
        *(uint32_t*)buffer = crc;
    }
}

static bool CheckPacket(const void* packet, unsigned bytes)
{
    uint8_t* buffer = (uint8_t*)packet;
#ifdef TEST_DATA_ALL_SAME
    if (bytes != 0)
#else
    if (bytes < 16)
#endif
    {
        if (bytes < 2)
            return false;

        uint8_t v = buffer[0];
        for (unsigned i = 1; i < bytes; ++i)
        {
            if (buffer[i] != v)
                return false;
        }
    }
    else
    {
        uint32_t crc = bytes;
        uint32_t readBytes = *(uint32_t*)(buffer + 4);
        if (readBytes != bytes)
            return false;
        for (unsigned i = 8; i < bytes; ++i)
        {
            uint8_t v = buffer[i];
            crc = (crc << 3) | (crc >> (32 - 3));
            crc += v;
        }
        uint32_t readCRC = *(uint32_t*)buffer;
        if (readCRC != crc)
            return false;
    }
    return true;
}


//------------------------------------------------------------------------------
// FunctionTimer

class FunctionTimer
{
public:
    FunctionTimer(const std::string& name)
    {
        FunctionName = name;
    }
    void BeginCall()
    {
        DEBUG_ASSERT(t0 == 0);
        t0 = GetTimeUsec();
    }
    void EndCall()
    {
        DEBUG_ASSERT(t0 != 0);
        const uint64_t t1 = GetTimeUsec();
        const uint64_t delta = t1 - t0;
        if (++Invokations == 1)
            MaxCallUsec = MinCallUsec = delta;
        else if (MaxCallUsec < delta)
            MaxCallUsec = delta;
        else if (MinCallUsec > delta)
            MinCallUsec = delta;
        TotalUsec += delta;
        t0 = 0;
    }
    void Reset()
    {
        DEBUG_ASSERT(t0 == 0);
        t0 = 0;
        Invokations = 0;
        TotalUsec = 0;
    }
    void Print(unsigned trials)
    {
        cout << FunctionName << " called " << Invokations / (float)trials << " times per trial. " << TotalUsec / (double)Invokations << " usec avg. " << TotalUsec / (float)trials << " usec for each of " << trials << " trials" << endl;
    }

    uint64_t t0 = 0;
    uint64_t Invokations = 0;
    uint64_t TotalUsec = 0;
    uint64_t MaxCallUsec = 0;
    uint64_t MinCallUsec = 0;
    std::string FunctionName;
};


//------------------------------------------------------------------------------
// Utility: Deck Shuffling function

/*
    Given a PRNG, generate a deck of cards in a random order.
    The deck will contain elements with values between 0 and count - 1.
*/

static void ShuffleDeck16(PCGRandom &prng, uint16_t * RESTRICT deck, uint32_t count)
{
    deck[0] = 0;

    // If we can unroll 4 times,
    if (count <= 256)
    {
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 8-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 24) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                break;

            case 3:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
            case 2:
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
            case 1:
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
            case 0:
                return;
            }
        }
    }
    else
    {
        // For each deck entry,
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 16-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint16_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                break;

            case 1:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
            case 0:
                return;
            }
        }
    }
}


//------------------------------------------------------------------------------
// Benchmark

static bool Benchmark(const TestParameters& params)
{
    const unsigned kTrials = params.original_count > 4000 ? kLargeTrialCount : kSmallTrialCount;

    std::vector<uint8_t*> original_data(params.original_count);

    const unsigned encode_work_count = codec_encode_work_count(params.original_count, params.recovery_count);
    const unsigned decode_work_count = codec_decode_work_count(params.original_count, params.recovery_count);

    std::vector<uint8_t*> codec_encode_work_data(encode_work_count);
    std::vector<uint8_t*> codec_decode_work_data(decode_work_count);

    FunctionTimer t_mem_alloc("memory_allocation");
    FunctionTimer t_encode("encode");
    FunctionTimer t_decode("decode");
    FunctionTimer t_mem_free("memory_free");

    const uint64_t total_bytes = (uint64_t)params.buffer_bytes * params.original_count;

    for (unsigned trial = 0; trial < kTrials; ++trial)
    {
        // Allocate memory:

        t_mem_alloc.BeginCall();
        for (unsigned i = 0, count = params.original_count; i < count; ++i)
            original_data[i] = codec::SIMDSafeAllocate(params.buffer_bytes);
        for (unsigned i = 0, count = encode_work_count; i < count; ++i)
            codec_encode_work_data[i] = codec::SIMDSafeAllocate(params.buffer_bytes);
        for (unsigned i = 0, count = decode_work_count; i < count; ++i)
            codec_decode_work_data[i] = codec::SIMDSafeAllocate(params.buffer_bytes);
        t_mem_alloc.EndCall();

        // Generate data:

        PCGRandom prng;
        prng.Seed(params.seed, trial);

        for (unsigned i = 0; i < params.original_count; ++i)
            WriteRandomSelfCheckingPacket(prng, original_data[i], params.buffer_bytes);

        // Encode:

        t_encode.BeginCall();
        Result encodeResult = encode(
                params.buffer_bytes,
                params.original_count,
                params.recovery_count,
                encode_work_count,
                (void **) &original_data[0],
                (void **) &codec_encode_work_data[0] // recovery data written here
        );
        t_encode.EndCall();

        if (encodeResult != Success)
        {
            if (encodeResult == TooMuchData)
            {
                cout << "Skipping this test: Parameters are unsupported by the codec" << endl;
                return true;
            }
            cout << "Error: Encode failed with result=" << encodeResult << ": " << result_string(encodeResult) << endl;
            DEBUG_BREAK;
            return false;
        }

        // Lose random original data:

        std::vector<uint16_t> original_losses(params.original_count);
        ShuffleDeck16(prng, &original_losses[0], params.original_count);

        for (unsigned i = 0, count = params.loss_count; i < count; ++i)
        {
            const unsigned loss_index = original_losses[i];
            codec::SIMDSafeFree(original_data[loss_index]);
            original_data[loss_index] = nullptr;
        }

        // Lose random recovery data:

        const unsigned recovery_loss_count = params.recovery_count - params.loss_count;

        std::vector<uint16_t> recovery_losses(params.recovery_count);
        ShuffleDeck16(prng, &recovery_losses[0], params.recovery_count);

        for (unsigned i = 0, count = recovery_loss_count; i < count; ++i)
        {
            const unsigned loss_index = recovery_losses[i];
            codec::SIMDSafeFree(codec_encode_work_data[loss_index]);
            codec_encode_work_data[loss_index] = nullptr;
        }

        // Decode:

        t_decode.BeginCall();
        Result decodeResult = decode(
            params.buffer_bytes,
            params.original_count,
            params.recovery_count,
            decode_work_count,
            (void**)&original_data[0],
            (void**)&codec_encode_work_data[0],
            (void**)&codec_decode_work_data[0]);
        t_decode.EndCall();

        if (decodeResult != Success)
        {
            cout << "Error: Decode failed with result=" << decodeResult << ": " << result_string(decodeResult) << endl;
            DEBUG_BREAK;
            return false;
        }

        for (unsigned i = 0; i < params.original_count; ++i)
        {
            if (!original_data[i])
            {
                if (!CheckPacket(codec_decode_work_data[i], params.buffer_bytes))
                {
                    cout << "Error: Data was corrupted" << endl;
                    DEBUG_BREAK;
                    return false;
                }
            }
        }

        // Free memory:

        t_mem_free.BeginCall();
        for (unsigned i = 0, count = params.original_count; i < count; ++i)
            codec::SIMDSafeFree(original_data[i]);
        for (unsigned i = 0, count = encode_work_count; i < count; ++i)
            codec::SIMDSafeFree(codec_encode_work_data[i]);
        for (unsigned i = 0, count = decode_work_count; i < count; ++i)
            codec::SIMDSafeFree(codec_decode_work_data[i]);
        t_mem_free.EndCall();
    }

#if 0
    t_mem_alloc.Print(kTrials);
    t_encode.Print(kTrials);
    t_decode.Print(kTrials);
    t_mem_free.Print(kTrials);
#endif

    float encode_input_MBPS = total_bytes / (float)(t_encode.MinCallUsec);
    float encode_output_MBPS = params.buffer_bytes * (uint64_t)params.recovery_count / (float)(t_encode.MinCallUsec);
    float decode_input_MBPS = total_bytes / (float)(t_decode.MinCallUsec);
    float decode_output_MBPS = params.buffer_bytes * (uint64_t)params.loss_count / (float)(t_decode.MinCallUsec);

    cout << "Encoder(" << total_bytes / 1000000.f << " MB in " << params.original_count << " pieces, " << params.loss_count << " losses): Input=" << encode_input_MBPS << " MB/s, Output=" << encode_output_MBPS << " MB/s" << endl;
    cout << "Decoder(" << total_bytes / 1000000.f << " MB in " << params.original_count << " pieces, " << params.loss_count << " losses): Input=" << decode_input_MBPS << " MB/s, Output=" << decode_output_MBPS << " MB/s" << endl << endl;

    return true;
}


//------------------------------------------------------------------------------
// Entrypoint

int main(int argc, char **argv)
{
    SetCurrentThreadPriority();

    FunctionTimer t_init("codec_init");

    t_init.BeginCall();
    if (0 != codec_init())
    {
        cout << "Failed to initialize" << endl;
        return -1;
    }
    t_init.EndCall();
    t_init.Print(1);

    TestParameters params;
    PCGRandom prng;

    if (argc >= 2)
        params.original_count = atoi(argv[1]);
    if (argc >= 3)
        params.recovery_count = atoi(argv[2]);
    if (argc >= 4)
        params.buffer_bytes = atoi(argv[3]);
    if (argc >= 5)
        params.loss_count = atoi(argv[4]);

    if (params.loss_count > params.recovery_count)
        params.loss_count = params.recovery_count;

    cout << "Parameters: [original count=" << params.original_count << "] [recovery count=" << params.recovery_count << "] [buffer bytes=" << params.buffer_bytes << "] [loss count=" << params.loss_count << "] [random seed=" << params.seed << "]" << endl;

    if (!Benchmark(params))
        goto Failed;

#if 1
    static const unsigned kMaxLargeRandomData = 32768;
    static const unsigned kMaxSmallRandomData = 128;

    prng.Seed(params.seed, 8);
    for (;; ++params.seed)
    {
        // Large:
        {
            params.original_count = prng.Next() % kMaxLargeRandomData + 1;
            params.recovery_count = prng.Next() % params.original_count + 1;
            params.loss_count = prng.Next() % params.recovery_count + 1;

            cout << "Parameters: [original count=" << params.original_count << "] [recovery count=" << params.recovery_count << "] [buffer bytes=" << params.buffer_bytes << "] [loss count=" << params.loss_count << "] [random seed=" << params.seed << "]" << endl;

            if (!Benchmark(params))
                goto Failed;
        }
        // Small:
        {
            params.original_count = prng.Next() % kMaxSmallRandomData + 1;
            params.recovery_count = prng.Next() % params.original_count + 1;
            params.loss_count = prng.Next() % params.recovery_count + 1;

            cout << "Parameters: [original count=" << params.original_count << "] [recovery count=" << params.recovery_count << "] [buffer bytes=" << params.buffer_bytes << "] [loss count=" << params.loss_count << "] [random seed=" << params.seed << "]" << endl;

            if (!Benchmark(params))
                goto Failed;
        }
    }
#endif

#if 1
    for (unsigned original_count = 1; original_count <= 256; ++original_count)
    {
        for (unsigned recovery_count = 1; recovery_count <= original_count; ++recovery_count)
        {
            params.original_count = original_count;
            params.recovery_count = recovery_count;
            params.loss_count = recovery_count;

            cout << "Parameters: [original count=" << params.original_count << "] [recovery count=" << params.recovery_count << "] [buffer bytes=" << params.buffer_bytes << "] [loss count=" << params.loss_count << "] [random seed=" << params.seed << "]" << endl;

            if (!Benchmark(params))
                goto Failed;
        }
    }
#endif

Failed:
    cout << "Tests completed." << endl;
    getchar();

    return 0;
}
