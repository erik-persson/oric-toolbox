//----------------------------------------------------------------------------
//
//  Downsampler implementation
//
//  Copyright (c) 2005-2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "Downsampler.h"
#include <tgmath.h>

//----------------------------------------------------------------------------

// sinc(x) = sin(PI*x)/(PI*x)
static __inline double sinc(double x)
{
    if (x==0)
        return 1.0;
    double t= M_PI*x;
    return sin(t)/t;
}

//----------------------------------------------------------------------------

Downsampler::Downsampler(int down_factor)
{
    m_down_factor= down_factor;
    m_coeff_cnt= down_factor==1? 1 : 8*down_factor;
    m_coeffs= new float[m_coeff_cnt];

    // Hann windowed sinc
    for (int i= 0; i<m_coeff_cnt; i++)
        m_coeffs[i]= sinc(((double) i)/down_factor)*(1 + cos(M_PI*i/m_coeff_cnt));

    // Normalize sum to 1.
    float s= m_coeffs[0]; // count nonzero indices twice.
    for (int i= 1; i<m_coeff_cnt; i++)
        s += 2*m_coeffs[i];

    for (int i= 0; i<m_coeff_cnt; i++)
        m_coeffs[i] /= s;
}

//----------------------------------------------------------------------------

Downsampler::~Downsampler()
{
    delete[] m_coeffs;
}

//----------------------------------------------------------------------------

void Downsampler::Downsample(float *buf, int len, const float *src, int srclen, int srcoffs) const
{
    int j= srcoffs;
    for (int i= 0; i<len; i++)
    {
        float s= m_coeffs[0]*((j>=0 && j<srclen)? src[j]:0);
        for (int k= 1; k<m_coeff_cnt; k++)
            s += m_coeffs[k]*
            ( ((j+k>=0 && j+k<srclen)? src[j+k]:0) +
              ((j-k>=0 && j-k<srclen)? src[j-k]:0)
            );

        buf[i]= s;
        j += m_down_factor;
    }
}

//----------------------------------------------------------------------------

// Return the no. of extra samples needed before and after the sample points in src
int Downsampler::GetExtraSamplesNeeded() const
{
    return m_coeff_cnt-1;
}
