//============================================================================
//
//  Sound implementation
//
//  See header file for overview of functionality.
//
//  Copyright (c) 2005-2022 Erik Persson
//
//============================================================================

#include "Sound.h"
#include "SoundReader.h"
#include "SoundWriter.h"
#include "Downsampler.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <tgmath.h>
#include <assert.h>
#include <utility>
#include <atomic>
#include <mutex>

//----------------------------------------------------------------------------

// Combine multiple (stereo) channels to one by averaging
static void average_channels(short *dst, int dstlen, const short *src, int channels)
{
    for (int i=0; i<dstlen; i++)
    {
        int sum = 0;
        for (int j= 0; j<channels; j++)
            sum += src[i*channels+j];

        dst[i] = sum/channels;
    }
}

//----------------------------------------------------------------------------
// SoundBackend
//----------------------------------------------------------------------------
// Shared reference counted object, referred to by Sound objects

class SoundBackend
{
private:
    std::atomic<int> m_ref_cnt = 0;

protected:
    int m_sample_rate; // Sample rate in Hz
    int64_t m_length;  // Length in samples

public:
    SoundBackend() {}
    virtual ~SoundBackend() {}

    // Read reference count
    // Callable from any thread
    int GetRefCount() const { return m_ref_cnt; }

    // Increase reference count
    // Callable from any thread
    void Retain() { m_ref_cnt++; }

    // Decrease reference count and delete object if count becomes zero
    // Callable from any thread
    void Release() { if (!(--m_ref_cnt)) delete this; }

    // Sample rate in Hz
    // Callable from any thread
    virtual int GetSampleRate() const { return m_sample_rate; };

    // Length in samples
    // Callable from any thread
    virtual int64_t GetLength() const { return m_length; };

    // Entry points for reading, callable from any thread
    // Floating point variety
    virtual bool Read(int64_t where, float *buf, int samples) const = 0;

    // Truncation to shorts
    virtual bool Read(int64_t where, short *buf, int samples) const;
};

//----------------------------------------------------------------------------

