//----------------------------------------------------------------------------
//
//  Trivial decoder to extract bytestream from .tap file
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "TrivialDecoder.h"
#include "DecodedByte.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//----------------------------------------------------------------------------

TrivialDecoder::TrivialDecoder(const DecoderOptions& options) :
    m_options(options)
{
    m_file = fopen(options.filename, "rb");
    if (!m_file)
    {
        fprintf(stderr, "Couldn't read %s\n", m_options.filename);
        exit(1);
    }
}

//----------------------------------------------------------------------------

TrivialDecoder::~TrivialDecoder()
{
    fclose(m_file);
}

//----------------------------------------------------------------------------

bool TrivialDecoder::DecodeByte(DecodedByte *b)
{
    if (!m_file)
        return false;

    double dt =
        m_options.slow ?
            209.0/m_options.f_ref :
            32.0/m_options.f_ref;

    while (1)
    {
        int c = fgetc(m_file);
        if (c == EOF)
            return false;

        double time = m_time;
        m_time += dt;

        // Discard byte outside user specified time interval
        if (m_options.start!=-1 && time < m_options.start)
            continue;
        if (m_options.end!=-1 && time >= m_options.end)
            return false;

        b->time = time;
        b->slow = m_options.slow;
        b->byte = (uint8_t) c;
        b->parity_error = 0;
        b->sync_error = 0;
        return true;
    }
}
