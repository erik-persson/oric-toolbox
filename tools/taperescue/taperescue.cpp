//============================================================================
//
//  taperescue - A tool for managing Oric tapes
// 
//  Copyright (c) 2021-2023 Erik Persson
//
//============================================================================

#include <tapeio/TapeFile.h>
#include <tapeio/TapeDecoder.h>
#include <tapeio/TapeEncoder.h>
#include <soundio/SoundPlayer.h>
#include <soundio/SoundRecorder.h>
#include <soundio/SoundWriter.h>
#include <option/Option.h>

#include <vector>
#include <unordered_set>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <tgmath.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <signal.h>
#include <unistd.h>

#define VERSION "1.0.3"

//----------------------------------------------------------------------------
// Command line options
//----------------------------------------------------------------------------

// When adding/removing flags here also update the README.

// Command flags
BoolOption g_help('h',"help", "Show command line syntax");
BoolOption g_version('V',"version", "Print program version");
BoolOption g_list('l',"list", "List contents of tape");
BoolOption g_extract('x',"extract", "Extract files from tape");
BoolOption g_decode('d',"decode", "Decode waveform to tape archive");
BoolOption g_encode('e',"encode", "Encode tape archive into waveform");
BoolOption g_play('p',"play", "Play waveform or tape archive to audio output device");
BoolOption g_record('r',"record", "Record waveform from audio input device");

// Other flags
TimeOption g_start('S',"start", "Specify start time in minutes:seconds notation", -1);
TimeOption g_end('E',"end", "Specify end time in minutes:seconds notation", -1);
StringOption g_output_dir('O',"output-dir", "Specify directory to extract files into", 0);
BoolOption g_fast('f',"fast", "Use fast tape format");
BoolOption g_slow('s',"slow", "Use slow tape format");
BoolOption g_dual('2',"dual", "Use dual-mode two-stage decoder");
BoolOption g_verbose('v',"verbose", "Print hex dump and diagnostic information");
BoolOption g_dump('D',"dump", "Write intermediate waveform(s) named dump-<xxx>.wav");
IntOption g_clock('c',"clock", "Decoder bit rate in Hz (default 4800)", 4800);

// Sub-options to the demodulation decoder
BoolOption g_low_band(0, "low-band", "Listen to 1200 Hz band only, ignore 2400 Hz");
BoolOption g_high_band(1, "high-band", "Listen to 2400 Hz band only, ignore 1200 Hz");

// Sub-options to the Xenon decoder
BoolOption g_area_cue(10, "area-cue", "Use only area measure to read bits");
BoolOption g_wide_cue(11, "wide-cue", "Use only wide pulse location to read bits");

// Sub-options to the dual decoder
BoolOption g_grid(20, "grid", "Use alteranative bit extractor named Grid");
BoolOption g_super(21, "super", "Use alteranative bit extractor named Super");
BoolOption g_plen(22, "plen", "Use alternative fast decoder named PLEN");
BoolOption g_barrel(23, "barrel", "Use alternative fast decoder named Barrel");

//----------------------------------------------------------------------------
// Help command
//----------------------------------------------------------------------------

static int help(const char *progname)
{
    fprintf(stderr,"Usage: %s -h/--help\n",progname);
    fprintf(stderr,"       %s -V/--version\n",progname);
    fprintf(stderr,"       %s -l/--list    [options] <in.tap/wav>\n",progname);
    fprintf(stderr,"       %s -x/--extract [options] <in.tap/wav>\n",progname);
    fprintf(stderr,"       %s -d/--decode  [options] <in.wav> <out.tap>\n",progname);
    fprintf(stderr,"       %s -e/--encode  [options] <in.tap> <out.wav>\n",progname);
    fprintf(stderr,"       %s -p/--play    [options] <in.tap/wav>\n",progname);
    fprintf(stderr,"       %s -r/--record  [options] <out.wav>\n",progname);
    fprintf(stderr,"\n");
    Option::Help(); // list command line flags
    return 0;
}

