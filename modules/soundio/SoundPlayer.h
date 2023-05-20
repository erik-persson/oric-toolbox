//----------------------------------------------------------------------------
//
//  SoundPlayer - Component for playing audio
//
//  * Class for playing audio to speaker or line out
//  * Provides a SoundSink interface - uniting offline and live cases
//  * Non-copyable and non-movable
//  * Nonblocking operation
//  * Hides the details of the audio I/O library it uses
//  * Implemented using PortAudio
//  * Uses a FIFO of data that PortAudio callback can collect
//  * Uses a thread to read from file in background
//  * Supports 16-bit integer and 32-bit float formats
//  * Only supports mono
//
//  Copyright (c) 2006-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef SOUNDPLAYER_H
#define SOUNDPLAYER_H

#include "SoundSink.h"
#include "Sound.h"

class SoundPlayerBackend;

class SoundPlayer : public SoundSink
{
private:
    SoundPlayerBackend *m_backend = 0;

public:
    SoundPlayer() {};
    SoundPlayer(const SoundPlayer& other) = delete;
    SoundPlayer(SoundPlayer&& other);
    SoundPlayer& operator=(const SoundPlayer& other) = delete;
    SoundPlayer& operator=(SoundPlayer&& other);
    virtual ~SoundPlayer();

    // Initialization before using Write interface
    bool Open(int sample_rate_hz);

    // SoundSink interface
    // Methods are commented in SoundSink.h
    int64_t GetWritePos() const override;
    bool Write(const short *buf, int len) override;
    bool Write(const float *buf, int len) override;
    void Flush(double timeout = 1e9) override;
    bool IsPlaying() const override;
    double GetWrittenTime() const override;
    double GetElapsedTime() const override;
    double GetTimeLeft() const override;
    void Close() override;

    // Play a sound, nonblocking
    bool Play(const Sound& sound);

    // Play a sound file, nonblocking
    bool Play(const char *filename);

    // Stop playing, discard queued audio
    void Stop();

    // Stop playing and release the device so other programs can play sound
    void ReleaseDevice();
};

#endif // SOUNDPLAYER_H
