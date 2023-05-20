//----------------------------------------------------------------------------
//
//  DualDecoder - a two-stage decoder capable of both slow and fast formats
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "DualDecoder.h"
#include "GridBinarizer.h"
#include "SuperBinarizer.h"
#include "PatternBinarizer.h"
#include "DecodedByte.h"
#include "filters.h"
#include "Balancer.h"

#include <soundio/Sound.h>

#include <assert.h>
#include <stdlib.h>
#include <tgmath.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------
// Slow mode binary to byte decoder
//----------------------------------------------------------------------------

// Slow format byte extraction from binarized signal
// Returns no. of bytes found
static int decode_slow_bytes(
    int *byte_xs, uint16_t *byte_zs, int maxcnt,
    const bool *bin_vals, int bin_cnt, // Binary signal
    int given_byte_x)   // <0: no known phase, >=0: force a given byte location
{
    const int NS = 13; // No. of physical bits per byte
    const int BOUNDARY_COST = 1<<30; // cost for violating given_byte_x

    // Forward pass
    bool *bits = new bool[bin_cnt];
    int *costs = new int[bin_cnt*NS]; // [x*NS+s]
    int *preds = new int[bin_cnt*NS];
    for (int x=0; x<bin_cnt; x++)
    {
        // Count 7-15 edges among 16 bit block starting x
        int edge_cnt = 0;
        for (int dx= 0; dx<15 && x+dx+1<bin_cnt; dx++)
            edge_cnt += (bin_vals[x+dx] != bin_vals[x+dx+1]);

        bits[x] = edge_cnt>=11; // bits as they look

        int c0 = edge_cnt-7;  // cost when 0 expected
        int c1 = 15-edge_cnt; // cost when 1 expected

        for (int s=0; s<NS; s++)
        {
            const int k = 3;  // 1=bad 2=ok
            int local_cost =
                s == 0      ? k*c0 :  // 0-bit cost -4..4
                s >= 10     ? k*c1 :  // 1-bit cost 0..4
                edge_cnt<11 ? c0 :  // 0-bit cost -4..4
                              c1;   // 1-bit cost 0..4

            // Cost for not starting on an edge
            if (x>0 && bin_vals[x] == bin_vals[x-1])
                local_cost ++;

            const int JUMP_MIN=14;
            const int JUMP_MAX=18;
            if (x<JUMP_MAX)
            {
                costs[x*NS+s] = local_cost;
                preds[x*NS+s] = x-16;

                if (given_byte_x >= 0)
                    // This is deducted later if given byte is hit
                    costs[x*NS+s] += BOUNDARY_COST;
            }
            else
            {
                int sp = s==0 ? NS-1: s-1;
                int best_xp = x-16;
                int best_cp = costs[best_xp*NS + sp];

                for (int jump = JUMP_MIN; jump <= JUMP_MAX; jump++)
                {
                    int jump_cost = abs(jump-(s==0 ? 17:16));
                    int xp = x-jump;
                    int cp = costs[xp*NS+sp] + jump_cost;
                    if (cp < best_cp)
                    {
                        best_cp = cp;
                        best_xp = xp;
                    }
                }
                costs[x*NS+s] = best_cp + local_cost;
                preds[x*NS+s] = best_xp;
            }
        }

        if (x == given_byte_x)
            costs[x*NS + 0] -= BOUNDARY_COST;
    }

    // Find end state
    assert(bin_cnt > 16);
    int best_x = bin_cnt-8;
    int best_s = 0;
    int best_c = costs[best_x*NS+best_s];
    for (int x = bin_cnt-16; x<bin_cnt; x++)
        for (int s=0; s<NS; s++)
        {
            auto c = costs[x*NS+s];
            if (best_c > c)
            {
                best_c = c;
                best_s = s;
                best_x = x;
            }
        }

    // Track backwards
    uint16_t z = 0;
    int s = best_s;
    int x = best_x;

    int byte_cnt = 0;
    bool have_end_bit = false;
    while (x>=0)
    {
        z <<= 1;
        z |= bits[x];
        z &= 0x1fff;
        //z = ((z<<1) | (bits[x])) & 0x1fff;
        if (s==NS-1)
            have_end_bit = true;
        if (s==0 && have_end_bit)
        {
            // Full byte
            assert(byte_cnt < maxcnt);
            byte_xs[byte_cnt] = x;
            byte_zs[byte_cnt] = z;
            byte_cnt++;
        }

        x = preds[x*NS+s];
        s = s==0 ? NS-1:s-1;
    }

    // The events we have picked are in backwards order.
    // Reverse to get them in the expected order.
    for (int i= 0; i<byte_cnt/2; i++)
    {
        int j = byte_cnt-1-i;

        auto tx = byte_xs[i];
        byte_xs[i] = byte_xs[j];
        byte_xs[j] = tx;

        auto tz = byte_zs[i];
        byte_zs[i] = byte_zs[j];
        byte_zs[j] = tz;
    }

    delete[] bits;
    delete[] costs;
    delete[] preds;
    return byte_cnt;
}

//----------------------------------------------------------------------------
// Fast mode binary to byte decoder
//----------------------------------------------------------------------------

// Mode choice
//#define USE_DFB_ORIG
// #define USE_DFB_PLEN
#define USE_DFB_BARREL

