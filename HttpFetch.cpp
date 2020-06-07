
#include <regex>
#include <unordered_map>

#include "HttpFetch.h"
#include "GZip.h"
#include "CommonLib/Base64.h"
#include <brotli/decode.h>

#if defined(_WIN32) || defined(_WIN64)
#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "x64/Debug/socketlib64d")
#pragma comment(lib, "x64/Debug/brotli")
#else
#pragma comment(lib, "Debug/socketlib32d")
#pragma comment(lib, "Debug/brotli")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "x64/Release/socketlib64")
#pragma comment(lib, "x64/Release/brotli")
#else
#pragma comment(lib, "Release/socketlib32")
#pragma comment(lib, "Release/brotli")
#endif
#endif

#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")
#else
extern void OutputDebugString(const wchar_t* pOut);
extern void OutputDebugStringA(const char* pOut);
#endif

using namespace std::placeholders;

vector<string> vecProtokolls = { {"h2"}, {"http/1.1"} };

HttpFetch::HttpFetch(function<void(HttpFetch*, void*, uint32_t)> fnNotify, void* vpUserData) : m_pcClientCon(nullptr), m_sPort(80), m_UseSSL(false), m_uiStatus(0), m_bIsHttp2(false), m_bOutpHeader(false), m_bEndOfHeader(false), m_nContentLength(SIZE_MAX), m_nContentReceived(0), m_nChuncked(-1), m_nNextChunk(0), m_nChunkFooter(0), m_fnNotify(fnNotify)
{
}


HttpFetch::~HttpFetch()
{
    if (m_pcClientCon != nullptr)
        delete m_pcClientCon;
}

bool HttpFetch::Fetch(const string& strAdresse, const string& strMethode /*= string("GET")*/)
{
    m_strServer = strAdresse;
    m_strMethode = strMethode;

    transform(begin(m_strServer), begin(m_strServer) + 5, begin(m_strServer), ::tolower);
    transform(begin(m_strMethode), end(m_strMethode), begin(m_strMethode), ::toupper);

/*    if (m_pTmpFileSend.get() != 0)
    {
        m_pTmpFileSend.get()->Flush();
        m_pTmpFileSend->Rewind();
    }*/

    if (m_strServer.compare(0, 4, "http") != 0)
        return false;

    m_strServer.erase(0, 4);
    if (m_strServer[0] == 's')
    {
        m_sPort = 443, m_UseSSL = true;
        m_strServer.erase(0, 1);
    }

    if (m_strServer.compare(0, 3, "://") != 0)
        return false;
    m_strServer.erase(0, 3);

    size_t nPos = m_strServer.find('/');
    if (nPos != string::npos)
    {
        m_strPath = m_strServer.substr(nPos);
        m_strServer.erase(nPos);
    }
    else
        m_strPath = "/";

    nPos = m_strServer.find(':');
    if (nPos != string::npos)
    {
        m_sPort = stoi(m_strServer.substr(nPos + 1));
        m_strServer.erase(nPos);
    }

    if (m_UseSSL == true)
    {
        SslTcpSocket* pSocket = new SslTcpSocket();
        pSocket->SetTrustedRootCertificates("./certs/ca-certificates.crt");
        pSocket->SetAlpnProtokollNames(vecProtokolls);
        m_pcClientCon = pSocket;
    }
    else
        m_pcClientCon = new TcpSocket();

    m_pcClientCon->BindFuncConEstablished(static_cast<function<void(TcpSocket*)>>(bind(&HttpFetch::Connected, this, _1)));
    m_pcClientCon->BindFuncBytesReceived(static_cast<function<void(TcpSocket*)>>(bind(&HttpFetch::DatenEmpfangen, this, _1)));
    m_pcClientCon->BindErrorFunction(static_cast<function<void(BaseSocket*)>>(bind(&HttpFetch::SocketError, this, _1)));
    m_pcClientCon->BindCloseFunction(static_cast<function<void(BaseSocket*)>>(bind(&HttpFetch::SocketCloseing, this, _1)));

    return m_pcClientCon->Connect(m_strServer.c_str(), m_sPort);
}

