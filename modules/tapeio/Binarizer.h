//============================================================================
//
//  Binarizer - Interface to physical bit stream extractors
//
//  Copyright (c) 2021-2023 Erik Persson
//
//============================================================================

#ifndef BINARIZER_H
#define BINARIZER_H

class Binarizer
{
public:
    virtual ~Binarizer() {}

    // Sound parameters
    virtual int GetSampleRate() const = 0;
    virtual int GetLength() const = 0;

    // Main entry point. Return no. of bit events found
    virtual int Read(
        // Locations of events, relative to core_start.
        // First one is rising edge
        int *evt_xs,
        bool *evt_vals,       // Value transitioned to (or sustained)
        int evt_maxcnt,       // Max no of events to detect

        int core_start,       // Offset in samples to region of interest
        int core_len,         // Length in samples of region of interest

        float *dbgbuf,        // Debug output buffer [core_len]
        int given_rise_edge,  // -1: no known phase, >=0: force a given rise edge
        double t_clk,         // Expected clock, nominally samplerate/4800.0
        double dt_clk) = 0;   // Half-range of clock search window
};

#endif
