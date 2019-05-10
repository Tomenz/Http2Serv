/* Copyright (C) Hauck Software Solutions - All Rights Reserved
 * You may use, distribute and modify this code under the terms
 * that changes to the code must be reported back the original
 * author
 *
 * Company: Hauck Software Solutions
 * Author:  Thomas Hauck
 * Email:   Thomas@fam-hauck.de
 *
 */

#pragma once

#include <unordered_map>

#include "socketlib/StdSocket.h"
#include "Timer.h"
#include "TempFile.h"
#include "H2Proto.h"

using namespace std;

class CHttpServ : public Http2Protocol
{
    typedef struct
    {
        shared_ptr<Timer> pTimer;
        string strBuffer;
        bool bIsH2Con;
        uint64_t nContentsSoll;
        uint64_t nContentRecv;
        shared_ptr<TempFile> TmpFile;
        HeadList HeaderList;
        deque<HEADERENTRY> lstDynTable;
        shared_ptr<mutex> mutStreams;
        STREAMLIST H2Streams;
        STREAMSETTINGS StreamParam;
        RESERVEDWINDOWSIZE StreamResWndSizes;
        shared_ptr<atomic_bool> atStop;
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
        string  m_strDhParam;
        string  m_strSslCipher;
        unordered_map<wstring, wstring> m_mstrRewriteRule;
        unordered_map<wstring, tuple<wstring, bool>> m_mstrAliasMatch;
        unordered_map<wstring, wstring> m_mstrForceTyp;
        unordered_map<wstring, vector<wstring>> m_mFileTypeAction;
        vector<tuple<wstring, wstring, wstring>> m_vRedirMatch;
        vector<tuple<wstring, wstring, wstring>> m_vEnvIf;  //  Request_URI "^/.*" "ENV-Name=ENV-Value"
        vector<string> m_vDeflateTyps;
        vector<wstring> m_vOptionsHandler;
        unordered_map<wstring, tuple<wstring, wstring, vector<wstring>>> m_mAuthenticate;   // Directory, (Realm, Methode, Liste mit Usern)
        vector<pair<string, string>> m_vHeader; // Header-Name, Header-Value
        unordered_map<wstring, wstring> m_mstrReverseProxy;
    } HOSTPARAM;

public:

    CHttpServ(const wstring& strRootPath = wstring(L"."), const string& strBindIp = string("127.0.0.1"), short sPort = 80, bool bSSL = false);
    CHttpServ(const CHttpServ&) = delete;
    CHttpServ(CHttpServ&& other) { *this = move(other); }
    CHttpServ& operator=(const CHttpServ&) = delete;
    CHttpServ& operator=(CHttpServ&& other);
    virtual ~CHttpServ();

    bool Start();
    bool Stop();
    bool IsStopped() noexcept;

    HOSTPARAM& GetParameterBlockRef(const string& szHostName);
    void ClearAllParameterBlocks();
    const string& GetBindAdresse() noexcept;
    short GetPort() noexcept;

private:
    void OnNewConnection(const vector<TcpSocket*>& vNewConnections);
    void OnDataRecieved(TcpSocket* const pTcpSocket);
    void OnSocketError(BaseSocket* const pBaseSocket);
    void OnSocketCloseing(BaseSocket* const pBaseSocket);
    void OnTimeout(const Timer* const pTimer, void*);

    size_t BuildH2ResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0);
    size_t BuildResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0);
    string LoadErrorHtmlMessage(HeadList& HeaderList, int iRespCode, const wstring& strMsgDir);
    void SendErrorRespons(TcpSocket* const pTcpSocket, const shared_ptr<Timer> pTimer, int iRespCode, int iFlag, HeadList& HeaderList, HeadList umHeaderList = HeadList());
    void SendErrorRespons(const MetaSocketData& soMetaDa, const uint32_t nStreamId, function<size_t(char*, size_t, int, int, HeadList, uint64_t)> BuildRespHeader, int iRespCode, int iFlag, string& strHttpVersion, HeadList& HeaderList, HeadList umHeaderList = HeadList());

    void DoAction(const MetaSocketData soMetaDa, const uint32_t nStreamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* const pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, function<size_t(char*, size_t, int, int, const HeadList&, uint64_t)> BuildRespHeader, atomic<bool>* const patStop);
    virtual void EndOfStreamAction(const MetaSocketData soMetaDa, const uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* const pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, atomic<bool>* const patStop) override;

private:
    TcpServer*             m_pSocket;
    CONNECTIONLIST         m_vConnections;
    mutex                  m_mtxConnections;

    string                 m_strBindIp;
    short                  m_sPort;
    map<string, HOSTPARAM> m_vHostParam;
    locale                 m_cLocal;
    unordered_multimap<thread::id, atomic<bool>*> m_umActionThreads;
    mutex                  m_ActThrMutex;

    static const array<MIMEENTRY, 111> MimeListe;
    static const map<uint32_t, string> RespText;
};
