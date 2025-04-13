//============================================================================
//
//  Signal processing filters
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#include "filters.h"
#include <tgmath.h>
#include <assert.h>

//----------------------------------------------------------------------------
// Cubic interpolation
//----------------------------------------------------------------------------

float interp(const float *vals, int cnt, float x)
{
    int x0= (int) floor(x);
    float frac= x-x0;
    float y0= (x0-1>=0 && x0-1<cnt)? vals[x0-1]:0;
    float y1= (x0  >=0 && x0  <cnt)? vals[x0  ]:0;
    float y2= (x0+1>=0 && x0+1<cnt)? vals[x0+1]:0;
    float y3= (x0+2>=0 && x0+2<cnt)? vals[x0+2]:0;
    return y1 + frac*(y2-y0 + frac*(2*y0-5*y1+4*y2-y3 + frac*(-y0+3*y1-3*y2+y3)))/2;
}

//----------------------------------------------------------------------------
// Running min filter
//----------------------------------------------------------------------------

void running_min(float *dst, int dstlen, const float *src, int srclen, int filterlen)
{
    assert(filterlen > 0);
    assert(dstlen == srclen-filterlen+1); // For now, exact sizes

    // i  i  i  i  i  i  i  i  i  i  i   input
    // l  l  s  l  l  s  l  l  s
    //    l  s     l  s     l  s
    // .  .  s  .  .  s  .  .  s
    //       s  r     s  r     s  r
    //       s  r  r  s  r  r  s  r  r
    //    o  o  o  o  o  o  o  o  o       dstput

    // Avoid special cases by aligning to a multiple of the filter length
    while (dstlen % filterlen)
    {
        // Trivial algorithm for edge case
        float acc = src[0];
        for (int j=1; j<filterlen; j++)
            acc = fmin(acc, src[j]);
        dst[0] = acc;

        dst++;
        src++;
        dstlen--;
        srclen--;
    }

    assert(dstlen % filterlen == 0);

    for (int i=0; i<dstlen; i+=filterlen)
    {
        // Starting element
        float acc = src[i+filterlen-1];
        dst[i+filterlen-1] = acc;      // Partial result

        // Left sweep
        for (int j= filterlen-2; j>=0; j--)
        {
            acc = fmin(acc, src[i+j]);  // Combine in 'l' element
            dst[i+j] = acc;            // Partial result in case of j>0
        }

        // Right sweep
        acc = src[i+filterlen-1]; // Use starting element again

        for (int j=1; j<filterlen; j++)
        {
            acc = fmin(acc, src[i+filterlen-1+j]); // Combine in 'r' element
            dst[i+j] = fmin(acc, dst[i+j]);        // Combine in 'l' elements
        }
    }
}

//----------------------------------------------------------------------------
// Running max filter
//----------------------------------------------------------------------------

void running_max(float *dst, int dstlen, const float *src, int srclen, int filterlen)
{
    assert(filterlen > 0);
    assert(dstlen == srclen-filterlen+1); // For now, exact sizes

    // i  i  i  i  i  i  i  i  i  i  i   input
    // l  l  s  l  l  s  l  l  s
    //    l  s     l  s     l  s
    // .  .  s  .  .  s  .  .  s
    //       s  r     s  r     s  r
    //       s  r  r  s  r  r  s  r  r
    //    o  o  o  o  o  o  o  o  o       dstput

    // Avoid special cases by aligning to a multiple of the filter length
    while (dstlen % filterlen)
    {
        // Trivial algorithm for edge case
        float acc = src[0];
        for (int j=1; j<filterlen; j++)
            acc = fmax(acc, src[j]);
        dst[0] = acc;

        dst++;
        src++;
        dstlen--;
        srclen--;
    }

    assert(dstlen % filterlen == 0);

    for (int i=0; i<dstlen; i+=filterlen)
    {
        // Starting element
        float acc = src[i+filterlen-1];
        dst[i+filterlen-1] = acc;      // Partial result

        // Left sweep
        for (int j= filterlen-2; j>=0; j--)
        {
            acc = fmax(acc, src[i+j]);  // Combine in 'l' element
            dst[i+j] = acc;             // Partial result in case of j>0
        }

        // Right sweep
        acc = src[i+filterlen-1]; // Use starting element again

        for (int j=1; j<filterlen; j++)
        {
            acc = fmax(acc, src[i+filterlen-1+j]); // Combine in 'r' element
            dst[i+j] = fmax(acc, dst[i+j]);        // Combine in 'l' elements
        }
    }
}

//----------------------------------------------------------------------------
// Hann low pass filter
//----------------------------------------------------------------------------

// Fast and accurate lowpass filter using Hann kernel
void hann_lowpass(float *dst, int dstlen, const float *src, int srclen, int filterlen)
{
    assert(filterlen > 0);
    assert(filterlen & 1); // so we can have 1 in the middle
    assert(dstlen == srclen-filterlen+1); // Require exact sizes

    // Initialize cosine and sine kernels
    float ckern[filterlen];
    float skern[filterlen];
    float k = 2*M_PI/filterlen;
    float csum = 0;
    for (int i=0; i<filterlen; i++)
    {
        float phi = k*(i-filterlen/2); // 0 degrees in the middle element
        ckern[i] = cos(phi);
        skern[i] = sin(phi);
        csum += ckern[i];
    }

    // Constant for normalizing the Hann kernel sum to 1
    float kh= 1.0/(filterlen + csum);

    // Initial window position
    float r=0, c=0, s=0;
    for (int i= 0; i<filterlen; i++)
    {
        float x = src[i];
        r += x;
        c += x*ckern[i];
        s += x*skern[i];
    }
    dst[0] = kh*(r + c);

    // Incremental update for remaining positions
    for (int i=1; i<dstlen; i++)
    {
        float dx = src[i+filterlen-1] - src[i-1];
        int j = (i-1) % filterlen;

        r += dx;
        c += dx*ckern[j];
        s += dx*skern[j];

        j = (i+filterlen/2) % filterlen;
        dst[i] = kh * (ckern[j]*c + skern[j]*s + r);
    }
}
