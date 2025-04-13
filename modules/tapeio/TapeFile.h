//============================================================================
//
//  TapeFile - File extracted from tape
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef TAPEFILE_H
#define TAPEFILE_H

#include <stdint.h>

//----------------------------------------------------------------------------

struct TapeFile
{
    uint8_t  header[9];

    // Parameters which are decoded from header
    uint16_t start_addr;
    uint16_t end_addr;
    int      len;
    bool     basic;
    bool     autorun;
    bool     slow;       // not actually stored in header

    uint8_t  name[16+1]; // One guard byte against corrupt string

    uint8_t  payload[64*1024];

    int      sync_errors;
    int      parity_errors;
    double   start_time;   // onset of first byte, seconds
    double   end_time;     // time past end byte, seconds
};

#endif
