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

#if defined(_WIN32) || defined(_WIN64)
#include "SvrLib/BaseSvr.h"
#include "SvrLib/svrctrl.h"

void se_translator(size_t e, _EXCEPTION_POINTERS* p)
{
    throw e;
}
#else
class CBaseSrv
{
public:
    CBaseSrv(wchar_t*) {}
    virtual int Run(void) { Start(); return 0; }
    virtual void Start(void) = 0;
};
#endif

class Service : public CBaseSrv
{
public:
    Service(wchar_t* szSrvName):CBaseSrv(szSrvName), m_bStop(false), m_bIsStopped(true) { }
    virtual void Start(void)
    {
        // Set the Exception Handler-function
        //_set_se_translator(se_translator);

        m_bIsStopped = false;

        wstring strModulePath(FILENAME_MAX, 0);
#ifndef _DEBUG
#if defined(_WIN32) || defined(_WIN64)
        if (GetModuleFileName(NULL, &strModulePath[0], MAX_PATH) > 0)
            strModulePath.erase(strModulePath.find_last_of(L'\\') + 1); // Sollte der Backslash nicht gefunden werden wird der ganz String gelöscht
#endif
#else
        strModulePath = L"./";
#endif
        const ConfFile& conf = ConfFile::GetInstance(strModulePath + L"server.cfg");

        deque<CHttpServ> vServers;

        const pair<wstring, int> strKeyWordUniqueItems[] = { { L"DefaultItem", 1 },{ L"RootDir", 2 },{ L"LogFile", 3 },{ L"ErrorLog",4 },{ L"SSL_DH_ParaFile",5 },{ L"KeyFile",6 },{ L"CertFile",7 },{ L"CaBundle",8 },{ L"SSL", 9 } };
        const pair<wstring, int> strKeyWordMultiItems[] = { { L"RewriteRule",1 },{ L"AliasMatch",2 },{ L"ForceType",3 },{ L"FileTyps",4 },{ L"SetEnvIf",5 },{ L"RedirectMatch",6 },{ L"DeflateTyps",7 } };

        vector<wstring>&& vFileTypExt = conf.get(L"FileTyps");

        vector<wstring>&& vListen = conf.get(L"Listen");
        for (const auto& strListen : vListen)
        {
            vector<wstring>&& vPort = conf.get(L"Listen", strListen);
            for (const auto& strPort : vPort)
            {
                // Default Werte setzen
                vServers.emplace_back(strModulePath + L"html", stoi(strPort), false);
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
                                for (const auto& strValue : vValues)
                                {
                                    wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                    if (token != wsregex_token_iterator())
                                        HostParam.m_mstrRewriteRule.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                                }
                            case 2: // AliasMatch
                                for (const auto& strValue : vValues)
                                {
                                    wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                    if (token != wsregex_token_iterator())
                                        HostParam.m_mstrAliasMatch.emplace(token->str(), next(token)->str());//HostParam.m_mstrAliasMatch.emplace(token->str(), strValue.substr(token->str().size() + 1));
                                }
                            case 3: // ForceType
                                for (const auto& strValue : vValues)
                                {
                                    wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                    if (token != wsregex_token_iterator())
                                        HostParam.m_mstrForceTyp.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                                }
                                break;
                            case 4: // FileTyps
                                for (const auto& strValue : vValues)
                                {
                                    wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                    if (token != wsregex_token_iterator())
                                        HostParam.m_mFileTypeAction.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                                }
                                break;
                            case 5: // SetEnvIf
                                for (const auto& strValue : vValues)
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
                                for (const auto& strValue : vValues)
                                {
                                    wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                    vector<wstring> vecTmp;
                                    while (token != wsregex_token_iterator())
                                        vecTmp.push_back(token++->str());
                                    if (vecTmp.size() == 3)
                                        HostParam.m_vRedirMatch.emplace_back(make_tuple(vecTmp[0], vecTmp[1], vecTmp[2]));
                                }
                                break;
                            case 7: // DeflateTyps
                                for (const auto& strValue : vValues)
                                {
                                    wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                    while (token != wsregex_token_iterator())
                                        HostParam.m_vDeflateTyps.push_back(Utf8Converter.to_bytes(token++->str()));
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

        while (m_bStop == false)
            this_thread::sleep_for(chrono::milliseconds(100));

        // Server stoppen
        for (auto& HttpServer : vServers)
            HttpServer.Stop();

        // Warten bis alle Verbindungen / Ressourcen geschlossen sind
        for (auto& HttpServer : vServers)
        {
            while (HttpServer.IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(10));
        }

        m_bIsStopped = true;
    };
    virtual void Stop(void) { m_bStop = true; }
    bool IsStopped(void) { return m_bIsStopped; }

private:
    bool m_bStop;
    bool m_bIsStopped;
};
/*
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
        Service svr(L"http2serv");
        svr.Run();
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
        if ((chdir(ModulPath.c_str())) < 0) // if ((chdir("/")) < 0)
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
    const pair<wstring, int> strKeyWordMultiItems[] = { {L"RewriteRule",1}, {L"AliasMatch",2 }, {L"ForceType",3 }, {L"FileTyps",4 }, {L"SetEnvIf",5 }, {L"RedirectMatch",6}, {L"DeflateTyps",7} };

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
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mstrRewriteRule.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                            }
                        case 2: // AliasMatch
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mstrAliasMatch.emplace(token->str(), next(token)->str());//HostParam.m_mstrAliasMatch.emplace(token->str(), strValue.substr(token->str().size() + 1));
                            }
                        case 3: // ForceType
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mstrForceTyp.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                            }
                            break;
                        case 4: // FileTyps
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                if (token != wsregex_token_iterator())
                                    HostParam.m_mFileTypeAction.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                            }
                            break;
                        case 5: // SetEnvIf
                            for (const auto& strValue : vValues)
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
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                vector<wstring> vecTmp;
                                while (token != wsregex_token_iterator())
                                    vecTmp.push_back(token++->str());
                                if (vecTmp.size() == 3)
                                    HostParam.m_vRedirMatch.emplace_back(make_tuple(vecTmp[0], vecTmp[1], vecTmp[2]));
                            }
                            break;
                        case 7: // DeflateTyps
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), seperator, -1);
                                while (token != wsregex_token_iterator())
                                    HostParam.m_vDeflateTyps.push_back(Utf8Converter.to_bytes(token++->str()));
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
*/

Service* pServ = 0;

#if defined(_WIN32) || defined(_WIN64)
BOOL CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
        // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
        wcout << L"Ctrl-C event\n\n";
        if (pServ != 0)
        {
            pServ->Stop();
            while (pServ->IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(100));
        }
        return TRUE;

        // CTRL-CLOSE: confirm that the user wants to exit.
    case CTRL_CLOSE_EVENT:
        wcout << L"Ctrl-Close event\r\n";
        if (pServ != 0)
        {
            pServ->Stop();
            while (pServ->IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(100));
        }
        return TRUE;

        // Pass other signals to the next handler.
    case CTRL_BREAK_EVENT:
        wcout << L"Ctrl-Break event\r\n";
        return FALSE;

    case CTRL_LOGOFF_EVENT:
        wcout << L"Ctrl-Logoff event\r\n";
        return FALSE;

    case CTRL_SHUTDOWN_EVENT:
        wcout << L"Ctrl-Shutdown event\r\n";
        return FALSE;

    default:
        return FALSE;
    }
}
#endif

int main(int argc, const char* argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));
    _setmode(_fileno(stdout), _O_U16TEXT);

    wchar_t szDspName[] = { L"HTTP/2 Server" };
    wchar_t szDescrip[] = { L"Http 2.0 Server by Thomas Hauck" };
