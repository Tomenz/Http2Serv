
#include <thread>
#include "TempFile.h"

#if defined (_WIN32) || defined (_WIN64)
#include <io.h>
#include <Windows.h>
#else
#include <stdio.h>
#include <unistd.h>
#define _tempnam tempnam
extern void OutputDebugString(const wchar_t* pOut);
extern void OutputDebugStringA(const char* pOut);
#endif

mutex TempFile::s_mxFileName;
string TempFile::s_strTempDir;
string TempFile::s_strPreFix;

TempFile::TempFile() : m_bIsFile(false)
{
    if (s_strTempDir.empty() == true)
    {
#if defined (_WIN32) || defined (_WIN64)
        const char* szTmpDir = getenv("TMP");
        if (szTmpDir == nullptr)
            szTmpDir = getenv("TEMP");
        if (szTmpDir != nullptr)
            s_strTempDir = szTmpDir;
        s_strPreFix = "H2U_" + to_string(GetCurrentProcessId()) + "_";
#else
        s_strTempDir = "/tmp/";
        s_strPreFix = "H2U_" + to_string(getpid()) + "_XXXXXX";
#endif
    }
}

TempFile::~TempFile()
{
    if (m_bIsFile == true)
    {
        Close();
        while (remove(m_strTmpFileName.c_str()) != 0)
        {
            OutputDebugStringA(string("Error removeing tempfile: " + m_strTmpFileName + " Errorcode: " + to_string(errno) + "\r\n").c_str());
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void TempFile::Open()
{
    do
    {
        lock_guard<mutex> lock(s_mxFileName);
#if defined (_WIN32) || defined (_WIN64)
        char* szTempFileName = _tempnam(s_strTempDir.c_str(), s_strPreFix.c_str());
        if (szTempFileName != nullptr)
        {
            m_strTmpFileName = string(szTempFileName) + ".tmp";
            free(szTempFileName);
            m_theFile.open(m_strTmpFileName.c_str(), ios::out | ios::in | ios::trunc | ios::binary, _SH_DENYRW);
        }
#else
        m_strTmpFileName = s_strTempDir + s_strPreFix;
        int fNr = mkstemp(&m_strTmpFileName[0]);
        if (fNr != -1)
        {
            m_theFile.open(m_strTmpFileName.c_str(), ios::out | ios::in | ios::trunc | ios::binary);
            close(fNr);
        }
#endif
    } while (m_theFile.is_open() == false);

    m_bIsFile = true;
}

void TempFile::Close()
{
    while (m_theFile.is_open() == true)
    {
        Flush();
        m_theFile.close();
    }
}

void TempFile::Flush()
{
    if (m_theFile.is_open() == true)
        m_theFile.flush();
}

void TempFile::Write(const void* szBuf, streamsize nLen)
{
    if (m_theFile.is_open() == true)
        m_theFile.write(reinterpret_cast<const char*>(szBuf), nLen);
}

streamoff TempFile::Read(unsigned char* szBuf, streamsize nLen)
{
    if (m_theFile.is_open() == false)
        m_theFile.open(m_strTmpFileName, ios::in | ios::binary);

    streamoff nCount = m_theFile.read(reinterpret_cast<char*>(szBuf), nLen).gcount();

    if (nCount == 0)
        Close();

    return nCount;
}

void TempFile::Rewind()
{
    if (m_theFile.is_open() == false)
        return;

    m_theFile.seekg(0, ios_base::beg);
}

string TempFile::GetFileName() noexcept
{
    return m_strTmpFileName;
}

streamoff TempFile::GetFileLength()
{
    bool bIsOpen = true;

    if (m_theFile.is_open() == false)
        m_theFile.open(m_strTmpFileName, ios::in | ios::binary), bIsOpen = false;
    else
        Flush();

    if (m_theFile.is_open() == false)
        return 0;

    streampos nCurPos = m_theFile.tellg();
    m_theFile.seekg(0, ios_base::end);
    streamoff nFSize = m_theFile.tellg();
    m_theFile.seekg(nCurPos, ios_base::beg);

    if (bIsOpen == false)
        Close();

    return nFSize;
}

fstream& TempFile::operator() () noexcept
{
    return m_theFile;
}

wostream& operator<<(wostream& os, TempFile& cTmpFile)
{
    if (cTmpFile.m_theFile.is_open() == false)
        cTmpFile.m_theFile.open(cTmpFile.m_strTmpFileName, ios::in | ios::binary);

    cTmpFile.m_theFile.seekg(0, ios_base::end);
    streamoff nFSize = cTmpFile.m_theFile.tellg();
    cTmpFile.m_theFile.seekg(0, ios_base::beg);

    //  http://wordaligned.org/articles/cpp-streambufs
    //  http://www.cplusplus.com/reference/fstream/ofstream/rdbuf/

    while (nFSize--)
        os.rdbuf()->sputc(cTmpFile.m_theFile.rdbuf()->sbumpc());

    cTmpFile.Close();

    return os;
}

ostream& operator<<(ostream& os, TempFile& cTmpFile)
{
    if (cTmpFile.m_theFile.is_open() == false)
        cTmpFile.m_theFile.open(cTmpFile.m_strTmpFileName, ios::in | ios::binary);

    cTmpFile.m_theFile.seekg(0, ios_base::end);
    streamoff nFSize = cTmpFile.m_theFile.tellg();
    cTmpFile.m_theFile.seekg(0, ios_base::beg);

    //  http://wordaligned.org/articles/cpp-streambufs
    //  http://www.cplusplus.com/reference/fstream/ofstream/rdbuf/

    while (nFSize-- && cTmpFile.m_theFile.is_open() == true)
        os.rdbuf()->sputc(cTmpFile.m_theFile.rdbuf()->sbumpc());

    cTmpFile.Close();

    return os;
}
