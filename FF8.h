#pragma once

#include "Common.h"

#ifdef HAS_FF8

/*
    8-bit Finite Field Math

    This finite field contains 256 elements and so each element is one byte.
    This library is designed for data that is a multiple of 64 bytes in size.

    Algorithms are described in Common.h
*/

namespace codec { namespace ff8 {


//------------------------------------------------------------------------------
// Datatypes and Constants

// Finite field element type
typedef uint8_t ffe_t;

// Number of bits per element
static const unsigned kBits = 8;

// Finite field order: Number of elements in the field
static const unsigned kOrder = 256;

// Modulus for field operations
static const ffe_t kModulus = 255;

// LFSR Polynomial that generates the field elements
static const unsigned kPolynomial = 0x11D;


//------------------------------------------------------------------------------
// API

// Returns false if the self-test fails
bool Initialize();

void ReedSolomonEncode(
    uint64_t buffer_bytes,
    unsigned original_count,
    unsigned recovery_count,
    unsigned m, // = NextPow2(recovery_count)
    const void* const * const data,
    void** work); // m * 2 elements

void ReedSolomonDecode(
    uint64_t buffer_bytes,
    unsigned original_count,
    unsigned recovery_count,
    unsigned m, // = NextPow2(recovery_count)
    unsigned n, // = NextPow2(m + original_count)
    const void* const * const original, // original_count elements
    const void* const * const recovery, // recovery_count elements
    void** work); // n elements


}} // namespace codec::ff8

#endif // HAS_FF8
