// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include "socketlib/SslSocket.h"

#include "H2Proto.h"
#include "GZip.h"
#include "Base64.h"
#include "TempFile.h"
#include "Timer.h"

#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#endif

#ifdef _DEBUG
#pragma comment(lib, "Debug/socketlib.lib")
#else
#pragma comment(lib, "Release/socketlib.lib")
#endif

using namespace std::placeholders;


class Http2Fetch : public Http2Protocol
{
public:
	Http2Fetch() : m_pcClientCon(nullptr), m_bIsHttp2(false), m_sPort(80), m_UseSSL(false), m_bDone(false), m_uiStatus(0), m_bEndOfHeader(false), m_nContentLength(0), m_nChuncked(-1), m_nNextChunk(0)
    {
    }

    ~Http2Fetch()
    {
        if (m_pcClientCon != nullptr)
            delete m_pcClientCon;
    }

    bool Fetch(wstring strAdresse)
    {
        m_bDone = false;

        m_strServer = string(begin(strAdresse), end(strAdresse));
        transform(begin(m_strServer), begin(m_strServer) + 5, begin(m_strServer), tolower);

        if (m_strServer.substr(0, 4).compare("http") != 0)
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

        if (m_strServer.substr(0, 3).compare("://") != 0)
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

        m_pcClientCon->BindFuncConEstablished(bind(&Http2Fetch::Connected, this, _1));
        m_pcClientCon->BindFuncBytesRecived(bind(&Http2Fetch::DatenEmpfangen, this, _1));
        m_pcClientCon->BindErrorFunction(bind(&Http2Fetch::SocketError, this, _1));
        m_pcClientCon->BindCloseFunction(bind(&Http2Fetch::SocketCloseing, this, _1));
        if (m_UseSSL == true)
        {
            m_pcClientCon->SetTrustedRootCertificates("./certs/ca-certificates.crt");
            m_pcClientCon->SetAlpnProtokollNames({ { "h2" },{ "http/1.1" } });
        }

        return m_pcClientCon->Connect(m_strServer.c_str(), m_sPort);
    }

    void Stop()
    {
        m_pcClientCon->Close();
    }

    void Connected(TcpSocket* pTcpSocket)
    {
        string Protocoll;
        if (m_UseSSL == true)
        {
            long nResult = m_pcClientCon->CheckServerCertificate(m_strServer.c_str());

            Protocoll = m_pcClientCon->GetSelAlpnProtocol();
            //wcerr << Protocoll.c_str() << endl;
        }

        if (Protocoll.compare("h2") == 0)
        {
            m_bIsHttp2 = true;
            pTcpSocket->Write("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
            pTcpSocket->Write("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x3\x0\x0\x0\x64\x0\x4\x0\x10\x0\x0", 21);// SETTINGS frame (4) with ParaID(3) and 100 Value and ParaID(4) and 1048576 Value
            pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\xf\x0\x1", 13);      // WINDOW_UPDATE frame (8) with value ?1048576? (minus 65535) == 983041

            char caBuffer[2048];
            size_t nHeaderLen = 0;

            size_t nReturn = HPackEncode(caBuffer + 9, 2048 - 9, ":method", "GET");
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
            nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, "user-agent", "http2fetch version 0.9 beta");
            if (nReturn != SIZE_MAX)
                nHeaderLen += nReturn;
            nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, "accept-encoding", "gzip;q=1.0, identity; q=0.5, *;q=0");
            if (nReturn != SIZE_MAX)
                nHeaderLen += nReturn;
            nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, "accept", "*/*");
            if (nReturn != SIZE_MAX)
                nHeaderLen += nReturn;

