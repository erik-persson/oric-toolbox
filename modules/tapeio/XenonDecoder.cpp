//============================================================================
//
//  XenonDecoder - a fast mode decoder inspired by the Xenon-1 tape
//
//  Copyright (c) 2021-2023 Erik Persson
//
//============================================================================

#include "XenonDecoder.h"
#include "DecodedByte.h"
#include "filters.h"

#include <soundio/Sound.h>

#include <assert.h>
#include <stdlib.h>
#include <tgmath.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

//----------------------------------------------------------------------------
// Ternary sign function
//----------------------------------------------------------------------------

template <class T>
static int sign(T x)
{
    return x>0 ?  1 :
           x<0 ? -1 :
                  0 ;
}

//----------------------------------------------------------------------------
// Fuzzy logic greyzone function
//----------------------------------------------------------------------------

// Return a confidence in range 0..1
// Linear mapping where false_bar maps to 0, true_bar maps to 1
// Clipped to 0..1 range
float greyzone(float false_bar, float true_bar, float val)
{
    return fmax(0.f, fmin(1.f, (val-false_bar)/(true_bar-false_bar)));
}

//----------------------------------------------------------------------------
// Center of gravity
//----------------------------------------------------------------------------

// Return location of center of gravity relative to coordinate x
float center_of_gravity(const float *wpif, int len, int x)
{
    int pol = sign(wpif[x]);
    if (!pol)
        return 0;

    // Use only the top 10%
    float thresh = 0.9*fabs(wpif[x]);

    int x0 = x, x1 = x;
    while(x0>0 && pol*wpif[x0-1]>thresh)
        x0--;
    while(x1+1<len && pol*wpif[x1+1]>thresh)
        x1++;

    float sum = 0;
    float wsum = 0;
    for (int i=x0; i <= x1; i++)
    {
        float w = pol*wpif[i] - thresh;
        sum += w*(i-x);
        wsum += w;
    }
    return sum/wsum;
}

//----------------------------------------------------------------------------
// Peak location interpolator
//----------------------------------------------------------------------------

// Return location of peak location relative to coordinate x
float interpolate_peak(const float *wpif, int len, int x)
{
    if (x<=0 || x>=len-1)
        return 0; // need 3-sample window

    double y0 = wpif[x-1];
    double y1 = wpif[x];
    double y2 = wpif[x+1];
    double d1 = .5*(y2-y0);
    double d2 = -y0+2*y1-y2;

    // Divide first derivative by second
    float dx = d2==0 ? 0 : d1/d2;

    return fmax(-0.5, fmin(0.5,dx));
}

//----------------------------------------------------------------------------
// Two-sided peak picker
//----------------------------------------------------------------------------

// Pick both positive and negative peaks
// Generate sequence of alternating positive and negative events
static int pick_all_peaks(
    int8_t *peak_detect,          // Labeling output
    float *peak_xs,               // Coord relative start of buffer
    int max_cnt,                  // Size of peak_xs
    const float *npif, int len)   // Input 'Narrow pulse indication function'
{
    int peak_cnt = 0;
    int needed_pol = -1;
    for (int i=0; i<len; i++)
    {
        int pol = sign(npif[i]);
        peak_detect[i] = 0;

        if (pol == needed_pol && i>0 && i<len-1)
        {
            float ym = pol*npif[i-1];
            float y  = pol*npif[i  ];
            float yp = pol*npif[i+1];
            if (y>ym && y>=yp) // peak
            {
                peak_detect[i] = pol;
                needed_pol = -pol;

                if (peak_cnt < max_cnt)
                {
                    float cog = center_of_gravity(npif, len, i);
                    peak_xs[peak_cnt] = i + cog;
                    peak_cnt++;
                }
            }
        }
    }
    return peak_cnt;
}

//----------------------------------------------------------------------------
// Start bit detection
//----------------------------------------------------------------------------
//
// Scan for sync patterns:
//
// ssssh S+ d0..d7 p sssh S- d0..d7 p sssh S+
// \------/         \------/         \------/
//
// Label start bit candidates using DETECT_MAX scale:
// -DETECT_MAX clear negative
//  0          not a start bit
// +DETECT_MAX clear positive start bit
//
// Use both wide and narrow peak indication functions (WPIF + NPIF) and
// make sure to cope with both of
// * Stretch (clock variation), like in welcome tape
//   - use pulse width as feature
// * Dropout of narrow peaks, like in xenon1 tape
//   - use pulse height as feature

