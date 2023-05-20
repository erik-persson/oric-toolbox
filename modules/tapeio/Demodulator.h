//----------------------------------------------------------------------------
//
//  Demodulator
//
//  Filter to recover tapes where 2400 Hz oscillations are
//  too faded to detect reliably.
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef DEMODULATOR_H
#define DEMODULATOR_H

#include <soundio/Sound.h>

class Demodulator
{
    Sound m_src;

    int m_ss_rate;     // subsampled output rate, 2400 seems a good value
    int m_ss_len;      // subsampled length
    bool m_use_high_band;
    int m_t_carrier;
    int m_t_lowpass;

    // Buffers used in demodulation (ReadDemodFullres)
    float *m_dm_ckern;
    float *m_dm_skern;
    float *m_dm_cbuf;
    float *m_dm_sbuf;
    float *m_dm_obuf0;
    float *m_dm_obuf1;
    int m_dm_obuf_size;

    // Buffers used in downsampling (ReadDemod)
    float *m_dsin_buf;
    int m_dsin_buf_size;

    // Balancing buffers (Read)
    int m_mm_filterlen; // length of min/max filters
    int m_th_filterlen; // length of threshold hann filter
    float *m_mm_ibuf;
    float *m_mm_m0buf;
    float *m_mm_m1buf;
    int m_mm_mbuf_size;

public:
    Demodulator(const Sound& src,
        int f_ref_hz,        // Reference physical symbol rate, nominally 4800 Hz
        bool use_high_band); // Set to use high band, '1' symbol band
    Demodulator() = delete;
    Demodulator(const Demodulator&) = delete;
    virtual ~Demodulator();

    // Interface similar to Sound for retreiving the output
    int GetSampleRate() const { return m_ss_rate; }
    int GetLength() const { return m_ss_len; }
    bool Read(int where, float *buf, int len);

private:
    // Stage 1 result - demodulation result, full resolution
    bool ReadDemodFullres(int where, float *buf, int len);

    // Stage 2 result - demodulation result, downsampled
    bool ReadDemod(int where, float *buf, int len);
};

#endif
