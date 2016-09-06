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
#include <string>
#include <vector>
#include <mutex>

#include "Trace.h"

using namespace std;

class ConfFile
{
public:
    static const ConfFile& GetInstance(const wstring& strConfigFile);
    ConfFile(const ConfFile& src);
    virtual ~ConfFile();
    vector<wstring> get() const;
    vector<wstring> get(const wstring& strSektion) const;
    vector<wstring> get(const wstring& strSektion, const wstring& strValue) const;
    const wstring& getUnique(const wstring& strSektion, const wstring& strValue) const;

private:
    ConfFile() = delete;
    explicit ConfFile(const wstring& strConfigFile) : m_strFileName(strConfigFile), m_tFileTime(0) {}
    ConfFile& operator=(ConfFile&&) = delete;
    ConfFile& operator=(const ConfFile&) = delete;

    void CheckFileLoaded() const;
    void LoadFile(const wstring& strFilename);
    bool AreFilesModifyed() const;

private:
    wstring m_strFileName;
    mutex   m_mtxLoad;
    chrono::steady_clock::time_point m_tLastCheck;
    time_t  m_tFileTime;
    unordered_map<wstring, unordered_multimap<wstring, wstring>> m_mSections;
    static map<wstring, ConfFile> s_lstConfFiles;
};
