
#include <stdio.h>
#include <fcntl.h>
#include <regex>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <process.h>
#include <Stdlib.h>
#include <Windows.h>
#define string_t wstring
#define FN_STR(x) x
#define NULLCHR L'\0'
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
#define _wgetcwd getcwd
#define _wchdir chdir
#define string_t string
#define FN_STR(x) wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(x)
#define NULLCHR '\0'
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
    m_strCmd += wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strCmd);
    const static regex rx("([^\\s\\\"]+|(?:\\\"[^\\\"]*\\\")+)+\\s*");
    vector<string> token;
    smatch mr;
    while (regex_search(m_strCmd, mr, rx) == true && mr[0].matched == true)
    {
        token.push_back(mr[1].str());
        m_strCmd.erase(0, mr[0].length());
        token.back().erase(token.back().find_last_not_of("\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
        token.back().erase(0, token.back().find_first_not_of("\" \t"));      // Trim Whitespace and " character on the left
    }
    unique_ptr<char*[]> wargv(new char*[token.size() + 1]);
    for (size_t n = 0; n < token.size(); ++n)
        wargv.get()[n] = &token[n][0];
    wargv.get()[token.size()] = nullptr;

    for (auto& str : m_vstrEnvironment)
        m_envp.push_back(&str[0]);  //m_envp.push_back("SCRIPT_FILENAME=C:/Users/hauck/Documents/My Web Sites/WebSite1/index.php");
    m_envp.push_back(nullptr);

    // Create the pipe
#if defined(_WIN32) || defined(_WIN64)
    if (_pipe(m_fdStdOutPipe, 65536, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    DWORD dwMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState((HANDLE)_get_osfhandle(m_fdStdOutPipe[READ_FD]), &dwMode, 0, 0);

    if (_pipe(m_fdStdInPipe, 65536, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    if (_pipe(m_fdStdErrPipe, 65536, O_NOINHERIT | O_BINARY) == -1)
        return   1;

    SetNamedPipeHandleState((HANDLE)_get_osfhandle(m_fdStdErrPipe[READ_FD]), &dwMode, 0, 0);
#else
    if (pipe2(m_fdStdOutPipe, O_CLOEXEC) == -1)
        return   1;

    fcntl(m_fdStdOutPipe[READ_FD], F_SETFL, fcntl(m_fdStdOutPipe[READ_FD], F_GETFL, 0) | O_NONBLOCK);

    if (pipe2(m_fdStdInPipe, O_CLOEXEC) == -1)
        return   1;

    if (pipe2(m_fdStdErrPipe, O_CLOEXEC) == -1)
        return   1;

    fcntl(m_fdStdErrPipe[READ_FD], F_SETFL, fcntl(m_fdStdErrPipe[READ_FD], F_GETFL, 0) | O_NONBLOCK);
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

    string_t strCurDir(FILENAME_MAX, 0);
    if (strWorkingDir.empty() == false && _wgetcwd(&strCurDir[0], FILENAME_MAX) == &strCurDir[0])
    {
        strCurDir.resize(strCurDir.find_first_of(NULLCHR));
        _wchdir(&FN_STR(strWorkingDir)[0]);
    }

    // Spawn process
#if defined(_WIN32) || defined(_WIN64)
    m_hProcess = _spawnvpe(P_NOWAIT, wargv.get()[0], wargv.get(), &m_envp[0]);
#else
    posix_spawn(&m_hProcess, wargv.get()[0], NULL, NULL, wargv.get(), &m_envp[0]);
#endif

    if (strCurDir.size() != FILENAME_MAX)
        _wchdir(&strCurDir[0]);

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
