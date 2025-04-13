//============================================================================
//
//  Balancer - Nonlinear highpass filter to remove offset in signal
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef BALANCER_H
#define BALANCER_H

#include <soundio/Sound.h>

class Balancer
{
    Sound m_src;

    // Balancing buffers (Read)
    int m_mm_filterlen; // length of min/max filters
    int m_lp_filterlen; // length of threshold hann filter
    float *m_ibuf;
    float *m_m0buf;
    float *m_m1buf;
    int m_mbuf_size;

public:
    Balancer(const Sound& src, int mm_filterlen, int lp_filterlen);
    Balancer() = delete;
    Balancer(const Balancer&) = delete;
    virtual ~Balancer();

    // Interface similar to Sound for retreiving the output
    int GetSampleRate() const { return m_src.GetSampleRate(); }
    int GetLength() const { return m_src.GetLength(); }
    bool Read(int where, float *buf, int len);
    bool Read(int where, float *buf, float *abuf, int len); // Version with amplitude
};

#endif
