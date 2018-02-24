#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <codecvt>
#include <locale>

using namespace std;

class SpawnProcess
{
public:
    SpawnProcess() noexcept;
    virtual ~SpawnProcess() noexcept;

    int Spawn(const wstring& strCmd, const wstring& strWorkingDir = wstring()) noexcept;
    bool StillSpawning() noexcept;
    int ReadFromSpawn(unsigned char* const pBuffer, const unsigned int nBufSize) noexcept;
    int ReadErrFromSpawn(unsigned char* const pBuffer, const unsigned int nBufSize) noexcept;
    int WriteToSpawn(unsigned char* const pBuffer, const unsigned int nBufSize) noexcept;
    void CloseWritePipe() noexcept;
    void AddEnvironment(const wstring& strEnvironment) noexcept { m_vstrEnvironment.push_back(wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strEnvironment)); }
    void AddEnvironment(const string& strEnvironment) noexcept { m_vstrEnvironment.push_back(strEnvironment); }

    static mutex s_mtxIOstreams;

private:
    int m_fdStdOutPipe[2];
    int m_fdStdInPipe[2];
    int m_fdStdErrPipe[2];
#if defined(_WIN32) || defined(_WIN64)
    intptr_t m_hProcess;
#else
    pid_t m_hProcess;
#endif
    vector<char*> m_envp;
    vector<string> m_vstrEnvironment;
    string m_strCmd;
};

