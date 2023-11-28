//----------------------------------------------------------------------------
//
//  TapeParser - Decode byte stream to files
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#include "TapeFile.h"
#include "TapeParser.h"
#include "DecodedByte.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <tgmath.h>

//----------------------------------------------------------------------------

// Print time in MM:SS:CC format
static void print_time(FILE *f, double time)
{
    int cent = floor(100*time);

    // Suppress negative numbers that would otherwise get printed strangely
    // First byte might protrude a bit to the left of 0
    if (cent<0)
        cent = 0;

    int secs = cent/100;
    cent %= 100;
    int mins = secs/60;
    secs %= 60;

    fprintf(f,"%02d:%02d.%02d", mins, secs, cent);
}

//----------------------------------------------------------------------------

TapeParser::TapeParser(bool verbose)
{
    Reset();
    m_verbose = verbose;
}

//----------------------------------------------------------------------------

void TapeParser::Reset()
{
    m_section_type = ST_SYNC; // Waiting for $16
    m_section_offs = 0;
    m_slow = false;
    m_consecutive_non_16 = 100;    // assume we saw some bad bytes
    m_consecutive_bad_bytes = 100;
    m_payload_active = false;
    m_payload_offs = 0;
    memset(&m_scout_file, 0, sizeof(m_scout_file));
    memset(&m_payload_file, 0, sizeof(m_payload_file));
}

//----------------------------------------------------------------------------

bool TapeParser::IsIdle() const
{
    return m_section_type == ST_SYNC && !m_payload_active;
}

//----------------------------------------------------------------------------

void TapeParser::VerboseLog(double time, const char *fmt, ...)
{
    if (!m_verbose)
        return;

    print_time(stdout, time);
    printf("  ");

    va_list ap;
    va_start(ap, fmt);
    (void) vprintf(fmt, ap);
    va_end(ap);
}

//----------------------------------------------------------------------------

// Variant with automatic time annotation
void TapeParser::VerboseLog(const char *fmt, ...)
{
    if (!m_verbose)
        return;

    print_time(stdout, m_last_time);
    printf("  ");

    va_list ap;
    va_start(ap, fmt);
    (void) vprintf(fmt, ap);
    va_end(ap);
}

//----------------------------------------------------------------------------

// Flush contents of hex dump buffer
// Print in format similar to hexdump -C
void TapeParser::PrintFlush()
{
    if (m_printbuf_cnt > 0)
    {
        const int N = sizeof(m_printbuf)/sizeof(m_printbuf[0]);

        // 5 wide column with section type or address in payload
        char abuf[5+1];
        if (m_printbuf_payload)
            snprintf(abuf, sizeof(abuf), "%04x ", (int) m_printbuf_addr);
        else
            strcpy(abuf,
                m_printbuf_section == ST_HEADER ? "Hdr  " :
                m_printbuf_section == ST_NAME   ? "Name " :
                                                  "Sync " );
        // Hex part, 3x16 = 48 chars wide
        char hbuf[3*N+1];
        for (int i=0; i<N; i++)
        {
            if (i<m_printbuf_cnt)
            {
                const auto& b = m_printbuf[i];
                char c = b.sync_error   ? '!' :
                         b.parity_error ? '?' :
                         ' ';
                snprintf(hbuf+3*i, 3+1, "%02x%c", b.byte, c);
            }
            else
                strcpy(hbuf+3*i, "   ");
        }

        // Text part, 16 chars wide
        char tbuf[N+1];
        for (int i=0; i<N; i++)
        {
            char c = i<m_printbuf_cnt ? m_printbuf[i].byte : ' ';
            if (!isprint(c))
                c = '.';
            tbuf[i] = c;
        }
        tbuf[N] = 0;

        VerboseLog(m_printbuf[0].time, "%s %s |%s|\n", abuf, hbuf, tbuf);

        m_printbuf_cnt = 0;
    }
}

//----------------------------------------------------------------------------

// Record byte for printing in hex dump
void TapeParser::PrintByte(const DecodedByte& b)
{
    // Flush out print when section type changes
    if (m_printbuf_cnt &&
        ( m_printbuf_payload != m_payload_active ||
          m_printbuf_section != m_section_type))
    {
        PrintFlush();
    }

    m_printbuf_payload = m_payload_active;
    m_printbuf_section = m_section_type;
    if (m_printbuf_cnt == 0)
        m_printbuf_addr = m_payload_file.start_addr + m_payload_offs;

    const int N = sizeof(m_printbuf)/sizeof(m_printbuf[0]);
    assert(m_printbuf_cnt<N);
    m_printbuf[m_printbuf_cnt++] = b;
    if (m_printbuf_cnt == N ||
        (m_printbuf_payload && (m_printbuf_addr&15)+m_printbuf_cnt==16))
        PrintFlush();
}

//----------------------------------------------------------------------------