//----------------------------------------------------------------------------
// Fast mode binary to byte decoder, barrel version
//----------------------------------------------------------------------------

static int decode_fast_bytes_barrel(
    int *byte_xs, uint16_t *byte_zs, int maxcnt,
    const bool *bin_vals, int bin_cnt, // Binary signal
    int given_byte_x)   // <0: no known phase, >=0: force a given byte location
{
    const int NS = 108; // 27 instances of 4-state H[H]L[L]

    // Tabulate intrinsic state costs
    int state_costs[NS];

    // Penalize long lead pulses, including half bit 13
    for (int s= 0; s<54; s++)
        state_costs[s] = ((s&3)==1)? 2:0;

    // Penalize long sync tail pulse
    // Reward short start tail pulse
    state_costs[4*0  +3] = -2; // start bit
    state_costs[4*10 +3] =  2; // stop bit
    state_costs[4*11 +3] =  2; // stop bit
    state_costs[4*12 +3] =  2; // stop bit

    // Second polarity has same costs as the first
    for (int s= 0; s<54; s++)
        state_costs[54+s] = state_costs[s];

    uint8_t *preds = new uint8_t[bin_cnt*NS];
    int costs[NS];

    // Initialize cost accumulators
    for (int s= 0; s<NS; s++)
        costs[s] = state_costs[s];

    // Forward pass
    uint8_t *pred_ptr = preds;
    for (int x=0; x<bin_cnt; x++)
    {
        // Costs of two potential predecessors
        int cp0 = costs[NS-2];
        int cp1 = costs[NS-1];

        int *cost_ptr = costs;
        for (int s= 0; s<NS; s+=2)
        {
            int sp0 = s==0 ? NS-2 : s-2;
            int old_c0 = cost_ptr[0]; // save since we overwrite
            int old_c1 = cost_ptr[1];

            *(cost_ptr++) = cp0 <= cp1 ? cp0 : cp1;
            *(cost_ptr++) = old_c0;

            *(pred_ptr++) = cp0 <= cp1 ? sp0 : sp0+1;
            *(pred_ptr++) = s;

            if (s==54-4 || s==108-4)
            {
                // Loop from 54-2 to 54-4
                // Motivated by extra stop bit in name zero termination
                // in Rat splat side2 fast.
                if (cost_ptr[-2] > cost_ptr[0]+1)
                {
                    cost_ptr[-2] = cost_ptr[0]+1;
                    pred_ptr[-2] = s+2;
                }
            }

            cp0 = old_c0;
            cp1 = old_c1;
        }

        // Handle boundary condition
        if (x == given_byte_x)
            for (int s= 0; s<NS; s++)
                costs[s] = s==0 || s==54 ? 0 : (1<<20);

        // Add state costs and signal cost
        int ch = bin_vals[x] ? -2 : 2; // cost of high state
        int cl = -ch;
        for (int s= 0; s<NS; s+=4)
        {
            costs[s+0] += state_costs[s+0] + ch;
            costs[s+1] += state_costs[s+1] + ch;
            costs[s+2] += state_costs[s+2] + cl;
            costs[s+3] += state_costs[s+3] + cl;
        }
    }

    // Find best end state
    int x = bin_cnt-1;
    int s = 0;
    for (int s1=0; s1<NS; s1++)
        if (costs[s1] < costs[s])
            s = s1;

    // Track backwards
    int byte_cnt = 0;
    uint16_t z = 0;
    int prev_byte_x = -1;
    while (x>=0)
    {
        int k = s<54 ? s : s-54;

        // Clear LSB of Z upon long tail pulse
        if ((k&3) == 3) // long tail pulse
            z &= ~1;
/*
        // Clear LSB of Z upon long pulse (head or tail)
        if ((k&1) == 1) // long pulse
            z &= ~1;
*/
        if (k == 0)
        {
            if (prev_byte_x != -1) // whole visible ?
            {
                assert(byte_cnt < maxcnt);
                byte_xs[byte_cnt] = x;
                byte_zs[byte_cnt] = z;
                byte_cnt++;
            }

            prev_byte_x = x;
        }

        // Shift at start of head pulse.
        if ((k&3) == 0)
            z = (((z<<1) & 0x1fff) | 1); // assume LSB 1

        s = preds[x*NS+s];
        x--;
    }

    // The events we have picked are in backwards order.
    // Reverse to get them in the expected order.
    for (int i= 0; i<byte_cnt/2; i++)
    {
        int j = byte_cnt-1-i;

        auto tx = byte_xs[i];
        byte_xs[i] = byte_xs[j];
        byte_xs[j] = tx;

        auto tz = byte_zs[i];
        byte_zs[i] = byte_zs[j];
        byte_zs[j] = tz;
    }

    delete[] preds;
    return byte_cnt;
}

//----------------------------------------------------------------------------
// Fast mode binary to byte decoder, PLEN version
//----------------------------------------------------------------------------

