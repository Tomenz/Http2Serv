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

#include <stdlib.h>
#include <fstream>
#include <memory>
#include <chrono>

using namespace std;

#if !defined (_WIN32) && !defined (_WIN64)
#define _tempnam tempnam
#endif

class TempFile
{
public:
    friend wostream& operator<<(wostream&, TempFile&);
    friend ostream& operator<<(ostream& os, TempFile& cTmpFile);

    TempFile() : m_bIsFile(false)
    {
    }

    virtual ~TempFile()
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

    void Open()
    {
        do
        {
            auto upFileName = make_unique<char[]>(L_tmpnam);
            if (std::tmpnam(upFileName.get()))
            {
                m_theFile.open(upFileName.get(), ios::out | ios::in | ios::trunc | ios::binary);
                m_strTmpFileName = upFileName.get();
            }
        } while (m_theFile.is_open() == false);

        m_bIsFile = true;
    }

    void Close()
    {
        while (m_theFile.is_open() == true)
        {
            Flush();
            m_theFile.close();
        }
    }

    void Flush()
    {
        if (m_theFile.is_open() == true)
            m_theFile.flush();
    }

    void Write(const void* szBuf, streamsize nLen)
    {
        if (m_theFile.is_open() == true)
            m_theFile.write(reinterpret_cast<const char*>(szBuf), nLen);
    }

    streamoff Read(unsigned char* szBuf, streamsize nLen)
    {
        if (m_theFile.is_open() == false)
            m_theFile.open(m_strTmpFileName, ios::in | ios::binary);

        streamoff nCount = m_theFile.read(reinterpret_cast<char*>(szBuf), nLen).gcount();

        if (nCount == 0)
            Close();

        return nCount;
    }

    void Rewind()
    {
        if (m_theFile.is_open() == false)
            return;

        m_theFile.seekg(0, ios_base::beg);
    }

    string GetFileName()
    {
        return m_strTmpFileName;
    }

    streamoff GetFileLength()
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

    fstream& operator() ()
    {
        return m_theFile;
    }

private:
    string  m_strTmpFileName;
    fstream m_theFile;
    bool    m_bIsFile;
};

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
