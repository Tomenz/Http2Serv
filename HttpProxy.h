
#include <memory>
#include <thread>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <regex>
#include <fstream>

#include "socketlib/SocketLib.h"
#include "Timer.h"

using namespace std;
using namespace std::placeholders;

#include <Windows.h>
#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "x64/Debug/socketlib64d")
#else
#pragma comment(lib, "Debug/socketlib32d")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "x64/Release/socketlib64")
#else
#pragma comment(lib, "Release/socketlib32")
#endif
#endif

#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")

class CHttpProxy
{
    typedef struct
    {
        shared_ptr<Timer> pTimer;
        string strBuffer;
        TcpSocket* pClientSocket;
        string strMethode;
        string strDestination;
        bool bConncted;
        bool bAnswerd;
        ofstream* pDebOut;
    } CONNECTIONDETAILS;

    typedef unordered_map<TcpSocket*, CONNECTIONDETAILS> CONNECTIONLIST;
    typedef unordered_map<TcpSocket*, TcpSocket*> REFERENCLIST;

public:
    CHttpProxy(const string& strBindIp = string("127.0.0.1"), short sPort = 8080) : m_pSocket(nullptr), m_strBindIp(strBindIp), m_sPort(sPort)
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
        m_pSocket->BindNewConnection(function<void(const vector<TcpSocket*>&)>(bind(&CHttpProxy::OnNewConnection, this, _1)));
        m_pSocket->BindErrorFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::OnSocketError, this, _1)));
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
        return m_vConnections.size() == 0 && m_vReferencList.size() == 0 ? true : false;
    }

    const string& GetBindAdresse() noexcept
    {
        return m_strBindIp;
    }

    short GetPort() noexcept
    {
        return m_sPort;
    }

