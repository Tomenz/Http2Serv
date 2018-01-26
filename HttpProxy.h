
#include <memory>
#include <thread>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <regex>

#include "socketlib/StdSocket.h"
#include "Timer.h"

using namespace std;
using namespace std::placeholders;

#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "x64/Debug/socketlib")
#else
#pragma comment(lib, "Debug/socketlib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "x64/Release/socketlib")
#else
#pragma comment(lib, "Release/socketlib")
#endif
#endif

class CHttpProxy
{
    typedef struct
    {
        shared_ptr<Timer> pTimer;
        string strBuffer;
        TcpSocket* pClientSocket;
        string strMethode;
//        bool bIsH2Con;
//        uint64_t nContentsSoll;
//        uint64_t nContentRecv;
//        shared_ptr<TempFile> TmpFile;
//        HeadList HeaderList;
//        deque<HEADERENTRY> lstDynTable;
//        shared_ptr<mutex> mutStreams;
//        STREAMLIST H2Streams;
//        STREAMSETTINGS StreamParam;
//        shared_ptr<atomic_bool> atStop;
    } CONNECTIONDETAILS;

    typedef unordered_map<TcpSocket*, CONNECTIONDETAILS> CONNECTIONLIST;

public:
    CHttpProxy(string strBindIp = "127.0.0.1", short sPort = 8080) : m_pSocket(nullptr), m_strBindIp(strBindIp), m_sPort(sPort)
    {
    }
    CHttpProxy(const CHttpProxy&) = delete;
    CHttpProxy(CHttpProxy&& other) { *this = move(other); }
    CHttpProxy& operator=(const CHttpProxy&) = delete;
    CHttpProxy& operator=(CHttpProxy&& other)
    {
        swap(m_pSocket, other.m_pSocket);
        other.m_pSocket = nullptr;
        swap(m_vConnections, other.m_vConnections);
        swap(m_strBindIp, other.m_strBindIp);
        swap(m_sPort, other.m_sPort);

        return *this;
    }

    virtual ~CHttpProxy()
    {
        Stop();

        while (IsStopped() == false)
            this_thread::sleep_for(chrono::milliseconds(10));
    }

    bool Start()
    {
        m_pSocket = new TcpServer();
        m_pSocket->BindNewConnection(bind(&CHttpProxy::OnNewConnection, this, _1));
        m_pSocket->BindErrorFunction(bind(&CHttpProxy::OnSocketError, this, _1));
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

private:
    void OnNewConnection(const vector<TcpSocket*>& vNewConnections)
    {
        vector<TcpSocket*> vCache;
        for (auto& pSocket : vNewConnections)
        {
            if (pSocket != nullptr)
            {
                pSocket->BindFuncBytesRecived(bind(&CHttpProxy::OnDataRecieved, this, _1));
                pSocket->BindErrorFunction(bind(&CHttpProxy::OnSocketError, this, _1));
                pSocket->BindCloseFunction(bind(&CHttpProxy::OnSocketCloseing, this, _1));
                vCache.push_back(pSocket);
            }
        }
        if (vCache.size())
        {
            m_mtxConnections.lock();
            for (auto& pSocket : vCache)
            {
                m_vConnections.emplace(pSocket, CONNECTIONDETAILS({ make_shared<Timer>(30000, bind(&CHttpProxy::OnTimeout, this, _1)), string(), nullptr }));
                pSocket->StartReceiving();
            }
            m_mtxConnections.unlock();
        }
    }

    void OnDataRecieved(TcpSocket* const pTcpSocket)
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

                if (pConDetails->pClientSocket == nullptr)  // Noch nicht schon ausgewertet
                {
                    size_t nPosEndOfHeader = pConDetails->strBuffer.find("\r\n\r\n");
                    if (nPosEndOfHeader != string::npos)
                    {
                        const static regex crlfSeperator("\r\n");
                        sregex_token_iterator line(begin(pConDetails->strBuffer), begin(pConDetails->strBuffer) + nPosEndOfHeader, crlfSeperator, -1);
                        if (line != sregex_token_iterator())
                        {
                            string strDestination, strVersion;

                            const string& strLine = line->str();
                            const static regex SpaceSeperator(" ");
                            sregex_token_iterator token(begin(strLine), end(strLine), SpaceSeperator, -1);
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                pConDetails->strMethode = token++->str();
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                strDestination = token++->str();
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                strVersion = token++->str();

                            short sPort = 80;

                            if (pConDetails->strMethode == "CONNECT" && strDestination.empty() == false)
                            {
                                sPort = 443;
                                size_t nPos = strDestination.find(':');
                                if (nPos != string::npos)
                                {
                                    sPort = stoi(strDestination.substr(nPos + 1));
                                    strDestination.erase(nPos);
                                }
                            }
                            else
                            {
                                size_t nPos = strDestination.find("//");
                                if (nPos != string::npos)
                                    strDestination.erase(0, nPos + 2);

                                nPos = strDestination.find("/");
                                if (nPos != string::npos)
                                {
                                    string strNewFirstLine = pConDetails->strMethode + " " + strDestination.substr(nPos) + " " + strVersion;
                                    pConDetails->strBuffer.replace(0, strLine.size(), strNewFirstLine);
                                    strDestination.erase(nPos);
                                }

                                nPos = strDestination.find(':');
                                if (nPos != string::npos)
                                {
                                    sPort = stoi(strDestination.substr(nPos + 1));
                                    strDestination.erase(nPos);
                                }
                            }

                            pConDetails->pClientSocket = new TcpSocket();

                            pConDetails->pClientSocket->BindFuncConEstablished(bind(&CHttpProxy::Connected, this, _1));
                            pConDetails->pClientSocket->BindFuncBytesRecived(bind(&CHttpProxy::OnDataRecievedDest, this, _1));
                            pConDetails->pClientSocket->BindErrorFunction(bind(&CHttpProxy::SocketErrorDest, this, _1));
                            pConDetails->pClientSocket->BindCloseFunction(bind(&CHttpProxy::SocketCloseingDest, this, _1));

                            pConDetails->pClientSocket->Connect(strDestination.c_str(), sPort);
                        }
                    }
                }
            }

