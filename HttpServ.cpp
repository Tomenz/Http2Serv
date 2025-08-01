/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#include <regex>
#include <map>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <chrono>
#include <codecvt>

#include "HttpServ.h"
#include "Timer.h"
#include <fstream>
#include "H2Proto.h"
#include "LogFile.h"
#include "CommonLib/Base64.h"
#include "GZip.h"
#include <brotli/encode.h>
#include "CommonLib/md5.h"
#include "CommonLib/sha256.h"
#include "CommonLib/UrlCode.h"
#include "SpawnProcess.h"
#include "FastCgi/FastCgi.h"

using namespace std;
using namespace std::placeholders;

#if defined(_WIN32) || defined(_WIN64)

#if _MSC_VER < 1700
using namespace tr1;
#endif

#define FN_CA(x) x.c_str()
#define FN_STR(x) x
constexpr wchar_t* g_szHttpFetch{L".//Http2Fetch.exe/$1"};
#else
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#define _stat stat
#define _wstat stat
#define _stat64 stat64
#define _wstat64 stat64
#define _waccess access
#define _S_IFDIR S_IFDIR
#define _S_IFREG S_IFREG
#define FN_CA(x) wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(x).c_str()
#define FN_STR(x) wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(x)
const wchar_t* g_szHttpFetch{L".//Http2Fetch/$1"};
extern void OutputDebugString(const wchar_t* pOut);
extern void OutputDebugStringA(const char* pOut);
#endif

const char* CHttpServ::SERVERSIGNATUR = "Http2Serv/1.0.0";

atomic_size_t s_nInstCount(0);
map<string, FastCgiClient> s_mapFcgiConnections;
mutex s_mxFcgi;
map<string, int32_t>       CHttpServ::s_lstIpConnect;
mutex                      CHttpServ::s_mxIpConnect;

CHttpServ::CHttpServ(const wstring& strRootPath/* = wstring(L".")*/, const string& strBindIp/* = string("127.0.0.1")*/, uint16_t sPort/* = 80*/, bool bSSL/* = false*/) : m_pSocket(nullptr), m_strBindIp(strBindIp), m_sPort(sPort), m_cLocal(locale("C"))
{
    ++s_nInstCount;

    HOSTPARAM hp;
    hp.m_strRootPath = strRootPath;
    hp.m_bSSL = bSSL;
    hp.m_nMaxConnPerIp = -1;
    m_vHostParam.emplace(string(), hp);
}

CHttpServ& CHttpServ::operator=(CHttpServ&& other) noexcept
{
    ++s_nInstCount;

    swap(m_pSocket, other.m_pSocket);
    other.m_pSocket = nullptr;
    swap(m_vConnections, other.m_vConnections);
    swap(m_strBindIp, other.m_strBindIp);
    swap(m_sPort, other.m_sPort);
    swap(m_vHostParam, other.m_vHostParam);
    swap(m_cLocal, other.m_cLocal);
    swap(m_umActionThreads, other.m_umActionThreads);

    return *this;
}

CHttpServ::~CHttpServ()
{
    Stop();

    while (IsStopped() == false)
        this_thread::sleep_for(chrono::milliseconds(10));

    const lock_guard<mutex> lock(s_mxFcgi);
    if (--s_nInstCount == 0 && s_mapFcgiConnections.size() > 0)
        s_mapFcgiConnections.clear();
}

bool CHttpServ::Start()
{
    if (m_vHostParam[""].m_bSSL == true)
    {
        auto pSocket = make_unique<SslTcpServer>();

        if (m_vHostParam[""].m_strCAcertificate.empty() == false && m_vHostParam[""].m_strHostCertificate.empty() == false && m_vHostParam[""].m_strHostKey.empty() == false)
        {
            if (pSocket->AddCertificate(m_vHostParam[""].m_strCAcertificate.c_str(), m_vHostParam[""].m_strHostCertificate.c_str(), m_vHostParam[""].m_strHostKey.c_str()) == false)
            {
                return false;
            }
            if (m_vHostParam[""].m_strSslCipher.empty() == false)
                pSocket->SetCipher(m_vHostParam[""].m_strSslCipher.c_str());
        }

        for (auto& Item : m_vHostParam)
        {
            if (Item.first != "" && Item.second.m_bSSL == true)
            {
                if (pSocket->AddCertificate(Item.second.m_strCAcertificate.c_str(), Item.second.m_strHostCertificate.c_str(), Item.second.m_strHostKey.c_str()) == false)
                {
                    return false;
                }
                if (Item.second.m_strSslCipher.empty() == false)
                    pSocket->SetCipher(Item.second.m_strSslCipher.c_str());
            }
        }

        const vector<string> Alpn({ { "h2" },{ "http/1.1" } });
        pSocket->SetAlpnProtokollNames(Alpn);

        m_pSocket = move(pSocket);
    }
    else
        m_pSocket = make_unique<TcpServer>();

    m_pSocket->BindNewConnection(static_cast<function<void(const vector<TcpSocket*>&)>>(bind(&CHttpServ::OnNewConnection, this, _1)));
    m_pSocket->BindErrorFunction(static_cast<function<void(BaseSocket* const)>>(bind(&CHttpServ::OnSocketError, this, _1)));
    return m_pSocket->Start(m_strBindIp.c_str(), m_sPort);
}