bool HttpFetch::AddContent(char* pBuffer, uint64_t nBufSize)
{
    m_mxVecData.lock();
    if (pBuffer == nullptr && nBufSize == 0)
        m_vecData.emplace_back(unique_ptr<char[]>(nullptr));
    else if (nBufSize > 0)
    {
        m_vecData.emplace_back(make_unique<char[]>(nBufSize + 4));
        copy(pBuffer, pBuffer + nBufSize, m_vecData.back().get() + 4);
        *reinterpret_cast<uint32_t*>(m_vecData.back().get()) = static_cast<uint32_t>(nBufSize);
    }
    m_mxVecData.unlock();
/*
    if (m_pTmpFileSend.get() == 0)
    {
        m_pTmpFileSend = make_shared<TempFile>();
        m_pTmpFileSend.get()->Open();
    }

    m_pTmpFileSend.get()->Write(pBuffer, nBufSize);
*/
    return true;
}

void HttpFetch::Stop()
{
    m_pcClientCon->Close();
}

void HttpFetch::Connected(TcpSocket* const pTcpSocket)
{
    string Protocoll;
    if (m_UseSSL == true)
    {
        long nResult = reinterpret_cast<SslTcpSocket*>(m_pcClientCon)->CheckServerCertificate(m_strServer.c_str());
        if (nResult != 0)
            OutputDebugString(L"Http2Fetch::Connected Zertifikat not verifyd\r\n");

        Protocoll = reinterpret_cast<SslTcpSocket*>(m_pcClientCon)->GetSelAlpnProtocol();
    }

    if (Protocoll == "h2")
    {
        m_umStreamCache.insert(make_pair(0, STREAMITEM({ 0, deque<DATAITEM>(), HeadList(), 0, 0, get<1>(m_tuStreamSettings), make_shared<mutex>(), {} })));

        m_bIsHttp2 = true;
        pTcpSocket->Write("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
        pTcpSocket->Write("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x3\x0\x0\x0\x64\x0\x4\x0\x10\x0\x0", 21);// SETTINGS frame (4) with ParaID(3) and 100 Value and ParaID(4) and 1048576 Value
        pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\xf\x0\x1", 13);      // WINDOW_UPDATE frame (8) with value ?1048576? (minus 65535) == 983041

        char caBuffer[2048];
        size_t nHeaderLen = 0;

        size_t nReturn = HPackEncode(caBuffer + 9, 2048 - 9, ":method", m_strMethode.c_str());
        if (nReturn != SIZE_MAX)
            nHeaderLen += nReturn;
        nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, ":path", m_strPath.c_str());
        if (nReturn != SIZE_MAX)
            nHeaderLen += nReturn;
        nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, ":authority", m_strServer.c_str());
        if (nReturn != SIZE_MAX)
            nHeaderLen += nReturn;
        nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, ":scheme", "https");
        if (nReturn != SIZE_MAX)
            nHeaderLen += nReturn;

        for (auto& itPair : m_umAddHeader)
        {
            transform(begin(itPair.first), end(itPair.first), begin(itPair.first), ::tolower);
            nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, itPair.first.c_str(), itPair.second.c_str());
            if (nReturn == SIZE_MAX)
                break;
            nHeaderLen += nReturn;
        }

