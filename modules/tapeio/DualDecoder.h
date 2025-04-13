//============================================================================
//
//  DualDecoder - a two-stage decoder capable of both slow and fast formats
//
//  Capable of both fast and slow formats, hence the name
//  The decoder which works in two steps
//  * Binarization, format neutral
//  * Bit to byte, slow and fast run in parallel
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef DUALDECODER_H
#define DUALDECODER_H

#include "DecoderBackend.h"
#include "DecoderOptions.h"
#include "Binarizer.h"

class Sound;

class DualDecoder : public DecoderBackend
{
    Binarizer *m_binarizer = 0;
    DecoderOptions m_options;
    int m_sample_rate = 0;

    // Clip interval
    int m_start_pos = 0;
    int m_end_pos = 0;

    // Clock parameters
    double m_t_ref  = 0;  // nominal physical bit period
    double m_t_clk  = 0;  // center of current search window
    double m_dt_min = 0;  // minimum search window half width
    double m_dt_max = 0;  // maximum search window half width
    double m_dt_clk = 0;  // current search window helf width

    // Main buffer, window length and hop size
    int m_windowlen = 0;
    int m_hopsize = 0;
    int m_window_offs = 0;

    // Bit event buffer
    int m_bit_evt_bufsize = 0;
    int *m_bit_evt_xs = 0;
    bool *m_bit_evt_vals = 0;
    int m_bit_evt_cnt = 0;

    struct ByteDecoder
    {
        // Byte event buffer
        bool enabled = false;
        int bufsize = 0;
        int *xs = 0;          // byte event locations, as bit offset in window
        uint16_t *zs = 0;     // 13-bit LSB first representation
        double *times = 0;    // global time in seconds
        int boundary_x = -1;  // event for use as viterbi boundary
        int last_x = -1;      // location of last emitted byte
        int emit_start = 0;   // range of events to emit
        int emit_end = 0;
    };

    ByteDecoder m_byte_decoders[2]; // 0=fast 1=slow

    // Dump
    Sound *m_dump_snd = 0;
    float *m_dump_buf = 0;

public:
    DualDecoder(const DualDecoder&) = delete;
    DualDecoder(const Sound& src,
                const DecoderOptions& options,
                bool enable_fast,
                bool enable_slow);
    virtual ~DualDecoder();

    bool DecodeByte(DecodedByte *b) override;

private:
    void DecodeByteWindow(bool last_window);
    void AdvanceByteWindow(int advance_bits);
    bool DecodeWindow();
};

#endif
