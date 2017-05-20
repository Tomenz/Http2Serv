
#include <regex>

#include "HttpFetch.h"
#include "GZip.h"
#include "Base64.h"
#include <brotli/decode.h>

#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "x64/Debug/socketlib")
#pragma comment(lib, "x64/Debug/brotli")
#else
#pragma comment(lib, "Debug/socketlib")
#pragma comment(lib, "Debug/brotli")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "x64/Release/socketlib")
#pragma comment(lib, "x64/Release/brotli")
#else
#pragma comment(lib, "Release/socketlib")
#pragma comment(lib, "Release/brotli")
#endif
#endif

using namespace std::placeholders;

HttpFetch::HttpFetch(function<void(HttpFetch*, void*)> fnNotify, void* vpUserData) : m_fnNotify(fnNotify), m_vpUserData(vpUserData), m_pcClientCon(nullptr), m_bIsHttp2(false), m_sPort(80), m_UseSSL(false), m_uiStatus(0), m_bEndOfHeader(false), m_nContentLength(SIZE_MAX), m_nChuncked(-1), m_nNextChunk(0), m_nChunkFooter(0)
{
}


HttpFetch::~HttpFetch()
{
    if (m_pcClientCon != nullptr)
        delete m_pcClientCon;
}

bool HttpFetch::Fetch(string strAdresse, string strMethode /*= "GET"*/)
{
    m_strServer = strAdresse;
    m_strMethode = strMethode;

    transform(begin(m_strServer), begin(m_strServer) + 5, begin(m_strServer), tolower);
    transform(begin(m_strMethode), end(m_strMethode), begin(m_strMethode), toupper);

    if (m_pTmpFileSend.get() != 0)
    {
        m_pTmpFileSend.get()->Flush();
        m_pTmpFileSend->Rewind();
    }

    if (m_strServer.compare(0, 4, "http") != 0)
        return false;

    m_strServer.erase(0, 4);
    if (m_strServer[0] == 's')
    {
        m_sPort = 443, m_UseSSL = true;
        m_strServer.erase(0, 1);

        m_pcClientCon = new SslTcpSocket();
    }
    else
        m_pcClientCon = reinterpret_cast<SslTcpSocket*>(new TcpSocket());

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

    m_pcClientCon->BindFuncConEstablished(bind(&HttpFetch::Connected, this, _1));
    m_pcClientCon->BindFuncBytesRecived(bind(&HttpFetch::DatenEmpfangen, this, _1));
    m_pcClientCon->BindErrorFunction(bind(&HttpFetch::SocketError, this, _1));
    m_pcClientCon->BindCloseFunction(bind(&HttpFetch::SocketCloseing, this, _1));
    if (m_UseSSL == true)
    {
        m_pcClientCon->SetTrustedRootCertificates("./certs/ca-certificates.crt");
        m_pcClientCon->SetAlpnProtokollNames({ { "h2" },{ "http/1.1" } });
    }

    return m_pcClientCon->Connect(m_strServer.c_str(), m_sPort);
}

bool HttpFetch::AddContent(void* pBuffer, uint64_t nBufSize)
{
    if (m_pTmpFileSend.get() == 0)
    {
        m_pTmpFileSend = make_shared<TempFile>();
        m_pTmpFileSend.get()->Open();
    }

    m_pTmpFileSend.get()->Write(pBuffer, nBufSize);

    return true;
}

void HttpFetch::Stop()
{
    m_pcClientCon->Close();
}

