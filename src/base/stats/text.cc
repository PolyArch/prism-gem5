/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Nathan Binkert
 */

#if defined(__APPLE__)
#define _GLIBCPP_USE_C99 1
#endif

#if defined(__sun)
#include <math.h>
#endif

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

#include "base/misc.hh"
#include "base/statistics.hh"
#include "base/stats/text.hh"
#include "base/stats/visit.hh"

using namespace std;

#ifndef NAN
float __nan();
/** Define Not a number. */
#define NAN (__nan())
/** Need to define __nan() */
#define __M5_NAN
#endif

#ifdef __M5_NAN
float
__nan()
{
    union {
        uint32_t ui;
        float f;
    } nan;

    nan.ui = 0x7fc00000;
    return nan.f;
}
#endif

namespace Stats {

Text::Text()
    : mystream(false), stream(NULL), compat(false), descriptions(false)
{
}

Text::Text(std::ostream &stream)
    : mystream(false), stream(NULL), compat(false), descriptions(false)
{
    open(stream);
}

Text::Text(const std::string &file)
    : mystream(false), stream(NULL), compat(false), descriptions(false)
{
    open(file);
}


Text::~Text()
{
    if (mystream) {
        assert(stream);
        delete stream;
    }
}

void
Text::open(std::ostream &_stream)
{
    if (stream)
        panic("stream already set!");

    mystream = false;
    stream = &_stream;
    if (!valid())
        fatal("Unable to open output stream for writing\n");
}

void
Text::open(const std::string &file)
{
    if (stream)
        panic("stream already set!");

    mystream = true;
    stream = new ofstream(file.c_str(), ios::trunc);
    if (!valid())
        fatal("Unable to open statistics file for writing\n");
}

bool
Text::valid() const
{
    return stream != NULL && stream->good();
}

void
Text::output()
{
    ccprintf(*stream, "\n---------- Begin Simulation Statistics ----------\n");
    list<Info *>::const_iterator i, end = statsList().end();
    for (i = statsList().begin(); i != end; ++i)
        (*i)->visit(*this);
    ccprintf(*stream, "\n---------- End Simulation Statistics   ----------\n");
    stream->flush();
}

bool
Text::noOutput(const Info &info)
{
    if (!(info.flags & print))
        return true;

    if (info.prereq && info.prereq->zero())
        return true;

    return false;
}

string
ValueToString(Result value, int precision, bool compat)
{
    stringstream val;

    if (!isnan(value)) {
        if (precision != -1)
            val.precision(precision);
        else if (value == rint(value))
            val.precision(0);

        val.unsetf(ios::showpoint);
        val.setf(ios::fixed);
        val << value;
    } else {
        val << (compat ? "<err: div-0>" : "no value");
    }

    return val.str();
}

struct ScalarPrint
{
    Result value;
    string name;
    string desc;
    StatFlags flags;
    bool compat;
    bool descriptions;
    int precision;
    Result pdf;
    Result cdf;

    void operator()(ostream &stream) const;
};

void
ScalarPrint::operator()(ostream &stream) const
{
    if ((flags & nozero && value == 0.0) ||
        (flags & nonan && isnan(value)))
        return;

    stringstream pdfstr, cdfstr;

    if (!isnan(pdf))
        ccprintf(pdfstr, "%.2f%%", pdf * 100.0);

    if (!isnan(cdf))
        ccprintf(cdfstr, "%.2f%%", cdf * 100.0);

    if (compat && flags & __substat) {
        ccprintf(stream, "%32s %12s %10s %10s", name,
                 ValueToString(value, precision, compat), pdfstr, cdfstr);
    } else {
        ccprintf(stream, "%-40s %12s %10s %10s", name,
                 ValueToString(value, precision, compat), pdfstr, cdfstr);
    }

    if (descriptions) {
        if (!desc.empty())
            ccprintf(stream, " # %s", desc);
    }
    stream << endl;
}

struct VectorPrint
{
    string name;
    string desc;
    vector<string> subnames;
    vector<string> subdescs;
    StatFlags flags;
    bool compat;
    bool descriptions;
    int precision;
    VResult vec;
    Result total;

