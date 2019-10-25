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

#include <ConfFile.h>
#include <HttpServ.h>
#include <Timer.h>

#ifdef _DEBUG
#pragma comment(lib, "Debug/socketlib.lib")
#else
#pragma comment(lib, "Release/socketlib.lib")
#endif

using namespace std::placeholders;

#ifndef _UTFCONVERTER
#define _UTFCONVERTER
std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> Utf8Converter;
#endif

class EchoServer
{
public:
    EchoServer()
    {
    }

    ~EchoServer()
    {
        while (m_vConnections.size())
        {
            delete m_vConnections.begin()->first;
        }
    }

    void Start()
    {
        m_Server.BindNewConnection(bind(&EchoServer::NeueVerbindungen, this, _1, _2));
        m_Server.BindErrorFunction(bind(&EchoServer::SocketError, this, _1));
        if (m_Server.Start("0.0.0.0", 80) == false)
        {
        }

        m_SslServer.BindNewConnection(bind(&EchoServer::NeueSslVerbindungen, this, _1, _2));
        m_SslServer.BindErrorFunction(bind(&EchoServer::SslSocketError, this, _1));
        m_SslServer.AddCertificat("./certs/ca-root.pem", "./certs/192-168-161-1.pem", "./certs/192-168-161-1-key.pem");
        if (m_SslServer.Start("0.0.0.0", 443) == false)
        {
        }

        /*
        m_Server1.BindNewConnection(bind(&EchoServer::NeueVerbindungen, this, _1, _2));
        m_Server1.BindErrorFunction(bind(&EchoServer::SocketError, this, _1));
        if (m_Server1.Start("0.0.0.0", 19) == false)
        {
        }*/

        m_cClientCon.BindFuncConEstablished(bind(&EchoServer::ClientConnected, this, _1));
        m_cClientCon.BindFuncBytesReceived(bind(&EchoServer::ClientDatenEmpfangen, this, _1));
        m_cClientCon.BindErrorFunction(bind(&EchoServer::ClientSocketError, this, _1));
        if (m_cClientCon.Connect("192.168.161.5", 8080) == false)
            ;// OutputDebugString(L"Verbindung zu xxx.xxx.xxx.xxx konnte nicht hergestellt werden\r\n");

        m_cUpnP.BindFuncBytesReceived(bind(&EchoServer::UpnPDatenEmpfangen, this, _1));
        m_cUpnP.Create("0.0.0.0", 1900);
        m_cUpnP.AddToMulticastGroup("239.255.255.250");

		m_cAdv.BindFuncBytesReceived(bind(&EchoServer::UpnPDatenEmpfangen, this, _1));
		m_cAdv.Create("0.0.0.0", 0);

        string strSend1("M-SEARCH * HTTP/1.1\r\nHost: 239.255.255.250:1900\r\nST: ssdp:all\r\nMan: \"ssdp:discover\"\r\nMX: 3\r\nUSER-AGENT: windows/6.1 UPnP/1.1 SOT/1.0\r\n\r\n");
		m_cAdv.Write(strSend1.c_str(), strSend1.size(), "239.255.255.250:1900");

    }

    void Stop()
    {
        m_Server.Close();
//        m_Server1.Close();

        m_mtConnections.lock();
        for (unordered_map<TcpSocket*, tuple<Timer*, string>>::iterator it = begin(m_vConnections); it != end(m_vConnections); ++it)
            it->first->Close();
        m_mtConnections.unlock();

        m_cClientCon.Close();
		m_cUpnP.RemoveFromMulticastGroup("239.255.255.250");
        m_cUpnP.Close();

        while (m_vConnections.size())
            this_thread::sleep_for(chrono::milliseconds(1));
    }

