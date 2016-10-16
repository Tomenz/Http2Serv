// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#else
#include <syslog.h>
#include <signal.h>
#pragma message("TODO!!! Folge Zeile wieder entfernen.")
#include <termios.h>
#include <fcntl.h>
#endif

#include "ConfFile.h"
#include "HttpServ.h"

#ifndef _UTFCONVERTER
#define _UTFCONVERTER
std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> Utf8Converter;
#endif

int main(int argc, const char* argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));

    _setmode(_fileno(stdout), _O_U16TEXT);
#endif

    bool bRunAsPrg = false;
    if (argc > 1 && strcmp(argv[1], "-f") == 0)
        bRunAsPrg = true;

    if (bRunAsPrg == false)
    {
#if defined(_WIN32) || defined(_WIN64)
#else
        //Set our Logging Mask and open the Log
        setlogmask(LOG_UPTO(LOG_NOTICE));
        openlog("http2serv", LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);

        syslog(LOG_NOTICE, "Starting Http2Serv");
        pid_t pid, sid;
        //Fork the Parent Process
        pid = fork();

        if (pid < 0)
            exit(EXIT_FAILURE);

        //We got a good pid, Close the Parent Process
        if (pid > 0)
            exit(EXIT_SUCCESS);

        //Create a new Signature Id for our child
        sid = setsid();
        if (sid < 0)
            exit(EXIT_FAILURE);

        //Fork second time the Process
        pid = fork();

        if (pid < 0)
            exit(EXIT_FAILURE);

        //We got a good pid, Close the Parent Process
        if (pid > 0)
            exit(EXIT_SUCCESS);

        //Change File Mask
        umask(0);

        stringstream ss;
        pid = getpid();
        string ModulPath(PATH_MAX, 0);
        ss << "/proc/" << pid << "/exe";
        if (readlink(ss.str().c_str(), &ModulPath[0], PATH_MAX) > 0)
            ModulPath.erase(ModulPath.find_last_of('/'));

        //Change Directory
        //If we cant find the directory we exit with failure.
        if ((chdir(ModulPath.c_str()/*"/"*/)) < 0)
            exit(EXIT_FAILURE);

        //Close Standard File Descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
#endif
    }
    else
        wcout << L"Http2Serv gestartet" << endl;

    //locale::global(std::locale(""));

    const ConfFile& conf = ConfFile::GetInstance(L"server.cfg");
#ifdef _DEBUG
    vector<wstring>&& vSec = conf.get();
    for (const auto& item : vSec)
    {
        wcout << L"[" << item << L"]" << endl;
        vector<wstring>&& vKeys = conf.get(item);
        for (const auto& key : vKeys)
        {
            vector<wstring>&& vValues = conf.get(item, key);
            for (const auto& value : vValues)
                wcout << key << L" = " << value << endl;
        }
    }
