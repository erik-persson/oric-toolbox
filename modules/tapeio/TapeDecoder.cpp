//============================================================================
//
//  TapeDecoder for Oric tape format
//
//  Internally two different decoders
//  * demod_decoder - a demodulation based, slow format only decoder
//  * dual_decoder - a decoder capable of both slow and fast formats
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#include "TapeDecoder.h"
#include "DecodedByte.h"
#include "DecoderBackend.h"
#include "TrivialDecoder.h"
#include "DemodDecoder.h"
#include "DualDecoder.h"
#include "XenonDecoder.h"
#include "TapeFile.h"
#include "TapeParser.h"

#include <soundio/Sound.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------
// MyParser - TapeParser subclass, rerouting OnFile to decoder
//----------------------------------------------------------------------------

class MyParser : public TapeParser
{
    TapeDecoder *m_dec;
public:
    MyParser(bool verbose, TapeDecoder *dec) : TapeParser(verbose)
    {
        m_dec = dec;
    }

    // This may be overriden to capture extracted files
    void OnFile(const TapeFile& file) override
    {
        m_dec->OnFile(file);
    }
};

//----------------------------------------------------------------------------
// TapeDecoder methods
//----------------------------------------------------------------------------

TapeDecoder::TapeDecoder(const DecoderOptions& options) :
    m_options(options)
{
    m_parser = new MyParser(options.verbose, this);
    Open();
}

//----------------------------------------------------------------------------

TapeDecoder::TapeDecoder(const char *filename) :
    m_options()
{
    m_options.filename = filename;
    m_parser = new MyParser(false, this);
    Open();
}

//----------------------------------------------------------------------------

void TapeDecoder::Open()
{
    assert(m_options.filename);

    // Select slow or fast in case clearly specified.
    // Otherwise clear both flags for autodetect.
    m_select_fast = m_options.fast && !m_options.slow;
    m_select_slow = m_options.slow && !m_options.fast;

    Sound src;

    if (!src.ReadFromFile(m_options.filename, true /*silent*/))
    {
        // Read as TAP archive
        m_backend0 = new TrivialDecoder(m_options);
    }
    else if (m_options.dual)
    {
        // Dual format (fast+slow) two-stage decoder
        // Enable just one format in case clearly specified
        // Otherwise enable both decoders for autodetect
        bool decode_fast = m_options.fast || !m_options.slow;
        bool decode_slow = m_options.slow || !m_options.fast;
        m_backend0 = new DualDecoder(src, m_options, decode_fast, decode_slow);
    }
    else
    {
        // For fast format: Xenon decoder
        if (!m_options.slow)
            m_backend0 = new XenonDecoder(src, m_options);

        // For slow format: Demodulation based decoder
        // Faster and more accurate than dual_decoder, but can't do fast mode
        if (!m_options.fast)
            m_backend1 = new DemodDecoder(src, m_options);
    }

    // Peek buffer
    // Always have one byte read out unless at EOF
    m_backend0_byte_ok = m_backend0 && m_backend0->DecodeByte(&m_backend0_byte);
    m_backend1_byte_ok = m_backend1 && m_backend1->DecodeByte(&m_backend1_byte);
}

//----------------------------------------------------------------------------

TapeDecoder::~TapeDecoder()
{
    delete m_backend0;
    delete m_backend1;
    delete m_parser;
}

//----------------------------------------------------------------------------

// Retreive one byte
// Mix bytes from up to two decoders
// Return false on end of tape
bool TapeDecoder::ReadByte(DecodedByte *b)
{
    // Weave together up to two streams
    while (m_backend0_byte_ok || m_backend1_byte_ok)
    {
        if (m_backend0_byte_ok &&
            (!m_backend1_byte_ok || m_backend0_byte.time <= m_backend1_byte.time))
        {
            *b = m_backend0_byte;

            // Keep peek buffer filled
            m_backend0_byte_ok = m_backend0->DecodeByte(&m_backend0_byte);
        }
        else
        {
            assert(m_backend1_byte_ok);
            *b = m_backend1_byte;

            // Keep peek buffer filled
            m_backend1_byte_ok = m_backend1->DecodeByte(&m_backend1_byte);
        }
        bool idle = m_parser->IsIdle();

        // Detect sync, perform mode switch
        if (b->byte == 0x16 && !b->sync_error && !b->parity_error && idle)
        {
            if (b->slow? !m_select_slow : !m_select_fast)
            {
                m_parser->VerboseLog(b->time, "Detected %s format\n",
                                     b->slow? "slow" : "fast");
            }

            m_select_fast = !b->slow;
            m_select_slow = b->slow;
        }

        bool selected = b->slow ? m_select_slow : m_select_fast;

        if (selected)
        {
            m_parser->PutByte(*b);

            // Do not return bytes with errors unless inside a file
            // This way --decode will print useful errors
            if ((!b->sync_error && !b->parity_error) || !idle)
                return true;
        }
    }
    return false; // End of tape
}

//----------------------------------------------------------------------------

// Decode waveform to bytestream and parse to files
bool TapeDecoder::ReadFile(TapeFile *file)
{
    // Decode whole tape
    m_result_file = file; // borrow temporarily
    m_result_file_produced = false;
    DecodedByte b;
    while (ReadByte(&b))
    {
        if (m_result_file_produced)
        {
            m_result_file = 0;
            return true;
        }
    }
    m_parser->Flush(); // might also produce a file
    m_result_file = 0;
    return m_result_file_produced;
}

//----------------------------------------------------------------------------

// Fnuction called by parser when a file has been parsed
void TapeDecoder::OnFile(const TapeFile& file)
{
    if (m_result_file) // inside ReadFile call?
    {
        // Copy the file to the caller's buffer
        (*m_result_file) = file;
        m_result_file_produced = true;
    }
}