    void NeueVerbindungen(TcpServer* pTcpServer, int nCountNewConnections)
    {
        //cout << nCountNewConnections << " neue Verbindung bekommen" << endl;

        for (int i = 0; i < nCountNewConnections; ++i)
        {
            TcpSocket* pSocket = pTcpServer->GetNextPendingConnection();
            if (pSocket != nullptr)
            {
                pSocket->BindFuncBytesReceived(bind(&EchoServer::DatenEmpfangen, this, _1));
                pSocket->BindErrorFunction(bind(&EchoServer::SocketError, this, _1));
                pSocket->BindCloseFunction(bind(&EchoServer::SocketCloseing, this, _1));
                lock_guard<mutex> lock(m_mtConnections);
                m_vConnections.emplace(pSocket, make_tuple(new Timer(10000, bind(&EchoServer::Timeout, this, _1)), string()));
                //m_vConnections.emplace(pSocket, make_tuple(nullptr, string()));
                pSocket->StartReceiving();
            }
        }
    }

    void DatenEmpfangen(TcpSocket* pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();
        //cout << nAvalible << " Bytes in DatenEmpfangen" << endl;

        if (nAvalible == 0)
        {
            pTcpSocket->Close();
            return;
        }

        shared_ptr<uint8_t> spBuffer(new uint8_t[nAvalible]);

        uint32_t nRead = pTcpSocket->Read(spBuffer.get(), nAvalible);

        if (nRead > 0)
        {
            m_mtConnections.lock();
            unordered_map<TcpSocket*, tuple<Timer*, string>>::iterator item = m_vConnections.find(pTcpSocket);
            if (item != end(m_vConnections))
            {
                if (get<0>(item->second) != nullptr)
                    get<0>(item->second)->Reset();
                get<1>(item->second).append(reinterpret_cast<char*>(spBuffer.get()), nRead);
                size_t nPos = get<1>(item->second).find("\r\n\r\n");
                if (nPos != string::npos)
                {
                    get<1>(item->second).erase(0, nPos + 4);
                    if (get<0>(item->second) != nullptr)
                        get<0>(item->second)->Stop();
                    m_mtConnections.unlock();

                    basic_fstream<uint8_t>  fin("./index.html", ios::in | ios::binary);
                    if (fin.bad() == false)
                    {
                        fin.seekg(0, ios_base::end);
                        uint32_t nFSize = static_cast<uint32_t>(fin.tellg());
                        fin.seekg(0, ios_base::beg);

                        shared_ptr<uint8_t> spBuffer1(new uint8_t[nFSize]);
                        fin.read(spBuffer1.get(), nFSize);
                        //basic_string<uint8_t> strFile((istreambuf_iterator<uint8_t>(fin)), istreambuf_iterator<uint8_t>());
                        fin.close();

                        //basic_stringstream<uint8_t> ss;
                        stringstream ss;
                        ss << "HTTP/1.0 200 OK\r\nContent-Length: " << nFSize << "\r\nContent-Type: text/html\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nExpires: Mon, 03 Apr 1961 05:00:00 GMT\r\nConnection: close\r\n\r\n";
                        pTcpSocket->Write(reinterpret_cast<const uint8_t*>(ss.str().c_str()), ss.str().size());
                        //pTcpSocket->Write(ss.str().c_str(), ss.str().size());

                        pTcpSocket->Write(spBuffer1.get(), nFSize);
                        //pTcpSocket->Write(strFile.c_str(), strFile.size());
                    }
                    else
                    {
                        basic_stringstream<uint8_t> ss;
                        ss << "HTTP/1.0 404 Not found\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nExpires: Mon, 03 Apr 1961 05:00:00 GMT\r\nConnection: close\r\n\r\n";
                        pTcpSocket->Write(ss.str().c_str(), ss.str().size());
                    }

                    pTcpSocket->Close();

                    return;
                }
            }
            m_mtConnections.unlock();

/*
            pTcpSocket->Write(spBuffer.get(), nRead);

            if (spBuffer.get()[0] == 3)
                pTcpSocket->Close();*/
        }
    }

    void SocketError(BaseSocket* pBaseSocket)
    {
        cout << "Error in Verbindung" << endl;
        pBaseSocket->Close();
    }