// Fast format byte extraction from binarized signal
// Returns no. of bytes found
static int decode_fast_bytes_plen(
    int *byte_xs, uint16_t *byte_zs, int maxcnt,
    const bool *bin_vals, int bin_cnt, // Binary signal
    int given_byte_x)   // <0: no known phase, >=0: force a given byte location
{
    //+---+---+---+---+---+---+---+---+---+---+---+---+---+-+
    //| 0 |b0 |b1 |b2 |b3 |b4 |b5 |b6 |b7 | p | 1 | 1 | 1 |h|
    //+---+---+---+---+---+---+---+---+---+---+---+---+---+-+
    //
    // Fast 0: .--.     . 208.83 us + 416.67 us
    //         '  '-----'
    // Fast 1: .--.  .    208.83 us + 208.83 us
    //         '  '--'
    // A byte contains 27 edges and can be 28-36 clock periods long.
    // Note that there seem to exist a $00 with extra stop bit in Rat splat
    // that gives a 38 clock periods long byte.
    //
    // Fast mode sequence which introduces the_ultra_side2_fast.wav - bytes 0x16 0x16
    // |100   100 1010100 10100 100 100 100 1010101| 011 011  0101011 01011 011011011 0101010 |
    // |start d0  d1d2d3  d4d5  d6  d7  P   stop   |start d0  d1d2d3  d4d5  d6 d7 P   st      |

    const int INVALID_COST  = 1<<30; // cost where no path found
    const int BOUNDARY_COST = 1<<29; // cost for violating given_byte_x
    const int PAD = 40;

    // Convert to pulse length notation
    uint8_t *pulse_lens = new uint8_t[bin_cnt];
    int *pulse_xs = new int[bin_cnt];
    int pulse_cnt = 0;
    int last_edge_x = -1;
    for (int x= 1; x<bin_cnt; x++)
    {
        if (bin_vals[x] != bin_vals[x-1])
        {
            if (last_edge_x != -1)
            {
                pulse_lens[pulse_cnt] = x-last_edge_x;
                pulse_xs[pulse_cnt] = last_edge_x;
                pulse_cnt++;
            }
            last_edge_x = x;
        }
    }

    // Initialize cost landscape
    int *costs = new int[pulse_cnt+PAD];
    int *preds = new int[pulse_cnt+PAD];
    uint16_t *zs = new uint16_t[pulse_cnt];
    for (int i=0; i<pulse_cnt+PAD; i++)
    {
        costs[i] = i>=27? INVALID_COST:
                   given_byte_x >= 0 ? BOUNDARY_COST :
                   0;
        preds[i] = i-27;
    }

    // Forward cost propagation pass
    for (int i=0; i<pulse_cnt-27; i++)
    {
        // Build 13-bit LSB-first byte representation
        uint16_t z = 0;
        if (i<2)
        {
            for (int k= 0; k<13; k++)
                if (pulse_lens[i+2*k] + pulse_lens[i+2*k+1] < 3)
                    z |= 1<<k;
        }
        else
        {
            // Fast case, just shift in one new bit
            z = zs[i-2] >> 1;
            if (pulse_lens[i+2*12] + pulse_lens[i+2*12+1] < 3)
                z |= 1<<12;
        }
        zs[i] = z;

        // Correlation weights
        //
        // Coefficients have been constructed as follows
        // a. Correlate for sync bits:       [ 1 1] 9*[ 0,0] 7*[-1]
        // b. Mul by 4 and add 1 to first 20 [ 5 5] 9*[ 1,1] 7*[-4] (acheives sum=0)
        // c. Pattern for polarity / phase   [-1 1] 9*[-1,1] 7*[ 0]
        // d. (b+c)/2                        [ 2 3] 9*[ 0,1] 7*[-2]
        // e. Rotate to get two stop bits to the left.
        //    This gives a distinct accuracy improvement.
        //    Otherwise for instance a $c0 byte can be found inside a $00 byte.
        //    This was seen to happen in the rat splat name string null terminator.
        //
        const int w[27] = {
            -2,-2,-2,-2,2,3,                      // 2 stop and 1 start bit
            0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,  // 9 data / parity bits
            -2,-2,-2,                             // 1.5 stop bits
        };

        // Correlate with the 0-sum weight pattern
        int corr = 0;
        for (int j=-4; j<23; j++)
            if (i+j >= 0 && i+j<pulse_cnt)
                corr += pulse_lens[i+j]*w[4+j];

        // Calculate local cost
        int c = -corr;
        if (is_sync_ok(z) && is_parity_ok(z))
            c -= 8;

        if (pulse_xs[i] == given_byte_x)
            c -= BOUNDARY_COST; // award for hitting boundary constraints

        // Add local cost
        costs[i] += c;

        // Propagate costs, step range 23..31
        for (int di = 23; di<=31; di++)
        {
            int i1 = i+di;
            int tc = di==27 ? 0 : 2*abs(di-27) + 4;
            if (costs[i1] > costs[i] + tc)
            {
                costs[i1] = costs[i] + tc;
                preds[i1] = i;
            }
        }
    }

    // Find end state among last 27 locations
    assert(pulse_cnt-27 > 27);
    int best_i = pulse_cnt-27-1;
    auto best_c = costs[best_i];
    for (int i = pulse_cnt-27-27; i<pulse_cnt-27; i++)
    {
        auto c = costs[i];
        if (best_c > c)
        {
            best_c = c;
            best_i = i;
        }
    }

    // Track backwards
    int byte_cnt = 0;
    int i = best_i;
    while (i>=0)
    {
        // Full byte
        assert(byte_cnt < maxcnt);
        byte_xs[byte_cnt] = pulse_xs[i];
        byte_zs[byte_cnt] = zs[i];
        byte_cnt++;

        i = preds[i];
    }

    // The events we have picked are in backwards order.
    // Reverse to get them in the expected order.
    for (int i= 0; i<byte_cnt/2; i++)
    {
        int j = byte_cnt-1-i;

        auto tx = byte_xs[i];
        byte_xs[i] = byte_xs[j];
        byte_xs[j] = tx;

        auto tz = byte_zs[i];
        byte_zs[i] = byte_zs[j];
        byte_zs[j] = tz;
    }

    delete[] pulse_lens;
    delete[] pulse_xs;
    delete[] costs;
    delete[] preds;
    delete[] zs;
    return byte_cnt;
}

