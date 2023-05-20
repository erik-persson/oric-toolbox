//----------------------------------------------------------------------------
//
//  Demodulation based bytestream dedoder
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef DEMODDECODER_H
#define DEMODDECODER_H

#include "DecoderBackend.h"
#include "DecoderOptions.h"
#include "Demodulator.h"

class Sound;

class DemodDecoder : public DecoderBackend
{
    Demodulator m_demod0, m_demod1;
    DecoderOptions m_options;

    // Clip interval
    int m_start_pos = 0;
    int m_end_pos = 0;

    // Clock parameters
    double m_t_ref  = 0;   // nominal physical bit period
    double m_t_clk  = 0;   // center of current search window
    double m_dt_min = 0;   // minimum search window half width
    double m_dt_max = 0;   // maximum search window half width
    double m_dt_clk = 0;   // current search window helf width

    // Main buffer, window length and hop size
    int m_windowlen = 0;
    int m_hopsize = 0;
    int m_window_offs = 0;
    int m_fno = 0;
    float *m_buf0 = 0;  // Low band demodulated signal
    float *m_buf1 = 0;  // High band demodulated signal
    float *m_buf = 0;   // Selected demodulated signal

    // Buffer of byte onset locations determined in window
    int m_onset_bufsize = 0;
    int *m_onset_buf = 0;
    int m_boundary_byte_onset = -1;  // onset for use as viterbi boundary
    int m_last_byte_onset = -1;      // location of last emitted byte

    // Buffer to hold bytes decoded from window
    int m_byte_bufsize = 0;
    DecodedByte *m_byte_buf = 0;
    int m_byte_cnt = 0;
    int m_byte_index = 0;

    Sound *m_dump_snd = 0;
    float *m_dump_buf = 0;

public:
    DemodDecoder(const DemodDecoder&) = delete;
    DemodDecoder(const Sound& src,
                 const DecoderOptions& options);

    virtual ~DemodDecoder();

    bool DecodeByte(DecodedByte *b) override;

private:
    bool DecodeWindow();
};

#endif