static void detect_start(
    int8_t *start_detect,         // Labeling output
    bool *use_area,               // Reader auto-select
    const float *wpif,            // Input wide peak indication function
    const float *npif, int len,   // Input narrow peak indication function
    DecoderOptions& options,      // User selectable settings
    float t_min,                  // Clock period in samples, lower bound
    float t_max,                  // Clock period in samples, upper bound
    int given_byte_x,             // Index where start is required / known
    bool given_byte_use_area,     // Reader select for the given byte
    float thresh)                 // WPIF threshold to qualify a peak
{
    // Settings
    bool use_hbc = true;  // Set to enable height based classifier
    bool use_wbc = true;  // Set to enable width based classifier
    bool wpif_pos = true; // Choice of position, true works for both readers

    // Threshold to qualify a start bit peak
    float avg_mag = 0;
    for (int i= 0; i<len; i++)
        avg_mag += fabs(npif[i]);
    avg_mag /= len;

    float *peak_xs = new float[len];
    float *peak_ys = new float[len];
    int8_t *peak_detect = new int8_t[len];

    // Pick all peaks, two sided (high or low pulses)
    int peak_cnt = pick_all_peaks(
        peak_detect,      // Labeling output
        peak_xs,
        len,
        npif, len);       // Peak indication function

    for (int j= 0; j<len; j++)
        peak_ys[j] = interp(npif,len,peak_xs[j]);

    for (int i=0; i<len; i++)
    {
        start_detect[i] = 0;
        use_area[i] = 0;
    }

    //---------------------------------------------------------------------------------

    // Distance windows for height based classifier

    int dwin_size = ceil(8*t_max);
    float dwin_14[dwin_size];
    float dwin_17[dwin_size];
    float dwin_38[dwin_size];
    for (int d=0; d<dwin_size; d++)
    {
        dwin_14[d] = fmin( greyzone(1.0*t_min, 1.0*t_max, d),
                           greyzone(4.0*t_max, 4.0*t_min, d) );
        dwin_17[d] = fmin( greyzone(1.0*t_min, 1.0*t_max, d),
                           greyzone(7.0*t_max, 7.0*t_min, d) );
        dwin_38[d] = fmin( greyzone(3.0*t_min, 3.0*t_max, d),
                           greyzone(8.0*t_max, 8.0*t_min, d) );
    }

    //---------------------------------------------------------------------------------

    // Classify the peaks
    for (int j=0; j<peak_cnt; j++)
    {
        // Location of start bit NPIF peak
        int i_npif = floor(0.5 + peak_xs[j]);

        // Reject if either of NPIF or WPIF has wrong sign
        int pol = (j&1) ? 1:-1;
        if (sign(npif[i_npif]) != pol || sign(wpif[i_npif]) != pol)
            continue;

        // Check peak height against average magnitude
        float m = pol*peak_ys[j];
        float common = greyzone(0.2*avg_mag, 0.8*avg_mag, m);

        //--------------------------------------------------------------
        // Height based classifier
        //--------------------------------------------------------------

        float hbc = use_hbc ? 1.0 : 0.0;

        // Home in on WPIF peak, may differ from NPIF peak
        int i_wpif = i_npif;
        while (i_wpif>=0 && pol*wpif[i_wpif-1] > pol*wpif[i_wpif])
            i_wpif--;
        while (i_wpif+1<len && pol*wpif[i_wpif+1] > pol*wpif[i_wpif])
            i_wpif++;

        // Check WPIF peak strength, against threshold with +/-30% grey zone.
        float h = wpif[i_wpif]*pol;
        hbc = greyzone(0.7*thresh, 1.3*thresh, h);

        // Stop bits should be largely quiet, but we must tolerate the
        // half-height opposite-sign sidelobe expected at -1.5.
        // Reject if same-polarity peak found 1..7 clocks before
        // Mark weak if not silent 3..8 clocks before
        for (int d=1; d<dwin_size && i_wpif-d>=0; d++)
        {
            float yd = pol*wpif[i_wpif-d];

            if (dwin_17[d] >= 0.5)
                hbc = fmin(hbc, greyzone(.8*h, .6*h, yd));

            if (dwin_38[d] >= 0.5)
                hbc = fmin(hbc, greyzone(1.2*h, 0.3*h, fabs(yd)));
        }

        // Sidelobe supression
        // Reject if there's a stronger peak 1..4 clocks after
        // since that indicates that we are at a sidelobe
        for (int d=1; d<dwin_size && i_wpif+d<len; d++)
        {
            float md = fabs(wpif[i_wpif+d]);
            if (dwin_14[d] >= 0.5)
                hbc = fmin(hbc, greyzone(1.4*h, 1.2*h, md));
        }

        //--------------------------------------------------------------
        // Width based classifier
        //--------------------------------------------------------------

        // Detect 1110 sequence: 3 short 1 long
        float wbc = 0;

        // First byte can preceeded by silence we must look at the next sync
        int j1 = j>=7 ? j : j+13;

        if (use_wbc && j1>=7 && j1+13+2<peak_cnt)
        {
            wbc = 1.0;

            // For width based detection we must have clear peaks
            float h1 = pol*peak_ys[j1-2];
            float h2 = pol*peak_ys[j1-4];
            float h3 = pol*peak_ys[j1-6];
            wbc = fmin(wbc, greyzone(.3*m, .8*m, h1));
            wbc = fmin(wbc, greyzone(.3*m, .8*m, h2));
            wbc = fmin(wbc, greyzone(.3*m, .8*m, h3));

            // The sequence must plausibly be 9 cycles long
            float w = peak_xs[j1+1]-peak_xs[j1-7];
            wbc = fmin(wbc, greyzone((9-2)*t_min, (9-1)*t_min, w));
            wbc = fmin(wbc, greyzone((9+2)*t_max, (9+1)*t_max, w));

            // Compare adj1acent pulse lengths
            float wm3 = peak_xs[j1-5]-peak_xs[j1-7];
            float wm2 = peak_xs[j1-3]-peak_xs[j1-5];
            float wm1 = peak_xs[j1-1]-peak_xs[j1-3];
            float w0  = peak_xs[j1+1]-peak_xs[j1-1]; // stop bit candidate
            float r0 = 5*(w0 -wm1)/(w0 +wm1);
            float r1 = 5*(wm1-wm2)/(wm1+wm2);
            float r2 = 5*(wm2-wm3)/(wm2+wm3);

            // Length differences must be low, low, positive.
            wbc = fmin(wbc, greyzone(0.2, 0.3, r0));       // positive
            wbc = fmin(wbc, greyzone(0.5, 0.4, fabs(r1))); // low
            wbc = fmin(wbc, greyzone(0.5, 0.4, fabs(r2))); // low

            // Sidelobe suppression
            // Widths between positive peaks
            float wm05 = peak_xs[j1  ]-peak_xs[j1-2];
            float wp05 = peak_xs[j1+2]-peak_xs[j1  ];

            // Other-polarity change measure
            // Margin of 0.1 is empirically tuned on welcome demo+driver
            float rp05 = 5*(wp05-wm05)/(wp05+wm05);
            wbc = fmin(wbc, greyzone(rp05-1.1, rp05-0.1, r0));
        }

        //--------------------------------------------------------------
        // Reader auto-selector
        //--------------------------------------------------------------

        // Check when area_cue reader might be the best to use
        float area_cue_quality = 0.0;
        int bcnt = 11;
        if (j>=7 && j+2*bcnt-1<peak_cnt)
        {
            // Heights must not drop to much
            float h0 = pol*peak_ys[j];
            float hmin = h0;
            float hmax = h0;
            for (int b=1; b<bcnt; b++)
            {
                float h = pol*peak_ys[j+2*b];
                hmin = fmin(hmin, h);
                hmax = fmax(hmax, h);
            }
            area_cue_quality = greyzone(0.2, 0.5, hmin/hmax);

            // The sequence must plausibly be bcnt bits long
            float w = peak_xs[j+2*bcnt-1] - peak_xs[j-1];
            area_cue_quality = fmin(area_cue_quality, greyzone((2*bcnt-2)*t_min, (2*bcnt-1)*t_min, w));
            area_cue_quality = fmin(area_cue_quality, greyzone((3*bcnt+2)*t_max, (3*bcnt+1)*t_max, w));
        }

        //--------------------------------------------------------------
        // Conclusion
        //--------------------------------------------------------------

        // Either the height or width classifier must have accepted
        common = fmin(common, fmax(hbc, wbc)); // hbc or wbc

        int i = wpif_pos ? i_wpif : i_npif;
        if (i>=0 && i<len)
        {
            start_detect[i] =
                pol*(
                    common <= 0.0 ? 0 :
                    common >= 1.0 ? DETECT_MAX :
                    1 + floor((DETECT_MAX-1)*common)
                );

            use_area[i] =
                options.cue == CUE_AREA ? true :
                options.cue == CUE_WIDE ? false:
                area_cue_quality > 0.5;
        }
    }

    if (given_byte_x >= 0 && given_byte_x < len && !start_detect[given_byte_x])
    {
        start_detect[given_byte_x] = DETECT_MAX*sign(npif[given_byte_x]);
        use_area[given_byte_x] = given_byte_use_area;
    }

    delete[] peak_xs;
    delete[] peak_ys;
    delete[] peak_detect;
}

