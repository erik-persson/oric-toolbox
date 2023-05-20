//----------------------------------------------------------------------------
//
//  GridBinarizer - alternative binarizer
//
//  Extract physical bits from signal
//  Appropriate for both fast and slow formats
//
//  Copyright (c) 2021-2023 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef GRIDBINARIZER_H
#define GRIDBINARIZER_H

#include "Binarizer.h"
#include "LowpassFilter.h"

class GridBinarizer : public Binarizer
{
    LowpassFilter m_lowpass;

    float *m_lpbuf = 0;  // Lowpass filtered input
    float *m_edfbuf = 0; // Edge detection function
    int m_bufsize = 0;

public:
    GridBinarizer(const GridBinarizer&) = delete;
    GridBinarizer(const Sound& src, double t_ref);
    virtual ~GridBinarizer();

    // Sound parameters
    int GetSampleRate() const override { return m_lowpass.GetSampleRate(); }
    int GetLength() const override { return m_lowpass.GetLength(); }

    // Main entry point. Return no. of events found
    int Read(
        // Locations of events, relative to core_start.
        // First one is rising edge
        int *evt_xs,
        bool *evt_vals,       // Value transitioned to (or sustained)
        int evt_maxcnt,       // Max no of events to detect

        int core_start,       // Offset in samples to region of interest
        int core_len,         // Length in samples of region of interest

        float *dbgbuf,        // Debug output buffer [len]
        int given_rise_edge,  // -1: no known phase, >=0: force a given rise edge
        double t_clk,         // Expected clock, nominally samplerate/4800.0
        double dt_clk         // Half-range of clock search window
    ) override;
};

#endif
