//----------------------------------------------------------------------------
//
//  SuperBinarizer - revised grid binarizer with less jitter issues
//
//  Copyright (c) 2021-2023 Erik Persson
//
//----------------------------------------------------------------------------

#include "SuperBinarizer.h"
#include "filters.h"

#include <soundio/Sound.h>

#include <assert.h>
#include <stdlib.h>
#include <tgmath.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------

SuperBinarizer::SuperBinarizer(const Sound& src,
                               double t_ref) :
    m_long_filter(
        src,
        ((int) floor(12.0*t_ref)) | 1 // lp_filterlen
    ),
    m_short_filter(
        src,
        ((int) floor(2.0*t_ref)) | 1 // lp_filterlen
    )
{
}

//----------------------------------------------------------------------------

SuperBinarizer::~SuperBinarizer()
{
    delete[] m_long_buf;
    delete[] m_band_buf;
    delete[] m_mag_buf;
    delete[] m_edf_buf;
}

//----------------------------------------------------------------------------
// Main function
//----------------------------------------------------------------------------

int SuperBinarizer::Read(
    int *evt_xs,          // Locations of events. First one is rising edge
    bool *evt_vals,       // Value transitioned to (or sustained)
    int evt_maxcnt,       // Max no of events to detect
    int core_start,       // Offset in samples to region of interest
    int core_len,         // Length in samples of region of interest
    float *dbgbuf,        // Debug output buffer [len]
    int given_rise_edge,  // -1: no known phase, >=0: force a given rise edge
    double t_clk,         // Expected clock, nominally samplerate/4800.0
    double dt_clk)        // Half-range of clock search window
{
    // Margin on each side of core window
    // This is about 0.05s, 2400 samples in case of 44.1 kHz,
    // Can be compared to a slow byte which is 1920 samles.
    int margin = 24*GetSampleRate()/441;
    int bufsize = margin + core_len + margin;

    if (given_rise_edge >= 0)
        given_rise_edge += margin;

    // Allocate buffers
    if (m_bufsize < bufsize)
    {
        delete[] m_long_buf;
        delete[] m_band_buf;
        delete[] m_mag_buf;
        delete[] m_edf_buf;
        m_long_buf = new float[bufsize];
        m_band_buf = new float[bufsize];
        m_mag_buf = new float[bufsize];
        m_edf_buf = new float[bufsize];
        m_bufsize = bufsize;
    }

    //------------------------------------------------
    // Band pass
    //------------------------------------------------

    bool ok = m_long_filter.Read(core_start-margin, m_long_buf, bufsize);
    assert(ok);
    ok = m_short_filter.Read(core_start-margin, m_band_buf, bufsize);
    assert(ok);
    for (int i=0; i<bufsize; i++)
        m_band_buf[i] -= m_long_buf[i];

    // Debug output
    for (int i= 0; i<core_len; i++)
        dbgbuf[i] = m_band_buf[margin+i];

    //------------------------------------------------
    // Phase detect function
    //------------------------------------------------

    // Form magnitude signal
    for (int i=0; i<bufsize; i++)
        m_mag_buf[i] = fabs(m_band_buf[i]);

    // A twice long filter, so we can reject period 4
    int mid_filterlen = ((int) floor(4.0*GetSampleRate()/4800)) | 1; // lp_filterlen
    int mid_margin = mid_filterlen/2;

    // Second high pass filter to re-balance signal
    hann_lowpass(m_edf_buf+mid_margin, bufsize-2*mid_margin,
                 m_mag_buf, bufsize, mid_filterlen);
    for (int i= 0; i<mid_margin; i++)
        m_edf_buf[i] = 0;
    for (int i= mid_margin; i<bufsize-mid_margin; i++)
        m_edf_buf[i] = m_mag_buf[i] - m_edf_buf[i];
    for (int i= bufsize-mid_margin; i<bufsize; i++)
        m_edf_buf[i] = 0;

    // Comb demonstration
    if (0)
    {
        float *tmp = new float[bufsize];
        for (int i=0; i<bufsize; i++)
        {
            float y = 0;
            for (int k=-15; k<=16; k++)
                y += interp_lin(m_edf_buf, bufsize, i + k*t_clk);
            tmp[i] = y;
        }
        for (int i= 0; i<core_len; i++)
            dbgbuf[i] = tmp[margin+i];

        delete[] tmp;
    }

    // Periodic averaging with the expected clock
    // When SCALE=1, this improves.
    // When SCALE=4, it worsens.
    if (0)
    {
        float *edf2 = new float[bufsize];
        for (int i= 0; i<bufsize; i++)
        {
            edf2[i] =
                (
                .5*interp_lin(m_edf_buf,bufsize,i-3*t_clk) +
                interp_lin(m_edf_buf,bufsize,i-2*t_clk) +
                interp_lin(m_edf_buf,bufsize,i-t_clk) +
                m_edf_buf[i] +
                interp_lin(m_edf_buf,bufsize,i+t_clk) +
                interp_lin(m_edf_buf,bufsize,i+2*t_clk) +
                .5*interp_lin(m_edf_buf,bufsize,i+3*t_clk)
                ) /6;
        }
        for (int i= 0; i<bufsize; i++)
            m_edf_buf[i] = edf2[i];
        delete[] edf2;
    }

    //------------------------------------------------
    // Forward propagation
    //------------------------------------------------

    // Upscale factor for viterbi propagation
    // Higher values are of course slower but also enable higher
    // inertia, which is desirable.
    const int SCALE = 4;

    const float INVALID_GRID_SCORE = -1e20;
    const float BOUNDARY_GRID_SCORE = 1e10;

    // Each state represents an incoming stride of (t_clk_min+s/SCALE)

    // While this would be logical, it worsens Super Advanced Breakout
    // int di_min = (int) floor(0.5 + SCALE*(t_clk - dt_clk));
    // int di_max = (int) floor(0.5 + SCALE*(t_clk + dt_clk));
    int di_min = SCALE*(int) floor(0.5 + t_clk - dt_clk);
    int di_max = SCALE*(int) floor(0.5 + t_clk + dt_clk);

    int ns = di_max - di_min + 1;
    assert(ns<256); // so we can use uint8_t

    int ni = SCALE*bufsize;
    float *grid_scores = new float[ni*ns];
    uint8_t *grid_pred_ss = new uint8_t[ni*ns];
    double kscale = 1.0/SCALE;
    for (int i=0; i<ni; i++)
    {
        double score =  i>=di_max         ? INVALID_GRID_SCORE :
                        given_rise_edge>=0 ? -BOUNDARY_GRID_SCORE :
                        0;
        for (int s= 0; s<ns; s++)
        {
            grid_scores[i*ns+s] = score;
            grid_pred_ss[i*ns+s] = ns/2;
        }
    }

    // Forward propagation
    for (int i=0; i<ni; i++)
    {
        float edge_score = interp_lin(m_edf_buf, bufsize, kscale*i);
        float boundary_score = i==SCALE*given_rise_edge? BOUNDARY_GRID_SCORE:0;

        for (int s= 0; s<ns; s++)
            grid_scores[i*ns+s] += edge_score + boundary_score;

        for (int s0 = 0; s0<ns; s0++)
            for (int s1=s0-1; s1<=s0+1; s1++)
                if (s1 >= 0 && s1 < ns && i+di_min + s1<ni)
                {
                    int i1 = i + di_min + s1;
                    int a0 = i*ns+s0;
                    int a1 = i1*ns+s1;
                    if (grid_scores[a1] < grid_scores[a0])
                    {
                        grid_scores[a1] = grid_scores[a0];
                        grid_pred_ss[a1] = s0;
                    }
                }
    }

    //------------------------------------------------
    // Find best end state
    //------------------------------------------------

    int best_i = ni-1;
    int best_s = 0;
    auto best_r = grid_scores[best_i*ns+best_s];
    for (int i= ni-di_max; i<ni; i++)
        for (int s=0; s<ns; s++)
            if (best_r < grid_scores[best_i*ns+best_s])
            {
                best_r = grid_scores[best_i*ns+best_s];
                best_i = i/SCALE;
                best_s = s;
            }

    //------------------------------------------------
    // Backtrace and set grid points
    //------------------------------------------------

    int i = best_i;
    int s = best_s;
    int evt_cnt = 0;
    bool found_given_edge;
    while (i>=0 && i>=SCALE*given_rise_edge)
    {
        int x = i/SCALE;
        assert(x>=0 && x<bufsize);
        assert(evt_cnt < evt_maxcnt);
        evt_xs[evt_cnt++] = x;
        if (i==given_rise_edge)
            found_given_edge = true;
        if (1)
            m_edf_buf[x] = 0.8; // Paint gridpoint
        int sp = grid_pred_ss[i*ns+s];
        i -= di_min + s;
        s = sp;
    }

    delete[] grid_scores;
    delete[] grid_pred_ss;

    //------------------------------------------------------------------------

    // Check that we managed to meet the boundary condition
    if (given_rise_edge>=0 && given_rise_edge<bufsize)
    {
        assert(found_given_edge);
    }

    // The onsets we have picked are in backwards order.
    // Reverse to get them in the expected order.
    for (int i= 0; i<evt_cnt/2; i++)
    {
        int j = evt_cnt-1-i;
        int t = evt_xs[i];
        evt_xs[i] = evt_xs[j];
        evt_xs[j] = t;
    }

    // Debug output
    if (0)
        for (int i= 0; i<core_len; i++)
            dbgbuf[i] = m_edf_buf[margin+i];

    //------------------------------------------------------------------------
    // Discriminate bits
    //------------------------------------------------------------------------

    // NOTE: We'd normally use a viterbi here to constrain pulse length
    // For now just sample bits here from the band buf.

    for (int i=0; i<evt_cnt; i++)
    {
        int x = evt_xs[i];
        evt_vals[i] = x>=0 && x<bufsize && m_band_buf[x]>0;
    }

    //------------------------------------------------------------------------

    // Discard events beyond leftmost rise edge
    // While we're not constraining pulse lengths could be a lot of bits
    int discard_cnt = 0;
    while (discard_cnt < evt_cnt &&
        evt_xs[discard_cnt] != given_rise_edge &&
        !(discard_cnt>0 && evt_vals[discard_cnt] && !evt_vals[discard_cnt-1]))
        discard_cnt++;

    if (discard_cnt)
        for (int i= 0; i<evt_cnt-discard_cnt; i++)
        {
            evt_xs[i]  = evt_xs[i+discard_cnt];
            evt_vals[i] = evt_vals[i+discard_cnt];
        }
    evt_cnt -= discard_cnt;

    // Remove the margin offset from the output coordinates
    // We may return some negative coordinates left of window
    for (int i= 0; i<evt_cnt; i++)
        evt_xs[i] -= margin;

    //------------------------------------------------------------------------

    return evt_cnt;
}
