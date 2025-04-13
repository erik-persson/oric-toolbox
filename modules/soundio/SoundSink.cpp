//============================================================================
//
//  SoundSink implementation
//
//  Copyright (c) 2005-2022 Erik Persson
//
//============================================================================

#include "SoundSink.h"

//----------------------------------------------------------------------------

// Implementation of floating point aspect of sink interface.
//
// Convert and call 16-bit interface.
// Expected input range is +-1, following PortAudio.
//
// SoundSink is not a pure interface since it adds this converting function
bool SoundSink::Write(const float *buf, int len)
{
    short shortbuf[len];
    for (int i= 0; i<len; i++)
    {
        // Multiply by 32768 and clip to 16-bit range -32768..32767
        double val= 32768*buf[i];
        if (val >= 32767)
            shortbuf[i]= 32767;
        else if (val < -32768)
            shortbuf[i]= -32768;
        else
            shortbuf[i]= (short) val;
    }
    return Write(shortbuf,len);
}
