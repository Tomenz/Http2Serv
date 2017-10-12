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
#include <chrono>
#include <codecvt>

#include "socketlib/SslSocket.h"
#include "Timer.h"
#include "TempFile.h"
#include "H2Proto.h"
#include "LogFile.h"
#include "CommonLib/Base64.h"
#include "GZip.h"
#include <brotli/encode.h>
#include "CommonLib/md5.h"

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
#ifdef _WIN64
#pragma comment(lib, "x64/Debug/socketlib")
#pragma comment(lib, "x64/Debug/brotli")
#pragma comment(lib, "x64/Debug/CommonLib")
#else
#pragma comment(lib, "Debug/socketlib")
#pragma comment(lib, "Debug/brotli")
#pragma comment(lib, "Debug/CommonLib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "x64/Release/socketlib")
#pragma comment(lib, "x64/Release/brotli")
#pragma comment(lib, "x64/Release/CommonLib")
#else
#pragma comment(lib, "Release/socketlib")
#pragma comment(lib, "Release/brotli")
#pragma comment(lib, "Release/CommonLib")
#endif
#endif

#else
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#define _wpopen popen
#define _pclose pclose
#define _stat stat
#define _wstat stat
#define _stat64 stat64
#define _wstat64 stat64
#define _waccess access
#define _S_IFDIR S_IFDIR
#define _S_IFREG S_IFREG
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
    typedef struct
    {
        shared_ptr<Timer> pTimer;
        string strBuffer;
        bool bIsH2Con;
        uint64_t nContentsSoll;
        uint64_t nContentRecv;
        shared_ptr<TempFile> TmpFile;
        HeadList HeaderList;
        deque<HEADERENTRY> lstDynTable;
        shared_ptr<mutex> mutStreams;
        STREAMLIST H2Streams;
        STREAMSETTINGS StreamParam;
        shared_ptr<atomic_bool> atStop;
    } CONNECTIONDETAILS;

    typedef struct
    {
        STREAMLIST& StreamList;
    }HEADERWRAPPER2;

    typedef unordered_map<TcpSocket*, CONNECTIONDETAILS> CONNECTIONLIST;

    typedef tuple<const wchar_t*, const char*> MIMEENTRY;
    #define MIMEEXTENSION(x) get<0>(x)
    #define MIMESTRING(x) get<1>(x)

    enum HEADERFLAGS : uint32_t
    {
        TERMINATEHEADER = 1,
        ADDCONNECTIONCLOSE = 2,
        ADDNOCACHE = 4,
        HTTPVERSION11 = 8,
        ADDCONENTLENGTH = 16,
        GZIPENCODING = 32,
        DEFLATEENCODING = 64,
        HSTSHEADER = 128,
        BROTLICODING = 256,
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
        string  m_strDhParam;
        unordered_map<wstring, wstring> m_mstrRewriteRule;
        unordered_map<wstring, wstring> m_mstrAliasMatch;
        unordered_map<wstring, wstring> m_mstrForceTyp;
        unordered_map<wstring, wstring> m_mFileTypeAction;
        vector<tuple<wstring, wstring, wstring>> m_vRedirMatch;
        vector<tuple<wstring, wstring, wstring>> m_vEnvIf;
        vector<string> m_vDeflateTyps;
        unordered_map<wstring, vector<wstring>> m_mAuthenticate;
    } HOSTPARAM;

public:

    CHttpServ(wstring strRootPath = L".", short sPort = 80, bool bSSL = false) : m_pSocket(nullptr), m_sPort(sPort), m_cLocal(locale("C"))
    {
        HOSTPARAM hp;
        hp.m_strRootPath = strRootPath;
        //hp.m_strLogFile = L"access.log";
        //hp.m_strErrLog = L"error.log";
        hp.m_bSSL = bSSL;
        m_vHostParam.emplace(L"", hp);
    }

    virtual ~CHttpServ()
    {
        Stop();

        while (IsStopped() == false)
            this_thread::sleep_for(chrono::milliseconds(10));
    }

    bool Start()
    {
        if (m_vHostParam[L""].m_bSSL == true)
        {
            SslTcpServer* pSocket = new SslTcpServer();
            pSocket->AddCertificat(m_vHostParam[L""].m_strCAcertificate.c_str(), m_vHostParam[L""].m_strHostCertificate.c_str(), m_vHostParam[L""].m_strHostKey.c_str());
            pSocket->SetDHParameter(m_vHostParam[L""].m_strDhParam.c_str());
            pSocket->BindNewConnection(bind(&CHttpServ::OnNewConnection, this, _1));

            for (auto& Item : m_vHostParam)
            {
                if (Item.first != L"" && Item.second.m_bSSL == true)
                {
                    pSocket->AddCertificat(Item.second.m_strCAcertificate.c_str(), Item.second.m_strHostCertificate.c_str(), Item.second.m_strHostKey.c_str());
                    pSocket->SetDHParameter(Item.second.m_strDhParam.c_str());
                }
            }

            m_pSocket = pSocket;
        }
        else
        {
            m_pSocket = new TcpServer();
            m_pSocket->BindNewConnection(bind(&CHttpServ::OnNewConnection, this, _1));
        }
        m_pSocket->BindErrorFunction(bind(&CHttpServ::OnSocketError, this, _1));
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
        for (auto& item : m_vConnections)
        {
            item.second.pTimer->Stop();
            item.first->Close();
        }
        m_mtxConnections.unlock();

        return true;
    }

    bool IsStopped() noexcept
    {
        return m_vConnections.size() == 0 ? true : false;
    }

    HOSTPARAM& GetParameterBlockRef(const wchar_t* szHostName = nullptr)
    {
        if (szHostName != nullptr && m_vHostParam.find(szHostName) == end(m_vHostParam))
            m_vHostParam[szHostName] = m_vHostParam[L""];

        return m_vHostParam[szHostName == nullptr ? L"" : szHostName];
    }

    CHttpServ& SetBindAdresse(const char* szBindIp) noexcept
    {
        m_strBindIp = szBindIp;
        return *this;
    }

