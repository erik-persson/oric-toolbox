//============================================================================
//
//  Test program for tapeio
//
//  Copyright (c) 2021-2022 Erik Persson
//
//============================================================================

#include <tapeio/TapeDecoder.h>
#include <tapeio/TapeEncoder.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

//----------------------------------------------------------------------------
// Loopback test
//----------------------------------------------------------------------------

void loopback_test(bool slow, bool dual)
{
    printf("Running loopback test, %s mode\n", slow ? "slow" : "fast");

    const uint8_t testvector[] = { 0x16, 0x16, 0x16, 0x24, 0x00, 0x55, 0xaa, 0xff };
    int testvector_len = sizeof(testvector)/sizeof(testvector[0]);

    bool test_ok = true;

    char filename[200];
    int err = snprintf(filename, sizeof(filename), "/tmp/loopback_test_%d.wav",(int) getpid());
    assert(err >= 0);

    printf("  Encoding to WAV file %s\n", filename);
    TapeEncoder enc;
    if (enc.Open(filename, slow))
    {
        printf("  Writing %d bytes\n", testvector_len);
        for (int i= 0; i<testvector_len; i++)
            enc.PutByte(testvector[i]);
    }
    if (!enc.Close())
    {
        fprintf(stderr, "Error: Write to %s failed\n", filename);
        test_ok = false;
    }

    DecoderOptions options;
    options.filename = filename;
    options.dual = dual;
    options.fast = !slow;
    options.slow = slow;

    TapeDecoder dec(options);

    // Decode all
    DecodedByte b;
    std::vector<uint8_t> decoded_bytes;
    int parity_errors = 0;
    int sync_errors = 0;
    while (dec.ReadByte(&b))
    {
        decoded_bytes.push_back(b.byte);
        parity_errors += b.parity_error;
        sync_errors += b.sync_error;
    }

    int decoded_len = (int) decoded_bytes.size();
    printf("  Decoded %d bytes using %s decoder\n", decoded_len, dual? "dual":"default");
    for (int i=0; i<decoded_len; i++)
        printf("  Byte %d: %02x\n", i, decoded_bytes[i] );
    printf("  Parity errors: %d\n", parity_errors);
    printf("  Sync errors: %d\n", sync_errors);

    if (decoded_len < testvector_len)
    {
        printf("  Decoded too few bytes (%d vs %d)\n", decoded_len, testvector_len);
        test_ok = false;
    }
    if (decoded_len > testvector_len+50)
    {
        printf("  Decoded too many bytes (%d vs %d)\n", decoded_len, testvector_len);
        test_ok = false;
    }
    for (int i=0; i<decoded_len && i<testvector_len; i++)
        if (decoded_bytes[i] != testvector[i])
        {
            printf("  Byte %d differs: %02x vs %02x\n", i, decoded_bytes[i], testvector[i]);
            test_ok = false;
        }

    if (test_ok)
    {
        (void) remove(filename);
        printf("  Removing file %s\n", filename);
    }

    if (test_ok)
    {
        printf("  Test successful\n");
    }
    else
    {
        printf("  Test failed\n");
        exit(1);
    }
}

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------

// Return test status (0=success)
int main(int, char **)
{
    //            slow   dual
    loopback_test(false, false);
    loopback_test(true,  false);
    loopback_test(false, true);
    loopback_test(true,  true);
    printf("Testing complete\n");
    return 0;
}