//----------------------------------------------------------------------------
// Version command
//----------------------------------------------------------------------------

static int version()
{
    printf("oric-toolbox taperescue vesion " VERSION "\n");
    return 0;
}

//----------------------------------------------------------------------------
// Destination directory preparation
//----------------------------------------------------------------------------

// Create or re-use destination directory
static bool prepare_dest_dir(const char *name, bool verbose)
{
    struct stat sbuf;
    if (stat(name, &sbuf) == 0)
    {
        // Something exists with name 'name'
        if (!S_ISDIR(sbuf.st_mode))
        {
            fprintf(stderr, "%s is not a directory\n", name);
            return false;
        }

        if (verbose)
            printf("Using existing destination directory %s\n", name);

        return true; // success - directory already there
    }

    if (errno != ENOENT)
    {
        // Some other error while accessing the destination directory
        perror(name);
        return false;
    }

    if (verbose)
        printf("Creating destination directory %s\n", name);
    if (mkdir(name, 0777) != 0)
    {
        // Could not create the directory
        perror(name);
        return false;
    }

    return true; // success - directory now exists
}

//----------------------------------------------------------------------------
// File name helpers
//----------------------------------------------------------------------------

// Check if file name can be used for extracted file
static bool is_valid_file_name(const uint8_t *name)
{
    bool is_valid = false;
    if (name[0]) // Forbid empty filename
    {
        is_valid = true;

        // Forbid non-ASCII chars
        // Forbid Windows illegal chars
        // 0-31 \ / : * ? " < > | 128-255
        for (int i= 0; name[i]; i++)
        {
            uint8_t c = name[i];
            if (c<32 || c>127 || c=='\\' || c=='/' || c==':' || c=='*' ||
                c=='?' || c=='"' || c=='<' || c=='>' || c=='|')
            {
                is_valid = false;
            }
        }

        // Also forbid names matching our autogenerated names
        if (name[0] == 'F' &&
            name[1] == 'I' &&
            name[2] == 'L' &&
            name[3] == 'E' &&
            name[4] == '_' &&
            name[5] == 'A' &&
            name[6] == 'T' &&
            name[7] == '_')
        {
            is_valid = false;
        }
    }
    return is_valid;
}

//----------------------------------------------------------------------------

// Adjust file name from tape so it can be used on disk
// Add used names to set
static void adjust_file_name(char *adjusted_name, int bufsize,
    std::unordered_set<std::string> *used_names,
    const TapeFile& file, bool add_extension)
{
    // Normally 0-15 chars in filename
    // Tolerate 16 in case terminal null is absent

    // Avoid empty or otherwise problematic names
    char valid_name[32];
    assert(strlen((const char *) file.name)<=16);
    if (is_valid_file_name(file.name))
        strcpy(valid_name, (const char *) file.name);
    else
    {
        int sec0 = (int) floor(file.start_time);
        int len = snprintf(valid_name, sizeof(valid_name),
                 "FILE_AT_%02d_%02d", sec0/60, sec0%60);
        assert(len < (int) sizeof(valid_name));
    }

    // If the same file name occurs multiple times,
    // append -<n> where n makes the file name unique.
    if (1)
    {
        char try_name[44];
        assert(strlen(valid_name) < sizeof(try_name));
        strcpy(try_name, valid_name);

        int unique_no = 0;
        while (used_names->find(try_name) != used_names->end())
        {
            unique_no++;
            int len = snprintf(try_name, sizeof(try_name),
                "%s-%d", valid_name, unique_no);
            assert(len < (int) sizeof(try_name));
        }
        assert((int) strlen(try_name) < bufsize);
        strcpy(adjusted_name, try_name);
        used_names->insert(adjusted_name);
    }

    // Add .tap extension
    if (add_extension)
    {
        assert((int) strlen(adjusted_name)+4 < bufsize);
        strcat(adjusted_name, ".tap");
    }
}