    void operator()(ostream &stream) const;
};

void
VectorPrint::operator()(std::ostream &stream) const
{
    size_type _size = vec.size();
    Result _total = 0.0;

    if (flags & (pdf | cdf)) {
        for (off_type i = 0; i < _size; ++i) {
            _total += vec[i];
        }
    }

    string base = name + (compat ? "_" : "::");

    ScalarPrint print;
    print.name = name;
    print.desc = desc;
    print.compat = compat;
    print.precision = precision;
    print.descriptions = descriptions;
    print.flags = flags;
    print.pdf = NAN;
    print.cdf = NAN;

    bool havesub = !subnames.empty();

    if (_size == 1) {
        print.value = vec[0];
        print(stream);
    } else if (!compat) {
        for (off_type i = 0; i < _size; ++i) {
            if (havesub && (i >= subnames.size() || subnames[i].empty()))
                continue;

            print.name = base + (havesub ? subnames[i] : to_string(i));
            print.desc = subdescs.empty() ? desc : subdescs[i];
            print.value = vec[i];

            if (_total && (flags & pdf)) {
                print.pdf = vec[i] / _total;
                print.cdf += print.pdf;
            }

            print(stream);
        }

        if (flags & ::Stats::total) {
            print.name = base + "total";
            print.desc = desc;
            print.value = total;
            print(stream);
        }
    } else {
        if (flags & ::Stats::total) {
            print.value = total;
            print(stream);
        }

        Result _pdf = 0.0;
        Result _cdf = 0.0;
        if (flags & dist) {
            ccprintf(stream, "%s.start_dist\n", name);
            for (off_type i = 0; i < _size; ++i) {
                print.name = havesub ? subnames[i] : to_string(i);
                print.desc = subdescs.empty() ? desc : subdescs[i];
                print.flags |= __substat;
                print.value = vec[i];

                if (_total) {
                    _pdf = vec[i] / _total;
                    _cdf += _pdf;
                }

                if (flags & pdf)
                    print.pdf = _pdf;
                if (flags & cdf)
                    print.cdf = _cdf;

                print(stream);
            }
            ccprintf(stream, "%s.end_dist\n", name);
        } else {
            for (off_type i = 0; i < _size; ++i) {
                if (havesub && subnames[i].empty())
                    continue;

                print.name = base;
                print.name += havesub ? subnames[i] : to_string(i);
                print.desc = subdescs.empty() ? desc : subdescs[i];
                print.value = vec[i];

                if (_total) {
                    _pdf = vec[i] / _total;
                    _cdf += _pdf;
                } else {
                    _pdf = _cdf = NAN;
                }

                if (flags & pdf) {
                    print.pdf = _pdf;
                    print.cdf = _cdf;
                }

                print(stream);
            }
        }
    }
}

struct DistPrint
{
    string name;
    string desc;
    StatFlags flags;
    bool compat;
    bool descriptions;
    int precision;

    Counter min;
    Counter max;
    Counter bucket_size;
    size_type size;
    bool fancy;

    const DistData &data;

