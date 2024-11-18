// Http2Proxy.cpp : Definiert den Einstiegspunkt f�r die Konsolenanwendung.
//

#if defined(_WIN32) || defined(_WIN64)
#define _HAS_STD_BYTE 0
#endif

#include <codecvt>
#include <fcntl.h>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "ConfFile.h"
#include "HttpProxy.h"
#include "SrvLib/Service.h"

int main(int argc, const char* argv[])
{
    SrvParam SrvPara;
    wstring m_strModulePath;
    deque<CHttpProxy> m_vServers;

#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));
    _setmode(_fileno(stdout), _O_U16TEXT);

    SrvPara.szDspName = L"HTTP/2 Proxy";
    SrvPara.szDescribe = L"Http 2.0 Proxy by Thomas Hauck";
#endif

    SrvPara.szSrvName = L"Http2Proxy";
    SrvPara.fnStartCallBack = [&m_strModulePath, &m_vServers]()
    {
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
        ConfFile& conf = ConfFile::GetInstance(m_strModulePath + L"proxy.cfg");
        vector<wstring>&& vListen = conf.get(L"Listen");
        if (vListen.empty() == true)
            vListen.push_back(L"127.0.0.1"), vListen.push_back(L"::1");

        map<string, vector<wstring>> mIpPortCombi;
        for (const auto& strListen : vListen)
        {
            string strIp = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strListen);
            vector<wstring>&& vPort = conf.get(L"Listen", strListen);
            if (vPort.empty() == true)
                vPort.push_back(L"8080");
            for (const auto& strPort : vPort)
            {   // Default Werte setzen
                if (mIpPortCombi.find(strIp) == end(mIpPortCombi))
                    mIpPortCombi.emplace(strIp, vector<wstring>({ strPort }));
                else
                    mIpPortCombi.find(strIp)->second.push_back(strPort);
                if (find_if(begin(m_vServers), end(m_vServers), [strPort, strListen](auto& HttpProxy) { return HttpProxy.GetPort() == stoi(strPort) && HttpProxy.GetBindAdresse() == wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strListen) ? true : false; }) != end(m_vServers))
                    continue;
                m_vServers.emplace_back(strIp, stoi(strPort));
            }
        }

        // Server starten
        for (auto& HttpProxy : m_vServers)
            HttpProxy.Start();
    };

    SrvPara.fnStopCallBack = [&m_vServers]()
    {
        // Server stoppen
        for (auto& HttpProxy : m_vServers)
            HttpProxy.Stop();

        // Warten bis alle Verbindungen / Ressourcen geschlossen sind
        for (auto& HttpProxy : m_vServers)
        {
            while (HttpProxy.IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(10));
        }

        m_vServers.clear();
};

    SrvPara.fnSignalCallBack = [&m_strModulePath, &m_vServers]()
    {
    };

    return ServiceMain(argc, argv, SrvPara);
}
