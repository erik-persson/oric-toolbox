//============================================================================
//
//  DemodDecoder - a demodulation based, slow format only decoder
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#include "DemodDecoder.h"
#include "Demodulator.h"
#include "DecodedByte.h"
#include "DecoderBackend.h"
#include "filters.h"

#include <soundio/Sound.h>

#include <assert.h>
#include <tgmath.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------

template <class T>
static inline T sq(T a)
{
    return a*a;
}

//----------------------------------------------------------------------------

// Viterbi byte segmentation of demodulated signal
// Returns no. of start bits found
static int demod_viterbi(
    int *xs, int maxcnt,       // Locations of start bits (Onsets)
    const float *buf, int len, // Demodulated signal.
    int given_onset,           // -1: no known phase, >=0: force a given onset
    double t_clk,              // Expected clock, nominally samplerate/4800.0
    double dt_clk)             // Half-range of clock search window
{
    // Only detect start/stop bits, ignore data/parity bits
    // +--+--------------------------+--------+
    // |0 |x  x  x  x  x  x  x  x  x |1  1  1 |
    // +--+--------------------------+--------+
    // <A>|<           D            >|<  E   >|
    //
    // A byte is 209 cycles @ 4800Hz.
    // Start and stop are 16 and 49 cycles.
    // Distribute min/max flexibility to add up nicely
    // Minimize relative error in ranges by starting with smallest
    //
    // 'A' is scored as -y (where y is the demodulated signal)
    // 'D' is scored as k_d*abs(y).
    // 'E' is scored y
    //
    // Scoring the 'D' state corrects a problem that the optimization
    // will otherwise squeeze in as many syncs as possible.
    // k_d should be between 0 and 1, 0.6 seems to work well.
    // If k_d is too high (say 1) we get the opposite problem where
    // we tend to avoid syncs.
    //
    double k_d = 0.6;
    double t_clk_min = t_clk - dt_clk;
    double t_clk_max = t_clk + dt_clk;

    int t_a_min = (int) floor(0.5 + 16*t_clk_min);
    int t_a_max = (int) floor(0.5 + 16*t_clk_max);
    int t_e_min = (int) floor(0.5 + 49*t_clk_min);
    int t_e_max = (int) floor(0.5 + 49*t_clk_max);
    int t_d_min = (int) floor(0.5 + 209*t_clk_min - t_a_min - t_e_min);
    int t_d_max = (int) floor(0.5 + 209*t_clk_max - t_a_max - t_e_max);

    static bool printed = false;
    if (!printed && false)
    {
        printf("t_byte_typ = %.2f\n", 209*t_clk);
        printf("t_byte_min = %d\n", t_a_min+t_d_min+t_e_min);
        printf("t_byte_max = %d\n", t_a_max+t_d_max+t_e_max);
        printf("t_a_min    = %d\n", t_a_min);
        printf("t_a_max    = %d\n", t_a_max);
        printf("t_e_min    = %d\n", t_e_min);
        printf("t_e_max    = %d\n", t_e_max);
        printf("t_d_min    = %d\n", t_d_min);
        printf("t_d_max    = %d\n", t_d_max);
        printed = true;
    }

    int ns = t_a_max + t_d_max + t_e_max;

    // Score the initial state against the first signal level
    int s_a = 0;
    int s_d = t_a_max;
    int s_e = t_a_max+t_d_max;
    float scores[ns];
    for (int s=0; s<ns; s+=2)
    {
        float y = buf[0];
        scores[s] =
          s<s_d ? -y :          // start state 'A'
          s<s_e ? k_d*fabs(y) : // data state 'D'
                  y;            // stop state 'E'
    }

    // Force onset if desired
    if (given_onset == 0)
        for (int s= 1; s<ns; s++)
            scores[s] = 1e-20;

    short *pred = new short[len*3];
    pred[0] = 0; // unused
    pred[1] = 0; // unused
    pred[2] = 0; // unused

    // Elasticity - shortcuts from t_min-1..t_max-1 to t_max
    // .--.  .--.  .--.  .--.  .--.  .--.  .--.
    // |  +->|  +->|  +->|  +->|  +->|  +->|  ++>
    // '--'  '--'  '--'  '-+'  '-+'  '-+'  '--'|
    //                     '-----+-----+-------+
    //  0                 t_min-1          t_max-1

    for (int i= 1; i<len; i++)
    {
        // Find best predecessor for each state
        int pred_a = s_e+t_e_max-1;
        int pred_d = s_a+t_a_max-1;
        int pred_e = s_d+t_d_max-1;

        float score_a = scores[pred_a];
        float score_d = scores[pred_d];
        float score_e = scores[pred_e];

        for (int s = s_e+t_e_min-1; s<s_e+t_e_max-1; s++)
            if (score_a < scores[s])
            {
                score_a= scores[s];
                pred_a = s;
            }

        for (int s = s_a+t_a_min-1; s<s_a+t_a_max-1; s++)
            if (score_d < scores[s])
            {
                score_d= scores[s];
                pred_d = s;
            }

        for (int s = s_d+t_d_min-1; s<s_d+t_d_max-1; s++)
            if (score_e < scores[s])
            {
                score_e= scores[s];
                pred_e = s;
            }

        // Save predecessor
        pred[i*3+0] = pred_a;
        pred[i*3+1] = pred_d;
        pred[i*3+2] = pred_e;

        // Level-keeping transitions
        // Roll in from states to the left
        for (int s=ns-1; s>0; s--)
            scores[s] = scores[s-1];
        scores[s_a] = score_a;
        scores[s_d] = score_d;
        scores[s_e] = score_e;

        // Score against local signal
        // Start state 'A' thrives on negative signal
        // Data state 'D' thrives on magnitude, for fair competition
        // Stop state 'E' thrives on positive signal.
        float y = buf[i];
        for (int s = s_a; s<s_d; s++)
            scores[s] -= y;
        for (int s = s_d; s<s_e; s++)
            scores[s] += k_d*fabs(y);
        for (int s = s_e; s<ns; s++)
            scores[s] += y;

        // Force onset if desired
        if (given_onset == i)
            for (int s= 1; s<ns; s++)
                scores[s] = 1e-20;
    }

    // Backtrace
    int s = 0;

    // Find best end state
    float score = scores[s];
    for (int s1=0; s1<ns; s1++)
        if (score < scores[s1])
        {
            score = scores[s1];
            s = s1;
        }

    // Trace back chain of predecessors
    // Note onset of start state
    int cnt = 0;
    for (int i= len-2; i>=0; i--)
    {
        s = s == s_a? pred[(i+1)*3+0] :
            s == s_d? pred[(i+1)*3+1] :
            s == s_e? pred[(i+1)*3+2] :
            s-1; // state with just one predecessor
        if (s==s_a && cnt<maxcnt)
            xs[cnt++] = i;
    }

    // The onsets we have picked are in backwards order.
    // Reverse to get them in the expected order.
    for (int i= 0; i<cnt/2; i++)
    {
        int j = cnt-1-i;
        double t = xs[i];
        xs[i] = xs[j];
        xs[j] = t;
    }

    delete[] pred;
    return cnt;
}

