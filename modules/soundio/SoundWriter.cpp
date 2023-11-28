//----------------------------------------------------------------------------
//
//  SoundWriter implementation
//
//  Copyright (c) 2005 - 2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "SoundWriter.h"
#include "SoundSink.h"
#include <sndfile.h>
#include <stdint.h>
#include <string.h>
#include <atomic>
#include <utility>

//----------------------------------------------------------------------------
// SoundWriterBackend
//----------------------------------------------------------------------------

class SoundWriterBackend
{
protected:
    SF_INFO m_info;
    SNDFILE *m_sf = 0;
    std::atomic<int64_t> m_write_pos = 0;

public:
    SoundWriterBackend() {}
    SoundWriterBackend(const SoundWriterBackend& other) = delete;
    SoundWriterBackend& operator=(const SoundWriterBackend& other) = delete;
    virtual ~SoundWriterBackend();

    bool Open(const char *path, int sample_rate);

    int GetSampleRate() const;
    int64_t GetWritePos() const;
    bool Write(const short *buf, int len);
    double GetWrittenTime() const;
    void Close();
};

//----------------------------------------------------------------------------

SoundWriterBackend::~SoundWriterBackend()
{
}

//----------------------------------------------------------------------------

bool SoundWriterBackend::Open(const char *path, int sample_rate)
{
    Close();

    memset(&m_info,0,sizeof(m_info));
    m_info.channels= 1;
    m_info.samplerate= sample_rate;
    m_info.format= SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    if (!sf_format_check(&m_info))
        fprintf(stderr,"sf_format_check: Invalid format\n");
    m_sf= sf_open(path, SFM_WRITE, &m_info);

    if (!m_sf)
    {
        fprintf(stderr,"Could not open sound file %s for writing\n", path);
        return false;
    }

    return true;
}

//----------------------------------------------------------------------------

// Return sample rate in Hz
int SoundWriterBackend::GetSampleRate() const
{
    return m_info.samplerate;
}

//----------------------------------------------------------------------------

int64_t SoundWriterBackend::GetWritePos() const
{
    return m_write_pos;
}

//----------------------------------------------------------------------------

bool SoundWriterBackend::Write(const short *buf, int len)
{
    int cnt2 = sf_write_short(m_sf,(short *) buf,len);
    m_write_pos += cnt2;
    return cnt2 == len;
}

//----------------------------------------------------------------------------

void SoundWriterBackend::Close()
{
    if (m_sf)
        sf_close(m_sf);
    m_sf = 0;
}

//----------------------------------------------------------------------------
// SoundWriter - front end part
//----------------------------------------------------------------------------

// Move-construct by taking over existing object
SoundWriter::SoundWriter(SoundWriter&& other)
{
    *this = std::move(other);
}

//----------------------------------------------------------------------------

// Move-assign by taking over existing object
SoundWriter& SoundWriter::operator=(SoundWriter&& other)
{
    // Discard our old belongings
    delete m_backend;

    // Take over belongings from other obeject
    m_backend = other.m_backend;

    // Clear other object
    other.m_backend = 0;

    return *this;
}

//----------------------------------------------------------------------------

SoundWriter::~SoundWriter()
{
    Close();
}

//----------------------------------------------------------------------------

bool SoundWriter::Open(const char *path, int sample_rate)
{
    delete m_backend;
    m_backend = new SoundWriterBackend;
    if (!m_backend->Open(path, sample_rate))
    {
        delete m_backend;
        m_backend = 0;
    }
    return m_backend != 0;
}

//----------------------------------------------------------------------------

int64_t SoundWriter::GetWritePos() const
{
    return m_backend ? m_backend->GetWritePos() : 0;
}

//----------------------------------------------------------------------------

bool SoundWriter::Write(const short *buf, int len)
{
    return m_backend ? m_backend->Write(buf,len) : false;
}

//----------------------------------------------------------------------------

bool SoundWriter::Write(const float *buf, int len)
{
    // Call SoundSink's default float write which converts to shorts
    return SoundSink::Write(buf, len);
}

//----------------------------------------------------------------------------

void SoundWriter::Flush(double)
{
    // not relevant when writing to file
}

//----------------------------------------------------------------------------

bool SoundWriter::IsPlaying() const
{
    return false; // not relevant when writing to file
}

//----------------------------------------------------------------------------

// Check how many seconds of audio has been written
// This may be called from any thread
double SoundWriter::GetWrittenTime() const
{
    if (!m_backend)
        return 0;
    return ((double) m_backend->GetWritePos()) / m_backend->GetSampleRate();
}

//----------------------------------------------------------------------------

// Check how long we have been playing
// Returns a value between 0 and the duration of input, in seconds
// This may be called from any thread
double SoundWriter::GetElapsedTime() const
{
    return GetWrittenTime(); // mimic a player that has finished
}

//----------------------------------------------------------------------------

// Check how long it is until playing ends
// Returns a value between 0 and the duration of input, in seconds
// This may be called from any thread
double SoundWriter::GetTimeLeft() const
{
    return 0; // not relevant when writing to file
}

//----------------------------------------------------------------------------

void SoundWriter::Close()
{
    if (m_backend)
        delete m_backend;
    m_backend = 0;
}
