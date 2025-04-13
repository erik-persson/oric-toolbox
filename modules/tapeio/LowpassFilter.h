//============================================================================
//
//  LowpassFilter - Bit pattern template correlation filter
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef SNIPPETFILTER_H
#define SNIPPETFILTER_H

#include <soundio/Sound.h>

class LowpassFilter
{
    Sound m_src;

    // Balancing buffers (Read)
    int m_lp_filterlen; // length of Hann lowpass filter
    float *m_ibuf = 0;
    int m_ibuf_size = 0;

public:
    LowpassFilter(const Sound& src, int lp_filterlen);
    LowpassFilter() = delete;
    LowpassFilter(const LowpassFilter&) = delete;
    virtual ~LowpassFilter();

    // Interface similar to Sound for retreiving the output
    int GetSampleRate() const { return m_src.GetSampleRate(); }
    int GetLength() const { return m_src.GetLength(); }
    bool Read(int where, float *buf, int len);
};

#endif
