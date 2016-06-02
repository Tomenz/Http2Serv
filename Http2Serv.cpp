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

#ifdef _DEBUG
    vector<wstring>&& vSec = ConfFile::GetInstance(L"server.cfg").get();
    for (const auto& item : vSec)
    {
        wcout << L"[" << item << L"]" << endl;
        vector<wstring>&& vKeys = ConfFile::GetInstance(L"server.cfg").get(item);
        for (const auto& key : vKeys)
        {
            vector<wstring>&& vValues = ConfFile::GetInstance(L"server.cfg").get(item, key);
            for (const auto& value : vValues)
                wcout << key << L" = " << value << endl;
        }
    }
#endif

    deque<CHttpServ> vServers;

    vector<wstring>&& vDefItem = ConfFile::GetInstance(L"server.cfg").get(L"common", L"DefaultItem");
    vector<wstring>&& vRootDir = ConfFile::GetInstance(L"server.cfg").get(L"common", L"RootDir");
    if (vRootDir.empty() == true)
        vRootDir.push_back(L"./html");
    vector<wstring>&& vLogFile = ConfFile::GetInstance(L"server.cfg").get(L"common", L"LogFile");
    vector<wstring>&& vErrLog = ConfFile::GetInstance(L"server.cfg").get(L"common", L"ErrorLog");
    vector<wstring>&& vReWrite = ConfFile::GetInstance(L"server.cfg").get(L"common", L"RewriteRule");
    vector<wstring>&& vAliasMatch = ConfFile::GetInstance(L"server.cfg").get(L"common", L"AliasMatch");

    vector<wstring>&& vFileTypExt = ConfFile::GetInstance(L"server.cfg").get(L"FileTyps");


    vector<wstring>&& vListen = ConfFile::GetInstance(L"server.cfg").get(L"Listen");
    for (const auto& strListen : vListen)
    {
        vector<wstring>&& vPort = ConfFile::GetInstance(L"server.cfg").get(L"Listen", strListen);
        for (const auto& strPort : vPort)
        {
            // Default Werte setzen
            vServers.emplace_back(*vRootDir.begin(), stoi(strPort), false);

            vServers.back().SetBindAdresse(string(begin(strListen), end(strListen)).c_str());
            if (vDefItem.empty() == false)
                vServers.back().SetDefaultItem(*vDefItem.begin());
            if (vLogFile.empty() == false)
                vServers.back().SetAccessLogFile(*vLogFile.begin());
            if (vErrLog.empty() == false)
                vServers.back().SetErrorLogFile(*vErrLog.begin());
            if (vReWrite.empty() == false)
                vServers.back().AddRewriteRule(vReWrite);
            if (vAliasMatch.empty() == false)
                vServers.back().AddAliasMatch(vAliasMatch);

            for (const auto& strFileExt : vFileTypExt)
            {
                vector<wstring>&& vFileTypAction = ConfFile::GetInstance(L"server.cfg").get(L"FileTyps", strFileExt);
                if (vFileTypAction.empty() == false)
                    vServers.back().AddFileTypAction(strFileExt, *vFileTypAction.begin());
            }

            // Host Parameter holen und setzen
            function<void(wstring, bool)> fuSetOstParam = [&](wstring strListenAddr, bool IsVHost)
            {
                auto tuSSLParam = make_tuple<bool, string, string, string>(false, "", "", "");
                vector<wstring> vHostList;
                vector<wstring>&& vHostPara = ConfFile::GetInstance(L"server.cfg").get(strListenAddr + L":" + strPort);
                for (const auto& strParamKey : vHostPara)
                {
                    vector<wstring>&& vParaValue = ConfFile::GetInstance(L"server.cfg").get(strListenAddr + L":" + strPort, strParamKey);
                    if (vParaValue.empty() == false)
                    {
                        if (strParamKey.compare(L"DefaultItem") == 0)
                            vServers.back().SetDefaultItem(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);
                        if (strParamKey.compare(L"RootDir") == 0)
                            vServers.back().SetRootDirectory(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);
                        if (strParamKey.compare(L"LogFile") == 0)
                            vServers.back().SetAccessLogFile(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);
                        if (strParamKey.compare(L"ErrorLog") == 0)
                            vServers.back().SetErrorLogFile(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);

                        if (strParamKey.compare(L"SSL") == 0 && vParaValue.begin()->compare(L"true") == 0)
                            get<0>(tuSSLParam) = true;
                        if (strParamKey.compare(L"KeyFile") == 0)
                            get<1>(tuSSLParam) = Utf8Converter.to_bytes(*vParaValue.begin());
                        if (strParamKey.compare(L"CertFile") == 0)
                            get<2>(tuSSLParam) = Utf8Converter.to_bytes(*vParaValue.begin());
                        if (strParamKey.compare(L"CaBundle") == 0)
                            get<3>(tuSSLParam) = Utf8Converter.to_bytes(*vParaValue.begin());

                        if (strParamKey.compare(L"VirtualHost") == 0 && IsVHost == false)
                        {
                            size_t nPos = vParaValue.begin()->find_first_of(L','), nStart = 0;
                            while (nPos != string::npos)
                            {
                                vHostList.push_back(vParaValue.begin()->substr(nStart, nPos++));
                                nStart += nPos;
                                nPos = vParaValue.begin()->find_first_of(L',', nStart);
                            }
                            vHostList.push_back(vParaValue.begin()->substr(nStart));
                        }
                    }
                }

                if (get<0>(tuSSLParam) == true && get<1>(tuSSLParam).empty() == false && get<2>(tuSSLParam).empty() == false && get<3>(tuSSLParam).empty() == false)
                    vServers.back().SetUseSSL(get<0>(tuSSLParam), get<3>(tuSSLParam), get<2>(tuSSLParam), get<1>(tuSSLParam), IsVHost == true ? strListenAddr.c_str() : nullptr);

                for (size_t i = 0; i < vHostList.size(); ++i)
                    fuSetOstParam(vHostList[i], true);
            };

            fuSetOstParam(strListen, false);
        }
    }

    // Server starten
    for (auto& HttpServer : vServers)
        HttpServer.Start();

#if defined(_WIN32) || defined(_WIN64)
    _getch();
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
        getchar();
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

