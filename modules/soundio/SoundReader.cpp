//----------------------------------------------------------------------------
//
// SoundReader implementation
//
// Copyright (c) 2005-2022 Erik Persson
//
// HAVE_LIBMPG123 -- when set, uses libmpg123
//
//----------------------------------------------------------------------------

#include "SoundReader.h"
#include <sys/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <sndfile.h>
#include <utility>

//----------------------------------------------------------------------------
// Base class for different format readers
//----------------------------------------------------------------------------

class SoundReaderBackend
{
protected:
    // Pararmeters set by Open()
    int m_sample_rate_hz = 0;
    int m_channel_cnt = 0;
    int64_t m_length = 0;
    bool m_seekable = false;
    int m_block_size = 0; // Preferred block size for reading, in samples

public:
    SoundReaderBackend() {}
    SoundReaderBackend(const SoundReaderBackend& other) = delete;
    virtual ~SoundReaderBackend() {}

    int GetSampleRate() const { return m_sample_rate_hz; }
    int GetChannelCnt() const { return m_channel_cnt; }
    int64_t GetLength() const { return m_length; }
    bool IsSeekable() const { return m_seekable; };
    int GetBlockSize() const  { return m_block_size; }

    // Attempt to open file for reading.
    // If silent flag is set, do not print error message,
    // this is useful when trying multiple formats
    // Return false if file format is wrong.
    virtual bool Open(const char *path, bool silent) = 0;

    // This may fail on non-seekable format
    // It may also fail on I/O error
    virtual bool SetReadPos(int64_t pos) = 0;

    // Read from current position
    virtual bool Read(short *buf, int cnt) = 0;
};

//----------------------------------------------------------------------------
// SndfileReader
//----------------------------------------------------------------------------
// wav, aiff etc unsing libsndfile

class SndfileReader : public SoundReaderBackend
{
    SF_INFO m_info;
    SNDFILE *m_sf;
    int64_t m_filepos;
    short *m_skipbuf;
    int m_skipbuf_size;

public:
    SndfileReader();
    virtual ~SndfileReader();

    bool Open(const char *path, bool silent) override;
    bool SetReadPos(int64_t pos) override;
    bool Read(short *buf, int cnt) override;
};

//----------------------------------------------------------------------------

SndfileReader::SndfileReader()
{
    memset(&m_info,0,sizeof(m_info));
    m_sf = 0;
    m_filepos = 0;
    m_skipbuf = 0;
    m_skipbuf_size = 0;
}

//----------------------------------------------------------------------------

SndfileReader::~SndfileReader()
{
    if (m_sf)
    {
        (void) sf_close(m_sf);
        m_sf= 0;
    }

    delete[] m_skipbuf;
}

//----------------------------------------------------------------------------

bool SndfileReader::Open(const char *path, bool silent)
{
    memset(&m_info,0,sizeof(m_info));
    m_filepos = 0;
    m_sf = sf_open(path, SFM_READ, &m_info);
    if (!m_sf)
    {
        if (!silent)
            fprintf(stderr,"Could not open sound file %s: %s\n", path, sf_strerror(NULL));
        return false;
    }

    m_length = m_info.frames*m_info.channels;
    m_sample_rate_hz = m_info.samplerate;
    m_channel_cnt = m_info.channels;
    m_seekable = m_info.seekable;

    // libsndfile requires that we read full tuples.
    // Use a reasonably large multiple of that, but small enough to fit in caches.
    m_block_size = 2048*m_channel_cnt;

    return true;
}

//----------------------------------------------------------------------------

// Seek to a absolute position
bool SndfileReader::SetReadPos(int64_t pos)
{
    assert(pos % m_channel_cnt == 0);

    if (m_info.seekable)
    {
        int64_t distance_in_samples = pos - m_filepos;
        if (distance_in_samples == 0)
            return true;

        m_filepos = m_info.channels*sf_seek(m_sf, distance_in_samples/m_info.channels, SEEK_CUR);
        return true;
    }

    return false;
}

//----------------------------------------------------------------------------

