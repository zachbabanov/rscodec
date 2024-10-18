#ifndef RS_H
#define RS_H

// Library version
#define VERSION 2

// Tweak if the functions are exported or statically linked
//#define DLL /* Defined when building/linking as DLL */
//#define BUILDING /* Defined by the library makefile */

#if defined(BUILDING)
# if defined(DLL)
    #define EXPORT __declspec(dllexport)
# else
    #define EXPORT
# endif
#else
# if defined(DLL)
    #define EXPORT __declspec(dllimport)
# else
    #define EXPORT extern
# endif
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


//------------------------------------------------------------------------------
// Initialization API

/*
    codec_init()

    Perform static initialization for the library, verifying that the platform
    is supported.

    Returns 0 on success and other values on failure.
*/

EXPORT int init_(int version);
#define codec_init() init_(VERSION)


//------------------------------------------------------------------------------
// Shared Constants / Datatypes

// Results
typedef enum ResultT
{
    Success           =  0, // Operation succeeded

    NeedMoreData      = -1, // Not enough recovery data received
    TooMuchData       = -2, // Buffer counts are too high
    InvalidSize       = -3, // Buffer size must be a multiple of 64 bytes
    InvalidCounts     = -4, // Invalid counts provided
    InvalidInput      = -5, // A function parameter was invalid
    Platform          = -6, // Platform is unsupported
    CallInitialize    = -7, // Call codec_init() first
} Result;

// Convert result to string
EXPORT const char* result_string(Result result);


//------------------------------------------------------------------------------
// Encoder API

/*
    codec_encode_work_count()

    Calculate the number of work_data buffers to provide to encode().

    The sum of original_count + recovery_count must not exceed 65536.

    Returns the work_count value to pass into encode().
    Returns 0 on invalid input.
*/
EXPORT unsigned codec_encode_work_count(
    unsigned original_count,
    unsigned recovery_count);

/*
    encode()

    Generate recovery data.

    original_count: Number of original_data[] buffers provided.
    recovery_count: Number of desired recovery data buffers.
    buffer_bytes:   Number of bytes in each data buffer.
    original_data:  Array of pointers to original data buffers.
    work_count:     Number of work_data[] buffers, from codec_encode_work_count().
    work_data:      Array of pointers to work data buffers.

    The sum of original_count + recovery_count must not exceed 65536.
    The recovery_count <= original_count.

    The buffer_bytes must be a multiple of 64.
    Each buffer should have the same number of bytes.
    Even the last piece must be rounded up to the block size.

    Let buffer_bytes = The number of bytes in each buffer:

        original_count = static_cast<unsigned>(
            ((uint64_t)total_bytes + buffer_bytes - 1) / buffer_bytes);

    Or if the number of pieces is known:

        buffer_bytes = static_cast<unsigned>(
            ((uint64_t)total_bytes + original_count - 1) / original_count);

    Returns Success on success.
    * The first set of recovery_count buffers in work_data will be the result.
    Returns other values on errors.
*/
EXPORT Result encode(
    uint64_t buffer_bytes,                    // Number of bytes in each data buffer
    unsigned original_count,                  // Number of original_data[] buffer pointers
    unsigned recovery_count,                  // Number of recovery_data[] buffer pointers
    unsigned work_count,                      // Number of work_data[] buffer pointers, from codec_encode_work_count()
    const void* const * const original_data,  // Array of pointers to original data buffers
    void** work_data);                        // Array of work buffers


//------------------------------------------------------------------------------
// Decoder API

/*
    codec_decode_work_count()

    Calculate the number of work_data buffers to provide to decode().

    The sum of original_count + recovery_count must not exceed 65536.

    Returns the work_count value to pass into encode().
    Returns 0 on invalid input.
*/
EXPORT unsigned codec_decode_work_count(
    unsigned original_count,
    unsigned recovery_count);

/*
    decode()

    Decode original data from recovery data.

    buffer_bytes:   Number of bytes in each data buffer.
    original_count: Number of original_data[] buffers provided.
    original_data:  Array of pointers to original data buffers.
    recovery_count: Number of recovery_data[] buffers provided.
    recovery_data:  Array of pointers to recovery data buffers.
    work_count:     Number of work_data[] buffers, from codec_decode_work_count().
    work_data:      Array of pointers to recovery data buffers.

    Lost original/recovery data should be set to NULL.

    The sum of recovery_count + the number of non-NULL original data must be at
    least original_count in order to perform recovery.

    Returns Success on success.
    Returns other values on errors.
*/
EXPORT Result decode(
    uint64_t buffer_bytes,                    // Number of bytes in each data buffer
    unsigned original_count,                  // Number of original_data[] buffer pointers
    unsigned recovery_count,                  // Number of recovery_data[] buffer pointers
    unsigned work_count,                      // Number of buffer pointers in work_data[]
    const void* const * const original_data,  // Array of original data buffers
    const void* const * const recovery_data,  // Array of recovery data buffers
    void** work_data);                        // Array of work data buffers


#ifdef __cplusplus
}
#endif


#endif // RS_H
