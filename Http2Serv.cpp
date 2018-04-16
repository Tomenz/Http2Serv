// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include <iostream>
#include <signal.h>

#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#else
#include <syslog.h>
#pragma message("TODO!!! Folge Zeile wieder entfernen.")
#include <termios.h>
#include <fcntl.h>
#include <dirent.h>
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
#include "Psapi.h"
#pragma comment(lib, "Psapi.lib")

void se_translator(size_t e, _EXCEPTION_POINTERS* p)
{
    throw e;
}
#else
class CBaseSrv
{
public:
    explicit CBaseSrv(wchar_t*) {}
    virtual int Run(void) { Start(); return 0; }
    virtual void Start(void) = 0;
};
#endif

const static wregex s_rxSepSpace(L"\\s+");
const static wregex s_rxSepComma(L"\\s*,\\s*");

class Service : public CBaseSrv
{
public:
    static Service& GetInstance(const wchar_t* szSrvName = nullptr)
    {
        if (s_pInstance == 0)
            s_pInstance.reset(new Service(szSrvName));
        return *s_pInstance.get();
    }

    virtual void Start(void)
    {
        // Set the Exception Handler-function
        //_set_se_translator(se_translator);

        m_bIsStopped = false;

        m_strModulePath = wstring(FILENAME_MAX, 0);
#if defined(_WIN32) || defined(_WIN64)
        if (GetModuleFileName(NULL, &m_strModulePath[0], FILENAME_MAX) > 0)
            m_strModulePath.erase(m_strModulePath.find_last_of(L'\\') + 1); // Sollte der Backslash nicht gefunden werden wird der ganz String gelöscht

        if (_wchdir(m_strModulePath.c_str()) != 0)
            m_strModulePath = L"./";
#else
        string strTmpPath(FILENAME_MAX, 0);
        if (readlink(string("/proc/" + to_string(getpid()) + "/exe").c_str(), &strTmpPath[0], FILENAME_MAX) > 0)
            strTmpPath.erase(strTmpPath.find_last_of('/'));

        //Change Directory
        //If we cant find the directory we exit with failure.
        if ((chdir(strTmpPath.c_str())) < 0) // if ((chdir("/")) < 0)
            strTmpPath = ".";
        m_strModulePath = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(strTmpPath) + L"/";
#endif
        ReadConfiguration();

        // Server starten
        //for (auto& HttpServer : m_vServers)
        //    HttpServer.Start();

        while (m_bStop == false)
            this_thread::sleep_for(chrono::milliseconds(100));

        // Server stoppen
        for (auto& HttpServer : m_vServers)
            HttpServer.Stop();

        // Warten bis alle Verbindungen / Ressourcen geschlossen sind
        for (auto& HttpServer : m_vServers)
        {
            while (HttpServer.IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(10));
        }

        m_bIsStopped = true;
    };
    virtual void Stop(void) { m_bStop = true; }
    bool IsStopped(void) { return m_bIsStopped; }