//----------------------------------------------------------------------------
// Fast mode binary to byte decoder (Original version)
//----------------------------------------------------------------------------

// Fast format byte extraction from binarized signal
// Returns no. of bytes found
static int decode_fast_bytes_orig(
    int *byte_xs, uint16_t *byte_zs, int maxcnt,
    const bool *bin_vals, int bin_cnt, // Binary signal
    int given_byte_x)   // <0: no known phase, >=0: force a given byte location
{
    //+---+---+---+---+---+---+---+---+---+---+---+---+---+-+
    //| 0 |b0 |b1 |b2 |b3 |b4 |b5 |b6 |b7 | p | 1 | 1 | 1 |h|
    //+---+---+---+---+---+---+---+---+---+---+---+---+---+-+
    //
    // Fast 0: .--.     . 208.83 us + 416.67 us
    //         '  '-----'
    // Fast 1: .--.  .    208.83 us + 208.83 us
    //         '  '--'
    // Fast mode sequence which introduces the_ultra_side2_fast.wav
    // |100   100 1010100 10100 100 100 100 1010101| 011 011  0101011 01011 011011011 0101010 |
    // |start d0  d1d2d3  d4d5  d6  d7  P   stop   |start d0  d1d2d3  d4d5  d6 d7 P   st      |
    //
    // The bytes are 0x16, bit pattern is 0 01101000 0 111
    // There are 5 valid lengths in fast mode:
    //
    // Shortest symbol: 0 11111111 1 111 h, 1*3 + 12*2 + 1 = 28 bins (bin = 208.83 us)
    //                  0 11111100 1 111 h, 3*3 + 10*2 + 1 = 30 bins
    //                  0 11110000 1 111 h, 5*3 +  8*2 + 1 = 32 bins
    //                  0 11000000 1 111 h, 7*3 +  6*2 + 1 = 34 bins
    // Longest symbol:  0 00000000 1 111 h, 9*3 +  4*2 + 1 = 36 bins
    //
    // Sync pattern is  0 01101000 0 111 h, 7*3 +  6*2 + 1 = 34 bins

    // 13 bit states + 1 half bit state, times 2 for polarity = 28
    const int NS = 28; // No of symbols
    const int INVALID_COST  = 1<<30; // cost where no path found
    const int BOUNDARY_COST = 1<<29; // cost for violating given_byte_x
    const int PAD = 4;

    // Have PAD extra time steps to the right to reduce bounds checks
    int *costs = new int[(bin_cnt+PAD)*NS]; // [x*NS+s]
    int *pred_xs = new int[(bin_cnt+PAD)*NS];
    bool *pred_bits = new bool[(bin_cnt+PAD)*NS];

    for (int x=0; x<bin_cnt+PAD; x++)
        for (int s= 0; s<NS; s++)
        {
            costs[NS*x+s] =
                (x<3 ? 2*x : INVALID_COST) +
                (given_byte_x>=0 ? BOUNDARY_COST:0);

            // Pretend everything is a zero bit
            int k = s%14;
            pred_xs[NS*x+s] = k==0 ? x-1:x-3;
            pred_bits[NS*x+s] = false;
        }

    // Detect perfect sync bytes
    bool *pos_syncs = new bool[bin_cnt];
    bool *neg_syncs = new bool[bin_cnt];
    uint64_t sr = 0;
    for (int x=bin_cnt-1; x>=0; x--)
    {
        sr = (sr<<1) | bin_vals[x];
        pos_syncs[x] = ((sr & 0x3ffffffffULL) == 0x2a924a549ULL);
        neg_syncs[x] =((~sr & 0x3ffffffffULL) == 0x2a924a549ULL);
    }

    for (int x=0; x<bin_cnt; x++)
    {
        // Signed version of signal
        int y0 = (bin_vals[x] ? 1:-1);
        int y1 = x+1 >= bin_cnt ? 0 : (bin_vals[x+1] ? 1:-1);
        int y2 = x+2 >= bin_cnt ? 0 : (bin_vals[x+2] ? 1:-1);
        int y3 = x+3 >= bin_cnt ? 0 : (bin_vals[x+3] ? 1:-1);
        //int ym = x-1 <  0       ? 0 : (bin_vals[x-1] ? 1:-1);

        // Matching costs
        int c1 = -2*y0+2*y1;            // 10 pattern (positive polarity 1)
        int c0 = -2*y0-0*y1+2*y2;       // 1x0 pattern (positive polarity 0)
        int c0l = -2*y0-2*y1+2*y2+2*y3; // 1100 pattern (positive overlong 0)

        if (given_byte_x == x)
        {
            costs[NS*x+0] -= BOUNDARY_COST; // positive pol
            costs[NS*x+14] -= BOUNDARY_COST; // negatve pol
        }

        // Boost sync bytes
        if (pos_syncs[x]) costs[NS*x+0] -= 8;
        if (neg_syncs[x]) costs[NS*x+14] -= 8;

        // Try making a 0 (1x0 pattern)
        // Nominally 100 but accepting 110 improves The Ultra
        for (int k=0; k<13; k++) // states that may be 0
        {
            int src= NS*x+k, dst = NS*(x+3) + k+1;
            int sync_cost = k>=10 ? 8 : 0;
            if (costs[dst] > costs[src] + c0 + sync_cost)
            {
                costs[dst] = costs[src] + c0 + sync_cost;
                pred_xs  [dst] = x;
                pred_bits[dst] = false;
            }
            src += 14; // Other polarity
            dst += 14;
            if (costs[dst] > costs[src] - c0 + sync_cost) // flipped sign
            {
                costs[dst] = costs[src] - c0 + sync_cost; // flipped sign
                pred_xs  [dst] = x;
                pred_bits[dst] = false;
            }
        }

        // Try making overlong 0 (1100 pattern)
        for (int k=0; k<13; k++) // states that may be 0
        {
            int src= NS*x+k, dst = NS*(x+4) + k+1;
            int sync_cost = (k>=10 ? 8 : 0) + 1; // +1 penalty for overlong pattern
            if (costs[dst] > costs[src] + c0l + sync_cost)
            {
                costs[dst] = costs[src] + c0l + sync_cost;
                pred_xs  [dst] = x;
                pred_bits[dst] = false;
            }
            src += 14; // Other polarity
            dst += 14;
            if (costs[dst] > costs[src] - c0l + sync_cost) // flipped sign
            {
                costs[dst] = costs[src] - c0l + sync_cost; // flipped sign
                pred_xs  [dst] = x;
                pred_bits[dst] = false;
            }
        }

        // Try making a 1 (10 pattern)
        for (int k=0; k<13; k++) // states that may be 1
        {
            int src= NS*x+k, dst = NS*(x+2) + k+1;
            int sync_cost = k==0 ? 8 : 0;
            if (costs[dst] > costs[src] + c1 + sync_cost)
            {
                costs[dst] = costs[src] + c1 + sync_cost;
                pred_xs  [dst] = x;
                pred_bits[dst] = true;
            }
            src += 14; // Other polarity
            dst += 14;
            if (costs[dst] > costs[src] - c1 + sync_cost) // flipped sign
            {
                costs[dst] = costs[src] - c1 + sync_cost; // flipped sign
                pred_xs  [dst] = x;
                pred_bits[dst] = true;
            }
        }

        // Make half bit
        costs[NS*(x+1) + 0 ] = costs[NS*x + 27] - 2*y0;
        costs[NS*(x+1) + 14] = costs[NS*x + 13] + 2*y0;
        pred_xs[NS*(x+1) + 0] = x;
        pred_xs[NS*(x+1) + 14] = x;
    }

    // Find end state
    assert(bin_cnt > 3);
    int best_x = bin_cnt-2;
    int best_s = 0;
    auto best_c = costs[best_x*NS+best_s];
    for (int x = bin_cnt-3; x<bin_cnt; x++)
        for (int s=0; s<NS; s++)
        {
            auto c = costs[x*NS+s];
            if (best_c > c)
            {
                best_c = c;
                best_s = s;
                best_x = x;
            }
        }

    // Track backwards
    uint16_t z = 0;
    int s = best_s;
    int x = best_x;

    int byte_cnt = 0;
    bool have_end_bit = false;
    bool cur_bit = false;
    while (x>=0)
    {
        z <<= 1;
        z |= cur_bit;
        z &= 0x1fff;
        if (s==13 || s==27)
            have_end_bit = true;

        if ((s==0 || s==14) && have_end_bit)
        {
            // Full byte
            assert(byte_cnt < maxcnt);
            byte_xs[byte_cnt] = x;
            byte_zs[byte_cnt] = z;
            byte_cnt++;
        }

        cur_bit = pred_bits[x*NS+s];
        x = pred_xs[x*NS+s];
        s = s==0 ? NS-1:s-1;
    }

    // The events we have picked are in backwards order.
    // Reverse to get them in the expected order.
    for (int i= 0; i<byte_cnt/2; i++)
    {
        int j = byte_cnt-1-i;

        auto tx = byte_xs[i];
        byte_xs[i] = byte_xs[j];
        byte_xs[j] = tx;

        auto tz = byte_zs[i];
        byte_zs[i] = byte_zs[j];
        byte_zs[j] = tz;
    }

    delete[] costs;
    delete[] pred_xs;
    delete[] pred_bits;
    delete[] pos_syncs;
    delete[] neg_syncs;
    return byte_cnt;
}