private:
    void OnNewConnection(const vector<TcpSocket*>& vNewConnections)
    {
        vector<TcpSocket*> vCache;
        for (auto& pSocket : vNewConnections)
        {
            if (pSocket != nullptr)
            {
                pSocket->BindFuncBytesReceived(static_cast<function<void(TcpSocket*)>>(bind(&CHttpProxy::OnDataRecieved, this, _1)));
                pSocket->BindErrorFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::OnSocketError, this, _1)));
                pSocket->BindCloseFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::OnSocketCloseing, this, _1)));
                vCache.push_back(pSocket);
            }
        }
        if (vCache.size())
        {
            m_mtxConnections.lock();
            for (auto& pSocket : vCache)
            {
                m_vConnections.emplace(pSocket, CONNECTIONDETAILS({ make_shared<Timer>(600000, bind(&CHttpProxy::OnTimeout, this, _1, _2), nullptr), string(), nullptr, string(), string(), false, false, nullptr }));
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

        shared_ptr<char[]> spBuffer(new char[nAvalible]);

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
                            string strVersion;

                            const string& strLine = line->str();
                            const static regex SpaceSeperator(" ");
                            sregex_token_iterator token(begin(strLine), end(strLine), SpaceSeperator, -1);
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                pConDetails->strMethode = token++->str();
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                pConDetails->strDestination = token++->str();
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                strVersion = token++->str();

                            short sPort = 80;

                            if (pConDetails->strMethode == "CONNECT" && pConDetails->strDestination.empty() == false)
                            {
                                sPort = 443;
                                size_t nPos = pConDetails->strDestination.find(':');
                                if (nPos != string::npos)
                                {
                                    sPort = stoi(pConDetails->strDestination.substr(nPos + 1));
                                    pConDetails->strDestination.erase(nPos);
                                }
                            }
                            else
                            {
                                size_t nPos = pConDetails->strDestination.find("//");
                                if (nPos != string::npos)
                                    pConDetails->strDestination.erase(0, nPos + 2);

                                nPos = pConDetails->strDestination.find("/");
                                if (nPos != string::npos)
                                {
                                    string strNewFirstLine = pConDetails->strMethode + " " + pConDetails->strDestination.substr(nPos) + " " + strVersion;
                                    pConDetails->strBuffer.replace(0, strLine.size(), strNewFirstLine);
                                    pConDetails->strDestination.erase(nPos);
                                }

                                nPos = pConDetails->strDestination.find(':');
                                if (nPos != string::npos)
                                {
                                    sPort = stoi(pConDetails->strDestination.substr(nPos + 1));
                                    pConDetails->strDestination.erase(nPos);
                                }
                            }

                            pConDetails->pClientSocket = new TcpSocket();

                            m_vReferencList.emplace(pConDetails->pClientSocket, pTcpSocket);

                            pConDetails->pClientSocket->BindFuncConEstablished(static_cast<function<void(TcpSocket*)>>(bind(&CHttpProxy::Connected, this, _1)));
                            pConDetails->pClientSocket->BindFuncBytesReceived(static_cast<function<void(TcpSocket*)>>(bind(&CHttpProxy::OnDataRecievedDest, this, _1)));
                            pConDetails->pClientSocket->BindErrorFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::SocketErrorDest, this, _1)));
                            pConDetails->pClientSocket->BindCloseFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::SocketCloseingDest, this, _1)));

                            if (pConDetails->pClientSocket->Connect(pConDetails->strDestination.c_str(), sPort) == false)
                            {
                                m_vReferencList.erase(pConDetails->pClientSocket);
                                pConDetails->pClientSocket->Delete();
                                pConDetails->pClientSocket = nullptr;
                                OutputDebugString(L"Connect schlug fehl (1)\r\n");

                                const string strRespons = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
                                pTcpSocket->Write(strRespons.c_str(), strRespons.size());
                            }
                        }
                    }
                    else
                        OutputDebugString(L"Destination Socket besteht bereits\r\n");
                }
                else
                    OutputDebugString(L"Zeitüberschneidung\r\n");
            }
            else
                OutputDebugString(L"Socket nicht in ConectionList (1)\r\n");

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
        else
            OutputDebugString(L"Socket nicht in ConectionList (2)\r\n");
        m_mtxConnections.unlock();

        pBaseSocket->Close();
    }

    void OnSocketCloseing(BaseSocket* const pBaseSocket)
    {
        //OutputDebugString(wstring(L"CHttpProxy::OnSocketCloseing - " + to_wstring(reinterpret_cast<size_t>(pBaseSocket)) +  L"\r\n").c_str());
        m_mtxConnections.lock();
        auto item = m_vConnections.find(reinterpret_cast<TcpSocket* const>(pBaseSocket));
        if (item != end(m_vConnections))
        {
            item->second.pTimer->Stop();
            Timer* pTimer = item->second.pTimer.get();
            m_mtxConnections.unlock();
            while (pTimer->IsStopped() == false)
                this_thread::sleep_for(chrono::microseconds(1));

            m_mtxConnections.lock();
            item = m_vConnections.find(reinterpret_cast<TcpSocket* const>(pBaseSocket));
            if (item != end(m_vConnections))
            {
                if (item->second.pDebOut != nullptr)
                {
                    item->second.pDebOut->flush();
                    item->second.pDebOut->close();
                    delete item->second.pDebOut;
                }

                TcpSocket* pSock = item->second.pClientSocket;
                m_vConnections.erase(item->first);
                if (pSock != nullptr)
                {
//                    m_vReferencList.erase(pSock);
                    pSock->BindCloseFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::SocketCloseingDelete, this, _1)));
                    pSock->Close();// pSock->SelfDestroy();
                }
            }
            else
                OutputDebugString(L"Socket nicht in ConectionList (3)\r\n");
        }
        else
            OutputDebugString(L"Socket nicht in ConectionList (4)\r\n");
        m_mtxConnections.unlock();
    }

    void OnTimeout(const Timer* const pTimer, void*)
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
        lock_guard<mutex> lock(m_mtxConnections);
        REFERENCLIST::iterator item = m_vReferencList.find(pTcpSocket);
        if (item != end(m_vReferencList))
        {
            auto& conn = m_vConnections.find(item->second);
            if (conn != end(m_vConnections))
            {
                conn->second.pTimer->Reset();
                conn->second.bConncted = true;
                conn->first->BindFuncBytesReceived(static_cast<function<void(TcpSocket*)>>(bind(&CHttpProxy::OnDataRecievedClient, this, _1)));

                if (conn->second.strMethode == "CONNECT")
                {
                    const string strConnected = "HTTP/1.1 200 Connection established\r\n\r\n";
                    conn->first->Write(strConnected.c_str(), strConnected.size());
                }
                else
                {
                    pTcpSocket->Write(conn->second.strBuffer.c_str(), conn->second.strBuffer.size());
/*
                    if (conn->second.pDebOut == nullptr)
                    {
                        //                        item.second.pDebOut = new ofstream;
                        if (conn->second.pDebOut != nullptr)
                        {
                            static atomic<uint32_t> uiCounter = 1;
                            conn->second.pDebOut->open(string("Debug_" + to_string(uiCounter++) + ".txt").c_str(), ios::binary);
                            if (conn->second.pDebOut->is_open() == true)
                                conn->second.pDebOut->write(conn->second.strBuffer.c_str(), conn->second.strBuffer.size());
                        }
                    }
*/
                    conn->second.strBuffer.clear();
                }
            }
            else
                OutputDebugString(L"Socket nicht in ConectionList (5)\r\n");
        }
        else
            OutputDebugString(L"Socket nicht in ReferenceList (1)\r\n");
    }

    void OnDataRecievedClient(TcpSocket* const pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();

        if (nAvalible == 0)
        {
            pTcpSocket->Close();
            return;
        }

        lock_guard<mutex> lock(m_mtxConnections);
        CONNECTIONLIST::iterator item = m_vConnections.find(pTcpSocket);
        if (item != end(m_vConnections))
        {
            if (item->second.pClientSocket->GetOutBytesInQue() > 0x100000)  // 1 MByt
            {
                item->second.pTimer->Reset();
                return;
            }

            shared_ptr<char[]> spBuffer(new char[nAvalible]);
            uint32_t nRead = pTcpSocket->Read(spBuffer.get(), nAvalible);

            if (nRead > 0)
            {
                item->second.pTimer->Reset();
                if (item->second.bAnswerd == true && item->second.strMethode != "CONNECT")
                {
                    item->second.strBuffer.append(spBuffer.get(), nRead);
                    size_t nPosEndOfHeader = item->second.strBuffer.find("\r\n\r\n");
                    while (nPosEndOfHeader != string::npos)
                    {
                        const static regex crlfSeperator("\r\n");
                        sregex_token_iterator line(begin(item->second.strBuffer), begin(item->second.strBuffer) + nPosEndOfHeader, crlfSeperator, -1);
                        if (line != sregex_token_iterator())
                        {
                            string strDestination, strVersion;

                            const string& strLine = line->str();
                            const static regex SpaceSeperator(" ");
                            sregex_token_iterator token(begin(strLine), end(strLine), SpaceSeperator, -1);
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                item->second.strMethode = token++->str();
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                strDestination = token++->str();
                            if (token != sregex_token_iterator() && token->str().empty() == false)
                                strVersion = token++->str();

                            short sPort = 80;

                            size_t nPos = strDestination.find("//");
                            if (nPos != string::npos)
                                strDestination.erase(0, nPos + 2);

                            nPos = strDestination.find("/");
                            if (nPos != string::npos)
                            {
                                string strNewFirstLine = item->second.strMethode + " " + strDestination.substr(nPos) + " " + strVersion;
                                item->second.strBuffer.replace(0, strLine.size(), strNewFirstLine);
                                strDestination.erase(nPos);
                                nPosEndOfHeader = item->second.strBuffer.find("\r\n\r\n");
                            }
                            nPos = strDestination.find(':');
                            if (nPos != string::npos)
                            {
                                sPort = stoi(strDestination.substr(nPos + 1));
                                strDestination.erase(nPos);
                            }

                            if (item->second.strDestination != strDestination)
                            {
                                item->second.strDestination = strDestination;
                                item->second.bConncted = false;

//                                m_vReferencList.erase(item->second.pClientSocket);
                                item->second.pClientSocket->BindCloseFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::SocketCloseingDelete, this, _1)));
                                item->second.pClientSocket->Close();// pSock->SelfDestroy();

                                item->second.pClientSocket = new TcpSocket();

                                m_vReferencList.emplace(item->second.pClientSocket, pTcpSocket);

                                item->second.pClientSocket->BindFuncConEstablished(static_cast<function<void(TcpSocket*)>>(bind(&CHttpProxy::Connected, this, _1)));
                                item->second.pClientSocket->BindFuncBytesReceived(static_cast<function<void(TcpSocket*)>>(bind(&CHttpProxy::OnDataRecievedDest, this, _1)));
                                item->second.pClientSocket->BindErrorFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::SocketErrorDest, this, _1)));
                                item->second.pClientSocket->BindCloseFunction(static_cast<function<void(BaseSocket*)>>(bind(&CHttpProxy::SocketCloseingDest, this, _1)));

                                if (item->second.pClientSocket->Connect(item->second.strDestination.c_str(), sPort) == false)
                                {
                                    m_vReferencList.erase(item->second.pClientSocket);
                                    item->second.pClientSocket->Delete();
                                    item->second.pClientSocket = nullptr;
                                    OutputDebugString(L"Connect schlug fehl (2)\r\n");

                                    const string strRespons = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
                                    if (pTcpSocket->Write(strRespons.c_str(), strRespons.size()) == 0)
                                        OutputDebugString(L"Socket bereits closed (0)\r\n");
                                }
                                return;
                            }

                            if (item->second.pClientSocket->Write(item->second.strBuffer.c_str(), nPosEndOfHeader + 4) == 0)
                                OutputDebugString(L"Socket bereits closed (1)\r\n");
                            //item->second.bAnswerd = false;

                            if (item->second.pDebOut != nullptr)
                            {
                                item->second.pDebOut->write("\r\n---***---\r\n\r\n", 15);
                                item->second.pDebOut->write(item->second.strBuffer.c_str(), nPosEndOfHeader + 4);
                            }
                            item->second.strBuffer.erase(0, nPosEndOfHeader + 4);
                            nPosEndOfHeader = item->second.strBuffer.find("\r\n\r\n");
                        }
                        else
                            return;

                    }
                    //else
                        return;

                }
                if (item->second.pClientSocket->Write(spBuffer.get(), nRead) == 0)
                    OutputDebugString(L"Socket bereits closed (2)\r\n");