bool CHttpServ::Stop()
{
    if (m_pSocket != nullptr)
    {
        m_pSocket->Close();
        m_pSocket.reset(nullptr);
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

bool CHttpServ::IsStopped() noexcept
{
    return m_vConnections.size() == 0 ? true : false;
}

CHttpServ::HOSTPARAM& CHttpServ::GetParameterBlockRef(const string& szHostName)
{
    if (szHostName != string() && m_vHostParam.find(szHostName) == end(m_vHostParam))
        m_vHostParam[szHostName] = m_vHostParam[string()];

    return m_vHostParam[szHostName];
}

void CHttpServ::ClearAllParameterBlocks()
{
    HOSTPARAM hp;
    hp.m_strRootPath = m_vHostParam[string()].m_strRootPath;
    hp.m_bSSL = false;
    m_vHostParam.clear();
    m_vHostParam.emplace(string(), hp);
}

const string& CHttpServ::GetBindAdresse() noexcept
{
    return m_strBindIp;
}

uint16_t CHttpServ::GetPort() noexcept
{
    return m_sPort;
}

void CHttpServ::OnNewConnection(const vector<TcpSocket*>& vNewConnections)
{
    vector<TcpSocket*> vCache;
    for (auto& pSocket : vNewConnections)
    {
        if (pSocket != nullptr)
        {
            if (m_vHostParam[string()].m_nMaxConnPerIp > 0)
            {
                s_mxIpConnect.lock();
                auto itIpItem = s_lstIpConnect.find(pSocket->GetClientAddr());
                if (itIpItem != s_lstIpConnect.end())
                {
                    if (itIpItem->second > m_vHostParam[string()].m_nMaxConnPerIp)
                    {
                        s_mxIpConnect.unlock();
                        CLogFile::GetInstance(m_vHostParam[string()].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", pSocket->GetClientAddr(), "] to many connections");
                        pSocket->Close();
                        continue;
                    }
                    itIpItem->second++;
                }
                else
                    s_lstIpConnect.insert({ pSocket->GetClientAddr(), 1 });
                s_mxIpConnect.unlock();
            }

            pSocket->BindFuncBytesReceived(static_cast<function<void(TcpSocket* const)>>(bind(&CHttpServ::OnDataReceived, this, _1)));
            pSocket->BindErrorFunction(static_cast<function<void(BaseSocket* const)>>(bind(&CHttpServ::OnSocketError, this, _1)));
            pSocket->BindCloseFunction(static_cast<function<void(BaseSocket* const)>>(bind(&CHttpServ::OnSocketClosing, this, _1)));
            vCache.push_back(pSocket);
        }
    }
    if (vCache.size())
    {
        m_mtxConnections.lock();
        for (auto& pSocket : vCache)
        {
            m_vConnections.emplace(pSocket, CONNECTIONDETAILS({ make_shared<Timer<TcpSocket>>(30000, bind(&CHttpServ::OnTimeout, this, _1, _2), pSocket), string(), false, 0, 0, {}, {}, make_shared<mutex>(), {}, make_tuple(UINT32_MAX, 65535, 16384, UINT32_MAX, 4096), {}, make_shared<atomic_bool>(false), make_shared<mutex>(), {}, {} }));
            pSocket->StartReceiving();
        }
        m_mtxConnections.unlock();
    }
}

void CHttpServ::OnDataReceived(TcpSocket* const pTcpSocket)
{
    const size_t nAvailable = pTcpSocket->GetBytesAvailable();

    if (nAvailable == 0)
    {
        pTcpSocket->Close();
        return;
    }

    const unique_ptr<char[]> spBuffer = make_unique<char[]>(nAvailable);

    const size_t nRead = pTcpSocket->Read(&spBuffer[0], nAvailable);

    if (nRead > 0)
    {
        m_mtxConnections.lock();
        const CONNECTIONLIST::iterator item = m_vConnections.find(pTcpSocket);
        if (item != end(m_vConnections))
        {
            CONNECTIONDETAILS* pConDetails = &item->second;
            pConDetails->pTimer->Reset();
            pConDetails->strBuffer.append(&spBuffer[0], nRead);

            if (pConDetails->bIsH2Con == false)
            {
                if (pConDetails->nContentsSoll == 0 && pConDetails->strBuffer.size() >= 24 && pConDetails->strBuffer.compare(0, 24, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n") == 0)
                {
                    pTcpSocket->Write("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x4\x0\x60\x0\x0\x0\x5\x0\x0\x40\x0", 21);// SETTINGS frame (4) with ParaID(4) and ?6291456? Value + ParaID(5) and 16384 Value
                    pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\x5f\x0\x1", 13);       // WINDOW_UPDATE frame (8) with value ??6291456?? (minus 65535) == ?6225921?
                    pConDetails->bIsH2Con = true;
                    pConDetails->strBuffer.erase(0, 24);

                    if (pConDetails->strBuffer.size() == 0)
                    {
                        m_mtxConnections.unlock();
                        return;
                    }
                }
                else if (pConDetails->nContentsSoll != 0 && pConDetails->nContentRecv < pConDetails->nContentsSoll)  // File upload in progress
                {
                    const size_t nBytesToWrite = static_cast<size_t>(min(static_cast<uint64_t>(pConDetails->strBuffer.size()), pConDetails->nContentsSoll - pConDetails->nContentRecv));
                    if (nBytesToWrite > 0)
                    {
                        pConDetails->mutReqData->lock();
                        pConDetails->vecReqData.emplace_back(make_unique<char[]>(nBytesToWrite + 4));
                        copy(&pConDetails->strBuffer[0], &pConDetails->strBuffer[nBytesToWrite], pConDetails->vecReqData.back().get() + 4);
                        *reinterpret_cast<uint32_t*>(pConDetails->vecReqData.back().get()) = static_cast<uint32_t>(nBytesToWrite);
//OutputDebugString(wstring(L"X. Datenempfang: " + to_wstring(nBytesToWrite) + L" Bytes\r\n").c_str());
                        pConDetails->mutReqData->unlock();
                        pConDetails->nContentRecv += nBytesToWrite;
                        pConDetails->strBuffer.erase(0, nBytesToWrite);
                    }
                    if (pConDetails->nContentRecv == pConDetails->nContentsSoll)    // Last byte of Data, we signal this with a empty vector entry
                    {
                        pConDetails->mutReqData->lock();
                        pConDetails->vecReqData.emplace_back(unique_ptr<char[]>(nullptr));
//OutputDebugString(wstring(L"Datenempfang beendet\r\n").c_str());
                        pConDetails->nContentRecv = pConDetails->nContentsSoll = 0;
                        pConDetails->mutReqData->unlock();
                        if (pConDetails->strBuffer.size() == 0)
                        {
                            m_mtxConnections.unlock();
                            return;
                        }
                    }
                    else
                    {
                        m_mtxConnections.unlock();
                        return;
                    }
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

                const MetaSocketData soMetaDa { pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer<TcpSocket>::Reset, pConDetails->pTimer), bind(&Timer<TcpSocket>::SetNewTimeout, pConDetails->pTimer, _1) };

                size_t nRet;
                if (nRet = Http2StreamProto(soMetaDa, &pConDetails->strBuffer[0], nLen, pConDetails->lstDynTable, pConDetails->StreamParam, pConDetails->H2Streams, ref(*pConDetails->mutStreams.get()), pConDetails->StreamResWndSizes, ref(*pConDetails->atStop.get()), ref(pConDetails->lstAuthInfo)), nRet != SIZE_MAX)
                {
                    pConDetails->strBuffer.erase(0,  pConDetails->strBuffer.size() - nLen);
                    m_mtxConnections.unlock();
                    return;
                }

                // After a GOAWAY we terminate the connection
                // we wait, until all action thread's are finished, otherwise we remove the connection while the action thread is still using it = crash
                *pConDetails->atStop.get() = true;
                auto patStop = pConDetails->atStop.get();
                m_ActThrMutex.lock();
                m_mtxConnections.unlock();
                for (unordered_multimap<thread::id, atomic<bool>&>::iterator iter = begin(m_umActionThreads); iter != end(m_umActionThreads);)
                {
                    if (&iter->second == patStop)
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
                return;
            }


            const size_t nPosEndOfHeader =  pConDetails->strBuffer.find("\r\n\r\n");
            if (nPosEndOfHeader != string::npos)
            {
//auto dwStart = chrono::high_resolution_clock::now();
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
                        if (token != sregex_token_iterator() && token->str().empty() == false)
                            pConDetails->HeaderList.emplace_back(make_pair(":method", token++->str()));
                        if (token != sregex_token_iterator() && token->str().empty() == false)
                            pConDetails->HeaderList.emplace_back(make_pair(":path", token++->str()));
                        if (token != sregex_token_iterator() && token->str().empty() == false)
                        {
                            const auto parResult = pConDetails->HeaderList.emplace(pConDetails->HeaderList.end(), make_pair(":version", token++->str()));
                            if (parResult != end(pConDetails->HeaderList))
                                parResult->second.erase(0, parResult->second.find_first_of('.') + 1);
                        }

                        if (pConDetails->HeaderList.size() != 3)    // The first line should have 3 part. method, path and HTTP/1.x version
                        {
                            pConDetails->HeaderList.emplace_back(make_pair(":1stline", strLine));
                            SendErrorRespons(pTcpSocket, pConDetails->pTimer, 400, HTTPVERSION11, pConDetails->HeaderList);
                            pTcpSocket->Close();
                            m_mtxConnections.unlock();
                            return;
                        }
                    }
                    else
                    {
                        const size_t nPos1 = line->str().find(':');
                        if (nPos1 != string::npos)
                        {
                            string strTmp = line->str().substr(0, nPos1);
                            transform(begin(strTmp), begin(strTmp) + nPos1, begin(strTmp), [](char c) noexcept { return static_cast<char>(::tolower(c)); });

                            const auto parResult = pConDetails->HeaderList.emplace(pConDetails->HeaderList.end(), make_pair(strTmp, line->str().substr(nPos1 + 1)));
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
                        {   // header without a : char are a bad request
                            parLastHeader = end(pConDetails->HeaderList);
                            SendErrorRespons(pTcpSocket, pConDetails->pTimer, 400, 0, pConDetails->HeaderList);
                            pTcpSocket->Close();
                            m_mtxConnections.unlock();
                            return;
                        }
                    }
                    ++line;
                }
                pConDetails->strBuffer.erase(0, nPosEndOfHeader + 4);

                const auto expect = pConDetails->HeaderList.find("expect");
                if (expect != end(pConDetails->HeaderList))
                {
                    if (expect->second == "100-continue")
                        pTcpSocket->Write("HTTP/1.1 100 Continue\r\n\r\n", 25);
                    else
                    {   // other expect is not yet supported
                        SendErrorRespons(pTcpSocket, pConDetails->pTimer, 417, 0, pConDetails->HeaderList);
                        pTcpSocket->Close();
                        m_mtxConnections.unlock();
                        return;
                    }
                }

                // set end of data signal preliminary
                pConDetails->mutReqData->lock();
                pConDetails->vecReqData.emplace_back(unique_ptr<char[]>(nullptr));
                pConDetails->mutReqData->unlock();

                // is data (body) on the request, only HTTP/1.1
                const auto contentLength = pConDetails->HeaderList.find("content-length");
                if (contentLength != end(pConDetails->HeaderList) && contentLength->second != "18446744073709551615")   // SSTL senden ULONGLONG_MAX as Content-length
                {
                    try
                    {
                        if (stoll(contentLength->second) < 0)
                        {   // other expect is not yet supported
                            SendErrorRespons(pTcpSocket, pConDetails->pTimer, 400, 0, pConDetails->HeaderList);
                            pTcpSocket->Close();
                            m_mtxConnections.unlock();
                            return;
                        }

                        pConDetails->nContentsSoll = stoull(contentLength->second);
                    }
                    catch (const std::exception& /*ex*/)
                    {
                        SendErrorRespons(pTcpSocket, pConDetails->pTimer, 400, 0, pConDetails->HeaderList);
                        pTcpSocket->Close();
                        m_mtxConnections.unlock();
                        return;
                    }

                    if (pConDetails->nContentsSoll > 0)
                    {
                        pConDetails->mutReqData->lock();
                        pConDetails->vecReqData.clear();    // Clear the end of Data entry

                        const size_t nBytesToWrite = static_cast<size_t>(min(static_cast<uint64_t>(pConDetails->strBuffer.size()), pConDetails->nContentsSoll));
                        if (nBytesToWrite > 0)
                        {
                            pConDetails->vecReqData.push_back(make_unique<char[]>(nBytesToWrite + 4));
                            copy(&pConDetails->strBuffer[0], &pConDetails->strBuffer[nBytesToWrite], pConDetails->vecReqData.back().get() + 4);
                            *reinterpret_cast<uint32_t*>(pConDetails->vecReqData.back().get()) = static_cast<uint32_t>(nBytesToWrite);
//OutputDebugString(wstring(L"1. Datenempfang: " + to_wstring(nBytesToWrite) + L" Bytes\r\n").c_str());
                            pConDetails->nContentRecv = nBytesToWrite;
                            pConDetails->strBuffer.erase(0, nBytesToWrite);
                        }
                        if (pConDetails->nContentRecv == pConDetails->nContentsSoll)    // Last byte of Data, we signal this with a empty vector entry
                        {
                            pConDetails->vecReqData.push_back(unique_ptr<char[]>(nullptr));
//OutputDebugString(wstring(L"Datenempfang sofort beendet\r\n").c_str());
                            pConDetails->nContentRecv = pConDetails->nContentsSoll = 0;
                        }
                        pConDetails->mutReqData->unlock();
                    }
                }
//auto dwDif = chrono::high_resolution_clock::now();
//MyTrace("Time in ms for Header parsing ", (chrono::duration<float, chrono::milliseconds::period>(dwDif - dwStart).count()));
            }
            else if (pConDetails->nContentsSoll == 0) // Noch kein End of Header, und kein Content empfangen
            {
                m_mtxConnections.unlock();
                return;
            }

            uint32_t nStreamId = 0;
            const auto upgradeHeader = pConDetails->HeaderList.find("upgrade");
            if (upgradeHeader != end(pConDetails->HeaderList) && upgradeHeader->second == "h2c")
            {
                const auto http2SettingsHeader = pConDetails->HeaderList.find("http2-settings");
                if (http2SettingsHeader != end(pConDetails->HeaderList))
                {
                    string strHttp2Settings = Base64::Decode(http2SettingsHeader->second, true);
                    size_t nHeaderLen = strHttp2Settings.size();

                    const MetaSocketData soMetaDa { pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer<TcpSocket>::Reset, pConDetails->pTimer), bind(&Timer<TcpSocket>::SetNewTimeout, pConDetails->pTimer, _1) };

                    pTcpSocket->Write("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: h2c\r\n\r\n", 71);
                    pTcpSocket->Write("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x4\x0\x10\x0\x0\x0\x5\x0\x0\x40\x0", 21);// SETTINGS frame (4) with ParaID(4) and 1048576 Value + ParaID(5) and 16384 Value
                    pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\xf\x0\x1", 13);       // WINDOW_UPDATE frame (8) with value ?1048576? (minus 65535) == 983041
                    nStreamId = 1;

                    size_t nRet;
                    if (nRet = Http2StreamProto(soMetaDa, &strHttp2Settings[0], nHeaderLen, pConDetails->lstDynTable, pConDetails->StreamParam, pConDetails->H2Streams, ref(*pConDetails->mutStreams.get()), pConDetails->StreamResWndSizes, ref(*pConDetails->atStop.get()), ref(pConDetails->lstAuthInfo)), nRet == SIZE_MAX)
                    {
                        *pConDetails->atStop.get() = true;
                        // After a GOAWAY we terminate the connection
                        soMetaDa.fSocketClose();
                        m_mtxConnections.unlock();
                        return;
                    }
                    else if (nHeaderLen != 0)
                        pConDetails->strBuffer.insert(0, strHttp2Settings.substr(strHttp2Settings.size() - nHeaderLen));

                }
            }

            if (pConDetails->bIsH2Con == false)  // If we received or send no GOAWAY Frame in HTTP/2 we end up here, and send the response to the request how made the upgrade
            {
                const MetaSocketData soMetaDa { pTcpSocket->GetClientAddr(), pTcpSocket->GetClientPort(), pTcpSocket->GetInterfaceAddr(), pTcpSocket->GetInterfacePort(), pTcpSocket->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer<TcpSocket>::Reset, pConDetails->pTimer), bind(&Timer<TcpSocket>::SetNewTimeout, pConDetails->pTimer, _1) };

                pConDetails->mutStreams->lock();
                const uint32_t nNextId = static_cast<uint32_t>(pConDetails->H2Streams.size());
                pConDetails->H2Streams.emplace(nNextId, STREAMITEM({ 0, deque<DATAITEM>(), move(pConDetails->HeaderList), 0, 0, INITWINDOWSIZE(pConDetails->StreamParam), make_shared<mutex>(), {} }));
                pConDetails->mutStreams->unlock();
                //m_mtxConnections.unlock();
                //DoAction(soMetaDa, nStreamId, pConDetails->H2Streams, pConDetails->StreamParam, pConDetails->mutStreams.get(), pConDetails->StreamResWndSizes, bind(nStreamId != 0 ? &CHttpServ::BuildH2ResponsHeader : &CHttpServ::BuildResponsHeader, this, _1, _2, _3, _4, _5, _6), pConDetails->atStop.get());
                m_ActThrMutex.lock();
                thread thTemp(&CHttpServ::DoAction, this, soMetaDa, static_cast<uint8_t>(1), nNextId, ref(pConDetails->H2Streams), ref(pConDetails->StreamParam), ref(*pConDetails->mutStreams.get()), ref(pConDetails->StreamResWndSizes), bind(nStreamId != 0 ? &CHttpServ::BuildH2ResponsHeader : &CHttpServ::BuildResponsHeader, this, _1, _2, _3, _4, _5, _6), ref(*pConDetails->atStop.get()), ref(*pConDetails->mutReqData.get()), ref(pConDetails->vecReqData), ref(pConDetails->lstAuthInfo));
                m_umActionThreads.emplace(thTemp.get_id(), *pConDetails->atStop.get());
                m_ActThrMutex.unlock();
                thTemp.detach();

                if (nStreamId != 0)
                    pConDetails->bIsH2Con = true;

                /*
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
                return;*/
            }
        }
        m_mtxConnections.unlock();
    }
}

void CHttpServ::OnSocketError(BaseSocket* const pBaseSocket)
{
    MyTrace("Error: Network error ", pBaseSocket->GetErrorNo());

    string szHost;

    m_mtxConnections.lock();
    const auto item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
    if (item != end(m_vConnections))
    {
        item->second.pTimer->Stop();
        *item->second.atStop.get() = true;

        auto host = item->second.HeaderList.find("host");
        if (host == end(item->second.HeaderList))
            host = item->second.HeaderList.find(":authority");
        if (host != end(item->second.HeaderList))
        {
            const string strTmp = host->second + (host->second.find(":") == string::npos ? (":" + to_string(GetPort())) : "");
            if (m_vHostParam.find(strTmp) != end(m_vHostParam))
                szHost = strTmp;
        }
    }
    m_mtxConnections.unlock();

    if (m_vHostParam[szHost].m_strErrLog.empty() == false)
    {
        TcpSocket* pTcpSocket = dynamic_cast<TcpSocket*>(pBaseSocket);
        CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", (pTcpSocket != nullptr ? pTcpSocket->GetClientAddr() : "0.0.0.0"), "] network error no.: ", pBaseSocket->GetErrorNo(), " @ ", pBaseSocket->GetErrorLoc());
    }

    pBaseSocket->Close();
}

void CHttpServ::OnSocketClosing(BaseSocket* const pBaseSocket)
{
    //OutputDebugString(L"CHttpServ::OnSocketClosing\r\n");
    m_mtxConnections.lock();
    auto item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
    if (item != end(m_vConnections))
    {
        Timer<TcpSocket>* pTimer = item->second.pTimer.get();
        m_mtxConnections.unlock();
        pTimer->Stop();
        while (pTimer->IsStopped() == false)
        {
            this_thread::sleep_for(chrono::microseconds(1));
            pTimer->Stop();
        }

        m_mtxConnections.lock();
        item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            // we wait, until all action thread's are finished for this connection, otherwise we remove the connection while the action thread is still using it = crash
            m_ActThrMutex.lock();
            for (auto iter = begin(m_umActionThreads); iter != end(m_umActionThreads);)
            {
                if (&iter->second == item->second.atStop.get())
                {
                    iter->second = true;   // Stop the DoAction thread
                    m_ActThrMutex.unlock();
                    m_mtxConnections.unlock();
                    this_thread::sleep_for(chrono::milliseconds(1));
                    m_mtxConnections.lock();
                    m_ActThrMutex.lock();
                    item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
                    if (item == end(m_vConnections))
                        break;
                    iter = begin(m_umActionThreads);
                    continue;
                }
                ++iter;
            }
            m_ActThrMutex.unlock();

            if (item != end(m_vConnections))
                m_vConnections.erase(item->first);

            s_mxIpConnect.lock();
            auto itIpItem = s_lstIpConnect.find(reinterpret_cast<TcpSocket*>(pBaseSocket)->GetClientAddr());
            if (itIpItem != s_lstIpConnect.end())
            {
                itIpItem->second--;
                if (itIpItem->second == 0)
                    s_lstIpConnect.erase(itIpItem);
            }
            s_mxIpConnect.unlock();
        }
    }
    m_mtxConnections.unlock();
}

void CHttpServ::OnTimeout(const Timer<TcpSocket>* const /*pTimer*/, TcpSocket* vpData)
{
    const lock_guard<mutex> lock(m_mtxConnections);
    const auto item = m_vConnections.find(vpData);
    if (item != end(m_vConnections))
    {
        if (item->second.nContentsSoll != 0 && item->second.nContentRecv < item->second.nContentsSoll)  // File upload in progress HTTP/1.1
            SendErrorRespons(vpData, item->second.pTimer, 408, 0, item->second.HeaderList);

        if (item->second.bIsH2Con == true)
            Http2Goaway(bind(&TcpSocket::Write, vpData, _1, _2), 0, 0, 0);    // 0 = Graceful shutdown

        *item->second.atStop.get() = true;
        vpData->Close();
    }
}

size_t CHttpServ::BuildH2ResponsHeader(uint8_t* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize/* = 0*/)
{
    size_t nHeaderSize = 0;
    size_t nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, ":status", to_string(iRespCode).c_str());
    if (nReturn == SIZE_MAX)
        return 0;
    nHeaderSize += nReturn;

    const auto in_time_t = chrono::system_clock::to_time_t(chrono::system_clock::now());

    stringstream strTemp;
    strTemp.imbue(m_cLocal);
    strTemp << put_time(::gmtime(&in_time_t), "%a, %d %b %Y %H:%M:%S GMT");

    nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "date", strTemp.str().c_str());
    if (nReturn == SIZE_MAX)
        return 0;
    nHeaderSize += nReturn;

    nReturn = HPackEncode(szBuffer + nHeaderSize, nBufLen - nHeaderSize, "server", SERVERSIGNATUR);
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
        transform(begin(strHeaderFiled), end(strHeaderFiled), begin(strHeaderFiled), [](char c) noexcept { return static_cast<char>(::tolower(c)); });
        if (strHeaderFiled == "cache-control")
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

    return nHeaderSize;
}

