//----------------------------------------------------------------------------
//
//  Downsampler -- Anti-aliasing downsampler using windowed-sinc filter
//
//  Copyright (c) 2005 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef DOWNSAMPLER_H
#define DOWNSAMPLER_H

class Downsampler
{
    int m_down_factor;
    int m_coeff_cnt;
    float *m_coeffs; // coeffs for non-negative offsets

public:
    Downsampler(int down_factor);
    ~Downsampler();

    void Downsample(float *buf, int len,
                    const float *src, int srclen, int srcoffs) const;

    // Return the no. of extra samples needed before and after the sample points in src
    int GetExtraSamplesNeeded() const;
};

#endif // DOWNSAMPLER_H
