/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#include <codecvt>

#if defined(_WIN32) || defined(_WIN64)
#include "Windows.h"
#else
#include <locale>
#include <iostream>
#endif
#include "Trace.h"

using namespace std;

thread_local stringstream ssTrace;

void MyTraceAdd(const uint8_t& value) {
    ssTrace << static_cast<int>(value);
}

void MyTraceAdd(const wstring& value) {
    ssTrace << wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(value);
}

void TraceOutput()
{
#if defined(_WIN32) || defined(_WIN64)
    ::OutputDebugStringA(ssTrace.str().c_str());
#else
    wcout << ssTrace.str().c_str();
#endif
}

