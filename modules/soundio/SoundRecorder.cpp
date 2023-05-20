//----------------------------------------------------------------------------
//
//  SoundRecorder implementation
//
//  Copyright (c) 2006-2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "SoundRecorder.h"
#include "SoundPort.h"
#include <portaudio.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <atomic>

//----------------------------------------------------------------------------
// SoundRecorderBackend
//----------------------------------------------------------------------------

class SoundRecorderBackend : public SoundPort
{
    using sample_t = float;

protected:
    int m_sample_rate_hz = 0;
    int m_samples_per_chunk = 0;
    int64_t m_length = 0;
    int64_t m_read_pos = 0;

    // FIFO
    struct Fifo
    {
        sample_t *m_buf = 0;
        int m_size = 0;
        std::atomic<int> m_write_cnt = 0;
        int m_write_index = 0;
        std::atomic<int> m_read_cnt = 0;
        int m_read_index = 0;

        virtual ~Fifo();
        void Alloc(int size);
        int GetReadAvail() const;
        int GetWriteAvail() const;
        int Read(sample_t *buf, int len);
        int Write(const sample_t *buf, int len);

    } m_fifo;

protected:
    int OnChunk(
        const void *input_buffer,
        void *output_buffer,
        unsigned long frames_per_buffer,
        const PaStreamCallbackTimeInfo *time_info,
        PaStreamCallbackFlags status_flags) override;

public:
    SoundRecorderBackend() {};
    SoundRecorderBackend(const SoundRecorderBackend& other) = delete;
    SoundRecorderBackend& operator=(const SoundRecorderBackend& other) = delete;
    virtual ~SoundRecorderBackend();

    bool Open(int sample_rate_hz, int samples_per_cnt);

    int GetSampleRate() const { return m_sample_rate_hz; }
    int GetChannelCnt() const { return 1; }
    int64_t GetLength() const { return m_length; }
    void Start();
    void Stop();
    bool IsRunning() const;
    double GetElapsedTime() const;
    bool Read(float *buf, int cnt);
    int GetReadAvail() const;
    int64_t GetReadPos() const { return m_read_pos; }
};

//----------------------------------------------------------------------------

bool SoundRecorderBackend::Open(int sample_rate_hz, int samples_per_chunk)
{
    CloseStream(); // in case a prevoius call Open'ed with other parameters

    m_sample_rate_hz = sample_rate_hz;
    m_samples_per_chunk = samples_per_chunk;

    // Set up FIFO
    // At least 3 seconds or 8 chunks
    int chunk_cnt = (3*sample_rate_hz + samples_per_chunk-1)/samples_per_chunk;
    if (chunk_cnt < 8)
        chunk_cnt = 8;

    m_fifo.Alloc(chunk_cnt*m_samples_per_chunk);

    m_length = 0;
    m_read_pos = 0;

    return OpenStream(false, paFloat32, sample_rate_hz, samples_per_chunk);
}

//----------------------------------------------------------------------------

SoundRecorderBackend::~SoundRecorderBackend()
{
    // Base class closes stream and terminates portaudio
}

//----------------------------------------------------------------------------

SoundRecorderBackend::Fifo::~Fifo()
{
    delete[] m_buf;
}

//----------------------------------------------------------------------------

// Set up FIFO to hold size samples
void SoundRecorderBackend::Fifo::Alloc(int size)
{
    m_size = size;
    delete[] m_buf;
    m_buf = new sample_t[m_size];
    m_write_cnt = 0;
    m_write_index = 0;
    m_read_cnt = 0;
    m_read_index = 0;
}

//----------------------------------------------------------------------------

// Check how many samples that are currently buffered
int SoundRecorderBackend::Fifo::GetReadAvail() const
{
    return m_write_cnt - m_read_cnt;
}

//----------------------------------------------------------------------------

// Check how much space is free in the FIFO
int SoundRecorderBackend::Fifo::GetWriteAvail() const
{
    return m_size - GetReadAvail();
}