//----------------------------------------------------------------------------
// Quantize - Interpret peak intervals as bit intervals
//----------------------------------------------------------------------------

static void quantize(
    uint16_t *out_z,             // Output: 13-bit code
    float *out_t_clk,            // Output: re-estimated clock period
    float *out_t_byte,           // Output: nominal length
    const float *peak_xs,        // Peak locations relative to start bit
    int peak_cnt,                // No. of peaks excluding start bit
    float t_min,                 // Clock period in samples, lower bound
    float t_max,                 // Clock period in samples, upper bound
    bool debug)                  // Set for debug prints
{
    const int MAX_PEAKS = 12;
    if (peak_cnt>MAX_PEAKS)
        peak_cnt = MAX_PEAKS;

    // Expected clock
    float t_exp = (t_min + t_max)/2;

    // Simple exit for peak_cnt==0 and peak_cnt==1
    if (peak_cnt<2)
    {
        if (peak_cnt==0)
            *out_z = 0x1ffe;
        else
        {
            int b = (int) floor(.5 + .5*(peak_xs[0]/t_exp - 1));
            *out_z = 0x1ffe & ~(1<<b);
        }
        *out_t_clk = t_exp;
        *out_t_byte = 28*t_exp;
        return;
    }

    //---------------------------------------------------------------------------
    // List clock candidates / dividers
    //---------------------------------------------------------------------------

    const int MAX_CLKS = 20;
    float clks[MAX_CLKS];
    int clk_cnt = 0;

    clks[clk_cnt++] = t_min;
    clks[clk_cnt++] = t_exp;
    clks[clk_cnt++] = t_max;

    if (debug)
    {
        printf("quantize: xs={");
        for (int i=0; i<peak_cnt; i++)
            printf(" %.1f", peak_xs[i]);
        printf(" }\n");
    }
    if (debug)
        for (int i=0; i<clk_cnt; i++)
            printf("clks[%d]=%.3f\n", i, clks[i]);

    for (int k=0; k<peak_cnt; k++)
    {
        // Interval from previous peak
        double dx = k==0 ? peak_xs[0] : peak_xs[k]-peak_xs[k-1];
        int db_min = (int) ceil(.5*(dx/t_max - 1));
        int db_max = (int) floor(.5*(dx/t_min - 1));
        for (int db=db_min; db<=db_max && clk_cnt<MAX_CLKS; db++)
        {
            float t =  dx / (2*db + 1);
            if (debug)
                printf("clks[%d]=%.3f k=%d dx=%.1f db=%d\n",clk_cnt,t,k,dx,db);
            clks[clk_cnt++] = t;
        }
    }

    // Sort in ascending order
    std::sort(clks, clks+clk_cnt);

    //------------------------------------------------------------------------
    // Evaluate candidate quantizations
    //------------------------------------------------------------------------

    float k_regul = 1;        // Regularization strength, counted in cycles
    float t_best = t_exp;     // Best clock found so far
    float e_best = 1e20;      // Lowest error found so far
    uint16_t z_best = 0xffff; // Best 13-bit code found so far
    uint16_t z_last = 0xffff;
    for (int i=0; i<clk_cnt; i++)
    {
        // Label peaks according to the clock candidate
        short bs[MAX_PEAKS], cs[MAX_PEAKS];
        bool sync_error = false;
        int fit_cnt = 0;
        if (debug)
            printf("clks[%d]=%.3f bs={", i, clks[i]);
        uint16_t z = 0x1ffe;
        int b=0;
        for (int k=0; k<peak_cnt; k++)
        {
            double dx = k==0 ? peak_xs[0] : peak_xs[k]-peak_xs[k-1];
            int db = (int) floor( .5*dx/clks[i] );
            b += db;
            bs[k] = b;           // bit no (0=start bit)
            cs[k] = 2*b + k + 1; // clock cycle
            z &= ~(1<<b);

            if (debug)
                printf(" %d", bs[k]);
            if (b==10 || b==11)
                sync_error = true;

            if (b <= 12 || fit_cnt<2)
                fit_cnt++;
            // else ignore for fitting purpose
        }

        if (z == z_last)
        {
             if (debug)
                printf(" } same\n");
             continue; // no need to re-evaluate
        }
        z_last = z;

        // Fit clock period to peak intervals
        // Minimize sq( sum (dxs - t_clk*dcs) )
        float sum_dcdx = k_regul*k_regul*t_exp;
        float sum_dcdc = k_regul*k_regul; // regularization
        for (int k=0; k<fit_cnt; k++)
        {
            int dc = k==0 ? cs[0] : cs[k]-cs[k-1];
            float dx = k==0 ? peak_xs[0] : peak_xs[k]-peak_xs[k-1];
            sum_dcdx += dc*dx;
            sum_dcdc += dc*dc;
        }
        float t_fit = sum_dcdx/sum_dcdc;

        // Clip fitted clock to search range
        if (1)
            t_fit = fmax(t_min, fmin(t_max, t_fit));

        float dt_clk = (t_fit-t_exp)*k_regul;
        float e_fit = dt_clk*dt_clk; // regularization
        for (int k=0; k<fit_cnt; k++)
        {
            int dc = k==0 ? cs[0] : cs[k]-cs[k-1];
            float dx = k==0 ? peak_xs[0] : peak_xs[k]-peak_xs[k-1];
            float r  = dx-dc*t_fit;
            e_fit += r*r;
        }

        // Penalize sync error as if moving peak 2cc
        if (sync_error)
            e_fit += 4*t_fit*t_fit;

        if (debug)
            printf(" } t_fit=%.3f e_fit=%.3f\n", t_fit, e_fit);

        if (i == 0 || e_fit<e_best)
        {
            e_best = e_fit;
            t_best = t_fit;
            z_best = z;
        }
    }

    //------------------------------------------------------------------------

    int dp_zero_cnt = 0;
    for (int b=1; b<=9; b++)
        if (!(z_best & (1<<b) ))
            dp_zero_cnt++;

    *out_z = z_best;
    *out_t_clk = t_best;
    *out_t_byte = (28 + dp_zero_cnt) * t_best;

    if (debug)
        printf("t_best=%.3f z_best=%04x t_byte=%.1f\n",t_best,z_best,*out_t_byte);
}

