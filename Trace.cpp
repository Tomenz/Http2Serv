
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

