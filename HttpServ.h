/* Copyright (C) Hauck Software Solutions - All Rights Reserved
 * You may use, distribute and modify this code under the terms
 * that changes to the code must be reported back the original
 * author
 *
 * Company: Hauck Software Solutions
 * Author:  Thomas Hauck
 * Email:   Thomas@fam-hauck.de
 *
 */

#pragma once

#include <regex>
#include <map>
#include <algorithm>
#include <iomanip>

#include "socketlib/SslSocket.h"
#include "Timer.h"
#include "TempFile.h"
#include "H2Proto.h"
#include "LogFile.h"
#include "Base64.h"
#include "GZip.h"

using namespace std;
using namespace std::placeholders;

#if defined(_WIN32) || defined(_WIN64)

#if _MSC_VER < 1700
using namespace tr1;
#endif

#define FN_CA(x) x.c_str()
#define FN_STR(x) x
const wchar_t* ENV = L"SET ";
const wchar_t* ENVJOIN = L"&";
const wchar_t* QUOTES = L"";
#define FIXENVSTR(x) regex_replace(x, regex("\\&"), string("^&"))
#define WFIXENVSTR(x) regex_replace(x, wregex(L"\\&"), wstring(L"^&"))
const wchar_t* PIPETYPE = L"rb";

#ifdef _DEBUG
#pragma comment(lib, "Debug/socketlib.lib")
#else
#pragma comment(lib, "Release/socketlib.lib")
#endif

#else
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include <sys/stat.h>
#include <unistd.h>
#define _wpopen popen
#define _pclose pclose
#define _stat stat
#define _wstat stat
#define _stat64 stat64
#define _wstat64 stat64
#define _waccess access
#ifndef _UTFCONVERTER
#define _UTFCONVERTER
std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> Utf8Converter;
#endif
#define FN_CA(x) Utf8Converter.to_bytes(x).c_str()
#define FN_STR(x) Utf8Converter.to_bytes(x).c_str()
const wchar_t* ENV = L"";
const wchar_t* ENVJOIN = L" ";
const wchar_t* QUOTES = L"\"";
const char* PIPETYPE = "r";
#define FIXENVSTR(x) x
#define WFIXENVSTR(x) x
#endif

class CHttpServ : public Http2Protocol
{
    //typedef tuple<unique_ptr<Timer>, string, bool, uint64_t, uint64_t, unique_ptr<TempFile>, HEADERLIST, deque<HEADERENTRY>, unique_ptr<mutex>, STREAMLIST, STREAMSETTINGS, atomic<bool>> CONNECTIONDETAILS;
    typedef struct
    {
        shared_ptr<Timer> pTimer;
        string strBuffer;
        bool bIsH2Con;
        uint64_t nContentsSoll;
        uint64_t nContentRecv;
        shared_ptr<TempFile> TmpFile;
        HEADERLIST HeaderList;
        deque<HEADERENTRY> lstDynTable;
        shared_ptr<mutex> mutStreams;
        STREAMLIST H2Streams;
        STREAMSETTINGS StreamParam;
        shared_ptr<atomic_bool> atStop;
    } CONNECTIONDETAILS;

    typedef struct
    {
        const HEADERLIST& umHeaderList;
    }HEADERWRAPPER;

    typedef struct
    {
        STREAMLIST& StreamList;
    }HEADERWRAPPER2;

    typedef unordered_map<TcpSocket*, CONNECTIONDETAILS> CONNECTIONLIST;

    typedef tuple<const wchar_t*, const char*> MIMEENTRY;
    #define MIMEEXTENSION(x) get<0>(x)
    #define MIMESTRING(x) get<1>(x)

    enum
    {
        TERMINATEHEADER = 1,
        ADDCONNECTIONCLOSE = 2,
        ADDNOCACHE = 4,
        HTTPVERSION11 = 8,
        ADDCONENTLENGTH = 16,
        GZIPENCODING = 32,
        HSTSHEADER = 64,
    };

    typedef struct
    {
        vector<wstring> m_vstrDefaultItem;
        wstring m_strRootPath;
        wstring m_strLogFile;
        wstring m_strErrLog;
        bool    m_bSSL;
        string  m_strCAcertificate;
        string  m_strHostCertificate;
        string  m_strHostKey;
        vector<wstring> m_vstrRewriteRule;
        vector<wstring> m_vstrAliasMatch;
        map<wstring, wstring> m_mFileTypeAction;
    } HOSTPARAM;

public:

    CHttpServ(wstring strRootPath = L"./html", short sPort = 80, bool bSSL = false) : m_pSocket(nullptr), m_sPort(sPort), m_cLocal(locale("C"))
    {
        HOSTPARAM hp;
        hp.m_strRootPath = strRootPath;
        //hp.m_strLogFile = L"access.log";
        //hp.m_strErrLog = L"error.log";
        hp.m_bSSL = bSSL;
        hp.m_strCAcertificate = "";
        hp.m_strHostCertificate = "";
        hp.m_strHostKey = "";
        m_vHostParam.insert(make_pair(L"", hp));
    }

    virtual ~CHttpServ()
    {
        Stop();
    }

    bool Start()
    {
        if (m_vHostParam[L""].m_bSSL == true)
        {
            SslTcpServer* pSocket = new SslTcpServer();
            pSocket->AddCertificat(m_vHostParam[L""].m_strCAcertificate.c_str(), m_vHostParam[L""].m_strHostCertificate.c_str(), m_vHostParam[L""].m_strHostKey.c_str());
            pSocket->BindNewConnection(bind(&CHttpServ::OnNewConnection, this, _1, _2));

            for (auto& Item : m_vHostParam)
            {
                if (Item.first.compare(L"") != 0 && Item.second.m_bSSL == true)
                {
                    pSocket->AddCertificat(Item.second.m_strCAcertificate.c_str(), Item.second.m_strHostCertificate.c_str(), Item.second.m_strHostKey.c_str());
                }
            }

            m_pSocket = pSocket;
        }
        else
        {
            m_pSocket = new TcpServer();
            m_pSocket->BindNewConnection(bind(&CHttpServ::OnNewConnection, this, _1, _2));
        }

        return m_pSocket->Start(m_strBindIp.c_str(), m_sPort);
    }

    bool Stop()
    {
        if (m_pSocket != nullptr)
        {
            m_pSocket->Close();
            delete m_pSocket;
            m_pSocket = nullptr;
        }

        m_mtxConnections.lock();
        for (auto item : m_vConnections)
            item.first->Close();
        m_mtxConnections.unlock();

        while (m_vConnections.size() != 0)
            this_thread::sleep_for(chrono::milliseconds(1));

        return true;
    }

    CHttpServ& SetDefaultItem(const wstring& strDefItem, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];

        wregex separator(L"\\s+");
        wcregex_token_iterator token(strDefItem.c_str(), strDefItem.c_str() + strDefItem.size(), separator, -1);
        while (token != wcregex_token_iterator())
            m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_vstrDefaultItem.push_back(*token++);
        return *this;
    }

    CHttpServ& SetRootDirectory(const wstring& strRootDir, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_strRootPath = strRootDir;
        return *this;
    }

    CHttpServ& SetBindAdresse(const char* szBindIp)
    {
        m_strBindIp = szBindIp;
        return *this;
    }

    CHttpServ& SetPort(const short& sPort)
    {
        m_sPort = sPort;
        return *this;
    }

    CHttpServ& SetUseSSL(const bool& bSSL, const string& strCAcertificate, const string& strHostCertificate, const string& strHostKey, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_bSSL = bSSL;
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_strCAcertificate = strCAcertificate;
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_strHostCertificate = strHostCertificate;
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_strHostKey = strHostKey;
        return *this;
    }

    CHttpServ& SetAccessLogFile(const wstring& strLogFile, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_strLogFile = strLogFile;
        return *this;
    }

    CHttpServ& SetErrorLogFile(const wstring& strErrorLogFile, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_strErrLog = strErrorLogFile;
        return *this;
    }

    CHttpServ& AddRewriteRule(const vector<wstring>& vstrRewriteRule, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_vstrRewriteRule = vstrRewriteRule;
        return *this;
    }

    CHttpServ& AddAliasMatch(const vector<wstring>& vstrAliasMatch, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_vstrAliasMatch = vstrAliasMatch;
        return *this;
    }

    CHttpServ& AddFileTypAction(const wstring& strFileExtension, const wstring& strFileTypAction, const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];
        m_vHostParam[szHostName == nullptr ? L"" : szHostName].m_mFileTypeAction.insert(make_pair(strFileExtension, strFileTypAction));
        return *this;
    }