// 'cnt is counted in scalar samples (not tuples)
bool SndfileReader::Read(short *buf, int cnt)
{
    assert(cnt % m_channel_cnt == 0);

    if (cnt==0)
        return true;

    int got_cnt= sf_read_short(m_sf, buf, cnt);
    if (got_cnt >= 0)
        m_filepos += got_cnt;

    return got_cnt == cnt;
}

//----------------------------------------------------------------------------
// Mp3Reader
//----------------------------------------------------------------------------
// mp3 reader using libmpg123

#ifdef HAVE_LIBMPG123

#include "mpg123.h"

static int g_mpg123_init_cnt = 0;

class Mp3Reader : public SoundReaderBackend
{
    mpg123_handle *m_handle;
    int64_t m_seekpos;         // initialized by Open(), updated by Read()

public:
    Mp3Reader();
    virtual ~Mp3Reader();

    bool Open(const char *path, bool silent) override;
    bool SetReadPos(int64_t pos) override;
    bool Read(short *buf, int cnt) override;
};

//----------------------------------------------------------------------------

Mp3Reader::Mp3Reader()
{
    // Init the library unless it's done already
    if (!(g_mpg123_init_cnt++))
    {
        int err = mpg123_init();
        assert(err == MPG123_OK);
    }

    m_handle = mpg123_new(NULL, NULL);
    m_seekpos = 0;
    assert(m_handle != NULL);
}

//----------------------------------------------------------------------------

Mp3Reader::~Mp3Reader()
{
    if (m_handle)
    {
        mpg123_close(m_handle);
        mpg123_delete(m_handle);
    }

    // Exit the library if we're the last user
    assert(g_mpg123_init_cnt);
    if (!(--g_mpg123_init_cnt))
        mpg123_exit();
}

//----------------------------------------------------------------------------

bool Mp3Reader::Open(const char *path, bool silent)
{
    if (!m_handle)
        return false;

    int err;
    err = mpg123_open(m_handle, (char *) path);

    long rate = 0;
    int encoding;
    if (err == MPG123_OK)
        err = mpg123_getformat(m_handle, &rate, &m_channel_cnt, &encoding);
    m_sample_rate_hz = rate;

    m_seekable = true;

    if (err != MPG123_OK)
    {
        if (!silent)
            fprintf(stderr,"Could not open sound file %s: %s\n", path, mpg123_strerror(m_handle));
        return false;
    }

    // libmpg123 defaults to this setting, just check it for paranoia
    assert(encoding == MPG123_ENC_SIGNED_16);

    // This is from mpg123_to_wav.c
    // Ensure that this output format will not change (it could, when we allow it)
    mpg123_format_none(m_handle);
    mpg123_format(m_handle, rate, m_channel_cnt, encoding);

    mpg123_scan(m_handle); // must do this before mpg123_length, or it's an estimate
    m_length = (int64_t) mpg123_length(m_handle);

    // Use the libmpg123 preferred buf size
    m_block_size = mpg123_outblock(m_handle)/sizeof(short);

    // We should now be at the beginning of the file
    m_seekpos = 0;
    return true;
}

//----------------------------------------------------------------------------

// pos is counted in samples, should be multiple of the channel count
bool Mp3Reader::SetReadPos(int64_t pos)
{
    assert(m_handle);

    // Seek if necessary
    if (m_seekpos != pos)
    {
        // The seek unit of mpg123_seek is tuples.
        // This has been confirmed empirically.
        assert(pos % m_channel_cnt == 0);
        off_t result = mpg123_seek(m_handle, pos/m_channel_cnt, SEEK_SET);

        if (m_channel_cnt*result != pos)
        {
            fprintf( stderr, "mpg123 seek error: %s\n", mpg123_strerror(m_handle));
            return false;
        }
        m_seekpos = pos;
    }
    return true;
}

//----------------------------------------------------------------------------

