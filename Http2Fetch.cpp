// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include "socketlib/SslSocket.h"

#include "H2Proto.h"
#include "GZip.h"
#include "Base64.h"
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
	Http2Fetch() : m_bIsHttp2(false)
    {
    }

    ~Http2Fetch()
    {
    }

    void Fetch(const string strIp, const short sPort)
    {
        m_strHost = strIp;
        m_strPath = "/";

        m_cClientCon.BindFuncConEstablished(bind(&Http2Fetch::Connected, this, _1));
        m_cClientCon.BindFuncBytesRecived(bind(&Http2Fetch::DatenEmpfangen, this, _1));
        m_cClientCon.BindErrorFunction(bind(&Http2Fetch::SocketError, this, _1));
        m_cClientCon.BindCloseFunction(bind(&Http2Fetch::SocketCloseing, this, _1));
        m_cClientCon.SetTrustedRootCertificates("./certs/ca-certificates.crt");
//        m_cClientCon.SetAlpnProtokollNames({ { "h2" },{ "http/1.1" } });
        if (m_cClientCon.Connect(strIp.c_str(), sPort) == false)
            ;// OutputDebugString(L"Verbindung zu xxx.xxx.xxx.xxx konnte nicht hergestellt werden\r\n");
    }

    void Stop()
    {
        m_cClientCon.Close();
    }

    void Connected(TcpSocket* pTcpSocket)
    {
        long nResult = m_cClientCon.CheckServerCertificate(m_strHost.c_str());

        string Protocoll = m_cClientCon.GetSelAlpnProtocol();
        wcout << Protocoll.c_str() << endl;

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
            nReturn = HPackEncode(caBuffer + 9 + nHeaderLen, 2048 - 9 - nHeaderLen, ":authority", m_strHost.c_str());
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
            string strRequest = "GET " + m_strPath + " HTTP/1.1\r\nHost: " + m_strHost + "\r\nUpgrade: h2c\r\nHTTP2-Settings: " + Base64::Encode("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x3\x0\x0\x3\x38\x0\x4\x0\x60\x0\x0", 21, true) + "\r\nAccept: */*\r\nAccept-Encoding: gzip;q=1.0, identity; q=0.5, *;q=0\r\n\r\n";
            pTcpSocket->Write(strRequest.c_str(), strRequest.size());
        }

        m_Timer = make_unique<Timer>(30000, bind(&Http2Fetch::OnTimeout, this, _1));
    }

    void DatenEmpfangen(TcpSocket* pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();
        //wcout << nAvalible << L" Bytes in DatenEmpfangen" << endl;

        if (nAvalible == 0)
        {
            pTcpSocket->Close();
            return;
        }

        shared_ptr<char> spBuffer(new char[nAvalible + 1]);

        uint32_t nRead = pTcpSocket->Read(spBuffer.get(), nAvalible);

        if (nRead > 0)
        {
            if (m_bIsHttp2 == true)
            {
                size_t nRet;
                if (nRet = Http2StreamProto(stSocketInfo, spBuffer.get(), nRead, qDynTable, tuStreamSettings, umStreamCache, &mtxStreams, pTmpFile, nullptr), nRet != SIZE_MAX)
                {
                    if (nRet > 0)
                        m_cSockSystem.PutbackReadData(stSocketInfo.iId, pBuf.get(), nRet);
                    return;
                }
                // After a GOAWAY we terminate the connection
                Http2Goaway(stSocketInfo, 0, umStreamCache.rbegin()->first, 0, CONNFLAGS::CLOSE);  // GOAWAY
            }
            else
                wcout << string().append(spBuffer.get(), nRead).c_str();
        }
    }

	void SocketError(BaseSocket* pBaseSocket)
	{
		wcout << L"Error in Verbindung" << endl;
		pBaseSocket->Close();
	}

    void SocketCloseing(BaseSocket* pBaseSocket)
    {
        wcout << L"SocketCloseing aufgerufen" << endl;
    }

    void OnTimeout(Timer* pTimer)
    {
        m_cClientCon.Close();
    }

    void EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop)
    {
    }

private:
    SslTcpSocket m_cClientCon;
    string m_strHost;
    string m_strPath;
    bool m_bIsHttp2;
    unique_ptr<Timer> m_Timer;
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
	fetch.Fetch("twitter.com", 443);

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