//----------------------------------------------------------------------------
// Fast format binary to byte decoder
//----------------------------------------------------------------------------

static int decode_fast_bytes(
    int fdec_id,
    int *byte_xs, uint16_t *byte_zs, int maxcnt,
    const bool *bin_vals, int bin_cnt, // Binary signal
    int given_byte_x)   // <0: no known phase, >=0: force a given byte location
{
    switch (fdec_id)
    {
        case FDEC_ORIG:
            return decode_fast_bytes_orig(
                byte_xs,byte_zs,maxcnt,bin_vals,bin_cnt,given_byte_x);

        case FDEC_PLEN:
            return decode_fast_bytes_plen(
                byte_xs,byte_zs,maxcnt,bin_vals,bin_cnt,given_byte_x);

        case FDEC_BARREL:
            return decode_fast_bytes_barrel(
                byte_xs,byte_zs,maxcnt,bin_vals,bin_cnt,given_byte_x);

        default:
            assert(0);
            return 0;
    }
}

//----------------------------------------------------------------------------
// DualDecoder implementation
//----------------------------------------------------------------------------

DualDecoder::DualDecoder(const Sound& src,
                         const DecoderOptions& options,
                         bool enable_fast,
                         bool enable_slow) :
    m_options(options)
{
    m_sample_rate = src.GetSampleRate();
    int full_len = src.GetLength();

    m_start_pos = 0;
    m_end_pos = full_len;
    if (m_options.start >= 0) // start specified?
        m_start_pos = (int) floor(0.5 + m_options.start*m_sample_rate);
    if (m_options.end >= 0) // end specified?
        m_end_pos = (int) floor(0.5 + m_options.end*m_sample_rate);
    if (m_end_pos > full_len)
        m_end_pos = full_len;
    if (m_end_pos < m_start_pos+1)
        m_end_pos = m_start_pos+1; // avoid empty ingerval for dump len

    // Clocking parameters
    m_t_ref = ((double) m_sample_rate)/options.f_ref;

    // Clock search window half width
    // This can at most be 20% since 2*1.2=3.8 before a 3-period can
    // look the same as a 2-period
    m_dt_max = .20*m_t_ref;  // maximum search window half width
    m_dt_min = .07*m_t_ref;  // minimum search window half width
    m_dt_clk = m_dt_max;
    m_t_clk = m_t_ref;

   if (options.binner == BINNER_GRID)
        m_binarizer = new GridBinarizer(src, m_t_ref);
    else if (options.binner == BINNER_SUPER)
        m_binarizer = new SuperBinarizer(src, m_t_ref);
    else if (options.binner == BINNER_PATTERN)
        m_binarizer = new PatternBinarizer(src, m_t_ref);
    else
        assert(0);

    // Windowlen and hopsize
    m_windowlen = ((int) floor(0.5 + 10*209*m_t_ref)) & ~3; // 10 nominal byte times
    m_hopsize = m_windowlen/2;

    assert(!(m_hopsize & 1));
    // Start with waveform start as the middle 'm_hopsize' part of the window
    m_window_offs = m_start_pos - m_start_pos%m_hopsize - m_windowlen/2 + m_hopsize/2;

    // Bit event buffer
    m_bit_evt_bufsize = m_windowlen/2;
    m_bit_evt_xs = new int[m_bit_evt_bufsize];
    m_bit_evt_vals = new bool[m_bit_evt_bufsize];
    m_bit_evt_cnt = 0;

    // Dump
    m_dump_snd = 0;
    m_dump_buf = 0;
    if (m_options.dump)
    {
        int dump_len = m_end_pos-m_start_pos;
        m_dump_snd = new Sound(dump_len, m_sample_rate);
    }
    m_dump_buf = new float[m_windowlen];

    // [0]=fast [1]=slow
    m_byte_decoders[0].enabled = enable_fast;
    m_byte_decoders[1].enabled = enable_slow;

    // Per decoder state
    for (int slow = 0; slow<2; slow++)
        if (m_byte_decoders[slow].enabled)
        {
            int bufsize = m_windowlen/8;
            m_byte_decoders[slow].bufsize = bufsize;
            m_byte_decoders[slow].xs = new int[bufsize];
            m_byte_decoders[slow].zs = new uint16_t[bufsize];
            m_byte_decoders[slow].times = new double[bufsize];
        }
}