size_t CHttpServ::BuildResponsHeader(uint8_t* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize/* = 0*/)
{
    string strRespons;
    strRespons.reserve(2048);
    //strRespons.imbue(m_cLocal);
    strRespons += "HTTP/1.";
    strRespons += ((iFlag & HTTPVERSION11) == HTTPVERSION11 ? "1 " : "0 ") + to_string(iRespCode) + " ";

    const auto& itRespText = RespText.find(iRespCode);
    strRespons += itRespText != end(RespText) ? itRespText->second : "undefined";
    strRespons += "\r\n";
    strRespons += "Server: ";
    strRespons += SERVERSIGNATUR;
    strRespons += "\r\n";

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
        string strHeaderFiled(item.first);
        transform(begin(strHeaderFiled), end(strHeaderFiled), begin(strHeaderFiled), [](char c) noexcept { return static_cast<char>(::tolower(c)); });

        if (strHeaderFiled == "pragma" || strHeaderFiled == "cache-control")
            iFlag &= ~ADDNOCACHE;
        strRespons += item.first + ": " + item.second + "\r\n";
    }

    if (iFlag & ADDNOCACHE)
    {
        strRespons += (iFlag & HTTPVERSION11) == HTTPVERSION11 ? "" : "Pragma: no-cache\r\n";
        strRespons += "Cache-Control: no-cache\r\nExpires: Mon, 03 Apr 1961 05:00:00 GMT\r\n";
    }

    if (iFlag & ADDCONNECTIONCLOSE)
        strRespons += "Connection: close\r\n";
    else
        strRespons += "Connection: Keep-Alive\r\n";

    if (iFlag & GZIPENCODING)
        strRespons += "Content-Encoding: gzip\r\n";
    else if (iFlag & DEFLATEENCODING)
        strRespons += "Content-Encoding: deflate\r\n";
    else if (iFlag & BROTLICODING)
        strRespons += "Content-Encoding: br\r\n";

    if (iFlag & TERMINATEHEADER)
        strRespons += "\r\n";

    const size_t nRet = strRespons.size();
    if (nRet > nBufLen)
        return 0;

    copy(begin(strRespons), begin(strRespons) + nRet, szBuffer);

    return nRet;
}

string CHttpServ::LoadErrorHtmlMessage(HeadList& HeaderList, int iRespCode, const wstring& strMsgDir)
{
    bool bSend = false;
    const auto& accept = HeaderList.find("accept");
    if (accept != end(HeaderList))
    {
        const static regex s_rxSepComma("\\s*,\\s*");
        vector<string> token(sregex_token_iterator(begin(accept->second), end(accept->second), s_rxSepComma, -1), sregex_token_iterator());
        for (size_t n = 0; n < token.size(); ++n)
        {
            if (token[n] == "text/html")
            {
                bSend = true;
                break;
            }
        }
    }

    if (accept == end(HeaderList) || bSend == true) // No accept header -> accept all
    {
        ifstream src(FN_STR(strMsgDir + to_wstring(iRespCode) + L".html"), ios::in | ios::binary);
        if (src.is_open() == true)
        {
            stringstream ssIn;
            copy(istreambuf_iterator<char>(src), istreambuf_iterator<char>(), ostreambuf_iterator<char>(ssIn));
            src.close();
            return ssIn.str();
        }
    }

    return string();
}

