//============================================================================
//
//  TapeDecoder - decoder for Oric tape format
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef TAPEDECODER_H
#define TAPEDECODER_H

#include "DecodedByte.h"
#include "DecoderOptions.h"
#include "TapeParser.h"

#include <vector>

class DecoderBackend;

//----------------------------------------------------------------------------

// TapeDecoder - common front-end to decoders
class TapeDecoder
{
    DecoderOptions m_options;

    DecoderBackend *m_backend0 = 0;
    DecoderBackend *m_backend1 = 0;

    // Peek buffer
    DecodedByte m_backend0_byte, m_backend1_byte;
    bool m_backend0_byte_ok = false;
    bool m_backend1_byte_ok = false;

    bool m_select_fast = false;
    bool m_select_slow = false;
    TapeParser *m_parser = 0;
    TapeFile *m_result_file = 0;
    bool m_result_file_produced = false;

public:
    TapeDecoder(const DecoderOptions& options);
    TapeDecoder(const char *filename);
    virtual ~TapeDecoder();

    // Main entry point - read one file from tape
    bool ReadFile(TapeFile *file);

    // Alternative entry point - read one byte from tape
    bool ReadByte(DecodedByte *b);

    // When verbosity is on, print message with time coordinate
    template<class... Args>
    void VerboseLog(Args&&... args)
    {
        m_parser->VerboseLog(args...);
    }

    // Function called by parser when file has been produced
    void OnFile(const TapeFile& file);

private:
    void Open();
};

#endif