#endif

    deque<CHttpServ> vServers;

    const pair<wstring, int> strKeyWordUniqueItems[] = { {L"DefaultItem", 1}, {L"RootDir", 2}, {L"LogFile", 3}, {L"ErrorLog",4}, {L"SSL_DH_ParaFile",5}, {L"KeyFile",6}, {L"CertFile",7}, {L"CaBundle",8}, {L"SSL", 9} };
    const pair<wstring, int> strKeyWordMultiItems[] = { {L"RewriteRule",1}, {L"AliasMatch",2 }, {L"ForceType",3 }, {L"FileTyps",4 }, {L"SetEnvIf",5 }, {L"RedirectMatch",6} };

    vector<wstring>&& vFileTypExt = conf.get(L"FileTyps");

    vector<wstring>&& vListen = conf.get(L"Listen");
    for (const auto& strListen : vListen)
    {
        vector<wstring>&& vPort = conf.get(L"Listen", strListen);
        for (const auto& strPort : vPort)
        {
            // Default Werte setzen
            vServers.emplace_back(L"./html", stoi(strPort), false);
            vServers.back().SetBindAdresse(string(begin(strListen), end(strListen)).c_str());

            // Default und Common Parameter of the listening socket
            function<void(const wstring&, const wchar_t*)> fnSetParameter = [&](const wstring& strSection, const wchar_t* szHost = nullptr)
            {
                static wregex seperator(L"\\s+");

                CHttpServ::HOSTPARAM& HostParam = vServers.back().GetParameterBlockRef(szHost);
                for (const auto& strKey : strKeyWordUniqueItems)
                {
                    wstring strValue = conf.getUnique(strSection, strKey.first);
                    if (strValue.empty() == false)
                    {
                        switch (strKey.second)
                        {
                        case 1:
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                while (token != wsregex_token_iterator())
                                    HostParam.m_vstrDefaultItem.push_back(token++->str());
                            }
                            break;
                        case 2: HostParam.m_strRootPath = strValue; break;
                        case 3: HostParam.m_strLogFile = strValue; break;
                        case 4: HostParam.m_strErrLog = strValue; break;
                        case 5: HostParam.m_strDhParam = Utf8Converter.to_bytes(strValue); break;
                        case 6: HostParam.m_strHostKey = Utf8Converter.to_bytes(strValue); break;
                        case 7: HostParam.m_strHostCertificate = Utf8Converter.to_bytes(strValue); break;
                        case 8: HostParam.m_strCAcertificate = Utf8Converter.to_bytes(strValue); break;
                        case 9: transform(begin(strValue), end(strValue), begin(strValue), ::toupper);
                                HostParam.m_bSSL = strValue == L"TRUE" ? true : false; break;
                        }
                    }
                }

                for (const auto& strKey : strKeyWordMultiItems)
                {
                    vector<wstring>&& vValues = conf.get(strSection, strKey.first);
                    if (vValues.empty() == false)
                    {
                        switch (strKey.second)
                        {
                        case 1: // RewriteRule
                            for (auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mstrRewriteRule.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                            }
                        case 2: // AliasMatch
                            for (auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mstrAliasMatch.emplace(token->str(), next(token)->str());//HostParam.m_mstrAliasMatch.emplace(token->str(), strValue.substr(token->str().size() + 1));
                            }
                        case 3: // ForceType
                            for (auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mstrForceTyp.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                            }
                            break;
                        case 4: // FileTyps
                            for (auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mFileTypeAction.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                            }
                            break;
                        case 5: // SetEnvIf
                            for (auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                vector<wstring> vecTmp;
                                while (token != wsregex_token_iterator())
                                    vecTmp.push_back(token++->str());
                                while (vecTmp.size() > 3)
                                {
                                    vecTmp[1] += vecTmp[2];
                                    vecTmp.erase(begin(vecTmp) + 2);
                                }
                                if (vecTmp.size() == 3)
                                {
                                    transform(begin(vecTmp[0]), end(vecTmp[0]), begin(vecTmp[0]), ::toupper);
                                    transform(begin(vecTmp[2]), end(vecTmp[2]), begin(vecTmp[2]), ::toupper);
                                    vecTmp[1].erase(vecTmp[1].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                    vecTmp[1].erase(0, vecTmp[1].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                    HostParam.m_vEnvIf.emplace_back(make_tuple(vecTmp[0], vecTmp[1], vecTmp[2]));
                                }
                            }
                            break;
                        case 6: // RedirectMatch
                            for (auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                vector<wstring> vecTmp;
                                while (token != wsregex_token_iterator())
                                    vecTmp.push_back(token++->str());
                                if (vecTmp.size() == 3)
                                    HostParam.m_vRedirMatch.emplace_back(make_tuple(vecTmp[0], vecTmp[1], vecTmp[2]));
                            }
                            break;
                        }
                    }
                }
            };
            fnSetParameter(L"common", nullptr);

            ///////////////////////////////////////////

            // Host Parameter holen und setzen
            function<void(wstring, bool)> fuSetHostParam = [&](wstring strListenAddr, bool IsVHost)
            {
                fnSetParameter(strListenAddr + L":" + strPort, IsVHost == true ? strListenAddr.c_str() : nullptr);

                const wstring strValue = conf.getUnique(strListenAddr + L":" + strPort, L"VirtualHost");
                if (strValue.empty() == false)
                {
                    size_t nPos = strValue.find_first_of(L','), nStart = 0;
                    while (nPos != string::npos)
                    {
                        fuSetHostParam(strValue.substr(nStart, nPos++), true);
                        nStart += nPos;
                        nPos = strValue.find_first_of(L',', nStart);
                    }
                    fuSetHostParam(strValue.substr(nStart), true);
                }
            };

            fuSetHostParam(strListen, false);
        }
    }

    // Server starten
    for (auto& HttpServer : vServers)
        HttpServer.Start();

#if defined(_WIN32) || defined(_WIN64)
    //_getch();
    const wchar_t caZeichen[] = L"\\|/-";
    int iIndex = 0;
    while (_kbhit() == 0)
    {
        size_t nHttpCon = 0;
        for (auto& HttpServer : vServers)
            nHttpCon += HttpServer.m_vConnections.size();

        wcout << L'\r' << caZeichen[iIndex++] << L"  Sockets:" << setw(3) << BaseSocket::s_atRefCount << L"  SSL-Pumpen:" << setw(3) << SslTcpSocket::s_atAnzahlPumps << L"  HTTP-Connections:" << setw(3) << nHttpCon << flush;// setw(6) << nCounter3 << setw(6) << nCounter2 << setw(6) << nCounter1 << setw(6) << nCounter4 << flush;

        if (iIndex > 3) iIndex = 0;
        this_thread::sleep_for(chrono::milliseconds(100));
    }
#else
    if (bRunAsPrg == false)
    {
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGTERM);
        sigprocmask(SIG_BLOCK, &sigset, NULL);

        int sig;
        sigwait(&sigset, &sig);
    }
    else
    {
        //getchar();
        auto _kbhit = [&]() -> int
        {
            struct termios oldt, newt;
            int ch;
            int oldf;

            tcgetattr(STDIN_FILENO, &oldt);
            newt = oldt;
            newt.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);
            oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

            ch = getchar();

            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            fcntl(STDIN_FILENO, F_SETFL, oldf);

            if (ch != EOF)
            {
                ungetc(ch, stdin);
                return 1;
            }

            return 0;
        };

        const wchar_t caZeichen[] = L"\\|/-";
        int iIndex = 0;
        while (_kbhit() == 0)
        {
            size_t nHttpCon = 0;
            for (auto& HttpServer : vServers)
                nHttpCon += HttpServer.m_vConnections.size();

            wcout << L'\r' << caZeichen[iIndex++] << L"  Sockets:" << setw(3) << BaseSocket::s_atRefCount << L"  SSL-Pumpen:" << setw(3) << SslTcpSocket::s_atAnzahlPumps << L"  HTTP-Connections:" << setw(3) << nHttpCon << flush;
            if (iIndex > 3) iIndex = 0;
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
#endif

    for (auto& HttpServer : vServers)
        HttpServer.Stop();

    if (bRunAsPrg == false)
    {
#if defined(_WIN32) || defined(_WIN64)
#else
        syslog(LOG_NOTICE, "Http2Serv beendet.");

        //Close the log
        closelog();
#endif
    }

    return 0;
}

