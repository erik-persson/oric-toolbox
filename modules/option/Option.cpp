//=============================================================================
//
//  Option -- C++ wrapper around getopt_long
//
//  Copyright (c) 2006-2022 Erik Persson
//
//=============================================================================

#include "Option.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <tgmath.h>

// Global linked list of options
static Option *g_first_option = 0;

//-----------------------------------------------------------------------------

Option::Option(char c, const char *long_name, const char *help)
{
    m_c = c;
    m_long_name = long_name;
    m_help = help;
    m_next = g_first_option; g_first_option = this;
    m_given = false;
}

//-----------------------------------------------------------------------------

// static
int Option::GetCount()
{
    Option *opt= g_first_option;
    int count= 0;
    while(opt)
    {
        count++;
        opt= opt->m_next;
    }
    return count;
}

//-----------------------------------------------------------------------------

// static
void Option::Help()
{
    Option *opt = g_first_option;

    if (opt)
        fprintf(stderr,"Options are:\n");
    else
        fprintf(stderr,"No command line options are used\n");

    int longest= 0;
    while(opt)
    {
        int l = strlen(opt->m_long_name);
        if (l>longest) longest = l;
        opt = opt->m_next;
    }

    // Print options with short form
    opt = g_first_option;
    while(opt)
    {
        auto c = opt->m_c;
        if (c>32)
            fprintf(stderr,"  -%c --%-*s %s\n",opt->m_c,longest,opt->m_long_name,opt->m_help);
        opt = opt->m_next;
    }

    // Print options without short form
    opt = g_first_option;
    while(opt)
    {
        auto c = opt->m_c;
        if (c<=32)
            fprintf(stderr,"     --%-*s %s\n",longest,opt->m_long_name,opt->m_help);
        opt = opt->m_next;
    }
}

//-----------------------------------------------------------------------------

// static
int Option::Parse(int argc, char **argv)
{
    int opt_cnt = GetCount();

    struct option options[opt_cnt+1];
    char str[2*opt_cnt+1];
    int nstr= 0;
    int i= 0;
    Option *opt = g_first_option;
    while (opt)
    {
        assert(i<opt_cnt);
        options[i].name = opt->m_long_name;
        options[i].has_arg = opt->RequiresArgument()? required_argument : no_argument;
        options[i].flag = NULL;
        options[i].val = opt->m_c;

        // If the character is below 32 it means no short option
        if (opt->m_c > 32)
        {
            // Add to the string of options which is passed to getopt_long
            assert(nstr<(int)sizeof(str));
            str[nstr++] = opt->m_c;
            if (options[i].has_arg)
                str[nstr++] = ':';
        }
        opt= opt->m_next;
        i++;
    }
    memset(&options[opt_cnt],0,sizeof(options[opt_cnt]));
    assert(nstr<(int)sizeof(str));
    str[nstr++]= 0;

    while (1)
    {
        int longindex = 0;
        int c = getopt_long(argc, argv, str, options, &longindex);
        if (c==-1)
            break;

        int matching_options = 0;
        Option *opt = g_first_option;
        while (opt)
        {
            if (c==opt->m_c)
            {
                opt->m_given = true;

                if (!optarg && opt->RequiresArgument())
                {
                    fprintf(stderr,"Error: Argument required for --%s\n",opt->m_long_name);
                    Help();
                    exit(1);
                }
                else
                {
                    if (optarg)
                        opt->Set(optarg);
                    matching_options++;
                }
            }
            opt = opt->m_next;
        }

        if (matching_options != 1)
        {
            Help();
            exit(1);
        }
    }

    return optind;
}

//-----------------------------------------------------------------------------
// IntOption
//-----------------------------------------------------------------------------

void IntOption::Set(const char *optarg)
{
    // Both negative and positive values are valid.
    if (sscanf(optarg,"%d",&m_val) != 1)
    {
        fprintf(stderr,"Invalid argument to --%s, integer expected\n",m_long_name);
        exit(1);
    }
}

//-----------------------------------------------------------------------------
// TimeOption
//-----------------------------------------------------------------------------

// Scan digits and return no. of digits found
static int scan_digits(const char **srcref, double *val)
{
    double y = 0;
    int n = 0;
    const char *src= *srcref;
    while (1)
    {
        int d = src[n] - '0';
        if (d<0 || d>=10)
            break;

        y = 10*y + d;
        n++;
    }
    *val = y;
    (*srcref) += n;
    return n;
}

//----------------------------------------------------------------------------

// Decode time in MM:SS.CC notation.
// "MM:" and ".CC" are optional
// Return true if successful
// Produce time in seconds in *result
static bool parse_time(const char *src, double *result)
{
    double d = 0;
    int n = scan_digits(&src, &d);
    if (n<1)
        return false;

    if (src[0] == ':' && src[1]>='0' && src[1]<='9')
    {
        // MM:SS with SS in range 0..59
        src++;
        double d1 = 0;
        (void) scan_digits(&src, &d1);
        if (d1 >= 60)
            return false;
        d = 60*d + d1;
    }

    if (src[0] == '.' && src[1]>='0' && src[1]<='9')
    {
        // Decimal ratio. Scan and decode fraction part
        src++;
        double d1 = 0;
        int n = scan_digits(&src, &d1);
        d = d + d1*pow(10,-n);
    }

    if (src[0])
        return false; // garbage at end
    *result = d;
    return true;
}

//----------------------------------------------------------------------------

void TimeOption::Set(const char *optarg)
{
    if (!parse_time(optarg, &m_val))
    {
        fprintf(stderr,"Invalid argument to --%s, minutes:seconds expected\n",m_long_name);
        exit(1);
    }
}