//----------------------------------------------------------------------------
// DemodDecoder methods
//----------------------------------------------------------------------------

DemodDecoder::DemodDecoder(const Sound& src,
                           const DecoderOptions& options) :
    m_demod0(src, 4800, false), // low band
    m_demod1(src, 4800, true),  // high band
    m_options(options)
{
    // Sub sampled sample rate
    int ss_sample_rate = m_demod0.GetSampleRate();

    // Clip interval
    int full_len = m_demod0.GetLength();
    m_start_pos = 0;
    if (options.start >= 0) // start specified?
        m_start_pos = (int) floor(0.5 + options.start*ss_sample_rate);
    m_end_pos = full_len;
    if (options.end >= 0) // end specified?
        m_end_pos = (int) floor(0.5 + options.end*ss_sample_rate);
    if (m_end_pos > full_len)
        m_end_pos = full_len;
    if (m_end_pos < m_start_pos+1)
        m_end_pos = m_start_pos+1; // avoid empty interval for dump len

    // Clock parameters
    m_t_ref = ((double) ss_sample_rate)/options.f_ref; // reference physical bit period
    m_t_clk = m_t_ref;         // center of current search window
    m_dt_min = .07*m_t_ref;    // minimum search window half width
    m_dt_max = .25*m_t_ref;    // maximum search window half width
    m_dt_clk = m_dt_max;       // current search window helf width

    // Main buffer, window length and hop size
    m_windowlen = ((int) floor(0.5 + 10*209*m_t_ref)) & ~3; // 10 nominal byte times
    m_hopsize = m_windowlen/2;
    assert(!(m_hopsize & 1));

    // Start with waveform start as the middle 'm_hopsize' part of the window
    m_window_offs = m_start_pos - m_start_pos%m_hopsize - m_windowlen/2 + m_hopsize/2;
    m_fno = 0;

    m_buf0 = new float[m_windowlen];
    m_buf1 = new float[m_windowlen];
    m_buf = new float[m_windowlen];

    m_onset_bufsize = m_windowlen/4;
    m_onset_buf = new int[m_onset_bufsize];
    m_last_byte_onset = -1;
    m_boundary_byte_onset = -1;

    m_byte_bufsize = m_onset_bufsize;
    m_byte_buf = new DecodedByte[m_byte_bufsize];
    m_byte_cnt = 0;
    m_byte_index = 0;

    // Dump support
    m_dump_snd = 0;
    m_dump_buf = 0;
    if (options.dump)
    {
        int dump_len = m_end_pos-m_start_pos;
        m_dump_snd = new Sound(dump_len, ss_sample_rate);
        m_dump_buf = new float[m_windowlen];
    }
}