// cnt is counted in samples, should be multiple of the channel count
bool Mp3Reader::Read(short *buf, int cnt)
{
    size_t done_bytes;
    int err = mpg123_read( m_handle, (uint8_t *) buf, cnt*sizeof(short),
        &done_bytes );
    if ( err != MPG123_OK && err == MPG123_DONE)
    {
        fprintf( stderr, "mpg123 read error: %s\n", mpg123_strerror(m_handle));
        return false;
    }

    if (done_bytes != cnt*sizeof(short))
    {
        fprintf( stderr, "mpg123 read error: expected %d bytes, got %d\n",
             (int) (cnt*sizeof(short)), (int) done_bytes);
        return false;
    }

    m_seekpos += cnt;
    return true;
}

#endif // HAVE_LIBMPG123

//----------------------------------------------------------------------------
// SoundReader front-end implementation
//----------------------------------------------------------------------------

// Move-construct by taking over existing object
SoundReader::SoundReader(SoundReader&& other)
{
    *this = std::move(other);
}

//----------------------------------------------------------------------------

// Move-assign by taking over existing object
SoundReader& SoundReader::operator=(SoundReader&& other)
{
    // No risk of self-assignment since other is temporary.
    delete m_backend;
    delete[] m_block_buf;

    // Take over everything from the other object
    m_backend = other.m_backend;
    m_block_size = other.m_block_size;
    m_block_cnt = other.m_block_cnt;
    m_block_no = other.m_block_no;
    m_block_buf = other.m_block_buf;
    m_read_pos = other.m_read_pos;

    // Clear other object
    other.m_backend = 0;
    other.m_block_size = 0;
    other.m_block_cnt = 0;
    other.m_block_no = -1;
    other.m_block_buf = 0;
    other.m_read_pos = 0;
    return *this;
}

//----------------------------------------------------------------------------

SoundReader::~SoundReader()
{
    delete m_backend;
    delete[] m_block_buf;
}

//----------------------------------------------------------------------------

int SoundReader::GetSampleRate() const
{
    return m_backend ? m_backend->GetSampleRate() : 0;
}

//----------------------------------------------------------------------------

int SoundReader::GetChannelCnt() const
{
    return m_backend? m_backend->GetChannelCnt() : 0;
}

//----------------------------------------------------------------------------

int64_t SoundReader::GetLength() const
{
    return m_backend? m_backend->GetLength() : 0;
}

//----------------------------------------------------------------------------

void SoundReader::Start()
{
    // no effect on offline file
}

//----------------------------------------------------------------------------

void SoundReader::Stop()
{
    // no effect on offline file
}

//----------------------------------------------------------------------------

bool SoundReader::IsRunning() const
{
    return false;
}

//----------------------------------------------------------------------------

// Return no. of seconds recording has run
double SoundReader::GetElapsedTime() const
{
    // This is mainly for a live recorder, not so meaningful to us
    // Act as if we recorded and stopped
    return ((double) GetLength())/GetSampleRate();
}

//----------------------------------------------------------------------------

// Check where we are currently reading
int64_t SoundReader::GetReadPos() const
{
    return m_read_pos;
}

//----------------------------------------------------------------------------

// Seek to a new position for reading
bool SoundReader::SetReadPos(int64_t pos)
{
    // Do not pretend to be seekable when format-reader is not
    if (!m_backend || !m_backend->IsSeekable())
        return false;

    assert(pos >= 0 && pos <= GetLength());

    m_read_pos = pos;
    return true; // seeking is supported
}

//----------------------------------------------------------------------------

// Read block into buffer, return pointer
short *SoundReader::GetBlock(int block_no)
{
    assert( block_no >= 0 && block_no < m_block_cnt);
    if (m_block_no == block_no)
        return m_block_buf;

    // Read the block
    int size = m_block_size;
    int64_t start = ((int64_t) block_no) * m_block_size;
    if (block_no == m_block_cnt-1)
        size = GetLength() - start; // last block is smaller

    assert(m_backend);
    if (m_backend->IsSeekable())
    {
        if (!m_backend->SetReadPos(start))
        {
            // Fail
            m_block_no = -1;
            return 0;
        }
    }
    // else make no attempt to seek

    if (m_backend->Read(m_block_buf, size))
    {
        m_block_no = block_no;
        return m_block_buf;
    }

    // Fail
    m_block_no = -1;
    return 0;
}

//----------------------------------------------------------------------------

