
#include <thread>
#include "TempFile.h"

#if !defined (_WIN32) && !defined (_WIN64)
#define _tempnam tempnam
#endif


TempFile::TempFile() : m_bIsFile(false)
{
}

TempFile::~TempFile()
{
    if (m_bIsFile == true)
    {
        Close();
        thread([](string strTmpFileName) {
            while (remove(strTmpFileName.c_str()) != 0)
            {
                //wstringstream ss; ss << L"Error removeing tempfile: " << strTmpFileName.c_str() << L" Errorcode: " << errno << endl; OutputDebugString(ss.str().c_str());
                this_thread::sleep_for(chrono::milliseconds(100));
            }
        }, m_strTmpFileName).detach();
    }
}

void TempFile::Open()
{
    do
    {
        string strFileName(L_tmpnam, 0);
        if (std::tmpnam(&strFileName[0]))
        {
            strFileName.erase(strFileName.find_first_of('\0'));
            m_theFile.open(strFileName.c_str(), ios::out | ios::in | ios::trunc | ios::binary);
            m_strTmpFileName = strFileName;
        }
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

string TempFile::GetFileName()
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

    streampos nCurPos = m_theFile.tellg();
    m_theFile.seekg(0, ios_base::end);
    streamoff nFSize = m_theFile.tellg();
    m_theFile.seekg(nCurPos, ios_base::beg);

    if (bIsOpen == false)
        Close();

    return nFSize;
}

fstream& TempFile::operator() ()
{
    return m_theFile;
}

wostream& operator<<(wostream& os, TempFile& cTmpFile)
{
    if (cTmpFile.m_theFile.is_open() == false)
        cTmpFile.m_theFile.open(cTmpFile.m_strTmpFileName, ios::in | ios::binary);
    else
        cTmpFile.m_theFile.seekg(0, ios_base::beg);

    //  http://wordaligned.org/articles/cpp-streambufs
    //  http://www.cplusplus.com/reference/fstream/ofstream/rdbuf/

    char c = cTmpFile.m_theFile.rdbuf()->sbumpc();
    while (c != EOF)
    {
        os.rdbuf()->sputc(c);
        c = cTmpFile.m_theFile.rdbuf()->sbumpc();
    }

    cTmpFile.Close();

    return os;
}

ostream& operator<<(ostream& os, TempFile& cTmpFile)
{
    if (cTmpFile.m_theFile.is_open() == false)
        cTmpFile.m_theFile.open(cTmpFile.m_strTmpFileName, ios::in | ios::binary);
    else
        cTmpFile.m_theFile.seekg(0, ios_base::beg);

    //  http://wordaligned.org/articles/cpp-streambufs
    //  http://www.cplusplus.com/reference/fstream/ofstream/rdbuf/

    char c = cTmpFile.m_theFile.rdbuf()->sbumpc();
    while (c != EOF)
    {
        os.rdbuf()->sputc(c);
        c = cTmpFile.m_theFile.rdbuf()->sbumpc();
    }

    cTmpFile.Close();

    return os;
}