private:
    void OnNewConnection(const vector<TcpSocket*>& vNewConnections)
    {
        vector<TcpSocket*> vCache;
        for (auto& pSocket : vNewConnections)
        {
            if (pSocket != nullptr)
            {
                pSocket->BindFuncBytesRecived(bind(&CHttpServ::OnDataRecieved, this, _1));
                pSocket->BindErrorFunction(bind(&CHttpServ::OnSocketError, this, _1));
                pSocket->BindCloseFunction(bind(&CHttpServ::OnSocketCloseing, this, _1));
                vCache.push_back(pSocket);
            }
        }
        if (vCache.size())
        {
            m_mtxConnections.lock();
            for (auto& pSocket : vCache)
            {
                m_vConnections.emplace(pair<TcpSocket*, CONNECTIONDETAILS>(pSocket, { make_shared<Timer>(30000, bind(&CHttpServ::OnTimeout, this, _1)), string(), false, 0, 0, shared_ptr<TempFile>(), {}, {}, make_shared<mutex>(), {}, make_tuple(UINT32_MAX, 65535, 16384, UINT32_MAX, 4096), make_shared<atomic_bool>(false) }));
                pSocket->StartReceiving();
            }
            m_mtxConnections.unlock();
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
                pConDetails->strBuffer.append(spBuffer.get(), nRead);

                if (pConDetails->bIsH2Con == false)
                {
                    if ( pConDetails->strBuffer.size() >= 24 && pConDetails->strBuffer.compare(0, 24, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n") == 0 && pConDetails->nContentsSoll == 0)
                    {
                        pTcpSocket->Write("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x4\x0\x10\x0\x0\x0\x5\x0\x0\x40\x0", 21);// SETTINGS frame (4) with ParaID(4) and 1048576 Value + ParaID(5) and 16384 Value
                        pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\xf\x0\x1", 13);       // WINDOW_UPDATE frame (8) with value ?1048576? (minus 65535) == 983041
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
                        size_t nBytesToWrite = static_cast<size_t>(min(static_cast<uint64_t>(pConDetails->strBuffer.size()), pConDetails->nContentsSoll - pConDetails->nContentRecv));
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
                    // we wait, until all action thread's are finished, otherwise we remove the connection while the action thread is still using it = crash
                    m_ActThrMutex.lock();
                    for (unordered_multimap<thread::id, atomic<bool>*>::iterator iter = begin(m_umActionThreads); iter != end(m_umActionThreads);)
                    {
                        if (iter->second == pConDetails->atStop.get())
                        {
                            m_ActThrMutex.unlock();
                            this_thread::sleep_for(chrono::milliseconds(1));
                            m_ActThrMutex.lock();
                            iter = begin(m_umActionThreads);
                            continue;
                        }
                        ++iter;
                    }
                    m_ActThrMutex.unlock();
                    soMetaDa.fSocketClose();
                    m_mtxConnections.unlock();
                    return;
                }


                size_t nPosEndOfHeader =  pConDetails->strBuffer.find("\r\n\r\n");
                if (nPosEndOfHeader != string::npos)
                {
auto dwStart = chrono::high_resolution_clock::now();
                    // If we get here we should have a HTTP request in strPuffer

                    HeadList::iterator parLastHeader = end(pConDetails->HeaderList);
                    const static regex crlfSeperator("\r\n");
                    sregex_token_iterator line(begin(pConDetails->strBuffer), begin(pConDetails->strBuffer) + nPosEndOfHeader, crlfSeperator, -1);
                    while (line != sregex_token_iterator())
                    {
                        if (pConDetails->HeaderList.size() == 0)    // 1 Zeile
                        {
                            const string& strLine = line->str();
                            const static regex SpaceSeperator(" ");
                            sregex_token_iterator token(begin(strLine), end(strLine), SpaceSeperator, -1);
                            if (token != sregex_token_iterator())
                                pConDetails->HeaderList.emplace_back(make_pair(":method", token++->str()));
                            if (token != sregex_token_iterator())
                                pConDetails->HeaderList.emplace_back(make_pair(":path", token++->str()));
                            if (token != sregex_token_iterator())
                            {
                                auto parResult = pConDetails->HeaderList.emplace(pConDetails->HeaderList.end(), make_pair(":version", token++->str()));
                                if (parResult != end(pConDetails->HeaderList))
                                    parResult->second.erase(0, parResult->second.find_first_of('.') + 1);
                            }
                        }
                        else
                        {
                            size_t nPos1 = line->str().find(':');
                            if (nPos1 != string::npos)
                            {
                                string strTmp = line->str().substr(0, nPos1);
                                transform(begin(strTmp), begin(strTmp) + nPos1, begin(strTmp), ::tolower);

                                auto parResult = pConDetails->HeaderList.emplace(pConDetails->HeaderList.end(), make_pair(strTmp, line->str().substr(nPos1 + 1)));
                                if (parResult != end(pConDetails->HeaderList))
                                {
                                    parResult->second.erase(parResult->second.find_last_not_of(" \r\n\t")  + 1);
                                    parResult->second.erase(0, parResult->second.find_first_not_of(" \t"));
                                    parLastHeader = parResult;

                                }
                            }
                            else if (line->str().find(" \t") == 0 && parLastHeader != end(pConDetails->HeaderList)) // Multi line Header
                            {
                                line->str().erase(line->str().find_last_not_of(" \r\n\t") + 1);
                                line->str().erase(0, line->str().find_first_not_of(" \t"));
                                parLastHeader->second += " " + line->str();
                            }
                            else
                                parLastHeader = end(pConDetails->HeaderList);
                        }
                        ++line;
                    }
                    pConDetails->strBuffer.erase(0, nPosEndOfHeader + 4);

                    auto expect = pConDetails->HeaderList.find("expect");
                    if (expect != end(pConDetails->HeaderList))
                    {
                        if (expect->second == "100-continue")
                            pTcpSocket->Write("HTTP/1.1 100 Continue\r\n\r\n", 25);
                        else
                        {
                            char caBuffer[4096];
                            size_t nHeaderLen = BuildResponsHeader(caBuffer, sizeof(caBuffer), ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 417, HeadList(), 0);
                            pTcpSocket->Write(caBuffer, nHeaderLen);
                            pConDetails->pTimer.get()->Reset();

                            CLogFile::GetInstance(m_vHostParam[L""].m_strLogFile) << pTcpSocket->GetClientAddr() << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                                << pConDetails->HeaderList.find(":method")->second << " " << pConDetails->HeaderList.find(":path")->second << " HTTP/1.1\" 417 - \""
                                << (pConDetails->HeaderList.find("referer") != end(pConDetails->HeaderList) ? pConDetails->HeaderList.find("referer")->second : "-") << "\" \""
                                << (pConDetails->HeaderList.find("user-agent") != end(pConDetails->HeaderList) ? pConDetails->HeaderList.find("user-agent")->second : "-") << "\""
                                << CLogFile::LOGTYPES::END;
                            CLogFile::GetInstance(m_vHostParam[L""].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", pTcpSocket->GetClientAddr(), "] Expectation Failed");

                            pTcpSocket->Close();
                            return;
                        }
                    }

                    auto contentLength = pConDetails->HeaderList.find("content-length");
                    if (contentLength != end(pConDetails->HeaderList))
                    {
                        //stringstream ssTmp(contentLength->second);
                        //ssTmp >> pConDetails->nContentsSoll;
                        pConDetails->nContentsSoll = stoull(contentLength->second);

                        if (pConDetails->nContentsSoll > 0)
                        {
                            pConDetails->TmpFile = make_unique<TempFile>();
                            pConDetails->TmpFile->Open();

                            if ( pConDetails->strBuffer.size() > 0)
                            {
                                size_t nBytesToWrite = static_cast<size_t>(min(static_cast<uint64_t>(pConDetails->strBuffer.size()), pConDetails->nContentsSoll - pConDetails->nContentRecv));
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
                if (upgradeHeader != end(pConDetails->HeaderList) && upgradeHeader->second == "h2c")
                {
                    auto http2SettingsHeader = pConDetails->HeaderList.find("http2-settings");
                    if (http2SettingsHeader != end(pConDetails->HeaderList))
                    {
                        string strHttp2Settings = Base64::Decode(http2SettingsHeader->second, true);
                        size_t nHeaderLen = strHttp2Settings.size();
                        auto upTmpBuffer = make_unique<char[]>(nHeaderLen);
                        copy(begin(strHttp2Settings), begin(strHttp2Settings) + nHeaderLen, upTmpBuffer.get());

                        MetaSocketData soMetaDa({ pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, pConDetails->pTimer) });

                        pTcpSocket->Write("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: h2c\r\n\r\n", 71);
                        pTcpSocket->Write("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x4\x0\x10\x0\x0\x0\x5\x0\x0\x40\x0", 21);// SETTINGS frame (4) with ParaID(4) and 1048576 Value + ParaID(5) and 16384 Value
                        pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\xf\x0\x1", 13);       // WINDOW_UPDATE frame (8) with value ?1048576? (minus 65535) == 983041
                        nStreamId = 1;

                        size_t nRet;
                        if (nRet = Http2StreamProto(soMetaDa, upTmpBuffer.get(), nHeaderLen, pConDetails->lstDynTable, pConDetails->StreamParam, pConDetails->H2Streams, pConDetails->mutStreams.get(), pConDetails->TmpFile, pConDetails->atStop.get()), nRet == SIZE_MAX)
                        {
                            *pConDetails->atStop.get() = true;
                            // After a GOAWAY we terminate the connection
                            soMetaDa.fSocketClose();
                            m_mtxConnections.unlock();
                            return;
                        }
                    }
                }

                if (pConDetails->bIsH2Con == false)  // If we received or send no GOAWAY Frame in HTTP/2 we end up here, and send the response to the request how made the upgrade
                {
                    MetaSocketData soMetaDa({ pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, pConDetails->pTimer) });

                    pConDetails->mutStreams->lock();
                    pConDetails->H2Streams.emplace(nStreamId, STREAMITEM(0, deque<DATAITEM>(), move(pConDetails->HeaderList), 0, 0, make_shared<atomic_int32_t>(INITWINDOWSIZE(pConDetails->StreamParam))));
                    pConDetails->mutStreams->unlock();
                    m_mtxConnections.unlock();
                    DoAction(soMetaDa, nStreamId, HEADERWRAPPER2{ pConDetails->H2Streams }, pConDetails->StreamParam, pConDetails->mutStreams.get(), move(pConDetails->TmpFile), bind(nStreamId != 0 ? &CHttpServ::BuildH2ResponsHeader : &CHttpServ::BuildResponsHeader, this, _1, _2, _3, _4, _5, _6), pConDetails->atStop.get());

                    lock_guard<mutex> lock1(m_mtxConnections);
                    if (m_vConnections.find(pTcpSocket) == end(m_vConnections))
                        return; // Sollte bei Socket Error oder Time auftreten

                    if (nStreamId != 0)
                        pConDetails->bIsH2Con = true;
                    else
                    {
                        lock_guard<mutex> lock2(*pConDetails->mutStreams.get());
                        pConDetails->H2Streams.clear();
                    }

                    pConDetails->nContentRecv = pConDetails->nContentsSoll = 0;
                    pConDetails->HeaderList.clear();
                    return;
                }
            }
            m_mtxConnections.unlock();
        }
    }

    void OnSocketError(BaseSocket* pBaseSocket)
    {
        MyTrace("Error: Network error ", pBaseSocket->GetErrorNo());
        TcpSocket* pTcpSocket = dynamic_cast<TcpSocket*>(pBaseSocket);
        CLogFile::GetInstance(m_vHostParam[L""].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", (pTcpSocket != nullptr ? pTcpSocket->GetClientAddr() : "0.0.0.0"), "] network error no.: ",  pBaseSocket->GetErrorNo());

        m_mtxConnections.lock();
        auto item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            item->second.pTimer->Stop();
            *item->second.atStop.get() = true;
        }
        m_mtxConnections.unlock();
        pBaseSocket->Close();
    }

    void OnSocketCloseing(BaseSocket* pBaseSocket)
    {
        //OutputDebugString(L"CHttpServ::OnSocketCloseing\r\n");
        m_mtxConnections.lock();
        auto item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            item->second.pTimer->Stop();
            Timer* pTimer = item->second.pTimer.get();
            m_mtxConnections.unlock();
            while (pTimer->IsStopped() == false)
                this_thread::sleep_for(chrono::nanoseconds(1));

            m_mtxConnections.lock();
            item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
            if (item != end(m_vConnections))
            {
                // we wait, until all action thread's are finished for this connection, otherwise we remove the connection while the action thread is still using it = crash
                m_ActThrMutex.lock();
                for (auto iter = begin(m_umActionThreads); iter != end(m_umActionThreads);)
                {
                    if (iter->second == item->second.atStop.get())
                    {
                        *iter->second = true;   // Stop the DoAction thread
                        m_ActThrMutex.unlock();
                        this_thread::sleep_for(chrono::milliseconds(1));
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
        m_mtxConnections.unlock();
    }

    void OnTimeout(Timer* pTimer)
    {
        lock_guard<mutex> lock(m_mtxConnections);
        for (auto it = begin(m_vConnections); it != end(m_vConnections); ++it)
        {
            if (it->second.pTimer.get() == pTimer)
            {
                *it->second.atStop.get() = true;
                it->first->Close();
                break;
            }
        }
    }

    size_t BuildH2ResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0)
    {
        size_t nHeaderSize = 0;
        size_t nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, ":status", to_string(iRespCode).c_str());
        if (nReturn == SIZE_MAX)
            return 0;
        nHeaderSize += nReturn;

        auto in_time_t = chrono::system_clock::to_time_t(chrono::system_clock::now());

        stringstream strTemp;
        strTemp.imbue(m_cLocal);
        strTemp << put_time(::gmtime(&in_time_t), "%a, %d %b %Y %H:%M:%S GMT");

        nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "date", strTemp.str().c_str());
        if (nReturn == SIZE_MAX)
            return 0;
        nHeaderSize += nReturn;

        nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "server", "Http2Serv");
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
/*
        if ((iRespCode / 100) == 2 || (iRespCode / 100) == 3)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "last-modified", strTemp.str().c_str());
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }
*/
        for (const auto& item : umHeaderList)
        {
            string strHeaderFiled(item.first);
            transform(begin(strHeaderFiled), end(strHeaderFiled), begin(strHeaderFiled), ::tolower);
            if (strHeaderFiled =="pragma" || strHeaderFiled == "cache-control")
                iFlag &= ~ADDNOCACHE;
            if (strHeaderFiled == "connection" || strHeaderFiled == "transfer-encoding")
                continue;
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
        else if (iFlag & DEFLATEENCODING)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "content-encoding", "deflate");
            if (nReturn == SIZE_MAX)
                return 0;
            nHeaderSize += nReturn;
        }
        else if (iFlag & BROTLICODING)
        {
            nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "content-encoding", "br");
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

    size_t BuildResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0)
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
        case 207: strRespons += "Multi-Status"; break;
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
        case 416: strRespons += "Requested Range Not Satisfiable"; break;
        case 417: strRespons += "Expectation Failed"; break;
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

        stringstream ss;
        ss.imbue(m_cLocal);
        ss << put_time(::gmtime(&in_time_t), "%a, %d %b %Y %H:%M:%S GMT\r\n");
        strRespons += ss.str();

        if (nContentSize != 0 || iFlag & ADDCONENTLENGTH)
            strRespons += "Content-Length: " + to_string(nContentSize) + "\r\n";

        for (const auto& item : umHeaderList)
        {
            if (item.first == "Pragma" || item.first == "Cache-Control")
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
        else if (iFlag & DEFLATEENCODING)
            strRespons += "Content-Encoding: deflate\r\n";
        else if (iFlag & BROTLICODING)
            strRespons += "Content-Encoding: br\r\n";

        if (iFlag & TERMINATEHEADER)
            strRespons += "\r\n";

        size_t nRet = strRespons.size();
        if (nRet > nBufLen)
            return 0;

        copy(begin(strRespons), begin(strRespons) + nRet, szBuffer);

        return nRet;
    }

    void DoAction(MetaSocketData soMetaDa, uint32_t nStreamId, HEADERWRAPPER2 hw2, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile> pTmpFile, function<size_t(char*, size_t, int, int, HeadList, uint64_t)> BuildRespHeader, atomic<bool>* patStop)
    {
        const static unordered_map<string, int> arMethoden = { {"GET", 0}, {"HEAD", 1}, {"POST", 2}, {"OPTIONS", 3} };

        if (patStop != nullptr)
        {
            lock_guard<mutex> lock(m_ActThrMutex);
            m_umActionThreads.emplace(this_thread::get_id(), patStop);
        }

        auto fuExitDoAction = [&]()
        {
            lock_guard<mutex> lock1(*pmtxStream);
            auto StreamItem = hw2.StreamList.find(nStreamId);
            if (StreamItem != end(hw2.StreamList))
                STREAMSTATE(StreamItem) |= RESET_STREAM;    // hw2.StreamList.erase(StreamItem);

            if (patStop != nullptr)
            {
                lock_guard<mutex> lock2(m_ActThrMutex);
                m_umActionThreads.erase(this_thread::get_id());
            }
        };

        auto fnIsStreamReset = [&](uint32_t nId) -> bool
        {
            if (nId == 0) return false;
            lock_guard<mutex> lock(*pmtxStream);
            auto StreamItem = hw2.StreamList.find(nId);
            if (StreamItem != end(hw2.StreamList))
                return ((STREAMSTATE(StreamItem) & RESET_STREAM) == RESET_STREAM ? true : false);
            return true;
        };

        auto fnSendCueReady = [&](int32_t& nStreamWndSize, size_t& nSendBufLen, uint64_t nBufSize, uint64_t nRestLenToSend) -> bool
        {
            size_t nInQue = soMetaDa.fSockGetOutBytesInQue();
            if (nInQue >= 0x200000 || nStreamWndSize <= 0)
            {
                this_thread::sleep_for(chrono::nanoseconds(1));
                return false;
            }

            nSendBufLen = min(static_cast<size_t>(min(nBufSize, nRestLenToSend)), static_cast<size_t>(nStreamWndSize));
            return true;
        };

        auto fnGetStreamWindowSize = [&](int32_t& iStreamWndSize) -> bool
        {
            if (nStreamId != 0)
            {
                int32_t iTotaleWndSize = UINT16_MAX;

                lock_guard<mutex> lock0(*pmtxStream);
                auto StreamItem = hw2.StreamList.find(nStreamId);
                if (StreamItem != end(hw2.StreamList))
                    iStreamWndSize = WINDOWSIZE(StreamItem);
                else
                    return false;  // Stream Item was removed, properly the stream was reseted
                StreamItem = hw2.StreamList.find(0);
                if (StreamItem != end(hw2.StreamList))
                    iTotaleWndSize = WINDOWSIZE(StreamItem);
                else
                    return false;  // Stream Item was removed, properly the stream was reseted
                iStreamWndSize = min(iStreamWndSize, iTotaleWndSize);
            }
            return true;
        };

        auto fnUpdateStreamParam = [&](size_t& nSendBufLen) -> int
        {
            if (nStreamId != 0)
            {
                lock_guard<mutex> lock(*pmtxStream);
                auto StreamItem = hw2.StreamList.find(nStreamId);
                if (StreamItem != end(hw2.StreamList))
                    WINDOWSIZE(StreamItem) -= static_cast<uint32_t>(nSendBufLen);
                else
                    return -1;  // Stream Item was removed, properly the stream was reseted
                StreamItem = hw2.StreamList.find(0);
                if (StreamItem != end(hw2.StreamList))
                    WINDOWSIZE(StreamItem) -= static_cast<uint32_t>(nSendBufLen);
                else
                    return -1;  // Stream Item was removed, properly the stream was reseted
            }
            return 0;
        };

        const uint32_t nHttp2Offset = nStreamId != 0 ? 9 : 0;
        const uint32_t nSizeSendBuf = MAXFRAMESIZE(tuStreamSettings);// 0x4000;
        char caBuffer[4096];
        int iHeaderFlag = 0;
        string strHttpVersion("0");

        pmtxStream->lock();
        if (hw2.StreamList.size() == 0 || hw2.StreamList.find(nStreamId) == end(hw2.StreamList))
        {
            pmtxStream->unlock();
            fuExitDoAction();
            return;
        }
        HeadList& lstHeaderFields = GETHEADERLIST(hw2.StreamList.find(nStreamId));
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

        auto itVersion = lstHeaderFields.find(":version");
        if (itVersion != end(lstHeaderFields))
        {
            strHttpVersion = itVersion->second;
            if (strHttpVersion == "1")
                iHeaderFlag = HTTPVERSION11;
        }

        auto connection = lstHeaderFields.find("connection");
        if (connection != end(lstHeaderFields) && connection->second == "close")
            iHeaderFlag |= ADDCONNECTIONCLOSE;

        bool bCloseConnection = ((iHeaderFlag & HTTPVERSION11) == 0 || (iHeaderFlag & ADDCONNECTIONCLOSE) == ADDCONNECTIONCLOSE) && nStreamId == 0 ? true : false; // if HTTP/1.0 and not HTTP/2.0 close connection after data is send
        if (bCloseConnection == true)
            iHeaderFlag |= ADDCONNECTIONCLOSE;

        if (soMetaDa.bIsSsl == true)
            iHeaderFlag |= HSTSHEADER;

        auto itMethode = lstHeaderFields.find(":method");
        auto itPath = lstHeaderFields.find(":path");
        if (itMethode == end(lstHeaderFields) || itPath == end(lstHeaderFields) || (strHttpVersion.find_first_not_of("01") != string::npos && nStreamId == 0))
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 400, HeadList(), 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();
            if (nStreamId == 0)
                soMetaDa.fSocketClose();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "]  Method or Path is not defined in the request");

            fuExitDoAction();
            return;
        }

        // Decode URL (%20 -> ' ')
        wstring strItemPath;
        size_t nLen = itPath->second.size();
        wchar_t wch = 0;
        int nAnzParts = 0;
        for (size_t n = 0; n < nLen; ++n)
        {
            int chr = itPath->second.at(n);
            if ('%' == chr)
            {
                if (n + 2 >= nLen || isxdigit(itPath->second.at(n + 1)) == 0 || isxdigit(itPath->second.at(n + 2)) == 0)
                {
                    size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 400, HeadList(), 0);
                    if (nStreamId != 0)
                        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                    soMetaDa.fResetTimer();
                    if (nStreamId == 0)
                        soMetaDa.fSocketClose();

                    CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \"" << itMethode->second << " " << lstHeaderFields.find(":path")->second << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion << "\" 400 - \"" << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \"" << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\"" << CLogFile::LOGTYPES::END;
                    CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] bad request: ", itPath->second);

                    fuExitDoAction();
                    return;
                }
                char Nipple1 = itPath->second.at(n + 1) - (itPath->second.at(n + 1) <= '9' ? '0' : (itPath->second.at(n + 1) <= 'F' ? 'A' : 'a') - 10);
                char Nipple2 = itPath->second.at(n + 2) - (itPath->second.at(n + 2) <= '9' ? '0' : (itPath->second.at(n + 2) <= 'F' ? 'A' : 'a') - 10);
                chr = 16 * Nipple1 + Nipple2;
                n += 2;

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
                strItemPath += static_cast<wchar_t>(itPath->second.at(n));
        }

        wstring strQuery;
        string::size_type nPos;
        if (string::npos != (nPos = strItemPath.find(L'?')))
        {
            strQuery = strItemPath.substr(nPos + 1);
            strItemPath.erase(nPos);
        }

        // Check for redirect
        for (auto& tuRedirect : m_vHostParam[szHost].m_vRedirMatch)    // RedirectMatch
        {
            wregex rx(get<1>(tuRedirect));
            wsmatch match;
            if (regex_match(strItemPath.cbegin(), strItemPath.cend(), match, rx) == true)
            {
                wstring strLokation = regex_replace(strItemPath, rx, get<2>(tuRedirect), regex_constants::format_first_only);
                strLokation = regex_replace(strLokation, wregex(L"\\%\\{SERVER_NAME\\}"), szHost);

                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 301, HeadList({make_pair("Location", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strLokation))}), 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                soMetaDa.fResetTimer();
                if (nStreamId == 0)
                    soMetaDa.fSocketClose();

                CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                    << itMethode->second << " " << lstHeaderFields.find(":path")->second
                    << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                    << "\" 301 -" << " \""
                    << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                    << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                    << CLogFile::LOGTYPES::END;

                fuExitDoAction();
                return;
            }
        }

        // Check for SetEnvIf
        for (auto& strEnvIf : m_vHostParam[szHost].m_vEnvIf)
        {
            const static map<wstring, int> mKeyWord = { {L"REMOTE_HOST", 1 }, { L"REMOTE_ADDR", 2 },{ L"SERVER_ADDR", 3 },{ L"REQUEST_METHOD", 4 },{ L"REQUEST_PROTOCOL", 5 },{ L"REQUEST_URI", 6 } };
            const auto& itKeyWord = mKeyWord.find(get<0>(strEnvIf));
            if (itKeyWord != mKeyWord.end())
            {
                bool bFound = false;

                switch (itKeyWord->second)
                {
                case 2: // REMOTE_ADDR
                    bFound = regex_match(begin(soMetaDa.strIpClient), end(soMetaDa.strIpClient), regex(wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<1>(strEnvIf))));
                    break;
                case 6: // REQUEST_URI
                    bFound = regex_match(begin(strItemPath), end(strItemPath), wregex(get<1>(strEnvIf)));
                    break;
                }

                if (bFound == true)
                {
                    if (get<2>(strEnvIf) == L"DONTLOG")
                        CLogFile::SetDontLog();
                }
            }
        }

        // Check for RewriteRule
        for (auto& strRule : m_vHostParam[szHost].m_mstrRewriteRule)
        {
            strItemPath = regex_replace(strItemPath, wregex(strRule.first), strRule.second, regex_constants::format_first_only);
        }

        // Test for forbidden element /.. /aux /lpt /com .htaccess .htpasswd .htgroup
        const static wregex rxForbidden(L"/\\.\\.|/aux$|/noel$|/prn$|/con$|/lpt[0-9]+$|/com[0-9]+$|\\.htaccess|\\.htpasswd|\\.htgroup", regex_constants::ECMAScript | regex_constants::icase);
        if (regex_search(strItemPath.c_str(), rxForbidden) == true)
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 403, HeadList(), 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                << "\" 403 " << "-" << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;
            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] forbidden element: ", itPath->second);

            if (bCloseConnection == true && nStreamId == 0)
                soMetaDa.fSocketClose();

            fuExitDoAction();
            return;
        }

        // Check for Authentication
        for (auto& strAuth : m_vHostParam[szHost].m_mAuthenticate)
        {
            if (regex_search(strItemPath, wregex(strAuth.first)) == true)
            {
                auto fnSendAuthRespons = [&]() -> void
                {
                    HeadList vHeader({ make_pair("WWW-Authenticate", "Basic realm=\"Http-Utility Basic\"") });
                    vHeader.push_back(make_pair("WWW-Authenticate", " Digest realm=\"Http-Utility Digest\",\r\n\tqop=\"auth,auth-int\",\r\n\tnonce=\"cmFuZG9tbHlnZW5lcmF0ZWRub25jZQ\",\r\n\topaque=\"c29tZXJhbmRvbW9wYXF1ZXN0cmluZw\""));
                    size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 401, vHeader, 0);
                    if (nStreamId != 0)
                        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                    soMetaDa.fResetTimer();
                    if (nStreamId == 0)
                        soMetaDa.fSocketClose();
                    fuExitDoAction();
                };

                auto itAuth = lstHeaderFields.find("authorization");
                if (itAuth == end(lstHeaderFields))
                    return fnSendAuthRespons();

                nPos = itAuth->second.find(' ');
                if (nPos == string::npos)
                    return fnSendAuthRespons();
                if (itAuth->second.substr(0, nPos) == "Basic")
                {
                    wstring strCredenial = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(Base64::Decode(itAuth->second.substr(nPos + 1)));
                    if (find_if(begin(strAuth.second), end(strAuth.second), [strCredenial](const auto& strUser) { return strCredenial == strUser ? true : false; }) == end(strAuth.second))
                        return fnSendAuthRespons();
                }
                else if (itAuth->second.substr(0, nPos) == "Digest")
                {   // username="Thomas", realm="Http-Utility Digest", nonce="cmFuZG9tbHlnZW5lcmF0ZWRub25jZQ", uri="/iso/", response="2254355340eede0649b9df7f0121dcca", opaque="c29tZXJhbmRvbW9wYXF1ZXN0cmluZw", qop=auth, nc=00000002, cnonce="78a4421707fa60bc"
                    const static regex spaceSeperator(",");
                    string strTmp = itAuth->second.substr(nPos + 1);
                    sregex_token_iterator token(begin(strTmp), end(strTmp), spaceSeperator, -1);
                    map<string, string> maDigest;
                    while (token != sregex_token_iterator())
                    {
                        if (nPos = token->str().find("="), nPos != string::npos)
                        {
                            string strKey = token->str().substr(0, nPos);
                            strKey.erase(strKey.find_last_not_of(" \t") + 1);
                            strKey.erase(0, strKey.find_first_not_of(" \t"));
                            auto itInsert = maDigest.emplace(strKey, token->str().substr(nPos + 1));
                            if (itInsert.second == true)
                            {
                                itInsert.first->second.erase(itInsert.first->second.find_last_not_of("\" \t") + 1);
                                itInsert.first->second.erase(0, itInsert.first->second.find_first_not_of("\" \t"));
                            }
                        }
                        token++;
                    }

                    wstring strUserName = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(maDigest["username"]);
                    auto item = find_if(begin(strAuth.second), end(strAuth.second), [strUserName](const auto& strUser) { return strUser.substr(0, strUserName.size()) == strUserName && strUser[strUserName.size()] == ':' ? true : false; });
                    if (item == end(strAuth.second))
                        return fnSendAuthRespons();

                    string PassWord = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(item->substr(strUserName.size()));
                    string strM1 = md5(maDigest["username"] + ":" + maDigest["realm"] + PassWord);
                    string strM2 = md5(itMethode->second + ":" + maDigest["uri"]);
                    string strRe = md5(strM1 + ":" + maDigest["nonce"] + ":" + maDigest["nc"] + ":" + maDigest["cnonce"] + ":" + maDigest["qop"] + ":" + strM2);
                    if (strRe != maDigest["response"])
                        return fnSendAuthRespons();
                }
                else
                    return fnSendAuthRespons();
            }
        }

        // AliasMatch
        bool bNewRootSet = false;
        for (auto& strAlias : m_vHostParam[szHost].m_mstrAliasMatch)
        {
            wstring strNewPath = regex_replace(strItemPath, wregex(strAlias.first), strAlias.second, regex_constants::format_first_only);
            if (strNewPath != strItemPath)
            {
                strItemPath = strNewPath;
                bNewRootSet = true;
                break;
            }
        }

        transform(begin(itMethode->second), end(itMethode->second), begin(itMethode->second), ::toupper);
        auto aritMethode = arMethoden.find(itMethode->second);

        if (aritMethode == arMethoden.end())
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 405, HeadList(), 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();
            if (nStreamId == 0)
                soMetaDa.fSocketClose();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "]  Method Not Allowed: ", itMethode->second);

            fuExitDoAction();
            return;
        }

        // OPTION
        if (aritMethode->second == 3)
        {
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER/* | ADDCONNECTIONCLOSE*/ | ADDCONENTLENGTH, 200, HeadList({make_pair("Allow", "OPTIONS, GET, HEAD, POST")}), 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();
            if (bCloseConnection == true)
                soMetaDa.fSocketClose();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                << "\" 200 -" << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;

            fuExitDoAction();
            return;
        }

        // DefaultType
        string strMineType;
        for (auto& strTyp : m_vHostParam[szHost].m_mstrForceTyp)
        {
            wregex rx(strTyp.first);
            if (regex_search(strItemPath, rx, regex_constants::format_first_only) == true)
                strMineType = string(begin(strTyp.second), end(strTyp.second));
        }

        // Get base directory and build filename
        if (bNewRootSet == false)
            strItemPath = m_vHostParam[szHost].m_strRootPath + strItemPath;

        // replace Slash in Backslash (Windows can handle them too)
        replace(begin(strItemPath), end(strItemPath), L'\\', L'/');

        // supplement default item
        struct _stat64 stFileInfo;
        int iRet = ::_wstat64(FN_CA(strItemPath), &stFileInfo);
        if (iRet == 0 && (stFileInfo.st_mode & _S_IFDIR) == _S_IFDIR)
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
            iRet = ::_wstat64(FN_CA(strItemPath), &stFileInfo);
        }

        if (iRet != 0 || (stFileInfo.st_mode & _S_IFDIR) == _S_IFDIR || (stFileInfo.st_mode & _S_IFREG) == 0)
        {
            if (errno == ENOENT || errno == ENAMETOOLONG || (stFileInfo.st_mode & _S_IFDIR) == _S_IFDIR)
            {
                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 404, HeadList(), 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);

                CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                    << itMethode->second << " " << lstHeaderFields.find(":path")->second
                    << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                    << "\" 404 - \""
                    << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                    << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                    << CLogFile::LOGTYPES::END;

                CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] File does not exist: ", itPath->second);
            }
            else //EINVAL
            {
                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, HeadList(), 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);

                CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                    << itMethode->second << " " << lstHeaderFields.find(":path")->second
                    << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                    << "\" 500 " << "-" << " \""
                    << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                    << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                    << CLogFile::LOGTYPES::END;

                CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] internal server error: ", itPath->second);
            }

            soMetaDa.fResetTimer();
            if (nStreamId == 0)
                soMetaDa.fSocketClose();

            fuExitDoAction();
            return;
        }

        // Get file type of item
        wstring strFileExtension(strItemPath.substr(strItemPath.rfind(L'.') + 1));
        transform(begin(strFileExtension), end(strFileExtension), begin(strFileExtension), ::tolower);

        uint32_t iStatus = 200;

        for (const auto& strFileTyp : m_vHostParam[szHost].m_mFileTypeAction)
        {
            if (strFileExtension == strFileTyp.first)
            {
                if (strFileTyp.second.empty() == false)
                {
                    uint64_t nSollLen = 0, nPostLen = 0;
                    if (pTmpFile != 0)
                        nPostLen = pTmpFile->GetFileLength();
                    auto itContLen = lstHeaderFields.find("content-length");
                    if (itContLen != end(lstHeaderFields))
                        nSollLen = stoll(itContLen->second);

                    wstringstream ss;
                    ss.imbue(m_cLocal);
                    const static array<pair<const char*, const wchar_t*>, 4> caHeaders = { { make_pair(":authority", L"HTTP_HOST") , make_pair(":scheme", L"REQUEST_SCHEME") , make_pair("content-type", L"CONTENT_TYPE"), make_pair("content-length", L"CONTENT_LENGTH") } };
                    for (auto& itHeader : lstHeaderFields)
                    {
                        if (itHeader.first != "content-length" || (pTmpFile != 0 && nSollLen == nPostLen))
                        {
                            auto itArray = find_if(begin(caHeaders), end(caHeaders), [&](auto& prItem) { return prItem.first == itHeader.first ? true : false; });
                            if (itArray != end(caHeaders))
                                ss << ENV << itArray->second << "=" << QUOTES << FIXENVSTR(itHeader.second).c_str() << QUOTES << ENVJOIN;
                            else
                            {
                                wstring strHeader(wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(itHeader.first));
                                strHeader.erase(0, strHeader.find_first_not_of(L':'));
                                transform(begin(strHeader), end(strHeader), begin(strHeader), ::toupper);
                                strHeader = regex_replace(strHeader, wregex(L"-"), wstring(L"_"));
                                ss << ENV << L"HTTP_" << strHeader << "=" << QUOTES << FIXENVSTR(itHeader.second).c_str() << QUOTES << ENVJOIN;
                            }
                        }
                    }

                    ss << ENV << "SERVER_SOFTWARE=Http2Util/0.1" << ENVJOIN << ENV << L"REDIRECT_STATUS=200" << ENVJOIN << ENV << L"REMOTE_ADDR=" << soMetaDa.strIpClient.c_str() << ENVJOIN << ENV << L"SERVER_PORT=" << soMetaDa.sPortInterFace;
                    ss << ENVJOIN << ENV << L"SERVER_ADDR=" << soMetaDa.strIpInterface.c_str() << ENVJOIN << ENV << L"REMOTE_PORT=" << soMetaDa.sPortClient;
                    ss << ENVJOIN << ENV << L"SERVER_PROTOCOL=" << (nStreamId != 0 ? L"HTTP/2." : L"HTTP/1.") << strHttpVersion.c_str();
                    ss << ENVJOIN << ENV << L"DOCUMENT_ROOT=" << QUOTES << m_vHostParam[szHost].m_strRootPath << QUOTES << ENVJOIN << ENV << L"GATEWAY_INTERFACE=CGI/1.1" << ENVJOIN << ENV << L"SCRIPT_NAME=" << QUOTES << itPath->second.substr(0, itPath->second.find("?")).c_str() << QUOTES;
                    ss << ENVJOIN << ENV << L"REQUEST_METHOD=" << itMethode->second.c_str() << ENVJOIN << ENV << L"REQUEST_URI=" << QUOTES << FIXENVSTR(itPath->second).c_str() << QUOTES;
                    if (soMetaDa.bIsSsl == true)
                        ss << ENVJOIN << ENV << L"HTTPS=on";
                    if (strQuery.empty() == false)
                        ss << ENVJOIN << ENV << L"QUERY_STRING=" << QUOTES << WFIXENVSTR(strQuery) << QUOTES;
                    ss << ENVJOIN << ENV << L"SCRIPT_FILENAME=" << QUOTES << strItemPath << QUOTES << ENVJOIN << strFileTyp.second;

                    if (pTmpFile != 0 && pTmpFile.get()->GetFileName().empty() == false)
                        ss << " < " << pTmpFile.get()->GetFileName().c_str();

                    FILE* hPipe = nullptr;
                    if (ss.str().size() < 8191) // 8191 Limit in Windows XP and higher
                        hPipe = _wpopen(FN_CA(ss.str()), PIPETYPE);
                    if (hPipe != nullptr && ferror(hPipe) == 0)
                    {
                        bool bEndOfHeader = false;
                        HeadList umPhpHeaders;
                        char psBuffer[4096];
                        basic_string<char> strBuffer;
                        size_t nTotal = 0, nOffset = 0;
                        bool bChunkedTransfer = false;
                        while (feof(hPipe) == 0 && (*patStop).load() == false)
                        {
                            size_t nRead = fread(psBuffer + nHttp2Offset + nOffset, 1, static_cast<int>(4096 - nHttp2Offset - nOffset), hPipe);
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
                                            // Build response header
                                            auto itCgiHeader = umPhpHeaders.ifind("status");
                                            if (itCgiHeader != end(umPhpHeaders))
                                            {
                                                iStatus = stoi(itCgiHeader->second);
                                                umPhpHeaders.erase(itCgiHeader);
                                            }
                                            if (umPhpHeaders.ifind("Transfer-Encoding") == end(umPhpHeaders) && umPhpHeaders.ifind("Content-Length") == end(umPhpHeaders) && nStreamId == 0 && strHttpVersion == "1")
                                            {
                                                umPhpHeaders.emplace_back(make_pair("Transfer-Encoding","chunked"));
                                                bChunkedTransfer = true;
                                            }
                                            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER, iStatus, umPhpHeaders, 0);
                                            if (nStreamId != 0)
                                                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                                            if (fnIsStreamReset(nStreamId) == false)
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
                                                auto iter = umPhpHeaders.emplace(umPhpHeaders.end(), make_pair(strBuffer.substr(0, nPos2), strBuffer.substr(nPos2 + 1, nPosStart - (nPos2 + 1))));
                                                if (iter != end(umPhpHeaders))
                                                {
                                                    iter->second.erase(0, iter->second.find_first_not_of(" "));
                                                }
                                            }

                                            if (nPosEnd - nPosStart > 2 && strBuffer.compare(nPosStart, 2, "\r\n") == 0)
                                                nPosEnd -= 2;
                                            else if (nPosEnd - nPosStart > 1 && strBuffer.compare(nPosStart, 2, "\n\n") == 0)
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

                                size_t nBytesTransfered = 0;
                                while (nBytesTransfered < nRead && (*patStop).load() == false)
                                {
                                    int32_t nStreamWndSize = INT32_MAX;
                                    if (fnGetStreamWindowSize(nStreamWndSize) == false)
                                        break;  // Stream Item was removed, properly the stream was reseted

                                    size_t nSendBufLen;
                                    if (fnSendCueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), nRead - nBytesTransfered) == false)
                                        continue;

                                    nBytesTransfered += nSendBufLen;

                                    if (nStreamId != 0)
                                        BuildHttp2Frame(psBuffer, nSendBufLen, 0x0, 0x0, nStreamId);
                                    else if (bChunkedTransfer == true)
                                    {
                                        stringstream ss;
                                        ss << hex << ::uppercase << nSendBufLen << "\r\n";
                                        soMetaDa.fSocketWrite(ss.str().c_str(), ss.str().size());
                                    }
                                    if (fnIsStreamReset(nStreamId) == false)
                                        soMetaDa.fSocketWrite(psBuffer, nSendBufLen + nHttp2Offset);
                                    if (bChunkedTransfer == true)
                                        soMetaDa.fSocketWrite("\r\n", 2);
                                    soMetaDa.fResetTimer();

                                    if (fnUpdateStreamParam(nSendBufLen) == -1)
                                        break;  // Stream Item was removed, properly the stream was reseted
                                }

                                nTotal += nBytesTransfered;
                            }
                            else if (ferror(hPipe) != 0)
                            {
                                CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Failed to read the pipe: ", itPath->second);
                                break;
                            }
                            else
                                this_thread::sleep_for(chrono::milliseconds(1));
                        }

                        if (bEndOfHeader == true && nStreamId != 0)
                        {
                            BuildHttp2Frame(psBuffer, 0, 0x0, 0x1, nStreamId);
                            soMetaDa.fSocketWrite(psBuffer, nHttp2Offset);
                            soMetaDa.fResetTimer();
                        }
                        else if (bEndOfHeader == true && bChunkedTransfer == true)
                            soMetaDa.fSocketWrite("0\r\n\r\n", 5);
                        else if (bEndOfHeader == false)
                        {   // Noch kein Header gesendet
                            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, HeadList(), 0);
                            if (nStreamId != 0)
                                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                            soMetaDa.fResetTimer();
                            if (nStreamId == 0)
                                bCloseConnection = true;
                        }

                        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                            << itMethode->second << " " << lstHeaderFields.find(":path")->second
                            << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                            << "\" " << iStatus << " " << nTotal << " \""
                            << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                            << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                            << CLogFile::LOGTYPES::END;

                        if (feof(hPipe) == 0 || ferror(hPipe) != 0)
                            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Failed to read the pipe to the end for: ", itPath->second);

                        /* Close pipe and print return value of pPipe. */
                        auto PipeExit = _pclose(hPipe);
                        if (PipeExit != 0)
                            MyTrace("Process returned ", PipeExit, " mit errno ", errno);
                    }
                    else
                    {
                        size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, HeadList(), 0);
                        if (nStreamId != 0)
                            BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                        soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                        soMetaDa.fResetTimer();
                        if (nStreamId == 0)
                            bCloseConnection = true;

                        CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Server error handling: ", itPath->second);
                    }
                }
                else
                {
                    size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, HeadList(), 0);
                    if (nStreamId != 0)
                        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                    soMetaDa.fResetTimer();
                    if (nStreamId == 0)
                        bCloseConnection = true;

                    CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Server error handling: ", itPath->second);
                }

                if (bCloseConnection == true)
                    soMetaDa.fSocketClose();

                fuExitDoAction();
                return;
            }
        }

        // Static Content, check if we have a Modified Since Header
        auto ifmodifiedsince = lstHeaderFields.find("if-modified-since");
        if (ifmodifiedsince != end(lstHeaderFields))
        {
            tm tmIfModified = { 0 };
            stringstream ss(ifmodifiedsince->second);
            ss >> get_time(&tmIfModified, "%a, %d %b %Y %H:%M:%S GMT");
            double dTimeDif = difftime(mktime(&tmIfModified), mktime(::gmtime(&stFileInfo.st_mtime)));
            if (fabs(dTimeDif) < 0.001)
            {
                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, 304, HeadList(), 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                soMetaDa.fResetTimer();
                if (bCloseConnection == true)
                    soMetaDa.fSocketClose();

                fuExitDoAction();
                return;
            }
        }

        // MimeType
        if (strMineType.empty() == true)    // if not empty we have a fixed mimetyp from the configuration
        {
            auto it = find_if(begin(MimeListe), end(MimeListe), [strFileExtension](const MIMEENTRY & item) { return strFileExtension == MIMEEXTENSION(item); });
            if (it != end(MimeListe))
                strMineType = MIMESTRING(*it);
            else
                strMineType = "application/octet-stream";
        }

        HeadList umPhpHeaders;
        stringstream strLastModTime; strLastModTime << put_time(::gmtime(&stFileInfo.st_mtime), "%a, %d %b %Y %H:%M:%S GMT");
        umPhpHeaders.emplace_back(make_pair("Content-Type", strMineType));
        umPhpHeaders.emplace_back(make_pair("Last-Modified", strLastModTime.str()));
        umPhpHeaders.emplace_back(make_pair("Cache-control", "must-revalidate"));

        if (aritMethode->second == 1) // HEAD
        {
            // Build response header
            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, stFileInfo.st_size);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            if (fnIsStreamReset(nStreamId) == false)
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);

            soMetaDa.fResetTimer();
            if (bCloseConnection == true)
                soMetaDa.fSocketClose();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                << "\" " << iStatus << " " << (stFileInfo.st_size == 0 ? "-" : to_string(stFileInfo.st_size)) << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;

            fuExitDoAction();
            return;
        }

        uint64_t nFSize = 0;

        // Load file
        fstream fin(FN_CA(strItemPath), ios_base::in | ios_base::binary);
        if (fin.is_open() == true)
        {
            nFSize = stFileInfo.st_size;

            auto acceptencoding = lstHeaderFields.find("accept-encoding");
            if (acceptencoding != end(lstHeaderFields))
            {
                // http://www.filesignatures.net/index.php?page=all
                if (find_if(begin(m_vHostParam[szHost].m_vDeflateTyps), end(m_vHostParam[szHost].m_vDeflateTyps), [&](const string& strType) { return strType == strMineType ? true : false; }) != end(m_vHostParam[szHost].m_vDeflateTyps))
                {
                    if (acceptencoding->second.find("br") != string::npos) iHeaderFlag |= BROTLICODING;
                    else if (acceptencoding->second.find("gzip") != string::npos) iHeaderFlag |= GZIPENCODING;
                    else if (acceptencoding->second.find("deflate") != string::npos) iHeaderFlag |= DEFLATEENCODING;
                }
            }

            //iHeaderFlag &= ~(GZIPENCODING | DEFLATEENCODING);
            if (iHeaderFlag & GZIPENCODING || iHeaderFlag & DEFLATEENCODING)
            {
                if (nStreamId == 0)
                    umPhpHeaders.emplace_back(make_pair("Transfer-Encoding", "chunked"));

                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                soMetaDa.fResetTimer();

                GZipPack gzipEncoder;
                if (gzipEncoder.Init((iHeaderFlag & DEFLATEENCODING) ? true : false) == Z_OK)
                {
                    unique_ptr<unsigned char> srcBuf(new unsigned char[nSizeSendBuf]);
                    unique_ptr<unsigned char> dstBuf(new unsigned char[nSizeSendBuf]);

                    uint64_t nBytesTransfered = 0;
                    int iRet;
                    do
                    {
                        int32_t nStreamWndSize = INT32_MAX;
                        if (fnGetStreamWindowSize(nStreamWndSize) == false || (*patStop).load() == true)
                            break;  // Stream Item was removed, properly the stream was reseted

                        size_t nSendBufLen;
                        if (fnSendCueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), nFSize - nBytesTransfered) == false)
                            continue;

                        streamsize nBytesRead = fin.read(reinterpret_cast<char*>(srcBuf.get()), nSendBufLen).gcount();
                        if (nBytesRead == 0)
                            break;
                        nBytesTransfered += nBytesRead;

                        gzipEncoder.InitBuffer(srcBuf.get(), static_cast<uint32_t>(nBytesRead));
                        int nFlush = nBytesTransfered == nFSize ? Z_FINISH : Z_NO_FLUSH;

                        size_t nBytesConverted;
                        do
                        {
                            nBytesConverted = nSizeSendBuf - nHttp2Offset;
                            iRet = gzipEncoder.Enflate(dstBuf.get() + nHttp2Offset, &nBytesConverted, nFlush);

                            if ((iRet == Z_OK || iRet == Z_STREAM_END) && ((nSizeSendBuf - nHttp2Offset) - nBytesConverted) != 0)
                            {
                                if (nStreamId != 0)
                                    BuildHttp2Frame(reinterpret_cast<char*>(dstBuf.get()), ((nSizeSendBuf - nHttp2Offset) - nBytesConverted), 0x0, /*(nFSize == nBytesTransfered ? 0x1 : 0x0)*/0, nStreamId);
                                else
                                {
                                    stringstream ss;
                                    ss << hex << ::uppercase << ((nSizeSendBuf - nHttp2Offset) - nBytesConverted) + nHttp2Offset << "\r\n";
                                    soMetaDa.fSocketWrite(ss.str().c_str(), ss.str().size());
                                }
                                soMetaDa.fSocketWrite(dstBuf.get(), ((nSizeSendBuf - nHttp2Offset) - nBytesConverted) + nHttp2Offset);
                                if (nStreamId == 0)
                                    soMetaDa.fSocketWrite("\r\n", 2);
                                soMetaDa.fResetTimer();

                                if (fnUpdateStreamParam(nSendBufLen) == -1)
                                    break;  // Stream Item was removed, properly the stream was reseted
                            }
                        } while (iRet == Z_OK && nBytesConverted == 0 && (*patStop).load() == false);
                    } while (iRet == Z_OK);

                    if (nStreamId != 0)
                    {
                        BuildHttp2Frame(reinterpret_cast<char*>(dstBuf.get()), 0, 0x0, 0x1, nStreamId);
                        soMetaDa.fSocketWrite(dstBuf.get(), nHttp2Offset);
                    }
                    else
                        soMetaDa.fSocketWrite("0\r\n\r\n", 5);
                }
            }
            else if (iHeaderFlag & BROTLICODING)
            {
                if (nStreamId == 0)
                    umPhpHeaders.emplace_back(make_pair("Transfer-Encoding", "chunked"));

                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, 0);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                soMetaDa.fResetTimer();

                BrotliEncoderState* s = BrotliEncoderCreateInstance(0, 0, 0);
                BrotliEncoderSetParameter(s, BROTLI_PARAM_QUALITY, (uint32_t)9);
                BrotliEncoderSetParameter(s, BROTLI_PARAM_LGWIN, (uint32_t)0);
