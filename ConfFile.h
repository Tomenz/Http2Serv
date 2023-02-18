/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#ifndef CONFFILE_H
#define CONFFILE_H

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
    static ConfFile& GetInstance(const wstring& strConfigFile);
    ConfFile(const ConfFile& src);
    virtual ~ConfFile() = default;
    vector<wstring> get();
    vector<wstring> get(const wstring& strSektion);
    vector<wstring> get(const wstring& strSektion, const wstring& strValue);
    const wstring& getUnique(const wstring& strSektion, const wstring& strValue);

private:
    ConfFile() = delete;
    ConfFile(ConfFile&&) = delete;
    explicit ConfFile(const wstring& strConfigFile) : m_strFileName(strConfigFile), m_tFileTime(0) {}
    ConfFile& operator=(ConfFile&&) = delete;
    ConfFile& operator=(const ConfFile&) = delete;

    void CheckFileLoaded();
    void LoadFile(const wstring& strFilename);
    bool AreFilesModified() const noexcept;

private:
    wstring m_strFileName;
    mutable mutex m_mtxLoad;
    mutable chrono::steady_clock::time_point m_tLastCheck;
    time_t  m_tFileTime;
    unordered_map<wstring, unordered_multimap<wstring, wstring>> m_mSections;
    static map<wstring, ConfFile> s_lstConfFiles;
};

#endif // !CONFFILE_H