void TapeParser::PutByte(const DecodedByte& b)
{
    if (m_slow != b.slow)
    {
        if (!IsIdle())
            Flush(); // truncate ongoing file
        m_slow = b.slow;
    }

    if (m_verbose)
        PrintByte(b);
    else
        PrintFlush();

    // Extend end time of file past this byte
    double t_byte = b.slow ? 209.0/4800 : 32.0/4800; // nominal
    m_scout_file.end_time = b.time + 1.5*t_byte; // 1.5 bytes ahead to have some margin
    m_payload_file.end_time = m_scout_file.end_time;

    if (m_payload_active)
    {
        int capacity = sizeof(m_payload_file.payload)/sizeof(m_payload_file.payload[0]);
        assert(m_payload_offs >= 0 && m_payload_offs < capacity);
        m_payload_file.payload[m_payload_offs++] = b.byte;

        // Count errors in mutually exclusive catagories (max 1 per byte)
        m_payload_file.sync_errors += b.sync_error;
        m_payload_file.parity_errors += b.parity_error && !b.sync_error;

        if (m_payload_offs == m_payload_file.len)
        {
            PrintFlush();
            if (m_verbose)
                VerboseLog(m_payload_file.end_time,
                    "File finished, %d sync errors, %d parity errors\n",
                    m_payload_file.sync_errors,
                    m_payload_file.parity_errors
            );
            OnFile(m_payload_file);
            m_payload_active = false;
        }
    }

    if (b.byte != 0x16)
        m_consecutive_non_16++;
    else
        m_consecutive_non_16 = 0;

    if (b.sync_error || b.parity_error)
        m_consecutive_bad_bytes++;
    else
        m_consecutive_bad_bytes = 0;

    if (m_section_type == ST_SYNC)
    {
        // Oric, when writing will write 16,16,16,24 but accept 16,16,16,A,24
        // where A is any random sequence when reading.
        // We try to balance missed/phantom files by allowing any A without
        // 8 non-16 bytes in a row with sync/parity errors in all the last 3.
        // An exception is when an old file is in progress, then we're more strict.
        if (m_section_offs == 0)
            m_scout_file.start_time = b.time;
        if (b.byte == 0x16)
        {
            m_section_offs++;
        }
        else if (b.byte == 0x24 && m_section_offs >= 3) // need at least three 0x16
        {
            PrintFlush();
            if (m_verbose)
                VerboseLog(b.time, "Found sync, %d leading bytes\n", m_section_offs);
            m_section_type = ST_HEADER;
            m_section_offs = 0;
            m_scout_file.sync_errors = 0;
            m_scout_file.parity_errors = 0;
        }
        else if (m_section_offs >= 3 &&
                 !m_payload_active && // When overlapping file, require strict sync
                 (m_consecutive_non_16<8 || m_consecutive_bad_bytes<4))
        {
            // Within tolerance - accept some funny bytes before giving up
            m_section_offs++;
        }
        else
        {
            // Reset the sync search
            m_section_offs = 0;
        }
    }
    else if (m_section_type == ST_HEADER)
    {
        int capacity = sizeof(m_scout_file.header)/sizeof(m_scout_file.header[0]);
        assert(m_section_offs >= 0 && m_section_offs<capacity);
        m_scout_file.header[m_section_offs++] = b.byte;

        // Count errors in mutually exclusive catagories (max 1 per byte)
        m_scout_file.sync_errors += b.sync_error;
        m_scout_file.parity_errors += b.parity_error && !b.sync_error;

        if (m_section_offs == capacity)
        {
            //  +-----+-----------+--------------------------------------------------------------------+
            //  |Bytes| Name      | Values                                                             |
            //  +-----+-----------+--------------------------------------------------------------------+
            //  |  0  | datatype0 | Ignored when filetype is BASIC or DATA  $00=Real/String, $80=Int   |
            //  +-----+-----------+--------------------------------------------------------------------+
            //  |  1  | datatype1 | Ignored when filetype is BASIC or DATA. $00=Int/Real, $FF=String   |
            //  +-----+-----------+--------------------------------------------------------------------+
            //  |  2  | filetype  | $00 = BASIC, $80 = DATA, $40 = ARRAY(V1.1 only)                    |
            //  +-----+-----------+--------------------------------------------------------------------+
            //  |  3  | autorun   | Autorun enabled when nonzero. Normally ($00 or $C7)                |
            //  +-----+-----------+--------------------------------------------------------------------+
            //  | 4-5 | endaddr   | BASIC/DATA: End address (inclusive), high byte first               |
            //  +-----+-----------+--------------------------------------------------------------------+
            //  | 6-7 | startaddr | BASIC/DATA: Start address, high byte first                         |
            //  +-----+-----------+--------------------------------------------------------------------+
            //  |  8  | unused8   | Ignored when filetype is BASIC or DATA. The value can vary.        |
            //  +-----+-----------+--------------------------------------------------------------------+

            uint8_t  filetype = m_scout_file.header[2];
            if (filetype == 0x00 || filetype == 0x80)
            {
                // Now expect name
                m_section_type = ST_NAME;
                m_section_offs = 0;
            }
            else
            {
                PrintFlush();
                if (m_verbose)
                    VerboseLog(b.time, "Unsupported header, ignoring file\n");
                else
                {
                    if (m_scout_file.sync_errors || m_scout_file.parity_errors)
                        // Suspect the reason is decoding quality rather than exotic file type
                        fprintf(stderr, "Warning: Corrupted header, ignoring file at ");
                    else
                        fprintf(stderr, "Warning: Unsupported header, ignoring file at ");
                    print_time(stderr, m_scout_file.start_time);
                    fprintf(stderr, "\n");
                }

                // Now expect name
                m_section_type = ST_SYNC;
                m_section_offs = 0;
            }
        }
    }
    else
    {
        assert(m_section_type == ST_NAME);

        int capacity = sizeof(m_scout_file.name)/sizeof(m_scout_file.name[0]);
        assert(m_section_offs >= 0 && m_section_offs < capacity);
        m_scout_file.name[m_section_offs++] = b.byte;

        // Count errors in mutually exclusive catagories (max 1 per byte)
        m_scout_file.sync_errors += b.sync_error;
        m_scout_file.parity_errors += b.parity_error && !b.sync_error;

        if (b.byte == 0)
        {
            uint8_t  filetype = m_scout_file.header[2];
            uint16_t endaddr   = (((uint16_t) m_scout_file.header[4]) << 8) | m_scout_file.header[5];
            uint16_t startaddr = (((uint16_t) m_scout_file.header[6]) << 8) | m_scout_file.header[7];

            // Calculate length as 1..65536
            int len = (endaddr-startaddr);
            len &= 0xffff;
            len += 1;

            m_scout_file.start_addr = startaddr;
            m_scout_file.end_addr = endaddr;
            m_scout_file.len = (int) len; //1..64k
            m_scout_file.autorun = m_scout_file.header[3] != 0;
            m_scout_file.basic = (filetype == 0x00);
            m_scout_file.slow = b.slow;

            // Interrupt previous file, if any
            // New file takes priority
            FlushPayload();

            if (m_verbose)
            {
                PrintFlush();

                char nambuf[18];
                int i = 0;
                while (1)
                {
                    char c = m_scout_file.name[i];
                    nambuf[i++] = c==0 || isprint(c) ? c : '?';
                    if (c==0)
                        break;
                }
                VerboseLog(b.time, "Found %s\n", nambuf);
            }

            // Spawn two parallel activities
            // * Parse payload
            // * Scan for sync again
            m_payload_active = true;
            m_payload_offs = 0;
            m_payload_file = m_scout_file;
            m_section_type = ST_SYNC;
            m_section_offs = 0;
        }
        else if (m_section_offs == capacity)
        {
            PrintFlush();
            if (m_verbose)
                VerboseLog(b.time, "Too long file name, ignoring file\n");
            else
            {
                if (m_scout_file.sync_errors || m_scout_file.parity_errors)
                    // Suspect the reason is decoding quality rather than exotic file type
                    fprintf(stderr, "Warning: Corrupted file name, ignoring file at ");
                else
                    fprintf(stderr, "Warning: Too long file name, ignoring file at ");
                print_time(stderr, m_scout_file.start_time);
                fprintf(stderr, "\n");
            }

            m_section_type = ST_SYNC;
            m_section_offs = 0;
        }
    }

    m_last_time = b.time;
}

