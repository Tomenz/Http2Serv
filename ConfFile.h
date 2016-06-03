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
#include <unordered_map>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <codecvt>

#if defined(_WIN32) || defined(_WIN64)
#define FN_CA(x) x.c_str()
#define FN_STR(x) x
#else
#include <locale>
#include <sys/stat.h>
#include <math.h>
#define _stat stat
#define _wstat stat
#ifndef _UTFCONVERTER
#define _UTFCONVERTER
std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> Utf8Converter;
#endif
#define FN_CA(x) Utf8Converter.to_bytes(x).c_str()
#define FN_STR(x) Utf8Converter.to_bytes(x).c_str()
#endif

using namespace std;

class ConfFile
{
public:
    static ConfFile& GetInstance(const wstring& strConfigFile)
    {
        const auto& instance = s_lstConfFiles.find(strConfigFile);
        if (instance == end(s_lstConfFiles))
        {
            s_lstConfFiles[strConfigFile] = move(ConfFile(strConfigFile));
            return ref(s_lstConfFiles[strConfigFile]);
        }

        return instance->second;
    }

    vector<wstring> get()
    {
        lock_guard<mutex> lock(m_mtxLoad);

        if (m_mSections.empty() == true || AreFilesModifyed() == true)
        {
            m_mSections.clear();
            LoadFile(m_strFileName);
        }

        vector<wstring> vReturn;

        for (const auto& item : m_mSections)
        {
            vReturn.push_back(item.first);
        }

        return vReturn;
    }

    vector<wstring> get(const wstring& strSektion)
    {
        lock_guard<mutex> lock(m_mtxLoad);

        if (m_mSections.empty() == true || AreFilesModifyed() == true)
        {
            m_mSections.clear();
            LoadFile(m_strFileName);
        }

        vector<wstring> vReturn;

        const auto& section = m_mSections.find(strSektion);
        if (section != end(m_mSections))
        {
            for (const auto& item : section->second)
            {
                if (vReturn.size() == 0 || vReturn[vReturn.size() - 1].compare(item.first) != 0)
                    vReturn.push_back(item.first);
            }
        }
        return vReturn;
    }

    vector<wstring> get(const wstring& strSektion, const wstring& strValue)
    {
        lock_guard<mutex> lock(m_mtxLoad);

        if (m_mSections.empty() == true || AreFilesModifyed() == true)
        {
            m_mSections.clear();
            LoadFile(m_strFileName);
        }

        vector<wstring> vReturn;

        const auto& section = m_mSections.find(strSektion);
        if (section != end(m_mSections))
        {
            auto item = section->second.equal_range(strValue);
            for (; item.first != item.second; ++item.first)
            {
                //return item.first->second;
                vReturn.push_back(item.first->second);
            }
        }

        return vReturn;
    }

    ConfFile() {};
    virtual ~ConfFile() {}

private:
    ConfFile(const wstring& strConfigFile) : m_strFileName(strConfigFile), m_tFileTime(0)
    {

    };

    ConfFile& operator =(const ConfFile& src)
    {
        m_strFileName = src.m_strFileName;
        return *this;
    }

    void LoadFile(const wstring& strFilename)
    {
        wifstream fin;
        fin.open(FN_STR(strFilename), ios::in | ios::binary);
        if (fin.is_open() == true)
        {
            fin.imbue(std::locale(fin.getloc(), new codecvt_utf8<wchar_t>));

            unordered_multimap<wstring, wstring>* LastSection = nullptr;
            auto TrimString = [](wstring strVal) -> wstring
            {
                size_t nPos = strVal.find_last_not_of(L" \t\r\n");
                strVal.erase(nPos + 1);  // Trim Whitespace character on the right
                nPos = strVal.find_first_not_of(L" \t");
                if (nPos != string::npos) strVal.erase(0, nPos);
                return strVal;
            };

            while (fin.eof() == false)
            {
                wstring strLine;
                getline(fin, strLine);

                size_t nPos = strLine.find_first_of(L"#;");
                if (nPos != wstring::npos) strLine.erase(nPos);   // erase commends from line
                nPos = strLine.find(L"//");
                if (nPos != wstring::npos) strLine.erase(nPos);   // erase commends from line
                strLine = TrimString(strLine);

                if (strLine.empty() == false)
                {
                    if (strLine[0] == L'[' && strLine[strLine.size() - 1] == L']')
                    {
                        const auto strTmp = TrimString(strLine.substr(1, strLine.size() - 2));
                        if (strTmp.empty() == false)
                        {
                            const auto& paRet = m_mSections.insert(make_pair(strTmp, unordered_multimap<wstring, wstring>()));
                            if (paRet.second == true)
                            {
                                LastSection = &paRet.first->second;
                                continue;
                            }
                        }
                        LastSection = nullptr;
                    }
                    else if (nPos = strLine.find(L'='), nPos != string::npos && LastSection != nullptr)
                    {
                        const auto strTmp = TrimString(strLine.substr(0, nPos));
                        if (strTmp.empty() == false)
                            LastSection->insert(make_pair(strTmp, TrimString(strLine.substr(nPos + 1))));
                    }
                    else if (strLine[0] == L'@')
                    {
                        LoadFile(TrimString(strLine.substr(1)));
                        LastSection = nullptr;
                    }
                }
            }

            fin.close();

            // We get the file time of the config file we just read, and safe it
            struct _stat stFileInfo;
            if (::_wstat(FN_CA(strFilename), &stFileInfo) == 0)
            {
                m_tFileTime = stFileInfo.st_mtime;
                m_tLastCheck = chrono::steady_clock::now();
            }
        }
    }

    bool AreFilesModifyed()
    {
        if (m_tFileTime == 0)    // We have no files, we return true as if the file is modified
            return true;

        if (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - m_tLastCheck).count() < 1000)
            return false;

        struct _stat stFileInfo;
        if (::_wstat(FN_CA(m_strFileName), &stFileInfo) != 0 || ::fabs(::difftime(stFileInfo.st_mtime, m_tFileTime)) > 0.00001)
        {   // error on getting the file time or the file was modified
            return true;
        }

        m_tLastCheck = chrono::steady_clock::now();

        return false;
    }

private:
    wstring m_strFileName;
    mutex   m_mtxLoad;
    chrono::steady_clock::time_point m_tLastCheck;
    time_t  m_tFileTime;
    unordered_map<wstring, unordered_multimap<wstring, wstring>> m_mSections;
    static map<wstring, ConfFile> s_lstConfFiles;
};

map<wstring, ConfFile> ConfFile::s_lstConfFiles;