//----------------------------------------------------------------------------

DualDecoder::~DualDecoder()
{
    delete m_binarizer;

    delete[] m_bit_evt_xs;
    delete[] m_bit_evt_vals;

    if (m_dump_snd)
    {
        const char *dump_file = "dump-dual.wav";
        printf("Writing dump to %s\n", dump_file);
        if (!m_dump_snd->WriteToFile(dump_file))
        {
            fprintf(stderr, "Couldn't write %s\n", dump_file);
            exit(1);
        }
    }
    delete m_dump_snd;
    delete[] m_dump_buf;

    for (int slow = 0; slow<2; slow++)
    {
        delete[] m_byte_decoders[slow].xs;
        delete[] m_byte_decoders[slow].zs;
        delete[] m_byte_decoders[slow].times;
    }
}

//----------------------------------------------------------------------------

// Decode bytes from bit window
void DualDecoder::DecodeByteWindow(bool last_window)
{
    // Detected new clock parameters
    double detected_t_clk = m_t_ref;
    double detected_dt_clk = m_dt_max;

    for (int slow = 0; slow<2; slow++)
    {
        ByteDecoder *byte_decoder = &m_byte_decoders[slow];
        if (!byte_decoder->enabled)
            continue; // only run the asked for mode

        // Portion of window which we need to interpret
        int right_limit = last_window ? m_windowlen : (m_windowlen+m_hopsize)/2;

        double k_time = 1.0/m_sample_rate; // seconds per balanced sample

        // Decode from bits to byte
        int byte_evt_cnt;
        if (slow)
            byte_evt_cnt = decode_slow_bytes(
                byte_decoder->xs, byte_decoder->zs, byte_decoder->bufsize,
                m_bit_evt_vals, m_bit_evt_cnt,
                byte_decoder->boundary_x);
        else
            byte_evt_cnt = decode_fast_bytes(m_options.fdec,
                byte_decoder->xs, byte_decoder->zs, byte_decoder->bufsize,
                m_bit_evt_vals, m_bit_evt_cnt,
                byte_decoder->boundary_x);

        int nominal_bins_per_byte = slow ? 209 : 32;
        int t_half_byte = (int) float(0.5 + nominal_bins_per_byte*m_t_ref/2);
        int healthy_byte_cnt = 0;
        int healthy_bit_cnt = 0;
        double healthy_samples = 0;

        // Clear range of byte events to be emitted
        byte_decoder->emit_start = byte_decoder->emit_end = 0;

        for (int i= 0; i<byte_evt_cnt; i++)
        {
            int bix = byte_decoder->xs[i]; // Bit index into bit window
            assert(bix >= 0 && bix < m_bit_evt_cnt);
            int x = m_window_offs + m_bit_evt_xs[bix]; // Global sample offset

            // Annotate global time
            byte_decoder->times[i] = k_time*x;

            if (x >= m_window_offs + right_limit)
                continue; // deal with in next window instead
            if (byte_decoder->last_x>=0 && x-byte_decoder->last_x<t_half_byte)
                continue; // too close to last accepted byte
            if (x<m_start_pos-t_half_byte || x>m_end_pos)
                continue; // outside user specified scan range

            auto z = byte_decoder->zs[i];

            // Add to range of events to emit bytes for
            if (byte_decoder->emit_end == 0)
                byte_decoder->emit_start = i;
            byte_decoder->emit_end = i+1;

            byte_decoder->last_x = x;       // sample coordinate
            if (is_parity_ok(z) && is_sync_ok(z) && i+1<byte_evt_cnt)
            {
                byte_decoder->boundary_x = bix; // bit index

                int bix1 = byte_decoder->xs[i+1];
                healthy_byte_cnt ++;
                if (slow)
                    healthy_bit_cnt += 209;
                else
                {
                    healthy_bit_cnt += 27;
                    for (int i=0; i<13; i++)
                        healthy_bit_cnt += !((z>>i) & 1);
                }
                healthy_samples += m_bit_evt_xs[bix1]-m_bit_evt_xs[bix];
            }
        }

        int emit_cnt = byte_decoder->emit_end - byte_decoder->emit_start;
        double health = emit_cnt==0 ? 0 : ((double) healthy_byte_cnt) / emit_cnt;

        if (health > 0.95)
        {
            detected_t_clk = ((double) healthy_samples)/healthy_bit_cnt;
            detected_dt_clk = m_dt_min;
        }
    }

    // Update clock parameters with exponential decay
    // The coefficients below approximate the 15/16 decay for 5 bytes in DemodDecoder.
    // Narrow or widen the clock search window
    // Improves in general but loses file 4 on The Ultra fast side.
    if (1)
    {
        m_t_clk = 0.75*m_t_clk + 0.25*detected_t_clk;
        m_dt_clk = 0.75*m_dt_clk + 0.25*detected_dt_clk;
    }
}