    void SocketCloseing(BaseSocket* pBaseSocket)
    {
        //cout << "Socket closing" << endl;

        lock_guard<mutex> lock(m_mtConnections);
        unordered_map<TcpSocket*, tuple<Timer*, string>>::iterator item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            if (get<0>(item->second) != nullptr)
                delete get<0>(item->second);
            m_vConnections.erase(item);
        }
    }

    void Timeout(Timer* pTimer)
    {
        cout << "Timer abgelaufen" << endl;

        lock_guard<mutex> lock(m_mtConnections);
        for (unordered_map<TcpSocket*, tuple<Timer*, string>>::iterator it = begin(m_vConnections); it != end(m_vConnections); ++it)
        {
            if (get<0>(it->second) == pTimer)
            {
                it->first->Close();
                break;
            }
        }
    }

    void ClientConnected(TcpSocket* pTcpSocket)
    {
        const uint8_t caRequest[] = "GET / HTTP/1.1\r\n\r\n";
        pTcpSocket->Write(caRequest, sizeof(caRequest));
    }

    void ClientDatenEmpfangen(TcpSocket* pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();
        //cout << nAvalible << " Bytes in DatenEmpfangen" << endl;

        if (nAvalible == 0)
        {
            pTcpSocket->Close();
            return;
        }

        shared_ptr<uint8_t> spBuffer(new uint8_t[nAvalible + 1]);

        uint32_t nRead = pTcpSocket->Read(spBuffer.get(), nAvalible);

        if (nRead > 0)
        {
            cout << string().append(reinterpret_cast<char*>(spBuffer.get()), nRead);
        }
    }

	void ClientSocketError(BaseSocket* pBaseSocket)
	{
		cout << "Error in Verbindung" << endl;
		pBaseSocket->Close();
	}

    void UpnPDatenEmpfangen(UdpSocket* pUdpSocket)
    {
        uint32_t nAvalible = pUdpSocket->GetBytesAvailible();

        shared_ptr<char> spBuffer(new char[nAvalible + 1]);

        string strFrom;
        uint32_t nRead = pUdpSocket->Read(spBuffer.get(), nAvalible, strFrom);

        if (nRead > 0)
        {
            cout << strFrom << endl << string().append(spBuffer.get(), nRead);
        }
    }

    void NeueSslVerbindungen(SslTcpServer* pSslTcpServer, int nCountNewConnections)
    {
        for (int i = 0; i < nCountNewConnections; ++i)
        {
            SslTcpSocket* pSocket = pSslTcpServer->GetNextPendingConnection();
            if (pSocket != nullptr)
            {
                pSocket->BindFuncBytesReceived(bind(&EchoServer::DatenEmpfangen, this, _1));
                pSocket->BindErrorFunction(bind(&EchoServer::SocketError, this, _1));
                pSocket->BindCloseFunction(bind(&EchoServer::SocketCloseing, this, _1));
                lock_guard<mutex> lock(m_mtConnections);
                //m_vSslConnections.emplace(pSocket, make_tuple(new Timer(10000, bind(&EchoServer::Timeout, this, _1)), string()));
                m_vConnections.emplace(pSocket, make_tuple(new Timer(10000, bind(&EchoServer::Timeout, this, _1)), string()));
                //m_vConnections.emplace(pSocket, make_tuple(nullptr, string()));
                pSocket->StartReceiving();
            }
        }
    }

    void SslSocketError(BaseSocket* pBaseSocket)
    {
        cout << "Error in Verbindung" << endl;
        pBaseSocket->Close();
    }

private:
    TcpServer m_Server;
    SslTcpServer m_SslServer;
    //TcpServer m_Server1;
    mutex m_mtConnections;
    unordered_map<TcpSocket*, tuple<Timer*, string>> m_vConnections;
    //unordered_map<SslTcpSocket*, tuple<Timer*, string>> m_vSslConnections;

    TcpSocket m_cClientCon;
    UdpSocket m_cUpnP;
	UdpSocket m_cAdv;
};