//----------------------------------------------------------------------------

DemodDecoder::~DemodDecoder()
{
    delete[] m_buf0;
    delete[] m_buf1;
    delete[] m_buf;
    delete[] m_onset_buf;
    delete[] m_byte_buf;

    if (m_dump_snd)
    {
        const char *dump_file = "dump-demod.wav";
        printf("Writing dump to %s\n", dump_file);
        if (!m_dump_snd->WriteToFile(dump_file))
        {
            fprintf(stderr, "Couldn't write %s\n", dump_file);
            exit(1);
        }
    }
    delete m_dump_snd;
    delete[] m_dump_buf;
}

//----------------------------------------------------------------------------

// Decode one window, return false if there was nothing to decode
bool DemodDecoder::DecodeWindow()
{
    if (m_window_offs>=m_end_pos)
        return false; // nothing to decode

    bool first_window = m_fno==0;
    bool last_window = (m_window_offs+m_hopsize >= m_end_pos);

    // Read demodulated signal
    int skip = 0;
    if (!first_window)
    {
        // Move data left
        skip = m_windowlen-m_hopsize;
        for (int i= 0; i<skip; i++)
            m_buf0[i] = m_buf0[i+m_hopsize];
        for (int i= 0; i<skip; i++)
            m_buf1[i] = m_buf1[i+m_hopsize];
    }

    // Read the low and high bands
    m_demod0.Read(m_window_offs+skip, m_buf0+skip, m_windowlen-skip);
    m_demod1.Read(m_window_offs+skip, m_buf1+skip, m_windowlen-skip);

    // Select band(s) for sync detection
    for (int i= 0; i<m_windowlen; i++)
        if (m_options.band == BAND_LOW)
            m_buf[i] = m_buf0[i]; // low band only
        else if (m_options.band == BAND_HIGH)
            m_buf[i] = m_buf1[i]; // high band only
        else
            m_buf[i] = m_buf0[i] + m_buf1[i]; // 2-band

    // Constrain viterbi to have an onset at m_boundary_byte_onset
    int given_onset = -1;
    if (!first_window &&
        m_boundary_byte_onset >= m_window_offs &&
        m_boundary_byte_onset < m_window_offs+m_windowlen)
        given_onset = m_boundary_byte_onset-m_window_offs;

    // Run viterbi to detect bytes in buffer
    int onset_cnt = demod_viterbi(
        m_onset_buf, m_onset_bufsize,
        m_buf, m_windowlen,
        given_onset,
        m_t_clk, m_dt_clk);

    // Portion of window which we need to convert
    int right_limit = last_window  ? m_windowlen : (m_windowlen+m_hopsize)/2;

    int t_half_byte = (int) float(0.5 + 209*m_t_ref/2);
    double k_time = 1.0/m_demod0.GetSampleRate(); // seconds per demodulated sample
    int perfect_byte_run = 0;

    assert(m_byte_cnt == 0);
    for (int i= 0; i<onset_cnt-1; i++)
    {
        int x0 = m_onset_buf[i];
        int x1 = m_onset_buf[i+1];
        int onset = m_window_offs+x0;

        if (x0 >= right_limit)
            continue; // deal with in next window instead
        if (m_last_byte_onset>=0 && onset-m_last_byte_onset<t_half_byte)
            continue; // too close to last accepted byte
        if (onset<m_start_pos-t_half_byte || onset>m_end_pos)
            continue; // outside user specified scan range

        // Sample bits in both bands
        float levels[2][13]; // [band][bit]
        for (int b= 0; b<13; b++)
        {
            double x = x0 + ((16.0/209)*b + (8.0/209))*(x1-x0);
            levels[0][b] = interp_lin(m_buf0, m_end_pos, x);
            levels[1][b] = interp_lin(m_buf1, m_end_pos, x);
        }

        // Normalize the levels to 0..1 range
        float norm_levels[2][13];
        for (int c=0; c<2; c++)
        {
            float ymin = levels[c][0];
            float ymax = levels[c][0];
            for (int b= 0; b<13; b++)
            {
                ymin = fmin(levels[c][b], ymin);
                ymax = fmax(levels[c][b], ymax);
            }
            for (int b= 0; b<13; b++)
                norm_levels[c][b] = ymax>ymin? (levels[c][b]-ymin)/(ymax-ymin) : 0.5;
        }

        // Mix the two bands
        float mix_levels[13];
        if (m_options.band == BAND_DUAL)
        {
            // Measure noise variance in each of the two bands
            float noise[2];
            for (int c=0; c<2; c++)
            {
                float e = sq( norm_levels[c][0] );
                for (int b=1; b<10; b++)
                    e = e + sq( fmin(norm_levels[c][b], 1 - norm_levels[c][b]) );
                for (int b=10; b<13; b++)
                    e = e + sq(1 - norm_levels[c][b]);
                noise[c] = e;
            }

            // Mix to minimize the resulting noise variance
            float v0 = noise[0], v1 = noise[1];
            float k0 = v0+v1>0 ? v1/(v0+v1) : 0.5;;
            for (int b=0; b<13; b++)
                mix_levels[b] = k0*norm_levels[0][b] + (1-k0)*norm_levels[1][b] - 0.5;
        }
        else
        {
            // Use just the user-selected band
            int csel = m_options.band == BAND_LOW ? 0 : 1;
            for (int b=0; b<13; b++)
                mix_levels[b] = levels[csel][b];
        }

        // Binarize
        int z = 0;
        for (int b=0; b<13; b++)
        {
            int val = mix_levels[b] > 0;
            z |= val << b;
        }

        assert(m_byte_cnt < m_byte_bufsize);
        DecodedByte *b = &m_byte_buf[m_byte_cnt];
        b->time = k_time*onset;
        b->slow = true;
        b->byte = get_data_bits(z);
        b->parity_error = !is_parity_ok(z);
        b->sync_error = !is_sync_ok(z);
        m_byte_cnt++;

        m_last_byte_onset = onset;

        // Tune the sync search window
        if (!b->sync_error && !b->parity_error)
        {
            // Perfect byte: Narrow the search window
            m_t_clk = (15*m_t_clk + (x1-x0)/209.0)/16;
            m_dt_clk = (15*m_dt_clk + m_dt_min)/16;

            if (++perfect_byte_run >= 2)
                // Note a boundary condition for next viterbi window
                m_boundary_byte_onset = onset;
        }
        else
        {
            // Imperfect byte: Widen the search window
            m_t_clk = (15*m_t_clk + m_t_ref)/16;
            m_dt_clk = (15*m_dt_clk + m_dt_max)/16;
            perfect_byte_run = 0;
        }
    }

    // Save data in debug dump
    if (m_dump_snd)
    {
        float maxval = m_buf[0];
        for (int i=0; i<m_windowlen; i++)
        {
            m_dump_buf[i] = m_buf[i];
            maxval = fmax(maxval, m_buf[i]);
        }

        // Draw a spike on every start bit onset
        for (int i= 0; i<onset_cnt; i++)
        {
            int x = m_onset_buf[i];
            if (x>=0 && x<m_windowlen)
                m_dump_buf[x] = 1.5*maxval;
        }

        // Write out core part only
        m_dump_snd->Write(m_window_offs+(m_windowlen-m_hopsize)/2-m_start_pos,
                        m_dump_buf+(m_windowlen-m_hopsize)/2,
                        m_hopsize);
    }

    m_window_offs += m_hopsize;
    m_fno++;
    return true; // success
}

//----------------------------------------------------------------------------

// Main entry point - retreive one byte from tape
// Return true if byte was decoded
// Return false on end of tape
bool DemodDecoder::DecodeByte(DecodedByte *b)
{
    while (m_byte_index == m_byte_cnt) // all read
    {
        m_byte_index = 0;
        m_byte_cnt = 0;
        if (!DecodeWindow())
            return false;
    }

    *b = m_byte_buf[m_byte_index++];
    return true;
}