private:
    void OnNewConnection(TcpServer* pTcpServer, int nCountNewConnections)
    {
        for (int i = 0; i < nCountNewConnections; ++i)
        {
            TcpSocket* pSocket = pTcpServer->GetNextPendingConnection();
            //SslTcpSocket* pSocket = reinterpret_cast<SslTcpServer*>(pTcpServer)->GetNextPendingConnection();
            if (pSocket != nullptr)
            {
                pSocket->BindFuncBytesRecived(bind(&CHttpServ::OnDataRecieved, this, _1));
                pSocket->BindErrorFunction(bind(&CHttpServ::OnSocketError, this, _1));
                pSocket->BindCloseFunction(bind(&CHttpServ::OnSocketCloseing, this, _1));
                lock_guard<mutex> lock(m_mtxConnections);
                m_vConnections.emplace(pair<TcpSocket*, CONNECTIONDETAILS>(pSocket, { make_shared<Timer>(30000, bind(&CHttpServ::OnTimeout, this, _1)), string(), false, 0, 0, make_shared<TempFile>(), {}, {}, make_shared<mutex>(), {}, make_tuple(UINT32_MAX, 65535, 16384), make_shared<atomic_bool>(false) }));
                pSocket->StartReceiving();
            }
        }
    }

    void OnDataRecieved(TcpSocket* pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();

        if (nAvalible == 0)
        {
            pTcpSocket->Close();
            return;
        }

        shared_ptr<char> spBuffer(new char[nAvalible]);

        uint32_t nRead = pTcpSocket->Read(spBuffer.get(), nAvalible);

        if (nRead > 0)
        {
            m_mtxConnections.lock();
            CONNECTIONLIST::iterator item = m_vConnections.find(pTcpSocket);
            if (item != end(m_vConnections))
            {
                CONNECTIONDETAILS* pConDetails = &item->second;
                pConDetails->pTimer->Reset();
                pConDetails->strBuffer.append(reinterpret_cast<char*>(spBuffer.get()), nRead);

                if (pConDetails->bIsH2Con == false)
                {
                    if ( pConDetails->strBuffer.size() >= 24 && ::memcmp( pConDetails->strBuffer.c_str(), "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) == 0 && pConDetails->nContentsSoll == 0)
                    {
                        pTcpSocket->Write("\x0\x0\x6\x4\x0\x0\x0\x0\x0\x0\x4\x0\xa0\x0\x0", 15);// SETTINGS frame (4) with ParaID(4) and ?10485760? Value
                        pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\x9f\x0\x1", 13);      // WINDOW_UPDATE frame (8) with value ?10420225? (minus 65535)
                        pConDetails->bIsH2Con = true;
                        pConDetails->strBuffer.erase(0, 24);

                        if (pConDetails->strBuffer.size() == 0)
                        {
                            m_mtxConnections.unlock();
                            return;
                        }
                    }
                    else if (pConDetails->nContentsSoll != 0 && pConDetails->nContentRecv < pConDetails->nContentsSoll)  // File Download in progress
                    {
                        uint32_t nBytesToWrite = static_cast<uint32_t>(min(static_cast<uint64_t>(pConDetails->strBuffer.size()), pConDetails->nContentsSoll - pConDetails->nContentRecv));
                        pConDetails->TmpFile->Write( pConDetails->strBuffer.c_str(), nBytesToWrite);
                        pConDetails->nContentRecv += nBytesToWrite;
                        pConDetails->strBuffer.erase(0, nBytesToWrite);

                        if (pConDetails->nContentRecv < pConDetails->nContentsSoll)
                        {
                            m_mtxConnections.unlock();
                            return;
                        }

                        pConDetails->TmpFile->Close();
                    }
                }


                if (pConDetails->bIsH2Con == true)   // bool is http2
                {
                    size_t nLen =  pConDetails->strBuffer.size();
                    if (nLen < 9)
                    {
                        m_mtxConnections.unlock();
                        return;
                    }
                    unique_ptr<char> pBuf(new char[nLen]);
                    copy(begin(pConDetails->strBuffer), begin(pConDetails->strBuffer) + nLen, pBuf.get());

                    MetaSocketData soMetaDa({ pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, pConDetails->pTimer) });

                    size_t nRet;
                    if (nRet = Http2StreamProto(soMetaDa, pBuf.get(), nLen, pConDetails->lstDynTable, pConDetails->StreamParam, pConDetails->H2Streams, pConDetails->mutStreams.get(), pConDetails->TmpFile, pConDetails->atStop.get()), nRet != SIZE_MAX)
                    {
                        pConDetails->strBuffer.erase(0,  pConDetails->strBuffer.size() - nLen);
                        m_mtxConnections.unlock();
                        return;
                    }
                    // After a GOAWAY we terminate the connection
                    soMetaDa.fSocketClose();
                    m_mtxConnections.unlock();
                    return;
                }


                size_t nPosEndOfHeader =  pConDetails->strBuffer.find("\r\n\r\n");
                if (nPosEndOfHeader != string::npos)
                {
auto dwStart = chrono::high_resolution_clock::now();
                    // If we get here we should have a HTTP request in strPuffer
                    size_t nPos =  pConDetails->strBuffer.find(' ');
                    if (nPos != string::npos)
                    {
                        /*auto parResult = */pConDetails->HeaderList.insert(make_pair(string(":method"),  pConDetails->strBuffer.substr(0, nPos)));
                        pConDetails->strBuffer.erase(0, nPos + 1);
                    }
                    nPos = pConDetails->strBuffer.find(' ');
                    if (nPos != string::npos)
                    {
                        /*auto parResult = */pConDetails->HeaderList.insert(make_pair(string(":path"),  pConDetails->strBuffer.substr(0, nPos)));
                        pConDetails->strBuffer.erase(0, nPos + 1);
                    }
                    nPos = pConDetails->strBuffer.find('\n');
                    if (nPos != string::npos)
                    {
                        auto parResult = pConDetails->HeaderList.insert(make_pair(string(":version"),  pConDetails->strBuffer.substr(0, nPos)));
                        if (parResult != end(pConDetails->HeaderList))
                        {
                            while (parResult->second.find('\r') != string::npos) parResult->second.replace(parResult->second.find('\r'), 1, "");
                            transform(begin(parResult->second), end(parResult->second), begin(parResult->second), ::toupper);
                            if (parResult->second.find("HTTP/1.") != string::npos)
                                parResult->second.replace(parResult->second.find("HTTP/1."), 7, "");
                        }
                        pConDetails->strBuffer.erase(0, nPos + 1);
                    }

                    while ((nPos =  pConDetails->strBuffer.find('\n')) != string::npos && nPos > 1)
                    {
                        size_t nPos1 = pConDetails->strBuffer.find(':');
                        if (nPos1 != string::npos)
                        {
                            auto strTmp = pConDetails->strBuffer.substr(0, nPos1);
                            transform(begin(strTmp), end(strTmp), begin(strTmp), ::tolower);

                            auto parResult = pConDetails->HeaderList.insert(make_pair(strTmp,  pConDetails->strBuffer.substr(nPos1 + 1, nPos - (nPos1 + 1))));
                            if (parResult != end(pConDetails->HeaderList))
                            {
                                while (parResult->second.at(0) == ' ') parResult->second.replace(parResult->second.find(' '), 1, "");
                                while (parResult->second.find('\r') != string::npos) parResult->second.replace(parResult->second.find('\r'), 1, "");
                                while (parResult->second.find('\n') != string::npos) parResult->second.replace(parResult->second.find('\n'), 1, "");
                            }
                        }
                        pConDetails->strBuffer.erase(0, nPos + 1);
                    }

                    if (nPos != string::npos)
                    {
                        pConDetails->strBuffer.erase(0, nPos + 1);
                    }

                    auto contentLength = pConDetails->HeaderList.find("content-length");
                    if (contentLength != end(pConDetails->HeaderList))
                    {
                        //stringstream ssTmp(contentLength->second);
                        //ssTmp >> pConDetails->nContentsSoll;
                        pConDetails->nContentsSoll = stoll(contentLength->second);

                        if (pConDetails->nContentsSoll > 0)
                        {
                            pConDetails->TmpFile = make_unique<TempFile>();
                            pConDetails->TmpFile->Open();

                            if ( pConDetails->strBuffer.size() > 0)
                            {
                                uint32_t nBytesToWrite = static_cast<uint32_t>(min(static_cast<uint64_t>(pConDetails->strBuffer.size()), pConDetails->nContentsSoll - pConDetails->nContentRecv));
                                pConDetails->TmpFile->Write( pConDetails->strBuffer.c_str(), nBytesToWrite);
                                pConDetails->nContentRecv += nBytesToWrite;
                                pConDetails->strBuffer.erase(0, nBytesToWrite);
                            }

                            if (pConDetails->nContentRecv < pConDetails->nContentsSoll)
                            {
                                m_mtxConnections.unlock();
                                return;
                            }
                            pConDetails->TmpFile->Close();
                        }
                    }
auto dwDif = chrono::high_resolution_clock::now();
MyTrace("Time in ms for Header parsing ", (chrono::duration<float, chrono::milliseconds::period>(dwDif - dwStart).count()));
                }
                else if (pConDetails->nContentsSoll == 0) // Noch kein End of Header, und kein Content empfangen
                {
                    m_mtxConnections.unlock();
                    return;
                }




                uint32_t nStreamId = 0;
                auto upgradeHeader = pConDetails->HeaderList.find("upgrade");
                if (upgradeHeader != end(pConDetails->HeaderList) && upgradeHeader->second.compare("h2c") == 0)
                {
                    auto http2SettingsHeader = pConDetails->HeaderList.find("http2-settings");
                    if (http2SettingsHeader != end(pConDetails->HeaderList))
                    {
                        string strHttp2Settings = Base64::Decode(http2SettingsHeader->second, true);
                        size_t nHeaderLen = strHttp2Settings.size();
                        auto upTmpBuffer = make_unique<char[]>(nHeaderLen);
                        copy(begin(strHttp2Settings), begin(strHttp2Settings) + nHeaderLen, upTmpBuffer.get());

                        MetaSocketData soMetaDa({ pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, pConDetails->pTimer) });

                        size_t nRet;
                        if (nRet = Http2StreamProto(soMetaDa, upTmpBuffer.get(), nHeaderLen, pConDetails->lstDynTable, pConDetails->StreamParam, pConDetails->H2Streams, pConDetails->mutStreams.get(), pConDetails->TmpFile, pConDetails->atStop.get()), nRet == 0)
                        {
                            pTcpSocket->Write("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: h2c\r\n\r\n", 71);
                            pTcpSocket->Write("\x0\x0\x0\x4\x0\x0\x0\x0\x0", 9);    // empty SETTINGS frame (4)
                            nStreamId = 1;
                        }
                    }
                }

                if (pConDetails->bIsH2Con == false)  // If we received a GOAWAY Frame in HTTP/2 we end up here, but we will not do any action
                {
                    MetaSocketData soMetaDa({ pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, pConDetails->pTimer) });

                    pConDetails->mutStreams->lock();
                    pConDetails->H2Streams.insert(make_pair(nStreamId, STREAMITEM(0, deque<DATAITEM>(), move(pConDetails->HeaderList), 0, 0, make_shared<atomic<int32_t>>(INITWINDOWSIZE(pConDetails->StreamParam)))));
                    pConDetails->mutStreams->unlock();
                    DoAction(soMetaDa, nStreamId, HEADERWRAPPER2{ pConDetails->H2Streams }, pConDetails->StreamParam, pConDetails->mutStreams.get(), move(pConDetails->TmpFile), bind(nStreamId != 0 ? &CHttpServ::BuildH2ResponsHeader : &CHttpServ::BuildResponsHeader, this, _1, _2, _3, _4, _5, _6), pConDetails->atStop.get());
                    if (nStreamId != 0)
                    {
                        pConDetails->bIsH2Con = true;
                    }

                    pConDetails->nContentRecv = pConDetails->nContentsSoll = 0;
                    lock_guard<mutex> log(*pConDetails->mutStreams.get());
                    pConDetails->H2Streams.clear();
                    pConDetails->HeaderList.clear();
                }

                m_mtxConnections.unlock();
                return;
            }
            m_mtxConnections.unlock();
        }
    }

    void OnSocketError(BaseSocket* pBaseSocket)
    {
        pBaseSocket->Close();
    }

    void OnSocketCloseing(BaseSocket* pBaseSocket)
    {
        //OutputDebugString(L"CHttpServ::OnSocketCloseing\r\n");
        lock_guard<mutex> lock(m_mtxConnections);
        CONNECTIONLIST::iterator item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            m_ActThrMutex.lock();
            for (unordered_multimap<thread::id, atomic<bool>*>::iterator iter = begin(m_umActionThreads); iter != end(m_umActionThreads);)
            {
                if (iter->second == item->second.atStop.get())
                {
                    m_ActThrMutex.unlock();
                    this_thread::sleep_for(milliseconds(1));
                    m_ActThrMutex.lock();
                    iter = begin(m_umActionThreads);
                    continue;
                }
                ++iter;
            }
            m_ActThrMutex.unlock();

            m_vConnections.erase(item);
        }
    }

    void OnTimeout(Timer* pTimer)
    {
        lock_guard<mutex> lock(m_mtxConnections);
        for (CONNECTIONLIST::iterator it = begin(m_vConnections); it != end(m_vConnections); ++it)
        {
            if (it->second.pTimer.get() == pTimer)
            {
                it->first->Close();
                break;
            }
        }
    }

    size_t BuildH2ResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, HEADERWRAPPER hw, uint64_t nContentSize = 0)
    {
        size_t nHeaderSize = 0;
        size_t nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, ":status", to_string(iRespCode).c_str());
        if (nReturn == SIZE_MAX)
            return 0;
        nHeaderSize += nReturn;

        auto in_time_t = chrono::system_clock::to_time_t(chrono::system_clock::now());
        struct tm* stTime = ::gmtime(&in_time_t);

        stringstream strTemp;
        strTemp.imbue(m_cLocal);
        char pattern[] = "%a, %d %b %Y %H:%M:%S GMT";
        use_facet <time_put <char> >(m_cLocal).put(strTemp.rdbuf(), strTemp, ' ', stTime, pattern, pattern + strlen(pattern));

        nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "date", strTemp.str().c_str());
        if (nReturn == SIZE_MAX)
            return 0;
        nHeaderSize += nReturn;

        nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "server", "Http2-Util");
        if (nReturn == SIZE_MAX)
            return 0;
        nHeaderSize += nReturn;

        if (nContentSize != 0 || iFlag & ADDCONENTLENGTH)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "content-length", to_string(nContentSize).c_str());
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }

        if ((iRespCode / 100) == 2 || (iRespCode / 100) == 3)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "last-modified", strTemp.str().c_str());
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }

        for (const auto& item : hw.umHeaderList)
        {
            string strHeaderFiled(item.first);
            transform(begin(strHeaderFiled), end(strHeaderFiled), begin(strHeaderFiled), ::tolower);
            if (strHeaderFiled.compare("pragma") == 0 || strHeaderFiled.compare("cache-control") == 0)
                iFlag &= ~ADDNOCACHE;
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, strHeaderFiled.c_str(), item.second.c_str());
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }

        if (iFlag & ADDNOCACHE)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "cache-control", "no-cache, no-store, must-revalidate, pre-check = 0, post-check = 0");
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;

            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "pragma", "no-cache");
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;

            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "expires", "Mon, 03 Apr 1961 05:00:00 GMT");
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }

        if (iFlag & GZIPENCODING)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "content-encoding", "gzip");
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }

        if (iFlag & HSTSHEADER)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "strict-transport-security", "max-age = 631138519; includeSubdomains; preload");
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }

        return nHeaderSize;
    }

    size_t BuildResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, HEADERWRAPPER hw, uint64_t nContentSize = 0)
    {
        string strRespons;
        strRespons.reserve(2048);
        //strRespons.imbue(m_cLocal);
        strRespons += "HTTP/1.";
        strRespons += ((iFlag & HTTPVERSION11) == HTTPVERSION11 ? "1 " : "0 ") + to_string(iRespCode) + " ";

        switch (iRespCode)
        {
        case 100: strRespons += "Continue"; break;
        case 101: strRespons += "Switching Protocols"; break;
        case 200: strRespons += "OK"; break;
        case 201: strRespons += "Created"; break;
        case 202: strRespons += "Accepted"; break;
        case 203: strRespons += "Non-Authoritative Information"; break;
        case 204: strRespons += "No Content"; break;
        case 205: strRespons += "Reset Content"; break;
        case 206: strRespons += "Partial Content"; break;
        case 300: strRespons += "Multiple Choices"; break;
        case 301: strRespons += "Moved Permanently"; break;
        case 302: strRespons += "Moved Temporarily"; break;
        case 303: strRespons += "See Other"; break;
        case 304: strRespons += "Not Modified"; break;
        case 305: strRespons += "Use Proxy"; break;
        case 400: strRespons += "Bad Request"; break;
        case 401: strRespons += "Unauthorized"; break;
        case 402: strRespons += "Payment Required"; break;
        case 403: strRespons += "Forbidden"; break;
        case 404: strRespons += "Not Found"; break;
        case 405: strRespons += "Method Not Allowed"; break;
        case 406: strRespons += "Not Acceptable"; break;
        case 407: strRespons += "Proxy Authentication Required"; break;
        case 408: strRespons += "Request Timeout"; break;
        case 409: strRespons += "Conflict"; break;
        case 410: strRespons += "Gone"; break;
        case 411: strRespons += "Length Required"; break;
        case 412: strRespons += "Precondition Failed"; break;
        case 413: strRespons += "Request Entity Too Large"; break;
        case 414: strRespons += "Request-URI Too Long"; break;
        case 415: strRespons += "Unsupported Media Type"; break;
        case 500: strRespons += "Internal Server Error"; break;
        case 501: strRespons += "Not Implemented"; break;
        case 502: strRespons += "Bad Gateway"; break;
        case 503: strRespons += "Service Unavailable"; break;
        case 504: strRespons += "Gateway Timeout"; break;
        case 505: strRespons += "HTTP Version Not Supported"; break;
        }

        strRespons += "\r\n";
        strRespons += "Server: Http2-Utility\r\n";

        strRespons += "Date: ";
        auto in_time_t = chrono::system_clock::to_time_t(chrono::system_clock::now());
        struct tm* stTime = ::gmtime(&in_time_t);

        char pattern[] = "%a, %d %b %Y %H:%M:%S GMT\r\n";
        //use_facet <time_put <char> >(m_cLocal).put(strRespons.rdbuf(), strRespons, ' ', stTime, pattern, pattern + strlen(pattern));
        stringstream ss;
        ss.imbue(m_cLocal);
        ss << put_time(stTime, pattern);
        strRespons += ss.str();

        if (nContentSize != 0 || iFlag & ADDCONENTLENGTH)
            strRespons += "Content-Length: " + to_string(nContentSize) + "\r\n";

        for (const auto& item : hw.umHeaderList)
        {
            if (item.first.compare("Pragma") == 0 || item.first.compare("Cache-Control") == 0)
                iFlag &= ~ADDNOCACHE;
            strRespons += item.first + ": " + item.second + "\r\n";
        }

        if (iFlag & ADDNOCACHE)
            strRespons += "Pragma: no-cache\r\nCache-Control: no-cache\r\nExpires: Mon, 03 Apr 1961 05:00:00 GMT\r\n";

        if (iFlag & ADDCONNECTIONCLOSE)
            strRespons += "Connection: close\r\n";
        else
            strRespons += "Connection: keep-alive\r\n";

        if (iFlag & GZIPENCODING)
            strRespons += "Content-Encoding: gzip\r\n";

        if (iFlag & HSTSHEADER)
            strRespons += "Strict-Transport-Security: max-age = 631138519; includeSubdomains; preload\r\n";

        if (iFlag & TERMINATEHEADER)
            strRespons += "\r\n";

        size_t nRet = strRespons.size();
        if (nRet > nBufLen)
            return 0;

        copy(begin(strRespons), begin(strRespons) + nRet, szBuffer);

        return nRet;
    }

    void DoAction(MetaSocketData soMetaDa, uint32_t nStreamId, HEADERWRAPPER2 hw2, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile> pTmpFile, function<size_t(char*, size_t, int, int, HEADERWRAPPER, uint64_t)> BuildRespHeader, atomic<bool>* patStop)
    {
        static array<size_t, 4>arMethoden = { hash<string>()("GET"), hash<string>()("HEAD"), hash<string>()("POST"), hash<string>()("OPTIONS") /*, hash<string>()("PUT"), hash<string>()("PATCH"), hash<string>()("DELETE"),*/ };

        if (patStop != nullptr)
        {
            lock_guard<mutex> lock(m_ActThrMutex);
            m_umActionThreads.insert(make_pair(this_thread::get_id(), patStop));
        }

        auto fuExitDoAction = [&]()
        {
            lock_guard<mutex> lockg1(*pmtxStream);
            auto StreamItem = hw2.StreamList.find(nStreamId);
            if (StreamItem != end(hw2.StreamList))
                hw2.StreamList.erase(StreamItem);

            if (patStop != nullptr)
            {
                lock_guard<mutex> lockg2(m_ActThrMutex);
                m_umActionThreads.erase(this_thread::get_id());
            }
        };

        const size_t nHttp2Offset = nStreamId != 0 ? 9 : 0;
        const uint32_t nSizeSendBuf = MAXFRAMESIZE(tuStreamSettings);// 0x4000;
        char caBuffer[4096];
        int iHeaderFlag = 0;

        pmtxStream->lock();
        HEADERLIST& lstHeaderFields = GETHEADERLIST(hw2.StreamList.find(nStreamId)->second);
        pmtxStream->unlock();

        const wchar_t* szHost = L"";
        wstring strHost;
        auto host = lstHeaderFields.find("host");
        if (host == end(lstHeaderFields))
            host = lstHeaderFields.find(":authority");
        if (host != end(lstHeaderFields))
        {
            strHost = wstring(begin(host->second), end(host->second)).c_str();
            if (m_vHostParam.find(strHost.c_str()) != end(m_vHostParam))
                szHost = strHost.c_str();
        }

        auto version = lstHeaderFields.find(":version");
        if (version != end(lstHeaderFields) && version->second.compare("1") == 0)
            iHeaderFlag = HTTPVERSION11;

        auto connection = lstHeaderFields.find("connection");
        if (connection != end(lstHeaderFields) && connection->second.compare("close") == 0)
            iHeaderFlag |= ADDCONNECTIONCLOSE;

        uint32_t nCloseConnection = ((iHeaderFlag & HTTPVERSION11) == 0 || (iHeaderFlag & ADDCONNECTIONCLOSE) == ADDCONNECTIONCLOSE) && nStreamId == 0 ? 1 : 0; // if HTTP/1.0 and not HTTP/2.0 close connection after data is send
        if (nCloseConnection != 0)
            iHeaderFlag |= ADDCONNECTIONCLOSE;

        if (soMetaDa.bIsSsl == true)
            iHeaderFlag |= HSTSHEADER;

        auto itMethode = lstHeaderFields.find(":method");
        auto itPath = lstHeaderFields.find(":path");
        if (itMethode == end(lstHeaderFields) || itPath == end(lstHeaderFields))
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 400, HEADERWRAPPER{ HEADERLIST() }, 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();
            soMetaDa.fSocketClose();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "]  Method or Path is not defined in the request");

            fuExitDoAction();
            return;
        }

        transform(begin(itMethode->second), end(itMethode->second), begin(itMethode->second), ::toupper);

        size_t nMethode = 0, nHashMethode = hash<string>()(itMethode->second);
        for (const auto& item : arMethoden)
        {
            if (item == nHashMethode)
                break;
            ++nMethode;
        }

        if (nMethode >= arMethoden.size())
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER/* | ADDCONNECTIONCLOSE*/, 405, HEADERWRAPPER{ HEADERLIST() }, 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "]  Method Not Allowed: ", itPath->second);

            fuExitDoAction();
            return;
        }

        // Decode URL (%20 -> ' ')
        wstring strItemPath;
        size_t nLen = itPath->second.size();
        wchar_t wch = 0;
        int nAnzParts = 0;
        for (size_t n = 0; n < nLen; n++)
        {
            int chr = itPath->second.at(n++);
            if ('%' == chr)
            {
                stringstream ss({ itPath->second.at(n++), itPath->second.at(n) });
                ss >> hex >> chr;
                if (chr < 0x7f)
                    strItemPath += static_cast<wchar_t>(chr);
                else if (chr >= 0x80 && chr <= 0xBF)
                {
                    chr &= 0x3F;
                    if (nAnzParts-- == 2)
                        chr = chr << 6;
                    wch |= chr;
                    if (nAnzParts == 0)
                        strItemPath += wch;
                }
                else if (chr > 0xC0 && chr <= 0xDF)
                {
                    wch = (chr & 0x1F) << 6;
                    nAnzParts = 1;
                }
                else if (chr > 0xE0 && chr <= 0xEF)
                {
                    wch = (chr & 0xF) << 12;
                    nAnzParts = 2;
                }
            }
            else
                strItemPath += static_cast<wchar_t>(itPath->second.at(--n));
        }

        wstring strQuery;
        string::size_type nPos;
        if (string::npos != (nPos = strItemPath.find(L'?')))
        {
            strQuery = strItemPath.substr(nPos + 1);
            strItemPath.erase(nPos);
        }

        for (auto& strRule : m_vHostParam[szHost].m_vstrRewriteRule)    // RewriteRule
        {
            nPos = strRule.find(L" ");
            wregex rx(strRule.substr(0, nPos));
            nPos += strRule.substr(nPos + 1).find_first_not_of(L" \t");
            strItemPath = regex_replace(strItemPath, rx, strRule.substr(nPos + 1), regex_constants::format_first_only);
        }

        // Test for forbidden element /.. /aux /lpt /com .htaccess .htpasswd .htgroup
        if (regex_search(strItemPath.c_str(), s_rxForbidden) == true)
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER/* | ADDCONNECTIONCLOSE*/, 403, HEADERWRAPPER{ HEADERLIST() }, 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - " << itMethode->second << " \"" << lstHeaderFields.find(":path")->second << "\" 403 - \"" << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \"" << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\"" << CLogFile::LOGTYPES::END;

            fuExitDoAction();
            return;
        }

        auto itAuth = lstHeaderFields.find("authorization");
        if (itAuth != end(lstHeaderFields))
        {
            nPos = itAuth->second.find(' ');
            if (nPos != string::npos)
            {
                string strCredenial = Base64::Decode(itAuth->second.substr(nPos + 1));
                //string strBase64 = Base64::Encode(strCredenial.c_str(), strCredenial.size());
                //if (strBase64.compare(itAuth->second.substr(nPos + 1)) == 0)
                //    MessageBeep(-1);
                //if (strCredenial.compare("Thomas:lanier") == 0)
                //    MessageBeep(-1);
            }
        }
        //else
        //{
        //    size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 401, HEADERWRAPPER{{{"WWW-Authenticate", "Basic realm=\"Http-Uttility Basic\""}}}, 0);
        //    if (nStreamId != 0)
        //        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
        //    m_cSockSystem.soMetaDa.fSocketWrite(stSocketData.iId, caBuffer, nHeaderLen + nHttp2Offset, CONNFLAGS::CLOSE);
        //    soMetaDa.fResetTimer();
        //    return;
        //}

        if (nMethode == 3)  // OPTION
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER/* | ADDCONNECTIONCLOSE*/ | ADDCONENTLENGTH, 200, HEADERWRAPPER{ { {"Allow", "GET, HEAD, POST"} } }, 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << (version != end(lstHeaderFields) ? version->second : "0")
                << "\" 200 -" << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;

            fuExitDoAction();
            return;
        }

        bool bNewRootSet = false;
        for (auto& strAlias : m_vHostParam[szHost].m_vstrAliasMatch)    // AliasMatch
        {
            nPos = strAlias.find(L" ");
            if (nPos != string::npos)
            {
                wregex rx(strAlias.substr(0, nPos));
                nPos += strAlias.substr(nPos + 1).find_first_not_of(L" \t");
                wstring strNewPath = regex_replace(strItemPath, rx, strAlias.substr(nPos + 1), regex_constants::format_first_only);
                if (strNewPath.compare(strItemPath) != 0)
                {
                    strItemPath = strNewPath;
                    bNewRootSet = true;
                    break;
                }
            }
        }

        // Get base directory and build filename
        if (bNewRootSet == false)
            strItemPath = m_vHostParam[szHost].m_strRootPath + strItemPath;

        // replace Slash in Backslash (Windows can handle them too)
        replace(begin(strItemPath), end(strItemPath), L'\\', L'/');

        // supplement default item
        if (strItemPath.substr(strItemPath.size() - 1).compare(L"/") == 0 && m_vHostParam[szHost].m_vstrDefaultItem.empty() == false)
        {
            for (auto& strDefItem : m_vHostParam[szHost].m_vstrDefaultItem)
            {
                if (_waccess(FN_CA((strItemPath + strDefItem)), 0) == 0)
                {
                    strItemPath += strDefItem;
                    itPath->second += string(strDefItem.begin(), strDefItem.end());
                    break;
                }
            }
        }

        struct _stat64 stFileInfo;
        if (::_wstat64(FN_CA(strItemPath), &stFileInfo) != 0)
        {
            if (errno == ENOENT)
            {
                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER/* | ADDCONNECTIONCLOSE*/, 404, HEADERWRAPPER{ HEADERLIST() }, 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                soMetaDa.fResetTimer();

                CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                    << itMethode->second << " " << lstHeaderFields.find(":path")->second
                    << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << (version != end(lstHeaderFields) ? version->second : "0")
                    << "\" 404 " << "-" << " \""
                    << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                    << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                    << CLogFile::LOGTYPES::END;

                CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] File does not exist: ", itPath->second);
            }
            else //EINVAL
            {
                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, HEADERWRAPPER{ HEADERLIST() }, 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                soMetaDa.fResetTimer();
                soMetaDa.fSocketClose();

                CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                    << itMethode->second << " " << lstHeaderFields.find(":path")->second
                    << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << (version != end(lstHeaderFields) ? version->second : "0")
                    << "\" 500 " << "-" << " \""
                    << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                    << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                    << CLogFile::LOGTYPES::END;

                CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] internal server error: ", itPath->second);
            }

            fuExitDoAction();
            return;
        }

        // Get file type of item
        wstring strFileExtension(strItemPath.substr(strItemPath.rfind(L'.') + 1));
        transform(begin(strFileExtension), end(strFileExtension), begin(strFileExtension), ::tolower);

        for (const auto& strFileTyp : m_vHostParam[szHost].m_mFileTypeAction)
        {
            if (strFileExtension.compare(strFileTyp.first) == 0)
            {
                if (strFileTyp.second.empty() == false)
                {
                    wstringstream ss;
                    ss.imbue(m_cLocal);
                    const array<pair<const char*, const wchar_t*>, 13> caHeaders = { { make_pair(":authority", L"HTTP_HOST") , make_pair("host", L"HTTP_HOST") , make_pair("cookie", L"HTTP_COOKIE") , make_pair("referer", L"HTTP_REFERER") , make_pair("user-agent", L"HTTP_USER_AGENT") , make_pair(":scheme", L"REQUEST_SCHEME") , make_pair("accept", L"HTTP_ACCEP") , make_pair("accept-encoding", L"HTTP_ACCEPT_ENCODING") , make_pair("dnt", L"HTTP_DNT") , make_pair("accept-language", L"HTTP_ACCEPT_LANGUAGE") , make_pair("upgrade-insecure-requests", L"HTTP_UPGRADE_INSECURE_REQUESTS"), make_pair("content-type", L"CONTENT_TYPE"), make_pair("content-length", L"CONTENT_LENGTH") } };
                    for (const auto itH : caHeaders)
                    {
                        auto itHeader = lstHeaderFields.find(itH.first);
                        if (itHeader != end(lstHeaderFields))
                            ss << ENV << itH.second << "=" << QUOTES << FIXENVSTR(itHeader->second).c_str() << QUOTES << ENVJOIN;
                    }

                    ss << ENV << "SERVER_SOFTWARE=Http2Util/0.1" << ENVJOIN << ENV << L"REDIRECT_STATUS=200" << ENVJOIN << ENV << L"REMOTE_ADDR=" << soMetaDa.strIpClient.c_str() << ENVJOIN << ENV << L"SERVER_PORT=" << soMetaDa.sPortInterFace;
                    ss << ENVJOIN << ENV << L"SERVER_ADDR=" << soMetaDa.strIpInterface.c_str() << ENVJOIN << ENV << L"REMOTE_PORT=" << soMetaDa.sPortClient;
                    ss << ENVJOIN << ENV << L"SERVER_PROTOCOL=" << (nStreamId != 0 ? L"HTTP/2." : L"HTTP/1.") << (version != end(lstHeaderFields) ? version->second : string("0")).c_str();
                    ss << ENVJOIN << ENV << L"DOCUMENT_ROOT=" << QUOTES << m_vHostParam[szHost].m_strRootPath << QUOTES << ENVJOIN << ENV << L"GATEWAY_INTERFACE=CGI/1.1" << ENVJOIN << ENV << L"SCRIPT_NAME=" << QUOTES << itPath->second.substr(0, itPath->second.find("?")).c_str() << QUOTES;
                    ss << ENVJOIN << ENV << L"REQUEST_METHOD=" << itMethode->second.c_str() << ENVJOIN << ENV << L"REQUEST_URI=" << QUOTES << FIXENVSTR(itPath->second).c_str() << QUOTES;
                    if (soMetaDa.bIsSsl == true)
                        ss << ENVJOIN << ENV << L"HTTPS=on";
                    if (strQuery.empty() == false)
                        ss << ENVJOIN << ENV << L"QUERY_STRING=" << QUOTES << WFIXENVSTR(strQuery) << QUOTES;
                    ss << ENVJOIN << ENV << L"SCRIPT_FILENAME=" << QUOTES << strItemPath << QUOTES << ENVJOIN << strFileTyp.second;

                    if (pTmpFile != 0 && pTmpFile.get()->GetFileName().empty() == false)
                        ss << " < " << pTmpFile.get()->GetFileName().c_str();

                    FILE* hPipe = _wpopen(FN_CA(ss.str()), PIPETYPE);
                    if (hPipe != nullptr)
                    {
                        bool bEndOfHeader = false;
                        HEADERLIST umPhpHeaders;
                        char psBuffer[4096];
                        basic_string<char> strBuffer;
                        size_t nTotal = 0, nRead = 0, nOffset = 0;
                        while (feof(hPipe) == 0 && (*patStop).load() == false)
                        {
                            nRead = fread(psBuffer + nHttp2Offset + nOffset, 1, static_cast<int>(4096 - nHttp2Offset - nOffset), hPipe);
                            if (nRead > 0 && ferror(hPipe) == 0)
                            {
                                nOffset = 0;

                                if (bEndOfHeader == false)
                                {
                                    strBuffer.assign(psBuffer + nHttp2Offset, nRead + nOffset);
                                    for (;;)
                                    {
                                        size_t nPosStart = strBuffer.find_first_of("\r\n");
                                        size_t nPosEnd = strBuffer.find_first_not_of("\r\n", nPosStart);
                                        if (nPosEnd == string::npos)
                                            nPosEnd = nRead;

                                        if (nPosStart == 0)
                                        {
                                            int iStatus = 200;
                                            // Build response header
                                            auto status = umPhpHeaders.find("Status");
                                            if (status != end(umPhpHeaders))
                                            {
                                                iStatus = stoi(status->second);
                                                umPhpHeaders.erase(status);
                                            }
                                            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER, iStatus, HEADERWRAPPER{ umPhpHeaders }, 0);
                                            if (nStreamId != 0)
                                                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                                            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                                            soMetaDa.fResetTimer();

                                            bEndOfHeader = true;
                                            nRead -= nPosEnd;
                                            strBuffer.erase(0,  nPosEnd);
                                            strBuffer.copy(psBuffer + nHttp2Offset, nRead);
                                            break;
                                        }
                                        else if (nPosStart != string::npos)
                                        {
                                            size_t nPos2 = strBuffer.substr(0, nPosStart).find(":");
                                            if (nPos2 != string::npos)
                                            {
                                                auto iter = umPhpHeaders.insert(make_pair(strBuffer.substr(0, nPos2), strBuffer.substr(nPos2 + 1, nPosStart - (nPos2 + 1))));
                                                if (iter != end(umPhpHeaders))
                                                    while (iter->second.find(' ') == 0) iter->second.erase(0, 1);
                                            }

                                            if (nPosEnd - nPosStart > 2 && strBuffer.substr(nPosStart, 2).compare("\r\n") == 0)
                                                nPosEnd -= 2;
                                            else if (nPosEnd - nPosStart > 1 && strBuffer.substr(nPosStart, 2).compare("\n\n") == 0)
                                                nPosEnd -= 1;

                                            nRead -= nPosEnd;
                                            strBuffer.erase(0,  nPosEnd);
                                        }
                                        else
                                        {
                                            nOffset = nRead;
                                            strBuffer.copy(psBuffer + nHttp2Offset, nRead);
                                            nRead = 0;
                                            break;
                                        }
                                    }

                                    if (nRead == 0)
                                        continue;
                                }

                                if (nStreamId != 0)
                                    BuildHttp2Frame(psBuffer, nRead, 0x0, 0x0, nStreamId);
                                soMetaDa.fSocketWrite(psBuffer, nRead + nHttp2Offset);
                                soMetaDa.fResetTimer();
                                nTotal += nRead;

                                // Check if we can send more Data in HTTP/2 - Window-sizes large enough for the next chunk
                                if (nStreamId != 0)
                                {
                                    (*pmtxStream).lock();
                                    for (;;)
                                    {
                                        uint32_t nStreamWndSize = UINT32_MAX;
                                        uint32_t nTotaleWndSize = UINT32_MAX;
                                        auto StreamItem = hw2.StreamList.find(nStreamId);
                                        if (StreamItem != end(hw2.StreamList))
                                        {
                                            (*WINDOWSIZE(StreamItem->second).get()) -= nRead;
                                            nStreamWndSize = (*WINDOWSIZE(StreamItem->second).get());
                                        }
                                        else
                                            break;  // Stream Item was removed, properly the stream was reseted
                                        StreamItem = hw2.StreamList.find(0);
                                        if (StreamItem != end(hw2.StreamList))
                                        {
                                            (*WINDOWSIZE(StreamItem->second).get()) -= nRead;
                                            nTotaleWndSize = (*WINDOWSIZE(StreamItem->second).get());
                                        }
                                        else
                                            break;  // Stream Item was removed, properly the stream was reseted

                                        nRead = 0;

                                        if (nStreamWndSize < 4096 || nTotaleWndSize < 4096)
                                        {
                                            (*pmtxStream).unlock();
                                            this_thread::sleep_for(chrono::milliseconds(1));
                                            (*pmtxStream).lock();
                                            continue;
                                        }
                                        else
                                            break;
                                    }
                                    (*pmtxStream).unlock();
                                }

                            }
                            else if (ferror(hPipe) != 0)
                            {
                                CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Failed to read the pipe: ", itPath->second);
                                break;
                            }
                            else
                                this_thread::sleep_for(chrono::milliseconds(1));
                        }

                        if (nTotal > 0)
                        {
                            if (nStreamId != 0)
                                BuildHttp2Frame(psBuffer, 0, 0x0, 0x1, nStreamId);
                            soMetaDa.fSocketWrite(psBuffer, nHttp2Offset);
                            soMetaDa.fResetTimer();
                            if (nStreamId == 0)
                                soMetaDa.fSocketClose();
                        }

                        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                            << itMethode->second << " " << lstHeaderFields.find(":path")->second
                            << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << (version != end(lstHeaderFields) ? version->second : "0")
                            << "\" 200 " << nTotal << " \""
                            << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                            << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                            << CLogFile::LOGTYPES::END;

                        /* Close pipe and print return value of pPipe. */
                        if (feof(hPipe))
                        {
                            auto PipeExit = _pclose(hPipe);
                            MyTrace("Process returned ", PipeExit, " mit errno ", errno);
                        }
                        else
                        {
                            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Failed to read the pipe to the end for: ", itPath->second);
                        }
                    }
                    else
                    {
                        size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, HEADERWRAPPER{ HEADERLIST() }, 0);
                        if (nStreamId != 0)
                            BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                        soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                        soMetaDa.fResetTimer();
                        soMetaDa.fSocketClose();

                        CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Server error handling: ", itPath->second);
                    }
                }
                else
                {
                    size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, HEADERWRAPPER{ HEADERLIST() }, 0);
                    if (nStreamId != 0)
                        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                    soMetaDa.fResetTimer();
                    soMetaDa.fSocketClose();

                    CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Server error handling: ", itPath->second);
                }

                fuExitDoAction();
                return;
            }
        }

        // Load file
        fstream fin(FN_CA(strItemPath), ios_base::in | ios_base::binary);
        if (fin.is_open() == true)
        {
            //fin.seekg(0, ios_base::end);
            uint64_t nFSize = stFileInfo.st_size; //static_cast<uint32_t>(fin.tellg());
            //fin.seekg(0, ios_base::beg);

            auto acceptencoding = lstHeaderFields.find("accept-encoding");
            if (acceptencoding != end(lstHeaderFields) && acceptencoding->second.find("gzip") != string::npos)
            {
                iHeaderFlag |= GZIPENCODING;

                // http://www.filesignatures.net/index.php?page=all
                if (wstring(L"zip|rar|7z|iso|png|jpg|gif|mp3|wma|avi|mpg|gz|tar").find(strFileExtension) != string::npos)
                    iHeaderFlag &= ~GZIPENCODING;
            }

            //iHeaderFlag &= ~GZIPENCODING;
            if (iHeaderFlag & GZIPENCODING)
            {
                GZipPack gzipEncoder;
                if (gzipEncoder.Init() == Z_OK)
                {
                    unique_ptr<TempFile> pDestFile = make_unique<TempFile>();
                    unique_ptr<unsigned char> srcBuf(new unsigned char[4096]);
                    unique_ptr<unsigned char> dstBuf(new unsigned char[4096]);

                    pDestFile->Open();

                    int iRet;
                    do
                    {
                        streamsize nBytesRead = fin.read(reinterpret_cast<char*>(srcBuf.get()), 4096).gcount();
                        if (nBytesRead == 0)
                            break;

                        gzipEncoder.InitBuffer(srcBuf.get(), static_cast<uint32_t>(nBytesRead));
                        int nFlush = fin.eof() ? Z_FINISH : Z_NO_FLUSH;//nBytesRead < 4096 ? Z_FINISH : Z_NO_FLUSH;

                        uint32_t nBytesConverted;
                        do
                        {
                            nBytesConverted = 4096;
                            iRet = gzipEncoder.Enflate(dstBuf.get(), &nBytesConverted, nFlush);

                            if ((iRet == Z_OK || iRet == Z_STREAM_END) && (4096 - nBytesConverted) != 0)
                                pDestFile->Write(reinterpret_cast<char*>(dstBuf.get()), (4096 - nBytesConverted));
                        } while (iRet == Z_OK && nBytesConverted == 0);
                    } while (iRet == Z_OK);

                    nFSize = static_cast<uint32_t>(pDestFile->GetFileLength());
                    pDestFile.get()->Rewind();
                    fin.swap((*pDestFile.get())());
                    pDestFile->Close();
                }
            }

            // MimeType
            string strMineType("application/octet-stream");
            auto it = find_if(begin(MimeListe), end(MimeListe), [strFileExtension](const MIMEENTRY & item) { return strFileExtension == MIMEEXTENSION(item); });
            if (it != end(MimeListe))
                strMineType = MIMESTRING(*it);

            // Build response header
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER, 200, HEADERWRAPPER{ { {"Content-Type", strMineType} } }, nFSize);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();

            if (nMethode == 0 || nMethode == 2) // GET or POST
            {
                auto apBuf = make_unique<char[]>(nSizeSendBuf + nHttp2Offset + 2);

                uint64_t nBytesTransfered = 0;
                while (nBytesTransfered < nFSize && (*patStop).load() == false)
                {
                    uint32_t nStreamWndSize = UINT32_MAX;
                    uint32_t nTotaleWndSize = UINT32_MAX;
                    if (nStreamId != 0)
                    {
                        lock_guard<mutex> lock0(*pmtxStream);
                        auto StreamItem = hw2.StreamList.find(nStreamId);
                        if (StreamItem != end(hw2.StreamList))
                            nStreamWndSize = (*WINDOWSIZE(StreamItem->second).get());
                        else
                            break;  // Stream Item was removed, properly the stream was reseted
                        StreamItem = hw2.StreamList.find(0);
                        if (StreamItem != end(hw2.StreamList))
                            nTotaleWndSize = (*WINDOWSIZE(StreamItem->second).get());
                        else
                            break;  // Stream Item was removed, properly the stream was reseted
                    }

                    size_t nInQue = soMetaDa.fSockGetOutBytesInQue();
                    if (/*nInQue != SIZE_MAX && */nInQue >= 0x200000 || nStreamWndSize < nSizeSendBuf || nTotaleWndSize < nSizeSendBuf)
                    {
                        this_thread::sleep_for(chrono::milliseconds(1));
                        continue;
                    }

                    uint32_t nSendBufLen = static_cast<uint32_t>(min(static_cast<uint64_t>(nSizeSendBuf), nFSize - nBytesTransfered));

                    nBytesTransfered += nSendBufLen;

                    fin.read(apBuf.get() + nHttp2Offset, nSendBufLen);
                    if (nStreamId != 0)
                        BuildHttp2Frame(apBuf.get(), nSendBufLen, 0x0, (nFSize - nBytesTransfered == 0 ? 0x1 : 0x0), nStreamId);
                    soMetaDa.fSocketWrite(apBuf.get(), nSendBufLen + nHttp2Offset);
                    soMetaDa.fResetTimer();
                    if (nFSize - nBytesTransfered == 0 && nCloseConnection != 0)
                        soMetaDa.fSocketClose();

                    if (nStreamId != 0)
                    {
                        lock_guard<mutex> lock0(*pmtxStream);
                        auto StreamItem = hw2.StreamList.find(nStreamId);
                        if (StreamItem != end(hw2.StreamList))
                            (*WINDOWSIZE(StreamItem->second).get()) -= nSendBufLen;
                        else
                            break;  // Stream Item was removed, properly the stream was reseted
                        StreamItem = hw2.StreamList.find(0);
                        if (StreamItem != end(hw2.StreamList))
                            (*WINDOWSIZE(StreamItem->second).get()) -= nSendBufLen;
                        else
                            break;  // Stream Item was removed, properly the stream was reseted
                    }
                }
            }
            fin.close();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << (version != end(lstHeaderFields) ? version->second : "0")
                << "\" 200 " << nFSize << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;
        }
        else
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER/* | ADDCONNECTIONCLOSE*/, 404, HEADERWRAPPER{ HEADERLIST() }, 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << (version != end(lstHeaderFields) ? version->second : "0")
                << "\" 404 " << "-" << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;

            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] File does not exist: ", itPath->second);
        }

        fuExitDoAction();
    }

    virtual void EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop) override
    {
        thread(&CHttpServ::DoAction, this, soMetaDa, streamId, HEADERWRAPPER2{ ref(StreamList)}, ref(tuStreamSettings), pmtxStream, move(pTmpFile), bind(&CHttpServ::BuildH2ResponsHeader, this, _1, _2, _3, _4, _5, _6), patStop).detach();
    }

