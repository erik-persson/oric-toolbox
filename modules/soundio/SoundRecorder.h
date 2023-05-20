//----------------------------------------------------------------------------
//
//  SoundRecorder - Component for recording audio
//
//  * Class for recording audio from microphone or line in
//  * Provides a SoundSource interface - uniting offline and live cases
//  * Non-copyable and non-movable
//  * Hides the details of the audio I/O library it uses
//  * Implemented using PortAudio
//  * Supports 16-bit integer and 32-bit float formats
//  * Only supports mono
//
//  Copyright (c) 2006-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef SOUNDRECORDER_H
#define SOUNDRECORDER_H

#include "SoundSource.h"
#include <stdint.h>

class SoundRecorderBackend;

class SoundRecorder : public SoundSource
{
private:
    SoundRecorderBackend *m_backend = 0;

public:
    SoundRecorder() {}
    SoundRecorder(const SoundRecorder& other) = delete;
    SoundRecorder(SoundRecorder&& other);
    SoundRecorder& operator=(const SoundRecorder& other) = delete;
    SoundRecorder& operator=(SoundRecorder&& other);
    virtual ~SoundRecorder();

    // Call to open output device before using SoundSource interface below.
    // Returns true on success.
    bool Open(int sample_rate_hz, int samples_per_chunt);

    // SoundSource interface
    // Methods are documented in SoundSource.h
    int GetSampleRate() const override;
    int GetChannelCnt() const override;
    int64_t GetLength() const override;
    void Start() override;
    void Stop() override;
    bool IsRunning() const override;
    double GetElapsedTime() const override;
    bool Read(short *buf, int cnt) override;
    bool Read(float *buf, int cnt) override;
    int GetReadAvail() const override;
    int64_t GetReadPos() const override;
    bool SetReadPos(int64_t) override { return false; } // seeking not supported
    void Close() override;
};

#endif // SOUNDRECORDER_H
