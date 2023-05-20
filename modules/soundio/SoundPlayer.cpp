//----------------------------------------------------------------------------
//
//  SoundPlayer implementation
//
//  Copyright (c) 2006-2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "SoundPlayer.h"
#include "SoundPort.h"
#include "Sound.h"
#include <stdio.h>
#include <tgmath.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <thread>
#include <atomic>

//----------------------------------------------------------------------------
// SoundPlayerBackend
//----------------------------------------------------------------------------

class SoundPlayerBackend : public SoundPort, public SoundSink
{
public:
    using sample_t = short;

private:
    Sound m_sound;
    int m_sample_rate_hz = 0;
    int m_samples_per_chunk = 0;

    // These are made atomic so Write() and GetTimeLeft()
    // can be used from different treads.
    std::atomic<int64_t> m_write_pos = 0;    // samples written
    std::atomic<int64_t> m_pending_len = 0;  // samples written but maybe not played yet

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
        int Write(const Sound& sound, int len);

    } m_fifo;

    bool m_refill_pending = false;
    std::thread *m_refill_thread = 0;

    bool m_stopping = false;

protected:
    void FinishThread();
    void FinishStream();

    int OnChunk(
        const void *input_buffer,
        void *output_buffer,
        unsigned long frames_per_buffer,
        const PaStreamCallbackTimeInfo *time_info,
        PaStreamCallbackFlags status_flags) override;

    void Refill();
    void RefillThread();

public:
    SoundPlayerBackend() {}
    SoundPlayerBackend(const SoundPlayerBackend& other) = delete;
    SoundPlayerBackend& operator=(const SoundPlayerBackend& other) = delete;
    virtual ~SoundPlayerBackend();

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

    bool Play(const Sound& sound);
    void Stop();
};

//----------------------------------------------------------------------------

SoundPlayerBackend::~SoundPlayerBackend()
{
    Stop();
}

//----------------------------------------------------------------------------

SoundPlayerBackend::Fifo::~Fifo()
{
    delete[] m_buf;
}

//----------------------------------------------------------------------------

// Set up FIFO to hold size samples
void SoundPlayerBackend::Fifo::Alloc(int size)
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

// Check how many samples are currently buffered
int SoundPlayerBackend::Fifo::GetReadAvail() const
{
    return m_write_cnt - m_read_cnt;
}

//----------------------------------------------------------------------------

// Check how much space is free
int SoundPlayerBackend::Fifo::GetWriteAvail() const
{
    return m_size - GetReadAvail();
}

//----------------------------------------------------------------------------

// Nonblocking read from buffer
// Return no. of bytes transfered
int SoundPlayerBackend::Fifo::Read(sample_t *buf, int len)
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
int SoundPlayerBackend::Fifo::Write(const sample_t *buf, int len)
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

// Nonblocking write to buffer, copying from Sound object
// Return no. of bytes transfered
int SoundPlayerBackend::Fifo::Write(const Sound& sound, int len)
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
        sound.Read(m_write_cnt, &(m_buf[m_write_index]), amount);
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
int SoundPlayerBackend::OnChunk(
    const void *input_buffer,
    void *output_buffer,
    unsigned long frames_per_buffer,
    const PaStreamCallbackTimeInfo *time_info,
    PaStreamCallbackFlags status_flags)
{
    (void) input_buffer;
    sample_t *output = (sample_t *) output_buffer;
    (void) time_info;
    (void) status_flags;

    // Transfer from FIFO to PortAudio's output buffer
    int transfered_len = m_fifo.Read(output, frames_per_buffer);

    // Pad out with zeros
    while (transfered_len < (int) frames_per_buffer)
        output[transfered_len++] = 0;

    // PortAudio v19.7.0 Linux tends to drop end of audio if we return
    // paComplete. So we always return paContinue here.
    return paContinue;
}

//----------------------------------------------------------------------------

// Populate FIFO with data from m_sound
void SoundPlayerBackend::Refill()
{
    if (m_stopping)
        m_refill_pending = false;
    if (!m_refill_pending)
        return;

    m_fifo.Write(m_sound, m_samples_per_chunk);

    m_refill_pending =
        m_fifo.m_write_cnt < m_sound.GetLength() &&
        !m_stopping;
}

//----------------------------------------------------------------------------

// Background thread to refill FIFO
void SoundPlayerBackend::RefillThread()
{
    while (m_refill_pending)
    {
        if (m_fifo.GetWriteAvail() > 0)
            Refill();

        double t_wait = ((double) m_samples_per_chunk)/m_sample_rate_hz;
        int ms = (int) floor(t_wait*1e3);
        Pa_Sleep(ms);
    }
}

//----------------------------------------------------------------------------

void SoundPlayerBackend::FinishThread()
{
    if (m_refill_thread)
    {
        m_refill_thread->join();
        delete m_refill_thread;
        m_refill_thread = 0;
    }
}

//----------------------------------------------------------------------------

// Stop and close portaudio stream
void SoundPlayerBackend::FinishStream()
{
    // Stop after playing all buffers processed in callback
    StopStream();
    CloseStream();
}

//----------------------------------------------------------------------------