//----------------------------------------------------------------------------

// Update bit coordinates to new frame of reference on bit window shift
void DualDecoder::AdvanceByteWindow(int advance_bits)
{
    assert(advance_bits >= 0);

    for (int slow = 0; slow<2; slow++)
    {
        ByteDecoder *byte_decoder = &m_byte_decoders[slow];

        // Update byte-level boundary condition to new frame of reference
        byte_decoder->boundary_x -= advance_bits; // ignored when negative
    }
}

//----------------------------------------------------------------------------

bool DualDecoder::DecodeWindow()
{
    if (m_window_offs >= m_end_pos)
        return false; // nothing to decode

    bool last_window = (m_window_offs+m_hopsize >= m_end_pos);

    // Boundary condition, and viterbi skipping, based on old events
    int given_rise_edge = -1;
    if (m_bit_evt_cnt > 0) // old events stashed?
    {
        // Skip portion which we already binarized, gives 25% speedup
        given_rise_edge = m_bit_evt_xs[m_bit_evt_cnt-1];
        assert(given_rise_edge>=0);

        // Viterbi algorithm will output the boundary condition rise edge again
        m_bit_evt_cnt--;
    }

    // By default we offset by 1/4 into the legacy window
    int core_start = m_window_offs + (m_windowlen-m_hopsize)/2;

    // If we have a reasonable boundary condition,
    // then use it as the start.
    if (given_rise_edge >= 0 && given_rise_edge < m_windowlen/2)
        core_start = m_window_offs + given_rise_edge;

    int core_end   = m_window_offs + (m_windowlen+m_hopsize)/2;
    int core_len   = core_end-core_start;
    int old_cnt    = m_bit_evt_cnt;

    if (given_rise_edge>=0)
        given_rise_edge -= (core_start-m_window_offs);

    // Run the binarizer
    // First is a rise event.
    m_bit_evt_cnt += m_binarizer->Read(
        m_bit_evt_xs+old_cnt, m_bit_evt_vals+old_cnt, m_bit_evt_bufsize-old_cnt,
        core_start, core_len,
        m_dump_buf+(core_start-m_window_offs),
        given_rise_edge,
        m_t_clk, m_dt_clk);

    for (int i= old_cnt; i<m_bit_evt_cnt; i++)
        m_bit_evt_xs[i] += (core_start-m_window_offs); // adjust for skipped part of waveform

    DecodeByteWindow(last_window);

    // Save data in debug dump
    if (m_dump_snd)
    {
        if (0) // Draw bits as pulse wave
        {
            for (int i= 0; i<m_bit_evt_cnt-1; i++)
            {
                int x0 = m_bit_evt_xs[i];
                int x1 = m_bit_evt_xs[i+1];
                for (int x=x0; x<x1; x++)
                    if (x>=0 && x<m_windowlen)
                        m_dump_buf[x] = m_bit_evt_vals[i] ? .2 : -.2;
            }
        }

        if (1)
        {
            // Draw spikes on bit onsets
            for (int i= 0; i<m_bit_evt_cnt; i++)
            {
                int x = m_bit_evt_xs[i];
                if (x>=0 && x<m_windowlen)
                {
                    m_dump_buf[x] = m_bit_evt_vals[i] ? .8 : -.8;
                }
            }
        }

        // Is just one of slow and fast enabled?
        if (m_byte_decoders[0].enabled != m_byte_decoders[1].enabled)
        {
            // Then draw spikes on each byte onset
            int dix = m_byte_decoders[1].enabled;
            const auto& d = m_byte_decoders[dix];
            for (int i= d.emit_start; i<d.emit_end; i++)
            {
                int bix = d.xs[i]; // Bit index into bit window
                assert(bix >= 0 && bix < m_bit_evt_cnt);
                int x = m_bit_evt_xs[bix]; // Global sample offset
                if (x>=0 && x<m_windowlen)
                {
                    m_dump_buf[x] = m_bit_evt_vals[bix] ? 1.0 : -1.0;
                }
            }
        }

        // Write out range that we binarized
        m_dump_snd->Write(core_start - m_start_pos,
                        m_dump_buf+(core_start-m_window_offs),
                        core_len);
    }

    int right_limit = last_window ? m_windowlen : (m_windowlen+m_hopsize)/2;
    while (m_bit_evt_cnt && m_bit_evt_xs[m_bit_evt_cnt-1] > right_limit)
        m_bit_evt_cnt--;
    // Make sure last kept event is a rise event

    // Discard events that will be to the left of next window
    // We can delete events to the left regardless of event type
    int delete_left = 0;
    while (delete_left<m_bit_evt_cnt && m_bit_evt_xs[delete_left] < 0)
        delete_left++;

    // Discard bit events that are to the right of the window core
    // They will be analyzed more reliably using the next window
    // However make sure that the last kept event is a rise edge.
    int delete_right = 0;
    for (int i= m_bit_evt_cnt-1; i>=delete_left; i--)
         if (i>0 && !m_bit_evt_vals[i-1] && m_bit_evt_vals[i]) // rise edge at i
             if (m_bit_evt_xs[i] >= right_limit)
                delete_right = m_bit_evt_cnt-1-i;

    int keep_cnt = m_bit_evt_cnt - delete_left - delete_right;
    assert(keep_cnt >= 1);

    // Shift bit events left in buffer
    // Change frame of reference to that of next window
    for (int i= 0; i<keep_cnt; i++)
    {
        m_bit_evt_xs[i] = m_bit_evt_xs[i+delete_left] -= m_hopsize;
        m_bit_evt_vals[i] = m_bit_evt_vals[i+delete_left];
    }
    m_bit_evt_cnt = keep_cnt;

    // Update byte-level boundary condition to new frame of reference
    AdvanceByteWindow(delete_left);

    m_window_offs += m_hopsize;
    return true; // success
}

