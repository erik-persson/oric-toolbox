//-----------------------------------------------------------------------------
//
//  Option -- C++ wrapper around getopt_long
//
//  Copyright (c) 2006-2022 Erik Persson
//
//-----------------------------------------------------------------------------

#ifndef OPTION_H
#define OPTION_H

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// Option
//-----------------------------------------------------------------------------

// Base class for different options
class Option
{
    Option *m_next;
protected:
    char m_c;                 // Character or number<=32 for no short form
    const char *m_long_name;
    const char *m_help;
    bool m_given;             // was the option given on the command line?

    static int GetCount();
    virtual bool RequiresArgument() = 0;
    virtual void Set(const char *optarg) = 0;

public:

    Option(char c, const char *long_name, const char *help);
    virtual ~Option() {}

    // Print the help text to stderr
    static void Help();

    // Parse command line arguments
    static int Parse(int argc, char **argv);
};

//-----------------------------------------------------------------------------
// BoolOption
//-----------------------------------------------------------------------------

// Class for defining a boolean-valued option
class BoolOption : public Option
{
public:
    BoolOption(char c, const char *long_name, const char *help)
        : Option(c,long_name,help)
    {}

    operator bool() { return m_given; }
    virtual void Set(const char *) override {}
    virtual bool RequiresArgument() override { return false; };
};

//-----------------------------------------------------------------------------
// IntOption
//-----------------------------------------------------------------------------

// Class for defining a integer-valued option
class IntOption : public Option
{
    int m_val;
public:
    IntOption(char c, const char *long_name, const char *help, int default_value)
        : Option(c,long_name,help)
    {
         m_val = default_value;
    }

    operator int() { return m_val; }
    virtual void Set(const char *optarg) override;
    virtual bool RequiresArgument() override { return true; };
};

//-----------------------------------------------------------------------------
// StringOption
//-----------------------------------------------------------------------------

// Class for defining a string-valued option
class StringOption : public Option
{
    char *m_val; // malloced string or 0
public:
    StringOption(char c, const char *long_name, const char *help,
        const char *default_value) // may be 0
        : Option(c,long_name,help)
    {
        m_val = default_value? strdup(default_value) : 0;
    }

    virtual ~StringOption() { if (m_val) free(m_val); }

    operator const char *() { return m_val; }

    virtual void Set(const char *optarg) override
    {
        if (m_val)
            free(m_val);
        m_val= strdup(optarg);
    }

    virtual bool RequiresArgument() override { return true; };
};

//-----------------------------------------------------------------------------
// TimeOption
//-----------------------------------------------------------------------------

// Class for defining an option representing a time in seconds
class TimeOption : public Option
{
    double m_val;
public:
    TimeOption(char c, const char *long_name, const char *help, double default_value)
        : Option(c,long_name,help)
    {
         m_val = default_value;
    }

    operator double() { return m_val; }
    virtual void Set(const char *optarg) override;
    virtual bool RequiresArgument() override { return true; };
};

#endif // OPTION_H
