// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//

#include "socketlib/SslSocket.h"

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


class Http2Fetch
{
public:
	Http2Fetch()
    {
    }

    ~Http2Fetch()
    {
    }

    void Fetch(const string strIp, const short sPort)
    {
        m_cClientCon.BindFuncConEstablished(bind(&Http2Fetch::ClientConnected, this, _1));
        m_cClientCon.BindFuncBytesRecived(bind(&Http2Fetch::ClientDatenEmpfangen, this, _1));
        m_cClientCon.BindErrorFunction(bind(&Http2Fetch::ClientSocketError, this, _1));
        if (m_cClientCon.Connect(strIp.c_str(), sPort) == false)
            ;// OutputDebugString(L"Verbindung zu xxx.xxx.xxx.xxx konnte nicht hergestellt werden\r\n");
    }

    void Stop()
    {
        m_cClientCon.Close();
    }

    void ClientConnected(TcpSocket* pTcpSocket)
    {
        const uint8_t caRequest[] = "GET / HTTP/1.1\r\n\r\n";
        pTcpSocket->Write(caRequest, sizeof(caRequest));
    }

    void ClientDatenEmpfangen(TcpSocket* pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();
        //wcout << nAvalible << L" Bytes in DatenEmpfangen" << endl;

        if (nAvalible == 0)
        {
            pTcpSocket->Close();
            return;
        }

        shared_ptr<uint8_t> spBuffer(new uint8_t[nAvalible + 1]);

        uint32_t nRead = pTcpSocket->Read(spBuffer.get(), nAvalible);

        if (nRead > 0)
        {
            wcout << string().append(reinterpret_cast<char*>(spBuffer.get()), nRead).c_str();
        }
    }

	void ClientSocketError(BaseSocket* pBaseSocket)
	{
		wcout << L"Error in Verbindung" << endl;
		pBaseSocket->Close();
	}

private:
    TcpSocket m_cClientCon;
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
	fetch.Fetch("192.168.161.5", 8080);

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

