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

#include <fstream>
#include <chrono>
#include <locale>

#include "LogFile.h"

#if defined(_WIN32) || defined(_WIN64)
#define FN_STR(x) x
#else
#include <thread>
#include <codecvt>
#define FN_STR(x) wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(x).c_str()
#endif

using namespace std::chrono;

map<wstring, CLogFile>CLogFile::s_lstLogFiles;
thread_local stringstream CLogFile::m_ssMsg;
thread_local bool CLogFile::s_bDontLog = false;

CLogFile& CLogFile::GetInstance(const wstring& strLogfileName)
{
    if (m_ssMsg.getloc().name().compare("C") != 0)
        m_ssMsg.imbue(locale("C"));

    auto instance = s_lstLogFiles.find(strLogfileName);
    if (instance == s_lstLogFiles.end())
    {
        s_lstLogFiles.emplace(strLogfileName, CLogFile(strLogfileName));
        return s_lstLogFiles.find(strLogfileName)->second;
    }

    return instance->second;
}

CLogFile::CLogFile(const CLogFile& src)
{
    m_strFileName = src.m_strFileName;
    m_lstMessages = src.m_lstMessages;
    m_atThrRunning.store(src.m_atThrRunning);
}

CLogFile::~CLogFile()
{
    m_mtxBacklog.lock();
    while (m_lstMessages.size() > 0 || m_atThrRunning == true)
    {
        m_mtxBacklog.unlock();
        this_thread::sleep_for(milliseconds(10));
        m_mtxBacklog.lock();
    }
    m_mtxBacklog.unlock();
}

CLogFile& CLogFile::operator << (const LOGTYPES lt)
{
    if (lt == LOGTYPES::END)
    {
        if (m_strFileName.empty() == false && s_bDontLog == false)
        {
            m_ssMsg << "\r\n";
            StartWriteThread(m_ssMsg.str());
        }
        stringstream().swap(m_ssMsg);
    }
    else if (lt == LOGTYPES::PUTTIME && m_strFileName.empty() == false)
    {
        auto in_time_t = system_clock::to_time_t(system_clock::now());
        struct tm* stTime = ::localtime(&in_time_t);
        const static string pattern = "%d/%b/%Y:%H:%M:%S %z";
        use_facet <time_put <char> >(locale("C")).put(m_ssMsg.rdbuf(), m_ssMsg, ' ', stTime, pattern.c_str(), pattern.c_str() + pattern.size());
    }

    return *this;
}

CLogFile& CLogFile::operator << (const uint64_t nSize)
{
    if (m_strFileName.empty() == false)
        m_ssMsg << nSize;
    return *this;
}

CLogFile& CLogFile::operator << (const uint32_t nSize)
{
    if (m_strFileName.empty() == false)
        m_ssMsg << nSize;
    return *this;
}

CLogFile& CLogFile::operator << (const string& strItem)
{
    if (m_strFileName.empty() == false)
        m_ssMsg << strItem;
    return *this;
}

CLogFile& CLogFile::operator << (const char* const strItem)
{
    if (m_strFileName.empty() == false)
        m_ssMsg << strItem;
    return *this;
}

CLogFile& CLogFile::WriteToLog()
{
    if (m_strFileName.empty() == false && s_bDontLog == false)
    {
        m_ssMsg << "\r\n";
        StartWriteThread(m_ssMsg.str());
    }
    stringstream().swap(m_ssMsg);

    return *this;
}

void CLogFile::SetDontLog(bool bDontLog/* = true*/)
{
    s_bDontLog = bDontLog;
}

void CLogFile::StartWriteThread(const string& szMessage)
{
    lock_guard<mutex>lock(m_mtxBacklog);
    m_lstMessages.emplace_back(szMessage);

    bool bTmp = false;
    if (m_atThrRunning.compare_exchange_strong(bTmp, true) == true)
    {
        thread([&]() {
            fstream fout;
            fout.open(FN_STR(m_strFileName), ios::out | ios::app | ios::binary);
            if (fout.is_open() == true)
            {
                m_mtxBacklog.lock();
                while (m_lstMessages.size() > 0)
                {
                    auto msg = *begin(m_lstMessages);
                    m_lstMessages.pop_front();
                    m_mtxBacklog.unlock();

                    fout.write(msg.c_str(), msg.size());

                    m_mtxBacklog.lock();
                }

                fout.close();

                atomic_exchange(&m_atThrRunning, false);

                m_mtxBacklog.unlock();
            }
            else
            {
                m_lstMessages.clear();
                atomic_exchange(&m_atThrRunning, false);
            }
        }).detach();
    }
}