            m_mtxConnections.unlock();
        }
    }

    void OnSocketError(BaseSocket* const pBaseSocket)
    {
//        MyTrace("Error: Network error ", pBaseSocket->GetErrorNo());

        m_mtxConnections.lock();
        auto item = m_vConnections.find(reinterpret_cast<TcpSocket*>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            item->second.pTimer->Stop();
        }
        m_mtxConnections.unlock();

        pBaseSocket->Close();
    }

    void OnSocketCloseing(BaseSocket* const pBaseSocket)
    {
        //OutputDebugString(L"CHttpServ::OnSocketCloseing\r\n");
        m_mtxConnections.lock();
        auto item = m_vConnections.find(reinterpret_cast<TcpSocket* const>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            item->second.pTimer->Stop();
            Timer* pTimer = item->second.pTimer.get();
            m_mtxConnections.unlock();
            while (pTimer->IsStopped() == false)
                this_thread::sleep_for(chrono::nanoseconds(1));

            m_mtxConnections.lock();
            item = m_vConnections.find(reinterpret_cast<TcpSocket* const>(pBaseSocket));
            if (item != end(m_vConnections))
            {
                TcpSocket* pSock = item->second.pClientSocket;
                m_vConnections.erase(item->first);
                if (pSock != nullptr)
                    pSock->SelfDestroy();
            }
        }
        m_mtxConnections.unlock();
    }

    void OnTimeout(Timer* const pTimer)
    {
        lock_guard<mutex> lock(m_mtxConnections);
        for (auto it = begin(m_vConnections); it != end(m_vConnections); ++it)
        {
            if (it->second.pTimer.get() == pTimer)
            {
                it->first->Close();
                break;
            }
        }
    }

    ///////////////////////////////////////////////////////

    void Connected(TcpSocket* const pTcpSocket)
    {
        m_mtxConnections.lock();
        for (const auto& item : m_vConnections)
        {
            if (item.second.pClientSocket == pTcpSocket)
            {
                item.second.pTimer->Reset();
                item.first->BindFuncBytesRecived(bind(&CHttpProxy::OnDataRecievedClient, this, _1));

                if (item.second.strMethode == "CONNECT")
                {
                    const string strConnected = "HTTP/1.1 200 Connection established\r\n\r\n";
                    item.first->Write(strConnected.c_str(), strConnected.size());
                }
                else
                {
                    pTcpSocket->Write(item.second.strBuffer.c_str(), item.second.strBuffer.size());
                }
                break;
            }
        }
        m_mtxConnections.unlock();
    }

    void OnDataRecievedClient(TcpSocket* const pTcpSocket)
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
                item->second.pTimer->Reset();
                item->second.pClientSocket->Write(spBuffer.get(), nRead);
            }
            m_mtxConnections.unlock();
        }
    }

    void OnDataRecievedDest(TcpSocket* const pTcpSocket)
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
            for (const auto& item : m_vConnections)
            {
                if (item.second.pClientSocket == pTcpSocket)
                {
                    item.second.pTimer->Reset();
                    item.first->Write(spBuffer.get(), nRead);
                    break;
                }
            }
            m_mtxConnections.unlock();
        }
    }

    void SocketErrorDest(BaseSocket* const pBaseSocket)
    {
        //pBaseSocket->Close();
        m_mtxConnections.lock();
        for (const auto& item : m_vConnections)
        {
            if (item.second.pClientSocket == pBaseSocket)
            {
                item.second.pTimer->Stop();
                item.first->Close();
                m_mtxConnections.unlock();
               return;
            }
        }
        pBaseSocket->Close();
        m_mtxConnections.unlock();
    }

    void SocketCloseingDest(BaseSocket* const pBaseSocket)
    {
        m_mtxConnections.lock();
        for (const auto& item : m_vConnections)
        {
            if (item.second.pClientSocket == pBaseSocket)
            {
                item.second.pTimer->Stop();
                item.first->Close();
                m_mtxConnections.unlock();
                return;
            }
        }
        pBaseSocket->Close();
        m_mtxConnections.unlock();
    }

private:
    TcpServer*             m_pSocket;
    CONNECTIONLIST         m_vConnections;
    mutex                  m_mtxConnections;

    string                 m_strBindIp;
    short                  m_sPort;
};
