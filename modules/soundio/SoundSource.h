//----------------------------------------------------------------------------
//
//  SoundSource - Interface for audio input from file or microphone
//
//  * Common interface for offline and live audio input
//  * Supports float
//  * Supports 16-bit integer
//  * API is counted in samples, not stereo tuples.
//
//  Copyright (c) 2005-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef SOUNDSOURCE_H
#define SOUNDSOURCE_H

#include <stdint.h>

class SoundSource
{
public:
    virtual ~SoundSource() {}

    // Return sample rate in Hz
    virtual int GetSampleRate() const = 0;

    // Return no. of channels. 1=mono, 2=stereo etc
    virtual int GetChannelCnt() const = 0;

    // Return length recorded so far, counted in samples
    // May increase while recording is running
    virtual int64_t GetLength() const = 0;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    // Return true if recording is running
    virtual bool IsRunning() const = 0;

    // Return no. of seconds recording has run
    virtual double GetElapsedTime() const = 0;

    // Return length read so far
    virtual int64_t GetReadPos() const = 0;

    // If seeking is not supported, return false
    // If seeking is supported, move read position and return true
    virtual bool SetReadPos(int64_t pos) = 0;

    virtual bool Read(short *buf, int cnt) = 0;
    virtual bool Read(float *buf, int cnt) = 0;

    // Check how many samples are immediately available for reading
    // without waiting for recording to progress further
    virtual int GetReadAvail() const = 0;

    // Release resources
    virtual void Close() = 0;
};

#endif // SOUNDSOURCE_H