//----------------------------------------------------------------------------
// Byte reader using wide peak locations
//----------------------------------------------------------------------------

// Read one byte starting from given start bit
void read_byte_wide_peak(
    uint16_t *out_z,              // Output: 13-bit code
    int *out_dx,                  // Output: length in samples
    float *out_t_clk,             // Output: re-estimated clock period
    const float *wpif,            // Wide peak indicator function
    int len,                      // length of wpif and start_detect
    int start_x,                  // Sample in middle of start bit
    float t_min,                  // Clock period in samples, lower bound
    float t_max,                  // Clock period in samples, upper bound
    float global_thresh)          // Threshold for accepting wpif peak
{
    // Pick start value
    assert(start_x >= 0 && start_x < len);
    float y0 = wpif[start_x];

    // Check polarity
    int pol = y0>0 ? 1 : -1;

    // Local threshold: Set at 70% of start bit height, since ripple can
    // be found to approach half of the start bit height.
    // Dilute it slightly with the global threshold (20%).
    float local_thresh = .8*.7*fabs(y0) + .2*global_thresh;
    assert(local_thresh>0);

    // Initial clock estimate
    float t_clk = (t_min+t_max)/2;

    bool use_cog = true; // Set to use center of gravity peak positioning
    bool debug = false;   // Set to get debug printouts

    //-----------------------------------------------------------------------
    // Peak picking
    //-----------------------------------------------------------------------

    const int MAX_PEAKS = 13;
    float peak_xs[MAX_PEAKS];
    int peak_cnt = 0;

    float start_cog = use_cog? center_of_gravity(wpif, len, start_x) : 0;

    float x = 0;    // Current coordinate, relative to start bit
    while (x < 38*t_max && peak_cnt < MAX_PEAKS)
    {
        // Look for peak 3 clocks ahead
        int i_min  = start_x + (int) floor(start_cog + x + 2.0*t_clk);
        int i_max  = start_x + (int) ceil (start_cog + x + 4.0*t_clk);
        int i_peak = i_max;
        float y_peak = 0;
        for (int i1 = i_min; i1<=i_max && i1<len; i1++)
        {
            float y = pol*wpif[i1];
            if (y_peak < y)
            {
                y_peak = y;
                i_peak = i1;
            }
        }

        if (y_peak > local_thresh &&
            i_peak != i_max) // must defer peak on end sample to next window
        {
            // '0' symbol
            peak_xs[peak_cnt] = i_peak - start_x;
            if (use_cog)
                peak_xs[peak_cnt] += center_of_gravity(wpif, len, i_peak) - start_cog;
            peak_cnt++;
            x = peak_xs[peak_cnt-1];

            // Update local threshold based on 70% of the approved peak.
            // Keep the global ingredient at 20%
            // Average old threshold and new
            local_thresh = .5*(.8*.7*y_peak + .2*global_thresh) + .5*local_thresh;
        }
        else
        {
            // '1' symbol
            x += 2*t_clk;
        }
    }

    if (debug)
    {
        printf("start_x=%d dxs={%.2f", start_x, peak_xs[0]);
        for (int k= 1; k<peak_cnt; k++)
            printf(" %.2f", peak_xs[k]-peak_xs[k-1]);
        printf("}\n");
    }

    //-----------------------------------------------------------------------
    // Quantization - convert peak locations to bit numbers
    //-----------------------------------------------------------------------

    uint16_t z = 0x1fff;
    float t_byte = 32*t_clk;
    quantize(
        &z,                 // Output: 13-bit code
        &t_clk,             // Output: re-estimated clock period
        &t_byte,            // Output: length of byte, estimate
        peak_xs,
        peak_cnt,
        t_min,              // Clock period in samples, lower bound
        t_max,              // Clock period in samples, upper bound
        debug);

    //-----------------------------------------------------------------------
    // Debug print
    //-----------------------------------------------------------------------

    bool ok = is_sync_ok(z) && is_parity_ok(z);
    if (ok && false)
    {
        printf("read_byte_wide_peak: start_x=%d %s %02x%c z=%04x ok=%d t_clk=%.3f t_out=%.3f\n",
               start_x,
               pol>0?"pos":"neg",
               get_data_bits(z),
               !is_sync_ok(z)   ? '!':
               !is_parity_ok(z) ? '?':
               ' ',
               z,
               ok,
               t_clk,
               *out_t_clk);
    }

    //-----------------------------------------------------------------------

    // Result 13-bit code
    *out_z = z;

    // Length in samples
    *out_dx = (int) floor(0.5 + start_cog + t_byte);

    // Clock period estimate
    *out_t_clk = t_clk;
}

//----------------------------------------------------------------------------
// Byte reader using underside narrow-pulses and area measurement
//----------------------------------------------------------------------------

