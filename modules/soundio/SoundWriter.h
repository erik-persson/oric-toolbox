//============================================================================
//
//  SoundWriter - Audio waveform file writer
//
//  * Class for writing to audio files
//  * Provides a SoundSink interface - uniting offline and live cases
//  * Non-copyable, but movable
//  * Hides the details of audio codec libraries
//  * Uses libsndfile internally
//  * Uses .wav format
//  * Only supports mono
//
//  Copyright (c) 2005-2022 Erik Persson
//
//============================================================================

#ifndef SOUNDWRITER_H
#define SOUNDWRITER_H

#include "SoundSink.h"
#include <stdint.h>

class SoundWriterBackend;

class SoundWriter : public SoundSink
{
protected:
    SoundWriterBackend *m_backend = 0;

public:
    SoundWriter() {}
    SoundWriter(const SoundWriter& other) = delete;
    SoundWriter(SoundWriter&& other);
    SoundWriter& operator=(const SoundWriter& other) = delete;
    SoundWriter& operator=(SoundWriter&& other);
    virtual ~SoundWriter();

    // Call to open file before using SoundSink interface below.
    // Returns true on success.
    bool Open(const char *path, int sample_rate);

    // SoundSink interface
    // Methods are documented in SoundSink.h
    int64_t GetWritePos() const override;
    bool Write(const short *buf, int len) override;
    bool Write(const float *buf, int len) override;
    void Flush(double timeout = 1e9) override;
    bool IsPlaying() const override;
    double GetWrittenTime() const override;
    double GetElapsedTime() const override;
    double GetTimeLeft() const override;
    void Close() override;
};

#endif // SOUNDWRITER_H
