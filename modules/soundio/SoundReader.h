//============================================================================
//
//  SoundReader - Interface to audio file readers
//
//  * Class for reading from audio files
//  * Provides a SoundSource interface - uniting offline and live cases
//  * Non-copyable, but movable
//  * Hides the details of audio codec libraries
//  * Can use libsndfile or libmpg123 internally
//  * Provides efficient random access
//  * Block cache (makes sense for compressed audio)
//  * API is counted in samples, not stereo tuples.
//
//  HAVE_LIBMPG123 -- when defined, uses libmpg123
//
//  Copyright (c) 2005-2022 Erik Persson
//
//============================================================================

#ifndef SOUNDREADER_H
#define SOUNDREADER_H

#include <stdint.h>
#include "SoundSource.h"

class SoundReaderBackend;

// Common interface to file format readers
class SoundReader : public SoundSource
{
protected:
    SoundReaderBackend *m_backend = 0;

    // Block buffer
    int m_block_size = 0;   // size of block
    int m_block_cnt = 0;    // no. of blocks in file
    int m_block_no = -1;    // no. of current loaded block or -1
    short *m_block_buf = 0; // storage

    // Outwards-facing read position
    int64_t m_read_pos = 0;

public:
    SoundReader() {}
    SoundReader(const SoundReader& other) = delete;
    SoundReader(SoundReader&& other);
    SoundReader& operator=(const SoundReader& other) = delete;
    SoundReader& operator=(SoundReader&& other);
    virtual ~SoundReader();

    // Open file
    bool Open(const char *path, bool silent = false);

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
    bool SetReadPos(int64_t pos) override;
    void Close() override;

protected:
    short *GetBlock(int block_no);
};

#endif // SOUNDREADER_H