int main()
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));

    _setmode(_fileno(stdout), _O_U16TEXT);
#endif

    //locale::global(std::locale(""));

#ifdef _DEBUG
    vector<wstring>&& vSec = ConfFile::GetInstance(L"server.cfg").get();
    for (const auto& item : vSec)
    {
        wcout << L"[" << item << L"]" << endl;
        vector<wstring>&& vKeys = ConfFile::GetInstance(L"server.cfg").get(item);
        for (const auto& key : vKeys)
        {
            vector<wstring>&& vValues = ConfFile::GetInstance(L"server.cfg").get(item, key);
            for (const auto& value : vValues)
                wcout << key << L" = " << value << endl;
        }
    }
#endif

//    EchoServer echoSrv;
//    echoSrv.Start();

    deque<CHttpServ> vServers;

    vector<wstring>&& vDefItem = ConfFile::GetInstance(L"server.cfg").get(L"common", L"DefaultItem");
    vector<wstring>&& vRootDir = ConfFile::GetInstance(L"server.cfg").get(L"common", L"RootDir");
    if (vRootDir.empty() == true)
        vRootDir.push_back(L"./html");
    vector<wstring>&& vLogFile = ConfFile::GetInstance(L"server.cfg").get(L"common", L"LogFile");
    vector<wstring>&& vErrLog = ConfFile::GetInstance(L"server.cfg").get(L"common", L"ErrorLog");
    vector<wstring>&& vReWrite = ConfFile::GetInstance(L"server.cfg").get(L"common", L"RewriteRule");
    vector<wstring>&& vAliasMatch = ConfFile::GetInstance(L"server.cfg").get(L"common", L"AliasMatch");

    vector<wstring>&& vFileTypExt = ConfFile::GetInstance(L"server.cfg").get(L"FileTyps");


    vector<wstring>&& vListen = ConfFile::GetInstance(L"server.cfg").get(L"Listen");
    for (const auto& strListen : vListen)
    {
        vector<wstring>&& vPort = ConfFile::GetInstance(L"server.cfg").get(L"Listen", strListen);
        for (const auto& strPort : vPort)
        {
            // Default Werte setzen
            vServers.emplace_back(*vRootDir.begin(), stoi(strPort), false);

            vServers.back().SetBindAdresse(string(begin(strListen), end(strListen)).c_str());
            if (vDefItem.empty() == false)
                vServers.back().SetDefaultItem(*vDefItem.begin());
            if (vLogFile.empty() == false)
                vServers.back().SetAccessLogFile(*vLogFile.begin());
            if (vErrLog.empty() == false)
                vServers.back().SetErrorLogFile(*vErrLog.begin());
            if (vReWrite.empty() == false)
                vServers.back().AddRewriteRule(vReWrite);
            if (vAliasMatch.empty() == false)
                vServers.back().AddAliasMatch(vAliasMatch);

            for (const auto& strFileExt : vFileTypExt)
            {
                vector<wstring>&& vFileTypAction = ConfFile::GetInstance(L"server.cfg").get(L"FileTyps", strFileExt);
                if (vFileTypAction.empty() == false)
                    vServers.back().AddFileTypAction(strFileExt, *vFileTypAction.begin());
            }

            // Host Parameter holen und setzen
            function<void(wstring, bool)> fuSetOstParam = [&](wstring strListenAddr, bool IsVHost)
            {
                auto tuSSLParam = make_tuple<bool, string, string, string>(false, "", "", "");
                vector<wstring> vHostList;
                vector<wstring>&& vHostPara = ConfFile::GetInstance(L"server.cfg").get(strListenAddr + L":" + strPort);
                for (const auto& strParamKey : vHostPara)
                {
                    vector<wstring>&& vParaValue = ConfFile::GetInstance(L"server.cfg").get(strListenAddr + L":" + strPort, strParamKey);
                    if (vParaValue.empty() == false)
                    {
                        if (strParamKey == L"DefaultItem")
                            vServers.back().SetDefaultItem(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);
                        if (strParamKey == L"RootDir")
                            vServers.back().SetRootDirectory(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);
                        if (strParamKey == L"LogFile")
                            vServers.back().SetAccessLogFile(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);
                        if (strParamKey == L"ErrorLog")
                            vServers.back().SetErrorLogFile(*vParaValue.begin(), IsVHost == true ? strListenAddr.c_str() : nullptr);

                        if (strParamKey == L"SSL" && vParaValue.begin() == L"true")
                            get<0>(tuSSLParam) = true;
                        if (strParamKey == L"KeyFile")
                            get<1>(tuSSLParam) = Utf8Converter.to_bytes(*vParaValue.begin());
                        if (strParamKey == L"CertFile")
                            get<2>(tuSSLParam) = Utf8Converter.to_bytes(*vParaValue.begin());
                        if (strParamKey == L"CaBundle")
                            get<3>(tuSSLParam) = Utf8Converter.to_bytes(*vParaValue.begin());

                        if (strParamKey == L"VirtualHost" && IsVHost == false)
                        {
                            size_t nPos = vParaValue.begin()->find_first_of(L','), nStart = 0;
                            while (nPos != string::npos)
                            {
                                vHostList.push_back(vParaValue.begin()->substr(nStart, nPos++));
                                nStart += nPos;
                                nPos = vParaValue.begin()->find_first_of(L',', nStart);
                            }
                            vHostList.push_back(vParaValue.begin()->substr(nStart));
                        }
                    }
                }

                if (get<0>(tuSSLParam) == true && get<1>(tuSSLParam).empty() == false && get<2>(tuSSLParam).empty() == false && get<3>(tuSSLParam).empty() == false)
                    vServers.back().SetUseSSL(get<0>(tuSSLParam), get<3>(tuSSLParam), get<2>(tuSSLParam), get<1>(tuSSLParam), IsVHost == true ? strListenAddr.c_str() : nullptr);

                for (size_t i = 0; i < vHostList.size(); ++i)
                    fuSetOstParam(vHostList[i], true);
            };

            fuSetOstParam(strListen, false);
        }
    }

    // Server starten
    for (auto& HttpServer : vServers)
        HttpServer.Start();