uint32_t nSend = 0;
        m_mxVecData.lock();
        // we wait until the first packet is in the que, at least a null packet, as end of data marker must come
        while (m_vecData.size() == 0)
        {
            m_mxVecData.unlock();
            this_thread::sleep_for(chrono::milliseconds(10));
            m_mxVecData.lock();
        }
        // get the first packet
        auto data = move(m_vecData.front());
        m_vecData.pop_front();
        m_mxVecData.unlock();

        m_umStreamCache.insert(make_pair(1, STREAMITEM({ 0, deque<DATAITEM>(), HeadList(), 0, 0, get<1>(m_tuStreamSettings), make_shared<mutex>(), {} })));
        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, data != nullptr ? 0x4 : 0x5, 1);
        pTcpSocket->Write(caBuffer, nHeaderLen + 9);

        while (data != nullptr)  // loop until we have nullptr packet
        {
            uint32_t nDataLen = *(reinterpret_cast<uint32_t*>(data.get()));

            auto apBuf = make_unique<unsigned char[]>(nDataLen + 9);
            copy(reinterpret_cast<unsigned char*>(data.get() + 4), reinterpret_cast<unsigned char*>(data.get() + 4 + nDataLen), apBuf.get() + 9);

nSend += nDataLen;
            m_mxVecData.lock();
            while (m_vecData.size() == 0)
            {   // wait until we have a packet again
                m_mxVecData.unlock();
                this_thread::sleep_for(chrono::milliseconds(10));
                m_mxVecData.lock();
            }
            if (m_vecData.size() > 0)
            {
                data = move(m_vecData.front());
                m_vecData.pop_front();
            }
            m_mxVecData.unlock();

            BuildHttp2Frame(reinterpret_cast<char*>(apBuf.get()), static_cast<size_t>(nDataLen), 0x0, data != nullptr ? 0x0 : 0x1, 1);
            pTcpSocket->Write(apBuf.get(), static_cast<size_t>(nDataLen) + 9);
        }

OutputDebugString(wstring(L"Daten gesendet:" + to_wstring(nSend) + L"\r\n").c_str());

        /*if (m_pTmpFileSend.get() != 0)
        {
            BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x4, 1);
            pTcpSocket->Write(caBuffer, nHeaderLen + 9);

            auto apBuf = make_unique<unsigned char[]>(0x4000 + 9 + 2);
            streamsize nBytesRead = m_pTmpFileSend.get()->Read(apBuf.get() + 9, 0x4000);
            while (nBytesRead != 0)
            {
                BuildHttp2Frame(reinterpret_cast<char*>(apBuf.get()), static_cast<size_t>(nBytesRead), 0x0, nBytesRead < 0x4000 ? 0x1 : 0x0, 1);

                pTcpSocket->Write(apBuf.get(), static_cast<size_t>(nBytesRead) + 9);
                nBytesRead = m_pTmpFileSend.get()->Read(apBuf.get() + 9, 0x4000);
            }
            m_pTmpFileSend.get()->Close();
        }
        else
        {
            BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, 1);
            pTcpSocket->Write(caBuffer, nHeaderLen + 9);
        }*/
    }
    else
    {
        string strRequest = m_strMethode + " " + m_strPath + " HTTP/1.1\r\nHost: " + m_strServer + "\r\n";
        for (auto& itPair : m_umAddHeader)
            strRequest += itPair.first + ": " + itPair.second + "\r\n";
        strRequest += "\r\n";
        pTcpSocket->Write(strRequest.c_str(), strRequest.size());

uint32_t nSend = 0;
        m_mxVecData.lock();
        // we wait until the first packet is in the que, at least a null packet, as end of data marker must come
        while (m_vecData.size() == 0)
        {
            m_mxVecData.unlock();
            this_thread::sleep_for(chrono::milliseconds(10));
            m_mxVecData.lock();
        }
        // get the first packet
        auto data = move(m_vecData.front());
        m_vecData.pop_front();
        m_mxVecData.unlock();
        while (data != nullptr)  // loop until we have nullptr packet
        {
            uint32_t nDataLen = *(reinterpret_cast<uint32_t*>(data.get()));
            pTcpSocket->Write(reinterpret_cast<unsigned char*>(data.get() + 4), nDataLen);
nSend += nDataLen;
            m_mxVecData.lock();
            while (m_vecData.size() == 0)
            {   // wait until we have a packet again
                m_mxVecData.unlock();
                this_thread::sleep_for(chrono::milliseconds(10));
                m_mxVecData.lock();
            }
            if (m_vecData.size() > 0)
            {
                data = move(m_vecData.front());
                m_vecData.pop_front();
            }
            m_mxVecData.unlock();
        }
OutputDebugString(wstring(L"Daten gesendet:" + to_wstring(nSend) + L"\r\n").c_str());
/*
        if (m_pTmpFileSend.get() != 0)
        {
            auto apBuf = make_unique<unsigned char[]>(0x4000 + 2);
            streamsize nBytesRead = m_pTmpFileSend.get()->Read(apBuf.get(), 0x4000);
            while (nBytesRead != 0)
            {
                pTcpSocket->Write(apBuf.get(), static_cast<size_t>(nBytesRead));
                nBytesRead = m_pTmpFileSend.get()->Read(apBuf.get(), 0x4000);
            }
            m_pTmpFileSend.get()->Close();
        }*/
    }

    m_Timer = make_unique<Timer>(30000, bind(&HttpFetch::OnTimeout, this, _1, _2));
    m_soMetaDa = { m_pcClientCon->GetClientAddr(), m_pcClientCon->GetClientPort(), m_pcClientCon->GetInterfaceAddr(), m_pcClientCon->GetInterfacePort(), m_pcClientCon->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, m_Timer.get()), bind(&Timer::SetNewTimeout, m_Timer.get(), _1) };
}

