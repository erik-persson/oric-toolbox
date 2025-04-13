//============================================================================
//
//  Sound - waveform representation
//
//  * Waveform representation
//  * Partially in memory, can dynamically load from disk
//  * Simple interface to audio file read/write.
//  * Reference counted, using atomic operations
//  * Thread safe interface
//  * Copy on write
//  * Stereo-to-mono conversion
//
//  Copyright (c) 2005-2022 Erik Persson
//
//============================================================================

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

class SoundBackend;
class SoundReader;

//----------------------------------------------------------------------------

class Sound
{
    SoundBackend *m_backend = 0;

    void SetBackend(SoundBackend *data);

public:
    // Constructors and destructor
    Sound();
    Sound(SoundBackend *data);
    Sound(const float *buf, int64_t len, int sample_rate);
    Sound(int64_t len, int sample_rate);
    Sound(const Sound& other);
    Sound(Sound&& other);
    Sound(SoundReader&& other);
    virtual ~Sound();

    // Assignment operators
    Sound& operator=(const Sound& other);
    Sound& operator=(Sound&& other);

    // Properties
    int64_t GetLength() const; // Length in samples
    int GetSampleRate() const; // Sample rate in Hz
    double GetDuration() const; // Duration in seconds

    // Check if sound is in a usable state
    // This returns false after default construction, and true after
    // successful ReadFromFile
    bool IsOk() const;

    // Data access
    // Read is callable from any thread, except that Write
    // must not be called simultaneously from a different thread.
    bool Read(int64_t where, float *buf, int samples) const;
    bool Read(int64_t where, short *buf, int samples) const;
    bool Write(int64_t where, const float *buf, int samples);

    // Direct buffer access. Will convert to a MemSound.
    float *GetBuffer();

    // Read from file
    // Only header is read during this call, data reads are deferred
    bool ReadFromFile(const char *path, bool silent = false);

    // Write to file as .wav
    bool WriteToFile(const char *path) const;

    // Cut out a part of the sound
    void Clip(double skip_seconds, double max_seconds);

    // Downsample by an integer factor
    void Downsample(int down_factor);

    // Mix with outher sound
    void Mix(const Sound& sound1, double proportion); // 0=only sound 0(this) 1= only sound 1
};

#endif // SOUND_H