/*
    CHttpServ httpServer;
    httpServer.SetBindAdresse("0.0.0.0");
    httpServer.SetPort(80);
    httpServer.SetDefaultItem(L"index.html index.php default.html dehault.php index.htm default.htm");
    httpServer.SetRootDirectory(L"D:/Users/Thomas/webgl");
    httpServer.AddFileTypAction(L"php", L"C:/Users/Thomas/Programme/php/php-cgi.exe");

    CHttpServ SslHttpServer;
    SslHttpServer.SetBindAdresse("0.0.0.0");
    SslHttpServer.SetPort(8080);
    SslHttpServer.SetUseSSL(true, "./certs/ca-root.pem", "./certs/192-168-161-1.pem", "./certs/192-168-161-1-key.pem");
    SslHttpServer.SetDefaultItem(L"index.html index.php default.html dehault.php index.htm default.htm");
    SslHttpServer.SetRootDirectory(L"D:/Users/Thomas/webgl");
    SslHttpServer.AddFileTypAction(L"php", L"C:/Users/Thomas/Programme/php/php-cgi.exe");

    httpServer.Start();
    SslHttpServer.Start();
*/
#if defined(_WIN32) || defined(_WIN64)
    //while (::_kbhit() == 0)
    //    this_thread::sleep_for(chrono::milliseconds(1));
    _getch();
#else
    getchar();
#endif

/*    SslHttpServer.Stop();
    httpServer.Stop();*/
    for (auto& HttpServer : vServers)
        HttpServer.Stop();

//    echoSrv.Stop();

    return 0;
}

