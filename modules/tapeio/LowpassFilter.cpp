//----------------------------------------------------------------------------
//
//  LowpassFilter
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "LowpassFilter.h"
#include "filters.h"
#include <assert.h>
#include <tgmath.h>

//----------------------------------------------------------------------------

LowpassFilter::LowpassFilter(const Sound& src, int lp_filterlen) :
    m_src(src)
{
    assert(lp_filterlen & 1);
    m_lp_filterlen = lp_filterlen;
}

//----------------------------------------------------------------------------

LowpassFilter::~LowpassFilter()
{
    delete[] m_ibuf;
}

//----------------------------------------------------------------------------

bool LowpassFilter::Read(int where, float *buf, int len)
{
    int lp_margin = m_lp_filterlen>>1;
    int ibuf_len =  len + 2*lp_margin;

     // Allocate buffers
    if (m_ibuf_size < ibuf_len)
    {
        delete[] m_ibuf;
        m_ibuf = new float[ibuf_len];
    }

    // Read source
    bool ok = m_src.Read(where - lp_margin, m_ibuf, ibuf_len);

    // Low-pass filter
    hann_lowpass(buf, len, m_ibuf, ibuf_len, m_lp_filterlen);

    return ok;
}
