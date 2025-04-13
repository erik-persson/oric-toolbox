//============================================================================
//
//  SoundPort -- Common base class for player and recorder classes
//               Wraps PortAudio stream management
//               Adds thread safe status inquiries
//
//  Copyright (c) 2006-2022 Erik Persson
//
//============================================================================

#ifndef SOUNDPORT_H
#define SOUNDPORT_H

#include <portaudio.h>
#include <atomic>

class SoundPort
{
private:
    bool m_portaudio_initialized = false;
protected:
    PaStream *m_stream = 0;

    // These are made atomic so status inquiries can be made from any thread
    std::atomic<double> m_start_time = 0;
    std::atomic<bool> m_stream_started = false;

public:
    SoundPort();
    virtual ~SoundPort();

protected:
    bool InitPortaudio();
    void FinishPortaudio();
    void ReportPortaudioError(const char *msg, PaError err);

    // Portaudio stream management
    // Not thread safe
    bool OpenStream(
        bool output,
        PaSampleFormat sample_format,
        double sample_rate_hz,
        int samples_per_chunk);
    bool StartStream();
    void StopStream();
    void CloseStream();

    // Thread safe status inquiry
public:
    // Check if the stream was started (and not then stopped).
    bool IsStreamStarted() const;

    // Get the time the stream was started
    double GetStartTime() const;

    // Get the current wall clock time
    double GetCurrentTime() const;

private:
    // Static trampoline to call OnCunk
    static int StreamCallback(
        const void *input_buffer,
        void *output_buffer,
        unsigned long frames_per_buffer,
        const PaStreamCallbackTimeInfo *time_info,
        PaStreamCallbackFlags status_flags,
        void *user_data );

    // Handler called to transfer a chunk of audio data
    virtual int OnChunk(
        const void *input_buffer,
        void *output_buffer,
        unsigned long frames_per_buffer,
        const PaStreamCallbackTimeInfo *time_info,
        PaStreamCallbackFlags status_flags) = 0;
};

#endif // SOUNDPORT_H
