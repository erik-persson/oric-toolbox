//============================================================================
//
//  SuperBinarizer - revised grid binarizer with less jitter issues
//
//  Extract physical bits from signal
//  Appropriate for both fast and slow formats
//
//  Copyright (c) 2021-2023 Erik Persson
//
//============================================================================

#ifndef SUPER_BINARIZER_H
#define SUPER_BINARIZER_H

#include "Binarizer.h"
#include "LowpassFilter.h"

class SuperBinarizer : public Binarizer
{
    LowpassFilter m_long_filter;
    LowpassFilter m_short_filter;

    float *m_long_buf = 0;  // Lowpass filtered input
    float *m_band_buf = 0;  // Bandpass filtered input
    float *m_mag_buf = 0;   // Manitude buffer
    float *m_edf_buf = 0;   // Edge detection function
    int m_bufsize = 0;

public:
    SuperBinarizer(const SuperBinarizer&) = delete;
    SuperBinarizer(const Sound& src, double t_ref);
    virtual ~SuperBinarizer();

    // Sound parameters
    int GetSampleRate() const override { return m_short_filter.GetSampleRate(); }
    int GetLength() const override { return m_long_filter.GetLength(); }

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
