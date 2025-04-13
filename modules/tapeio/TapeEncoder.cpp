//============================================================================
//
//  TapeEncoder for Oric tape format
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#include "TapeEncoder.h"
#include <soundio/SoundWriter.h>
#include <soundio/SoundPlayer.h>
#include <tgmath.h>
#include <assert.h>
#include <unistd.h>

//----------------------------------------------------------------------------

TapeEncoder::TapeEncoder()
{
    // Form a template m_ramp from 0.0 to 1.0
    float k = M_PI/RAMP_LEN;
    for (int i= 0; i<RAMP_LEN; i++)
        m_ramp[i] = .5 - .5 * cos(k*i);
}

//----------------------------------------------------------------------------

TapeEncoder::~TapeEncoder()
{
    Close();
}

//----------------------------------------------------------------------------

// Write out buffered samples to sound file or line out
void TapeEncoder::EmitFlush()
{
    if (m_buf_cnt && m_open && m_ok)
    {
        m_ok = m_sink->Write(m_buf, m_buf_cnt);
    }
    m_buf_cnt = 0;
}

//----------------------------------------------------------------------------

void TapeEncoder::EmitSample(float y)
{
    assert(m_buf_cnt < ENCODER_BUFSIZE);
    m_buf[m_buf_cnt++] = y;
    if (m_buf_cnt == ENCODER_BUFSIZE)
        EmitFlush();
}

//----------------------------------------------------------------------------

// Switch to value via cosine ramp
void TapeEncoder::RampTo(float y)
{
    float y0 = m_last_y;
    while (m_ramp_phase<RAMP_LEN)
    {
        float yy = y0 + m_ramp[m_ramp_phase]*(y-y0);
        EmitSample(yy);
        m_ramp_phase += RAMP_STEP;
    }
    m_ramp_phase -= RAMP_LEN;
    m_last_y = y;
}

//----------------------------------------------------------------------------

void TapeEncoder::EmitBit(bool val)
{
    // We use 60% of the available amplitude range
    RampTo(val ? 0.6 : -0.6);
    m_last_bit = val;
}

//----------------------------------------------------------------------------

// If no filename is given then output to speaker
bool TapeEncoder::Open(const char *opt_filename, bool slow)
{
    Close();
    m_slow = slow;

    if (opt_filename)
    {
        SoundWriter *writer = new SoundWriter;
        m_sink = writer;
        m_ok = m_open = writer->Open(opt_filename, ENCODER_RATE);
    }
    else
    {
        SoundPlayer *player = new SoundPlayer;
        m_sink = player;
        m_ok = m_open = player->Open(ENCODER_RATE);
    }
    m_last_y = 0;
    m_last_bit = false;
    m_ramp_phase = 0;
    return m_ok;
}

//----------------------------------------------------------------------------

bool TapeEncoder::EncodeBit(bool val)
{
    bool polarity = m_last_bit;
    if (m_slow)
    {
        for (int i= 0; i<16; i++)
        {
            bool y = val ? !(i&1) : !(i&2);
            EmitBit( y ^ polarity );
        }
    }
    else
    {
        EmitBit( !polarity );
        EmitBit( polarity );
        if (!val)
            EmitBit( polarity );
    }
    return m_ok;
}

//----------------------------------------------------------------------------

void TapeEncoder::EncodeByte(uint8_t byte)
{
    EncodeBit(false);    // start bit
    bool parity = true;
    for (int i=0; i<8; i++)
    {
        bool bit = (byte>>i)&1;
        EncodeBit(bit); // data bit
        parity ^= bit;
    }
    EncodeBit(parity);    // odd parity
    EncodeBit(true);      // stop bits
    EncodeBit(true);      // stop bits
    EncodeBit(true);      // stop bits
    EmitBit(!m_last_bit); // extra cycle
}

//----------------------------------------------------------------------------

// Finish thread launched by EncodeFile
void TapeEncoder::FinishEncode()
{
    if (m_enc_thread)
    {
        m_enc_thread->join();
        delete m_enc_thread;
        m_enc_thread = 0;
    }
}

//----------------------------------------------------------------------------

// Function which runs in background thread
void TapeEncoder::EncodeThread()
{
    for (auto c: m_inbuf)
        EncodeByte(c);
    m_inbuf.clear();
    RampTo(0);
    EmitFlush();
    m_sink->Flush(); // make sure player starts even if sound was short
}

//----------------------------------------------------------------------------