// Read one byte starting from given start bit
void read_byte_underside(
    uint16_t *out_z,              // Output: 13-bit code
    int *out_dx,                  // Output: length in samples
    float *out_t_clk,             // Output: re-estimated clock period
    const float *lfsig,           // Low pass filtered input signal
    const float *npif,            // Narrow pulse indicator function
    int len,                      // length of wpif and start_detect
    int start_x,                  // Sample in middle of start bit
    float t_min,                  // Clock period in samples, lower bound
    float t_max)                  // Clock period in samples, upper bound
{
    assert(start_x >= 0 && start_x<len);

    float t_clk = (t_min+t_max)/2;
    bool debug = false;

    // Default outputs on peak picking failure
    *out_z = 0;                       // 13-bit code
    *out_dx = (int) floor(32*t_clk);  // Length in samples
    *out_t_clk = t_clk;               // Re-estimated clock period

    //----------------------------------------------------------------
    // Pick dips
    //----------------------------------------------------------------

    int pol = sign(npif[start_x]);

    // No. of extra bits to scan to the left of start bit
    const int nb_left = 3;
    const int dip_max = nb_left + 14;
    float dip_xs[dip_max];
    int dip_cnt = 0;

    // Search back to trench before start bit
    int i = start_x;
    while (i>0 && (sign(npif[i])==pol || pol*npif[i-1] <= pol*npif[i]))
        i--;

    // Then search further to get past nb_left more bits
    for (int k=0; k<nb_left; k++)
    {
        while (i>0 && (sign(npif[i])==-pol || pol*npif[i-1] >= pol*npif[i]))
            i--;
        while (i>0 && (sign(npif[i])==pol || pol*npif[i-1] <= pol*npif[i]))
            i--;
    }

    if (i<=0)
        return; // edge of buffer reached

    while (dip_cnt<dip_max && i+1<len)
    {
        float ym = -pol*npif[i-1];
        float y  = -pol*npif[i  ];
        float yp = -pol*npif[i+1];

        if (y>ym && y>=yp && y>0) // peak on underside
        {
            float dx = interpolate_peak(npif, len, i);
            float x = i - start_x + dx;
            dip_xs[dip_cnt] = x;
            dip_cnt++;

            // hysteresis by skipping to next sign flip
            while (i+1<len && -pol*npif[i+1] > 0)
                i++;
        }
        i++;
    }

    if (dip_cnt<dip_max)
        return; // Too few dips, edge of buffer reached

    //----------------------------------------------------------------------------

    // Pulse width measurement
    float ws[13];
    for (int k=0; k<13; k++)
        ws[k] = dip_xs[nb_left+k+1] - dip_xs[nb_left+k];

    if (debug)
    {
        printf("start_x=%d ws = {", start_x);
        for (int k= 0; k<13; k++)
            printf(" %.1f", ws[k] );
        printf(" } t_clk=%.3f\n", t_clk);
    }

    // Pulse area measurement
    float as[nb_left+13];
    for (int k=0; k<nb_left+13; k++)
    {
        int x0 = start_x + (int) floor(0.5+dip_xs[k]);
        int x1 = start_x + (int) floor(0.5+dip_xs[k+1]);
        assert(x0>=0 && x0<len);
        assert(x1>=x0 && x1<len);

        float bottom = .5*(lfsig[x0] + lfsig[x1]);
        float sum = 0;
        for (int i=x0+1; i<x1; i++)
            sum += lfsig[i] - bottom;
        as[k] = pol*sum;
    }

    if (debug)
    {
        printf("start_x=%d as = {", start_x);
        for (int k= 0; k<nb_left+13; k++)
        {
            if (k==nb_left)
                printf(" |");
            printf(" %.1f", as[k] );
        }
        printf(" }\n");
    }

    // Fit a line through the low-area peaks
    float a_low_line[nb_left+13];
    float a_left  = (as[0]+as[1]+as[2])/3;
    float a_right = (as[nb_left+10]+as[nb_left+11]+as[nb_left+12])/3;
    for (int k=0; k<nb_left+13; k++)
        a_low_line[k] = a_left + (a_right-a_left)*(k-1)/13;

    // Estimate the typical high-low area difference
    // Use the start bit, and if they look reasonable, the two largest areas
    // among the data/parity bits. Zeros come in pairs because of the parity.
    // No gain has been seen for using more than those potential two.
    float das[9];
    for (int k=0; k<9; k++)
        das[k] = as[nb_left+1+k] - a_low_line[nb_left+1+k];
    std::sort(das, das+9);
    float typ_da = as[nb_left] - a_low_line[nb_left];
    if (das[7]+das[8] > typ_da)
        typ_da = (das[7] + das[8] + typ_da)/3;

    //----------------------------------------------------------------------------
    // Change measure
    //----------------------------------------------------------------------------

    // Set to demonstrate use of width in place of area, will perform worse
    // on tapes with stretch
    bool use_width = false;

    // Pulse length change measure
    float chgs[12];
    float kc = 0.5; // Counterweight for the change measure

    if (use_width)
    {
        // Multiplier 5 gives units of clock cycles, since on a transition between
        // pulse lengths 2 and 3, the denominator is 1cc, and the divder is 5cc.
        for (int k= 0; k<12; k++)
            chgs[k] = 5*(ws[k+1]-ws[k])/(ws[k]+ws[k+1]);

        // Counterweight for the change measure: 38% of max change
        // Tuned on welcome driver, xenon-1 and super breakout
        float max_chg = fabs(chgs[0]);
        for (int k=1; k<12; k++)
            max_chg = fmax( max_chg, fabs(chgs[k]) );
        kc = max_chg*.38;
    }
    else
    {
        for (int k= 0; k<12; k++)
        {
            float a0 = as[nb_left+k], a1 = as[nb_left+k+1];

            // NOTE: Removing fmin/fmax here reveals a byte tracking issue in
            //       super advanced breakout where last byte in a file is lost
            chgs[k] = fmax(-1, fmin(1, 3*(a1-a0)/(a1+a0) ));
        }
    }

    if (debug)
    {
        printf("start_x=%d chgs = {", start_x);
        for (int k= 0; k<12; k++)
            printf(" %.2f", chgs[k]);
        printf(" }\n");
    }

    //----------------------------------------------------------------------------
    // Re-estimate the local clock based on the pulse widths
    //----------------------------------------------------------------------------

    float minw = ws[0];
    float maxw = ws[0];
    for (int i=0; i<10; i++)
    {
        minw = fmin(minw, ws[i]);
        maxw = fmax(maxw, ws[i]);
    }

    // ws[10..12] may represent the future, t_min/t_max get to represent the past
    // with the same total weight of 6 cycles.
    t_clk = (3*t_min + 3*t_max + minw + maxw + ws[10] + ws[11] + ws[12])/17;

    //----------------------------------------------------------------------------
    // Viterbi
    //----------------------------------------------------------------------------

    const int nb = 13;
    const int ns = 2;
    const float BAD_SCORE = -1e10;
    float scores[nb*ns];
    uint8_t pred[nb*ns];

    // Start bit is always 0
    scores[0*2+0] = 0;
    scores[0*2+1] = BAD_SCORE;
    pred[0*2+0] = 0;
    pred[0*2+1] = 0;

    // Forward
    for (int b=1; b<nb; b++)
    {
        float a_thresh, long_bonus;
        if (use_width) // Use width
        {
            // Neutral 3.5% range tuned on demo, driver, xenon1 and super
            long_bonus = fmax(0,   ws[b]/t_clk - 2.5 - 0.035)
                       - fmax(0, - ws[b]/t_clk + 2.5 - 0.035);
        }
        else
        {
            a_thresh = a_low_line[nb_left+b] + .5*typ_da;
            long_bonus = (as[nb_left+b] - a_thresh)/(a_thresh/1.5);
        }

        // Rise/fall rewarded when change measure exceeds kc
        float rise_reward = -chgs[b-1] - kc;
        float fall_reward =  chgs[b-1] - kc;

        float score_00 = scores[(b-1)*2+0] + long_bonus;
        float score_11 = scores[(b-1)*2+1] - long_bonus;
        float score_01 = scores[(b-1)*2+0] - long_bonus + rise_reward;
        float score_10 = scores[(b-1)*2+1] + long_bonus + fall_reward;

        scores[b*2+0] = fmax(score_00, score_10);
        scores[b*2+1] = fmax(score_01, score_11);
        pred[b*2+0] = score_00>score_10 ? 0:1;
        pred[b*2+1] = score_01>score_11 ? 0:1;
    }

    // Backtrace
    uint16_t z = 0;
    int b = nb-1;
    int s = scores[b*ns+0] > scores[b*ns+1] ? 0:1;
    while(b>0)
    {
        z |= s<<b;
        s = pred[b*ns + s];
        b--;
    }

    // Add up clock cycles
    int dc = 0;
    float w = 0;
    for (int b=0; b<13; b++)
    {
        w += ws[b];
        dc += 3 - ((z>>b) & 1);
    }

    t_clk = fmax(t_min, fmin(t_max, w/dc));
    w += t_clk;                    // Count the extra half-bit

    *out_z = z;                     // 13-bit code
    *out_dx = (int) floor(0.5+w);   // Length in samples
    *out_t_clk = t_clk;             // Re-estimated clock period
}