//----------------------------------------------------------------------------
// Concatenate opt_dirname and filename into malloced string
// opt_dirname may be 0
static char *malloced_path_cat(const char *opt_dirname, const char *filename)
{
    assert(filename);
    if (!opt_dirname)
        return strdup(filename);

    int dlen = strlen(opt_dirname);
    if (dlen && opt_dirname[dlen-1]=='/')
        dlen--;

    int flen = strlen(filename);

    char *result = (char *) malloc(dlen+1+flen+1);

    for (int i= 0; i<dlen; i++)
        result[i] = opt_dirname[i];

    result[dlen] = '/';

    for (int i= 0; i<flen; i++)
        result[dlen+1+i] = filename[i];

    result[dlen+1+flen] = 0;

    return result;
}

//----------------------------------------------------------------------------
// List command
//----------------------------------------------------------------------------

static void list_file(TapeDecoder& dec, const TapeFile& file, const char *unique_name)
{
    int sec0 = (int) floor(file.start_time);
    int sec1 = (int) ceil(file.end_time);
    if (g_verbose)
    {
        dec.VerboseLog(file.end_time, "Location:      %02d:%02d - %02d:%02d\n",
            sec0/60, sec0%60,
            sec1/60, sec1%60);
        dec.VerboseLog(file.end_time, "Start address: $%04x\n",file.start_addr);
        dec.VerboseLog(file.end_time, "End address:   $%04x\n",file.end_addr);
        dec.VerboseLog(file.end_time, "Length:        %d bytes\n",file.len);
        dec.VerboseLog(file.end_time, "Type:          %s\n",file.basic ? "BASIC":"DATA");
        dec.VerboseLog(file.end_time, "Autorun:       %s\n",file.autorun ? "Yes":"No");
        dec.VerboseLog(file.end_time, "Format:        %s\n",file.slow ? "Slow":"Fast");
        dec.VerboseLog(file.end_time, "Sync errors:   %d\n",file.sync_errors);
        dec.VerboseLog(file.end_time, "Parity errors: %d\n",file.parity_errors);
        dec.VerboseLog(file.end_time, "Original name: %s\n",file.name);
        dec.VerboseLog(file.end_time, "Extracted as:  %s\n",unique_name);
    }
    else
    {
        printf("%02d:%02d - %02d:%02d %8d  %c %c %c %8d  %s\n",
            sec0/60, sec0%60,
            sec1/60, sec1%60,
            file.len,
            file.basic?'B':'-',
            file.autorun?'A':'-',
            file.slow?'S':'-',
            file.sync_errors + file.parity_errors,
            unique_name);
    }
}

//----------------------------------------------------------------------------

// Return command status (0=success)
static int list(DecoderOptions& options)
{
    int file_cnt = 0;
    int len_sum = 0;
    int error_sum = 0;
    std::unordered_set<std::string> used_names;

    if (!g_verbose)
    {
        printf("-------------  -------  -----  -------  ---------------\n");
        printf("Location       Length   Flags  Errors   Name           \n");
        printf("-------------  -------  -----  -------  ---------------\n");
    }

    TapeDecoder dec(options);

    // Read all files from tape archive
    TapeFile file;
    while (dec.ReadFile(&file))
    {
        // Change name so it can be used on disk
        bool add_extension = g_extract;
        char adjusted_name[34+4];
        adjust_file_name(adjusted_name, sizeof(adjusted_name), &used_names,
                         file, add_extension);

        list_file(dec, file, adjusted_name);

        if (g_verbose)
            dec.VerboseLog(file.end_time, "---------------------------------------\n");

        file_cnt++;
        len_sum += file.len;
        error_sum += file.sync_errors + file.parity_errors;
    }

    if (g_verbose)
    {
        dec.VerboseLog("Total length:  %d bytes\n",len_sum);
        dec.VerboseLog("Total errors:  %d\n",error_sum);
        dec.VerboseLog("File count:    %d\n",file_cnt);
    }
    else
    {
        if (file_cnt)
            printf("-------------  -------  -----  -------  ---------------\n");
        printf("              %8d        %8d  %d file(s)\n",
            len_sum,
            error_sum,
            file_cnt);
    }
    return 0;
}

