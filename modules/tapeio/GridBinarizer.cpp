//----------------------------------------------------------------------------
//
//  GridBinarizer - alternative binarizer
//
//  Copyright (c) 2021-2023 Erik Persson
//
//----------------------------------------------------------------------------

#include "GridBinarizer.h"
#include "filters.h"

#include <soundio/Sound.h>

#include <assert.h>
#include <stdlib.h>
#include <tgmath.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------

GridBinarizer::GridBinarizer(const Sound& src,
                             double t_ref) :
    m_lowpass(
        src,
        ((int) floor(2.0*t_ref)) | 1 // lp_filterlen
    )
{
}

//----------------------------------------------------------------------------

GridBinarizer::~GridBinarizer()
{
    delete[] m_lpbuf;
    delete[] m_edfbuf;
}

//----------------------------------------------------------------------------
// Main function
//----------------------------------------------------------------------------

int GridBinarizer::Read(
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

    // Allocate buffers
    if (m_bufsize < bufsize)
    {
        delete[] m_lpbuf;
        delete[] m_edfbuf;
        m_lpbuf = new float[bufsize];
        m_edfbuf = new float[bufsize];
        m_bufsize = bufsize;
    }

    bool ok = m_lowpass.Read(core_start-margin, m_lpbuf, bufsize);
    if (!ok)
        return 0;

    // Debug output: Edge detection function
    for (int i= 0; i<core_len; i++)
        dbgbuf[i] = m_lpbuf[margin+i];

    if (given_rise_edge >= 0)
        given_rise_edge += margin;

    int t_clk_min = (int) floor(0.5 + t_clk - dt_clk);
    int t_clk_max = (int) floor(0.5 + t_clk + dt_clk);
    int t_clk_typ = (int) floor(0.5 + t_clk);

    //------------------------------------------------
    // Pass 1: Edge detection finction
    //------------------------------------------------

    for (int i=0; i<bufsize; i++)
    {
        // Sample 4 bits
        float y0 = interp_lin(m_lpbuf, bufsize, i-1.5*t_clk);
        float y1 = interp_lin(m_lpbuf, bufsize, i-0.5*t_clk);
        float y2 = interp_lin(m_lpbuf, bufsize, i+0.5*t_clk);
        float y3 = interp_lin(m_lpbuf, bufsize, i+1.5*t_clk);

        // Form wave packet correlates
        float c0010 = -.25*y0 -.25*y1 +.75*y2 -.25*y3;
        float c0011 =  -.5*y0  -.5*y1  +.5*y2  +.5*y3;
        float c0100 = -.25*y0 +.75*y1 -.25*y2 -.25*y3;
        float c0101 =  -.5*y0  +.5*y1  -.5*y2  +.5*y3;
        float c0110 =  -.5*y0  +.5*y1  +.5*y2  -.5*y3;

        // Another variant
        float edge =   .25*y0  -.75*y1 +.75*y2 -.25*y3;
        // Form edge detection function
        if (0)
            // Looks nice at first glance but has more 1.5
            // long periods in 100 patterns.
            m_edfbuf[i] = abs(edge);
        else
            // Den här får mindre fasfel och bara 1.0 långa perioder
            m_edfbuf[i] =
                sqrt(c0010*c0010 +
                     c0011*c0011 +
                     c0100*c0100 +
                     c0101*c0101 +
                     c0110*c0110);
    }

    //------------------------------------------------
    // Enhance the edge detection function
    //------------------------------------------------

    // Subtract two surrounding values to get zero-average signal
    // This is necessary for the peak picking maximization to make
    // sense, it will not work for regions of constant sign.
    if (1)
    {
        float *edfbuf2 = new float[bufsize];
        for (int i= 0; i<bufsize; i++)
        {
            edfbuf2[i] =
                -.5*interp_lin(m_edfbuf,bufsize,i-.5*t_clk) +
                m_edfbuf[i] +
                -.5*interp_lin(m_edfbuf,bufsize,i+.5*t_clk);
        }
        for (int i= 0; i<bufsize; i++)
            m_edfbuf[i] = edfbuf2[i];
        delete[] edfbuf2;
    }

    // Periodic averaging with the expected clock
    if (1)
    {
        float *edfbuf2 = new float[bufsize];
        for (int i= 0; i<bufsize; i++)
        {
            edfbuf2[i] =
                (
                .5*interp_lin(m_edfbuf,bufsize,i-3*t_clk) +
                interp_lin(m_edfbuf,bufsize,i-2*t_clk) +
                interp_lin(m_edfbuf,bufsize,i-t_clk) +
                m_edfbuf[i] +
                interp_lin(m_edfbuf,bufsize,i+t_clk) +
                interp_lin(m_edfbuf,bufsize,i+2*t_clk) +
                .5*interp_lin(m_edfbuf,bufsize,i+3*t_clk)
                ) /6;
        }
        for (int i= 0; i<bufsize; i++)
            m_edfbuf[i] = edfbuf2[i];
        delete[] edfbuf2;
    }

    //------------------------------------------------
    // Pass 2: Grid extraction
    //------------------------------------------------

    const float INVALID_GRID_SCORE = -1e20;
    const float BOUNDARY_GRID_SCORE = 1e10;

    float *grid_scores = new float[bufsize];
    int *grid_pred = new int[bufsize];

    for (int i=0; i<bufsize; i++)
    {
        grid_scores[i] = i>=t_clk_max       ? INVALID_GRID_SCORE :
                         given_rise_edge>=0 ? -BOUNDARY_GRID_SCORE :
                                              0;
        grid_pred[i] = i-t_clk_typ;
    }

    // Forward propagation
    for (int i=0; i<bufsize; i++)
    {
        grid_scores[i] += m_edfbuf[i];
        if (i==given_rise_edge)
            grid_scores[i] += BOUNDARY_GRID_SCORE;

        for (int i1 = i+t_clk_min; i1 <= i+t_clk_max && i1<bufsize; i1++)
            if (grid_scores[i1] < grid_scores[i])
            {
                grid_scores[i1] = grid_scores[i];
                grid_pred[i1] = i;
            }
    }

    // Find best end state
    int best_x = bufsize-1;
    auto best_r = grid_scores[best_x];
    for (int x= bufsize-t_clk_max; x<bufsize; x++)
        if (best_r < grid_scores[x])
        {
            best_r = grid_scores[x];
            best_x = x;
        }

    // Backtrace and set grid points
    int x = best_x;
    int evt_cnt = 0;
    bool found_given_edge;
    while (x>=0 && x>=given_rise_edge)
    {
        assert(evt_cnt < evt_maxcnt);
        evt_xs[evt_cnt++] = x;
        if (x==given_rise_edge)
            found_given_edge = true;
        if (1)
            m_edfbuf[x] = 0.8; // Paint gridpoint
        x = grid_pred[x];
    }

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

    //------------------------------------------------------------------------
    // Pass 3: Discriminate bits
    //------------------------------------------------------------------------

    // NOTE: We'd normally use a viterbi here to constrain pulse length
    // For now just interpret each bit on its own.

    for (int i=0; i<evt_cnt; i++)
    {
        float x0 = i>0 ? evt_xs[i-1] : evt_xs[i] - t_clk; // onset of previous bit
        float x1 = evt_xs[i];
        float x2 = i+1<evt_cnt ? evt_xs[i+1] : x1 + t_clk;
        float x3 = i+2<evt_cnt ? evt_xs[i+2] : x2 + t_clk;

        float y0 = interp(m_lpbuf, bufsize, (x0+x1)/2);
        float y1 = interp(m_lpbuf, bufsize, (x1+x2)/2);
        float y2 = interp(m_lpbuf, bufsize, (x2+x3)/2);

        evt_vals[i] = 2*y1 > y0+y2;
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

    delete[] grid_scores;
    delete[] grid_pred;

    return evt_cnt;
}