// Truncate to shorts. Currently used by the SoundPlayer object.
bool SoundBackend::Read(int64_t where, short *buf, int samples) const
{
    float fbuf[samples];
    if (Read(where,fbuf,samples)) // expected range -1..1
    {
        for (int i=0; i<samples; i++)
        {
            // Multiply by 32768 and clip to 16-bit range -32768..32767
            double val= 32768*fbuf[i];
            if (val>32767)
                buf[i] = 32767;
            else if (fbuf[i]<-32768)
                buf[i] = -32768;
            else
                buf[i] = (short) val;
        }
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// FileBackend
//----------------------------------------------------------------------------
// * Uses a SoundReader
// * Converts stereo=>mono
//   uses a one-second buffer for this
// * Caches one-second blocks that have been read already
// * Pads with zeros
//----------------------------------------------------------------------------

class FileBackend : public SoundBackend
{
protected:
    mutable SoundReader m_reader;
    mutable std::mutex m_mutex;

    // Block cache
    int m_block_size = 0;
    int m_block_cnt = 0;
    std::atomic<short *> *m_blocks = 0;

    // Buffer for stereo-to-mono conversion
    short *m_stereo_buf = 0;

public:
    FileBackend(SoundReader&& reader);
    FileBackend(const FileBackend&) = delete;
    virtual ~FileBackend();

    // Retrieve a pointer to a cached block of audio
    short *GetBlock(int block_no) const;

    // Read via cache, callable from any thread
    bool ReadFromCache(int64_t where, short *buf, int samples) const;

    // Entry points for reading, callable from any thread
    bool Read(int64_t where, short *buf, int samples) const override;
    bool Read(int64_t where, float *buf, int samples) const override;
};

//----------------------------------------------------------------------------

FileBackend::FileBackend(SoundReader&& reader) : m_reader( std::move(reader) )
{
    int channels = m_reader.GetChannelCnt();

    m_sample_rate = m_reader.GetSampleRate();
    m_length = m_reader.GetLength()/channels; // since we convert to mono

    // Block cache
    if (m_length)
    {
        m_block_size = m_sample_rate; // 1 second blocks
        if (m_block_size>m_length)
            m_block_size = m_length;

        m_block_cnt = (m_length + m_block_size-1)/m_block_size;
        m_blocks= new std::atomic<short *>[m_block_cnt];
        for (int i=0; i<m_block_cnt; i++)
            m_blocks[i] = 0;

        // Stereo-to-mono buffer
        if (channels>1)
        {
            // Keep a buffer for 1 stereo block
            m_stereo_buf = new short[m_block_size*channels];
        }
    }
}

//----------------------------------------------------------------------------

FileBackend::~FileBackend()
{
    // Delete block cache
    for (int i=0; i<m_block_cnt; i++)
        delete[] m_blocks[i];
    delete[] m_blocks;

    // Delete stereo-to-mono buffer
    delete[] m_stereo_buf;
    m_stereo_buf = 0;
}

//----------------------------------------------------------------------------

// Retrieve a pointer to a cached block
// Read from file, stereo-to-mono, and cache.
// Callable from any thread.
short *FileBackend::GetBlock(int block_no) const
{
    assert( block_no >= 0 && block_no < m_block_cnt);

    // Cache lookup
    // First do a quick check before locking mutex.
    if (m_blocks[block_no]) // atomic read
        return m_blocks[block_no]; // already in cache

    // Block needs to be fetched
    m_mutex.lock();

    // Check again, so that nobody fetched it just while we got the mutex
    if (m_blocks[block_no]) // atomic read
    {
        m_mutex.unlock();
        return m_blocks[block_no]; // already in cache
    }

    int64_t length = m_reader.GetLength();
    int channels= m_reader.GetChannelCnt();

    // Attempt to seek
    // Some file formats may not support seeking.
    // In that case we read all the blocks from the beginning.
    (void) m_reader.SetReadPos( ((int64_t) block_no) * m_block_size * channels  );
    int64_t at_pos = m_reader.GetReadPos()/channels;
    int at_block_no = at_pos/m_block_size;
    assert(((int64_t) at_block_no) * m_block_size == at_pos);

    while (at_block_no <= block_no)
    {
        if (m_blocks[at_block_no])
            continue; // already fetched

        // Allocate block buffer
        int size = m_block_size;
        if (size > length - at_pos)
            size = length - at_pos; // last block is smaller
        m_blocks[at_block_no] = new short[size];

        // Read from file
        bool ok = false;
        if (channels==1)
            // Mono already, no conversion needed
            ok = m_reader.Read(m_blocks[at_block_no], size);
        else
        {
            // Read first to stereo buffer, then convert to mono
            ok = m_reader.Read(m_stereo_buf, size*channels);
            if (ok)
                average_channels(m_blocks[at_block_no], size, m_stereo_buf, channels);
        }

        if (!ok)
        {
            delete[] m_blocks[at_block_no];
            m_blocks[at_block_no] = 0;
        }
        at_block_no++;
        at_pos += m_block_size;
    }
    m_mutex.unlock();

    return m_blocks[block_no];
}

//----------------------------------------------------------------------------

// Read via cache: Read from file, stereo-to-mono, cache
// Callable from any thread
bool FileBackend::ReadFromCache(int64_t where, short *buf, int cnt) const
{
    int64_t length = GetLength();

    while (cnt>0)
    {
        assert(where >= 0 && where < length);
        assert(where+cnt <= length);

        int block_no= where/m_block_size;
        int64_t block_start = ((int64_t) block_no)*m_block_size;
        int64_t block_end = block_start + m_block_size;

        int do_cnt = block_end - where;
        if (do_cnt>cnt) do_cnt = cnt;

        short *block = GetBlock(block_no);
        if (!block)
            return false;

        memcpy(buf, block + (where - block_start), do_cnt*sizeof(short));

        where += do_cnt;
        buf += do_cnt;
        cnt -= do_cnt;
    }

    return true;
}

//----------------------------------------------------------------------------

// Entry point for reading, 16-bit variant
// Read from file, stereo-to-mono, cache and pad
// Callable from any thread
bool FileBackend::Read(int64_t where, short *buf, int samples) const
{
    // Add padding to the left
    while (where<0 && samples>0)
    {
        *buf++= 0;
        where++;
        samples--;
    }

    // Add padding to the right
    int64_t length = GetLength();
    while (samples>0 && where+samples > length)
    {
        buf[--samples]= 0;
    }

    // Exit if remainder is empty
    if (samples==0)
        return true;

    // Read interior portion of sound, from cache
    return ReadFromCache(where, buf, samples);
}

//----------------------------------------------------------------------------

// Entry point for reading, float variant
// Read from file, stereo-to-mono, cache, pad, convert to float
// Callable from any thread
bool FileBackend::Read(int64_t where, float *buf, int cnt) const
{
    // Convert in chunks, so buffer can be on stack
    const int sbufsize = 2048;
    short sbuf[sbufsize];
    bool ok = true;

    for (int dx= 0; dx<cnt; dx += sbufsize)
    {
        int remain = cnt-dx;
        int chunk = sbufsize<remain ? sbufsize:remain;
        if (!Read(where+dx, sbuf, chunk))
            ok = false;

        float k= 1.0/32768; // Convert to +-1 range like portaudio does
        for (int i=0; i<chunk; i++)
            buf[dx+i]= k*sbuf[i];
    }
    return ok;
}

//----------------------------------------------------------------------------
// MemBackend - Sound data that is stored in primary memory.
//----------------------------------------------------------------------------

class MemBackend : public SoundBackend
{
    float *m_buf;

public:
    MemBackend(const float *buf, int64_t len, int sample_rate);
    MemBackend(int64_t len, int sample_rate);
    virtual ~MemBackend();

    float *GetBuffer();

    bool Read(int64_t where, float *buf, int samples) const override;
};

//----------------------------------------------------------------------------

MemBackend::MemBackend(const float *buf, int64_t len, int sample_rate)
{
    assert(len >= 0);
    assert(sample_rate > 0);
    if (len)
    {
        m_buf= new float[len];
        for (int64_t i=0; i<len; i++) m_buf[i]= buf[i];
    }
    else
        m_buf= 0;
    m_length= len;
    m_sample_rate= sample_rate;
}

//----------------------------------------------------------------------------

MemBackend::MemBackend(int64_t len, int sample_rate)
{
    assert(len > 0);
    assert(sample_rate > 0);

    m_buf = new float[len];
    for (int64_t i=0; i<len; i++)
        m_buf[i] = 0;

    m_length = len;
    m_sample_rate = sample_rate;
}

//----------------------------------------------------------------------------

MemBackend::~MemBackend()
{
    delete[] m_buf;
}

//----------------------------------------------------------------------------

float *MemBackend::GetBuffer()
{
    return m_buf;
}

//----------------------------------------------------------------------------

// Entry point for reading
// Callable from any thread, except when Write interface is also used.
bool MemBackend::Read(int64_t where, float *buf, int samples) const
{
    for (int i=0; i<samples; i++)
    {
        buf[i]= (where>=0 && where<m_length? m_buf[where]:0);
        where++;
    }
    return true;
}

//----------------------------------------------------------------------------
// ClipBackend: Cut out a part of a sound
// Used to implement Sound::Clip()
//----------------------------------------------------------------------------

class ClipBackend : public SoundBackend
{
    Sound m_sound0;
    int64_t m_offset;

public:
    ClipBackend(const Sound& sound0, double skip_seconds, double max_seconds);
    virtual ~ClipBackend() {};

    bool Read(int64_t where, float *buf, int samples) const override;
};

//----------------------------------------------------------------------------

ClipBackend::ClipBackend(const Sound& sound0, double skip_seconds, double max_seconds) :
    m_sound0(sound0)
{
    int64_t maxlen= (int64_t) floor(0.5 + max_seconds*m_sound0.GetSampleRate());

    m_offset= (int64_t) floor(0.5 + skip_seconds*m_sound0.GetSampleRate());

    m_sample_rate = sound0.GetSampleRate();

    m_length = sound0.GetLength() - m_offset;
    if (maxlen >= 0 && m_length > maxlen)
        m_length= maxlen;
    if (m_length<0) m_length= 0;
}

//----------------------------------------------------------------------------

// Entry point for reading, callable from any thread
bool ClipBackend::Read(int64_t where, float *buf, int samples) const
{
    // Add padding to the left
    while (where<0 && samples>0)
    {
        *buf++= 0;
        where++;
        samples--;
    }

    // Add padding to the right
    int64_t length = GetLength();
    while (samples>0 && where+samples > length)
    {
        buf[--samples]= 0;
    }

    // Exit if remainder is empty
    if (samples==0)
        return true;

    // Read interior portion of sound
    return m_sound0.Read(m_offset+where, buf, samples);
}

//----------------------------------------------------------------------------
// DownsampleBackend : used to implement Sound::Downsample
//----------------------------------------------------------------------------

class DownsampleBackend : public SoundBackend
{
    Sound m_sound0;
    Downsampler m_downsampler;
    int m_down_factor;

public:
    DownsampleBackend(const Sound& sound0, int down_factor);
    virtual ~DownsampleBackend() {}

    bool Read(int64_t where, float *buf, int samples) const override;
};

//----------------------------------------------------------------------------

DownsampleBackend::DownsampleBackend(const Sound& sound0, int down_factor) :
    m_sound0(sound0),
    m_downsampler(down_factor)
{
    assert(down_factor>1);
    m_down_factor = down_factor;

    m_sample_rate = sound0.GetSampleRate()/down_factor;
    m_length = sound0.GetLength()/down_factor;
}

//----------------------------------------------------------------------------

// Entry point for reading, callable from any thread
bool DownsampleBackend::Read(int64_t where, float *buf, int samples) const
{
    int extra_samples= m_downsampler.GetExtraSamplesNeeded();
    int highlen= m_down_factor * samples + 2*extra_samples;
    float highbuf[highlen];

    if (m_sound0.Read(m_down_factor*where + extra_samples, highbuf,highlen))
    {
        m_downsampler.Downsample(buf,samples,highbuf,highlen,extra_samples);
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------
// MixBackend - Sound-file like object that mixes two sounds
//----------------------------------------------------------------------------

// Sound-file like object that mixes two sounds
// Deletes the two input sounds on destruction.
class MixBackend : public SoundBackend
{
    Sound m_sound0;
    Sound m_sound1;
    float m_k;

public:
    MixBackend(const Sound& sound0, const Sound& sound1,
               double proportion); // 0=only sound0, 1=only sound1
    virtual ~MixBackend() {}

    bool Read(int64_t where, float *buf, int samples) const override;
};

//----------------------------------------------------------------------------

MixBackend::MixBackend(const Sound& sound0, const Sound& sound1,
                       double proportion) :
    m_sound0(sound0),
    m_sound1(sound1)
{
    m_k = proportion;
    assert( proportion >= 0 && proportion <= 1);
    assert( m_sound0.GetSampleRate() == m_sound1.GetSampleRate() );
    assert( m_sound0.GetLength() == m_sound1.GetLength() );

    m_sample_rate = m_sound0.GetSampleRate();
    m_length = m_sound0.GetLength();
}

//----------------------------------------------------------------------------

// Entry point for reading, callable from any thread
bool MixBackend::Read(int64_t where, float *buf, int samples) const
{
    float tmp[samples];
    if (!m_sound0.Read(where,buf,samples))
        return 0;

    if (!m_sound1.Read(where,tmp,samples))
        return 0;

    for (int i=0; i<samples; i++)
        buf[i] += m_k*(tmp[i] - buf[i]);

    return true;
}

//----------------------------------------------------------------------------
// Sound class
//----------------------------------------------------------------------------

// Install new reference
void Sound::SetBackend(SoundBackend *data)
{
    // Do this first - data and m_backend might be the same
    if (data)
        data->Retain();

    if (m_backend)
        m_backend->Release();

    m_backend = data;
}

//----------------------------------------------------------------------------

// Null sound
Sound::Sound()
{
}

//----------------------------------------------------------------------------

// MemBackend constructor, initialized from buffer
Sound::Sound(const float *buf, int64_t len, int sample_rate)
{
    SetBackend( new MemBackend(buf, len, sample_rate) );
}

//----------------------------------------------------------------------------

// MemBackend constructor, initialized with zeros
Sound::Sound(int64_t len, int sample_rate)
{
    SetBackend( new MemBackend(len, sample_rate) );
}

//----------------------------------------------------------------------------

// Copy-construct by adding a reference to shared object
Sound::Sound(const Sound& other)
{
    SetBackend( other.m_backend );
}

//----------------------------------------------------------------------------

// Move-construct by taking over other Sound's data object
Sound::Sound(Sound&& other)
{
    m_backend = other.m_backend;
    other.m_backend = 0;
}

//----------------------------------------------------------------------------

// Move-construct by taking over existing SoundReader
Sound::Sound(SoundReader&& reader)
{
    SetBackend( new FileBackend( std::move(reader) ) );
}

//----------------------------------------------------------------------------

Sound::~Sound()
{
    if (m_backend)
        m_backend->Release();
}

//----------------------------------------------------------------------------

// Copy-assign by adding a reference to shared object
Sound& Sound::operator=(const Sound& other)
{
    SetBackend(other.m_backend);
    return *this;
}

//----------------------------------------------------------------------------

// Move-assign by taking over existing object
Sound& Sound::operator=(Sound&& other)
{
    // Cant have self-assignment since other is temporary.
    if (m_backend)
        m_backend->Release();
    m_backend = other.m_backend;
    other.m_backend = 0;
    return *this;
}

//----------------------------------------------------------------------------

int64_t Sound::GetLength() const
{
    if (!m_backend)
        return 0;
    return m_backend->GetLength();
}

//----------------------------------------------------------------------------

int Sound::GetSampleRate() const
{
    if (!m_backend)
        return 0;
    return m_backend->GetSampleRate();
}

//----------------------------------------------------------------------------

double Sound::GetDuration() const
{
    if (!m_backend)
        return 0;
    return ((double) m_backend->GetLength())/m_backend->GetSampleRate();
}

//----------------------------------------------------------------------------

bool Sound::IsOk() const
{
    return m_backend != 0;
}

//----------------------------------------------------------------------------

bool Sound::Read(int64_t where, float *buf, int samples) const
{
    if (!m_backend)
        return false;
    return m_backend->Read(where, buf, samples);
}

//----------------------------------------------------------------------------

bool Sound::Read(int64_t where, short *buf, int samples) const
{
    if (!m_backend)
        return false;
    return m_backend->Read(where, buf, samples);
}

//----------------------------------------------------------------------------

// Read from file
// Only header is read during this call, data reads are deferred
bool Sound::ReadFromFile(const char *path, bool silent)
{
    SoundReader reader;
    if (reader.Open(path, silent))
    {
        SetBackend(new FileBackend( std::move(reader) ));
        return true;
    }
    SetBackend(0);
    return false;
}

//----------------------------------------------------------------------------

// Write to file as .wav
bool Sound::WriteToFile(const char *path) const
{
    SoundWriter wr;
    if (!wr.Open(path,GetSampleRate()))
        return false;

    const int bufsize= 65536;
    float buf[bufsize];
    int64_t len= GetLength();
    for (int64_t offs=0; offs<len; offs+=bufsize)
    {
        int chunk_size= len-offs;
        if (chunk_size>bufsize) chunk_size= bufsize;
        if (!Read(offs,buf,chunk_size))
            return false;
        if (!wr.Write(buf,chunk_size))
            return false;
    }

    return true;
}

//----------------------------------------------------------------------------

// Get a writable pointer to the sound's buffer
// Convert to MemBackend.
// Make it an exclusive (non-shared) copy.
float *Sound::GetBuffer()
{
    assert(m_backend);

    MemBackend *ms = dynamic_cast<MemBackend *>(m_backend);
    if (ms == 0 || m_backend->GetRefCount() != 1)
    {
        // Either it wasn't a MemBackend or it wasn't exclusive.

        // Create a new MemBackend, with the same length and samplerate
        int64_t len = GetLength();
        int samplerate = GetSampleRate();
        ms = new MemBackend(len, samplerate);

        // Copy data from old to new oblect
        float *buffer = ms->GetBuffer();
        Read(0,buffer,len);

        // Install new reference
        SetBackend(ms);
    }

    assert(ms);
    assert(ms->GetRefCount() == 1);
    return ms->GetBuffer();
}

//----------------------------------------------------------------------------

// Cut out a part of a sound
void Sound::Clip(double skip_seconds, double max_seconds)
{
    assert(skip_seconds >= 0);

    double duration = GetDuration();
    if (skip_seconds > 0 || duration>max_seconds)
    {
        SetBackend( new ClipBackend(*this, skip_seconds, max_seconds) );
    }
}

//----------------------------------------------------------------------------

// Downsample by an integer factor
void Sound::Downsample(int down_factor)
{
    assert(down_factor >= 1);
    if (down_factor>1)
    {
        SetBackend( new DownsampleBackend(*this, down_factor) );
    }
}

//----------------------------------------------------------------------------

// Mix with outher sound
void Sound::Mix(const Sound& sound1, double proportion) // 0=only sound 0(this) 1= only sound 1
{
    SetBackend( new MixBackend(*this, sound1, proportion) );
}

//----------------------------------------------------------------------------

// Write - Modify a section of the sound
//
// This interface permits streaming file operation but the current
// implementation works will the full sound in memory.
//
bool Sound::Write(int64_t where, const float *buf, int samples)
{
    float *dst = GetBuffer(); // convert to in-memory sound
    int64_t len = GetLength();

    // Ignore writes to the left of the sound
    while (where<0 && samples>0)
    {
        where++;
        samples--;
    }

    // Ignore writes to the right of the sound
    while (samples>0 && where+samples>len)
        samples--;

    for (int i=0; i<samples; i++)
        dst[where+i] = buf[i];
    return true;
}
