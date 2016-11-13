#pragma once

#include "socketlib/SslSocket.h"
#include "Timer.h"
#include "H2Proto.h"

class HttpFetch : public Http2Protocol
{
public:
    HttpFetch(function<void(HttpFetch*, void*)>, void*);
    virtual ~HttpFetch();
    bool Fetch(string strAdresse);
    uint32_t GetStatus() { return m_uiStatus; }
    HeadList GetHeaderList() { return m_umRespHeader; }
    uint64_t GetContentSize() { return m_pTmpFile == nullptr ? 0 : m_pTmpFile->GetFileLength(); }
    bool GetContent(uint8_t Buffer[], uint64_t nBufSize);

    operator TempFile& ();

private:
    void Stop();
    void Connected(TcpSocket* pTcpSocket);
    void DatenEmpfangen(TcpSocket* pTcpSocket);
    void SocketError(BaseSocket* pBaseSocket);
    void SocketCloseing(BaseSocket* pBaseSocket);
    void OnTimeout(Timer* pTimer);
    void EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop);

private:
    SslTcpSocket*        m_pcClientCon;
    string               m_strServer;
    short                m_sPort;
    string               m_strPath;
    bool                 m_UseSSL;
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
    HeadList             m_umRespHeader;

    bool                 m_bEndOfHeader;
    uint64_t             m_nContentLength;
    int                  m_nChuncked;
    size_t               m_nNextChunk;

    function<void(HttpFetch*, void*)> m_fnNotify;
    void*                             m_vpUserData;
};