            BuildHttp2Frame(caBuffer, nHeaderLen, 0x1, 0x5, 1);
            pTcpSocket->Write(caBuffer, nHeaderLen + 9);
        }
        else
        {
            string strRequest = "GET " + m_strPath + " HTTP/1.1\r\nHost: " + m_strServer + "\r\nUpgrade: h2c\r\nHTTP2-Settings: " + Base64::Encode("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x3\x0\x0\x3\x38\x0\x4\x0\x60\x0\x0", 21, true) + "\r\nAccept: */*\r\nAccept-Encoding: gzip;q=1.0, identity; q=0.5, *;q=0\r\n\r\n";
            pTcpSocket->Write(strRequest.c_str(), strRequest.size());
        }

        m_Timer = make_unique<Timer>(30000, bind(&Http2Fetch::OnTimeout, this, _1));
        m_soMetaDa = { m_pcClientCon->GetClientAddr(), m_pcClientCon->GetClientPort(), m_pcClientCon->GetInterfaceAddr(), m_pcClientCon->GetInterfacePort(), m_pcClientCon->IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, m_Timer.get()) };
    }

    void DatenEmpfangen(TcpSocket* pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();
        //wcerr << nAvalible << L" Bytes in DatenEmpfangen" << endl;

        if (nAvalible == 0)
        {
            pTcpSocket->Close();
            return;
        }

        shared_ptr<char> spBuffer(new char[m_strBuffer.size() + nAvalible + 1]);
        copy(begin(m_strBuffer), begin(m_strBuffer) + m_strBuffer.size(), spBuffer.get());

        uint32_t nRead = pTcpSocket->Read(spBuffer.get() + m_strBuffer.size(), nAvalible);

        if (nRead > 0)
        {
            m_Timer->Reset();

            nRead += m_strBuffer.size();
            m_strBuffer.clear();

            if (m_bIsHttp2 == true)
            {
                size_t nRet;
                if (nRet = Http2StreamProto(m_soMetaDa, spBuffer.get(), nRead, m_qDynTable, m_tuStreamSettings, m_umStreamCache, &m_mtxStreams, m_pTmpFile, nullptr), nRet != SIZE_MAX)
                {
                    // no GOAWAY frame
                    if (nRet > 0)
                        m_strBuffer.append(spBuffer.get(), nRet);
                }
                return;
            }
            else
            {
                uint32_t nWriteOffset = 0;

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
                        if (status != m_umRespHeader.end() && stoi(status->second) == 101 && upgrade != m_umRespHeader.end() && upgrade->second.compare("h2c") == 0)
                        {
                            m_bIsHttp2 = true;
                            copy(pEndOfLine + 2, pEndOfLine + 2 + nRead + 1, spBuffer.get());
                            size_t nRet;
                            if (nRet = Http2StreamProto(m_soMetaDa, spBuffer.get(), nRead, m_qDynTable, m_tuStreamSettings, m_umStreamCache, &m_mtxStreams, m_pTmpFile, nullptr), nRet != SIZE_MAX)
                            {
                                if (nRet > 0)
                                    m_strBuffer.append(spBuffer.get(), nRet);
                            }
                            return;
                        }

                        if (m_nContentLength == 0)    // Server send a content-lentgh from 0 to signal end of header we are done!
                        {
                            m_umStreamCache.insert(make_pair(0, STREAMITEM(0, deque<DATAITEM>(), move(m_umRespHeader), 0, 0, make_shared<atomic<int32_t>>(INITWINDOWSIZE(m_tuStreamSettings)))));
                            EndOfStreamAction(m_soMetaDa, 0, m_umStreamCache, m_tuStreamSettings, &m_mtxStreams, m_pTmpFile, nullptr);
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
                                m_umRespHeader.insert(make_pair(string(":status"), string(pFirstSpace + 1, pSecondSpace - pFirstSpace - 1)));
                        }
                        else
                        {
                            char* pColums = strstr(spBuffer.get(), ":");
                            if (pColums != nullptr)
                            {
                                string strHeader(spBuffer.get(), pColums - spBuffer.get());
                                transform(begin(strHeader), end(strHeader), begin(strHeader), tolower);
                                auto parResult = m_umRespHeader.insert(make_pair(strHeader, string(pColums + 1, pEndOfLine - pColums - 1)));
                                if (parResult != m_umRespHeader.end())
                                    while (parResult->second.at(0) == ' ') parResult->second.replace(parResult->second.find(' '), 1, "");
                            }

                            if (m_nContentLength == 0)
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
                                    transform(begin(pHeader->second), end(pHeader->second), begin(strTmp), tolower);
                                    m_nChuncked = strTmp.compare("chunked");
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

                if (m_pTmpFile.get() == 0)
                {
                    m_pTmpFile = make_unique<TempFile>();
                    m_pTmpFile.get()->Open();
                    if (m_nChuncked == 0) // Chunked transfer ecoding ist enabled
                        nRead += 2, nWriteOffset -= 2;
                }


            nextchunck:
                if (m_nChuncked == 0 && m_nNextChunk == 0)
                {
                    static regex rx("^[\\r]?\\n([0-9a-fA-F]+)[\\r]?\\n");
                    match_results<const char*> mr;
                    if (regex_search(spBuffer.get() + nWriteOffset, mr, rx, regex_constants::format_first_only) == true && mr[0].matched == true && mr[1].matched == true)
                    {
                        m_nNextChunk = strtol(mr[1].str().c_str(), 0, 16);
                        nWriteOffset += mr.length();
                        nRead -= mr.length();
                    }
                    else
                        OutputDebugString(L"Buffer Fehler\r\n");
                }

                m_pTmpFile.get()->Write(spBuffer.get() + nWriteOffset, m_nNextChunk != 0 ? min(nRead, m_nNextChunk) : nRead);
                if (m_nNextChunk != 0)
                {
                    size_t nSaveLen = nRead;
                    static size_t nSaveChunk = m_nNextChunk;
                    nRead -= min(nRead, m_nNextChunk);
                    if (nRead != 0)  // We wrote only the rest of the chunck
                        nWriteOffset += m_nNextChunk;
                    m_nNextChunk -= min(nSaveLen, m_nNextChunk);
                    if (nRead != 0)
                        goto nextchunck;
                }

                if ((m_nContentLength != SIZE_MAX && m_nContentLength == m_pTmpFile->GetFileLength())
                    || (m_nChuncked == 0 && m_nNextChunk == 0))
                {
                    m_pTmpFile.get()->Flush();
                    m_umStreamCache.insert(make_pair(0, STREAMITEM(0, deque<DATAITEM>(), move(m_umRespHeader), 0, 0, make_shared<atomic<int32_t>>(INITWINDOWSIZE(m_tuStreamSettings)))));
                    EndOfStreamAction(m_soMetaDa, 0, m_umStreamCache, m_tuStreamSettings, &m_mtxStreams, m_pTmpFile, nullptr);
                }

                //cerr << spBuffer.get() << flush;// OutputDebugStringA(spBuffer.get());
            }
        }
    }

	void SocketError(BaseSocket* pBaseSocket)
	{
        OutputDebugString(L"Http2Fetch::SocketError\r\n");
		wcerr << L"Error in Verbindung" << endl;
		pBaseSocket->Close();
	}

    void SocketCloseing(BaseSocket* pBaseSocket)
    {
        OutputDebugString(L"Http2Fetch::SocketCloseing\r\n");
        wcerr << L"SocketCloseing aufgerufen" << endl;
        m_bDone = true;
    }

    void OnTimeout(Timer* pTimer)
    {
        OutputDebugString(L"Http2Fetch::OnTimeout\r\n");
        m_pcClientCon->Close();
    }

    void EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop)
    {
        m_umRespHeader = move(GETHEADERLIST(StreamList.find(streamId)->second));

        for (const auto& Header : m_umRespHeader)
            wcerr << Header.first.c_str() << L": " << Header.second.c_str() << endl;
        wcerr << endl;

        auto status = m_umRespHeader.find(":status");
        if (status != m_umRespHeader.end())
            m_uiStatus = stoul(status->second);

        auto encoding = m_umRespHeader.find("content-encoding");
        if (encoding != m_umRespHeader.end() && encoding->second.find("gzip") != string::npos)
        {
            GZipUnpack gzipDecoder;
            if (gzipDecoder.Init() == Z_OK)
            {
                unique_ptr<unsigned char> srcBuf(new unsigned char[4096]);
                unique_ptr<unsigned char> dstBuf(new unsigned char[4096]);

                pTmpFile->Rewind();
                shared_ptr<TempFile> pDestFile = make_unique<TempFile>();
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

#ifdef _DEBUG
        if (m_uiStatus == 200)
        {
            auto contenttype = m_umRespHeader.find("content-type");
            if (contenttype != m_umRespHeader.end() && contenttype->second.find("text/") != string::npos)
                cout << (*pTmpFile) << flush;
        }
#endif
        if (m_bIsHttp2 == true)
            m_bDone = true, Http2Goaway(soMetaDa.fSocketWrite, 0, StreamList.rbegin()->first, 0);  // GOAWAY
        else
            m_pcClientCon->Close();
    }

    bool RequestFinished()
    {
        return m_bDone;
    }

private:
    SslTcpSocket*        m_pcClientCon;
    string               m_strServer;
    short                m_sPort;
    string               m_strPath;
    bool                 m_UseSSL;
    bool                 m_bDone;
    uint32_t             m_uiStatus;

    bool                 m_bIsHttp2;
    deque<HEADERENTRY>   m_qDynTable;
    mutex                m_mtxStreams;
    STREAMLIST           m_umStreamCache;
    STREAMSETTINGS       m_tuStreamSettings = make_tuple(UINT32_MAX, 65535, 16384, UINT32_MAX);
    shared_ptr<TempFile> m_pTmpFile;
    unique_ptr<Timer>    m_Timer;
    MetaSocketData       m_soMetaDa;
    string               m_strBuffer;
    HEADERLIST           m_umRespHeader;

    bool                 m_bEndOfHeader;
    uint64_t             m_nContentLength;
    int                  m_nChuncked;
    size_t               m_nNextChunk;
};


int main(int argc, const char* argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));

    //_setmode(_fileno(stdout), _O_U16TEXT);
#endif

    //locale::global(std::locale(""));

	Http2Fetch fetch;
	//fetch.Fetch(L"https://twitter.com/");
    //fetch.Fetch(L"https://www.microsoft.com/de-de");
    //fetch.Fetch(L"https://192.168.161.1/");
    //fetch.Fetch(L"https://192.66.65.226/");
    fetch.Fetch(L"https://www.google.de/");

    while (fetch.RequestFinished() == false)
        this_thread::sleep_for(chrono::milliseconds(10));
    wcerr << L"Request beendet!" << endl;

#if defined(_WIN32) || defined(_WIN64)
    //while (::_kbhit() == 0)
    //    this_thread::sleep_for(chrono::milliseconds(1));
    _getch();
#else
    getchar();
#endif

	fetch.Stop();

    return 0;
}

