//----------------------------------------------------------------------------
//
//  Decoders to extract bytestream from Oric tape format
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef DECODERBACKEND_H
#define DECODERBACKEND_H

#include "DecoderOptions.h"

#include <stdint.h>

struct DecodedByte;

//----------------------------------------------------------------------------
// DecoderBackend - base class for decoder backends
//----------------------------------------------------------------------------

class DecoderBackend
{
public:
    virtual ~DecoderBackend() {}
    virtual bool DecodeByte(DecodedByte *b) = 0;
};

//----------------------------------------------------------------------------
// Common low level functions
//----------------------------------------------------------------------------

// Xor together bits in byte
static inline int parity8(uint8_t x)
{
    x ^= (x>>4);
    x ^= (x>>2);
    x ^= (x>>1);
    return x & 1;
}

//----------------------------------------------------------------------------

// Check if sync bits are ok in 13-bit representation (LSB first)
// Nominally there are 3 stop bits, but for similarity with the Oric tape
// reading routine we check only the first two (bits 10 and 11).
static inline bool is_sync_ok(uint16_t z)
{
    return (z & 0x0c01) == 0x0c00;
}

//----------------------------------------------------------------------------

// Check if parity is OK in 13-bit representation (LSB first)
static inline bool is_parity_ok(uint16_t z)
{
    uint8_t byte = (z>>1) & 255;
    int parity = (z>>9) & 1;
    int expected_parity = !parity8(byte);
    return parity == expected_parity;
}

//----------------------------------------------------------------------------

// Get data bits from 13-bit representation (LSB first)
static inline uint8_t get_data_bits(uint16_t z)
{
    uint8_t byte = (z>>1) & 255;
    return byte;
}

#endif