// Initialization before using Write interface
bool SoundPlayerBackend::Open(int sample_rate_hz)
{
    m_sample_rate_hz = sample_rate_hz;
    m_samples_per_chunk = sample_rate_hz/8; // 125 ms chunks

    // Set up FIFO
    // 3 seconds of buffer, divided in 24 125 ms chunks
    m_fifo.Alloc( 24 * m_samples_per_chunk );

    // 16 bit fixed point mono output
    return OpenStream(true, paInt16, m_sample_rate_hz, m_samples_per_chunk);
}

//----------------------------------------------------------------------------

// SoundSink interface
// Return no of samples written (or enqueued using Play)
int64_t SoundPlayerBackend::GetWritePos() const
{
    return m_write_pos;
}

//----------------------------------------------------------------------------

// SoundSink interface, short variety (preferred)
// Blocking write
// Non-blocking when not writing more than m_fifo.GetWriteAvail()
bool SoundPlayerBackend::Write(const short *buf, int len)
{
    assert(len >= 0);
    assert(m_stream); // stream must be open

    while (len>0)
    {
        int free = m_fifo.GetWriteAvail();

        // Start stream once FIFO half full
        if (free <= m_fifo.GetReadAvail() && Pa_IsStreamActive(m_stream) == 0)
            (void) StartStream();

        while (free==0 && Pa_IsStreamActive(m_stream) == 1)
        {
            // Wait 1/4 chunk time
            int ms = (int) floor(250.0*m_samples_per_chunk/m_sample_rate_hz);
            Pa_Sleep(ms);
            free = m_fifo.GetWriteAvail();
        }

        // Call non-blocking write function to copy into FIFO
        int transfered_len = m_fifo.Write(buf, len);
        if (transfered_len == 0)
            break;

        buf += transfered_len;
        len -= transfered_len;
        m_pending_len += transfered_len;
        m_write_pos += transfered_len;
    }

    return len==0; // all transfered
}

//----------------------------------------------------------------------------

// SoundSink interface, float variety
bool SoundPlayerBackend::Write(const float *buf, int len)
{
    // Call SoundSink's default float write which converts to shorts
    return SoundSink::Write(buf, len);
}

//----------------------------------------------------------------------------

// Finish using SoundSink interface
void SoundPlayerBackend::Close()
{
    Flush();
    Stop();
}

//----------------------------------------------------------------------------

// Play a sound file
bool SoundPlayerBackend::Play(const Sound& sound)
{
    // Stop previous stream
    Stop();

    if (Open(sound.GetSampleRate()))
    {
        m_sound = sound;

        int64_t len = sound.GetLength();
        m_write_pos += len;   // note as written
        m_pending_len += len; // note as not yet played

        // Fill up buffer before starting stream
        m_refill_pending = true;
        m_stopping = false;
        while (m_fifo.GetWriteAvail()>0 && m_refill_pending)
            Refill();

        if (StartStream())
        {
            if (m_refill_pending)
                m_refill_thread = new std::thread(
                    &SoundPlayerBackend::RefillThread,
                    this);
            return true;
        }
        m_refill_pending = false;
    }

    FinishStream();
    return false;
}

//----------------------------------------------------------------------------

// Stop playing
void SoundPlayerBackend::Stop()
{
    m_stopping = true; // Tell thread to exit quickly
    FinishStream();    // Disable audio directly
    FinishThread();    // Then wait for the refill thread
    m_stopping = false;

    // Mark all playback completed
    m_pending_len = 0;

    // FIFO may still have contents, but it is cleared on Open
}

//----------------------------------------------------------------------------

bool SoundPlayerBackend::IsPlaying() const
{
    return GetTimeLeft() > 0;
}

//----------------------------------------------------------------------------

// Check how many seconds of audio has been written
// This may be called from any thread
double SoundPlayerBackend::GetWrittenTime() const
{
    return ((double) m_write_pos)/m_sample_rate_hz;
}

//----------------------------------------------------------------------------

// Check how long we have been playing
// Returns a value between 0 and the duration of input, in seconds
// This may be called from any thread
double SoundPlayerBackend::GetElapsedTime() const
{

    if (IsStreamStarted())
    {
        double dt = GetCurrentTime() - GetStartTime();
        double t1 = GetWrittenTime();
        return
            dt <= 0  ? 0  :
            dt >= t1 ? t1 :
            dt;
    }
    return 0;
}

//----------------------------------------------------------------------------

// Check how long it is until playing ends
// Returns a value between 0 and the duration of input, in seconds
// This may be called from any thread
double SoundPlayerBackend::GetTimeLeft() const
{
    double t0 = GetElapsedTime();
    double t1 = GetWrittenTime();
    return t1-t0;
}

//----------------------------------------------------------------------------