//----------------------------------------------------------------------------

// Main entry point - retreive one byte from tape
// Produce mixture of slow and fast events when both are enabled
// Return true if byte was decoded
// Return false on end of tape
bool DualDecoder::DecodeByte(DecodedByte *b)
{
    // Range of bytes empty?
    while (m_byte_decoders[0].emit_start == m_byte_decoders[0].emit_end &&
           m_byte_decoders[1].emit_start == m_byte_decoders[1].emit_end)
    {
        if (!DecodeWindow())
            return false;
    }

    bool have_fast = m_byte_decoders[0].emit_start != m_byte_decoders[0].emit_end;
    bool have_slow = m_byte_decoders[1].emit_start != m_byte_decoders[1].emit_end;

    int slow = have_slow ? 1:0;
    if (have_fast && have_slow)
    {
        // Output chronologically when have two types of event
        double t0 = m_byte_decoders[0].times[m_byte_decoders[0].emit_start];
        double t1 = m_byte_decoders[1].times[m_byte_decoders[1].emit_start];
        slow = t1<t0 ? 1:0;
    }

    auto i = m_byte_decoders[slow].emit_start;
    auto z = m_byte_decoders[slow].zs[i];
    b->time = m_byte_decoders[slow].times[i];
    b->slow = slow;
    b->byte = get_data_bits(z);
    b->parity_error = !is_parity_ok(z);
    b->sync_error = !is_sync_ok(z);
    m_byte_decoders[slow].emit_start++;
    return true;
}