//----------------------------------------------------------------------------
// Xenon byte decoder
//----------------------------------------------------------------------------
// Scan for sync patterns in 'wide peak indication function' -
// correlation with wave packet of (-1 1 1 -1) pattern.
//
// ssssh S+ d0..d7 p sssh S- d0..d7 p sssh S+
// \------/         \------/         \------/

static int xenon_decode_bytes(
    int *byte_xs, uint16_t *byte_zs, int maxcnt,   // Event buffer
    float *t_est,                                  // Re-estimated clock
    int8_t *start_detect,                          // Start labelling (output)
    bool *use_area,                                // Reader select (output)
    const float *lfsig,                            // Low pass filtered signal
    const float *wpif, const float *npif, int len, // Pulse indicators
    DecoderOptions& options,                       // User selectable settings
    float t_min, float t_max,                      // Clock range
    int given_byte_x,                              // Location of given byte
    bool given_byte_use_area)                      // Reader for given byte
{
    // Settings
    float t_clk = (t_min+t_max)/2;

    // Default output clock estimate
    *t_est = t_clk;

    // Threshold to qualify a peak
    float thresh = 0;
    for (int i= 0; i<len; i++)
        thresh += fabs(wpif[i]);
    thresh /= len;

    //---------------------------------------------------------------------
    // Label start bit candidates
    //---------------------------------------------------------------------

    detect_start(
        start_detect,        // Labeling output
        use_area,            // Reader selection output
        wpif,                // Input wide peak indication function
        npif, len,           // Input narrow peak indication function
        options,             // User selectable settings
        t_min, t_max,        // Clock period range (samples)
        given_byte_x,        // Index where start is required / known
        given_byte_use_area, // Reader select for the given byte
        thresh);

    //---------------------------------------------------------------------
    // Read bytes from start bit candidates
    //---------------------------------------------------------------------

    int *rd_xs = new int[len];
    uint16_t *rd_dxs = new uint16_t[len];
    float *rd_tcs = new float[len];
    uint16_t *rd_zs = new uint16_t[len];
    int rd_cnt = 0;

    for (int i= 0; i<len; i++)
    {
        int pol = sign(start_detect[i]);
        if (!pol)
            continue;
        assert(pol==1 || pol==-1);

        uint16_t z = 0;
        int dx = 1;
        float tc = t_clk;

        if (use_area[i])
        {
            // Use method which can handle Welcome demo with tape stretch
            read_byte_underside(&z, &dx, &tc, lfsig, npif, len, i, t_min, t_max);
        }
        else
        {
            // Use method which can handle Xenon-1 with loss of high frequencies
            read_byte_wide_peak(&z, &dx, &tc, wpif, len, i, t_min, t_max, thresh);
        }

        if (i+dx > len-1)
            break; // skip byte reaching outside window

        assert(rd_cnt < len);
        rd_xs[rd_cnt] = i;
        rd_dxs[rd_cnt] = dx;
        rd_tcs[rd_cnt] = tc;
        rd_zs[rd_cnt] = z;
        rd_cnt++;
    }

    //---------------------------------------------------------------------
    // Byte track selection
    //---------------------------------------------------------------------

    // This differs from classic Activity Selection in that we must favour
    // bytes that come directly after another byte.
    // A two-state model where chained bytes are rewarded
    const int ns = 2; // States: 0=skip 1=take
    int *scores = new int[len*ns];
    uint8_t *pred_ss = new uint8_t[len*ns];
    int *pred_xs = new int[len*ns];
    int *pred_zs = new int[len*ns];
    float *pred_tcs = new float[len*ns];
    for (int i= 0; i<len*ns; i++)
    {
        scores[i] = 0;
        pred_ss[i] = 0;
        pred_xs[i] = -1;
        pred_zs[i] = 0;
        pred_tcs[i] = t_clk;
    }

    int rd_ix = 0; // Scan position in extrapolated bytes

    // Forward pass
    for (int i=0; i<len; i++)
    {
        // Skipping: Propagate to both states the right.
        for (int s1= 0; s1<ns; s1++)
            if (i+1<len && scores[(i+1)*2+s1]<scores[i*2+0])
            {
                scores [(i+1)*2+s1] = scores [i*2+0];
                pred_ss[(i+1)*2+s1] = pred_ss[i*2+0];
                pred_xs[(i+1)*2+s1] = pred_xs[i*2+0];
                pred_zs[(i+1)*2+s1] = pred_zs[i*2+0];
                pred_tcs[(i+1)*2+s1] = pred_tcs[i*2+0];
            }

        // Award the given byte position
        int given_bonus = given_byte_x == i ? 100000 : 0;

        // Award based on clarity of start bit
        int start_score = abs(start_detect[i]);

        if (rd_ix<rd_cnt && rd_xs[rd_ix]==i) // byte to be taken?
        {
            auto dx = rd_dxs[rd_ix];
            auto z  = rd_zs[rd_ix];
            auto tc = rd_tcs[rd_ix];
            int vanity_bonus = is_sync_ok(z) && is_parity_ok(z);

            // Add local score for taking the byte
            scores[i*2+1] += start_score + 50*vanity_bonus + 50*given_bonus;

            // Jump to where the next byte should be
            // Add up to 50 bonus when chaining to another take
            // Add 15 bonus for polarity flip
            int d_max = (int) floor(0.5 + 4*tc);   // search range on each side
            for (int d=-d_max; d<=d_max; d++)
            {
                int chain_score = 50 - 50*std::abs(d)/(d_max+1);

                int i1 = i+dx+d;
                if (i1>i && i1<len)
                {
                    int polarity_bonus = sign(start_detect[i1]) == -sign(start_detect[i]);
                    for (int s1=0; s1<ns; s1++)
                    {
                        int score = scores[i*2+1] + chain_score*(s1==1) + 15*polarity_bonus;

                        if (scores[i1*2+s1] < score)
                        {
                            scores[i1*2+s1] = score;
                            pred_ss[i1*2+s1] = 1;
                            pred_xs[i1*2+s1] = i;
                            pred_zs[i1*2+s1] = z;
                            pred_tcs[i1*2+s1] = rd_tcs[rd_ix];
                        }
                    }
                }
            }
            rd_ix++;
        }
        else
            scores[i*2+1] = -100000; // nothing to take here
    }

    // Backtrace, with gap filling
    // Find best end state
    int s = 0;
    for (int s1=0; s1<ns; s1++)
        if (scores[(len-1)*ns+s] < scores[(len-1)*ns+s1])
            s = s1;

    int a = (len-1)*ns+s;
    s = pred_ss[a];
    int      x = pred_xs[a];
    uint16_t z = pred_zs[a];
    float tc = pred_tcs[a];
    int byte_cnt = 0;
    int good_byte_cnt = 0;
    float sum_tc = 0;
    while (x >= 0)
    {
        // Pad insertion
        // We clearly don't want a missed byte to cause a displacement
        // of the whole file.
        if (byte_cnt)
        {
            // Does the distance corrrespond to 2 bytes or more?
            // In that case insert equidistant pads
            int last_x = byte_xs[byte_cnt-1];
            int dx = last_x - x;
            int n = (int) floor(0.5 + dx/(32*t_clk));
            while (n >= 2)
            {
                int x_pad = x + (dx*(n-1) + n/2)/n;

                assert (byte_cnt < maxcnt);
                byte_xs[byte_cnt] = x_pad;
                byte_zs[byte_cnt] = 0x1fff; // $ff with sync error
                byte_cnt++;
                n--;
            }
        }

        assert (byte_cnt < maxcnt);
        byte_xs[byte_cnt]= x;
        byte_zs[byte_cnt]= z;
        byte_cnt++;
        if (is_sync_ok(z) && is_parity_ok(z))
        {
            good_byte_cnt++;
            sum_tc += tc;
        }

        int a = x*ns+s;
        s = pred_ss[a];
        x = pred_xs[a];
        z = pred_zs[a];
        tc = pred_tcs[a];
    }

    delete[] scores;
    delete[] pred_ss;
    delete[] pred_xs;
    delete[] pred_zs;
    delete[] pred_tcs;

    if (good_byte_cnt >= 5)
        *t_est = fmax(t_min, fmin(t_max, sum_tc / good_byte_cnt));

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
    if (0)
    {
        // Print all read or padded bytes, mark the selected bytes with *
        int i=0, j=0;
        while (i<rd_cnt || j<byte_cnt)
        {
            int x = 0;
            int z = 0;
            int dx = 0;
            float tc = 0;
            bool selected = false;
            bool padded = false;
            if (i<rd_cnt && !(j<byte_cnt && rd_xs[i]>byte_xs[j]))
            {
                selected = j<byte_cnt && rd_xs[i]==byte_xs[j];
                x = rd_xs[i];
                z = rd_zs[i];
                dx = rd_dxs[i];
                tc = rd_tcs[i];
                i++;
                if (selected)
                    j++;
            }
            else if (j<byte_cnt)
            {
                x = byte_xs[j];
                z = byte_zs[j];
                dx = 0;
                padded = true;
                j++;
            }

            printf("byte track: x=%d..%d tc=%.2f z=%04x %02x%c %s %s\n",
                x,
                x+dx,
                tc,
                z,
                get_data_bits(z),
                !is_sync_ok(z)   ? '!':
                !is_parity_ok(z) ? '?':
                ' ',
                padded      ?"!":
                !selected   ?" ":
                "*",
                start_detect[x] >=  DETECT_MAX     ? "+++++" :
                start_detect[x] >=  3*DETECT_MAX/4 ? "++++ " :
                start_detect[x] >=  2*DETECT_MAX/4 ? "+++  " :
                start_detect[x] >=  1*DETECT_MAX/4 ? "++   " :
                start_detect[x] >=  1              ? "+    " :
                start_detect[x] <= -DETECT_MAX     ? "-----" :
                start_detect[x] <= -3*DETECT_MAX/4 ? "---- " :
                start_detect[x] <= -2*DETECT_MAX/4 ? "---  " :
                start_detect[x] <= -1*DETECT_MAX/4 ? "--   " :
                start_detect[x] <= -1              ? "-    " :
                                                     "     ");
        }
    }

    delete[] rd_xs;
    delete[] rd_dxs;
    delete[] rd_tcs;
    delete[] rd_zs;

    return byte_cnt;
}