// Read from buffer
// 'cnt' is counted in scalar samples (not tuples)
// Should be multiple of the no. of channels
bool SoundReader::Read(short *buf, int cnt)
{
    if (cnt == 0)
        return true;
    if (!m_backend)
        return false;
    assert(cnt >= 0);

    auto length = GetLength();
    auto block_size = m_block_size;

    while (cnt>0)
    {
        assert(m_read_pos >= 0 && m_read_pos < length);
        assert(m_read_pos+cnt <= length);

        int block_no= m_read_pos/block_size;
        int64_t block_start = ((int64_t) block_no)*block_size;
        int64_t block_end = block_start + block_size;

        int do_cnt = block_end - m_read_pos;
        if (do_cnt>cnt) do_cnt = cnt;

        short *block = GetBlock(block_no);
        if (!block)
            return false;

        memcpy(buf, block + (m_read_pos - block_start), do_cnt*sizeof(short));

        m_read_pos += do_cnt;
        buf += do_cnt;
        cnt -= do_cnt;
    }

    return true;
}

//----------------------------------------------------------------------------

// Read from buffer
// 'cnt' is counted in scalar samples (not tuples)
// Should be multiple of the no. of channels
bool SoundReader::Read(float *buf, int cnt)
{
    if (cnt == 0)
        return true;
    if (!m_backend)
        return false;

    assert(cnt >= 0);

    auto length = GetLength();
    auto block_size = m_block_size;

    while (cnt>0)
    {
        assert(m_read_pos >= 0 && m_read_pos < length);
        assert(m_read_pos+cnt <= length);

        int block_no = m_read_pos/block_size;
        int64_t block_start = ((int64_t) block_no)*block_size;
        int64_t block_end = block_start + block_size;

        int do_cnt = block_end - m_read_pos;
        if (do_cnt>cnt) do_cnt = cnt;

        short *block = GetBlock(block_no);
        if (!block)
            return false;

        float k= 1.0/32768; // Convert to +-1 range like portaudio does
        for (int i=0; i<do_cnt; i++)
            buf[i] = k*block[m_read_pos - block_start + i];

        m_read_pos += do_cnt;
        buf += do_cnt;
        cnt -= do_cnt;
    }

    return true;
}

//----------------------------------------------------------------------------

// Check how many samples are immediately available for reading
// without waiting for recording to progress further
int SoundReader::GetReadAvail() const
{
    int64_t avail = GetLength() - GetReadPos();
    return avail<INT_MAX ? (int) avail : INT_MAX;
}

//----------------------------------------------------------------------------

bool SoundReader::Open(const char *path, bool silent /*=false*/)
{
    // Allow multiple calls to open - discard previous state
    delete m_backend;
    m_backend = 0;
    m_read_pos = 0;

    // Check if file exists
    struct stat st;
    if (stat(path,&st))
    {
        perror(path);
        return false;
    }

    bool open_as_mp3 = false;
    int len= strlen(path);
    if (len >= 4 && path[len-4]=='.' &&
            tolower(path[len-3])=='m' &&
            tolower(path[len-2])=='p' &&
            tolower(path[len-1])=='3')
    {
        open_as_mp3 = true;
    }

    SoundReaderBackend *backend;
    if (open_as_mp3)
    {
#ifdef HAVE_LIBMPG123
        backend = new Mp3Reader();
#else
        if (!silent)
            fprintf(stderr,"Could not open sound file %s: MP3 not supported\n", path);
        return 0;
#endif
    }
    else
        backend = new SndfileReader();

    // Call the format-specific Open
    if (backend->Open(path, silent))
    {
        m_backend = backend;

        auto length = m_backend->GetLength();

        m_block_size = m_backend->GetBlockSize();
        if (m_block_size > length)
            m_block_size = length;
        m_block_cnt = (length + m_block_size-1)/m_block_size;
        if (length)
            m_block_buf = new short[m_block_size];
    }
    else
    {
        // Print no error message here, that is done in the format-specific Open
        delete backend;
    }
    return m_backend != 0;
}

//----------------------------------------------------------------------------

void SoundReader::Close()
{
    delete m_backend;
    m_backend = 0;
}
