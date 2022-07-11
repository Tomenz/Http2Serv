/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#include <stdio.h>
#include <fcntl.h>
#include <regex>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <Windows.h>
static const std::vector<std::string> vEnvFilter{"COMPUTERNAME=","HOMEDRIVE=","HOMEPATH=","USERNAME=","USERPROFILE="};
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#define _environ environ
#define _close close
#define _write write
#define _read read
static const std::vector<std::string> vEnvFilter{"USER=", "HOME="};
#endif

#include "SpawnProcess.h"

#define READ_FD 0
#define WRITE_FD 1

mutex SpawnProcess::s_mtxIOstreams;

SpawnProcess::SpawnProcess()
{
    m_fdStdOutPipe[0] = -1;
    m_fdStdOutPipe[1] = -1;
    m_fdStdInPipe[0] = -1;
    m_fdStdInPipe[1] = -1;
    m_fdStdErrPipe[0] = -1;
    m_fdStdErrPipe[1] = -1;
#if defined(_WIN32) || defined(_WIN64)
    m_hProcess = INVALID_HANDLE_VALUE;
#endif

    char** aszEnv = _environ;
    while (*aszEnv)
    {
        if (std::find_if(vEnvFilter.begin(), vEnvFilter.end(), [&](auto& strFilter) { return std::string(*aszEnv).find(strFilter) == 0 ? true : false; }) != vEnvFilter.end())
            m_envp.push_back(*aszEnv);
        ++aszEnv;
    }
}


SpawnProcess::~SpawnProcess() noexcept
{
    if (m_fdStdOutPipe[READ_FD] != -1)
        _close(m_fdStdOutPipe[READ_FD]);
    if (m_fdStdInPipe[WRITE_FD] != -1)
        _close(m_fdStdInPipe[WRITE_FD]);
    if (m_fdStdErrPipe[READ_FD] != -1)
        _close(m_fdStdErrPipe[READ_FD]);

#if defined(_WIN32) || defined(_WIN64)
    if (m_hProcess != INVALID_HANDLE_VALUE)
        CloseHandle(m_hProcess);
#endif
}

