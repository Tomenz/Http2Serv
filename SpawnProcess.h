/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#ifndef SPAWNPROCESS_H
#define SPAWNPROCESS_H

#include <vector>
#include <string>
#include <mutex>
#include <codecvt>
#include <locale>

using namespace std;

class SpawnProcess
{
public:
    SpawnProcess();
    virtual ~SpawnProcess() noexcept;
    SpawnProcess(const SpawnProcess&) = delete;
    SpawnProcess(SpawnProcess&&) = delete;
    SpawnProcess& operator=(const SpawnProcess&) = delete;
    SpawnProcess& operator=(SpawnProcess&&) = delete;

    int Spawn(const wstring& strCmd, const wstring& strWorkingDir = wstring());
    int KillProcess() noexcept;
    bool StillSpawning() noexcept;
    size_t ReadFromSpawn(unsigned char* const pBuffer, const uint32_t nBufSize) noexcept;
    size_t ReadErrFromSpawn(unsigned char* const pBuffer, const uint32_t nBufSize) noexcept;
    uint32_t WriteToSpawn(const unsigned char* const pBuffer, const uint32_t nBufSize) noexcept;
    void CloseWritePipe() noexcept;
    void AddEnvironment(const wstring& strEnvironment) noexcept { m_vstrEnvironment.push_back(wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strEnvironment)); }
    void AddEnvironment(const string& strEnvironment) noexcept { m_vstrEnvironment.push_back(strEnvironment); }
    void AddEnvironment(const string& strEnvVar, const string& strEnvValue) noexcept { m_vstrEnvironment.push_back(strEnvVar + "=" + strEnvValue); }

    static mutex s_mtxIOstreams;

private:
    int m_fdStdOutPipe[2];
    int m_fdStdInPipe[2];
    int m_fdStdErrPipe[2];
#if defined(_WIN32) || defined(_WIN64)
    HANDLE m_hProcess;
#else
    pid_t m_hProcess;
#endif
    vector<char*> m_envp;
    vector<string> m_vstrEnvironment;
};

#endif // !SPAWNPROCESS_H
