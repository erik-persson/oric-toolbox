//----------------------------------------------------------------------------
//
//  Oric BASIC utility
//
//  Copyright (c) 2022-2023 Erik Persson
//
//----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <tapeio/TapeFile.h>
#include <tapeio/TapeDecoder.h>

#define VERSION "1.0.3"

//----------------------------------------------------------------------------
// Keywords
//----------------------------------------------------------------------------

// Keywords for Oric BASIC 1.0
const char *g_basic10_keywords[128] =
{
    "END",    "EDIT",   "INVERSE","NORMAL", "TRON",   "TROFF",  "POP",    "PLOT",
    "PULL",   "LORES",  "DOKE",   "REPEAT", "UNTIL",  "FOR",    "LLIST",  "LPRINT",
    "NEXT",   "DATA",   "INPUT",  "DIM",    "CLS",    "READ",   "LET",    "GOTO",
    "RUN",    "IF",     "RESTORE","GOSUB",  "RETURN", "REM",    "HIMEM",  "GRAB",
    "RELEASE","TEXT",   "HIRES",  "SHOOT",  "EXPLODE","ZAP",    "PING",   "SOUND",
    "MUSIC",  "PLAY",   "CURSET", "CURMOV", "DRAW",   "CIRCLE", "PATTERN","FILL",
    "CHAR",   "PAPER",  "INK",    "STOP",   "ON",     "WAIT",   "CLOAD",  "CSAVE",
    "DEF",    "POKE",   "PRINT",  "CONT",   "LIST",   "CLEAR",  "GET",    "CALL",
    "!",      "NEW",    "TAB(",   "TO",     "FN",     "SPC(",   "@",      "AUTO",
    "ELSE",   "THEN",   "NOT",    "STEP",   "+",      "-",      "*",      "/",
    "^",      "AND",    "OR",     ">",      "=",      "<",      "SGN",    "INT",
    "ABS",    "USR",    "FRE",    "POS",    "HEX$",   "&",      "SQR",    "RND",
    "LN",     "EXP",    "COS",    "SIN",    "TAN",    "ATN",    "PEEK",   "DEEK",
    "LOG",    "LEN",    "STR$",   "VAL",    "ASC",    "CHR$",   "PI",     "TRUE",
    "FALSE",  "KEY$",   "SCRN",   "POINT",  "LEFT$",  "RIGHT$", "MID$",   "GO",
    0,        0,        0,        0,        0,        0,        0,        0
};

// Keywords for Oric BASIC 1.1
const char *g_basic11_keywords[128] =
{
    "END",    "EDIT",   "STORE",  "RECALL", "TRON",   "TROFF",  "POP",    "PLOT",
    "PULL",   "LORES",  "DOKE",   "REPEAT", "UNTIL",  "FOR",    "LLIST",  "LPRINT",
    "NEXT",   "DATA",   "INPUT",  "DIM",    "CLS",    "READ",   "LET",    "GOTO",
    "RUN",    "IF",     "RESTORE","GOSUB",  "RETURN", "REM",    "HIMEM",  "GRAB",
    "RELEASE","TEXT",   "HIRES",  "SHOOT",  "EXPLODE","ZAP",    "PING",   "SOUND",
    "MUSIC",  "PLAY",   "CURSET", "CURMOV", "DRAW",   "CIRCLE", "PATTERN","FILL",
    "CHAR",   "PAPER",  "INK",    "STOP",   "ON",     "WAIT",   "CLOAD",  "CSAVE",
    "DEF",    "POKE",   "PRINT",  "CONT",   "LIST",   "CLEAR",  "GET",    "CALL",
    "!",      "NEW",    "TAB(",   "TO",     "FN",     "SPC(",   "@",      "AUTO",
    "ELSE",   "THEN",   "NOT",    "STEP",   "+",      "-",      "*",      "/",
    "^",      "AND",    "OR",     ">",      "=",      "<",      "SGN",    "INT",
    "ABS",    "USR",    "FRE",    "POS",    "HEX$",   "&",      "SQR",    "RND",
    "LN",     "EXP",    "COS",    "SIN",    "TAN",    "ATN",    "PEEK",   "DEEK",
    "LOG",    "LEN",    "STR$",   "VAL",    "ASC",    "CHR$",   "PI",     "TRUE",
    "FALSE",  "KEY$",   "SCRN",   "POINT",  "LEFT$",  "RIGHT$", "MID$",   0,
    0,        0,        0,        0,        0,        0,        0,        0
};

//----------------------------------------------------------------------------

// Function called for each file in tape archive
// List the content as BASIC
void handle_file(const TapeFile& file)
{
    printf("Name:          %s\n",file.name);
    printf("Start address: $%04x\n",(int) file.start_addr);
    printf("End address:   $%04x\n",(int) file.end_addr);
    printf("Len:           %d\n",(int) file.len);
    printf("Type:          %s\n",file.basic ? "BASIC":"DATA");

    const uint8_t *payload = file.payload;
    int offs = 0;

    const char **keywords = g_basic11_keywords;

    // BASIC listing
    if (file.basic)
    {
        while (offs+4 <= file.len)
        {
            uint16_t here = file.start_addr + offs;
            uint16_t next = (((uint16_t) payload[offs+1]) << 8) | payload[offs];
            uint16_t line = (((uint16_t) payload[offs+3]) << 8) | payload[offs+2];
            if (next == 0)
                // A null value found where the next pointer would be.
                // This is the normal way the BASIC program ends.
                break;

            offs += 4;

            printf("%d ", line);
            while (offs<file.len)
            {
                uint8_t c = payload[offs++];
                const char *kw = keywords[c & 127];
                if (c==0)
                    break;
                else if (c>=32 && c<128)
                    putc(c, stdout);
                else if (c>=128 && kw != 0)
                    printf("%s",kw);
                else
                    printf("<%02x>",(int) c);
            }
            printf("\n");

            if (next>here)
            {
                // Next pointer looks OK, use it to find next line
                offs = next-file.start_addr;
            }
            else
            {
                // Avoid loop by taking all non advancing pointers
                // as end of program.
                break;
            }
        }
        // A program usually ends with 00 00 xx
        // The last byte is unused.
        if (offs+1<file.len && payload[offs]==0 && payload[offs+1]==0)
            offs += 2;
        else
            printf("Warning: Program lacks final null pointer\n");

        int trailer_bytes = file.len - offs;
        if (trailer_bytes==1)
            offs++; // One trailer byte is the common case so don't report this

        if (trailer_bytes>1)
            printf("%d bytes are trailing the BASIC program:\n", file.len-offs);
    }

    // Hexadecimal listing (binary file or trailer after BASIC)
    while (offs<file.len)
    {
        int block = 16;
        uint16_t here = file.start_addr + offs;

        block -= (here % block); // make next location aligned
        printf("%04X: ",here);
        while(offs<file.len && block--)
            printf(" %02X",file.payload[offs++]);
        printf("\n");
    }
}

//----------------------------------------------------------------------------

int main(int argc, const char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "oric-toolbox basictool " VERSION "\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        exit(1);
    }

    const char *filename = argv[1];

    TapeDecoder dec(filename); // will exit on failure

    // Read all files from tape archive
    TapeFile file;
    while (dec.ReadFile(&file))
        handle_file(file);

    return 0;
}
