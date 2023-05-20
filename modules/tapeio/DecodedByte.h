//----------------------------------------------------------------------------
//
//  DecodedByte - Structure representing a byte decoded from tape
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef DECODEDBYTE_H
#define DECODEDBYTE_H

#include <stdint.h>

struct DecodedByte
{
    double time;       // Onset in seconds
    bool slow;         // Slow format
    uint8_t byte;      // Data
    bool parity_error; // Set if parity bit was incorrect
    bool sync_error;   // Set if a sync bit was incorrect
};

#endif