//----------------------------------------------------------------------------
// XenonDecoder implementation
//----------------------------------------------------------------------------

XenonDecoder::XenonDecoder(const Sound& src,
                           const DecoderOptions& options) :
    m_lp_filter(
        src,
        // Set a filter length of two reference clock cycles
        ((int) floor(2.0*src.GetSampleRate()/options.f_ref)) | 1
    ),
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

    // Core window / hop size: about 0.217s
    m_hopsize = (int) floor(0.5 + 5*209*m_t_ref);

    // Margin on each side of core window: about 0.0625s,
    m_window_margin = (int) floor(0.5 + 300*m_t_ref);

    m_windowlen = m_hopsize + 2*m_window_margin;

    // Allocate buffers
    m_lp_buf = new float[m_windowlen];
    m_wpif_buf = new float[m_windowlen];
    m_npif_buf = new float[m_windowlen];
    m_start_detect_buf = new int8_t[m_windowlen];
    m_use_area_buf = new bool[m_windowlen];

    // Start with waveform start as the middle 'm_hopsize' part of the window
    m_window_offs = m_start_pos - m_start_pos%m_hopsize - m_window_margin;

    // Dump
    m_dump_snd = 0;
    m_dump_buf = 0;
    if (m_options.dump)
    {
        int dump_len = m_end_pos-m_start_pos;
        m_dump_snd = new Sound(dump_len, m_sample_rate);
    }
    m_dump_buf = new float[m_windowlen];

    // Byte decoder state
    int bufsize = m_windowlen/8;
    m_byte_bufsize = bufsize;
    m_byte_xs = new int[bufsize];
    m_byte_zs = new uint16_t[bufsize];
    m_byte_times = new double[bufsize];
}

//----------------------------------------------------------------------------