//----------------------------------------------------------------------------
// Extract command
//----------------------------------------------------------------------------

// Extract one file from tape
static void extract_file(TapeDecoder& dec, const TapeFile& file, const char *extended_name)
{
    char *full_name = malloced_path_cat(g_output_dir, extended_name);

    if (g_verbose)
    {
        dec.VerboseLog(file.end_time, "Extracting %s, %d sync errors, %d parity errors\n",
                full_name, file.sync_errors, file.parity_errors);
    }
    else
    {
        printf("Extracting %s", full_name);
        if (file.sync_errors)
            printf(", %d sync errors", file.sync_errors);
        if (file.parity_errors)
            printf(", %d parity errors", file.parity_errors);
        printf("\n");
    }

    bool ok = false;
    if (FILE *f = fopen(full_name, "wb"))
    {
        ok = true;

        uint8_t preamble[4] = { 0x16, 0x16, 0x16, 0x24 };
        if (fwrite(preamble, 1, 4, f) != 4)
            ok = false;

        if (ok)
        {
            if (fwrite(file.header, 1, sizeof(file.header), f) != sizeof(file.header))
                ok = false;
        }

        if (ok)
        {
            size_t namelen = strlen((const char *) file.name) + 1;
            if (fwrite(file.name, 1, namelen, f) != namelen)
                ok = false;
        }

        if (ok && file.len)
        {
            if (fwrite(file.payload, 1, file.len, f) != (size_t) file.len)
                ok = false;
        }

        fclose(f);
    }

    if (!ok)
        perror(full_name);
    free(full_name);
    if (!ok)
        exit(1);
}

//----------------------------------------------------------------------------

// Return command status (0=success)
static int extract(DecoderOptions& options)
{
    // Prepare output directory, if specified
    if (g_output_dir && !prepare_dest_dir(g_output_dir, g_verbose))
        exit(1);

    int error_sum = 0;
    std::unordered_set<std::string> used_names;

    TapeDecoder dec(options);

    // Read all files from tape archive
    TapeFile file;
    while (dec.ReadFile(&file))
    {
        // Change name so it can be used on disk
        bool add_extension = g_extract;
        char adjusted_name[34+4];
        adjust_file_name(adjusted_name, sizeof(adjusted_name), &used_names,
                         file, add_extension);

        extract_file(dec, file, adjusted_name);

        if (g_verbose)
            dec.VerboseLog(file.end_time, "---------------------------------------\n");

        error_sum += file.sync_errors + file.parity_errors;
    }

    if (error_sum)
    {
        fprintf(stderr,"Errors were encountered during extraction\n");
        return 1;
    }

    return 0;
}

//----------------------------------------------------------------------------
// Decode command
//----------------------------------------------------------------------------

// Decode .wav to .tap
// Return command status (0=success)
static int decode(const DecoderOptions& options, const char *oname)
{
    assert(oname);
    printf("Decoding %s to %s\n", options.filename, oname);
    TapeDecoder dec(options);

    int sync_errors = 0;
    int parity_errors = 0;
    int bytes = 0;

    if (FILE *f = fopen(oname, "wb"))
    {
        DecodedByte b;
        while (dec.ReadByte(&b))
        {
            bytes++;

            // Count errors in mutually exclusive categories (max 1 per byte)
            sync_errors += b.sync_error;
            parity_errors += b.parity_error && !b.sync_error;
            if (fwrite(&b.byte, 1, 1, f) != 1)
            {
                fprintf(stderr, "Error writing %s\n",oname);
                exit(1);
            }
        }
        fclose(f);
    }
    printf("Decoded %d bytes, %d sync errors, %d parity errors\n",
           bytes, sync_errors, parity_errors);
    return sync_errors || parity_errors ? 1 : 0;
}

//----------------------------------------------------------------------------
// Encode command
//----------------------------------------------------------------------------