void CHttpServ::SendErrorRespons(TcpSocket* const pTcpSocket, const shared_ptr<Timer<TcpSocket>> pTimer, int iRespCode, int iFlag, HeadList& HeaderList, HeadList umHeaderList/* = HeadList()*/)
{
    if (HeaderList.find(":version") != end(HeaderList) && HeaderList.find(":version")->second == "1")
        iFlag |= HTTPVERSION11;

    string szHost;
    const auto& host = HeaderList.find("host");   // Get the Host Header from the request
    if (host != end(HeaderList))
    {
        const string strTmp = host->second + (host->second.find(":") == string::npos ? (":" + to_string(GetPort())) : "");
        if (m_vHostParam.find(strTmp) != end(m_vHostParam))   // If we have it in our configuration, we use the host parameter for logging
            szHost = strTmp;
    }

    const string strHtmlRespons = LoadErrorHtmlMessage(HeaderList, iRespCode, m_vHostParam[szHost].m_strMsgDir.empty() == false ? m_vHostParam[szHost].m_strMsgDir : L"./msg/");
    umHeaderList.insert(end(umHeaderList), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

    constexpr uint32_t nBufSize = 1024;
    const unique_ptr<uint8_t[]> pBuffer = make_unique<uint8_t[]>(nBufSize);
    uint8_t* caBuffer = pBuffer.get();
    const size_t nHeaderLen = BuildResponsHeader(caBuffer, nBufSize, iFlag | TERMINATEHEADER | ADDCONNECTIONCLOSE, iRespCode, umHeaderList, strHtmlRespons.size());
    pTcpSocket->Write(caBuffer, nHeaderLen);
    if (strHtmlRespons.size() > 0)
        pTcpSocket->Write(strHtmlRespons.c_str(), strHtmlRespons.size());
    pTimer.get()->Reset();

    if (HeaderList.find(":method") != end(HeaderList) && HeaderList.find(":path") != end(HeaderList))
    {
        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << pTcpSocket->GetClientAddr() << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << HeaderList.find(":method")->second << " " << HeaderList.find(":path")->second << " HTTP/1.1\" " << to_string(iRespCode) << " - \""
            << (HeaderList.find("referer") != end(HeaderList) ? HeaderList.find("referer")->second : "-") << "\" \""
            << (HeaderList.find("user-agent") != end(HeaderList) ? HeaderList.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;
    }
    else if (HeaderList.find(":1stline") != end(HeaderList))
    {
        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << pTcpSocket->GetClientAddr() << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << HeaderList.find(":1stline")->second << "\" " << to_string(iRespCode) << " - \""
            << (HeaderList.find("referer") != end(HeaderList) ? HeaderList.find("referer")->second : "-") << "\" \""
            << (HeaderList.find("user-agent") != end(HeaderList) ? HeaderList.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;
    }

    CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", pTcpSocket->GetClientAddr(), "] ", RespText.find(iRespCode) != end(RespText) ? RespText.find(iRespCode)->second : "");
}

void CHttpServ::SendErrorRespons(const MetaSocketData& soMetaDa, const uint8_t httpVers, const uint32_t nStreamId, function<size_t(uint8_t*, size_t, int, int, HeadList, uint64_t)> BuildRespHeader, int iRespCode, int iFlag, const string& strHttpVersion, HeadList& HeaderList, HeadList umHeaderList/* = HeadList()*/)
{
    string szHost;
    const auto& host = HeaderList.find("host");   // Get the Host Header from the request
    if (host != end(HeaderList))
    {
        const string strTmp = host->second + (host->second.find(":") == string::npos ? (":" + to_string(GetPort())) : "");
        if (m_vHostParam.find(strTmp) != end(m_vHostParam))   // If we have it in our configuration, we use the host parameter for logging
            szHost = strTmp;
    }

    const string strHtmlRespons = LoadErrorHtmlMessage(HeaderList, iRespCode, m_vHostParam[szHost].m_strMsgDir.empty() == false ? m_vHostParam[szHost].m_strMsgDir : L"./msg/");
    umHeaderList.insert(end(umHeaderList), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

    const uint32_t nHttp2Offset = httpVers == 2 ? 9 : 0;
    constexpr uint32_t nBufSize = 1024;
    const unique_ptr<uint8_t[]> pBuffer = make_unique<uint8_t[]>(nBufSize);
    uint8_t* caBuffer = pBuffer.get();
    const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, /*iHeaderFlag | ADDNOCACHE |*/ iFlag | TERMINATEHEADER | ADDCONNECTIONCLOSE, iRespCode, umHeaderList, strHtmlRespons.size());
    if (nHeaderLen < 1024)
    {
        if (httpVers == 2)
            BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, strHtmlRespons.size() == 0 ? 0x5 : 0x4, nStreamId);
        soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
        if (strHtmlRespons.size() > 0)
        {
            if (httpVers == 2)
            {
                BuildHttp2Frame(caBuffer, strHtmlRespons.size(), 0x0, 0x1, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHttp2Offset);
            }
            soMetaDa.fSocketWrite(strHtmlRespons.c_str(), strHtmlRespons.size());
        }
    }
    soMetaDa.fResetTimer();

    if (HeaderList.find(":method") != end(HeaderList) && HeaderList.find(":path") != end(HeaderList))
    {
        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << HeaderList.find(":method")->second << " " << HeaderList.find(":path")->second << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion << "\" " << to_string(iRespCode) << " - \""
            << (HeaderList.find("referer") != end(HeaderList) ? HeaderList.find("referer")->second : "-") << "\" \""
            << (HeaderList.find("user-agent") != end(HeaderList) ? HeaderList.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;
    }

    CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] ", RespText.find(iRespCode) != end(RespText) ? RespText.find(iRespCode)->second : "");
}

void CHttpServ::DoAction(const MetaSocketData soMetaDa, const uint8_t httpVers, const uint32_t nStreamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex& pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, function<size_t(uint8_t*, size_t, int, int, const HeadList&, uint64_t)> BuildRespHeader, atomic<bool>& patStop, mutex& pmtxReqdata, deque<unique_ptr<char[]>>& vecData, deque<AUTHITEM>& lstAuthInfo)
{
    const static unordered_map<string, int> arMethoden = { {"GET", 0}, {"HEAD", 1}, {"POST", 2}, {"OPTIONS", 3} };

    function<void()> fuExitDoAction = [&]()
    {
        if (httpVers == 2)
        {
            const lock_guard<mutex> lock(pmtxStream);
            const auto StreamItem = StreamList.find(nStreamId);
            if (StreamItem != end(StreamList))
                STREAMSTATE(StreamItem) |= RESET_STREAM;    // StreamList.erase(StreamItem);
        }
        const lock_guard<mutex> lock(m_ActThrMutex);
        while (m_umActionThreads.find(this_thread::get_id()) != end(m_umActionThreads))
        {
            m_umActionThreads.erase(this_thread::get_id());
            this_thread::sleep_for(chrono::milliseconds(1));
        }

    };

    auto fnIsStreamReset = [&](uint32_t nId) -> bool
    {
        if (httpVers < 2) return false;
        const lock_guard<mutex> lock(pmtxStream);
        const auto StreamItem = StreamList.find(nId);
        if (StreamItem != end(StreamList))
            return ((STREAMSTATE(StreamItem) & RESET_STREAM) == RESET_STREAM ? true : false);
        return true;
    };

    auto fnSendQueueReady = [&](int64_t& nStreamWndSize, size_t& nSendBufLen, uint64_t nBufSize, uint64_t nRestLenToSend) -> bool
    {
        bool bRet = true;
        if (soMetaDa.fSockGetOutBytesInQue() >= 0x200000 || nStreamWndSize <= 0)
        {
            nSendBufLen = 0;
            bRet = false;
        }
        else
            nSendBufLen = min(min(nBufSize, nRestLenToSend), static_cast<uint64_t>(nStreamWndSize));

        if (httpVers == 2)
        {
            // Clean up the list of reserved window sizes for the next send
            const lock_guard<mutex> lock(pmtxStream);
            const auto it = maResWndSizes.find(nStreamId);
            if (it != end(maResWndSizes))
            {
                maResWndSizes[0] -= it->second;
                maResWndSizes.erase(it);
            }

            if (nSendBufLen > 0)
            {
                maResWndSizes[0] += static_cast<uint64_t>(nSendBufLen);
                maResWndSizes[nStreamId] = static_cast<uint64_t>(nSendBufLen);
            }
        }

        if (bRet == false)
            this_thread::sleep_for(chrono::microseconds(5));

        return bRet;
    };

    auto fnGetStreamWindowSize = [&](int64_t& iStreamWndSize) -> bool
    {
        if (httpVers == 2)
        {
            int64_t iTotaleWndSize = UINT16_MAX;

            const lock_guard<mutex> lock(pmtxStream);
            // Clean up the list of reserved window sizes for the next send
            const auto it = maResWndSizes.find(nStreamId);
            if (it != end(maResWndSizes))
            {
                maResWndSizes[0] -= it->second;
                maResWndSizes.erase(it);
            }

            auto StreamItem = StreamList.find(nStreamId);
            if (StreamItem != end(StreamList))
                iStreamWndSize = WINDOWSIZE(StreamItem);
            else
                return false;  // Stream Item was removed, probably the stream was reset
            StreamItem = StreamList.find(0);
            if (StreamItem != end(StreamList))
                iTotaleWndSize = WINDOWSIZE(StreamItem) - maResWndSizes[0];
            else
                return false;  // Stream Item was removed, probably the stream was reset
            iStreamWndSize = min(iStreamWndSize, iTotaleWndSize);

            if (iStreamWndSize > 0)
            {
                maResWndSizes[0] += iStreamWndSize;
                maResWndSizes[nStreamId] = iStreamWndSize;
            }
        }
        return true;
    };

    auto fnUpdateStreamParam = [&](size_t& nSendBufLen) -> int
    {
        if (httpVers == 2)
        {
            lock_guard<mutex> lock(pmtxStream);

            const auto it = maResWndSizes.find(nStreamId);
            if (it != end(maResWndSizes))
            {
                maResWndSizes[0] -= it->second;
                maResWndSizes.erase(it);
            }

            auto StreamItem = StreamList.find(nStreamId);
            if (StreamItem != end(StreamList))
                WINDOWSIZE(StreamItem) -= static_cast<uint32_t>(nSendBufLen);
            else
                return -1;  // Stream Item was removed, probably the stream was reset
            StreamItem = StreamList.find(0);
            if (StreamItem != end(StreamList))
                WINDOWSIZE(StreamItem) -= static_cast<uint32_t>(nSendBufLen);
            else
                return -1;  // Stream Item was removed, probably the stream was reset
        }
        return 0;
    };

    auto fnResetReservierteWindowSize = [&]()
    {
        if (httpVers == 2)
        {
            lock_guard<mutex> lock(pmtxStream);
            // Clean up the list of reserved window sizes for the next send
            const auto it = maResWndSizes.find(nStreamId);
            if (it != end(maResWndSizes))
            {
                maResWndSizes[0] -= it->second;
                maResWndSizes.erase(it);
            }
        }
    };

    const size_t nHttp2Offset = httpVers == 2 ? 9 : 0;
    const uint32_t nSizeSendBuf = httpVers == 2 ? MAXFRAMESIZE(tuStreamSettings) : 0x4000;
    constexpr uint32_t nBufSize = 4096;
    unique_ptr<uint8_t[]> pBuffer = make_unique<uint8_t[]>(nBufSize);
    uint8_t* caBuffer = pBuffer.get();
    int iHeaderFlag = 0;
    string strHttpVersion("0");

    pmtxStream.lock();
    if (StreamList.size() == 0 || StreamList.find(nStreamId) == end(StreamList))
    {
        pmtxStream.unlock();
        fuExitDoAction();
        return;
    }
    HeadList& lstHeaderFields = GETHEADERLIST(StreamList.find(nStreamId));
    pmtxStream.unlock();

    string szHost;
    string strHostRedirect;
    auto host = lstHeaderFields.find("host");
    if (host == end(lstHeaderFields))
        host = lstHeaderFields.find(":authority");
    if (host != end(lstHeaderFields))
    {
        strHostRedirect = host->second;
        string strTmp = host->second + (host->second.find(":") == string::npos ? (":" + to_string(GetPort())) : "");
        if (m_vHostParam.find(strTmp) != end(m_vHostParam))
            szHost = strTmp;
    }

    const auto itVersion = lstHeaderFields.find(":version");
    if (itVersion != end(lstHeaderFields))
    {
        strHttpVersion = itVersion->second;
        if (strHttpVersion == "1")
            iHeaderFlag = HTTPVERSION11;
    }

    const auto connection = lstHeaderFields.find("connection");
    if (connection != end(lstHeaderFields) && connection->second == "close")
        iHeaderFlag |= ADDCONNECTIONCLOSE;

    bool bCloseConnection = ((iHeaderFlag & HTTPVERSION11) == 0 || (iHeaderFlag & ADDCONNECTIONCLOSE) == ADDCONNECTIONCLOSE) && httpVers < 2 ? true : false; // if HTTP/1.0 and not HTTP/2.0 close connection after data is send
    if (bCloseConnection == true)
        iHeaderFlag |= ADDCONNECTIONCLOSE;

    auto itMethode = lstHeaderFields.find(":method");
    auto itPath = lstHeaderFields.find(":path");
    if (itMethode == end(lstHeaderFields) || itPath == end(lstHeaderFields) || (strHttpVersion.find_first_not_of("01") != string::npos && httpVers < 2))
    {
        SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 400, iHeaderFlag, strHttpVersion, lstHeaderFields);
        if (httpVers < 2)
            soMetaDa.fSocketClose();
        fuExitDoAction();
        return;
    }
    transform(begin(itMethode->second), end(itMethode->second), begin(itMethode->second), [](char c) noexcept { return static_cast<char>(::toupper(c)); });

    // Decode URL (%20 -> ' ')
    wstring strItemPath;
    string strQuery;
    const size_t nLen = itPath->second.size();
    wchar_t wch = 0;
    int nAnzParts = 0;
    for (size_t n = 0; n < nLen; ++n)
    {
        int chr = itPath->second.at(n);
        if ('%' == chr)
        {
            if (n + 2 >= nLen || isxdigit(itPath->second.at(n + 1)) == 0 || isxdigit(itPath->second.at(n + 2)) == 0)
            {
                SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 400, iHeaderFlag, strHttpVersion, lstHeaderFields);
                if (httpVers < 2)
                    soMetaDa.fSocketClose();
                fuExitDoAction();
                return;
            }
            const char Nipple1 = itPath->second.at(n + 1) - (itPath->second.at(n + 1) <= '9' ? '0' : (itPath->second.at(n + 1) <= 'F' ? 'A' : 'a') - 10);
            const char Nipple2 = itPath->second.at(n + 2) - (itPath->second.at(n + 2) <= '9' ? '0' : (itPath->second.at(n + 2) <= 'F' ? 'A' : 'a') - 10);
            chr = 16 * Nipple1 + Nipple2;
            n += 2;

            if (chr < 0x20) // unprintable character
            {
                SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 400, iHeaderFlag, strHttpVersion, lstHeaderFields);
                if (httpVers < 2)
                    soMetaDa.fSocketClose();
                fuExitDoAction();
                return;
            }
            else if (chr < 0x7f)
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
        else if ('?' == chr)
        {
            strQuery = itPath->second.substr(n + 1);
            break;
        }
        else
            strItemPath += static_cast<wchar_t>(itPath->second.at(n));
    }

    wstring strOrgRequest(strItemPath);
    string strOrgQuery(strQuery);

    // Check for RewriteRule
    for (auto& strRule : m_vHostParam[szHost].m_mstrRewriteRule)
    {
        strItemPath = regex_replace(strItemPath, wregex(strRule.first), strRule.second, regex_constants::format_first_only);
    }

    // In case the RewriteRule adds something to the Query_String
    const size_t nPos = strItemPath.find_first_of(L'?');
    if (nPos != string::npos)
    {
        strQuery += (strQuery.empty() == false ? "&" : "") + wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strItemPath.substr(nPos + 1));
        strItemPath.erase(nPos);
    }

    // Check for redirect
    for (auto& tuRedirect : m_vHostParam[szHost].m_vRedirMatch)    // RedirectMatch
    {
        wregex rx(get<1>(tuRedirect));
        if (regex_match(strItemPath, rx) == true)
        {
            wstring strLocation = regex_replace(strItemPath, rx, get<2>(tuRedirect), regex_constants::format_first_only);
            if (strHostRedirect == "")
                strLocation = regex_replace(strLocation, wregex(L"\\%\\{SERVER_NAME\\}"), wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().from_bytes(soMetaDa.strIpInterface));
            else
                strLocation = regex_replace(strLocation, wregex(L"\\%\\{SERVER_NAME\\}"), wstring(begin(strHostRedirect), end(strHostRedirect)));

            HeadList redHeader({ make_pair("Location", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strLocation)) });
            redHeader.insert(end(redHeader), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

            const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 307, redHeader, 0);
            if (nHeaderLen < nBufSize)
            {
                if (httpVers == 2)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            }
            soMetaDa.fResetTimer();
            if (httpVers < 2)
                soMetaDa.fSocketClose();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                << "\" 307 -" << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;

            fuExitDoAction();
            return;
        }
    }

    vector<pair<string, string>> vStrEnvVariable;
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
                bFound = regex_match(soMetaDa.strIpClient, regex(wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<1>(strEnvIf))));
                break;
            case 6: // REQUEST_URI
                bFound = regex_match(strItemPath, wregex(get<1>(strEnvIf)));
                break;
            }

            if (bFound == true)
            {
                string strTmp = wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<2>(strEnvIf));
                const size_t nPosEqual = strTmp.find("=");
                auto itFound = find_if(begin(vStrEnvVariable), end(vStrEnvVariable), [&strTmp, &nPosEqual](const auto& itItem) noexcept { return itItem.first == strTmp.substr(0, nPosEqual); });
                if (itFound == end(vStrEnvVariable))
                    vStrEnvVariable.emplace_back(make_pair(strTmp.substr(0, nPosEqual), nPosEqual != string::npos ? strTmp.substr(nPosEqual + 1) : ""));
                else
                    itFound->second = nPosEqual != string::npos ? strTmp.substr(nPosEqual + 1) : "";

                transform(begin(strTmp), end(strTmp), begin(strTmp), [](char c) noexcept { return static_cast<char>(::toupper(c)); });
                if (strTmp == "DONTLOG")
                    CLogFile::SetDontLog();
            }
        }
    }

    // Test for forbidden element /.. \.. ../ ..\ /aux /lpt /com .htaccess .htpasswd .htgroup
    const static wregex rxForbidden(L"/\\.\\.|\\\\\\.\\.|\\.\\./|\\.\\.\\\\|/aux$|/noel$|/prn$|/con$|/lpt[0-9]+$|/com[0-9]+$|\\.htaccess|\\.htpasswd|\\.htgroup", regex_constants::ECMAScript | regex_constants::icase);
    if (regex_search(strItemPath.c_str(), rxForbidden) == true || strItemPath == L"." || strItemPath == L"..")
    {
        SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 403, iHeaderFlag, strHttpVersion, lstHeaderFields);
        if (httpVers < 2)
            soMetaDa.fSocketClose();
        fuExitDoAction();
        return;
    }

    bool bAuthHandlerInScript = false;
    for (auto& strAlias : m_vHostParam[szHost].m_vAuthHandler)
    {
        if (regex_match(strItemPath, wregex(strAlias)) == true)
        {
            bAuthHandlerInScript = true;
            break;
        }
    }

    wstring strRemoteUser;
    // Check for Authentication
    if (bAuthHandlerInScript == false)
    {
        for (auto& strAuth : m_vHostParam[szHost].m_mAuthenticate)
        {
            match_results<wstring::const_iterator> mr;
            if (regex_search(strItemPath, mr, wregex(strAuth.first), regex_constants::format_first_only) == true && strItemPath.substr(0, mr.str().size()) == mr.str())
            {
                const auto fnSendAuthRespons = [&]() -> void
                {
                    HeadList vHeader;
                    if (get<1>(strAuth.second).find(L"DIGEST") != string::npos)
                    {
                        const auto in_time_t = chrono::system_clock::to_time_t(chrono::system_clock::now());
                        auto duration = chrono::system_clock::now().time_since_epoch();
                        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                        string strNonce = md5(to_string(in_time_t) + "." + to_string(millis) + ":" + soMetaDa.strIpClient + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strItemPath));
                        strNonce = Base64::Encode(strNonce.c_str(), strNonce.size());
                        if (httpVers < 2)
                        {   // most browser to not suport rfc 7616
                            //vHeader.push_back(make_pair("WWW-Authenticate", "Digest realm=\"" + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<0>(strAuth.second)) + "\",\r\n\tqop=\"auth,auth-int\",\r\n\talgorithm=SHA-256,\r\n\tnonce=\"" + strNonce + "\",\r\n\topaque=\"rc7tZXhKlemRvbW9wYXFGddjluZw\""));
                            //vHeader.push_back(make_pair("WWW-Authenticate", "Digest realm=\"" + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<0>(strAuth.second)) + "\",\r\n\tqop=\"auth,auth-int\",\r\n\talgorithm=MD5,\r\n\tnonce=\"" + strNonce + "\",\r\n\topaque=\"rc7tZXhKlemRvbW9wYXFGddjluZw\""));
                            vHeader.push_back(make_pair("WWW-Authenticate", "Digest realm=\"" + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<0>(strAuth.second)) + "\",\r\n\tqop=\"auth,auth-int\",\r\n\tnonce=\"" + strNonce + "\",\r\n\topaque=\"rc7tZXhKlemRvbW9wYXFGddjluZw\""));
                        }
                        else
                        {   // most browser to not suport rfc 7616
                            //vHeader.push_back(make_pair("WWW-Authenticate", "Digest realm=\"" + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<0>(strAuth.second)) + "\",qop=\"auth,auth-int\",algorithm=SHA-256,nonce=\"" + strNonce + "\",opaque=\"rc7tZXhKlemRvbW9wYXFGddjluZw\""));
                            //vHeader.push_back(make_pair("WWW-Authenticate", "Digest realm=\"" + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<0>(strAuth.second)) + "\",qop=\"auth,auth-int\",algorithm=MD5,nonce=\"" + strNonce + "\",opaque=\"rc7tZXhKlemRvbW9wYXFGddjluZw\""));
                            vHeader.push_back(make_pair("WWW-Authenticate", "Digest realm=\"" + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<0>(strAuth.second)) + "\",qop=\"auth,auth-int\",nonce=\"" + strNonce + "\",opaque=\"rc7tZXhKlemRvbW9wYXFGddjluZw\""));
                        }
                    }
                    if (get<1>(strAuth.second).find(L"BASIC") != string::npos)
                        vHeader.push_back(make_pair("WWW-Authenticate", "Basic realm=\"" + wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(get<0>(strAuth.second)) + "\""));

                    string strHtmlRespons = LoadErrorHtmlMessage(lstHeaderFields, 401, m_vHostParam[szHost].m_strMsgDir.empty() == false ? m_vHostParam[szHost].m_strMsgDir : L"./msg/");
                    vHeader.insert(end(vHeader), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

                    const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONENTLENGTH/* | ADDCONNECTIONCLOSE*/, 401, vHeader, strHtmlRespons.size());
                    if (nHeaderLen < nBufSize)
                    {
                        if (httpVers == 2)
                            BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, strHtmlRespons.size() == 0 ? 0x5 : 0x4, nStreamId);
                        soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                        if (strHtmlRespons.size() > 0)
                        {
                            if (httpVers == 2)
                            {
                                BuildHttp2Frame(caBuffer, strHtmlRespons.size(), 0x0, 0x1, nStreamId);
                                soMetaDa.fSocketWrite(caBuffer, nHttp2Offset);
                            }
                            if (fnIsStreamReset(nStreamId) == false)
                                soMetaDa.fSocketWrite(strHtmlRespons.c_str(), strHtmlRespons.size());
                        }
                    }
                    soMetaDa.fResetTimer();

                    CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                        << itMethode->second << " " << lstHeaderFields.find(":path")->second
                        << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                        << "\" 401 " << "-" << " \""
                        << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                        << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                        << CLogFile::LOGTYPES::END;

                    //if (httpVers < 2)
                    //    soMetaDa.fSocketClose();
                    fuExitDoAction();
                };

                const auto itAuth = lstHeaderFields.find("authorization");
                if (itAuth == end(lstHeaderFields))
                {
                    for (auto& itAuthInfo : lstAuthInfo)
                    {
                        if (itAuthInfo.strUrl == mr.str())
                        {
                            strRemoteUser = itAuthInfo.strUser;
                            break;
                        }
                    }
                    if (strRemoteUser.empty() == false)
                        break;
                    return fnSendAuthRespons();
                }

                string::size_type nPosSpace = itAuth->second.find(' ');
                if (nPosSpace == string::npos)
                    return fnSendAuthRespons();
                if (itAuth->second.substr(0, nPosSpace) == "Basic" && get<1>(strAuth.second).find(L"BASIC") != string::npos)
                {
                    wstring strCredential = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(Base64::Decode(itAuth->second.substr(nPosSpace + 1)));
                    if (find_if(begin(get<2>(strAuth.second)), end(get<2>(strAuth.second)), [strCredential](const auto& strUser) noexcept { return strCredential == strUser ? true : false; }) == end(get<2>(strAuth.second)))
                        return fnSendAuthRespons();
                    strRemoteUser = strCredential.substr(0, strCredential.find(L':'));
                    break;
                }
                else if (itAuth->second.substr(0, nPosSpace) == "Digest" && get<1>(strAuth.second).find(L"DIGEST") != string::npos)
                {   // username="Thomas", realm="Http-Utility Digest", nonce="cmFuZG9tbHlnZW5lcmF0ZWRub25jZQ", uri="/iso/", response="2254355340eede0649b9df7f0121dcca", opaque="c29tZXJhbmRvbW9wYXF1ZXN0cmluZw", qop=auth, nc=00000002, cnonce="78a4421707fa60bc"
                    const static regex spaceSeperator(",");
                    string strTmp = itAuth->second.substr(nPosSpace + 1);
                    sregex_token_iterator token(begin(strTmp), end(strTmp), spaceSeperator, -1);
                    map<string, string> maDigest;
                    while (token != sregex_token_iterator())
                    {
                        if (nPosSpace = token->str().find("="), nPosSpace != string::npos)
                        {
                            string strKey = token->str().substr(0, nPosSpace);
                            strKey.erase(strKey.find_last_not_of(" \t") + 1);
                            strKey.erase(0, strKey.find_first_not_of(" \t"));
                            const auto itInsert = maDigest.emplace(strKey, token->str().substr(nPosSpace + 1));
                            if (itInsert.second == true)
                            {
                                itInsert.first->second.erase(itInsert.first->second.find_last_not_of("\" \t") + 1);
                                itInsert.first->second.erase(0, itInsert.first->second.find_first_not_of("\" \t"));
                            }
                        }
                        token++;
                    }

                    wstring strUserName = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(maDigest["username"]);
                    auto item = find_if(begin(get<2>(strAuth.second)), end(get<2>(strAuth.second)), [strUserName](const auto& strUser) { return strUser.substr(0, strUserName.size()) == strUserName && strUser[strUserName.size()] == ':' ? true : false; });
                    if (item == end(get<2>(strAuth.second)))
                        return fnSendAuthRespons();

                    string PassWord = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(item->substr(strUserName.size()));
                    string strM1 = md5(maDigest["username"] + ":" + maDigest["realm"] + PassWord);
                    string strM2 = md5(itMethode->second + ":" + maDigest["uri"]);
                    string strRe = md5(strM1 + ":" + maDigest["nonce"] + ":" + maDigest["nc"] + ":" + maDigest["cnonce"] + ":" + maDigest["qop"] + ":" + strM2);
                    if (strRe != maDigest["response"])
                        return fnSendAuthRespons();
                    strRemoteUser = strUserName;
                    lstAuthInfo.push_back({ strUserName , get<0>(strAuth.second) , maDigest["nonce"], mr.str() });
                    break;
                }
                else
                    return fnSendAuthRespons();
            }
        }
    }

    bool bOptionsHandlerInScript = false;
    for (auto& strAlias : m_vHostParam[szHost].m_vOptionsHandler)
    {
        if (regex_match(strItemPath, wregex(strAlias)) == true)
        {
            bOptionsHandlerInScript = true;
            break;
        }
    }

    // AliasMatch
    bool bNewRootSet = false;
    bool bExecAsScript = false;
    vector<wstring>* pveAliasMatch = nullptr;
    for (auto& strAlias : m_vHostParam[szHost].m_mstrAliasMatch)
    {
        wregex re(strAlias.first);
        wstring strNewPath = regex_replace(strItemPath, re, get<0>(strAlias.second).back(), regex_constants::format_first_only);
        if (strNewPath != strItemPath || regex_match(strItemPath, re) == true)
        {
            strItemPath = strNewPath;
            bNewRootSet = true;
            pveAliasMatch = &get<0>(strAlias.second);
            bExecAsScript = get<1>(strAlias.second);
            break;
        }
    }

    // ReverseProxy
    if (m_vHostParam[szHost].m_mstrReverseProxy.size() > 0)
    {
        wstring strReferer(strItemPath);
        const auto itReferer = lstHeaderFields.find("referer");
        if (itReferer != end(lstHeaderFields))
        {
            const size_t nPosHost = itReferer->second.find(strHostRedirect);
            strReferer = wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(itReferer->second);
            strReferer.erase(0, nPosHost + strHostRedirect.size());
        }

        for (auto& strProxyAlias : m_vHostParam[szHost].m_mstrReverseProxy)
        {
            wregex reProxy(strProxyAlias.first);

            wstring strStriptRef = regex_replace(strReferer, reProxy, L"$1", regex_constants::format_first_only);
            size_t nPosScriptRef = strReferer.find(strStriptRef);
            if (nPosScriptRef != string::npos) nPosScriptRef--;
            wstring strTmp = strReferer.substr(0, nPosScriptRef);
            if (strTmp.empty() == false && strTmp.back() == L'/') strTmp.erase(strTmp.size() - 1);

            wstring strNewPath = regex_replace(strItemPath, reProxy, g_szHttpFetch, regex_constants::format_first_only);
            if (strNewPath != strItemPath)
            {
                vStrEnvVariable.emplace_back(make_pair("PROXYURL", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strProxyAlias.second)));
                if (strTmp.empty() == false)
                    vStrEnvVariable.emplace_back(make_pair("PROXYMARK", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strTmp)));
                strItemPath = strNewPath;
                bNewRootSet = true;
                bExecAsScript = true;
                break;
            }
            else if (regex_search(strReferer, reProxy) == true && regex_search(strItemPath, reProxy) == false)
            {
                HeadList redHeader({ make_pair("Location", wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strTmp) + lstHeaderFields.find(":path")->second) });
                redHeader.insert(end(redHeader), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

                const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER, 307, redHeader, 0);
                if (nHeaderLen < nBufSize)
                {
                    if (httpVers == 2)
                        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                    if (fnIsStreamReset(nStreamId) == false)
                        soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                }
                soMetaDa.fResetTimer();

                CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                    << itMethode->second << " " << lstHeaderFields.find(":path")->second
                    << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                    << "\" 307 -" << " \""
                    << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                    << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                    << CLogFile::LOGTYPES::END;

                fuExitDoAction();
                return;
            }
        }
    }

    // DefaultType
    string strMineType;
    for (auto& strTyp : m_vHostParam[szHost].m_mstrForceTyp)
    {
        wregex rx(strTyp.first);
        if (regex_search(strItemPath, rx, regex_constants::format_first_only) == true)
            strMineType = wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strTyp.second);
    }

    // Get base directory and build filename
    if (bNewRootSet == false && strItemPath != L"*")
        strItemPath = m_vHostParam[szHost].m_strRootPath + strItemPath;

    // replace Slash in Backslash (Windows can handle them too)
    replace(begin(strItemPath), end(strItemPath), L'\\', L'/');

    // Suche PATH_INFO
    wstring strPathInfo;
    static wregex seperator(L"/");
    wsregex_token_iterator token(begin(strItemPath), end(strItemPath), seperator, -1);
    if  (token != wsregex_token_iterator())
    {
        wstring strPath(token->str());  // the first token shut exist
        while (++token, token != wsregex_token_iterator())
        {
            strPath += L"/" + token->str();

            struct _stat64 stFileInfo;
            if (::_wstat64(FN_CA(regex_replace(strPath, wregex(L"\""), L"")), &stFileInfo) == 0 && (stFileInfo.st_mode & _S_IFDIR) == 0) // We have a file, after that we can stop
            {
                strPathInfo = strItemPath.substr(strPath.size());
                strItemPath.erase(strItemPath.size() - strPathInfo.size());
                break;
            }
        }
    }

    // supplement default item
    struct _stat64 stFileInfo;
    int iRet = ::_wstat64(FN_CA(regex_replace(strItemPath, wregex(L"\""), L"")), &stFileInfo);
    if (iRet == 0 && (stFileInfo.st_mode & _S_IFDIR) == _S_IFDIR)
    {
        if (strItemPath.back() != L'/')
        {
            HeadList redHeader({ make_pair("Location", lstHeaderFields.find(":path")->second + '/') });
            redHeader.insert(end(redHeader), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

            const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER, 301, redHeader, 0);
            if (nHeaderLen < nBufSize)
            {
                if (httpVers == 2)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            }
            soMetaDa.fResetTimer();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                << "\" 301 -" << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;

            fuExitDoAction();
            return;
        }

        for (auto& strDefItem : m_vHostParam[szHost].m_vstrDefaultItem)
        {
            if (_waccess(FN_CA((strItemPath + strDefItem)), 0) == 0)
            {
                strItemPath += strDefItem;
                break;
            }
        }
        iRet = ::_wstat64(FN_CA(regex_replace(strItemPath, wregex(L"\""), L"")), &stFileInfo);
    }

    // OPTIONS
    if (strItemPath != L"*" && bExecAsScript == false)
    {
        if (iRet != 0 || (stFileInfo.st_mode & _S_IFDIR) == _S_IFDIR || (stFileInfo.st_mode & _S_IFREG) == 0)
        {
            if (errno == ENOENT || errno == ENAMETOOLONG || (stFileInfo.st_mode & _S_IFDIR) == _S_IFDIR)
                SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 404, iHeaderFlag, strHttpVersion, lstHeaderFields);
            else //EINVAL
                SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 500, iHeaderFlag, strHttpVersion, lstHeaderFields);

            if (httpVers < 2)
                soMetaDa.fSocketClose();

            fuExitDoAction();
            return;
        }
    }

    // Get file type of item
    wstring strFileExtension(strItemPath.substr(strItemPath.rfind(L'.') + 1));
    transform(begin(strFileExtension), end(strFileExtension), begin(strFileExtension), [](wchar_t c) noexcept { return static_cast<wchar_t>(::tolower(c)); });

    uint32_t iStatus = 200;

    const auto itFileTyp = m_vHostParam[szHost].m_mFileTypeAction.find(strFileExtension);
    if ((itFileTyp != end(m_vHostParam[szHost].m_mFileTypeAction) || bExecAsScript == true) && (itMethode->second != "OPTIONS" || bOptionsHandlerInScript == true))
    {
        uint64_t nSollLen = 0;
        const auto itContLen = lstHeaderFields.find("content-length");
        if (itContLen != end(lstHeaderFields))
        {
            try { nSollLen = stoull(itContLen->second); }
            catch (const std::exception& /*ex*/) {}
        }

        vector<pair<string, string>> vCgiParam;
        const static array<pair<const char*, const char*>, 5> caHeaders = { { make_pair(":authority", "HTTP_HOST") , make_pair(":scheme", "REQUEST_SCHEME") , make_pair("content-type", "CONTENT_TYPE"), make_pair("content-length", "CONTENT_LENGTH"), make_pair("authorization", "HTTP_AUTHORIZATION") } };
        for (auto& itHeader : lstHeaderFields)
        {
            if ((bAuthHandlerInScript == false && itHeader.first == "authorization")
            || (nSollLen == 0 && itHeader.first == "content-length"))
                continue;

            auto itArray = find_if(begin(caHeaders), end(caHeaders), [&](auto& prItem) noexcept { return prItem.first == itHeader.first ? true : false; });
            if (itArray != end(caHeaders))
                vCgiParam.emplace_back(make_pair(itArray->second, itHeader.second));
            else if (itHeader.first[0] != ':')
            {
                string strHeader(itHeader.first);
                //strHeader.erase(0, strHeader.find_first_not_of(L':'));
                transform(begin(strHeader), end(strHeader), begin(strHeader), [](char c) noexcept { return static_cast<char>(::toupper(c)); });
                strHeader = regex_replace(strHeader, regex("-"), string("_"));
                vCgiParam.emplace_back(make_pair("HTTP_" + strHeader, itHeader.second));
            }
        }

        vCgiParam.emplace_back(make_pair("SERVER_SOFTWARE", string(SERVERSIGNATUR)));
        vCgiParam.emplace_back(make_pair("REDIRECT_STATUS", "200"));
        vCgiParam.emplace_back(make_pair("REMOTE_ADDR", soMetaDa.strIpClient));
        vCgiParam.emplace_back(make_pair("REMOTE_HOST", soMetaDa.strIpClient));
        vCgiParam.emplace_back(make_pair("SERVER_PORT", to_string(soMetaDa.sPortInterFace)));
        vCgiParam.emplace_back(make_pair("SERVER_ADDR", soMetaDa.strIpInterface));
        vCgiParam.emplace_back(make_pair("SERVER_NAME", strHostRedirect.empty() == false ? strHostRedirect : soMetaDa.strIpInterface));
        vCgiParam.emplace_back(make_pair("REMOTE_PORT", to_string(soMetaDa.sPortClient)));
        vCgiParam.emplace_back(make_pair("SERVER_PROTOCOL", string(httpVers == 2 ? "HTTP/2." : "HTTP/1.") + strHttpVersion));
        vCgiParam.emplace_back(make_pair("DOCUMENT_ROOT", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(m_vHostParam[szHost].m_strRootPath)));
        vCgiParam.emplace_back(make_pair("GATEWAY_INTERFACE", "CGI/1.1"));
        vCgiParam.emplace_back(make_pair("SCRIPT_NAME", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strOrgRequest.substr(0, strOrgRequest.size() - strPathInfo.size()))));
        vCgiParam.emplace_back(make_pair("REQUEST_METHOD", itMethode->second));
        vCgiParam.emplace_back(make_pair("REQUEST_URI", itPath->second));
        if (strPathInfo.empty() == false)
        {
            vCgiParam.emplace_back(make_pair("PATH_INFO", url_encode(wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strPathInfo))));
            vCgiParam.emplace_back(make_pair("PATH_TRANSLATED", url_encode(wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(m_vHostParam[szHost].m_strRootPath + strPathInfo))));
        }
        if (soMetaDa.bIsSsl == true)
            vCgiParam.emplace_back(make_pair("HTTPS", "on"));
        if (strRemoteUser.empty() == false)
            vCgiParam.emplace_back(make_pair("REMOTE_USER", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strRemoteUser)));
        if (strQuery.empty() == false)
            vCgiParam.emplace_back(make_pair("QUERY_STRING", strQuery));
        vCgiParam.emplace_back(make_pair("SCRIPT_FILENAME", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strItemPath)));
        for (const auto& ptEnvVar : vStrEnvVariable)
        {
            vCgiParam.emplace_back(make_pair(ptEnvVar.first, ptEnvVar.second.empty() == false ? ptEnvVar.second : "1"));
        }
        //AUTH_TYPE,

        bool bEndOfHeader = false;
        HeadList umPhpHeaders;
        basic_string<char> strBuffer;
        size_t nTotal = 0;
        size_t nOffset = 0;
        bool bChunkedTransfer = false;

        function<void(uint8_t*, size_t)> fnSendOutput = [&](uint8_t* szBuffer, size_t nBufLen)
        {
            if (bEndOfHeader == false)
            {
                strBuffer.assign(reinterpret_cast<char*>(szBuffer) + nHttp2Offset, nBufLen);
                for (;;)
                {
                    const size_t nPosStart = strBuffer.find_first_of("\r\n");
                    size_t nPosEnd = strBuffer.find_first_not_of("\r\n", nPosStart);
                    if (nPosEnd == string::npos)
                        nPosEnd = nBufLen;

                    if (nPosStart == 0) // second crlf = end of header
                    {
                        // Build response header
                        auto itCgiHeader = umPhpHeaders.ifind("status");
                        if (itCgiHeader == end(umPhpHeaders))
                            itCgiHeader = umPhpHeaders.ifind(":status");
                        if (itCgiHeader != end(umPhpHeaders))
                        {
                            try { iStatus = stoi(itCgiHeader->second); }
                            catch (const std::exception& /*ex*/) { }

                            umPhpHeaders.erase(itCgiHeader);
                        }
                        if (umPhpHeaders.ifind("Transfer-Encoding") == end(umPhpHeaders) && umPhpHeaders.ifind("Content-Length") == end(umPhpHeaders) && httpVers < 2 && strHttpVersion == "1")
                        {
                            umPhpHeaders.emplace_back(make_pair("Transfer-Encoding", "chunked"));
                            bChunkedTransfer = true;
                        }
                        umPhpHeaders.insert(end(umPhpHeaders), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));
                        const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER, iStatus, umPhpHeaders, 0);
                        if (nHeaderLen < nBufSize)
                        {
                            if (httpVers == 2)
                                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                            if (fnIsStreamReset(nStreamId) == false)
                                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                        }
                        soMetaDa.fResetTimer();

                        bEndOfHeader = true;
                        nBufLen -= nPosEnd;
                        strBuffer.erase(0, nPosEnd);
                        strBuffer.copy(reinterpret_cast<char*>(szBuffer) + nHttp2Offset, nBufLen);
                        break;
                    }
                    else if (nPosStart != string::npos)
                    {
                        size_t nPos2 = strBuffer.substr(1, nPosStart).find(":");
                        if (nPos2++ != string::npos)
                        {
                            const auto iter = umPhpHeaders.emplace(umPhpHeaders.end(), make_pair(strBuffer.substr(0, nPos2), strBuffer.substr(nPos2 + 1, nPosStart - (nPos2 + 1))));
                            if (iter != end(umPhpHeaders))
                            {
                                iter->second.erase(0, iter->second.find_first_not_of(" "));
                            }
                        }

                        if (nPosEnd - nPosStart > 2 && strBuffer.compare(nPosStart, 2, "\r\n") == 0)
                            nPosEnd -= 2;
                        else if (nPosEnd - nPosStart > 1 && strBuffer.compare(nPosStart, 2, "\n\n") == 0)
                            nPosEnd -= 1;

                        nBufLen -= nPosEnd;
                        strBuffer.erase(0, nPosEnd);
                    }
                    else
                    {
                        nOffset = nBufLen;
                        strBuffer.copy(reinterpret_cast<char*>(szBuffer) + nHttp2Offset, nBufLen);
                        nBufLen = 0;
                        break;
                    }
                }

                if (nBufLen == 0)
                    return;
            }

            if (httpVers == 2)
            {
                nOffset = min(nBufLen, static_cast<size_t>(10));
                nBufLen -= nOffset;
            }

            size_t nBytesTransferred = 0;
            while (nBytesTransferred < nBufLen && patStop.load() == false)
            {
                int64_t nStreamWndSize = INT32_MAX;
                if (fnGetStreamWindowSize(nStreamWndSize) == false)
                    break;  // Stream Item was removed, probably the stream was reset

                size_t nSendBufLen;
                if (fnSendQueueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), nBufLen - nBytesTransferred) == false)
                    continue;

                if (httpVers == 2)
                    BuildHttp2Frame(szBuffer + nBytesTransferred, nSendBufLen, 0x0, 0x0, nStreamId);
                else if (bChunkedTransfer == true)
                {
                    stringstream ss;
                    ss << hex << ::uppercase << nSendBufLen << "\r\n";
                    soMetaDa.fSocketWrite(ss.str().c_str(), ss.str().size());
                }
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(szBuffer + nBytesTransferred, nSendBufLen + nHttp2Offset);
                if (bChunkedTransfer == true && fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite("\r\n", 2);
                soMetaDa.fResetTimer();

                nBytesTransferred += nSendBufLen;

                if (fnUpdateStreamParam(nSendBufLen) == -1)
                    break;  // Stream Item was removed, probably the stream was reset
            }
            fnResetReservierteWindowSize();

            nTotal += nBytesTransferred;
            copy(szBuffer + nHttp2Offset + nBytesTransferred, szBuffer + nHttp2Offset + nBytesTransferred + nOffset, szBuffer + nHttp2Offset);
        };

        function<void(uint8_t*)> fnAfterCgi = [&](uint8_t* szBuffer)
        {
            if (bEndOfHeader == true && httpVers == 2)
            {
                BuildHttp2Frame(szBuffer, nOffset, 0x0, 0x1, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(szBuffer, nHttp2Offset + nOffset);
                soMetaDa.fResetTimer();
            }
            else if (bEndOfHeader == true && bChunkedTransfer == true)
                soMetaDa.fSocketWrite("0\r\n\r\n", 5);
            else if (bEndOfHeader == false)
            {   // Noch kein Header gesendet
                HeadList tmpHeader;
                tmpHeader.insert(end(tmpHeader), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));
                size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, tmpHeader, 0);
                if (nHeaderLen < nBufSize)
                {
                    if (httpVers == 2)
                        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                    if (fnIsStreamReset(nStreamId) == false)
                        soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
                }
                soMetaDa.fResetTimer();
                if (httpVers < 2)
                    bCloseConnection = true;
            }

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                << "\" " << iStatus << " " << nTotal << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;
        };

        function<void()> fnSendError = [&]()
        {
            HeadList tmpHeader;
            tmpHeader.insert(end(tmpHeader), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));
            const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | ADDNOCACHE | TERMINATEHEADER | ADDCONNECTIONCLOSE, 500, tmpHeader, 0);
            if (nHeaderLen < nBufSize)
            {
                if (httpVers == 2)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            }
            soMetaDa.fResetTimer();
            if (httpVers < 2)
                bCloseConnection = true;

            CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Server error handling: ", itPath->second);
        };

        // Fast-CGI Interface
        if ((itFileTyp != end(m_vHostParam[szHost].m_mFileTypeAction) && itFileTyp->second.size() >= 2 && itFileTyp->second[0].compare(L"fcgi") == 0)
        || (pveAliasMatch != nullptr && pveAliasMatch->size() >= 2 && pveAliasMatch->at(0).compare(L"fcgi") == 0))
        {
            string strIpAdd(wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(itFileTyp != end(m_vHostParam[szHost].m_mFileTypeAction) && bExecAsScript == false ? itFileTyp->second[1] : pveAliasMatch->at(1)));
            uint16_t nPort = 0;
            const size_t nPosColon = strIpAdd.find_first_of(":");

            if (nPosColon != string::npos)
            {
                try { nPort = static_cast<uint16_t>(stoi(strIpAdd.substr(nPosColon + 1))); }
                catch (const std::exception& /*ex*/) { }
            }

            if (nPosColon != string::npos && nPort > 0)
            {
                s_mxFcgi.lock();
                auto itFcgi = s_mapFcgiConnections.find(strIpAdd);
                if (itFcgi == s_mapFcgiConnections.end())
                {
                    s_mapFcgiConnections.emplace(strIpAdd, itFileTyp != end(m_vHostParam[szHost].m_mFileTypeAction) && itFileTyp->second.size() >= 3 ? FastCgiClient(itFileTyp->second[2]) : FastCgiClient());
                    itFcgi = s_mapFcgiConnections.find(strIpAdd);
                }

                if (itFcgi->second.IsFcgiProcessActiv() == true && itFcgi->second.IsConnected() == false)
                    itFcgi->second.Connect(strIpAdd.substr(0, nPosColon), nPort);
                s_mxFcgi.unlock();

                if (itFcgi->second.IsFcgiProcessActiv() == true && itFcgi->second.IsConnected() == true)
                {
                    soMetaDa.fSetNewTimeout(60000 * 10);   // 10 Min = 60.000 Millisekunden * 10

                    typedef struct
                    {
                        std::mutex mxOutData;
                        std::deque<std::tuple<std::unique_ptr<uint8_t[]>, size_t>> dqOutData;
                        size_t nHttp2Offset;
                    }SENDPARAM;
                    SENDPARAM stSendParam;
                    stSendParam.nHttp2Offset = nHttp2Offset;

                    bool l_bReqEnde{false};
                    condition_variable l_cvReqEnd;
                    uint16_t nReqId{0};
                    bool bReConnect{false};
                    do
                    {
                        nReqId = itFcgi->second.SendRequest(vCgiParam, &l_cvReqEnd, &l_bReqEnde, [](const uint16_t /*nRequestId*/, const unsigned char* pData, uint16_t nDataLen, void* vpCbParam)
                        {
                            if (nDataLen != 0)
                            {
                                SENDPARAM* pstSendParam = reinterpret_cast<SENDPARAM*>(vpCbParam);
                                auto tmp = make_unique<uint8_t[]>(nDataLen + pstSendParam->nHttp2Offset);
                                std::copy_n(&static_cast<const uint8_t*>(pData)[0], nDataLen, &tmp[pstSendParam->nHttp2Offset]);
                                std::lock_guard<std::mutex> lock(pstSendParam->mxOutData);
                                pstSendParam->dqOutData.emplace_back(move(tmp), nDataLen);
                            }
                        }, &stSendParam);
                        if (nReqId == 0)
                        {
                            s_mxFcgi.lock();
                            if (itFcgi->second.IsConnected() == true)
                                this_thread::sleep_for(chrono::milliseconds(1));
                            else if (bReConnect == false)
                                bReConnect = true, itFcgi->second.Connect(strIpAdd.substr(0, nPosColon), nPort);
                            s_mxFcgi.unlock();
                        }
                    } while (nReqId == 0 && itFcgi->second.IsFcgiProcessActiv() == true && patStop.load() == false);

                    if (nReqId != 0)    // We have a request send to the app server
                    {
                        bool bStreamReset = false;
                        pmtxReqdata.lock();
                        // we wait until the first packet is in the que, at least a null packet, as end of data marker must come
                        while (vecData.size() == 0 && patStop.load() == false && bStreamReset == false)
                        {
                            pmtxReqdata.unlock();
                            this_thread::sleep_for(chrono::milliseconds(10));
                            bStreamReset = fnIsStreamReset(nStreamId);
                            pmtxReqdata.lock();
                        }
                        pmtxReqdata.unlock();
                        if (patStop.load() == false && fnIsStreamReset(nStreamId) == false)
                        {
                            // get the first packet
                            pmtxReqdata.lock();
                            auto data = move(vecData.front());
                            vecData.pop_front();
                            pmtxReqdata.unlock();
                            while (data != nullptr && patStop.load() == false && fnIsStreamReset(nStreamId) == false)  // loop until we have nullptr packet
                            {
                                const uint32_t nDataLen = *(reinterpret_cast<uint32_t*>(data.get()));
                                itFcgi->second.SendRequestData(nReqId, reinterpret_cast<char*>(data.get() + 4), nDataLen);

                                pmtxReqdata.lock();
                                while (vecData.size() == 0 && patStop.load() == false && bStreamReset == false)
                                {   // wait until we have a packet again
                                    pmtxReqdata.unlock();
                                    this_thread::sleep_for(chrono::microseconds(1));
                                    bStreamReset = fnIsStreamReset(nStreamId);
                                    pmtxReqdata.lock();
                                }
                                if (vecData.size() > 0)
                                {
                                    data = move(vecData.front());
                                    vecData.pop_front();
                                }
                                pmtxReqdata.unlock();
                            }
                        }
                        if (patStop.load() == false)
                            itFcgi->second.SendRequestData(nReqId, "", 0);

                        bool bReqDone = false, bAbort = false;
                        std::unique_ptr<Timer<bool>> pAbortWatchDog;
                        do
                        {
                            stSendParam.mxOutData.lock();
                            while(stSendParam.dqOutData.size() > 1)
                            {
                                auto data1 = move(stSendParam.dqOutData.front());
                                stSendParam.dqOutData.pop_front();
                                auto data2 = move(stSendParam.dqOutData.front());
                                stSendParam.dqOutData.pop_front();
                                stSendParam.mxOutData.unlock();

                                auto tmp = make_unique<uint8_t[]>(nHttp2Offset + std::get<1>(data1) + std::get<1>(data2));
                                std::copy_n(&std::get<0>(data1).get()[nHttp2Offset], std::get<1>(data1), &tmp[nHttp2Offset]);
                                std::copy_n(&std::get<0>(data2).get()[nHttp2Offset], std::get<1>(data2), &tmp[nHttp2Offset + std::get<1>(data1)]);

                                fnSendOutput(tmp.get(), std::get<1>(data1) + std::get<1>(data2));
                                stSendParam.mxOutData.lock();
                                if (nOffset != 0)
                                {
                                    stSendParam.dqOutData.emplace_front(move(tmp), nOffset);
                                    nOffset = 0;
                                    break;
                                }
                            }
                            stSendParam.mxOutData.unlock();

                            mutex mxReqEnde;
                            unique_lock<mutex> lock(mxReqEnde);
                            bReqDone = l_cvReqEnd.wait_for(lock, chrono::milliseconds(10), [&]() noexcept { return l_bReqEnde; });
                            if ((patStop.load() == true || fnIsStreamReset(nStreamId) == true) && bAbort == false)   // Klient connection was interrupted, we Abort the app server request once
                            {
                                itFcgi->second.AbortRequest(nReqId);
                                bAbort = true;
                                pAbortWatchDog = make_unique<Timer<bool>>(5000, [](const Timer<bool>* /*pThis*/, bool* pUerData) { *pUerData = true;}, &l_bReqEnde);
//OutputDebugString(wstring(L"FastCGI Abort gesendet\r\n").c_str());
                            }
                        } while (bReqDone == false);
                        itFcgi->second.RemoveRequest(nReqId);
                        pAbortWatchDog.reset();

                        if (bAbort == false)
                        {
                            size_t nRestLen{nHttp2Offset};
                            for (size_t n = 0; n < stSendParam.dqOutData.size(); ++n)
                                nRestLen += std::get<1>(stSendParam.dqOutData[n]);
                            auto tmp = make_unique<uint8_t[]>(nRestLen);
                            nRestLen = 0;
                            while(stSendParam.dqOutData.size())
                            {
                                auto data = move(stSendParam.dqOutData.front());
                                stSendParam.dqOutData.pop_front();
                                std::copy_n(&std::get<0>(data).get()[nHttp2Offset], std::get<1>(data), &tmp[nHttp2Offset + nRestLen]);
                                nRestLen += std::get<1>(data);
                            }
                            if (nRestLen > 0)
                                fnSendOutput(tmp.get(), nRestLen);
                            fnAfterCgi(tmp.get());
                        }
                    }
                    else if (patStop.load() == false)
                        fnSendError();
                    soMetaDa.fSetNewTimeout(30000);   // back to 30 Seconds
                }
                else
                {
                    fnSendError();
                    s_mxFcgi.lock();
                    s_mapFcgiConnections.erase(strIpAdd);
                    s_mxFcgi.unlock();
                    CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] ", "FastCgi Process not activ.");
                }
            }
        }

        // Normal CGI Interface
        else if (bExecAsScript == true || (itFileTyp->second.empty() == false && itFileTyp->second.back().empty() == false))
        {
            SpawnProcess run;

            for (const auto& itCgiParam : vCgiParam)
                run.AddEnvironment(itCgiParam.first, itCgiParam.second);

            if (run.Spawn((bExecAsScript == true ? strItemPath : regex_replace(itFileTyp->second.back(), wregex(L"\\$1"), /*L"\" \"" +*/ strItemPath)), strItemPath.substr(0, strItemPath.find_last_of(L"\\/"))) == 0)
            {
                soMetaDa.fSetNewTimeout(60000 * 10);   // 10 Min = 60.000 Millisekunden * 10
                bool bStillRunning = true;

                bool bStreamReset = false;
                pmtxReqdata.lock();
                // we wait until the first packet is in the que, at least a null packet, as end of data marker must come
                while (vecData.size() == 0 && patStop.load() == false && bStreamReset == false)
                {
                    pmtxReqdata.unlock();
                    this_thread::sleep_for(chrono::milliseconds(10));
                    bStreamReset = fnIsStreamReset(nStreamId);
                    pmtxReqdata.lock();
                }
                pmtxReqdata.unlock();
                if (patStop.load() == false && fnIsStreamReset(nStreamId) == false)
                {
                    pmtxReqdata.lock();
                    // get the first packet
                    auto data = move(vecData.front());
                    vecData.pop_front();
                    pmtxReqdata.unlock();
                    while (data != nullptr && patStop.load() == false && fnIsStreamReset(nStreamId) == false)  // loop until we have nullptr packet
                    {
                        const uint32_t nDataLen = *(reinterpret_cast<uint32_t*>(data.get()));
//                        OutputDebugStringA(reinterpret_cast<const char*>(basic_string<unsigned char>(reinterpret_cast<unsigned char*>(data.get() + 4), nDataLen).c_str()));
                        if (run.WriteToSpawn(reinterpret_cast<unsigned char*>(data.get() + 4), nDataLen) != nDataLen)
                        {
                            run.KillProcess();
                            while (run.StillSpawning() == true && patStop.load() == false)
                                this_thread::sleep_for(chrono::milliseconds(1));
                            bStillRunning = false;
                            break;
                        }

                        pmtxReqdata.lock();
                        //OutputDebugString(wstring(L"CGI Datentransfer: " + to_wstring(nDataLen) + L" Bytes\r\n").c_str());
                        while (vecData.size() == 0 && patStop.load() == false && bStreamReset == false)
                        {   // wait until we have a packet again
                            pmtxReqdata.unlock();
                            this_thread::sleep_for(chrono::milliseconds(10));
                            bStreamReset = fnIsStreamReset(nStreamId);
                            pmtxReqdata.lock();
                        }
                        if (vecData.size() > 0)
                        {
                            data = move(vecData.front());
                            vecData.pop_front();
                        }
                        pmtxReqdata.unlock();
                    }
                }
                run.CloseWritePipe();

                unique_ptr<uint8_t[]> pBuf(new uint8_t[65536 + nHttp2Offset]);
                while (bStillRunning == true && patStop.load() == false && fnIsStreamReset(nStreamId) == false)
                {
                    bStillRunning = run.StillSpawning();

                    bool bHasRead = false;
                    size_t nRead;
                    while (nRead = run.ReadFromSpawn(reinterpret_cast<unsigned char*>(pBuf.get() + nHttp2Offset + nOffset), static_cast<uint32_t>(65536 - nOffset)), nRead > 0 && patStop.load() == false)
                    {
                        bHasRead = true;
                        nRead += nOffset;
                        nOffset = 0;
//                        OutputDebugStringA(reinterpret_cast<const char*>(basic_string<unsigned char>(pBuf.get() + nHttp2Offset, nRead).c_str()));
                        fnSendOutput(pBuf.get(), nRead);
                    }
                    if (nRead = run.ReadErrFromSpawn(reinterpret_cast<unsigned char*>(pBuf.get()), 65536), nRead > 0)
                    {
                        CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] CGI error output: ", string(reinterpret_cast<char*>(pBuf.get()), nRead));
                    }
                    else if (bStillRunning == true && bHasRead == false)
                        this_thread::sleep_for(chrono::milliseconds(1));
                }

                soMetaDa.fSetNewTimeout(30000);   // back to 30 Seconds

                if (patStop.load() == true || fnIsStreamReset(nStreamId) == true)  // Socket closed during cgi
                {
                    CLogFile::GetInstance(m_vHostParam[szHost].m_strErrLog).WriteToLog("[", CLogFile::LOGTYPES::PUTTIME, "] [error] [client ", soMetaDa.strIpClient, "] Socket closed during cgi: ", itPath->second);
                    run.KillProcess();
                    while (run.StillSpawning() == true)
                        this_thread::sleep_for(chrono::milliseconds(1));
                    fuExitDoAction();
                    return;
                }

                fnAfterCgi(pBuf.get());
            }
            else
            {
                fnSendError();
            }
        }
        else
        {
            fnSendError();
        }

        if (bCloseConnection == true)
            soMetaDa.fSocketClose();

        fuExitDoAction();
        return;
    }

    // evaluating Range header
    vector<pair<uint64_t, uint64_t>> vecRanges;
    const auto range = lstHeaderFields.find("range");
    if (range != end(lstHeaderFields))
    {
        static const regex rx("\\s*(?:(?:bytes\\s*=)|,)\\s*(\\d*)\\s*-\\s*(\\d*)");
        smatch match;
        string strSearch(range->second);
        while (regex_search(strSearch, match, rx) == true && match.size() == 3)
        {
            try
            {
                vecRanges.push_back(make_pair(match[1].length() == 0 ? 0 : stoull(match[1].str()), match[2].length() == 0 ? stFileInfo.st_size : stoull(match[2].str())));
            }
            catch (const std::exception& /*ex*/)
            {
                vecRanges.clear();
                break;
            }

            strSearch = strSearch.substr(match[0].str().size());
        }
        sort(begin(vecRanges), end(vecRanges), [](auto& elm1, auto& elm2) noexcept { return elm1.first == elm2.first ? (elm1.second < elm2.second ? true : false) : (elm1.first < elm2.first ? true : false); });
        for (size_t n = 0; vecRanges.size() > 1 && n < vecRanges.size() - 1; ++n)
        {
            if (vecRanges[n].first >= vecRanges[n].second || vecRanges[n + 1].first >= vecRanges[n + 1].second || vecRanges[n].first >= static_cast<uint64_t>(stFileInfo.st_size) || vecRanges[n + 1].first >= static_cast<uint64_t>(stFileInfo.st_size))
            {
                SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 416, iHeaderFlag, strHttpVersion, lstHeaderFields);

                if (httpVers < 2)
                    soMetaDa.fSocketClose();

                fuExitDoAction();
                return;
            }

            if ((vecRanges[n].first >= vecRanges[n + 1].first && vecRanges[n].first <= vecRanges[n + 1].second)
            || (vecRanges[n + 1].first > vecRanges[n].first && vecRanges[n + 1].first <= vecRanges[n].second))
            {
                vecRanges[n].first = min(vecRanges[n].first, vecRanges[n + 1].first);
                vecRanges[n].second = max(vecRanges[n].second, vecRanges[n + 1].second);
                vecRanges.erase(next(begin(vecRanges), n + 1));
                --n;
            }
        }
    }

    // Calc ETag
    string strEtag = "\"" + md5(wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strItemPath) + ":" + to_string(stFileInfo.st_mtime) + ":" + to_string(stFileInfo.st_size)) + "\"";

    // MimeType
    if (strMineType.empty() == true)    // if not empty we have a fixed mimetyp from the configuration
    {
        auto it = find_if(begin(MimeListe), end(MimeListe), [strFileExtension](const MIMEENTRY & item) noexcept { return strFileExtension == MIMEEXTENSION(item); });
        if (it != end(MimeListe))
            strMineType = MIMESTRING(*it);
        else
            strMineType = "application/octet-stream";
    }

    HeadList umPhpHeaders;
    stringstream strLastModTime; strLastModTime << put_time(::gmtime(&stFileInfo.st_mtime), "%a, %d %b %Y %H:%M:%S GMT");
    umPhpHeaders.emplace_back(make_pair("Content-Type", strMineType));
    umPhpHeaders.emplace_back(make_pair("Last-Modified", strLastModTime.str()));
    umPhpHeaders.emplace_back(make_pair("Cache-Control", "private, must-revalidate"));
    umPhpHeaders.emplace_back(make_pair("ETag", strEtag));
    umPhpHeaders.insert(end(umPhpHeaders), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

    // Static Content, check if we have a matching etag
    const auto ifnonematch = lstHeaderFields.find("if-none-match");
    if (ifnonematch != end(lstHeaderFields) && ifnonematch->second == strEtag)
    {
        const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, 304, umPhpHeaders, 0);
        if (nHeaderLen < nBufSize)
        {
            if (httpVers == 2)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            if (fnIsStreamReset(nStreamId) == false)
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
        }
        soMetaDa.fResetTimer();

        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << itMethode->second << " " << lstHeaderFields.find(":path")->second
            << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
            << "\" 304 " << (stFileInfo.st_size == 0 ? "-" : to_string(stFileInfo.st_size)) << " \""
            << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
            << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;

        if (bCloseConnection == true)
            soMetaDa.fSocketClose();

        fuExitDoAction();
        return;
    }

    // Static Content, check if we have a Modified Since Header
    const auto ifmodifiedsince = lstHeaderFields.find("if-modified-since");
    if (ifmodifiedsince != end(lstHeaderFields))
    {
        tm tmIfModified;
        stringstream ss(ifmodifiedsince->second);
        ss >> get_time(&tmIfModified, "%a, %d %b %Y %H:%M:%S GMT");
        const double dTimeDif = difftime(mktime(&tmIfModified), mktime(::gmtime(&stFileInfo.st_mtime)));
        if (fabs(dTimeDif) < 0.001)
        {
            const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, 304, umPhpHeaders, 0);
            if (nHeaderLen < nBufSize)
            {
                if (httpVers == 2)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            }
            soMetaDa.fResetTimer();

            CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
                << itMethode->second << " " << lstHeaderFields.find(":path")->second
                << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
                << "\" 304 " << (stFileInfo.st_size == 0 ? "-" : to_string(stFileInfo.st_size)) << " \""
                << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
                << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
                << CLogFile::LOGTYPES::END;

            if (bCloseConnection == true)
                soMetaDa.fSocketClose();

            fuExitDoAction();
            return;
        }
    }

    const auto ifrange = lstHeaderFields.find("if-range");
    if (ifrange != end(lstHeaderFields))
    {
        if (ifrange->second.find_first_of(",:") != string::npos)    // Datum
        {
            tm tmIfModified;
            stringstream ss(ifrange->second);
            ss >> get_time(&tmIfModified, "%a, %d %b %Y %H:%M:%S GMT");
            const double dTimeDif = difftime(mktime(&tmIfModified), mktime(::gmtime(&stFileInfo.st_mtime)));
            if (fabs(dTimeDif) > 0.001)
                vecRanges.clear();
        }
        else // etag
        {
            if (ifrange->second != strEtag)
                vecRanges.clear();
        }
    }

    // OPTIONS
    if (itMethode->second == "OPTIONS")
    {
        HeadList optHeader({ make_pair("Allow", "OPTIONS, GET, HEAD, POST") });
        optHeader.insert(end(optHeader), begin(m_vHostParam[szHost].m_vHeader), end(m_vHostParam[szHost].m_vHeader));

        const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | TERMINATEHEADER | ADDCONENTLENGTH, 200, optHeader, 0);
        if (nHeaderLen < nBufSize)
        {
            if (httpVers == 2)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            if (fnIsStreamReset(nStreamId) == false)
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
        }
        soMetaDa.fResetTimer();
        if (bCloseConnection == true)
            soMetaDa.fSocketClose();

        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << itMethode->second << " " << lstHeaderFields.find(":path")->second
            << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
            << "\" 200 -" << " \""
            << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
            << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;

        fuExitDoAction();
        return;
    }

    if (strItemPath == L"*")
    {
        SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 404, iHeaderFlag, strHttpVersion, lstHeaderFields);
        if (httpVers < 2)
            soMetaDa.fSocketClose();

        fuExitDoAction();
        return;
    }

    // HEAD
    if (itMethode->second == "HEAD")
    {
        umPhpHeaders.emplace_back(make_pair("Accept-Ranges", "bytes"));

        // Build response header
        const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, stFileInfo.st_size);
        if (nHeaderLen < nBufSize)
        {
            if (httpVers == 2)
                BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, nStreamId);
            if (fnIsStreamReset(nStreamId) == false)
                soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
        }
        soMetaDa.fResetTimer();
        if (bCloseConnection == true)
            soMetaDa.fSocketClose();

        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << itMethode->second << " " << lstHeaderFields.find(":path")->second
            << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
            << "\" " << iStatus << " " << (stFileInfo.st_size == 0 ? "-" : to_string(stFileInfo.st_size)) << " \""
            << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
            << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;

        fuExitDoAction();
        return;
    }

    if (itMethode->second != "GET")    // POST and anything else is not allowed any more
    {
        SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 405, iHeaderFlag, strHttpVersion, lstHeaderFields);
        if (bCloseConnection == true || httpVers < 2)
            soMetaDa.fSocketClose();
        fuExitDoAction();
        return;
    }

    // Load file
    fstream fin(FN_CA(strItemPath), ios_base::in | ios_base::binary);
    if (fin.is_open() == true)
    {
        uint64_t nFSize = stFileInfo.st_size;
        if (vecRanges.size() == 1)  // Momentan nur 1 Range
        {
            nFSize = vecRanges[0].second - vecRanges[0].first;
            fin.seekg(vecRanges[0].first, ios_base::beg);
            umPhpHeaders.emplace_back(make_pair("Content-Range", "bytes " + to_string(vecRanges[0].first) + "-" + to_string(vecRanges[0].second) + "/" + to_string(stFileInfo.st_size)));
            iStatus = 206;
        }
        else
            umPhpHeaders.emplace_back(make_pair("Accept-Ranges", "bytes"));

        const auto acceptencoding = lstHeaderFields.find("accept-encoding");
        if (acceptencoding != end(lstHeaderFields))
        {
            // http://www.filesignatures.net/index.php?page=all
            if (find_if(begin(m_vHostParam[szHost].m_vDeflateTyps), end(m_vHostParam[szHost].m_vDeflateTyps), [&](const string& strType) noexcept { return strType == strMineType ? true : false; }) != end(m_vHostParam[szHost].m_vDeflateTyps))
            {
                if (acceptencoding->second.find("br") != string::npos) iHeaderFlag |= BROTLICODING;
                else if (acceptencoding->second.find("gzip") != string::npos) iHeaderFlag |= GZIPENCODING;
                else if (acceptencoding->second.find("deflate") != string::npos) iHeaderFlag |= DEFLATEENCODING;
            }
        }

        //iHeaderFlag &= ~(GZIPENCODING | DEFLATEENCODING);
        if (iHeaderFlag & GZIPENCODING || iHeaderFlag & DEFLATEENCODING)
        {
            if (httpVers < 2)
                umPhpHeaders.emplace_back(make_pair("Transfer-Encoding", "chunked"));

            const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, 0);
            if (nHeaderLen < nBufSize)
            {
                if (httpVers == 2)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            }
            soMetaDa.fResetTimer();

            GZipPack gzipEncoder;
            if (gzipEncoder.Init((iHeaderFlag & DEFLATEENCODING) ? true : false) == Z_OK)
            {
                unique_ptr<unsigned char[]> srcBuf(new unsigned char[nSizeSendBuf]);
                unique_ptr<unsigned char[]> dstBuf(new unsigned char[nSizeSendBuf]);

                uint64_t nBytesTransferred = 0;
                int iResult = 0;
                do
                {
                    const streamsize nBytesRead = fin.read(reinterpret_cast<char*>(srcBuf.get()), nSizeSendBuf).gcount();
                    if (nBytesRead == 0)
                        break;
                    nBytesTransferred += nBytesRead;

                    gzipEncoder.InitBuffer(srcBuf.get(), static_cast<uint32_t>(nBytesRead));
                    const int nFlush = nBytesTransferred == nFSize ? Z_FINISH : Z_NO_FLUSH;

                    size_t nBytesConverted;
                    do
                    {
                        nBytesConverted = nSizeSendBuf - nHttp2Offset;
                        iResult = gzipEncoder.Inflate(dstBuf.get() + nHttp2Offset, &nBytesConverted, nFlush);

                        size_t nOffset = 0;
                        while ((iResult == Z_OK || iResult == Z_STREAM_END) && ((nSizeSendBuf - nHttp2Offset) - nBytesConverted - nOffset) != 0 && patStop.load() == false && fnIsStreamReset(nStreamId) == false)
                        {
                            int64_t nStreamWndSize = INT32_MAX;
                            if (fnGetStreamWindowSize(nStreamWndSize) == false)
                                break;  // Stream Item was removed, probably the stream was reset

                            size_t nSendBufLen;
                            if (fnSendQueueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), ((nSizeSendBuf - nHttp2Offset) - nBytesConverted - nOffset)) == false)
                                continue;

                            if (httpVers == 2)
                            {
                                bool bLastPaket = false;
                                if (iResult == Z_STREAM_END && ((nSizeSendBuf - nHttp2Offset) - nBytesConverted - nOffset) == nSendBufLen) // Letztes Paket
                                    bLastPaket = true;
                                BuildHttp2Frame(dstBuf.get() + nOffset, nSendBufLen, 0x0, bLastPaket == true ? 0x1 : 0x0, nStreamId);
                            }
                            else
                            {
                                stringstream ss;
                                ss << hex << ::uppercase << nSendBufLen << "\r\n";
                                soMetaDa.fSocketWrite(ss.str().c_str(), ss.str().size());
                            }
                            soMetaDa.fSocketWrite(dstBuf.get() + nOffset, nSendBufLen + nHttp2Offset);
                            if (httpVers < 2)
                                soMetaDa.fSocketWrite("\r\n", 2);
                            soMetaDa.fResetTimer();

                            if (fnUpdateStreamParam(nSendBufLen) == -1)
                                break;  // Stream Item was removed, probably the stream was reset

                            //nBytesConverted += nSendBufLen;
                            nOffset += nSendBufLen;
                        }

                        fnResetReservierteWindowSize();

                    } while (iResult == Z_OK && nBytesConverted == 0 && patStop.load() == false && fnIsStreamReset(nStreamId) == false);

                } while (iResult == Z_OK && patStop.load() == false && fnIsStreamReset(nStreamId) == false);

                if (httpVers < 2 && patStop.load() == false)
                    soMetaDa.fSocketWrite("0\r\n\r\n", 5);
            }
        }
        else if (iHeaderFlag & BROTLICODING)
        {
            if (httpVers < 2)
                umPhpHeaders.emplace_back(make_pair("Transfer-Encoding", "chunked"));

            const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, 0);
            if (nHeaderLen < nBufSize)
            {
                if (httpVers == 2)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            }
            soMetaDa.fResetTimer();

            BrotliEncoderState* s = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
            BrotliEncoderSetParameter(s, BROTLI_PARAM_QUALITY, (uint32_t)9);
            BrotliEncoderSetParameter(s, BROTLI_PARAM_LGWIN, (uint32_t)0);