/*                if (dictionary_path != NULL) {
                    size_t dictionary_size = 0;
                    uint8_t* dictionary = ReadDictionary(dictionary_path, &dictionary_size);
                    BrotliEncoderSetCustomDictionary(s, dictionary_size, dictionary);
                    free(dictionary);
                }
*/
                unique_ptr<unsigned char> srcBuf(new unsigned char[nSizeSendBuf]);
                unique_ptr<unsigned char> dstBuf(new unsigned char[nSizeSendBuf]);

                size_t nBytIn = 0;
                const uint8_t* input;
                size_t nBytOut = nSizeSendBuf - nHttp2Offset;
                uint8_t* output = dstBuf.get() + nHttp2Offset;

                uint64_t nBytesTransfered = 0;
                while ((*patStop).load() == false && fnIsStreamReset(nStreamId) == false)
                {
                    int32_t nStreamWndSize = INT32_MAX;
                    if (fnGetStreamWindowSize(nStreamWndSize) == false)
                        break;  // Stream Item was removed, properly the stream was reseted

                    size_t nSendBufLen;
                    if (fnSendCueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), nFSize - nBytesTransfered) == false)
                        continue;

                    if (nBytIn == 0)
                    {
                        streamsize nBytesRead = fin.read(reinterpret_cast<char*>(srcBuf.get()), nSizeSendBuf).gcount();
                        nBytIn = static_cast<size_t>(nBytesRead);
                        input = srcBuf.get();
                        nBytesTransfered += nBytIn;
                    }

                    if (!BrotliEncoderCompressStream(s, nBytIn == 0 ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS, &nBytIn, &input, &nBytOut, &output, NULL))
                        break;

                    if (nBytOut != nSizeSendBuf - nHttp2Offset)
                    {
                        if (nStreamId != 0)
                            BuildHttp2Frame(reinterpret_cast<char*>(dstBuf.get()), ((nSizeSendBuf - nHttp2Offset) - nBytOut), 0x0, /*(nFSize - nBytesTransfered == 0 ? 0x1 : 0x0)*/0, nStreamId);
                        else
                        {
                            stringstream ss;
                            ss << hex << ::uppercase << ((nSizeSendBuf - nHttp2Offset) - nBytOut) + nHttp2Offset << "\r\n";
                            soMetaDa.fSocketWrite(ss.str().c_str(), ss.str().size());
                        }
                        soMetaDa.fSocketWrite(dstBuf.get(), ((nSizeSendBuf - nHttp2Offset) - nBytOut) + nHttp2Offset);
                        if (nStreamId == 0)
                            soMetaDa.fSocketWrite("\r\n", 2);
                        soMetaDa.fResetTimer();

                        if (fnUpdateStreamParam(nSendBufLen) == -1)
                            break;  // Stream Item was removed, properly the stream was reseted

                        nBytOut = nSizeSendBuf - nHttp2Offset;
                        output = dstBuf.get() + nHttp2Offset;
                    }

                    if (BrotliEncoderIsFinished(s))
                        break;
                }

                if (nStreamId != 0)
                {
                    BuildHttp2Frame(reinterpret_cast<char*>(dstBuf.get()), 0, 0x0, 0x1, nStreamId);
                    soMetaDa.fSocketWrite(dstBuf.get(), nHttp2Offset);
                }
                else
                    soMetaDa.fSocketWrite("0\r\n\r\n", 5);

                BrotliEncoderDestroyInstance(s);
            }
            else
            {
                // Build response header
                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, nFSize);
                if (nStreamId != 0)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                soMetaDa.fResetTimer();

                auto apBuf = make_unique<char[]>(nSizeSendBuf + nHttp2Offset + 2);

                uint64_t nBytesTransfered = 0;
                while (nBytesTransfered < nFSize && (*patStop).load() == false && fnIsStreamReset(nStreamId) == false)
                {
                    int32_t nStreamWndSize = INT32_MAX;
                    if (fnGetStreamWindowSize(nStreamWndSize) == false)
                        break;  // Stream Item was removed, properly the stream was reseted

                    size_t nSendBufLen;
                    if (fnSendCueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), nFSize - nBytesTransfered) == false)
                        continue;

                    nBytesTransfered += nSendBufLen;
                    fin.read(apBuf.get() + nHttp2Offset, nSendBufLen);

                    if (nStreamId != 0)
                        BuildHttp2Frame(apBuf.get(), nSendBufLen, 0x0, (nFSize - nBytesTransfered == 0 ? 0x1 : 0x0), nStreamId);
                    soMetaDa.fSocketWrite(apBuf.get(), nSendBufLen + nHttp2Offset);
                    soMetaDa.fResetTimer();

                    if (fnUpdateStreamParam(nSendBufLen) == -1)
                        break;  // Stream Item was removed, properly the stream was reseted
                }
            }
            fin.close();
        }
        else
        {
            iStatus = 500;
            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] File could not be opened: ", itPath->second);

            size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, sizeof(caBuffer) - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER/* | ADDCONNECTIONCLOSE*/, iStatus, HeadList(), 0);
            if (nStreamId != 0)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            soMetaDa.fResetTimer();
        }

        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << itMethode->second << " " << lstHeaderFields.find(":path")->second
            << (nStreamId != 0 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
            << "\" " << iStatus << " " << (nFSize == 0 ? "-" : to_string(nFSize)) << " \""
            << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
            << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;

        if (bCloseConnection == true)
            soMetaDa.fSocketClose();

        fuExitDoAction();
    }

    virtual void EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop) override
    {
        auto StreamPara = StreamList.find(streamId);
        if (StreamPara != end(StreamList))
        {
            HeadList& lstHeaderFields = GETHEADERLIST(StreamPara);
            if (find_if(begin(lstHeaderFields), end(lstHeaderFields), [&](HeadList::const_reference cIter) { return ":status" == cIter.first; }) != end(lstHeaderFields))
                throw H2ProtoException(H2ProtoException::WRONG_HEADER);
        }

        thread(&CHttpServ::DoAction, this, soMetaDa, streamId, HEADERWRAPPER2{ ref(StreamList)}, ref(tuStreamSettings), pmtxStream, move(pTmpFile), bind(&CHttpServ::BuildH2ResponsHeader, this, _1, _2, _3, _4, _5, _6), patStop).detach();
    }

private:
#pragma message("TODO!!! Folge Zeile wieder entfernen.")
    friend int main(int, const char*[]);
    friend void sigusr1_handler(int);
    friend class Service;
    TcpServer*             m_pSocket;
    CONNECTIONLIST         m_vConnections;
    mutex                  m_mtxConnections;

    string                 m_strBindIp;
    short                  m_sPort;
    map<wstring, HOSTPARAM> m_vHostParam;
    locale                 m_cLocal;
    unordered_multimap<thread::id, atomic<bool>*> m_umActionThreads;
    mutex                  m_ActThrMutex;

    const array<MIMEENTRY, 111>  MimeListe = { {
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
        MIMEENTRY(L"svg", "image/svg+xml"),
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
