//----------------------------------------------------------------------------
//
//  PatternBinarizer - Viterbi pattern-matching binarizer
//
//  Applicable to both fast and slow formats
//
//  Copyright (c) 2021-2023 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef PATTERNBINARIZER_H
#define PATTERNBINARIZER_H

#include "Binarizer.h"
#include "Balancer.h"

class PatternBinarizer : public Binarizer
{
    Balancer m_balancer;

    float *m_buf = 0;    // Balanced signal
    float *m_abuf = 0;   // Amplitude buffer
    int m_bufsize = 0;
    int m_loaded_start = 0;
    int m_loaded_end = 0;

public:
    PatternBinarizer(const PatternBinarizer&) = delete;
    PatternBinarizer(const Sound& src, double t_ref);
    virtual ~PatternBinarizer();

    // Sound parameters
    int GetSampleRate() const override { return m_balancer.GetSampleRate(); }
    int GetLength() const override { return m_balancer.GetLength(); }

    // Main entry point. Return no. of bit events found
    int Read(
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
        double dt_clk         // Half-range of clock search window
    ) override;
};

#endif