private:
    TcpServer*             m_pSocket;
    CONNECTIONLIST         m_vConnections;
    mutex                  m_mtxConnections;

    string                 m_strBindIp;
    short                  m_sPort;
    map<wstring, HOSTPARAM> m_vHostParam;
    locale                 m_cLocal;
    unordered_multimap<thread::id, atomic<bool>*> m_umActionThreads;
    mutex                  m_ActThrMutex;

    static const wregex    s_rxForbidden;
    const array<MIMEENTRY, 110>  MimeListe = { {
        MIMEENTRY(L"txt", "text/plain"),
        MIMEENTRY(L"rtx", "text/richtext"),
        MIMEENTRY(L"css", "text/css"),
        MIMEENTRY(L"xml", "text/xml"),
        MIMEENTRY(L"htm", "text/html"),
        MIMEENTRY(L"html", "text/html"),
        MIMEENTRY(L"shtm", "text/html"),
        MIMEENTRY(L"shtml", "text/html"),
        MIMEENTRY(L"rtf", "text/rtf"),
        MIMEENTRY(L"js", "application/javascript"),
        MIMEENTRY(L"tsv", "text/tab-separated-values"),
        MIMEENTRY(L"etx", "text/x-setext"),
        MIMEENTRY(L"sgm", "text/x-sgml"),
        MIMEENTRY(L"sgml", "text/x-sgml"),
        MIMEENTRY(L"xsl", "text/xsl"),
        MIMEENTRY(L"gif", "image/gif"),
        MIMEENTRY(L"jpeg", "image/jpeg"),
        MIMEENTRY(L"jpg", "image/jpeg"),
        MIMEENTRY(L"jpe", "image/jpeg"),
        MIMEENTRY(L"png", "image/png"),
        MIMEENTRY(L"tiff", "image/tiff"),
        MIMEENTRY(L"tif", "image/tiff"),
        MIMEENTRY(L"ras", "image/cmu-raster"),
        MIMEENTRY(L"wbmp", "image/vnd.wap.wbmp"),
        MIMEENTRY(L"fh4", "image/x-freehand"),
        MIMEENTRY(L"fh5", "image/x-freehand"),
        MIMEENTRY(L"fhc", "image/x-freehand"),
        MIMEENTRY(L"ico", "image/x-icon"),
        MIMEENTRY(L"ief", "image/ief"),
        MIMEENTRY(L"pnm", "image/x-portable-anymap"),
        MIMEENTRY(L"pbm", "image/x-portable-bitmap"),
        MIMEENTRY(L"pgm", "image/x-portable-graymap"),
        MIMEENTRY(L"ppm", "image/x-portable-pixmap"),
        MIMEENTRY(L"rgb", "image/x-rgb"),
        MIMEENTRY(L"xwd", "image/x-windowdump"),
        MIMEENTRY(L"exe", "application/octet-stream"),
        MIMEENTRY(L"com", "application/octet-stream"),
        MIMEENTRY(L"dll", "application/octet-stream"),
        MIMEENTRY(L"bin", "application/octet-stream"),
        MIMEENTRY(L"class", "application/octet-stream"),
        MIMEENTRY(L"iso", "application/octet-stream"),
        MIMEENTRY(L"zip", "application/zip"),
        MIMEENTRY(L"pdf", "application/pdf"),
        MIMEENTRY(L"ps", "application/postscript"),
        MIMEENTRY(L"ai", "application/postscript"),
        MIMEENTRY(L"eps", "application/postscript"),
        MIMEENTRY(L"pac", "application/x-ns-proxy-autoconfig"),
        MIMEENTRY(L"dwg", "application/acad"),
        MIMEENTRY(L"dxf", "application/dxf"),
        MIMEENTRY(L"mif", "application/mif"),
        MIMEENTRY(L"doc", "application/msword"),
        MIMEENTRY(L"dot", "application/msword"),
        MIMEENTRY(L"ppt", "application/mspowerpoint"),
        MIMEENTRY(L"ppz", "application/mspowerpoint"),
        MIMEENTRY(L"pps", "application/mspowerpoint"),
        MIMEENTRY(L"pot", "application/mspowerpoint"),
        MIMEENTRY(L"xls", "application/msexcel"),
        MIMEENTRY(L"xla", "application/msexcel"),
        MIMEENTRY(L"hlp", "application/mshelp"),
        MIMEENTRY(L"chm", "application/mshelp"),
        MIMEENTRY(L"sh", "application/x-sh"),
        MIMEENTRY(L"csh", "application/x-csh"),
        MIMEENTRY(L"latex", "application/x-latex"),
        MIMEENTRY(L"tar", "application/x-tar"),
        MIMEENTRY(L"bcpio", "application/x-bcpio"),
        MIMEENTRY(L"cpio", "application/x-cpio"),
        MIMEENTRY(L"sv4cpio", "application/x-sv4cpio"),
        MIMEENTRY(L"sv4crc", "application/x-sv4crc"),
        MIMEENTRY(L"hdf", "application/x-hdf"),
        MIMEENTRY(L"ustar", "application/x-ustar"),
        MIMEENTRY(L"shar", "application/x-shar"),
        MIMEENTRY(L"tcl", "application/x-tcl"),
        MIMEENTRY(L"dvi", "application/x-dvi"),
        MIMEENTRY(L"texinfo", "application/x-texinfo"),
        MIMEENTRY(L"texi", "application/x-texinfo"),
        MIMEENTRY(L"t", "application/x-troff"),
        MIMEENTRY(L"tr", "application/x-troff"),
        MIMEENTRY(L"roff", "application/x-troff"),
        MIMEENTRY(L"man", "application/x-troff-man"),
        MIMEENTRY(L"me", "application/x-troff-me"),
        MIMEENTRY(L"ms", "application/x-troff-ms"),
        MIMEENTRY(L"nc", "application/x-netcdf"),
        MIMEENTRY(L"cdf", "application/x-netcdf"),
        MIMEENTRY(L"src", "application/x-wais-source"),
        MIMEENTRY(L"au", "audio/basic"),
        MIMEENTRY(L"snd", "audio/basic"),
        MIMEENTRY(L"aif", "audio/x-aiff"),
        MIMEENTRY(L"aiff", "audio/x-aiff"),
        MIMEENTRY(L"aifc", "audio/x-aiff"),
        MIMEENTRY(L"dus", "audio/x-dspeeh"),
        MIMEENTRY(L"cht", "audio/x-dspeeh"),
        MIMEENTRY(L"midi", "audio/x-midi"),
        MIMEENTRY(L"mid", "audio/x-midi"),
        MIMEENTRY(L"ram", "audio/x-pn-realaudio"),
        MIMEENTRY(L"ra", "audio/x-pn-realaudio"),
        MIMEENTRY(L"rpm", "audio/x-pn-realaudio-plugin"),
        MIMEENTRY(L"mpeg", "video/mpeg"),
        MIMEENTRY(L"mpg", "video/mpeg"),
        MIMEENTRY(L"mpe", "video/mpeg"),
        MIMEENTRY(L"qt", "video/quicktime"),
        MIMEENTRY(L"mov", "video/quicktime"),
        MIMEENTRY(L"avi", "video/x-msvideo"),
        MIMEENTRY(L"movie", "video/x-sgi-movie"),
        MIMEENTRY(L"wrl", "x-world/x-vrml"),
        MIMEENTRY(L"jar", "application/x-jar"),
        MIMEENTRY(L"jnlp", "application/x-java-jnlp-file"),
        MIMEENTRY(L"jad", "text/vnd.sun.j2me.app-descriptor"),
        MIMEENTRY(L"wml", "text/vnd.wap.wml"),
        MIMEENTRY(L"wmlc", "application/vnd.wap.wmlc"),
        MIMEENTRY(L"wbmp", "image/vnd.wap.wbmp")
    } };
};

const wregex CHttpServ::s_rxForbidden(L"/\\.\\.|/aux$|/noel$|/prn$|/con$|/lpt[0-9]+$|/com[0-9]+$|\\.htaccess|\\.htpasswd|\\.htgroup", regex_constants::ECMAScript | regex_constants::icase);
