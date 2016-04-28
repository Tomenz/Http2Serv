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

#include <map>
#include <atomic>
#include <mutex>
#include <fstream>
#include <list>
#include <chrono>
#include <locale>

#if defined(_WIN32) || defined(_WIN64)
#define FN_STR(x) x
#else
#ifndef _UTFCONVERTER
#define _UTFCONVERTER
std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> Utf8Converter;
#endif
#define FN_STR(x) Utf8Converter.to_bytes(x).c_str()
#endif

using namespace std;
using namespace std::chrono;

class CLogFile
{
public:
    enum class LOGTYPES : uint32_t
    {
        END = 1, PUTTIME = 2
    };

    static CLogFile& GetInstance(const wstring& strLogfileName)
    {
        if (m_ssMsg.getloc().name().compare("C") != 0)
            m_ssMsg.imbue(locale("C"));

        auto instance = s_lstLogFiles.find(strLogfileName);
        if (instance == s_lstLogFiles.end())
        {
            s_lstLogFiles[strLogfileName] = move(CLogFile(strLogfileName));
            return ref(s_lstLogFiles[strLogfileName]);
        }

        return instance->second;
    }

    CLogFile& operator << (const LOGTYPES lt)
    {
        if (lt == LOGTYPES::END)
        {
            m_ssMsg << "\r\n";
            StartWriteThread(m_ssMsg.str().c_str());
            stringstream().swap(m_ssMsg);
        }
        else if (lt == LOGTYPES::PUTTIME)
        {
            auto in_time_t = system_clock::to_time_t(system_clock::now());
            struct tm* stTime = ::localtime(&in_time_t);
            char *pattern = "%d/%b/%Y:%H:%M:%S %z";
            use_facet <time_put <char> >(locale("C")).put(m_ssMsg.rdbuf(), m_ssMsg, ' ', stTime, pattern, pattern + strlen(pattern));
        }

        return *this;
    }

    CLogFile& operator << (const uint64_t nSize)
    {
        m_ssMsg << nSize;
        return *this;
    }

    CLogFile& operator << (const size_t nSize)
    {
        m_ssMsg << nSize;
        return *this;
    }

    CLogFile& operator << (const string& strItem)
    {
        m_ssMsg << strItem;
        return *this;
    }

    CLogFile& operator << (const char* const strItem)
    {
        m_ssMsg << strItem;
        return *this;
    }

    CLogFile& WriteToLog()
    {
        m_ssMsg << "\r\n";
        StartWriteThread(m_ssMsg.str().c_str());
        stringstream().swap(m_ssMsg);

        return *this;
    }

    template<typename ...Args>
    CLogFile& WriteToLog(LOGTYPES value, const Args&... rest)
    {
        (*this) << value;
        return WriteToLog(rest...);
    }

    template<typename T, typename ...Args>
    CLogFile& WriteToLog(const T& value, const Args&... rest)
    {
        m_ssMsg << value;
        return WriteToLog(rest...);
    }

    CLogFile() {};
    virtual ~CLogFile()
    {
        m_mtxBacklog.lock();
        while (m_lstMessages.size() > 0 || m_atThrRunning == true)
        {
            m_mtxBacklog.unlock();
            this_thread::sleep_for(milliseconds(1));
            m_mtxBacklog.lock();
        }
        m_mtxBacklog.unlock();
    }

private:
    CLogFile(const wstring& strLogfileName) : m_strFileName(strLogfileName), m_atThrRunning(false)
    {
    }

    CLogFile& operator =(const CLogFile& src)
    {
        m_strFileName = src.m_strFileName;
        m_atThrRunning.store(src.m_atThrRunning);
        return *this;
    }

    void StartWriteThread(const char* const szMessage)
    {
        lock_guard<mutex>lock(m_mtxBacklog);
        m_lstMessages.push_back(szMessage);

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
                        this_thread::sleep_for(microseconds(1));
                        m_mtxBacklog.lock();
                    }

                    fout.close();

                    bool bTmp = true;
                    m_atThrRunning.compare_exchange_strong(bTmp, false);

                    m_mtxBacklog.unlock();
                }
                else
                {
                    m_lstMessages.clear();
                    bool bTmp = true;
                    m_atThrRunning.compare_exchange_strong(bTmp, false);
                }
            }).detach();
        }
    }

private:
    wstring m_strFileName;
    list<string> m_lstMessages;
    mutex m_mtxBacklog;
    atomic<bool> m_atThrRunning;
    thread_local static stringstream m_ssMsg;
    static map<wstring, CLogFile> s_lstLogFiles;
};

map<wstring, CLogFile>CLogFile::s_lstLogFiles;
thread_local stringstream CLogFile::m_ssMsg;