XenonDecoder::~XenonDecoder()
{
    delete[] m_lp_buf;
    delete[] m_wpif_buf;
    delete[] m_npif_buf;
    delete[] m_start_detect_buf;
    delete[] m_use_area_buf;

    if (m_dump_snd)
    {
        const char *dump_file = "dump-xenon.wav";
        printf("Writing dump to %s\n", dump_file);
        if (!m_dump_snd->WriteToFile(dump_file))
        {
            fprintf(stderr, "Couldn't write %s\n", dump_file);
            exit(1);
        }
    }
    delete m_dump_snd;
    delete[] m_dump_buf;

    delete[] m_byte_xs;
    delete[] m_byte_zs;
    delete[] m_byte_times;
}

//----------------------------------------------------------------------------

bool XenonDecoder::DecodeWindow()
{
    if (m_window_offs >= m_end_pos)
        return false; // nothing to decode

    bool last_window = (m_window_offs+m_hopsize >= m_end_pos);
    int windowlen = m_windowlen;

    //------------------------------------------------------------------------
    // Low pass
    //------------------------------------------------------------------------

    bool ok = m_lp_filter.Read(m_window_offs, m_lp_buf, windowlen);
    assert(ok);

    //------------------------------------------------------------------------
    // Wide peak indicator function
    //------------------------------------------------------------------------

    for (int i= 0; i<windowlen; i++)
    {
        float x0 = interp_lin(m_lp_buf,windowlen,i-1.5*m_t_clk);
        float x1 = interp_lin(m_lp_buf,windowlen,i-0.5*m_t_clk);
        float x2 = interp_lin(m_lp_buf,windowlen,i+0.5*m_t_clk);
        float x3 = interp_lin(m_lp_buf,windowlen,i+1.5*m_t_clk);
        m_wpif_buf[i] = -x0+x1+x2-x3;
    }

    //------------------------------------------------------------------------
    // Narrow peak indicator function
    //------------------------------------------------------------------------

    for (int i= 0; i<windowlen; i++)
    {
        float x0 = interp_lin(m_lp_buf,windowlen,i-1.0*m_t_clk);
        float x1 = interp_lin(m_lp_buf,windowlen,i+0.0*m_t_clk);
        float x2 = interp_lin(m_lp_buf,windowlen,i+1.0*m_t_clk);
        m_npif_buf[i] = -x0+2*x1-x2;
    }

    //------------------------------------------------------------------------
    // Byte decoding
    //------------------------------------------------------------------------

    int given_byte_x = m_byte_boundary_x>=0 ?
        m_byte_boundary_x-m_window_offs : -1;
    bool given_byte_use_area = m_byte_boundary_use_area;

    float t_est = m_t_clk;
    int byte_evt_cnt = xenon_decode_bytes(
        m_byte_xs, m_byte_zs, m_byte_bufsize,
        &t_est,
        m_start_detect_buf,
        m_use_area_buf,
        m_lp_buf, m_wpif_buf, m_npif_buf, windowlen,
        m_options,
        m_t_clk-m_dt_clk, m_t_clk+m_dt_clk,
        given_byte_x, given_byte_use_area);

    // Add a dummy byte if nothing was decoded
    if (byte_evt_cnt == 0)
    {
        assert(m_byte_bufsize >= 1);
        m_byte_xs[0] = m_byte_bufsize/2;
        m_byte_zs[0] = 0x1fff; // ff with sync error
        byte_evt_cnt = 1;
    }

    //------------------------------------------------------------------------
    // Byte post processing
    //------------------------------------------------------------------------

    // Portion of window which we need to interpret
    int right_limit = last_window ? m_windowlen : m_window_margin + m_hopsize;

    double k_time = 1.0/m_sample_rate; // seconds per balanced sample
    int t_half_byte = (int) float(0.5 + 32*m_t_ref/2);

    int healthy_byte_cnt = 0;

    // Clear range of byte events to be emitted
    m_byte_emit_start = m_byte_emit_end = 0;

    for (int i= 0; i<byte_evt_cnt; i++)
    {
        int x = m_window_offs + m_byte_xs[i]; // Global sample offset

        // Annotate global time
        m_byte_times[i] = k_time*x;

        if (x >= m_window_offs + right_limit)
            continue; // deal with in next window instead
        if (m_byte_last_x>=0 && x-m_byte_last_x<t_half_byte)
            continue; // too close to last accepted byte
        if (x<m_start_pos-t_half_byte || x>m_end_pos)
            continue; // outside user specified scan range

        // Add to range of events to emit bytes for
        if (m_byte_emit_end == 0)
            m_byte_emit_start = i;
        m_byte_emit_end = i+1;

        m_byte_last_x = x;       // sample coordinate

        auto z = m_byte_zs[i];
        if (is_parity_ok(z) && is_sync_ok(z))
        {
            m_byte_boundary_x = x;
            m_byte_boundary_use_area = m_use_area_buf[x-m_window_offs];
            healthy_byte_cnt ++;
        }
    }

    // Detected new clock parameters
    double detected_t_clk = m_t_ref;
    double detected_dt_clk = m_dt_max;
    int emit_cnt = m_byte_emit_end - m_byte_emit_start;
    if (emit_cnt >= 4 &&
        t_est >= m_t_ref-m_dt_max &&
        t_est <= m_t_ref+m_dt_max)
    {
        double health =  ((double) healthy_byte_cnt) / emit_cnt;
        if (health > 0.95)
        {
            detected_t_clk = t_est;
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

    //------------------------------------------------------------------------
    // Epilogue
    //------------------------------------------------------------------------

    // Save data in debug dump
    if (m_dump_snd)
    {
        // Debug output: our wide peak indication function
        // Annotate start bits
        for (int i= 0; i<windowlen; i++)
            m_dump_buf[i] =
                .5*m_start_detect_buf[i]/DETECT_MAX +
                .5*m_npif_buf[i];

        // Write out core part of window oinly
        m_dump_snd->Write(m_window_offs + m_window_margin - m_start_pos,
                          m_dump_buf + m_window_margin,
                          m_hopsize);
    }

    m_window_offs += m_hopsize;
    return true; // success
}

//----------------------------------------------------------------------------

// Main entry point - retreive one byte from tape
// Return true if byte was decoded
// Return false on end of tape
bool XenonDecoder::DecodeByte(DecodedByte *b)
{
    // Range of bytes empty?
    while (m_byte_emit_start == m_byte_emit_end)
    {
        if (!DecodeWindow())
            return false;
    }

    assert(m_byte_emit_start < m_byte_emit_end);

    auto i = m_byte_emit_start;
    auto z = m_byte_zs[i];
    b->time = m_byte_times[i];
    b->slow = false;
    b->byte = get_data_bits(z);
    b->parity_error = !is_parity_ok(z);
    b->sync_error = !is_sync_ok(z);
    m_byte_emit_start++;
    return true;
}
