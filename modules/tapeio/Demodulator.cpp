//----------------------------------------------------------------------------
//
//  Demodulator
//
//  Filter to recover tapes where 2400 Hz oscillations are
//  too faded to detect reliably.
//
//  Copyright (c) 2021-2022 Erik Persson
//
//     .----.  .----.                    .-----------------------------.
//  .->|*cos|->| LP |--.                 |  .---.                      |
//  |  '----'  '----'  |  .---.  .----.  +->|min|--.                 - v
//--+                   =>|abs|->|down|--+  '---'  |  .---.  .----. +.---.
//  |  .----.  .----.  |  '---'  '----'  |          =>|avg|->| LP |->| + |-->
//  '->|*sin|->| LP |--'                 |  .---.  |  '---'  '----'  '---'
//     '----'  '----'                    '->|max|--'
//                                          '---'
// |<---------- Demodulation --------->|<----------- Balancing ---------->|
//
//  Demodulating with a 1200 Hz carrier (the '0' bit signal)
//  then downsampling to 2400 Hz.
//----------------------------------------------------------------------------

#include "Demodulator.h"
#include "filters.h"
#include <assert.h>
#include <tgmath.h>

//----------------------------------------------------------------------------

Demodulator::Demodulator(const Sound& src,
                         int f_ref_hz,       // Reference physical symbol rate
                         bool use_high_band) // Select 2400 Hz band when set
    : m_src(src)
{
    m_use_high_band = use_high_band;

    // As carrier we take either:
    // Low band: 1200 Hz for the nominal '1' pattern
    // High band: 2400 Hz for the nominal '0' pattern
    int carrier_hz = f_ref_hz/(m_use_high_band ? 2:4);

    m_ss_rate = f_ref_hz/2; // our subsampled output rate, nominally 2400 Hz

    int src_rate = src.GetSampleRate();

    // Length of entire ta  xpe in subsampled resolution
    m_ss_len = (int) floor( 0.5 + ((double)src.GetLength()) * m_ss_rate / src_rate );

    // Carrier period in input samples
    m_t_carrier = (src_rate + carrier_hz/2)/carrier_hz;

    // Size of lowpass kernel, 4 low carrier periods
    m_t_lowpass = (16*src_rate/f_ref_hz)|1;

    // Cos and sin kernels
    double k = 2*M_PI/m_t_carrier;
    m_dm_ckern = new float[m_t_carrier];
    m_dm_skern = new float[m_t_carrier];
    for (int i=0; i<m_t_carrier; i++)
    {
        double phi = k*i;
        m_dm_ckern[i] = cos(phi);
        m_dm_skern[i] = sin(phi);
    }

    // Demand allocated demodulation buffers
    m_dm_cbuf = 0;
    m_dm_sbuf = 0;
    m_dm_obuf0 = 0;
    m_dm_obuf1 = 0;
    m_dm_obuf_size = 0;

    // Downsampling input buffer
    m_dsin_buf = 0;
    m_dsin_buf_size = 0;

    // Size of minmax, 256 reference periods in low sample rate
    // C.f. one byte is 209/4 = 52.25 carrier periods
    m_mm_filterlen = (256*m_ss_rate/f_ref_hz)|1;
    m_th_filterlen = (3*m_mm_filterlen)|1;
    m_mm_ibuf = 0;
    m_mm_m0buf = 0;
    m_mm_m1buf = 0;
    m_mm_mbuf_size = 0;
}

//----------------------------------------------------------------------------

Demodulator::~Demodulator()
{
    delete[] m_dm_ckern;
    delete[] m_dm_skern;
    delete[] m_dm_cbuf;
    delete[] m_dm_sbuf;
    delete[] m_dm_obuf0;
    delete[] m_dm_obuf1;
    delete[] m_dsin_buf;
    delete[] m_mm_ibuf;
    delete[] m_mm_m0buf;
    delete[] m_mm_m1buf;
}

//----------------------------------------------------------------------------