#if defined(_WIN32) || defined(_WIN64)
int SpawnProcess::Spawn(const wstring& strCmd, const wstring& strWorkingDir/* = wstring()*/)
{
    if (_pipe(m_fdStdOutPipe, 65536, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    DWORD dwMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState(reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdOutPipe[READ_FD])), &dwMode, nullptr, nullptr);
    SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdOutPipe[WRITE_FD])), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    if (_pipe(m_fdStdInPipe, 65536, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdInPipe[READ_FD])), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    if (_pipe(m_fdStdErrPipe, 65536, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    SetNamedPipeHandleState(reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdErrPipe[READ_FD])), &dwMode, nullptr, nullptr);
    SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdErrPipe[WRITE_FD])), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    STARTUPINFOA stInfo = { 0 };
    PROCESS_INFORMATION ProcInfo = { nullptr, nullptr, 0, 0 };

    stInfo.cb = sizeof(STARTUPINFO);
    stInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    stInfo.wShowWindow = 0/*SW_HIDE*/;
    stInfo.hStdInput = reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdInPipe[READ_FD]));
    stInfo.hStdOutput = reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdOutPipe[WRITE_FD]));
    stInfo.hStdError = reinterpret_cast<HANDLE>(_get_osfhandle(m_fdStdErrPipe[WRITE_FD]));

    string strEnvironment;
    for (auto& pstr : m_envp)
        strEnvironment += string(pstr) + '\0';
    for (auto& str : m_vstrEnvironment)
        strEnvironment += str + '\0';
    strEnvironment += '\0';

    if (CreateProcessA(nullptr, &wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strCmd)[0], nullptr, nullptr, TRUE, CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS, &strEnvironment[0], &wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strWorkingDir)[0], &stInfo, &ProcInfo) == TRUE)
    {
        CloseHandle(ProcInfo.hThread);
        m_hProcess = ProcInfo.hProcess;
    }
    else
    {
        static DWORD dwError = GetLastError();
        m_hProcess = INVALID_HANDLE_VALUE;
    }

    _close(m_fdStdInPipe[READ_FD]);
    m_fdStdInPipe[READ_FD] = -1;
    _close(m_fdStdOutPipe[WRITE_FD]);
    m_fdStdOutPipe[WRITE_FD] = -1;
    _close(m_fdStdErrPipe[WRITE_FD]);
    m_fdStdErrPipe[WRITE_FD] = -1;

    if (m_hProcess == INVALID_HANDLE_VALUE)
    {
        _close(m_fdStdOutPipe[READ_FD]);
        m_fdStdOutPipe[READ_FD] = -1;
        _close(m_fdStdInPipe[WRITE_FD]);
        m_fdStdInPipe[WRITE_FD] = -1;
        _close(m_fdStdErrPipe[READ_FD]);
        m_fdStdErrPipe[READ_FD] = -1;
        return 4;
    }
    return 0;
}
#else
int SpawnProcess::Spawn(const wstring& strCmd, const wstring& strWorkingDir/* = wstring()*/)
{
    string l_strCmd = wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strCmd);
    //const static regex rx("([^\\s\\\"]+|(?:\\\"[^\\\"]*\\\")+)+\\s*");
    const static regex rx("([^\\s\\\"]+)(?:\\s|$)|((?:[^\\s]*\\\"[^\\\"]*\\\"[^\\s\\\"]*)+)+(?:\\s|$)");
    vector<string> token;
    smatch mr;
    while (regex_search(l_strCmd, mr, rx) == true && mr[0].matched == true)
    {
        token.push_back(mr[0].str());
        l_strCmd.erase(0, mr[0].length());
        token.back().erase(token.back().find_last_not_of("\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
        token.back().erase(0, token.back().find_first_not_of("\" \t"));      // Trim Whitespace and " character on the left
    }

    unique_ptr<char*[]> wargv(new char*[token.size() + 1]);
    for (size_t n = 0; n < token.size(); ++n)
        wargv.get()[n] = &token[n][0];
    wargv.get()[token.size()] = nullptr;

    for (auto& str : m_vstrEnvironment)
        m_envp.push_back(&str[0]);
    m_envp.push_back(nullptr);

    if (pipe2(m_fdStdOutPipe, O_CLOEXEC) == -1)
        return   1;

    fcntl(m_fdStdOutPipe[READ_FD], F_SETFL, fcntl(m_fdStdOutPipe[READ_FD], F_GETFL, 0) | O_NONBLOCK);

    if (pipe2(m_fdStdInPipe, O_CLOEXEC) == -1)
        return   1;

    if (pipe2(m_fdStdErrPipe, O_CLOEXEC) == -1)
        return   1;

    fcntl(m_fdStdErrPipe[READ_FD], F_SETFL, fcntl(m_fdStdErrPipe[READ_FD], F_GETFL, 0) | O_NONBLOCK);

    lock_guard<mutex> lock(s_mtxIOstreams);

    // Duplicate stdout file descriptor (next line will close original)
    int fdStdOut = dup(fileno(stdout));   // Out as seen by the spawn process
    int fdStdIn = dup(fileno(stdin));     // In as seen by the spawn process
    int fdStdErr = dup(fileno(stderr));     // Err as seen by the spawn process

                                              // Duplicate write end of pipe to stdout file descriptor
    if (dup2(m_fdStdOutPipe[WRITE_FD], fileno(stdout)) < 0)
        return   2;

    if (dup2(m_fdStdInPipe[READ_FD], fileno(stdin)) < 0)
        return   2;

    if (dup2(m_fdStdErrPipe[WRITE_FD], fileno(stderr)) < 0)
        return   2;

    // Close original write end of pipe
    _close(m_fdStdOutPipe[WRITE_FD]);
    _close(m_fdStdInPipe[READ_FD]);
    _close(m_fdStdErrPipe[WRITE_FD]);

    string strCurDir(FILENAME_MAX, 0);
    if (strWorkingDir.empty() == false && getcwd(&strCurDir[0], FILENAME_MAX) == &strCurDir[0])
    {
        strCurDir.resize(strCurDir.find_first_of('\0'));
        chdir(&wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strWorkingDir)[0]);
    }

    int iResult = posix_spawn(&m_hProcess, wargv.get()[0], NULL, NULL, wargv.get(), &m_envp[0]);

    if (strCurDir.size() != FILENAME_MAX)
        chdir(&strCurDir[0]);

    // Duplicate copy of original stdout back into stdout
    if (dup2(fdStdOut, fileno(stdout)) < 0)
        return   3;
    if (dup2(fdStdIn, fileno(stdin)) < 0)
        return   3;
    if (dup2(fdStdErr, fileno(stderr)) < 0)
        return   3;

    // Close duplicate copy of original stdout
    _close(fdStdOut);
    _close(fdStdIn);
    _close(fdStdErr);

    if (iResult != 0 || m_hProcess == -1)
    {
        _close(m_fdStdOutPipe[READ_FD]);
        m_fdStdOutPipe[READ_FD] = -1;
        _close(m_fdStdInPipe[WRITE_FD]);
        m_fdStdInPipe[WRITE_FD] = -1;
        _close(m_fdStdErrPipe[READ_FD]);
        m_fdStdErrPipe[READ_FD] = -1;
        return 4;
    }
    return 0;
}
#endif