void HttpFetch::Connected(TcpSocket* pTcpSocket)
{
    string Protocoll;
    if (m_UseSSL == true)
    {
        long nResult = m_pcClientCon->CheckServerCertificate(m_strServer.c_str());

        Protocoll = m_pcClientCon->GetSelAlpnProtocol();
    }

    if (Protocoll == "h2")
    {
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
            transform(begin(itPair.first), end(itPair.first), begin(itPair.first), tolower);
            nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, itPair.first.c_str(), itPair.second.c_str());
            if (nReturn == SIZE_MAX)
                break;
            nHeaderLen += nReturn;
        }

        BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, 1);
        pTcpSocket->Write(caBuffer, nHeaderLen + 9);
    }
    else
    {
        string strRequest = m_strMethode + " " + m_strPath + " HTTP/1.1\r\nHost: " + m_strServer + "\r\n";
        for (auto& itPair : m_umAddHeader)
            strRequest += itPair.first + ": " + itPair.second + "\r\n";
        strRequest += "\r\n";
        pTcpSocket->Write(strRequest.c_str(), strRequest.size());

        if (m_pTmpFileSend.get() != 0)
        {
            auto apBuf = make_unique<unsigned char[]>(0x4000 + 9 + 2);
            streamsize nBytesRead = m_pTmpFileSend.get()->Read(apBuf.get(), 0x4000);
            while (nBytesRead != 0)
            {
                pTcpSocket->Write(apBuf.get(), static_cast<size_t>(nBytesRead));
                nBytesRead = m_pTmpFileSend.get()->Read(apBuf.get(), 0x4000);
            }
            m_pTmpFileSend.get()->Close();
        }
    }

    m_Timer = make_unique<Timer>(30000, bind(&HttpFetch::OnTimeout, this, _1));
    m_soMetaDa = { m_pcClientCon->GetClientAddr(), m_pcClientCon->GetClientPort(), m_pcClientCon->GetInterfaceAddr(), m_pcClientCon->GetInterfacePort(), m_pcClientCon->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, m_Timer.get()) };
}

void HttpFetch::DatenEmpfangen(TcpSocket* pTcpSocket)
{
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
            size_t nRet;
            if (nRet = Http2StreamProto(m_soMetaDa, spBuffer.get(), nRead, m_qDynTable, m_tuStreamSettings, m_umStreamCache, &m_mtxStreams, m_pTmpFileRec, nullptr), nRet != SIZE_MAX)
            {
                // no GOAWAY frame
                if (nRet > 0)
                    m_strBuffer.append(spBuffer.get(), nRet);
            }
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
                        if (nRet = Http2StreamProto(m_soMetaDa, spBuffer.get(), nRead, m_qDynTable, m_tuStreamSettings, m_umStreamCache, &m_mtxStreams, m_pTmpFileRec, nullptr), nRet != SIZE_MAX)
                        {
                            if (nRet > 0)
                                m_strBuffer.append(spBuffer.get(), nRet);
                        }
                        return;
                    }

                    if ((m_nContentLength == SIZE_MAX || m_nContentLength == 0) && m_nChuncked != 0)    // Server send a content-length from 0 to signal end of header we are done, and we do not have a chunked transfer encoding!
                    {
                        m_umStreamCache.insert(make_pair(0, STREAMITEM(0, deque<DATAITEM>(), move(m_umRespHeader), 0, 0, make_shared<atomic_size_t>(INITWINDOWSIZE(m_tuStreamSettings)))));
                        EndOfStreamAction(m_soMetaDa, 0, m_umStreamCache, m_tuStreamSettings, &m_mtxStreams, m_pTmpFileRec, nullptr);
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

            if (m_pTmpFileRec.get() == 0)
            {
                m_pTmpFileRec = make_shared<TempFile>();
                m_pTmpFileRec.get()->Open();
            }

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
            m_pTmpFileRec.get()->Write(spBuffer.get() + nWriteOffset, nAnzahlDatenBytes);

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

            if ((m_nContentLength != SIZE_MAX && m_nContentLength == m_pTmpFileRec->GetFileLength())
                || (m_nChuncked == 0 && m_nNextChunk == 0))
            {
                m_pTmpFileRec.get()->Flush();
                m_umStreamCache.insert(make_pair(0, STREAMITEM(0, deque<DATAITEM>(), move(m_umRespHeader), 0, 0, make_shared<atomic_size_t>(INITWINDOWSIZE(m_tuStreamSettings)))));
                EndOfStreamAction(m_soMetaDa, 0, m_umStreamCache, m_tuStreamSettings, &m_mtxStreams, m_pTmpFileRec, nullptr);
            }
        }
    }
}

