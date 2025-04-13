//============================================================================
//
//  Signal processing filters
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef FILTERS_H
#define FILTERS_H

// Cubic interpolation
float interp(const float *vals, int cnt, float x);
static inline float interp_lin(const float *vals, int cnt, float x);

// Running min filter
void running_min(float *dst, int dstlen, const float *src, int srclen, int filterlen);

// Running max filter
void running_max(float *dst, int dstlen, const float *src, int srclen, int filterlen);

// Low pass filter using Hann kernel
void hann_lowpass(float *dst, int dstlen, const float *src, int srclen, int filterlen);

//----------------------------------------------------------------------------
// Linear interpolation implementation
//----------------------------------------------------------------------------

#include <tgmath.h>

static inline float interp_lin(const float *vals, int cnt, float x)
{
    int x0= (int) floor(x);
    float frac= x-x0;
    float y0= (x0  >=0 && x0  <cnt)? vals[x0  ]:0;
    float y1= (x0+1>=0 && x0+1<cnt)? vals[x0+1]:0;
    return y0 + frac*(y1-y0);
}

#endif