/*
                if (item->second.pDebOut != nullptr)
                    item->second.pDebOut->write(spBuffer.get(), nRead);
*/
            }
            else
                OutputDebugString(L"Master Socket nicht in Connection List\r\n");
        }
    }

    void OnDataRecievedDest(TcpSocket* const pTcpSocket)
    {
        uint32_t nAvalible = pTcpSocket->GetBytesAvailible();

        if (nAvalible == 0)
        {
            // we always close the socket from the client, never the socket to the destination
            lock_guard<mutex> lock(m_mtxConnections);
            REFERENCLIST::iterator item = m_vReferencList.find(pTcpSocket);
            if (item != end(m_vReferencList))
            {
                auto& conn = m_vConnections.find(item->second);
                if (conn != end(m_vConnections))
                {
                    conn->second.pTimer->Stop();
                    conn->first->Close();
                }
                //else
                //    OutputDebugString(L"Socket nicht in ConectionList (6)\r\n");
            }
            else
                OutputDebugString(L"Socket nicht in ReferenceList (2)\r\n");
            return;
        }

        shared_ptr<char[]> spBuffer(new char[nAvalible]);

        uint32_t nRead = pTcpSocket->Read(spBuffer.get(), nAvalible);

        if (nRead > 0)
        {
            lock_guard<mutex> lock(m_mtxConnections);
            REFERENCLIST::iterator item = m_vReferencList.find(pTcpSocket);
            if (item != end(m_vReferencList))
            {
                auto& conn = m_vConnections.find(item->second);
                if (conn != end(m_vConnections))
                {
                    conn->second.pTimer->Reset();
                    conn->second.bAnswerd = true;
                    if (conn->first->Write(spBuffer.get(), nRead) == 0)
                        OutputDebugString(L"Socket bereits closed (3)\r\n");
/*
                    if (conn->second.pDebOut != nullptr)
                        conn->second.pDebOut->write(spBuffer.get(), nRead);
*/
                }
                else
                    OutputDebugString(L"Socket nicht in ConectionList (7)\r\n");
            }
            else
                OutputDebugString(L"Socket nicht in ReferenceList (3)\r\n");
        }
    }

    void SocketErrorDest(BaseSocket* const pBaseSocket)
    {
        lock_guard<mutex> lock(m_mtxConnections);
        REFERENCLIST::iterator item = m_vReferencList.find(reinterpret_cast<TcpSocket* const>(pBaseSocket));
        if (item != end(m_vReferencList))
        {
            auto& conn = m_vConnections.find(item->second);
            if (conn != end(m_vConnections))
            {
                conn->second.pTimer->Stop();
                if (conn->second.bConncted == false)
                {
                    static const string strRespons = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
                    if (conn->first->Write(strRespons.c_str(), strRespons.size()) == 0)
                        OutputDebugString(L"Socket bereits closed (4)\r\n");
                    conn->second.bConncted = true;
                }
                conn->first->Close();
            }
            else
                OutputDebugString(L"Socket nicht in ConectionList (8)\r\n");
        }
        else
            OutputDebugString(L"Socket nicht in ReferenceList (4)\r\n");
    }

    void SocketCloseingDest(BaseSocket* const pBaseSocket)
    {
        lock_guard<mutex> lock(m_mtxConnections);
        REFERENCLIST::iterator item = m_vReferencList.find(reinterpret_cast<TcpSocket* const>(pBaseSocket));
        if (item != end(m_vReferencList))
        {
            auto& conn = m_vConnections.find(item->second);
            if (conn != end(m_vConnections))
            {
                conn->second.pTimer->Stop();
                if (conn->second.bConncted == false)
                {
                    static const string strRespons = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
                    if (conn->first->Write(strRespons.c_str(), strRespons.size()) == 0)
                        OutputDebugString(L"Socket bereits closed (5)\r\n");
                    conn->second.bConncted = true;
                }
                conn->first->Close();
            }
            else
                OutputDebugString(L"Socket nicht in ConectionList (9)\r\n");
        }
        else
            OutputDebugString(L"Socket nicht in ReferenceList (5)\r\n");
    }

    void SocketCloseingDelete(BaseSocket* const pBaseSocket)
    {
        m_mtxConnections.lock();
        m_vReferencList.erase(reinterpret_cast<TcpSocket* const>(pBaseSocket));
        m_mtxConnections.unlock();
        reinterpret_cast<TcpSocket*>(pBaseSocket)->Delete();
    }

private:
    TcpServer*             m_pSocket;
    CONNECTIONLIST         m_vConnections;
    REFERENCLIST           m_vReferencList;
    mutex                  m_mtxConnections;

    string                 m_strBindIp;
    short                  m_sPort;
};