//----------------------------------------------------------------------------

// Nonblocking read from buffer
// Return no. of bytes transfered
int SoundRecorderBackend::Fifo::Read(sample_t *buf, int len)
{
    int transfered_len = 0;
    assert(len >= 0);
    while (1)
    {
        int avail = GetReadAvail();
        int distance_to_wrap = m_size - m_read_index;
        int amount = len;
        if (amount > avail)
            amount = avail;
        if (amount > distance_to_wrap)
            amount = distance_to_wrap;
        if (amount == 0)
            break;

        assert(amount > 0);
        assert(m_read_index>=0);
        assert(m_read_index + amount <= m_size);
        memcpy(buf, &(m_buf[m_read_index]), amount*sizeof(sample_t));
        buf += amount;
        len -= amount;
        transfered_len += amount;
        m_read_cnt += amount;
        m_read_index += amount;
        if (m_read_index == m_size)
            m_read_index = 0;
    }
    return transfered_len;
}

//----------------------------------------------------------------------------

// Nonblocking write to buffer
// Return no. of bytes transfered
int SoundRecorderBackend::Fifo::Write(const sample_t *buf, int len)
{
    int transfered_len = 0;
    assert(len >= 0);
    while (1)
    {
        int avail = GetWriteAvail();
        int distance_to_wrap = m_size - m_write_index;
        int amount = len;
        if (amount > avail)
            amount = avail;
        if (amount > distance_to_wrap)
            amount = distance_to_wrap;
        if (amount == 0)
            break;

        assert(amount > 0);
        assert(m_write_index >= 0);
        assert(m_write_index + amount <= m_size);
        memcpy(&(m_buf[m_write_index]), buf, amount*sizeof(sample_t));
        buf += amount;
        len -= amount;
        transfered_len += amount;
        m_write_cnt += amount;
        m_write_index += amount;
        if (m_write_index == m_size)
            m_write_index = 0;
    }
    return transfered_len;
}

//----------------------------------------------------------------------------

// Handler called to transfer a chunk of audio data from stream
// May called at interrupt level on some machines so avoid system calls
int SoundRecorderBackend::OnChunk(
    const void *input_buffer,
    void *output_buffer,
    unsigned long frames_per_buffer,
    const PaStreamCallbackTimeInfo *time_info,
    PaStreamCallbackFlags status_flags)
{
    const sample_t *input = (const sample_t *) input_buffer;
    (void) output_buffer;
    (void) time_info;
    (void) status_flags;

    int transfered_len = m_fifo.Write(input, frames_per_buffer);

    if (transfered_len < (int) frames_per_buffer)
        fprintf(stderr, "Overflow in SoundRecorderBackend\n");

    m_length += transfered_len;

    return paContinue;
}


//----------------------------------------------------------------------------

void SoundRecorderBackend::Start()
{
    StartStream();
}

//----------------------------------------------------------------------------

void SoundRecorderBackend::Stop()
{
    StopStream();
}

//----------------------------------------------------------------------------

bool SoundRecorderBackend::IsRunning() const
{
    return IsStreamStarted();
}

//----------------------------------------------------------------------------

// Return no. of seconds recording has run
double SoundRecorderBackend::GetElapsedTime() const
{
    // TODO: We also have GetLength() which operates on a very
    //       different principle. May want to harmonize/simplify.

    if (IsStreamStarted())
        return GetCurrentTime() - GetStartTime();

    return 0;
}

//----------------------------------------------------------------------------

// Check how many samples that are immediately available
int SoundRecorderBackend::GetReadAvail() const
{
    return m_fifo.GetReadAvail();
}

//----------------------------------------------------------------------------

