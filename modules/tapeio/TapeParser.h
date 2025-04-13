//============================================================================
//
//  TapeParser - Decode byte stream to files
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef TAPEPARSER_H
#define TAPEPARSER_H

#include "TapeFile.h"
#include "DecodedByte.h"

//----------------------------------------------------------------------------

class TapeParser
{
    // File state machine
    int m_section_type;      // current section, major state for scouting
        #define ST_SYNC    (0)
        #define ST_HEADER  (1)
        #define ST_NAME    (2)
    int m_section_offs;      // bytes scanned in current section
    bool m_slow;             // set when slow format is used
    int m_consecutive_non_16;
    int m_consecutive_bad_bytes;

    TapeFile m_scout_file;   // data of file in early stage processing

    bool m_payload_active;   // payload processing started
    int m_payload_offs;      // bytes scanned so far in payload
    TapeFile m_payload_file; // data of file in late stage processing

    bool m_verbose;          // print out log of parser events when set

    DecodedByte m_printbuf[16];
    int m_printbuf_cnt = 0;
    bool m_printbuf_payload = false;
    int m_printbuf_section = 0;
    uint16_t m_printbuf_addr;

protected:
    double m_last_time = 0;  // time coordinate of last processed byte

public:
    TapeParser(bool verbose);
    virtual ~TapeParser() {};

    // This may be overriden to capture extracted files
    virtual void OnFile(const TapeFile& file);

    // Main entry point - process one byte
    void PutByte(const DecodedByte& byte);

    // Finish parsing, call at end of tape
    void Flush();

    // Return when the parser is in the initial state looking for sync
    bool IsIdle() const;

    // When verbosity is on, print message with time coordinate
    void VerboseLog(double time, const char *fmt, ...);
    void VerboseLog(const char *fmt, ...);

private:
    void PrintByte(const DecodedByte& byte);
    void PrintFlush();
    void FlushPayload();
    void Reset();
};

#endif
