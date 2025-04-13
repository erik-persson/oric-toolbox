//============================================================================
//
//  Balancer - Nonlinear highpass filter to remove offset in signal
//
//  Copyright (c) 2021-2022 Erik Persson
//
//            .-----------------------------.
//            |  .---.                      |
//  Input     +->|min|--.                 + v
//  Signal  --+  '---'  |  .---.  .----. -.---.
//            |          =>|avg|->| LP |->| + |-->  Balanced output
//            |  .---.  |  '---'  '----'  '---'
//            '->|max|--'
//               '---'
//============================================================================

#include "Balancer.h"
#include "filters.h"
#include <assert.h>
#include <tgmath.h>

//----------------------------------------------------------------------------

Balancer::Balancer(const Sound& src, int mm_filterlen, int lp_filterlen) :
    m_src(src)
{
    assert(mm_filterlen & 1);
    assert(lp_filterlen & 1);
    m_mm_filterlen = mm_filterlen;
    m_lp_filterlen = lp_filterlen;
    m_ibuf = 0;
    m_m0buf = 0;
    m_m1buf = 0;
    m_mbuf_size = 0;
}

//----------------------------------------------------------------------------

Balancer::~Balancer()
{
    delete[] m_ibuf;
    delete[] m_m0buf;
    delete[] m_m1buf;
}

//----------------------------------------------------------------------------

bool Balancer::Read(int where, float *buf, float *abuf, int len)
{
    // Generate a threshold level
    int mm_margin = m_mm_filterlen>>1;
    int lp_margin = m_lp_filterlen>>1;
    int mm_mbuf_len = len + 2*lp_margin;
    int mm_ibuf_len = len + 2*lp_margin + 2*mm_margin;

    // Allocate min/max input buffer
    if (m_mbuf_size < mm_mbuf_len)
    {
        delete[] m_ibuf;
        delete[] m_m0buf;
        delete[] m_m1buf;
        m_ibuf = new float[mm_ibuf_len];
        m_m0buf = new float[mm_mbuf_len];
        m_m1buf = new float[mm_mbuf_len];
        m_mbuf_size = mm_mbuf_len;
    }

    // Read source
    bool ok = m_src.Read(where - mm_margin - lp_margin, m_ibuf, mm_ibuf_len);

    // Run min and max filters
    running_min(m_m0buf, mm_mbuf_len, m_ibuf, mm_ibuf_len, m_mm_filterlen);
    running_max(m_m1buf, mm_mbuf_len, m_ibuf, mm_ibuf_len, m_mm_filterlen);

    // Average min and max to get a threshold level
    for (int i= 0; i<mm_mbuf_len; i++)
    {
        float m0 = m_m0buf[i];
        float m1 = m_m1buf[i];
        m_m0buf[i] = .5*(m0 + m1);
        m_m1buf[i] = .5*(m1 - m0); // save difference for potential use
    }

    // Low-pass filter the threshold level into the output buffer
    hann_lowpass(buf, len, m_m0buf, mm_mbuf_len, m_lp_filterlen);

    // Subtract filtered threshold level from the input signal.
    for (int i= 0; i<len; i++)
        buf[i]= m_ibuf[mm_margin+lp_margin+i] - buf[i];

    if (abuf)
        // Low-pass filter the max-min to get an amplitude
        hann_lowpass(abuf, len, m_m1buf, mm_mbuf_len, m_lp_filterlen);

    return ok;
}

//----------------------------------------------------------------------------

bool Balancer::Read(int where, float *buf, int len)
{
    return Read(where, buf, 0, len);
}
