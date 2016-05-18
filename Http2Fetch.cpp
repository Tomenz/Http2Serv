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
	Http2Fetch() : m_bIsHttp2(false), m_sPort(80), m_UseSSL(false), m_bDone(false), m_uiStatus(0)
    {
    }

    ~Http2Fetch()
    {
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
        }

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

        m_cClientCon.BindFuncConEstablished(bind(&Http2Fetch::Connected, this, _1));
        m_cClientCon.BindFuncBytesRecived(bind(&Http2Fetch::DatenEmpfangen, this, _1));
        m_cClientCon.BindErrorFunction(bind(&Http2Fetch::SocketError, this, _1));
        m_cClientCon.BindCloseFunction(bind(&Http2Fetch::SocketCloseing, this, _1));
        m_cClientCon.SetTrustedRootCertificates("./certs/ca-certificates.crt");
        m_cClientCon.SetAlpnProtokollNames({ { "h2" },{ "http/1.1" } });

        return m_cClientCon.Connect(m_strServer.c_str(), m_sPort);
    }

    void Stop()
    {
        m_cClientCon.Close();
    }

    void Connected(TcpSocket* pTcpSocket)
    {
        long nResult = m_cClientCon.CheckServerCertificate(m_strServer.c_str());

        string Protocoll = m_cClientCon.GetSelAlpnProtocol();
        //wcerr << Protocoll.c_str() << endl;

        if (Protocoll.compare("h2") == 0)
        {
            m_bIsHttp2 = true;
            pTcpSocket->Write("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24);
            pTcpSocket->Write("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x3\x0\x0\x3\x38\x0\x4\x0\x60\x0\x0", 21);// SETTINGS frame (4) with ParaID(3) and 1000 Value and ParaID(4) and 6291456 Value
            pTcpSocket->Write("\x0\x0\x4\x8\x0\x0\x0\x0\x0\x0\x9f\x0\x1", 13);      // WINDOW_UPDATE frame (8) with value ?10420225? (minus 65535)

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
        m_soMetaDa = { m_cClientCon.GetClientAddr(), m_cClientCon.GetClientPort(), m_cClientCon.GetInterfaceAddr(), m_cClientCon.GetInterfacePort(), m_cClientCon.IsSslConnection(), bind(&TcpSocket::Write, pTcpSocket, _1, _2), bind(&TcpSocket::Close, pTcpSocket), bind(&TcpSocket::GetOutBytesInQue, pTcpSocket), bind(&Timer::Reset, m_Timer.get()) };
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
                    if (nRet > 0)
                        m_strBuffer.append(spBuffer.get(), nRet);
                    return;
                }
                // After a GOAWAY we terminate the connection
                return;
            }
            else
                wcerr << string().append(spBuffer.get(), nRead).c_str();
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
        m_cClientCon.Close();
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
                wcout << (*pTmpFile) << flush;
        }
#endif
        m_cClientCon.Close();
    }

    bool RequestFinished()
    {
        return m_bDone;
    }

private:
    SslTcpSocket         m_cClientCon;
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
    STREAMSETTINGS       m_tuStreamSettings = make_tuple(UINT32_MAX, 65535, 16384);
    shared_ptr<TempFile> m_pTmpFile;
    unique_ptr<Timer>    m_Timer;
    MetaSocketData       m_soMetaDa;
    string               m_strBuffer;
    HEADERLIST           m_umRespHeader;
};


int main(int argc, const char* argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));

    _setmode(_fileno(stdout), _O_U16TEXT);
#endif

    //locale::global(std::locale(""));

	Http2Fetch fetch;
	fetch.Fetch(L"https://twitter.com/");

    while (fetch.RequestFinished() == false)
        this_thread::sleep_for(chrono::milliseconds(1));
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

