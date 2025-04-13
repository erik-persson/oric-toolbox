//============================================================================
//
//  XenonDecoder - a fast mode decoder inspired by the Xenon-1 tape
//
//  Copyright (c) 2021-2023 Erik Persson
//
//============================================================================

#ifndef XENONDECODER_H
#define XENONDECODER_H

#include "DecoderBackend.h"
#include "DecoderOptions.h"
#include "LowpassFilter.h"

class Sound;

class XenonDecoder : public DecoderBackend
{
    LowpassFilter m_lp_filter;
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
    int m_window_margin = 0;
    int m_window_offs = 0;
    float *m_lp_buf = 0;    // Lowpass filtered input
    float *m_wpif_buf = 0;  // Wide pulse indication (0110)
    float *m_npif_buf = 0;  // Narrow pulse indication (010)
    int8_t *m_start_detect_buf = 0; // 1..100 positive start bit, -1..-100 negative
    bool *m_use_area_buf = 0; // 1=use area based reader

    // Max value and resolution of start detect function
    #define DETECT_MAX (100)

    // Byte event buffer
    int m_byte_bufsize = 0;
    int *m_byte_xs = 0;          // byte event locations, as bit offset in window
    uint16_t *m_byte_zs = 0;     // 13-bit LSB first representation
    double *m_byte_times = 0;    // global time in seconds
    int m_byte_boundary_x = -1;  // event for use as viterbi boundary
    bool m_byte_boundary_use_area = false; // read mode for boundary
    int m_byte_last_x = -1;      // location of last emitted byte
    int m_byte_emit_start = 0;   // range of events to emit
    int m_byte_emit_end = 0;

    // Dump
    Sound *m_dump_snd = 0;
    float *m_dump_buf = 0;

public:
    XenonDecoder(const XenonDecoder&) = delete;
    XenonDecoder(const Sound& src,
                const DecoderOptions& options);
    virtual ~XenonDecoder();

    bool DecodeByte(DecodedByte *b) override;

private:
    bool DecodeWindow();
};

#endif
