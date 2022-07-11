/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#include <regex>
#include <codecvt>
#include <fcntl.h>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#pragma comment(lib, "SrvLib.lib")
#else
#include <unistd.h>
#endif

#include "SrvLib/Service.h"
#include "ConfFile.h"
#include "HttpServ.h"
#include "SpawnProcess.h"
#include "LogFile.h"

#ifndef _UTFCONVERTER
#define _UTFCONVERTER
std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> Utf8Converter;
#endif


const static wregex s_rxSepSpace(L"\\s+");
const static wregex s_rxSepComma(L"\\s*,\\s*");


void ReadConfiguration(const wstring& m_strModulePath, deque<CHttpServ>& m_vServers)
{
    ConfFile& conf = ConfFile::GetInstance(m_strModulePath + L"server.cfg");

    static const pair<wstring, int> strKeyWordUniqueItems[] = { { L"DefaultItem", 1 },{ L"RootDir", 2 },{ L"LogFile", 3 },{ L"ErrorLog",4 },{ L"KeyFile",6 },{ L"CertFile",7 },{ L"CaBundle",8 },{ L"SSL", 9 },{ L"MsgDir", 10 },{ L"SSLCipher", 11 },{ L"MaxConnPerIp", 12 } };
    static const pair<wstring, int> strKeyWordMultiItems[] = { { L"RewriteRule",1 },{ L"AliasMatch",2 },{ L"ForceType",3 },{ L"FileTyps",4 },{ L"SetEnvIf",5 },{ L"RedirectMatch",6 },{ L"DeflateTyps",7 },{ L"Authenticate",8 },{ L"ScriptAliasMatch",9 },
                                                               { L"ScriptOptionsHdl",10 },{ L"AddHeader", 11 },{ L"ReverseProxy", 12},{ L"ScriptAuthHdl",13 } };

    vector<wstring> vFileTypExt = move(conf.get(L"FileTyps"));

    vector<wstring> vListen = move(conf.get(L"Listen"));
    if (vListen.empty() == true)
        vListen.push_back(L"127.0.0.1"), vListen.push_back(L"::1");

    map<string, vector<wstring>> mIpPortCombi;
    deque<CHttpServ> vNewServers;
    for (const auto& strListen : vListen)
    {
        string strIp = Utf8Converter.to_bytes(strListen);
        vector<wstring> vPort = move(conf.get(L"Listen", strListen));
        if (vPort.empty() == true)
            vPort.push_back(L"80");
        for (const auto& strPort : vPort)
        {   // Default Werte setzen
            uint16_t nPort = 0;
            try { nPort = static_cast<uint16_t>(stoi(strPort));}
            catch (const std::exception& /*ex*/) { }

            if (mIpPortCombi.find(strIp) == end(mIpPortCombi))
                mIpPortCombi.emplace(strIp, vector<wstring>({ strPort }));
            else
                mIpPortCombi.find(strIp)->second.push_back(strPort);
            if (find_if(begin(m_vServers), end(m_vServers), [nPort, strListen](auto& HttpServer) { return HttpServer.GetPort() == nPort && HttpServer.GetBindAdresse() == Utf8Converter.to_bytes(strListen) ? true : false; }) != end(m_vServers))
                continue;
            vNewServers.emplace_back(m_strModulePath + L".", strIp, nPort, false);
        }
    }

    // Server stoppen how should be deleted
    for (deque<CHttpServ>::iterator itServer = begin(m_vServers); itServer != end(m_vServers);)
    {
        const map<string, vector<wstring>>::iterator itIp = mIpPortCombi.find(itServer->GetBindAdresse());
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
            bool bClearList = false;

            for (const auto& strKey : strKeyWordUniqueItems)
            {
                wstring strValue = conf.getUnique(strSection, strKey.first);
                if (strValue.empty() == false)
                {
                    switch (strKey.second)
                    {
                    case 1:
                    {
                        HostParam.m_vstrDefaultItem.clear();
                        wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
                        while (token != wsregex_token_iterator())
                        {
                            HostParam.m_vstrDefaultItem.push_back(token++->str());
                            HostParam.m_vstrDefaultItem.back().erase(HostParam.m_vstrDefaultItem.back().find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                            HostParam.m_vstrDefaultItem.back().erase(0, HostParam.m_vstrDefaultItem.back().find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                        }
                    }
                    break;
                    case 2: HostParam.m_strRootPath = strValue; break;
                    case 3: HostParam.m_strLogFile = strValue; break;
                    case 4: HostParam.m_strErrLog = strValue; break;
                    case 6: HostParam.m_strHostKey = Utf8Converter.to_bytes(strValue); break;
                    case 7: HostParam.m_strHostCertificate = Utf8Converter.to_bytes(strValue); break;
                    case 8: HostParam.m_strCAcertificate = Utf8Converter.to_bytes(strValue); break;
                    case 9: transform(begin(strValue), end(strValue), begin(strValue), [](wchar_t c) noexcept { return static_cast<wchar_t>(::toupper(c)); });
                        HostParam.m_bSSL = strValue == L"TRUE" ? true : false; break;
                    case 10:HostParam.m_strMsgDir = strValue; break;
                    case 11:HostParam.m_strSslCipher = Utf8Converter.to_bytes(strValue); break;
                    case 12:HostParam.m_nMaxConnPerIp = stoi(strValue); break;
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
                        HostParam.m_mstrRewriteRule.clear();
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
                        if (bClearList == false)
                            HostParam.m_mstrAliasMatch.clear(), bClearList = true;
                        for (const auto& strValue : vValues)
                        {
                            const static wregex rx(L"([^\\s\\\"]+)|\\\"([^\"\\\\]*(?:\\\\.[^\"\\\\]*)*)\\\"");
                            vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                            if (token.size() >= 2)
                            {
                                for (size_t n = 0; n < token.size(); ++n)
                                {
                                    token[n] = regex_replace(token[n], wregex(L"\\\\\\\""), L"`");
                                    token[n].erase(token[n].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                    token[n].erase(0, token[n].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                    token[n] = regex_replace(token[n], wregex(L"`"), L"\"");
                                }
                                if (HostParam.m_mstrAliasMatch.find(token[0]) == end(HostParam.m_mstrAliasMatch))
                                    HostParam.m_mstrAliasMatch.emplace(token[0], make_tuple(token, strKey.second == 9 ? true : false));
                                else
                                    HostParam.m_mstrAliasMatch[token[0]] = make_tuple(token, strKey.second == 9 ? true : false);
                                get<0>(HostParam.m_mstrAliasMatch[token[0]]).erase(get<0>(HostParam.m_mstrAliasMatch[token[0]]).begin());
                            }
                        }
                        break;
                    case 3: // ForceType
                        HostParam.m_mstrForceTyp.clear();
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
                        HostParam.m_mFileTypeAction.clear();
                        for (const auto& strValue : vValues)
                        {
                            const static wregex rx(L"([^\\s\\\"]+)|\\\"([^\"\\\\]*(?:\\\\.[^\"\\\\]*)*)\\\"");
                            vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                            if (token.size() >= 2)
                            {
                                for (size_t n = 0; n < token.size(); ++n)
                                {
                                    token[n] = regex_replace(token[n], wregex(L"\\\\\\\""), L"`");
                                    token[n].erase(token[n].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                    token[n].erase(0, token[n].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                    token[n] = regex_replace(token[n], wregex(L"`"), L"\"");
                                }
                                if (HostParam.m_mFileTypeAction.find(token[0]) == end(HostParam.m_mFileTypeAction))
                                    HostParam.m_mFileTypeAction.emplace(token[0], token);
                                else
                                    HostParam.m_mFileTypeAction[token[0]] = token;
                                HostParam.m_mFileTypeAction[token[0]].erase(HostParam.m_mFileTypeAction[token[0]].begin());
                            }
                        }
                        break;
                    case 5: // SetEnvIf
                        HostParam.m_vEnvIf.clear();
                        for (const auto& strValue : vValues)
                        {
                            const static wregex rx(L"([^\\s,\\\"]+)|\\\"([^\\\"]+)\\\"");
                            vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                            if (token.size() >= 3)
                            {
                                transform(begin(token[0]), end(token[0]), begin(token[0]), [](wchar_t c) noexcept { return static_cast<wchar_t>(::toupper(c)); });
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
                        HostParam.m_vRedirMatch.clear();
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
                        HostParam.m_vDeflateTyps.clear();
                        for (const auto& strValue : vValues)
                        {
                            wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
                            while (token != wsregex_token_iterator())
                                HostParam.m_vDeflateTyps.push_back(Utf8Converter.to_bytes(token++->str()));
                        }
                        break;
                    case 8: // Authenticate
                        HostParam.m_mAuthenticate.clear();
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
                                transform(begin(token[2]), end(token[2]), begin(token[2]), [](wchar_t c) noexcept { return static_cast<wchar_t>(::toupper(c)); });

                                pair<unordered_map<wstring, tuple<wstring, wstring, vector<wstring>>>::iterator, bool> itNew;
                                if (HostParam.m_mAuthenticate.find(token[0]) == end(HostParam.m_mAuthenticate))
                                    itNew = HostParam.m_mAuthenticate.emplace(token[0], make_tuple(token[1], token[2], vector<wstring>()));
                                else
                                {
                                    itNew = make_pair(HostParam.m_mAuthenticate.find(token[0]), true);
                                    itNew.first->second = make_tuple(token[1], token[2], vector<wstring>());
                                }

                                if (itNew.second == true)
                                {
                                    for (size_t n = 3; n < token.size(); ++n)
                                        get<2>(itNew.first->second).emplace_back(token[n]);
                                }
                            }
                        }
                        break;
                    case 10:// ScriptOptionsHdl
                    case 13:// ScriptAuthHdl
                        if (strKey.second == 10)
                            HostParam.m_vOptionsHandler.clear();
                        else if (strKey.second == 13)
                            HostParam.m_vAuthHandler.clear();
                        for (const auto& strValue : vValues)
                        {
                            strKey.second == 10 ? HostParam.m_vOptionsHandler.push_back(strValue) : HostParam.m_vAuthHandler.push_back(strValue);
                        }
                        break;
                    case 11:// AddHeader
                        HostParam.m_vHeader.clear();
                        for (const auto& strValue : vValues)
                        {
                            const static wregex rx(L"([^\\s,\\\"]+)|\\\"([^\\\"]+)\\\"");
                            vector<wstring> token(wsregex_token_iterator(begin(strValue), end(strValue), rx), wsregex_token_iterator());
                            if (token.size() == 2)
                            {
                                for (size_t n = 0; n < token.size(); ++n)
                                {
                                    token[n].erase(token[n].find_last_not_of(L"\" \t\r\n") + 1);  // Trim Whitespace and " character on the right
                                    token[n].erase(0, token[n].find_first_not_of(L"\" \t"));      // Trim Whitespace and " character on the left
                                }

                                string strKeyWord = Utf8Converter.to_bytes(token[0]);
                                const auto& itHeader = find_if(begin(HostParam.m_vHeader), end(HostParam.m_vHeader), [&](auto& itHeader) noexcept { return get<0>(itHeader).compare(strKeyWord) == 0 ? true : false; });
                                if (itHeader == end(HostParam.m_vHeader))
                                    HostParam.m_vHeader.emplace_back(make_pair(strKeyWord, Utf8Converter.to_bytes(token[1])));
                                else
                                    get<1>(*itHeader) = Utf8Converter.to_bytes(token[1]);
                            }
                        }
                        break;
                    case 12:// ReverseProxy
                        HostParam.m_mstrReverseProxy.clear();
                        for (const auto& strValue : vValues)
                        {
                            wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepSpace, -1);
                            if (token != wsregex_token_iterator())
                            {
                                if (HostParam.m_mstrReverseProxy.find(token->str()) == end(HostParam.m_mstrReverseProxy))
                                    HostParam.m_mstrReverseProxy.emplace(token->str(), next(token)->str());//strValue.substr(token->str().size() + 1));
                                else
                                    HostParam.m_mstrReverseProxy[token->str()] = next(token)->str();
                            }
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
            fnSetParameter(strListenAddr + L":" + to_wstring(HttpServer.GetPort()), IsVHost == true ? Utf8Converter.to_bytes(strListenAddr) + ":" + to_string(HttpServer.GetPort()) : string());

            const wstring strValue = conf.getUnique(strListenAddr + L":" + to_wstring(HttpServer.GetPort()), L"VirtualHost");
            wsregex_token_iterator token(begin(strValue), end(strValue), s_rxSepComma, -1);
            while (token != wsregex_token_iterator() && token->str().empty() == false)
                fuSetHostParam(token++->str(), true);
        };

        fuSetHostParam(Utf8Converter.from_bytes(HttpServer.GetBindAdresse()), false);
    }

    // Server starten und speichern
    for (auto& HttpServer : m_vServers)
    {
        if (HttpServer.Start() == false)
            CLogFile::GetInstance(HttpServer.GetParameterBlockRef(string()).m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] could not start server on: ", HttpServer.GetBindAdresse());
    }
}


int main(int argc, const char* argv[])
{
    SrvParam SrvPara;
    wstring m_strModulePath;
    deque<CHttpServ> m_vServers;

#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));
    _setmode(_fileno(stdout), _O_U16TEXT);

    SrvPara.szDspName = L"HTTP/2 Server";
    SrvPara.szDescrip = L"Http 2.0 Server by Thomas Hauck";
#endif

    SrvPara.szSrvName = L"Http2Serv";
    SrvPara.fnStartCallBack = [&m_strModulePath, &m_vServers]()
    {
        m_strModulePath = wstring(FILENAME_MAX, 0);
#if defined(_WIN32) || defined(_WIN64)
        if (GetModuleFileName(nullptr, &m_strModulePath[0], FILENAME_MAX) > 0)
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
        ReadConfiguration(m_strModulePath, m_vServers);
    };
    
    SrvPara.fnStopCallBack = [&m_vServers]()
    {
        // Server stoppen
        for (auto& HttpServer : m_vServers)
            HttpServer.Stop();

        // Warten bis alle Verbindungen / Ressourcen geschlossen sind
        for (auto& HttpServer : m_vServers)
        {
            while (HttpServer.IsStopped() == false)
                this_thread::sleep_for(chrono::milliseconds(10));
        }

        m_vServers.clear();
    };

    SrvPara.fnSignalCallBack = [&m_strModulePath, &m_vServers]()
    {
        ReadConfiguration(m_strModulePath, m_vServers);
    };

    return ServiceMain(argc, argv, SrvPara);
}
