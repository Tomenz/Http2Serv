/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#ifndef TRACE_H
#define TRACE_H

#include <sstream>
#include <cstdint>

using namespace std;

extern thread_local stringstream ssTrace;

void TraceOutput();
void MyTraceAdd(const uint8_t& value);
void MyTraceAdd(const wstring& value);


template<typename T>
void MyTraceAdd(const T& value) {
    ssTrace << value;
}

template<typename T>
void MyTrace([[maybe_unused]] const T& value) noexcept
{
#if defined(_DEBUG) || defined(DEBUG)
    MyTraceAdd(value);
    ssTrace << endl;
    TraceOutput();
    stringstream().swap(ssTrace);
#elif defined(_WIN32) || defined(_WIN64)
    (void)value;
#endif
}

template<typename T, typename ...Args>
void MyTrace([[maybe_unused]] const T& value, [[maybe_unused]] const Args&... rest) noexcept
{
#if defined(_DEBUG) || defined(DEBUG) || defined(Debug)
    if (ssTrace.getloc().name() != "C")
        ssTrace.imbue(locale("C"));

    MyTraceAdd(value);
    MyTrace(rest...);
#elif defined(_WIN32) || defined(_WIN64)
    (void)value;
    MyTrace(rest...);
#endif
}

#endif // TRACE_H