/*                if (dictionary_path != NULL) {
                size_t dictionary_size = 0;
                uint8_t* dictionary = ReadDictionary(dictionary_path, &dictionary_size);
                BrotliEncoderSetCustomDictionary(s, dictionary_size, dictionary);
                free(dictionary);
            }
*/
            unique_ptr<unsigned char[]> srcBuf(new unsigned char[nSizeSendBuf]);
            unique_ptr<unsigned char[]> dstBuf(new unsigned char[nSizeSendBuf]);

            size_t nBytIn = 0;
            const uint8_t* input = nullptr;
            size_t nBytOut = nSizeSendBuf - nHttp2Offset;
            uint8_t* output = dstBuf.get() + nHttp2Offset;

            uint64_t nBytesTransferred = 0;
            while (patStop.load() == false && fnIsStreamReset(nStreamId) == false)
            {
                if (nBytIn == 0)
                {
                    const streamsize nBytesRead = fin.read(reinterpret_cast<char*>(srcBuf.get()), nSizeSendBuf).gcount();
                    nBytIn = static_cast<size_t>(nBytesRead);
                    input = srcBuf.get();
                    nBytesTransferred += nBytIn;
                }

                if (!BrotliEncoderCompressStream(s, nBytIn == 0 ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS, &nBytIn, &input, &nBytOut, &output, nullptr))
                    break;

                size_t nOffset = 0;
                while (nBytOut != nSizeSendBuf - nHttp2Offset && patStop.load() == false && fnIsStreamReset(nStreamId) == false)
                {
                    int64_t nStreamWndSize = INT32_MAX;
                    if (fnGetStreamWindowSize(nStreamWndSize) == false)
                        break;  // Stream Item was removed, probably the stream was reset

                    size_t nSendBufLen;
                    if (fnSendQueueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), ((nSizeSendBuf - nHttp2Offset) - nBytOut)) == false)
                        continue;

                    if (httpVers == 2)
                        BuildHttp2Frame(dstBuf.get() + nOffset, nSendBufLen, 0x0, BrotliEncoderIsFinished(s) == 1 ? 0x1 : 0x0, nStreamId);
                    else
                    {
                        stringstream ss;
                        ss << hex << ::uppercase << nSendBufLen << "\r\n";
                        soMetaDa.fSocketWrite(ss.str().c_str(), ss.str().size());
                    }
                    soMetaDa.fSocketWrite(dstBuf.get() + nOffset, nSendBufLen + nHttp2Offset);
                    if (httpVers < 2)
                        soMetaDa.fSocketWrite("\r\n", 2);
                    soMetaDa.fResetTimer();

                    if (fnUpdateStreamParam(nSendBufLen) == -1)
                        break;  // Stream Item was removed, probably the stream was reset

                    nBytOut += nSendBufLen;
                    nOffset += nSendBufLen;
                }

                fnResetReservierteWindowSize();

                if (BrotliEncoderIsFinished(s))
                    break;

                output = dstBuf.get() + nHttp2Offset;
            }

            if (httpVers < 2 && patStop.load() == false)
                soMetaDa.fSocketWrite("0\r\n\r\n", 5);

            BrotliEncoderDestroyInstance(s);
        }
        else
        {
            // Build response header
            const size_t nHeaderLen = BuildRespHeader(caBuffer + nHttp2Offset, nBufSize - nHttp2Offset, iHeaderFlag | TERMINATEHEADER, iStatus, umPhpHeaders, nFSize);
            if (nHeaderLen < nBufSize)
            {
                if (httpVers == 2)
                    BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, nStreamId);
                if (fnIsStreamReset(nStreamId) == false)
                    soMetaDa.fSocketWrite(caBuffer, nHeaderLen + nHttp2Offset);
            }
            soMetaDa.fResetTimer();

            auto apBuf = make_unique<uint8_t[]>(nSizeSendBuf + nHttp2Offset + 2);

            uint64_t nBytesTransferred = 0;
            while (nBytesTransferred < nFSize && patStop.load() == false && fnIsStreamReset(nStreamId) == false)
            {
                int64_t nStreamWndSize = INT32_MAX;
                if (fnGetStreamWindowSize(nStreamWndSize) == false)
                    break;  // Stream Item was removed, probably the stream was reset

                size_t nSendBufLen;
                if (fnSendQueueReady(nStreamWndSize, nSendBufLen, static_cast<uint64_t>(nSizeSendBuf - nHttp2Offset), nFSize - nBytesTransferred) == false)
                    continue;

                nBytesTransferred += nSendBufLen;
                fin.read(reinterpret_cast<char*>(apBuf.get()) + nHttp2Offset, nSendBufLen);

                if (httpVers == 2)
                    BuildHttp2Frame(apBuf.get(), nSendBufLen, 0x0, (nFSize - nBytesTransferred == 0 ? 0x1 : 0x0), nStreamId);
                soMetaDa.fSocketWrite(apBuf.get(), nSendBufLen + nHttp2Offset);
                soMetaDa.fResetTimer();

                if (fnUpdateStreamParam(nSendBufLen) == -1)
                    break;  // Stream Item was removed, probably the stream was reset
            }
            fnResetReservierteWindowSize();
        }
        fin.close();

        CLogFile::GetInstance(m_vHostParam[szHost].m_strLogFile) << soMetaDa.strIpClient << " - - [" << CLogFile::LOGTYPES::PUTTIME << "] \""
            << itMethode->second << " " << lstHeaderFields.find(":path")->second
            << (httpVers == 2 ? " HTTP/2." : " HTTP/1.") << strHttpVersion
            << "\" " << iStatus << " " << (nFSize == 0 ? "-" : to_string(nFSize)) << " \""
            << (lstHeaderFields.find("referer") != end(lstHeaderFields) ? lstHeaderFields.find("referer")->second : "-") << "\" \""
            << (lstHeaderFields.find("user-agent") != end(lstHeaderFields) ? lstHeaderFields.find("user-agent")->second : "-") << "\""
            << CLogFile::LOGTYPES::END;
    }
    else
    {
        SendErrorRespons(soMetaDa, httpVers, nStreamId, BuildRespHeader, 500, iHeaderFlag, strHttpVersion, lstHeaderFields);
        if (httpVers < 2)
            bCloseConnection = true;
    }

    if (bCloseConnection == true)
        soMetaDa.fSocketClose();

    fuExitDoAction();
}

