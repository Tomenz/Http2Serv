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

extern thread_local stringstream ssTrace;

void TraceOutput();
void MyTraceAdd(const uint8_t& value);
void MyTraceAdd(const wstring& value);


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
    TraceOutput();
    stringstream().swap(ssTrace);
#endif
}

template<typename T, typename ...Args>
void MyTrace(const T& value, const Args&... rest)
{
#ifdef _DEBUG
    if (ssTrace.getloc().name() != "C")
        ssTrace.imbue(locale("C"));

    MyTraceAdd(value);
    MyTrace(rest...);
#endif
}