bool Demodulator::ReadDemodFullres(int where, float *buf, int len)
{
    int filter_margin = m_t_lowpass/2;
    int ibuf_len = len+2*filter_margin;
    int obuf_len = len;

    // Allocate demodulation buffers
    if (m_dm_obuf_size < obuf_len)
    {
        delete[] m_dm_cbuf;
        delete[] m_dm_sbuf;
        delete[] m_dm_obuf0;
        delete[] m_dm_obuf1;
        m_dm_cbuf  = new float[ibuf_len];
        m_dm_sbuf  = new float[ibuf_len];
        m_dm_obuf0 = new float[obuf_len];
        m_dm_obuf1 = new float[obuf_len];
        m_dm_obuf_size = obuf_len;
    }

    float *cbuf   = m_dm_cbuf;
    float *sbuf   = m_dm_sbuf;
    float *obuf0  = m_dm_obuf0;
    float *obuf1  = m_dm_obuf1;

    if (!m_src.Read(where-filter_margin, cbuf, ibuf_len))
        return false;

    // Prodce cosine and sine multiplied version
    for (int i= 0; i<ibuf_len; i++)
    {
        int j = i % m_t_carrier;
        sbuf[i] = cbuf[i];
        cbuf[i] *= m_dm_ckern[j];
        sbuf[i] *= m_dm_skern[j];
    }

    hann_lowpass(obuf0, len, cbuf, ibuf_len, m_t_lowpass);
    hann_lowpass(obuf1, len, sbuf, ibuf_len, m_t_lowpass);

    for (int i= 0; i<obuf_len; i++)
        buf[i] = sqrt(obuf0[i]*obuf0[i] + obuf1[i]*obuf1[i]);
    return true;
}

//----------------------------------------------------------------------------

bool Demodulator::ReadDemod(int where, float *buf, int bufsize)
{
    // First read in high resolution,
    // Then subsample to the output resolution
    int src_rate = m_src.GetSampleRate();
    double k_subsamp = double(src_rate)/m_ss_rate;

    int interp_filter_margin = 3;
    int t0 = (int) ( floor(k_subsamp*where) - interp_filter_margin);
    int t1 = (int) ( ceil(k_subsamp*(where+bufsize-1)) ) + interp_filter_margin;
    int dsin_len = t1+1-t0;

    // Allocate downsampling input buffer
    if (m_dsin_buf_size < dsin_len)
    {
        delete[] m_dsin_buf;
        m_dsin_buf = new float[dsin_len];
        m_dsin_buf_size = dsin_len;
    }
    float *dsin_buf = m_dsin_buf;

    bool ok = ReadDemodFullres(t0, dsin_buf, dsin_len);

    for (int i = 0; i<bufsize; i++)
        buf[i] = interp(dsin_buf, dsin_len, k_subsamp*(where+i) - t0);

    return ok;
}

//----------------------------------------------------------------------------

bool Demodulator::Read(int where, float *buf, int len)
{
    // Generate a threshold level for the demodulated signal
    int mm_margin = m_mm_filterlen/2;
    int th_margin = m_th_filterlen/2;
    int mm_mbuf_len = len + 2*th_margin;
    int mm_ibuf_len = len + 2*th_margin + 2*mm_margin;

    // Allocate min/max input buffer
    if (m_mm_mbuf_size < mm_mbuf_len)
    {
        delete[] m_mm_ibuf;
        delete[] m_mm_m0buf;
        delete[] m_mm_m1buf;
        m_mm_ibuf = new float[mm_ibuf_len];
        m_mm_m0buf = new float[mm_mbuf_len];
        m_mm_m1buf = new float[mm_mbuf_len];
        m_mm_mbuf_size = mm_mbuf_len;
    }

    // Read demodulated signal
    bool ok = ReadDemod(where - mm_margin - th_margin, m_mm_ibuf, mm_ibuf_len);

    // Run min and max filters
    running_min(m_mm_m0buf, mm_mbuf_len, m_mm_ibuf, mm_ibuf_len, m_mm_filterlen);
    running_max(m_mm_m1buf, mm_mbuf_len, m_mm_ibuf, mm_ibuf_len, m_mm_filterlen);

    // Threshold level: Blend 65% min and 35% max. Compared to 50-50 averaging
    // this handles dips in signal strength better. For instance it can decode
    // correctly even when magnitude falls below 50%.
    for (int i= 0; i<mm_mbuf_len; i++)
        m_mm_m0buf[i] = .65*m_mm_m0buf[i] + .35*m_mm_m1buf[i];

    // Low-pass filter the threshold level into the output buffer
    hann_lowpass(buf,len, m_mm_m0buf, mm_mbuf_len, m_th_filterlen);

    if (m_use_high_band)
    {
        // The modulation signal indicates when there is a '1'.
        // Subtract the threshold level.
        for (int i= 0; i<len; i++)
            buf[i] = m_mm_ibuf[mm_margin+th_margin+i] - buf[i];
    }
    else
    {
        // The modulation signal indicates when there is a '0'.
        // Subtract threshold and negate, so '1' becomes the positive direction
        for (int i= 0; i<len; i++)
            buf[i] -= m_mm_ibuf[mm_margin+th_margin+i];
    }
    return ok;
}
