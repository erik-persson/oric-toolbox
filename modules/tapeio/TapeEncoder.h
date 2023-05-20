//----------------------------------------------------------------------------
//
//  TapeEncoder - encoder for Oric tape format
//
//  Copyright (c) 2021-2022 Erik Persson
//
//----------------------------------------------------------------------------

#ifndef TAPEENCODER_H
#define TAPEENCODER_H

#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <mutex>

class SoundSink;

#define ENCODER_BUFSIZE (1024)
#define ENCODER_RATE    (44100)
#define RAMP_LEN        (441) // No. of samples in ramp template
#define RAMP_STEP       (48)  // Step to take for 4800 Hz switching rate

class TapeEncoder
{
    int m_put_phys_bits = 0;
    std::vector<uint8_t> m_inbuf;

    float m_buf[ENCODER_BUFSIZE];
    int m_buf_cnt = 0;
    SoundSink *m_sink = 0;
    bool m_open = false;
    bool m_ok = true;
    bool m_slow = true;
    float m_ramp[RAMP_LEN];
    int m_ramp_phase = 0; // 0..RAMP_LEN-1;
    float m_last_y = 0;
    bool m_last_bit = false;

    // Background encoding
    std::thread *m_enc_thread = 0;

public:
    TapeEncoder();
    ~TapeEncoder();

    // Open output file or player
    bool Open(const char *opt_filename, bool slow);

    // Enqueue single byte for encoding
    void PutByte(uint8_t byte);

    // Enqueue bytestream stored in archive file
    // This will proceed in background
    bool PutFile(const char *iname);

    // Flush output and close file
    bool Close();

    // Get length in seconds that has been put in
    double GetDuration() const;

    // Get length in seconds that has been sent
    double GetTimeLeft() const;

    // Get length in seconds that has been played on line out
    double GetElapsedTime() const;

    // Wait for t seconds
    static void Sleep(double t);

    // Start outputting to file or line out unless done so already
    // Wait for output to finish or timeout to be reached
    void Flush(double timeout = 1e9);

protected:
    void EmitFlush();
    void EmitSample(float y);
    void RampTo(float y);
    void EmitBit(bool val);
    bool EncodeBit(bool val);
    void EncodeByte(uint8_t byte);
    void CountBit(bool val);

    void EncodeThread();
    void StartEncode();
    void FinishEncode();
};

#endif // ENCODER_H