void HttpFetch::DatenEmpfangen(TcpSocket* const pTcpSocket)
{
    static atomic_bool atTmp;
    static deque<AUTHITEM> dqAuth;

    uint32_t nAvalible = pTcpSocket->GetBytesAvailible();

    if (nAvalible == 0)
    {
        pTcpSocket->Close();
        return;
    }

    shared_ptr<char> spBuffer(new char[m_strBuffer.size() + nAvalible + 1]);
    copy(begin(m_strBuffer), begin(m_strBuffer) + m_strBuffer.size(), spBuffer.get());

    size_t nRead = pTcpSocket->Read(spBuffer.get() + m_strBuffer.size(), nAvalible);

    if (nRead > 0)
    {
        if (m_Timer != nullptr)
            m_Timer->Reset();

        nRead += m_strBuffer.size();
        m_strBuffer.clear();
        spBuffer.get()[nRead] = 0;

        if (m_bIsHttp2 == true)
        {
            size_t nRet, nReadSave = nRead;
            if (nRet = Http2StreamProto(m_soMetaDa, spBuffer.get(), nRead, m_qDynTable, m_tuStreamSettings, m_umStreamCache, m_mtxStreams, m_mResWndSizes, atTmp, dqAuth), nRet != SIZE_MAX)
            {
                // no GOAWAY frame
                if (nRead > 0)
                    m_strBuffer.append(spBuffer.get() + (nReadSave - nRead), nRead);
            }
            else
                pTcpSocket->Close();
            return;
        }
        else
        {
            size_t nWriteOffset = 0;

            if (m_bEndOfHeader == false)
            {
            next:
                char* pEndOfLine = strstr(spBuffer.get(), "\r\n");
                if (pEndOfLine == nullptr)    // No found
                {
                    m_strBuffer.append(spBuffer.get(), nRead);
                    return;
                }

                if (pEndOfLine == spBuffer.get())   // End of Header
                {
                    m_bEndOfHeader = true, nRead -= 2, nWriteOffset += 2;

                    auto status = m_umRespHeader.find(":status");
                    auto upgrade = m_umRespHeader.find("upgrade");
                    if (status != m_umRespHeader.end() && stoi(status->second) == 101 && upgrade != m_umRespHeader.end() && upgrade->second == "h2c")
                    {
                        m_bIsHttp2 = true;
                        copy(pEndOfLine + 2, pEndOfLine + 2 + nRead + 1, spBuffer.get());
                        size_t nRet;
                        if (nRet = Http2StreamProto(m_soMetaDa, spBuffer.get(), nRead, m_qDynTable, m_tuStreamSettings, m_umStreamCache, m_mtxStreams, m_mResWndSizes, atTmp, dqAuth), nRet != SIZE_MAX)
                        {
                            if (nRet > 0)
                                m_strBuffer.append(spBuffer.get(), nRet);
                        }
                        else
                            pTcpSocket->Close();
                        return;
                    }

                    //if ((m_nContentLength == SIZE_MAX || m_nContentLength == 0) && m_nChuncked != 0)    // Server send a content-length from 0 to signal end of header we are done, and we do not have a chunked transfer encoding!
                    //{
                        m_umStreamCache.insert(make_pair(0, STREAMITEM({ 0, deque<DATAITEM>(), move(m_umRespHeader), 0, 0, INITWINDOWSIZE(m_tuStreamSettings) })));
                        EndOfStreamAction(m_soMetaDa, 0, m_umStreamCache, m_tuStreamSettings, m_mtxStreams, m_mResWndSizes, atTmp, m_mxVecData, m_vecData, dqAuth);
                    //    return;
                    //}
                    if ((m_nContentLength == SIZE_MAX || m_nContentLength == 0) && m_nChuncked != 0)    // Server send a content-length from 0 to signal end of header we are done, and we do not have a chunked transfer encoding!
                    {
                        m_mxVecData.lock();
                        m_vecData.emplace_back(unique_ptr<char[]>(nullptr));
                        m_mxVecData.unlock();
                        return;
                    }
                }
                else
                {
                    if (m_umRespHeader.empty() == true) // First line with status code
                    {
                        char *pSecondSpace = nullptr, *pFirstSpace = strchr(spBuffer.get(), ' ');
                        if (pFirstSpace != nullptr && pFirstSpace < pEndOfLine)
                            pSecondSpace = strchr(pFirstSpace + 1, ' ');
                        if (pSecondSpace != nullptr && pSecondSpace < pEndOfLine)
                        {
                            string strStatus(pFirstSpace + 1, pSecondSpace - pFirstSpace - 1);
                            int iStatus = stoi(strStatus);
                            if (iStatus >= 200)
                                m_umRespHeader.emplace_back(make_pair(":status", strStatus));
                            else // 1xx Statuscode
                                pEndOfLine += 2;
                        }
                    }
                    else
                    {
                        char* pColums = strstr(spBuffer.get(), ":");
                        if (pColums != nullptr)
                        {
                            string strHeader(spBuffer.get(), pColums - spBuffer.get());
                            transform(begin(strHeader), end(strHeader), begin(strHeader), ::tolower);
                            auto parResult = m_umRespHeader.emplace(m_umRespHeader.end(), make_pair(strHeader, string(pColums + 1, pEndOfLine - pColums - 1)));
                            if (parResult != m_umRespHeader.end())
                                parResult->second.erase(0, parResult->second.find_first_not_of(" "));
                        }

                        if (m_nContentLength == SIZE_MAX)
                        {
                            auto pHeader = m_umRespHeader.find("content-length");
                            if (pHeader != m_umRespHeader.end())
                                m_nContentLength = stoul(pHeader->second);
                        }

                        if (m_nChuncked == -1)
                        {
                            auto pHeader = m_umRespHeader.find("transfer-encoding");
                            if (pHeader != m_umRespHeader.end())
                            {
                                string strTmp(pHeader->second.size(), 0);
                                transform(begin(pHeader->second), end(pHeader->second), begin(strTmp), ::tolower);
                                m_nChuncked = strTmp == "chunked" ? 0 : 1;
                            }
                        }
                    }

                    nRead -= (pEndOfLine + 2) - spBuffer.get();
                    copy(pEndOfLine + 2, pEndOfLine + 2 + nRead + 1, spBuffer.get());
                    goto next;
                }

                if (nRead == 0)
                    return;
            }

            // Ab hier werden der Content in eine Temp Datei geschrieben

            //if (m_pTmpFileRec.get() == 0)
            //{
            //    m_pTmpFileRec = make_shared<TempFile>();
            //    m_pTmpFileRec.get()->Open();
            //}

            nextchunck:
            bool bLastChunk = false;
            if (m_nChuncked == 0 && m_nNextChunk == 0 && m_nChunkFooter == 0)
            {
                static regex rx("^([0-9a-fA-F]+)[\\r]?\\n"); //rx("^[\\r]?\\n([0-9a-fA-F]+)[\\r]?\\n");
                match_results<const char*> mr;
                if (regex_search(spBuffer.get() + nWriteOffset, mr, rx, regex_constants::format_first_only) == true && mr[0].matched == true && mr[1].matched == true)
                {
                    m_nNextChunk = strtol(mr[1].str().c_str(), 0, 16);
                    nWriteOffset += mr.length();
                    nRead -= mr.length();
                    m_nChunkFooter = 2;
                    if (m_nNextChunk == 0)
                        bLastChunk = true;
                }
                else
                    OutputDebugString(L"Buffer Fehler\r\n");
            }

            size_t nAnzahlDatenBytes = m_nNextChunk != 0 || m_nChunkFooter != 0 ? min(nRead, m_nNextChunk) : nRead;
            //m_pTmpFileRec.get()->Write(spBuffer.get() + nWriteOffset, nAnzahlDatenBytes);
            if (nAnzahlDatenBytes > 0)
            {
                m_mxVecData.lock();
                m_vecData.emplace_back(make_unique<char[]>(nAnzahlDatenBytes + 4));
                copy(&spBuffer.get()[nWriteOffset], &spBuffer.get()[nAnzahlDatenBytes + nWriteOffset], &m_vecData.back().get()[4]);
                *reinterpret_cast<uint32_t*>(m_vecData.back().get()) = static_cast<uint32_t>(nAnzahlDatenBytes);
                m_mxVecData.unlock();
                m_nContentReceived += nAnzahlDatenBytes;
            }

            if (m_nNextChunk != 0 || m_nChunkFooter != 0)
            {
                nRead -= nAnzahlDatenBytes;
                nWriteOffset += nAnzahlDatenBytes; // m_nNextChunk; War seither so
                m_nNextChunk -= nAnzahlDatenBytes;

                if (nRead == 0 && m_nNextChunk == 0)    // we expect the \r\n after the chunk, the next Data we receive should be a \r\n
                    ;
                if (nRead >= 2 && string(spBuffer.get() + nWriteOffset).substr(0, 2) == "\r\n")
                    nRead -= 2, nWriteOffset += 2, m_nChunkFooter = 0;

                if (nRead == 0 && bLastChunk == false) // No more Bytes received, we need a chunk header if m_nNextChunk = 0, or the rest of the current chunk
                    return;
                // More bytes available in the buffer, we need a chunk header
                if (bLastChunk == false)
                {
                    m_nNextChunk = 0;
                    goto nextchunck;
                }
            }

            if ((m_nContentLength != SIZE_MAX && m_nContentLength == m_nContentReceived)
                || (m_nChuncked == 0 && m_nNextChunk == 0))
            {
                //m_pTmpFileRec.get()->Flush();
                //m_umStreamCache.insert(make_pair(0, STREAMITEM({ 0, deque<DATAITEM>(), move(m_umRespHeader), 0, 0, INITWINDOWSIZE(m_tuStreamSettings) })));
                //EndOfStreamAction(m_soMetaDa, 0, m_umStreamCache, m_tuStreamSettings, m_mtxStreams, m_mResWndSizes, atomic_bool(), m_mxVecData, m_vecData, deque<AUTHITEM>());
                m_mxVecData.lock();
                m_vecData.emplace_back(unique_ptr<char[]>(nullptr));
                m_mxVecData.unlock();
            }
        }
    }
}

