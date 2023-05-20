//----------------------------------------------------------------------------
//
//  SoundSink - Interface for audio output to file or speaker
//
//  * Common interface for offline and live audio output
//  * Supports float
//  * Supports 16-bit integer
//  * Only supports mono
//
//  Copyright (c) 2005-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef SOUNDSINK_H
#define SOUNDSINK_H

#include <stdint.h>

class SoundSink
{
public:
    // Return length written so far
    virtual int64_t GetWritePos() const = 0;

    // Blocking write function - short version
    // Return true if successful
    virtual bool Write(const short *buf, int len) = 0;

    // Blocking write function - float version
    // Return true if successful
    virtual bool Write(const float *buf, int len);

    // Start playing if not done so already
    // Playing might only start automatically after writing a threshould amount
    // Wait until playing has finished, or timeout reached
    // Nonblocking when passed a zero argument
    virtual void Flush(double timeout = 1e9) = 0;

    // Return true if there is written data which has not yet played
    // Callable from any thread
    virtual bool IsPlaying() const = 0;

    // Check duration of audio that has been written, in seconds
    // Callable from any thread
    virtual double GetWrittenTime() const = 0;

    // Check duration of written data that has been played, in seconds
    // Callable from any thread
    virtual double GetElapsedTime() const = 0;

    // Check duration of written data that remain to be played, in seconds
    // Callable from any thread
    virtual double GetTimeLeft() const = 0;

    // Finish writing file or wait for playback to finish, release resources
    virtual void Close() = 0;

    virtual ~SoundSink() {}
};

#endif // SOUNDSINK_H