void TapeEncoder::CountBit(bool val)
{
    if (m_slow)
        m_put_phys_bits += 16;
    else
        m_put_phys_bits += val ? 2 : 3;
}

//----------------------------------------------------------------------------

void TapeEncoder::PutByte(uint8_t byte)
{
    // Finish previous encoding, if any
    FinishEncode();

    m_inbuf.push_back(byte);
    if (m_slow)
        m_put_phys_bits += 209;
    else
    {
        CountBit(false);    // start bit
        bool parity = true;
        for (int i=0; i<8; i++)
        {
            bool bit = (byte>>i)&1;
            CountBit(bit); // data bit
            parity ^= bit;
        }
        CountBit(parity);    // odd parity
        CountBit(true);      // stop bits
        CountBit(true);      // stop bits
        CountBit(true);      // stop bits
        m_put_phys_bits += 1; // extra cycle
    }
}

//----------------------------------------------------------------------------

// Encode bytestream stored in archive file
// Return false on input error
// (output errors are indicated by Close())
bool TapeEncoder::PutFile(const char *iname)
{
    // Finish previous encoding, if any
    FinishEncode();

    if (FILE *f = fopen(iname, "rb"))
    {
        // We expect a TAP file to start with three or more 0x16 followed by one 0x24.
        int c = fgetc(f);
        int n = 0;
        while (c == 0x16)
        {
            n++;
            c = fgetc(f);
        }
        if (c == 0x24 && n >= 3)
        {
            // Sync found - prolong to about 2/3 seconds if shorter.
            // If it's just three 0x16's we fail to decode it ourselves
            int nn = m_slow ? 15 : 99;
            if (n<nn)
                n= nn;

            while (n--)
                PutByte(0x16);
            // 0x24 gets put below
        }
        else
            fprintf(stderr, "Warning: Tape archive not introduced by standard sync\n");

        while (c != EOF)
        {
            PutByte(c);
            c = fgetc(f);
        }
        fclose(f);
        return true;
    }
    return false;
}

//----------------------------------------------------------------------------

// Start encoding from input buffer
void TapeEncoder::StartEncode()
{
    if (!m_enc_thread && m_inbuf.size())
    {
        m_enc_thread = new std::thread(&TapeEncoder::EncodeThread, this);
    }
}

//----------------------------------------------------------------------------

// Return true if file was written without errors
bool TapeEncoder::Close()
{
    StartEncode();
    FinishEncode();

    if (m_open)
    {
        m_sink->Close();
        m_open = false;
    }

    if (m_sink)
    {
        delete m_sink;
        m_sink = 0;
    }

    return m_ok;
}

//----------------------------------------------------------------------------

// Check how long the output is, in seconds
double TapeEncoder::GetDuration() const
{
    if (m_put_phys_bits == 0)
        return 0; // no ramping out in this case

    // A.k.a 1.0/4800
    double cycle_time = ((double) RAMP_LEN)/RAMP_STEP/ENCODER_RATE;
    return cycle_time*(m_put_phys_bits+1); // one extra for end ramp
}

//----------------------------------------------------------------------------

// Get length in seconds that is yet to be sent
double TapeEncoder::GetTimeLeft() const
{
    return GetDuration() - GetElapsedTime();
}

//----------------------------------------------------------------------------

// Get length in seconds that has been sent
double TapeEncoder::GetElapsedTime() const
{
    double t = m_sink->GetElapsedTime(); // thread safe
    double t1 = GetDuration();
    double tol = 10.0/ENCODER_RATE; // 10 sample tolerance for rounding error

    // Make sure to arrive at duration even in case of some roundoff error.
    return t > t1-tol ? t1 : t;
}

//----------------------------------------------------------------------------

// Wait for t seconds
void TapeEncoder::Sleep(double t)
{
    int us = (int) floor(t*1e6);
    while (us > 0)
    {
        int maxwait = 999999; // Upper limit for usleep on BSDs
        int us1 = us < maxwait ? us : maxwait;

        usleep(us1);
        us -= us1;
    }
}

//----------------------------------------------------------------------------

// Start outputting to file or line out unless done so already
// Wait for output to finish or timeout to be reached
void TapeEncoder::Flush(double t_timeout /* = 1e9 */)
{
    // The encoding might not have been started yet
    StartEncode();

    // If timeout seems shorter than what's left, then wait just the timeout
    double t_left = GetTimeLeft();
    if (t_timeout < t_left)
    {
        Sleep(t_timeout);
        return;
    }

    // Blocking finalization
    Close();
}
