//----------------------------------------------------------------------------
//
//  PatternBinarizer - Viterbi pattern-matching binarizer
//
//  Applicable to both fast and slow formats
//
//  Copyright (c) 2021-2023 Erik Persson
//
//----------------------------------------------------------------------------

#include "PatternBinarizer.h"
//#include "filters.h"

#include <soundio/Sound.h>

#include <assert.h>
#include <stdlib.h>
#include <tgmath.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------

PatternBinarizer::PatternBinarizer(const Sound& src,
                                   const double t_ref) :
    // Balancing filter parameters
    // Set these to 1 in order to disable filters
    m_balancer(
        src,
        ((int) floor(4.5*t_ref)) | 1, // mm_filterlen
        ((int) floor(12.0*t_ref)) | 1 // lp_filterlen
    )
{
}

//----------------------------------------------------------------------------

PatternBinarizer::~PatternBinarizer()
{
    delete[] m_buf;
    delete[] m_abuf;
}

//----------------------------------------------------------------------------
// Main function
//----------------------------------------------------------------------------

// Viterbi physical bit segmentation of demodulated signal
// Returns no. of physical bits found
int PatternBinarizer::Read(
    int *evt_xs,          // Locations of events. First one is rising edge
    bool *evt_vals,       // Value transitioned to (or sustained)
    int evt_maxcnt,       // Max no of events to detect
    int core_start,       // Offset in samples to region of interest
    int core_len,         // Length in samples of region of interest
    float *dbgbuf,        // Debug output buffer [core_len]
    int given_rise_edge,  // -1: no known phase, >=0: force a given rise edge
    double t_clk,         // Expected clock, nominally samplerate/4800.0
    double dt_clk)        // Half-range of clock search window
{
    // Margin on each side of core window
    // This is about 0.05s, 2400 samples in case of 44.1 kHz,
    // Can be compared to a slow byte which is 1920 samles.
    int left_margin = 24*GetSampleRate()/441;
    int right_margin = left_margin;

    // Disable left margin when we have a given rise edge
    // Gives 10-25% speedup
    if (given_rise_edge >= 0)
        left_margin = 0;

    int bufsize = left_margin + core_len + right_margin;

    // Allocate buffers
    if (m_bufsize < bufsize)
    {
        delete[] m_buf;
        delete[] m_abuf;
        m_buf = new float[bufsize];
        m_abuf = new float[bufsize];
        m_bufsize = bufsize;
        m_loaded_start = 0;
        m_loaded_end = 0; // nothing loaded in buffers
    }

    //--------------------------------------------------
    // Load buffers
    //--------------------------------------------------

    // Eliminate overlapping reads
    int window_offs = core_start-left_margin;
    int overlap = 0; // amount of overlap
    if (m_loaded_start< window_offs &&
        m_loaded_end  > window_offs) // old overlaps our start
    {
        int hop = window_offs-m_loaded_start;
        if (hop > 0 && hop < bufsize)
        {
            // Move overlapping data left
            overlap = m_loaded_end - window_offs;
            assert(overlap>0);
            if (overlap>bufsize-1)
                overlap = bufsize-1;
            for (int i= 0; i<overlap; i++)
                m_buf[i] = m_buf[i+hop];
            for (int i= 0; i<overlap; i++)
                m_abuf[i] = m_abuf[i+hop];
        }
    }

    // Read balanced signal
    m_balancer.Read(
        window_offs+overlap, m_buf+overlap, m_abuf+overlap, bufsize-overlap);

    // Note what we loaded so we can reuse overlap
    m_loaded_start = window_offs;
    m_loaded_end = window_offs + bufsize;

    // Adjust given_rise_edge to by margin
    if (given_rise_edge >= 0)
        given_rise_edge += left_margin;

    //--------------------------------------------------
    // Viterbi binarizer
    //--------------------------------------------------

    int t_clk_min = (int) floor(0.5 + t_clk - dt_clk);
    int t_clk_max = (int) floor(0.5 + t_clk + dt_clk);

    // State encoding "RHFL" - Rise High Fall low
    // R states:           0 ..1*tclk_max-1
    // H states: 1*tclk_max .. 2*tclk_max-1
    // F states: 2*tclk_max .. 3*tclk_max-1
    // L states: 3*tclk_max .. 4*tclk_max-1
    //  .-----------------------------------------------------------------------.
    //  |   .-------.---.    .-------.---.    .-------.---.    .-------.---.    |
    //  '-->| R     |   +-+->| H     |   +-+->| F     |   +-+->| L     |   +-+--'
    //      '-------'---' |  '-------'---' |  '-------'---' |  '-------'---' |
    //                    '----------------'                '----------------'
    //           _-------     ------------     --_
    //         _-                                 -_
    //       --                                     ------     -------------

    int ns = 4*t_clk_max;
    int s_r = 0;
    int s_h = 1*t_clk_max;
    int s_f = 2*t_clk_max;
    int s_l = 3*t_clk_max;

    // We want a state where pattern is zero so we have a well defined
    // zero crossing location. This is good for splicing sequences.
    // Angle is k*(i+1) so when i is tslope/2-1, we get pattern=-cos(PI/2)=0
    int t_slope = t_clk_min + (t_clk_min&1); // use even number
    int s_trig_r = s_r + t_slope/2-1; // State which rises through 0
    int s_trig_h = s_h + t_slope/2-1; // State where a sustained 1 is detected
    int s_trig_f = s_f + t_slope/2-1; // State which falls through 0
    int s_trig_l = s_l + t_slope/2-1; // State where a sustained 0 is detected

    float pattern[ns];
    float k = M_PI/t_slope;
    for (int i=0; i<t_slope; i++)
        pattern[i] = -cos(k*(i+1)); // rise
    for (int i=t_slope; i<2*t_clk_max; i++)
        pattern[i] = 1.0; // high
    for (int i=0; i<2*t_clk_max; i++)
        pattern[2*t_clk_max + i] = -pattern[i]; // fall, low

    // Allocate a movable "scrollable" cost vector
    int scroll_margin = ns>64 ? ns:64;
    float cost_storage[ns + scroll_margin];
    float *costs = cost_storage + scroll_margin;

    // Set initial costs
    for (int s=0; s<ns; s++)
        costs[s] = fabs( m_buf[0] - pattern[s]*m_abuf[0] );

    // Force a rise edge if requested
    if (given_rise_edge == 0)
        for (int s= 0; s<ns; s++)
            costs[s] = s==s_trig_r ? 0 : 1e20;

    short *pred = new short[bufsize*4];
    pred[0] = 0; // unused
    pred[1] = 0; // unused
    pred[2] = 0; // unused
    pred[3] = 0; // unused

    for (int i= 1; i<bufsize; i++)
    {
        // Find best predecessor for each state
        int p;
        float c;

        // Find best predecessor of H
        p = s_r + t_clk_max-1;
        c = costs[p];
        for (int s = s_r+t_clk_min-1; s<s_r+t_clk_max-1; s++)
            if (c > costs[s])
            {
                c= costs[s];
                p = s;
            }
        pred[i*4+1] = p;
        float c_h = c;

        // Find best predecessor of F
        // This might be H or H's predecessor R
        // Start with p,c kept from H's predececessor R
        for (int s = s_h+t_clk_min-1; s<s_h+t_clk_max; s++)
            if (c > costs[s])
            {
                c= costs[s];
                p = s;
            }
        pred[i*4+2] = p;
        float c_f = c;

        // Find best predecessor of L
        p = s_f + t_clk_max-1;
        c = costs[p];
        for (int s = s_f+t_clk_min-1; s<s_f+t_clk_max-1; s++)
            if (c > costs[s])
            {
                c= costs[s];
                p = s;
            }
        pred[i*4+3] = p;
        float c_l = c;

        // Find best predecessor of R
        // This might be L or L's predecessor F
        // Start with p,c kept from L's predececessor F
        for (int s = s_l+t_clk_min-1; s<s_l+t_clk_max; s++)
            if (c > costs[s])
            {
                c= costs[s];
                p = s;
            }
        pred[i*4+0] = p;
        float c_r = c;

        // Move costs one step down (to higher index)
        if (costs != cost_storage)
            // Fast case: Move elements down by moving base pointer up
            costs--;
        else
        {
            // Slow case: Place array at cost_storage+scroll_margin
            //            Copy old data to offset 1 in the new storage
            memcpy(cost_storage+scroll_margin+1, cost_storage, (ns-1)*sizeof(costs[0]));
            costs = cost_storage + scroll_margin;
        }

        costs[s_r] = c_r;
        costs[s_h] = c_h;
        costs[s_f] = c_f;
        costs[s_l] = c_l;

        // Score local signal against pattern
        // First 2*t_clk_max states are mirrored by the later 2*clk_max states
        float amp = m_abuf[i], sig = m_buf[i];
        for (int s=0; s<t_slope; s++)
        {
            float p = pattern[s]*amp; // rise curve
            costs[s] += fabs(sig-p);
            costs[2*t_clk_max + s] += fabs(sig+p); // flipped
        }
        float dh = fabs(sig-amp); // cost of high plateau
        float dl = fabs(sig+amp); // cost of low plateau
        for (int s=t_slope; s<2*t_clk_max; s++)
        {
            costs[s] += dh;
            costs[2*t_clk_max + s] += dl;
        }

        // Force a rise edge if requested
        if (given_rise_edge == i)
            for (int s= 0; s<ns; s++)
                costs[s] = s==s_trig_r ? 0 : 1e20;
    }

    // Backtrace
    int s = 0;

    // Find best end state
    float c = costs[s];
    for (int s1=0; s1<ns; s1++)
        if (c > costs[s1])
        {
            c = costs[s1];
            s = s1;
        }

    // Reconstruct signal there
    int x = bufsize-1-left_margin;
    if (x >= 0 && x < core_len)
//        dbgbuf[x] = pattern[s]*m_abuf[bufsize-1];
      dbgbuf[x] = m_buf[bufsize-1];

    // Trace back chain of predecessors
    // Note onset of start state
    int evt_cnt = 0;
    int last_rise = -1;
    for (int i=bufsize-2; i>=0 && i>=given_rise_edge; i--)
    {
        s = s == s_r? pred[(i+1)*4+0] :
            s == s_h? pred[(i+1)*4+1] :
            s == s_f? pred[(i+1)*4+2] :
            s == s_l? pred[(i+1)*4+3] :
            s-1; // state with just one predecessor

        // Reconstruct signal there
        if (i-left_margin >= 0 && i-left_margin < core_len)
            dbgbuf[i-left_margin] = pattern[s]*m_abuf[i];

        if ((s==s_trig_r || s==s_trig_h || s==s_trig_f || s==s_trig_l) && evt_cnt<evt_maxcnt)
        {
            if (s==s_trig_r)
                last_rise = evt_cnt;
            evt_vals[evt_cnt] = (s==s_trig_r || s==s_trig_h);
            evt_xs[evt_cnt++] = i;
        }
    }

    // Discard events beyond leftmost rise edge
    evt_cnt = last_rise+1;

    // The onsets we have picked are in backwards order.
    // Reverse to get them in the expected order.
    for (int i= 0; i<evt_cnt/2; i++)
    {
        int j = evt_cnt-1-i;
        int t = evt_xs[i];
        evt_xs[i] = evt_xs[j];
        evt_xs[j] = t;
        bool tt = evt_vals[i];
        evt_vals[i] = evt_vals[j];
        evt_vals[j] = tt;
    }

    // Make the output coordinates relative to core_start
    for (int i= 0; i<evt_cnt; i++)
        evt_xs[i] -= left_margin;

    delete[] pred;
    return evt_cnt;
}
