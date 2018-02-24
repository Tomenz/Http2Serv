
#include <stdio.h>
#include <fcntl.h>
#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <process.h>
#include <Stdlib.h>
#include <Windows.h>
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#define _environ environ
#define _close close
#define _dup dup
#define _dup2 dup2
#define _fileno fileno
#define _write write
#define _read read
#endif

#include "SpawnProcess.h"

#define READ_FD 0
#define WRITE_FD 1

mutex SpawnProcess::s_mtxIOstreams;

SpawnProcess::SpawnProcess() noexcept
{
    m_fdStdOutPipe[0] = -1;
    m_fdStdOutPipe[1] = -1;
    m_fdStdInPipe[0] = -1;
    m_fdStdInPipe[1] = -1;
    m_fdStdErrPipe[0] = -1;
    m_fdStdErrPipe[1] = -1;

    char** aszEnv = _environ;
    while (*aszEnv)
    {
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
}

int SpawnProcess::Spawn(const wstring& strCmd, const wstring& strWorkingDir/* = wstring()*/) noexcept
{
    if (strWorkingDir.empty() == false)
    {
        if (strWorkingDir[1] == L':')
            m_strCmd = wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strWorkingDir.substr(0, 2)) + " && ";
        m_strCmd += "cd \"" + wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strWorkingDir) + "\" && ";
    }
    m_strCmd += wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strCmd);
    for (auto& str : m_vstrEnvironment)
        m_envp.push_back(&str[0]);  //m_envp.push_back("SCRIPT_FILENAME=C:/Users/hauck/Documents/My Web Sites/WebSite1/index.php");
    m_envp.push_back(nullptr);

    // Create the pipe
#if defined(_WIN32) || defined(_WIN64)
    if (_pipe(m_fdStdOutPipe, 512, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    DWORD dwMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState((HANDLE)_get_osfhandle(m_fdStdOutPipe[READ_FD]), &dwMode, 0, 0);

    if (_pipe(m_fdStdInPipe, 512, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    if (_pipe(m_fdStdErrPipe, 512, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    SetNamedPipeHandleState((HANDLE)_get_osfhandle(m_fdStdErrPipe[READ_FD]), &dwMode, 0, 0);
#else
    if (pipe2(m_fdStdOutPipe, O_CLOEXEC | O_NONBLOCK) == -1)
        return   1;

    if (pipe2(m_fdStdInPipe, O_CLOEXEC) == -1)
        return   1;

    if (pipe2(m_fdStdErrPipe, O_CLOEXEC | O_NONBLOCK) == -1)
        return   1;
#endif

    lock_guard<mutex> lock(s_mtxIOstreams);

    // Duplicate stdout file descriptor (next line will close original)
    int fdStdOut = _dup(_fileno(stdout));   // Out as seen by the spawn process
    int fdStdIn = _dup(_fileno(stdin));     // In as seen by the spawn process
    int fdStdErr = _dup(_fileno(stderr));     // Err as seen by the spawn process

    // Duplicate write end of pipe to stdout file descriptor
    if (_dup2(m_fdStdOutPipe[WRITE_FD], _fileno(stdout)) < 0)
        return   2;

    if (_dup2(m_fdStdInPipe[READ_FD], _fileno(stdin)) < 0)
        return   2;

    if (_dup2(m_fdStdErrPipe[WRITE_FD], _fileno(stderr)) < 0)
        return   2;

    // Close original write end of pipe
    _close(m_fdStdOutPipe[WRITE_FD]);
    _close(m_fdStdInPipe[READ_FD]);
    _close(m_fdStdErrPipe[WRITE_FD]);

    // Spawn process
#if defined(_WIN32) || defined(_WIN64)
    const char *const argv[] = { "cmd.exe", "/C", &m_strCmd[0] , nullptr };
    m_hProcess = _spawnvpe(P_NOWAIT, "cmd.exe", argv, &m_envp[0]);
#else
    char *const argv[] = { "/bin/bash", "-c" , &m_strCmd[0] , nullptr };
    posix_spawn(&m_hProcess, "/bin/bash", NULL, NULL, argv, &m_envp[0]);
#endif

    // Duplicate copy of original stdout back into stdout
    if (_dup2(fdStdOut, _fileno(stdout)) < 0)
        return   3;
    if (_dup2(fdStdIn, _fileno(stdin)) < 0)
        return   3;
    if (_dup2(fdStdErr, _fileno(stderr)) < 0)
        return   3;

    // Close duplicate copy of original stdout
    _close(fdStdOut);
    _close(fdStdIn);
    _close(fdStdErr);

    if (m_hProcess == -1)
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

bool SpawnProcess::StillSpawning() noexcept
{
    int nExitCode;

    if (m_hProcess)
    {
#if defined(_WIN32) || defined(_WIN64)
        nExitCode = STILL_ACTIVE;

        if (!GetExitCodeProcess(reinterpret_cast<HANDLE>(m_hProcess), (unsigned long*)&nExitCode))
            return false;
        return nExitCode == STILL_ACTIVE ? true : false;
#else
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

int SpawnProcess::ReadFromSpawn(unsigned char* const pBuffer, const unsigned int nBufSize) noexcept
{
    return _read(m_fdStdOutPipe[READ_FD], pBuffer, nBufSize);
}

int SpawnProcess::ReadErrFromSpawn(unsigned char* const pBuffer, const unsigned int nBufSize) noexcept
{
    return _read(m_fdStdErrPipe[READ_FD], pBuffer, nBufSize);
}

int SpawnProcess::WriteToSpawn(unsigned char* const pBuffer, const unsigned int nBufSize) noexcept
{
    return _write(m_fdStdInPipe[WRITE_FD], pBuffer, nBufSize);
}

void SpawnProcess::CloseWritePipe() noexcept
{
    _close(m_fdStdInPipe[WRITE_FD]);
    m_fdStdInPipe[WRITE_FD] = -1;
}