void HttpFetch::SocketError(BaseSocket* pBaseSocket)
{
    OutputDebugString(L"Http2Fetch::SocketError\r\n");

    pBaseSocket->Close();
}

void HttpFetch::SocketCloseing(BaseSocket* pBaseSocket)
{
    OutputDebugString(L"Http2Fetch::SocketCloseing\r\n");

    if (m_fnNotify != nullptr)
        m_fnNotify(this, m_vpUserData);
    m_Timer.get()->Stop();
}

void HttpFetch::OnTimeout(Timer* pTimer)
{
    OutputDebugString(L"Http2Fetch::OnTimeout\r\n");
    m_pcClientCon->Close();
}

void HttpFetch::EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop)
{
    m_umRespHeader = move(GETHEADERLIST(StreamList.find(streamId)));

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
                unique_ptr<unsigned char> srcBuf(new unsigned char[4096]);
                unique_ptr<unsigned char> dstBuf(new unsigned char[4096]);

                pTmpFile->Rewind();
                shared_ptr<TempFile> pDestFile = make_shared<TempFile>();
                pDestFile->Open();

                int iRet;
                do
                {
                    int nBytesRead = static_cast<int>(pTmpFile->Read(srcBuf.get(), 4096));
                    if (nBytesRead == 0)
                        break;

                    gzipDecoder.InitBuffer(srcBuf.get(), nBytesRead);

                    uint32_t nBytesConverted;
                    do
                    {
                        nBytesConverted = 4096;
                        iRet = gzipDecoder.Deflate(dstBuf.get(), &nBytesConverted);
                        if ((iRet == Z_OK || iRet == Z_STREAM_END) && nBytesConverted != 0)
                            pDestFile->Write(reinterpret_cast<char*>(dstBuf.get()), nBytesConverted);
                    } while (iRet == Z_OK && nBytesConverted == 4096);
                } while (iRet == Z_OK);

                pDestFile->Close();
                swap(pDestFile, pTmpFile);
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

            pTmpFile->Rewind();
            shared_ptr<TempFile> pDestFile = make_shared<TempFile>();
            pDestFile->Open();

            BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT;
            const uint8_t* input = srcBuf.get();
            uint8_t* next_out = dstBuf.get();
            size_t nBytesRead = 1;
            size_t nBytesOut = 4096;

            while (1)
            {
                if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
                {
                    //if (nBytesRead == 0)
                    //    break;
                    nBytesRead = static_cast<size_t>(pTmpFile->Read(srcBuf.get(), 4096));
                    input = srcBuf.get();
                }
                else if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
                {
                    pDestFile->Write(reinterpret_cast<char*>(dstBuf.get()), 4096);
                    nBytesOut = 4096;
                    next_out = dstBuf.get();
                }
                else
                    break; /* Error or success. */

                result = BrotliDecoderDecompressStream(s, &nBytesRead, &input, &nBytesOut, &next_out, 0);
            }
            if (next_out != dstBuf.get())
                pDestFile->Write(reinterpret_cast<char*>(dstBuf.get()), (next_out - dstBuf.get()));

            BrotliDecoderDestroyInstance(s);

            pDestFile->Close();
            swap(pDestFile, pTmpFile);

            if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
                OutputDebugString(L"failed to write output\n");
            else if (result != BROTLI_DECODER_RESULT_SUCCESS)
                OutputDebugString(L"corrupt input\n"); /* Error or needs more input. */

        }
    }

    if (m_bIsHttp2 == true)
        Http2Goaway(soMetaDa.fSocketWrite, 0, StreamList.rbegin()->first, 0);  // GOAWAY
    m_pcClientCon->Close();
}

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