// Encode .tap to .wav
// If no output filename is given, play .tap to speaker
// Return command status (0=success)
static int encode(const char *iname, const char *opt_oname)
{
    bool slow = g_slow; // default to fast mode when --slow not given

    if (opt_oname)
        printf("Converting tape archive %s to WAV file %s\n", iname, opt_oname);
    else
        printf("Playing tape archive %s\n", iname);

    TapeEncoder enc;
    if (enc.Open(opt_oname, slow))
    {
        if (!enc.PutFile(iname))
        {
            fprintf(stderr,"Couldn't read %s\n", iname);
            exit(1);
        }

        if (!opt_oname) // playing?
        {
            // Loop while playing to present time progress on stdout
            int t1 = (int) floor(enc.GetDuration());
            for (int t=0; t<=t1; t++)
            {
                double te;
                if ((te = enc.GetElapsedTime()) < t-.01)
                    enc.Flush(t-te);

                printf("\rPlaying %02d:%02d / %02d:%02d", t/60, t%60, t1/60, t1%60);
                fflush(stdout);
            }
            enc.Flush(); // wait the last fraction of second
            printf("\n");
        }
        if (enc.Close())
            return 0; // success
    }

    if (opt_oname)
        fprintf(stderr, "Error: Write to %s failed\n", opt_oname);
    else
        fprintf(stderr, "Error: Playing audio failed\n");
    exit(1);
}

//----------------------------------------------------------------------------
// Play command
//----------------------------------------------------------------------------

// Play either .tap or .wav to speaker
// Return command status (0=success)
static int play(const char *filename)
{
    // Try playing .wav to speaker
    Sound src;
    if (src.ReadFromFile(filename, true /*silent*/))
    {
        // Play .wav as is
        SoundPlayer player;
        player.Play(src);

        // Loop while playing to present time progress on stdout
        int t1 = (int) floor( src.GetDuration() );
        for (int t=0; t<=t1; t++)
        {
            double te;
            if ((te = player.GetElapsedTime()) < t-.01)
                player.Flush(t-te);

            printf("\rPlaying %02d:%02d / %02d:%02d", t/60, t%60, t1/60, t1%60);
            fflush(stdout);
        }
        player.Flush(); // wait the last fraction of second
        printf("\n");
        return 0;
    }

    // Wasn't .wav - try to encode .tap to waveform and play to speaker
    return encode(filename, 0);
}

//----------------------------------------------------------------------------
// Record command
//----------------------------------------------------------------------------

bool g_broken = false;

static void sigint_handler(int)
{
    g_broken = true;
}

// Record from line in or speaker and write .wav file
// Return command status (0=success)
static int record(const char *filename)
{
    // Catch Ctrl-C
    signal(SIGINT, sigint_handler);

    double sample_rate_hz = 44100;
    int chunk_len = (int) floor(0.5 + sample_rate_hz/10);
    float *chunk = new float[chunk_len];

    SoundRecorder recorder;
    SoundWriter writer;
    bool read_ok = recorder.Open(sample_rate_hz, chunk_len);
    bool write_ok = writer.Open(filename, sample_rate_hz);
    recorder.Start();

    printf("Recording %02d:%02d", 0,0);
    fflush(stdout);

    // Loop the following operations
    // * Read from SoundRecorder
    // * Print RMS values
    // * Write using SoundWriter
    while (read_ok && write_ok)
    {
        read_ok = recorder.Read(chunk, chunk_len);
        if (!read_ok)
            break;
        double time = recorder.GetElapsedTime();

        // Calculate RMS of the window
        float sum_x = 0, sum_x2 = 0;
        for (int i=0; i<chunk_len; i++)
        {
            auto x = chunk[i];
            sum_x += x;
            sum_x2 += x*x;
        }
        // sum( (x - a)^2 ) = sum( x2 + a2 - 2xa) = sum(x2) + n*a2 - 2a*sum(x)
        float a = sum_x/chunk_len; // average
        float rms = sqrt(sum_x2/chunk_len + a*a - 2*a*sum_x/chunk_len);

        // Display using 20-step log volume scale
        float rms_low  = 0.001, rms_high = 0.9;
        int steps = 20;
        int vol = rms<=rms_low  ? 0 :
                  rms>=rms_high ? steps-1 :
                  floor(0.5 + (steps-1)*log(rms/rms_low)/log(rms_high/rms_low));
        char indicator[steps+1];
        for (int i=0; i<steps; i++)
            indicator[i] = vol>i ? '#': '-';
        indicator[steps] = 0;

        int secs = floor(time);
        int mins = secs/60;
        secs %= 60;
        printf("\rRecording %02d:%02d |%s|", mins, secs, indicator);
        fflush(stdout);

        if (g_broken)
        {
            recorder.Stop();
            break;
        }
        write_ok = writer.Write(chunk, chunk_len);
    }
    printf("\n");

    delete[] chunk;

    if (g_broken)
    {
        printf("Recording stopped\n");
        g_broken = false;
        return 0; // success
    }
    else if (!read_ok)
    {
        fprintf(stderr,"Error reading audio input\n");
    }
    else if (!write_ok)
    {
        fprintf(stderr,"Error writing %s\n",filename);
    }
    return 1; // failure
}