    void ReadConfiguration()
    {
        const ConfFile& conf = ConfFile::GetInstance(m_strModulePath + L"server.cfg");

        static const pair<wstring, int> strKeyWordUniqueItems[] = { { L"DefaultItem", 1 },{ L"RootDir", 2 },{ L"LogFile", 3 },{ L"ErrorLog",4 },{ L"SSL_DH_ParaFile",5 },{ L"KeyFile",6 },{ L"CertFile",7 },{ L"CaBundle",8 },{ L"SSL", 9 },{ L"MsgDir", 10 } };
        static const pair<wstring, int> strKeyWordMultiItems[] = { { L"RewriteRule",1 },{ L"AliasMatch",2 },{ L"ForceType",3 },{ L"FileTyps",4 },{ L"SetEnvIf",5 },{ L"RedirectMatch",6 },{ L"DeflateTyps",7 },{ L"Authenticate",8 },{ L"ScriptAliasMatch",9 },{L"ScriptOptionsHdl",10} };

        vector<wstring>&& vFileTypExt = conf.get(L"FileTyps");

        vector<wstring>&& vListen = conf.get(L"Listen");
        if (vListen.empty() == true)
            vListen.push_back(L"127.0.0.1"), vListen.push_back(L"::1");

        map<string, vector<wstring>> mIpPortCombi;
        deque<CHttpServ> vNewServers;
        for (const auto& strListen : vListen)
        {
            string strIp = string(begin(strListen), end(strListen));
            vector<wstring>&& vPort = conf.get(L"Listen", strListen);
            if (vPort.empty() == true)
                vPort.push_back(L"80");
            for (const auto& strPort : vPort)
            {   // Default Werte setzen
                if (mIpPortCombi.find(strIp) == end(mIpPortCombi))
                    mIpPortCombi.emplace(strIp, vector<wstring>({ strPort }));
                else
                    mIpPortCombi.find(strIp)->second.push_back(strPort);
                if (find_if(begin(m_vServers), end(m_vServers), [strPort, strListen](auto& HttpServer) { return HttpServer.GetPort() == stoi(strPort) && HttpServer.GetBindAdresse() == string(begin(strListen), end(strListen)) ? true : false; }) != end(m_vServers))
                    continue;
                vNewServers.emplace_back(m_strModulePath + L".", strIp, stoi(strPort), false);
            }
        }

        // Server stoppen how should be deleted
        for (deque<CHttpServ>::iterator itServer = begin(m_vServers); itServer != end(m_vServers);)
        {
            map<string, vector<wstring>>::iterator itIp = mIpPortCombi.find(itServer->GetBindAdresse());
            if (itIp != end(mIpPortCombi))
            {
                if (find_if(begin(itIp->second), end(itIp->second), [itServer](auto strPort) { return itServer->GetPort() == stoi(strPort) ? true : false; }) != end(itIp->second))
                {
                    ++itServer;
                    continue;
                }
            }
            // Alle Server hier sollten gestoppt und entfernt werden
            itServer->Stop();
            // Warten bis alle Verbindungen / Ressourcen geschlossen sind
            while (itServer->IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(10));

            itServer = m_vServers.erase(itServer);
        }

        // Move the new Servers to the main list
        for (auto& HttpServer : vNewServers)
            m_vServers.emplace_back(move(HttpServer));

        // Read for every server the configuration
        for (auto& HttpServer : m_vServers)
        {
            HttpServer.Stop();
            // Warten bis alle Verbindungen / Ressourcen geschlossen sind
            while (HttpServer.IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(10));

            // Default und Common Parameter of the listening socket
            auto fnSetParameter = [&](const wstring& strSection, const string& szHost = string())
            {
                if (strSection == L"common")  // Default Parametersatz
                    HttpServer.ClearAllParameterBlocks();
                CHttpServ::HOSTPARAM& HostParam = HttpServer.GetParameterBlockRef(szHost);

                for (const auto& strKey : strKeyWordUniqueItems)
                {
                    wstring strValue = conf.getUnique(strSection, strKey.first);
                    if (strValue.empty() == false)
                    {
                        switch (strKey.second)
                        {
                        case 1:
                        {
                            wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
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
                        case 10:HostParam.m_strMsgDir = strValue; break;
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
                                wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
                                if (token != wsregex_token_iterator())
                                {
                                    if (HostParam.m_mstrRewriteRule.find(token->str()) == end(HostParam.m_mstrRewriteRule))
                                        HostParam.m_mstrRewriteRule.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                                    else
                                        HostParam.m_mstrRewriteRule[token->str()] = next(token)->str();
                                }
                            }
                            break;
                        case 2: // AliasMatch
                        case 9: // ScriptAliasMatch
                            for (const auto& strValue : vValues)
                            {
                                //const static wregex rx(L"([^\\s\\\"]+)|\\\"([^\\\"]+)\\\"");
                                const static wregex rx(L"([^\\s\\\"]+)|\\\"([^\"\\\\]*(?:\\\\.[^\"\\\\]*)*)\\\"");
                                vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                                if (token.size() == 2)
                                {
                                    for (size_t n = 0; n < token.size(); ++n)
                                    {
                                        token[n] = regex_replace(token[n], wregex(L"\\\\\\\""), L"`");
                                        token[n].erase(token[n].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                        token[n].erase(0, token[n].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                        token[n] = regex_replace(token[n], wregex(L"`"), L"\"");
                                    }
                                    if (HostParam.m_mstrAliasMatch.find(token[0]) == end(HostParam.m_mstrAliasMatch))
                                        HostParam.m_mstrAliasMatch.emplace(token[0], make_tuple(token[1], strKey.second == 9 ? true : false));
                                    else
                                        HostParam.m_mstrAliasMatch[token[0]] = make_tuple(token[1], strKey.second == 9 ? true : false);
                                }
                            }
                            break;
                        case 3: // ForceType
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
                                if (token != wsregex_token_iterator())
                                {
                                    if (HostParam.m_mstrForceTyp.find(token->str()) == end(HostParam.m_mstrForceTyp))
                                        HostParam.m_mstrForceTyp.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                                    else
                                        HostParam.m_mstrForceTyp[token->str()]= next(token)->str();
                                }
                            }
                            break;
                        case 4: // FileTyps
                            for (const auto& strValue : vValues)
                            {
                                //const static wregex rx(L"([^\\s\\\"]+)|\\\"([^\\\"]+)\\\"");
                                const static wregex rx(L"([^\\s\\\"]+)|\\\"([^\"\\\\]*(?:\\\\.[^\"\\\\]*)*)\\\"");
                                vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                                if (token.size() == 2)
                                {
                                    for (size_t n = 0; n < token.size(); ++n)
                                    {
                                        token[n] = regex_replace(token[n], wregex(L"\\\\\\\""), L"`");
                                        token[n].erase(token[n].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                        token[n].erase(0, token[n].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                        token[n] = regex_replace(token[n], wregex(L"`"), L"\"");
                                    }
                                    if (HostParam.m_mFileTypeAction.find(token[0]) == end(HostParam.m_mFileTypeAction))
                                        HostParam.m_mFileTypeAction.emplace(token[0], token[1]);//strValue.substr(token->str().size() + 1));
                                    else
                                        HostParam.m_mFileTypeAction[token[0]] = token[1];
                                }
                            }
                            break;
                        case 5: // SetEnvIf
                            for (const auto& strValue : vValues)
                            {
                                const static wregex rx(L"([^\\s,\\\"]+)|\\\"([^\\\"]+)\\\"");
                                vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                                if (token.size() >= 3)
                                {
                                    transform(begin(token[0]), end(token[0]), begin(token[0]), ::toupper);
                                    for (size_t n = 0; n < token.size(); ++n)
                                    {
                                        token[n].erase(token[n].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                        token[n].erase(0, token[n].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                    }
                                    for (size_t n = 2; n < token.size(); ++n)
                                        HostParam.m_vEnvIf.emplace_back(make_tuple(token[0], token[1], token[n]));
                                }
                            }
                            break;
                        case 6: // RedirectMatch
                            for (const auto& strValue : vValues)
                            {
                                wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
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
                                wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
                                while (token != wsregex_token_iterator())
                                    HostParam.m_vDeflateTyps.push_back(Utf8Converter.to_bytes(token++->str()));
                            }
                            break;
                        case 8: // Authenticate
                            for (const auto& strValue : vValues)
                            {
                                const static wregex rx(L"([^\\s,\\\"]+)|\\\"([^\\\"]+)\\\"");
                                vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                                if (token.size() >= 3)
                                {
                                    for (size_t n = 0; n < token.size(); ++n)
                                    {
                                        token[n].erase(token[n].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                        token[n].erase(0, token[n].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                    }
                                    transform(begin(token[2]), end(token[2]), begin(token[2]), ::toupper);

                                    pair<unordered_map<wstring, tuple<wstring, wstring, vector<wstring>>>::iterator, bool> itNew;
                                    if (HostParam.m_mAuthenticate.find(token[0]) == end(HostParam.m_mAuthenticate))
                                        itNew = HostParam.m_mAuthenticate.emplace(token[0], make_tuple(token[1], token[2], vector<wstring>()));
                                    else
                                        itNew = make_pair(HostParam.m_mAuthenticate.find(token[0]), true);

                                    if (itNew.second == true)
                                    {
                                        for (size_t n = 3; n < token.size(); ++n)
                                            get<2>(itNew.first->second).emplace_back(token[n]);
                                    }
                                }
                            }
                            break;
                        case 10:// ScriptOptionsHdl
                            for (const auto& strValue : vValues)
                            {
                                HostParam.m_vOptionsHandler.push_back(strValue);
                            }
                            break;
                        }
                    }
                }
            };
            fnSetParameter(L"common");

            ///////////////////////////////////////////

            // Host Parameter holen und setzen
            function<void(wstring, bool)> fuSetHostParam = [&](wstring strListenAddr, bool IsVHost)
            {
                fnSetParameter(strListenAddr + L":" + to_wstring(HttpServer.GetPort()), IsVHost == true ? string(begin(strListenAddr), end(strListenAddr)) + ":" + to_string(HttpServer.GetPort()) : string());

                const wstring strValue = conf.getUnique(strListenAddr + L":" + to_wstring(HttpServer.GetPort()), L"VirtualHost");
                wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepComma, -1);
                while (token != wsregex_token_iterator() && token->str().empty() == false)
                    fuSetHostParam(token++->str(), true);
            };

            fuSetHostParam(Utf8Converter.from_bytes(HttpServer.GetBindAdresse()), false);
        }

        // Server starten und speichern
        for (auto& HttpServer : m_vServers)
            HttpServer.Start();
    }

    static void SignalHandler(int iSignal)
    {
        signal(iSignal, Service::SignalHandler);

        Service::GetInstance().ReadConfiguration();

#if defined(_WIN32) || defined(_WIN64)
        OutputDebugString(L"STRG+C-Signal empfangen\r\n");
#else
        wcout << L"Signal SIGHUP empfangen\r\n";
#endif
    }

private:
    Service(const wchar_t* szSrvName) : CBaseSrv(szSrvName), m_bStop(false), m_bIsStopped(true) { }

private:
    static shared_ptr<Service> s_pInstance;
    wstring m_strModulePath;
    deque<CHttpServ> m_vServers;
    bool m_bStop;
    bool m_bIsStopped;
};

shared_ptr<Service> Service::s_pInstance = nullptr;


#if defined(_WIN32) || defined(_WIN64)
DWORD WINAPI RemoteThreadProc(LPVOID/* lpParameter*/)
{
    return raise(SIGINT);
}
#endif

int main(int argc, const char* argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));
    _setmode(_fileno(stdout), _O_U16TEXT);

    static const wchar_t szDspName[] = { L"HTTP/2 Server" };
    static wchar_t szDescrip[] = { L"Http 2.0 Server by Thomas Hauck" };

    signal(SIGINT, Service::SignalHandler);

#else

    signal(SIGHUP, Service::SignalHandler);

    auto _kbhit = []() -> int
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
#endif

    static const wchar_t szSvrName[] = { L"Http2Serv" };
    int iRet = 0;

    if (argc > 1)
    {
        while (++argv, --argc)
        {
            if (argv[0][0] == '-')
            {
                switch ((argv[0][1] & 0xdf))
                {
#if defined(_WIN32) || defined(_WIN64)
                case 'I':
                    iRet = CSvrCtrl().Install(szSvrName, szDspName, szDescrip);
                    break;
                case 'R':
                    iRet = CSvrCtrl().Remove(szSvrName);
                    break;
                case 'S':
                    iRet = CSvrCtrl().Start(szSvrName);
                    break;
                case 'E':
                    iRet = CSvrCtrl().Stop(szSvrName);
                    break;
                case 'P':
                    iRet = CSvrCtrl().Pause(szSvrName);
                    break;
                case 'C':
                    iRet = CSvrCtrl().Continue(szSvrName);
                    break;
#endif
                case 'F':
                    {
                        wcout << szSvrName << L" gestartet" << endl;

                        Service::GetInstance(szSvrName);

                        thread th([&]() {
                            Service::GetInstance().Start();
                        });

                        const wchar_t caZeichen[] = L"\\|/-";
                        int iIndex = 0;
                        while (_kbhit() == 0)
                        {
                            SpawnProcess::s_mtxIOstreams.lock();
                            wcout << L'\r' << caZeichen[iIndex++] /*<< L"  Sockets:" << setw(3) << BaseSocket::s_atRefCount << L"  SSL-Pumpen:" << setw(3) << SslTcpSocket::s_atAnzahlPumps << L"  HTTP-Connections:" << setw(3) << nHttpCon*/ << flush;
                            SpawnProcess::s_mtxIOstreams.unlock();
                            if (iIndex > 3) iIndex = 0;
                            this_thread::sleep_for(chrono::milliseconds(100));
                        }

                        wcout << szSvrName << L" gestoppt" << endl;
                        Service::GetInstance().Stop();
                        th.join();
                    }
                    break;
                case 'K':
                    {
                        //raise(SIGINT);
#if defined(_WIN32) || defined(_WIN64)
                        wstring strPath(MAX_PATH, 0);
                        GetModuleFileName(NULL, &strPath[0], MAX_PATH);
                        strPath.erase(strPath.find_first_of(L'\0'));
                        strPath.erase(0, strPath.find_last_of(L'\\') + 1);

                        if (strPath.empty() == false)
                        {
                            DWORD dwInitSize = 1024;
                            DWORD dwIdReturned = 0;
                            unique_ptr<DWORD[]> pBuffer = make_unique<DWORD[]>(dwInitSize);
                            while (dwInitSize < 16384 && EnumProcesses(pBuffer.get(), sizeof(DWORD) * dwInitSize, &dwIdReturned) != 0)
                            {
                                if (dwIdReturned == sizeof(DWORD) * dwInitSize) // Buffer to small
                                {
                                    dwInitSize *= 2;
                                    pBuffer = make_unique<DWORD[]>(dwInitSize);
                                    continue;
                                }
                                dwIdReturned /= sizeof(DWORD);

                                for (DWORD n = 0; n < dwIdReturned; ++n)
                                {
                                    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pBuffer.get()[n]);
                                    if (hProcess != NULL)
                                    {
                                        wstring strEnumPath(MAX_PATH, 0);
                                        if (GetModuleBaseName(hProcess, NULL, &strEnumPath[0], MAX_PATH) > 0)
                                            strEnumPath.erase(strEnumPath.find_first_of(L'\0'));
                                        else
                                        {
                                            strEnumPath.erase(GetProcessImageFileName(hProcess, &strEnumPath[0], MAX_PATH));
                                            strEnumPath.erase(0, strEnumPath.find_last_of('\\') + 1);
                                        }

                                        if (strEnumPath.empty() == false && strEnumPath == strPath) // Same Name
                                        {
                                            if (GetCurrentProcessId() != pBuffer.get()[n])          // but other process
                                            {
                                                CreateRemoteThread(hProcess, nullptr, 0, RemoteThreadProc, nullptr, 0, nullptr);
                                                CloseHandle(hProcess);
                                                break;
                                            }
                                        }
                                        CloseHandle(hProcess);
                                    }
                                }
                                break;
                            }
                        }
#else
                        pid_t nMyId = getpid();
                        string strMyName(64, 0);
                        FILE* fp = fopen("/proc/self/comm", "r"); 
                        if (fp)
                        {
                            if (fgets(&strMyName[0], strMyName.size(), fp) != NULL)
                            {
                                strMyName.erase(strMyName.find_last_not_of('\0') + 1);
                                strMyName.erase(strMyName.find_last_not_of('\n') + 1);
                                //wcout << "Meine PID = " << nMyId << " = " << strMyName.c_str() << endl;
                            }
                            fclose(fp);
                        }

                        DIR* dir = opendir("/proc");
                        if (dir != nullptr)
                        {
                            struct dirent* ent;
                            char* endptr;

                            while ((ent = readdir(dir)) != NULL)
                            {
                                // if endptr is not a null character, the directory is not entirely numeric, so ignore it
                                long lpid = strtol(ent->d_name, &endptr, 10);
                                if (*endptr != '\0')
                                    continue;

                                // if the number is our own pid we ignore it
                                if ((pid_t)lpid == nMyId)
                                    continue;

                                // try to open the cmdline file
                                FILE* fp = fopen(string("/proc/" + to_string(lpid) + "/comm").c_str(), "r");
                                if (fp != nullptr)
                                {
                                    string strName(64, 0);
                                    if (fgets(&strName[0], strName.size(), fp) != NULL)
                                    {
                                        strName.erase(strName.find_last_not_of('\0') + 1);
                                        strName.erase(strName.find_last_not_of('\n') + 1);
                                        if (strName == strMyName)
                                        {
                                            //wcout << strName.c_str() << L" = " << (pid_t)lpid << endl;
                                            kill((pid_t)lpid, SIGHUP);
                                            break;
                                        }
                                    }
                                    fclose(fp);
                                }
                            }
                            closedir(dir);
                        }
#endif
                    }
                    break;
                case 'H':
                case '?':
                    wcout << L"\r\n";
#if defined(_WIN32) || defined(_WIN64)
                    wcout << L"-i   Istalliert den Systemdienst\r\n";
                    wcout << L"-r   Entfernt den Systemdienst\r\n";
                    wcout << L"-s   Startet den Systemdienst\r\n";
                    wcout << L"-e   Beendet den Systemdienst\r\n";
                    wcout << L"-p   Systemdienst wird angehaltet (Pause)\r\n";
                    wcout << L"-c   Systemdienst wird fortgesetzt (Continue)\r\n";
#endif
                    wcout << L"-f   Start die Anwendung als Konsolenanwendung\r\n";
                    wcout << L"-k   Konfiguration neu laden\r\n";
                    wcout << L"-h   Zeigt diese Hilfe an\r\n";
                    return iRet;
                }
            }
        }
    }
    else
    {
        Service::GetInstance(szSvrName);

#if !defined(_WIN32) && !defined(_WIN64)
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

        //Close Standard File Descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        thread([&]()
        {
            sigset_t sigset;
            sigemptyset(&sigset);
            sigaddset(&sigset, SIGTERM);
            sigprocmask(SIG_BLOCK, &sigset, NULL);

            int sig;
            sigwait(&sigset, &sig);

            Service::GetInstance().Stop();
        }).detach();

#endif
        iRet = Service::GetInstance().Run();
    }

    return iRet;
}
