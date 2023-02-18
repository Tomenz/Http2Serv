/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#ifndef LOGFILE_H
#define LOGFILE_H

#include <sstream>
#include <map>
#include <list>
#include <atomic>
#include <mutex>

using namespace std;

class CLogFile
{
public:
    enum class LOGTYPES : uint32_t
    {
        END = 1, PUTTIME = 2
    };

    static CLogFile& GetInstance(const wstring& strLogfileName);
    CLogFile(const CLogFile& src);
    virtual ~CLogFile();

    CLogFile& operator << (const LOGTYPES lt);
    CLogFile& operator << (const uint64_t nSize);
    CLogFile& operator << (const uint32_t nSize);
    CLogFile& operator << (const string& strItem);
    CLogFile& operator << (const char* const strItem);
    CLogFile& WriteToLog();
    template<typename ...Args>
    CLogFile& WriteToLog(LOGTYPES value, const Args&... rest)
    {
        if (m_strFileName.empty() == false)
            (*this) << value;
        return WriteToLog(rest...);
    }

    template<typename T, typename ...Args>
    CLogFile& WriteToLog(const T& value, const Args&... rest)
    {
        if (m_strFileName.empty() == false)
            m_ssMsg << value;
        return WriteToLog(rest...);
    }

    static void SetDontLog(bool bDontLog = true) noexcept;

private:
    CLogFile() = delete;
    CLogFile(CLogFile&&) = delete;
    explicit CLogFile(const wstring& strLogfileName) : m_strFileName(strLogfileName), m_atThrRunning(false) {}
    CLogFile& operator=(CLogFile&&) = delete;
    CLogFile& operator=(const CLogFile&) = delete;

    void StartWriteThread(const string& szMessage);

private:
    wstring                             m_strFileName;
    list<string>                        m_lstMessages;
    mutex                               m_mtxBacklog;
    atomic<bool>                        m_atThrRunning;
    thread_local static stringstream    m_ssMsg;
    thread_local static bool            s_bDontLog;
    static map<wstring, CLogFile>       s_lstLogFiles;
};

#endif // !LOGFILE_H
