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

#ifndef TEMPFILE_H
#define TEMPFILE_H

#include <fstream>
#include <mutex>
#include <string>

using namespace std;

class TempFile
{
public:
    friend wostream& operator<<(wostream&, TempFile&);
    friend ostream& operator<<(ostream& os, TempFile& cTmpFile);

    TempFile();
    virtual ~TempFile();
    void Open();
    void Close();
    void Flush();
    void Write(const void* szBuf, streamsize nLen);
    streamoff Read(unsigned char* szBuf, streamsize nLen);
    void Rewind();
    string GetFileName() noexcept;
    streamoff GetFileLength();
    fstream& operator() () noexcept;

private:
    string  m_strTmpFileName;
    fstream m_theFile;
    bool    m_bIsFile;

    static mutex  s_mxFileName;
    static string s_strTempDir;
    static string s_strPreFix;
};

#endif // !TEMPFILE_H