// Blocking read
// Non-blocking when not reading past GetReadAvail()
// Pad with zeros if reading past end of stopped recording
bool SoundRecorderBackend::Read(float *buf, int cnt)
{
    assert(cnt >= 0);
    m_read_pos += cnt;

    while (cnt>0)
    {
        int avail = GetReadAvail();
        while (avail==0 && IsRunning())
        {
            // Wait 1/4 chunk time
            int ms = 250*m_samples_per_chunk/m_sample_rate_hz;
            Pa_Sleep(ms);
            avail = GetReadAvail();
        }

        if (avail==0)
            break;

        int transfered_len = m_fifo.Read(buf, cnt);
        buf += transfered_len;
        cnt -= transfered_len;
    }

    // Pad with zeros. This only happens if recording has stopped.
    while (cnt--)
        *buf++ = 0;

    return true;
}

//----------------------------------------------------------------------------
// SoundRecorder - front end
//----------------------------------------------------------------------------

// Move-construct by taking over existing object
SoundRecorder::SoundRecorder(SoundRecorder&& other)
{
    *this = std::move(other);
}

//----------------------------------------------------------------------------

// Move-assign by taking over existing object
SoundRecorder& SoundRecorder::operator=(SoundRecorder&& other)
{
    // No risk of self-assignment since other is temporary.
    delete m_backend;

    // Take over everything from the other object
    m_backend = other.m_backend;

    // Clear other object
    other.m_backend = 0;
    return *this;
}

//----------------------------------------------------------------------------

SoundRecorder::~SoundRecorder()
{
    delete m_backend;
}

//----------------------------------------------------------------------------

bool SoundRecorder::Open(int sample_rate_hz, int samples_per_chunk)
{
    delete m_backend;
    m_backend = new SoundRecorderBackend;
    if (!m_backend->Open(sample_rate_hz, samples_per_chunk))
    {
        delete m_backend;
        m_backend = 0;
    }
    return m_backend != 0;
}

//----------------------------------------------------------------------------

int SoundRecorder::GetSampleRate() const
{
    return m_backend ? m_backend->GetSampleRate() : 0;
}

//----------------------------------------------------------------------------

int SoundRecorder::GetChannelCnt() const
{
    return m_backend ? m_backend->GetChannelCnt() : 0;
}

//----------------------------------------------------------------------------

int64_t SoundRecorder::GetLength() const
{
    return m_backend ? m_backend->GetLength() : 0;
}

//----------------------------------------------------------------------------

void SoundRecorder::Start()
{
    if (m_backend)
        m_backend->Start();
}

//----------------------------------------------------------------------------

void SoundRecorder::Stop()
{
    if (m_backend)
        m_backend->Stop();
}

//----------------------------------------------------------------------------

bool SoundRecorder::IsRunning() const
{
    return m_backend? m_backend->IsRunning() : false;
}

//----------------------------------------------------------------------------

// Return no. of seconds recording has run
double SoundRecorder::GetElapsedTime() const
{
    return m_backend? m_backend->GetElapsedTime() : 0;
}

//----------------------------------------------------------------------------

// Check how many samples that are immediately available
int SoundRecorder::GetReadAvail() const
{
    return m_backend? m_backend->GetReadAvail() : 0;
}

//----------------------------------------------------------------------------

int64_t SoundRecorder::GetReadPos() const
{
    return m_backend ? m_backend->GetReadPos() : 0;
}

//----------------------------------------------------------------------------

// Blocking read
// Non-blocking when not reading past GetReadAvail()
// Pad with zeros if reading past end of stopped recording
bool SoundRecorder::Read(float *buf, int cnt)
{
    return m_backend ? m_backend->Read(buf, cnt) : false;
}

//----------------------------------------------------------------------------

// Blocking read
// Non-blocking when not reading past GetReadAvail()
// Pad with zeros if reading past end of stopped recording
bool SoundRecorder::Read(short *buf, int cnt)
{
    (void) buf;
    (void) cnt;
    assert(0); // Not implemented
    return false;
}

//----------------------------------------------------------------------------

void SoundRecorder::Close()
{
    delete m_backend;
    m_backend = 0;
}