void CHttpServ::EndOfStreamAction(const MetaSocketData& soMetaDa, const uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex& pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, atomic<bool>& patStop, mutex& pmtxReqdata, deque<unique_ptr<char[]>>& vecData, deque<AUTHITEM>& lstAuthInfo)
{
    const auto StreamPara = StreamList.find(streamId);
    if (StreamPara != end(StreamList))
    {
        HeadList& lstHeaderFields = GETHEADERLIST(StreamPara);
        if (find_if(begin(lstHeaderFields), end(lstHeaderFields), [&](HeadList::const_reference cIter) noexcept { return ":status" == cIter.first; }) != end(lstHeaderFields))
            throw H2ProtoException(H2ProtoException::WRONG_HEADER);
    }

    m_ActThrMutex.lock();
    thread thTemp(&CHttpServ::DoAction, this, soMetaDa, static_cast<uint8_t>(2), streamId, ref(StreamList), ref(tuStreamSettings), ref(pmtxStream), ref(maResWndSizes), bind(&CHttpServ::BuildH2ResponsHeader, this, _1, _2, _3, _4, _5, _6), ref(patStop), ref(pmtxReqdata), ref(vecData), ref(lstAuthInfo));
    m_umActionThreads.emplace(thTemp.get_id(), patStop);
    m_ActThrMutex.unlock();
    thTemp.detach();
}

