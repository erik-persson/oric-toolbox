//----------------------------------------------------------------------------
//
//  Trivial decoder to extract bytestream from .tap file
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef TRIVIALDECODER_H
#define TRIVIALDECODER_H

#include "DecoderBackend.h"
#include "DecoderOptions.h"

#include <stdio.h>

// TapeDecoder from TAP bytestream format
// No signal processing - just forwarding a byte stream
// This accepts any byte stream, no format checks
class TrivialDecoder : public DecoderBackend
{
    DecoderOptions m_options;
    FILE *m_file = 0;
    double m_time = 0;

public:
    TrivialDecoder(const DecoderOptions& options);
    ~TrivialDecoder();

    bool DecodeByte(DecodedByte *b) override;
};

#endif