#endif

    wchar_t szSvrName[] = { L"Http2Serv" };
    int iRet = 0;

    if (argc > 1)
    {
        while (++argv, --argc)
        {
            if (argv[0][0] == '-')
            {
//                CSvrCtrl cSC;

                switch ((argv[0][1] & 0xdf))
                {
#if defined(_WIN32) || defined(_WIN64)
                case 'I':
                    iRet = cSC.Install(szSvrName, szDspName);
                    cSC.SetServiceDescription(szSvrName, szDescrip);
                    break;
                case 'R':
                    iRet = cSC.Remove(szSvrName);
                    break;
                case 'S':
                    iRet = cSC.Start(szSvrName);
                    break;
                case 'E':
                    iRet = cSC.Stop(szSvrName);
                    break;
                case 'P':
                    iRet = cSC.Pause(szSvrName);
                    break;
                case 'C':
                    iRet = cSC.Continue(szSvrName);
                    break;
                case 'F':
                    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
#else
                case 'F':
#endif
                    {
                        Service svr(szSvrName);
                        pServ = &svr;
                        wcout << L"Http2Serv gestartet" << endl;
                        svr.Start();
                    }
                    break;
                case 'H':
                    wcout << L"\r\n";
                    wcout << L"-i   Istalliert den Systemdienst\r\n";
                    wcout << L"-r   Entfernt den Systemdienst\r\n";
                    wcout << L"-s   Startet den Systemdienst\r\n";
                    wcout << L"-e   Beendet den Systemdienst\r\n";
                    wcout << L"-p   Systemdienst wird angehaltet (Pause)\r\n";
                    wcout << L"-c   Systemdienst wird fortgesetzt (Continue)\r\n";
                    wcout << L"-f   Start die Anwendung als Konsolenanwendung\r\n";
                    //wcout << L"-f x Start die Anwendung als Konsolenanwendung mit Verbosmeldungen(Siehe unten)\r\n";
                    //wcout << L"-v x Setzt den Level der Verbosemeldungen(Siehe unten)\r\n";
                    //wcout << L"\r\n";
                    //wcout << L"Verbosezahl: Bitcodierung jedes Bit hat eine andere Bedeutung\r\n";
                    //wcout << L"             Die Zahlen können addiert werden. Beispiel: 2 + 8 = 10\r\n";
                    //wcout << L"0x01  (1) Telegramm-anforderungen fuer Lese, Schreib und Ueberwachungen\r\n";
                    //wcout << L"0x02  (2) Telegramme die gesendet oder empfangen werden\r\n";
                    //wcout << L"0x04  (4) Ueberwachungen(Monitor) wird nach dem Einrichten angezeigt mit Handle\r\n";
                    //wcout << L"0x08  (8) Benachrichtigung über Variablenaenderung\r\n";
                    //wcout << L"0x10 (16) Eventlog Meldungen\r\n";
                    //wcout << L"0x20 (32) Debugfile mit Bytes der Netzwerksverbindung zum Klient\r\n";
                    return iRet;
                }
            }
        }
    }
    else
    {
        Service svr(szSvrName);
        iRet = svr.Run();
    }

    return iRet;
}
