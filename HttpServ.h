/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#ifndef HTTPSERV_H
#define HTTPSERV_H

#include <unordered_map>

#include "SocketLib/SocketLib.h"
#include "Timer.h"
#include "H2Proto.h"

using namespace std;

class CHttpServ : public Http2Protocol
{
    typedef struct
    {
        shared_ptr<Timer<TcpSocket>> pTimer;
        string strBuffer;
        bool bIsH2Con;
        uint64_t nContentsSoll;
        uint64_t nContentRecv;
        HeadList HeaderList;
        deque<HEADERENTRY> lstDynTable;
        shared_ptr<mutex> mutStreams;
        STREAMLIST H2Streams;
        STREAMSETTINGS StreamParam;
        RESERVEDWINDOWSIZE StreamResWndSizes;
        shared_ptr<atomic_bool> atStop;
        shared_ptr<mutex> mutReqData;
        deque<unique_ptr<char[]>> vecReqData;
        deque<AUTHITEM> lstAuthInfo;
    } CONNECTIONDETAILS;

    typedef unordered_map<TcpSocket*, CONNECTIONDETAILS> CONNECTIONLIST;

    typedef tuple<const wchar_t*, const char*> MIMEENTRY;
    #define MIMEEXTENSION(x) get<0>(x)
    #define MIMESTRING(x) get<1>(x)

    enum HEADERFLAGS : uint32_t
    {
        TERMINATEHEADER = 1,
        ADDCONNECTIONCLOSE = 2,
        ADDNOCACHE = 4,
        HTTPVERSION11 = 8,
        ADDCONENTLENGTH = 16,
        GZIPENCODING = 32,
        DEFLATEENCODING = 64,
        BROTLICODING = 128,
    };

    static const char* SERVERSIGNATUR;

public:
    typedef struct
    {
        vector<wstring> m_vstrDefaultItem;
        wstring m_strRootPath;
        wstring m_strLogFile;
        wstring m_strErrLog;
        wstring m_strMsgDir;
        bool    m_bSSL;
        string  m_strCAcertificate;
        string  m_strHostCertificate;
        string  m_strHostKey;
        string  m_strSslCipher;
        unordered_map<wstring, wstring> m_mstrRewriteRule;
        unordered_map<wstring, tuple<vector<wstring>, bool>> m_mstrAliasMatch;
        unordered_map<wstring, wstring> m_mstrForceTyp;
        unordered_map<wstring, vector<wstring>> m_mFileTypeAction;
        vector<tuple<wstring, wstring, wstring>> m_vRedirMatch;
        vector<tuple<wstring, wstring, wstring>> m_vEnvIf;  //  Request_URI "^/.*" "ENV-Name=ENV-Value"
        vector<string> m_vDeflateTyps;
        vector<wstring> m_vOptionsHandler;
        unordered_map<wstring, tuple<wstring, wstring, vector<wstring>>> m_mAuthenticate;   // Directory, (Realm, Methode, Liste mit Usern)
        vector<pair<string, string>> m_vHeader; // Header-Name, Header-Value
        unordered_map<wstring, wstring> m_mstrReverseProxy;
        vector<wstring> m_vAuthHandler;
        int32_t m_nMaxConnPerIp;
    } HOSTPARAM;

public:

    CHttpServ(const wstring& strRootPath = wstring(L"."), const string& strBindIp = string("127.0.0.1"), uint16_t sPort = 80, bool bSSL = false);
    CHttpServ(const CHttpServ&) = delete;
    CHttpServ(CHttpServ&& other) noexcept { *this = move(other); }
    CHttpServ& operator=(const CHttpServ&) = delete;
    CHttpServ& operator=(CHttpServ&& other) noexcept;
    ~CHttpServ();

    bool Start();
    bool Stop();
    bool IsStopped() noexcept;

    HOSTPARAM& GetParameterBlockRef(const string& szHostName);
    void ClearAllParameterBlocks();
    const string& GetBindAdresse() noexcept;
    uint16_t GetPort() noexcept;

private:
    void OnNewConnection(const vector<TcpSocket*>& vNewConnections);
    void OnDataReceived(TcpSocket* const pTcpSocket);
    void OnSocketError(BaseSocket* const pBaseSocket);
    void OnSocketClosing(BaseSocket* const pBaseSocket);
    void OnTimeout(const Timer<TcpSocket>* const pTimer, TcpSocket*);

    size_t BuildH2ResponsHeader(uint8_t* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0);
    size_t BuildResponsHeader(uint8_t* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0);
    string LoadErrorHtmlMessage(HeadList& HeaderList, int iRespCode, const wstring& strMsgDir);
    void SendErrorRespons(TcpSocket* const pTcpSocket, const shared_ptr<Timer<TcpSocket>> pTimer, int iRespCode, int iFlag, HeadList& HeaderList, HeadList umHeaderList = HeadList());
    void SendErrorRespons(const MetaSocketData& soMetaDa, const uint8_t httpVers, const uint32_t nStreamId, function<size_t(uint8_t*, size_t, int, int, HeadList, uint64_t)> BuildRespHeader, int iRespCode, int iFlag, const string& strHttpVersion, HeadList& HeaderList, HeadList umHeaderList = HeadList());

    void DoAction(const MetaSocketData soMetaDa, const uint8_t httpVers, const uint32_t nStreamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex& pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, function<size_t(uint8_t*, size_t, int, int, const HeadList&, uint64_t)> BuildRespHeader, atomic<bool>& patStop, mutex& pmtxReqdata, deque<unique_ptr<char[]>>& vecData, deque<AUTHITEM>& lstAuthInfo);
    void EndOfStreamAction(const MetaSocketData soMetaDa, const uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex& pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, atomic<bool>& patStop, mutex& pmtxReqdata, deque<unique_ptr<char[]>>& vecData, deque<AUTHITEM>& lstAuthInfo) override;

private:
    unique_ptr<TcpServer>  m_pSocket;
    CONNECTIONLIST         m_vConnections;
    mutex                  m_mtxConnections;

    string                 m_strBindIp;
    uint16_t               m_sPort;
    map<string, HOSTPARAM> m_vHostParam;
    locale                 m_cLocal;
    unordered_multimap<thread::id, atomic<bool>&> m_umActionThreads;
    mutex                  m_ActThrMutex;

    static const array<MIMEENTRY, 111> MimeListe;
    static const map<uint32_t, string> RespText;

    static map<string, int32_t>       s_lstIpConnect;
    static mutex                       s_mxIpConnect;
};

#endif // !HTTPSERV_H
