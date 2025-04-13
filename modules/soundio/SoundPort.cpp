//============================================================================
//
//  SoundPort -- Common base class for player and recorder classes
//               Wraps PortAudio stream management
//               Adds thread safe status inquiries
//
//  Copyright (c) 2006-2022 Erik Persson
//
//============================================================================

#include "SoundPort.h"
#include <portaudio.h>
#include <stdio.h>
#include <time.h>

//----------------------------------------------------------------------------

SoundPort::SoundPort()
{
}

//----------------------------------------------------------------------------

SoundPort::~SoundPort()
{
    CloseStream();
    FinishPortaudio();
}

//----------------------------------------------------------------------------

void SoundPort::ReportPortaudioError(const char *msg, PaError err)
{
    if (err == paNoError)
        fprintf( stderr, "%s\n", msg);
    else
        fprintf( stderr, "%s: %s\n", msg, Pa_GetErrorText( err ) );
}

//----------------------------------------------------------------------------

bool SoundPort::InitPortaudio()
{
    if (!m_portaudio_initialized)
    {
        PaError err = Pa_Initialize();
        if( err == paNoError )
            m_portaudio_initialized= true;
        else
            ReportPortaudioError("Could not intialize portaudio", err);
    }

    return m_portaudio_initialized;
}

//----------------------------------------------------------------------------

void SoundPort::FinishPortaudio()
{
    if (m_portaudio_initialized)
    {
        Pa_Terminate();
        m_portaudio_initialized= false;
    }
}

//----------------------------------------------------------------------------

// Open stream and return true if successful
bool SoundPort::OpenStream(
    bool output,
    PaSampleFormat sample_format,
    double sample_rate_hz,
    int samples_per_chunk)
{
    if (m_stream)
        return true; // already open

    if (!InitPortaudio())
        return false;

    // Open a stream for mono output
    PaDeviceIndex dev =
        output ?
        Pa_GetDefaultOutputDevice() :
        Pa_GetDefaultInputDevice();
    if (dev == paNoDevice)
    {
        ReportPortaudioError("No audio device found", paNoError);
        return false;
    }

    const PaDeviceInfo *info = Pa_GetDeviceInfo( dev );
    if (info == NULL)
    {
        ReportPortaudioError("Could not get portaudio device info", paNoError);
        return false;
    }

    PaStreamParameters params;
    params.device = dev;
    params.channelCount = 1;       // mono output
    params.sampleFormat = sample_format;
    params.suggestedLatency = output ? info->defaultLowOutputLatency : info->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = NULL;

    PaError err = Pa_OpenStream(
        &m_stream,
        output ? 0 : &params, // input parameters
        output ? &params : 0, // output parameters
        sample_rate_hz,
        samples_per_chunk,
        paClipOff,
        StreamCallback,
        this );

    if ( err != paNoError )
    {
        ReportPortaudioError("Could not open portaudio stream", err);
        m_stream = 0;
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------

// This routine will be called by the PortAudio engine when audio is needed.
// It may called at interrupt level on some machines so don't do anything
// that could mess up the system like calling malloc() or free().
//static
int SoundPort::StreamCallback(
    const void *input_buffer,
    void *output_buffer,
    unsigned long frames_per_buffer,
    const PaStreamCallbackTimeInfo *time_info,
    PaStreamCallbackFlags status_flags,
    void *user_data )
{
    SoundPort *port = (SoundPort *) user_data;

    return port->OnChunk(
        input_buffer,
        output_buffer,
        frames_per_buffer,
        time_info,
        status_flags);
}

//----------------------------------------------------------------------------

// Start portaudio stream
// This must be callable from different thread than GetElapsedTime()
bool SoundPort::StartStream()
{
    if (m_stream && !m_stream_started)
    {
        PaError err = Pa_StartStream( m_stream );
        if (err != paNoError)
        {
            ReportPortaudioError("Could not start portaudio stream", err);
            return false;
        }
        m_start_time = GetCurrentTime();
        m_stream_started = true;
    }
    return m_stream_started;
}

//----------------------------------------------------------------------------
// Stop portaudio stream
// This will play all buffers processed in callback
void SoundPort::StopStream()
{
    if (m_stream)
    {
        // Stop after playing all buffers processed in callback
        PaError err = Pa_StopStream( m_stream );
        if (err != paNoError)
            ReportPortaudioError("Could not stop portaudio stream", err);
        m_stream_started = false;
    }
}

//----------------------------------------------------------------------------

// Close portaudio stream
void SoundPort::CloseStream()
{
    if (m_stream)
    {
        PaError err = Pa_CloseStream( m_stream );
        if (err != paNoError)
            ReportPortaudioError("Could not close portaudio stream", err);
        m_stream= 0;
        m_stream_started = false;
    }
}

//----------------------------------------------------------------------------

// Return true after successful StartStream up to StopStream or CloseStream
// This can be called from any thread
bool SoundPort::IsStreamStarted() const
{
    return m_stream_started;
}

//----------------------------------------------------------------------------

// Get the the stream was started
// This can be called from any thread
double SoundPort::GetStartTime() const
{
    return m_start_time;
}

//----------------------------------------------------------------------------

// Get current wall clock time in seconds
// This can be called from any thread
double SoundPort::GetCurrentTime() const
{
    // We don't use Pa_GetStreamTime since it is not clearly thread safe.
    // Also, we use clock_gettime rather than gettimeofday so that
    // we're unnaffected by user changing clock.
    struct timespec tv;
    clock_gettime(CLOCK_REALTIME, &tv);
    return tv.tv_nsec*1e-9 + tv.tv_sec;
}