// Start playing unless started already
// Wait until playing has finished, or timeout reached
void SoundPlayerBackend::Flush(double t_timeout /*=1e9*/)
{
    // Get stream started if blocking write interface wrote below threshold
    if (m_stream && Pa_IsStreamActive(m_stream) == 0 && m_fifo.GetReadAvail())
        StartStream();
    if (t_timeout <= 0)
        return; // nonblocking in this case

    // If timeout seems shorter than what's left, then wait just the timeout
    double t_left = GetTimeLeft();
    if (t_timeout < t_left)
    {
        int ms = (int) floor(t_timeout*1e3);
        Pa_Sleep(ms);
        return;
    }

    // Wait for refill thread to exit
    FinishThread();

    // Wait for audio stream to complete
    double t_min = .01; // 10 ms
    double t_max = 1.0; // 1 s
    while ((t_left = GetTimeLeft()) > 0)
    {
        double t_wait = t_left<t_min ? t_min :
                        t_left>t_max ? t_max :
                        t_left;
        int ms = (int) floor(t_wait*1e3);
        Pa_Sleep(ms);
    }

    // Mark all playback completed
    m_pending_len = 0;
}

//----------------------------------------------------------------------------
// SoundPlayer - frontend
//
// Frontend keeps a pointer to the backend
// This hides the library details, and allows the frontend to be movable.
// Backend lifetime is between Open and Close, or Play and ReleaseDevice
//----------------------------------------------------------------------------

// Move-construct by taking over existing object
SoundPlayer::SoundPlayer(SoundPlayer&& other)
{
    *this = std::move(other);
}

//----------------------------------------------------------------------------

// Move-assign by taking over existing object
SoundPlayer& SoundPlayer::operator=(SoundPlayer&& other)
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

SoundPlayer::~SoundPlayer()
{
    delete m_backend;
}

//----------------------------------------------------------------------------

// Initialization before using Write interface
bool SoundPlayer::Open(int sample_rate_hz)
{
    if (!m_backend)
        m_backend = new SoundPlayerBackend;
    if (!m_backend->Open(sample_rate_hz))
    {
        delete m_backend;
        m_backend = 0;
    }
    return m_backend != 0;
}

//----------------------------------------------------------------------------

// SoundSink interface
// Return no of samples written (or enqueued using Play)
int64_t SoundPlayer::GetWritePos() const
{
    return m_backend ? m_backend->GetWritePos() : 0;
}

//----------------------------------------------------------------------------

// SoundSink interface, short variety (preferred)
// Blocking write
// Non-blocking when not writing more than m_fifo.GetWriteAvail()
bool SoundPlayer::Write(const short *buf, int len)
{
    return m_backend ? m_backend->Write(buf, len) : false;
}

//----------------------------------------------------------------------------

// SoundSink interface, float variety
bool SoundPlayer::Write(const float *buf, int len)
{
    // Call SoundSink's default float write which converts to shorts
    return SoundSink::Write(buf, len);
}

//----------------------------------------------------------------------------

// Finish using SoundSink interface
// Wait for playback to finish, then release audio device
void SoundPlayer::Close()
{
    if (m_backend)
    {
        m_backend->Close(); // this waits for playback to finish
        delete m_backend;   // this releases the audio device
        m_backend = 0;
    }
}

//----------------------------------------------------------------------------

// Play a sound, nonblocking
bool SoundPlayer::Play(const Sound& sound)
{
    if (!m_backend)
        m_backend = new SoundPlayerBackend;
    if (!m_backend->Play(sound))
    {
        delete m_backend;
        m_backend = 0;
    }
    return m_backend != 0;
}

//----------------------------------------------------------------------------

// Play a sound file, nonblocking
bool SoundPlayer::Play(const char *filename)
{
    Sound sound;
    if (!sound.ReadFromFile(filename)) // will report errors
        return false;
    return Play(sound);
}

//----------------------------------------------------------------------------

// Stop playing
void SoundPlayer::Stop()
{
    if (m_backend)
        m_backend->Stop();
}

//----------------------------------------------------------------------------

// Stop playing and release the device so other programs can play sound
// Can be used after Play when IsPlaying has returned false.
void SoundPlayer::ReleaseDevice()
{
    Close();
}

//----------------------------------------------------------------------------

// Return true if there is written data which has not yet played
// Callable from any thread
bool SoundPlayer::IsPlaying() const
{
    return m_backend ? m_backend->IsPlaying() : false;
}

//----------------------------------------------------------------------------

// Check how many seconds of audio has been written
// This may be called from any thread
double SoundPlayer::GetWrittenTime() const
{
    return m_backend ? m_backend->GetWrittenTime() : 0;
}

//----------------------------------------------------------------------------

// Check how long we have been playing
// Returns a value between 0 and the duration of input, in seconds
// This may be called from any thread
double SoundPlayer::GetElapsedTime() const
{
    return m_backend ? m_backend->GetElapsedTime() : 0;
}

//----------------------------------------------------------------------------

// Check how long it is until playing ends
// Returns a value between 0 and the duration of input, in seconds
// This may be called from any thread
double SoundPlayer::GetTimeLeft() const
{
    return m_backend ? m_backend->GetTimeLeft() : 0;
}

//----------------------------------------------------------------------------

// Start playing unless started already
// Wait until playing has finished, or timeout reached
void SoundPlayer::Flush(double t_timeout /*=1e9*/)
{
    if (m_backend)
        m_backend->Flush(t_timeout);
}
