/* Copyright (C) Hauck Software Solutions - All Rights Reserved
 * You may use, distribute and modify this code under the terms
 * that changes to the code must be reported back the original
 * author
 *
 * Company: Hauck Software Solutions
 * Author:  Thomas Hauck
 * Email:   Thomas@fam-hauck.de
 *
 */

#pragma once

#include <sstream>

using namespace std;

thread_local stringstream ssTrace;

void MyTraceAdd(const uint8_t& value) {
    ssTrace << static_cast<int>(value);
}

template<typename T>
void MyTraceAdd(const T& value) {
    ssTrace << value;
}

template<typename T>
void MyTrace(const T& value)
{
#ifdef _DEBUG
    MyTraceAdd(value);
    ssTrace << endl;
#if defined(_WIN32) || defined(_WIN64)
    ::OutputDebugStringA(ssTrace.str().c_str());
#else
    wcout << ssTrace.str().c_str();
#endif
    stringstream().swap(ssTrace);
#endif
}

template<typename T, typename ...Args>
void MyTrace(const T& value, const Args&... rest)
{
#ifdef _DEBUG
    if (ssTrace.getloc().name().compare("C") != 0)
        ssTrace.imbue(locale("C"));

    MyTraceAdd(value);
    MyTrace(rest...);
#endif
}