//----------------------------------------------------------------------------

// Truncate and output current file in payload processing
void TapeParser::FlushPayload()
{
    if (m_payload_active)
    {
        int capacity = sizeof(m_payload_file.payload)/sizeof(m_payload_file.payload[0]);

        int missing_bytes = m_payload_file.len - m_payload_offs;
        fprintf(stderr, "Warning: File truncated with %d missing bytes\n", missing_bytes);

        // Pad file to its expected length
        while (missing_bytes--)
        {
            assert(m_payload_offs >= 0 && m_payload_offs < capacity);
            m_payload_file.payload[m_payload_offs++] = 0xcd;
            m_payload_file.sync_errors += 1;
            m_payload_file.parity_errors += 1;
        }
        if (m_verbose)
            VerboseLog(m_payload_file.end_time,
                "File truncated, %d sync errors, %d parity errors\n",
                m_payload_file.sync_errors,
                m_payload_file.parity_errors
        );
        OnFile(m_payload_file);
        m_payload_active = false;
    }
}

//----------------------------------------------------------------------------

void TapeParser::Flush()
{
    PrintFlush();
    FlushPayload();
    Reset();
}

//----------------------------------------------------------------------------

// This may be overriden to capture extracted files
void TapeParser::OnFile(const TapeFile&)
{
}