const array<CHttpServ::MIMEENTRY, 112>  CHttpServ::MimeListe = { {
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
    MIMEENTRY(L"json", "application/json"),
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

const map<uint32_t, string> CHttpServ::RespText = {
    { 100, "Continue" },
    { 101, "Switching Protocols" },
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 203, "Non-Authoritative Information" },
    { 204, "No Content" },
    { 205, "Reset Content" },
    { 206, "Partial Content" },
    { 207, "Multi-Status" },
    { 300, "Multiple Choices" },
    { 301, "Moved Permanently" },
    { 302, "Moved Temporarily" },
    { 303, "See Other" },
    { 304, "Not Modified" },
    { 305, "Use Proxy" },
    { 307, "Temporary Redirect" },
    { 308, "Permanent Redirect" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 402, "Payment Required" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 405, "Method Not Allowed" },
    { 406, "Not Acceptable" },
    { 407, "Proxy Authentication Required" },
    { 408, "Request Timeout" },
    { 409, "Conflict" },
    { 410, "Gone" },
    { 411, "Length Required" },
    { 412, "Precondition Failed" },
    { 413, "Request Entity Too Large" },
    { 414, "Request-URI Too Long" },
    { 415, "Unsupported Media Type" },
    { 416, "Requested Range Not Satisfiable" },
    { 417, "Expectation Failed" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 504, "Gateway Timeout" },
    { 505, "HTTP Version Not Supported" }
};