void HttpFetch::SocketError(BaseSocket* const pBaseSocket)
{
    OutputDebugString(L"Http2Fetch::SocketError\r\n");

    pBaseSocket->Close();
}

void HttpFetch::SocketCloseing(BaseSocket* const pBaseSocket)
{
    OutputDebugString(L"Http2Fetch::SocketCloseing\r\n");

    if (m_fnNotify != nullptr)
        m_fnNotify(this, nullptr, 0);   // Signal end of data
    if (m_Timer != nullptr)
        m_Timer.get()->Stop();
}

void HttpFetch::OnTimeout(const Timer* const pTimer, void* /*pUser*/)
{
    OutputDebugString(L"Http2Fetch::OnTimeout\r\n");
    m_pcClientCon->Close();
}

void HttpFetch::EndOfStreamAction(const MetaSocketData soMetaDa, const uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex& pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, atomic<bool>& patStop, mutex& pmtxReqdata, deque<unique_ptr<char[]>>& vecData, deque<AUTHITEM>& lstAuthInfo)
{
    m_umRespHeader = move(GETHEADERLIST(StreamList.find(streamId)));

    thread([&](function<size_t(const void*, size_t)> fSocketWrite, const uint32_t streamId)
    {
        //shared_ptr<TempFile>& pTmpFile = m_pTmpFileRec;

        m_fnNotify(this, const_cast<char*>(""), 0);  // Signal to send Header

        auto status = m_umRespHeader.find(":status");
        if (status != m_umRespHeader.end())
            m_uiStatus = stoul(status->second);

        auto encoding = m_umRespHeader.find("content-encoding");
        if (encoding != m_umRespHeader.end())
        {
            if (encoding->second.find("gzip") != string::npos || encoding->second.find("deflate") != string::npos)
            {
                GZipUnpack gzipDecoder;
                if (gzipDecoder.Init() == Z_OK)
                {
                    unique_ptr<unsigned char> dstBuf(new unsigned char[4096]);

                    //shared_ptr<TempFile> pDestFile = make_shared<TempFile>();
                    //pDestFile->Open();

                    int iRet;
                    do
                    {
                        pmtxReqdata.lock();
                        while (vecData.size() == 0)
                        {   // wait until we have a packet again
                            pmtxReqdata.unlock();
                            this_thread::sleep_for(chrono::milliseconds(10));
                            pmtxReqdata.lock();
                        }

                        // get the first packet
                        auto data = move(vecData.front());
                        vecData.pop_front();
                        pmtxReqdata.unlock();
                        if (data == nullptr)
                            break;
                        uint32_t nDataLen = *(reinterpret_cast<uint32_t*>(data.get()));
                        gzipDecoder.InitBuffer(reinterpret_cast<unsigned char*>(data.get() + 4), nDataLen);

                        uint32_t nBytesConverted;
                        do
                        {
                            nBytesConverted = 4096;
                            iRet = gzipDecoder.Deflate(dstBuf.get(), &nBytesConverted);
                            if ((iRet == Z_OK || iRet == Z_STREAM_END) && nBytesConverted != 0)
                                m_fnNotify(this, reinterpret_cast<unsigned char*>(dstBuf.get()), nBytesConverted); //pDestFile->Write(reinterpret_cast<char*>(dstBuf.get()), nBytesConverted);
                        } while (iRet == Z_OK && nBytesConverted == 4096);
                    } while (iRet == Z_OK);

                    //pDestFile->Close();
                    //swap(pDestFile, pTmpFile);
                }
            }
            else if (encoding->second.find("br") != string::npos)
            {
                BrotliDecoderState* s = BrotliDecoderCreateInstance(NULL, NULL, NULL);

                //if (dictionary_path != NULL) {
                //    size_t dictionary_size = 0;
                //    dictionary = ReadDictionary(dictionary_path, &dictionary_size);
                //    BrotliDecoderSetCustomDictionary(s, dictionary_size, dictionary);
                //}

                unique_ptr<unsigned char> srcBuf(new unsigned char[4096]);
                unique_ptr<unsigned char> dstBuf(new unsigned char[4096]);

                //shared_ptr<TempFile> pDestFile = make_shared<TempFile>();
                //pDestFile->Open();

                BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT;
                const uint8_t* input = srcBuf.get();
                uint8_t* next_out = dstBuf.get();
                size_t nBytesRead = 1;
                size_t nBytesOut = 4096;

                while (1)
                {
                    if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
                    {
                        pmtxReqdata.lock();
                        while (vecData.size() == 0)
                        {   // wait until we have a packet again
                            pmtxReqdata.unlock();
                            this_thread::sleep_for(chrono::milliseconds(10));
                            pmtxReqdata.lock();
                        }

                        // get the first packet
                        auto data = move(vecData.front());
                        vecData.pop_front();
                        pmtxReqdata.unlock();
                        if (data == nullptr)
                            break;
                        uint32_t nDataLen = *(reinterpret_cast<uint32_t*>(data.get()));
                        nBytesRead = min(nDataLen, static_cast<uint32_t>(4096));
                        copy(data.get() + 4, data.get() + 4 + nBytesRead, srcBuf.get());
                        if (nBytesRead < nDataLen)
                        {
                            *(reinterpret_cast<uint32_t*>(data.get() + nBytesRead)) = nDataLen - static_cast<uint32_t>(nBytesRead);
                            vecData.push_front(unique_ptr<char[]>(data.get() + nBytesRead));
                        }
                        input = srcBuf.get();
                    }
                    else if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
                    {
                        m_fnNotify(this, reinterpret_cast<unsigned char*>(dstBuf.get()), 4096); //pDestFile->Write(reinterpret_cast<char*>(dstBuf.get()), 4096);
                        nBytesOut = 4096;
                        next_out = dstBuf.get();
                    }
                    else
                        break; /* Error or success. */

                    result = BrotliDecoderDecompressStream(s, &nBytesRead, &input, &nBytesOut, &next_out, 0);
                }
                if (next_out != dstBuf.get())
                    m_fnNotify(this, reinterpret_cast<unsigned char*>(dstBuf.get()), static_cast<uint32_t>(next_out - dstBuf.get())); //pDestFile->Write(reinterpret_cast<char*>(dstBuf.get()), (next_out - dstBuf.get()));

                BrotliDecoderDestroyInstance(s);

                //pDestFile->Close();
                //swap(pDestFile, pTmpFile);

                if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
                    OutputDebugString(L"failed to write output\n");
                else if (result != BROTLI_DECODER_RESULT_SUCCESS)
                    OutputDebugString(L"corrupt input\n"); /* Error or needs more input. */

            }
        }
        else
        {
            while(1)
            {
                pmtxReqdata.lock();
                while (vecData.size() == 0)
                {   // wait until we have a packet again
                    pmtxReqdata.unlock();
                    this_thread::sleep_for(chrono::milliseconds(10));
                    pmtxReqdata.lock();
                }

                // get the first packet
                auto data = move(vecData.front());
                vecData.pop_front();
                pmtxReqdata.unlock();
                if (data == nullptr)
                    break;
                uint32_t nDataLen = *(reinterpret_cast<uint32_t*>(data.get()));
                m_fnNotify(this, reinterpret_cast<unsigned char*>(data.get() + 4), nDataLen);
            }
        }

        if (m_bIsHttp2 == true)
            Http2Goaway(fSocketWrite, 0, streamId/*StreamList.rbegin()->first*/, 0);  // GOAWAY
        m_pcClientCon->Close();
    }, soMetaDa.fSocketWrite, streamId).detach();
}
/*
bool HttpFetch::GetContent(uint8_t Buffer[], uint64_t nBufSize)
{
    if (m_pTmpFileRec == nullptr)
        return false;

    m_pTmpFileRec.get()->Rewind();
    if (m_pTmpFileRec.get()->Read(Buffer, nBufSize) != nBufSize)
        return false;

    return true;
}

HttpFetch::operator TempFile&()
{
    return (*m_pTmpFileRec.get());
}

void HttpFetch::ExchangeTmpFile(shared_ptr<TempFile>& rhs)
{
    rhs.swap(m_pTmpFileRec);
}
*/