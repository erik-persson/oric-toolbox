//============================================================================
//
//  TapeDecoder settings struct
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#ifndef DECODEROPTIONS_H
#define DECODEROPTIONS_H

#define BAND_LOW  (0)
#define BAND_HIGH (1)
#define BAND_DUAL (2)

#define CUE_AREA (0)
#define CUE_WIDE (1)
#define CUE_AUTO (2)

#define BINNER_PATTERN (0)
#define BINNER_GRID    (1)
#define BINNER_SUPER   (2)

#define FDEC_ORIG   (0)
#define FDEC_PLEN   (1)
#define FDEC_BARREL (2)

struct DecoderOptions
{
    const char *filename = 0;    // Input file name
    double start = -1;           // Start time in seconds, -1 if unspecified
    double end = -1;             // End time in seconds, -1 if unspecified
    bool verbose = false;        // Verbose log mode
    bool fast = false;           // Decode only fast mode when set
    bool slow = false;           // Decode only slow mode when set
    bool dual = false;           // Use dual-mode (fast+slow) decoder when set
    bool dump = false;           // Write dump-demod.wav and/or dump-dual.wav
    int binner = BINNER_PATTERN; // Bit extractor for dual decoder
    int band = BAND_DUAL;        // Band to use in demodulation based decoder
    int cue = CUE_AUTO;          // Method to recognize bits in Xenon decoder
    int fdec = FDEC_ORIG;        // Bit to byte decoder to use for fast format
    int f_ref = 4800;            // Nominal bit frequency in Hz
};

#endif