    DistPrint(const Text *text, const DistInfoBase &info);
    DistPrint(const Text *text, const VectorDistInfoBase &info, int i);
    void init(const Text *text, const Info &info, const DistParams *params);
    void operator()(ostream &stream) const;
};

DistPrint::DistPrint(const Text *text, const DistInfoBase &info)
    : data(info.data)
{
    init(text, info, safe_cast<const DistParams *>(info.storageParams));
}

DistPrint::DistPrint(const Text *text, const VectorDistInfoBase &info, int i)
    : data(info.data[i])
{
    init(text, info, safe_cast<const DistParams *>(info.storageParams));

    name = info.name + "_" +
        (info.subnames[i].empty() ? (to_string(i)) : info.subnames[i]);

    if (!info.subdescs[i].empty())
        desc = info.subdescs[i];
}

void
DistPrint::init(const Text *text, const Info &info, const DistParams *params)
{
    name = info.name;
    desc = info.desc;
    flags = info.flags;
    precision = info.precision;
    compat = text->compat;
    descriptions = text->descriptions;

    fancy = params->fancy;
    min = params->min;
    max = params->max;
    bucket_size = params->bucket_size;
    size = params->buckets;
}

void
DistPrint::operator()(ostream &stream) const
{
    Result stdev = NAN;
    if (data.samples)
        stdev = sqrt((data.samples * data.squares - data.sum * data.sum) /
                     (data.samples * (data.samples - 1.0)));

    if (fancy) {
        ScalarPrint print;
        string base = name + (compat ? "_" : "::");

        print.precision = precision;
        print.flags = flags;
        print.compat = compat;
        print.descriptions = descriptions;
        print.desc = desc;
        print.pdf = NAN;
        print.cdf = NAN;

        print.name = base + "mean";
        print.value = data.samples ? data.sum / data.samples : NAN;
        print(stream);

        print.name = base + "stdev";
        print.value = stdev;
        print(stream);

        print.name = "**Ignore: " + base + "TOT";
        print.value = data.samples;
        print(stream);
        return;
    }

    assert(size == data.cvec.size());

    Result total = 0.0;

    total += data.underflow;
    for (off_type i = 0; i < size; ++i)
        total += data.cvec[i];
    total += data.overflow;

    string base = name + (compat ? "." : "::");

    ScalarPrint print;
    print.desc = compat ? "" : desc;
    print.flags = flags;
    print.compat = compat;
    print.descriptions = descriptions;
    print.precision = precision;
    print.pdf = NAN;
    print.cdf = NAN;

    if (compat) {
        ccprintf(stream, "%-42s", base + "start_dist");
        if (descriptions && !desc.empty())
            ccprintf(stream, "                     # %s", desc);
        stream << endl;
    }

    print.name = base + "samples";
    print.value = data.samples;
    print(stream);

    print.name = base + "min_value";
    print.value = data.min_val;
    print(stream);

    if (!compat || data.underflow > 0.0) {
        print.name = base + "underflows";
        print.value = data.underflow;
        if (!compat && total) {
            print.pdf = data.underflow / total;
            print.cdf += print.pdf;
        }
        print(stream);
    }

    if (!compat) {
        for (off_type i = 0; i < size; ++i) {
            stringstream namestr;
            namestr << base;

            Counter low = i * bucket_size + min;
            Counter high = ::min(low + bucket_size, max);
            namestr << low;
            if (low < high)
                namestr << "-" << high;

            print.name = namestr.str();
            print.value = data.cvec[i];
            if (total) {
                print.pdf = data.cvec[i] / total;
                print.cdf += print.pdf;
            }
            print(stream);
        }
    } else {
        Counter _min;
        Result _pdf;
        Result _cdf = 0.0;

        print.flags = flags | __substat;

        for (off_type i = 0; i < size; ++i) {
            if ((flags & nozero && data.cvec[i] == 0.0) ||
                (flags & nonan && isnan(data.cvec[i])))
                continue;

            _min = i * bucket_size + min;
            _pdf = data.cvec[i] / total * 100.0;
            _cdf += _pdf;


            print.name = ValueToString(_min, 0, compat);
            print.value = data.cvec[i];
            print.pdf = (flags & pdf) ? _pdf : NAN;
            print.cdf = (flags & cdf) ? _cdf : NAN;
            print(stream);
        }

        print.flags = flags;
    }

    if (!compat || data.overflow > 0.0) {
        print.name = base + "overflows";
        print.value = data.overflow;
        if (!compat && total) {
            print.pdf = data.overflow / total;
            print.cdf += print.pdf;
        } else {
            print.pdf = NAN;
            print.cdf = NAN;
        }
        print(stream);
    }

    print.pdf = NAN;
    print.cdf = NAN;

    if (!compat) {
        print.name = base + "total";
        print.value = total;
        print(stream);
    }

    print.name = base + "max_value";
    print.value = data.max_val;
    print(stream);

    if (!compat && data.samples != 0) {
        print.name = base + "mean";
        print.value = data.sum / data.samples;
        print(stream);

        print.name = base + "stdev";
        print.value = stdev;
        print(stream);
    }

    if (compat)
        ccprintf(stream, "%send_dist\n\n", base);
}

void
Text::visit(const ScalarInfoBase &info)
{
    if (noOutput(info))
        return;

    ScalarPrint print;
    print.value = info.result();
    print.name = info.name;
    print.desc = info.desc;
    print.flags = info.flags;
    print.compat = compat;
    print.descriptions = descriptions;
    print.precision = info.precision;
    print.pdf = NAN;
    print.cdf = NAN;

    print(*stream);
}

void
Text::visit(const VectorInfoBase &info)
{
    if (noOutput(info))
        return;

    size_type size = info.size();
    VectorPrint print;

    print.name = info.name;
    print.desc = info.desc;
    print.flags = info.flags;
    print.compat = compat;
    print.descriptions = descriptions;
    print.precision = info.precision;
    print.vec = info.result();
    print.total = info.total();

    if (!info.subnames.empty()) {
        for (off_type i = 0; i < size; ++i) {
            if (!info.subnames[i].empty()) {
                print.subnames = info.subnames;
                print.subnames.resize(size);
                for (off_type i = 0; i < size; ++i) {
                    if (!info.subnames[i].empty() &&
                        !info.subdescs[i].empty()) {
                        print.subdescs = info.subdescs;
                        print.subdescs.resize(size);
                        break;
                    }
                }
                break;
            }
        }
    }

    print(*stream);
}

void
Text::visit(const Vector2dInfoBase &info)
{
    if (noOutput(info))
        return;

    bool havesub = false;
    VectorPrint print;

    print.subnames = info.y_subnames;
    print.flags = info.flags;
    print.compat = compat;
    print.descriptions = descriptions;
    print.precision = info.precision;

    if (!info.subnames.empty()) {
        for (off_type i = 0; i < info.x; ++i)
            if (!info.subnames[i].empty())
                havesub = true;
    }

    VResult tot_vec(info.y);
    Result super_total = 0.0;
    for (off_type i = 0; i < info.x; ++i) {
        if (havesub && (i >= info.subnames.size() || info.subnames[i].empty()))
            continue;

        off_type iy = i * info.y;
        VResult yvec(info.y);

        Result total = 0.0;
        for (off_type j = 0; j < info.y; ++j) {
            yvec[j] = info.cvec[iy + j];
            tot_vec[j] += yvec[j];
            total += yvec[j];
            super_total += yvec[j];
        }

        print.name = info.name + "_" +
            (havesub ? info.subnames[i] : to_string(i));
        print.desc = info.desc;
        print.vec = yvec;
        print.total = total;
        print(*stream);
    }

    if ((info.flags & ::Stats::total) && (info.x > 1)) {
        print.name = info.name;
        print.desc = info.desc;
        print.vec = tot_vec;
        print.total = super_total;
        print(*stream);
    }
}

void
Text::visit(const DistInfoBase &info)
{
    if (noOutput(info))
        return;

    DistPrint print(this, info);
    print(*stream);
}

void
Text::visit(const VectorDistInfoBase &info)
{
    if (noOutput(info))
        return;

    for (off_type i = 0; i < info.size(); ++i) {
        DistPrint print(this, info, i);
        print(*stream);
    }
}

void
Text::visit(const FormulaInfoBase &info)
{
    visit((const VectorInfoBase &)info);
}

bool
initText(const string &filename, bool desc, bool compat)
{
    static Text text;
    static bool connected = false;

    if (connected)
        return false;

    extern list<Output *> OutputList;

    text.open(*simout.find(filename));
    text.descriptions = desc;
    text.compat = compat;
    OutputList.push_back(&text);
    connected = true;

    return true;
}

/* namespace Stats */ }
