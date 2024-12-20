
#ifndef HTTPFETCH_H
#define HTTPFETCH_H

#include "SocketLib/SocketLib.h"
#include "Timer.h"
#include "H2Proto.h"
#include "TempFile.h"

class HttpFetch : public Http2Protocol
{
public:
    HttpFetch(function<void(HttpFetch*, void*, uint32_t)>, void*);
    virtual ~HttpFetch();
    bool Fetch(const string& strAdresse, const string& strMethode = string("GET"));
    void AddToHeader(string strHeader, string strHeaderValue) { m_umAddHeader.emplace_back(make_pair(strHeader, strHeaderValue)); };
    bool AddContent(char* pBuffer, uint64_t nBufSize);
    void OutRespHeader(bool bOutpHeader = true) { m_bOutpHeader = bOutpHeader; }
    bool HasOutRespHeader() { return m_bOutpHeader; }
    uint32_t GetStatus() { return m_uiStatus; }
    HeadList GetHeaderList() { return m_umRespHeader; }
//    uint64_t GetContentSize() { return m_pTmpFileRec == nullptr ? 0 : m_pTmpFileRec->GetFileLength(); }
//    bool GetContent(uint8_t Buffer[], uint64_t nBufSize);
//    void ExchangeTmpFile(shared_ptr<TempFile>&);

//    operator TempFile& ();

private:
    void Stop();
    void Connected(TcpSocket* pTcpSocket);
    void DatenEmpfangen(TcpSocket* pTcpSocket);
    void SocketError(BaseSocket* pBaseSocket);
    void SocketClosing(BaseSocket* pBaseSocket);
    void OnTimeout(const Timer<TcpSocket>* const pTimer, TcpSocket*);
    void EndOfStreamAction(const MetaSocketData& soMetaDa, const uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex& pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, atomic<bool>& patStop, mutex& pmtxReqdata, deque<unique_ptr<char[]>>& vecData, deque<AUTHITEM>& lstAuthInfo) override;

private:
    TcpSocket*           m_pcClientCon;
    string               m_strMethode;
    string               m_strServer;
    short                m_sPort;
    string               m_strPath;
    bool                 m_UseSSL;
    uint32_t             m_uiStatus;

    bool                 m_bIsHttp2;
    bool                 m_bOutpHeader;
    deque<HEADERENTRY>   m_qDynTable;
    mutex                m_mtxStreams;
    STREAMLIST           m_umStreamCache;
    STREAMSETTINGS       m_tuStreamSettings = make_tuple(UINT32_MAX, 65535, 16384, UINT32_MAX, 4096);
    RESERVEDWINDOWSIZE   m_mResWndSizes;
//    shared_ptr<TempFile> m_pTmpFileRec;
//    shared_ptr<TempFile> m_pTmpFileSend;
    unique_ptr<Timer<TcpSocket>>    m_Timer;
    MetaSocketData       m_soMetaDa{};
    string               m_strBuffer;
    HeadList             m_umRespHeader;
    HeadList             m_umAddHeader;

    bool                 m_bEndOfHeader;
    uint64_t             m_nContentLength;
    uint64_t             m_nContentReceived;
    int                  m_nChuncked;
    size_t               m_nNextChunk;
    size_t               m_nChunkFooter;

    mutex               m_mxVecData;
    deque<unique_ptr<char[]>> m_vecData;

    function<void(HttpFetch*, void*, uint32_t)> m_fnNotify;
};

#endif // !HTTPFETCH_H