//----------------------------------------------------------------------------
// main
//----------------------------------------------------------------------------

int main(int argc, char **argv)
{
    int optind= Option::Parse(argc,argv);

    int commands_given = 0 +
                         g_help +
                         g_version +
                         g_list +
                         g_extract +
                         g_decode +
                         g_encode +
                         g_play +
                         g_record;

    // Non option arguments are filenames
    int filename_cnt = argc-optind;
    const char *filename0 = filename_cnt>=1 ? argv[optind  ] : 0;
    const char *filename1 = filename_cnt>=2 ? argv[optind+1] : 0;

    bool illegal_options = false;

    if (commands_given != 1)
    {
        fprintf(stderr, "Error: %d commands specified, one expected\n", commands_given);
        illegal_options = true;
    }

    if (g_fast && g_slow)
    {
        fprintf(stderr, "Error: Both slow and fast format specified\n");
        illegal_options = true;
    }

    if (g_area_cue && g_wide_cue)
    {
        fprintf(stderr, "Error: Both --area-cue and --wide-only specified\n");
        illegal_options = true;
    }

    int filename_cnt_expected =
        g_help   ? 0 :
        g_version? 0 :
        g_decode ? 2 :
        g_encode ? 2 :
                   1;

    if (!illegal_options && filename_cnt != filename_cnt_expected)
    {
        fprintf(stderr, "Error: %d filename(s) provided but %d expected\n",
            filename_cnt,
            filename_cnt_expected);
        illegal_options = true;
    }

    if (g_output_dir && !g_extract)
        fprintf(stderr, "Warning: Option --output-dir/-O has no effect without --extract/-x\n");

    DecoderOptions options;
    options.filename = filename0;
    options.dump = g_dump;
    options.start = g_start;
    options.end = g_end;
    options.verbose = g_verbose;
    options.f_ref = g_clock;
    options.fast = g_fast;
    options.slow = g_slow;
    options.dual = g_dual;
    options.band = g_low_band  ? BAND_LOW :
                   g_high_band ? BAND_HIGH :
                   BAND_DUAL;
    options.cue = g_area_cue ? CUE_AREA :
                  g_wide_cue ? CUE_WIDE :
                  CUE_AUTO;
    options.binner = g_grid ? BINNER_GRID :
                     g_super? BINNER_SUPER :
                     BINNER_PATTERN;
    options.fdec = g_plen ? FDEC_PLEN :
                   g_barrel ? FDEC_BARREL :
                   FDEC_ORIG;

    if (illegal_options)
    {
        (void) help(argv[0]);
        exit(1);
    }

    if (g_help)
        return help(argv[0]);

    if (g_version)
        return version();

    if (g_list)
        return list(options);

    if (g_extract)
        return extract(options);

    if (g_decode)
        return decode(options, filename1);

    if (g_encode)
        return encode(filename0, filename1);

    if (g_play)
        return play(filename0);

    if (g_record)
        return record(filename0);

    return 1; // should not come here
}