int SpawnProcess::KillProcess() noexcept
{
#if defined(_WIN32) || defined(_WIN64)
    return TerminateProcess(m_hProcess, 0) != 0 ? 0 : GetLastError();
#else
    int iRet = kill(m_hProcess, SIGTERM);
    sleep(2);   // The process has 2 sec. to shut down until we kill him hard
    return iRet | kill(m_hProcess, SIGKILL);
#endif
}

bool SpawnProcess::StillSpawning() noexcept
{
    if (m_hProcess)
    {
#if defined(_WIN32) || defined(_WIN64)
        DWORD nExitCode = STILL_ACTIVE;

        if (!GetExitCodeProcess(m_hProcess, &nExitCode))
            return false;
        return nExitCode == STILL_ACTIVE ? true : false;
#else
        int nExitCode = 0;
        int status = waitpid(m_hProcess, &nExitCode, WNOHANG);
        if (status < 0)
            return false;
        else if (status == 0)
            return true;
        return WIFEXITED(nExitCode) || WIFSIGNALED(nExitCode) ? false : true;
#endif
    }
    return false;
}

size_t SpawnProcess::ReadFromSpawn(unsigned char* const pBuffer, const uint32_t nBufSize) noexcept
{
    const int iRead = _read(m_fdStdOutPipe[READ_FD], pBuffer, nBufSize);
    if (iRead >= 0)
        return iRead;
    return 0;
}

size_t SpawnProcess::ReadErrFromSpawn(unsigned char* const pBuffer, const uint32_t nBufSize) noexcept
{
    const int iRead = _read(m_fdStdErrPipe[READ_FD], pBuffer, nBufSize);
    if (iRead >= 0)
        return iRead;
    return 0;
}

uint32_t SpawnProcess::WriteToSpawn(const unsigned char* const pBuffer, const uint32_t nBufSize) noexcept
{
    const int iWrite = _write(m_fdStdInPipe[WRITE_FD], pBuffer, nBufSize);
    if (iWrite >= 0)
        return static_cast<uint32_t>(iWrite);
    return 0;
}

void SpawnProcess::CloseWritePipe() noexcept
{
    _close(m_fdStdInPipe[WRITE_FD]);
    m_fdStdInPipe[WRITE_FD] = -1;
}
