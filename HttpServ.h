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

    typedef struct
    {
        STREAMLIST& StreamList;
    }HEADERWRAPPER2;

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

    const char* SERVERSIGNATUR = "Http2Serv/1.0.0";

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
    void OnTimeout(Timer* const pTimer);

    size_t BuildH2ResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0);
    size_t BuildResponsHeader(char* const szBuffer, size_t nBufLen, int iFlag, int iRespCode, const HeadList& umHeaderList, uint64_t nContentSize = 0);
    string LoadErrorHtmlMessage(HeadList& HeaderList, int iRespCode, const wstring& strMsgDir);
    void SendErrorRespons(TcpSocket* const pTcpSocket, const shared_ptr<Timer> pTimer, int iRespCode, int iFlag, HeadList& HeaderList, HeadList umHeaderList = HeadList());
    void SendErrorRespons(const MetaSocketData& soMetaDa, const uint32_t nStreamId, function<size_t(char*, size_t, int, int, HeadList, uint64_t)> BuildRespHeader, int iRespCode, int iFlag, string& strHttpVersion, HeadList& HeaderList, HeadList umHeaderList = HeadList());

    void DoAction(const MetaSocketData soMetaDa, const uint32_t nStreamId, HEADERWRAPPER2 hw2, STREAMSETTINGS& tuStreamSettings, mutex* const pmtxStream, RESERVEDWINDOWSIZE& maResWndSizes, function<size_t(char*, size_t, int, int, const HeadList&, uint64_t)> BuildRespHeader, atomic<bool>* const patStop);
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

    const array<MIMEENTRY, 111>  MimeListe = { {
        MIMEENTRY(L"txt", "text/plain"),
        MIMEENTRY(L"rtx", "text/richtext"),
        MIMEENTRY(L"css", "text/css"),
        MIMEENTRY(L"xml", "text/xml"),
        MIMEENTRY(L"htm", "text/html"),
        MIMEENTRY(L"html", "text/html"),
        MIMEENTRY(L"shtm", "text/html"),
        MIMEENTRY(L"shtml", "text/html"),
        MIMEENTRY(L"rtf", "text/rtf"),
        MIMEENTRY(L"js", "application/javascript"),
        MIMEENTRY(L"tsv", "text/tab-separated-values"),
        MIMEENTRY(L"etx", "text/x-setext"),
        MIMEENTRY(L"sgm", "text/x-sgml"),
        MIMEENTRY(L"sgml", "text/x-sgml"),
        MIMEENTRY(L"xsl", "text/xsl"),
        MIMEENTRY(L"gif", "image/gif"),
        MIMEENTRY(L"jpeg", "image/jpeg"),
        MIMEENTRY(L"jpg", "image/jpeg"),
        MIMEENTRY(L"jpe", "image/jpeg"),
        MIMEENTRY(L"png", "image/png"),
        MIMEENTRY(L"svg", "image/svg+xml"),
        MIMEENTRY(L"tiff", "image/tiff"),
        MIMEENTRY(L"tif", "image/tiff"),
        MIMEENTRY(L"ras", "image/cmu-raster"),
        MIMEENTRY(L"wbmp", "image/vnd.wap.wbmp"),
        MIMEENTRY(L"fh4", "image/x-freehand"),
        MIMEENTRY(L"fh5", "image/x-freehand"),
        MIMEENTRY(L"fhc", "image/x-freehand"),
        MIMEENTRY(L"ico", "image/x-icon"),
        MIMEENTRY(L"ief", "image/ief"),
        MIMEENTRY(L"pnm", "image/x-portable-anymap"),
        MIMEENTRY(L"pbm", "image/x-portable-bitmap"),
        MIMEENTRY(L"pgm", "image/x-portable-graymap"),
        MIMEENTRY(L"ppm", "image/x-portable-pixmap"),
        MIMEENTRY(L"rgb", "image/x-rgb"),
        MIMEENTRY(L"xwd", "image/x-windowdump"),
        MIMEENTRY(L"exe", "application/octet-stream"),
        MIMEENTRY(L"com", "application/octet-stream"),
        MIMEENTRY(L"dll", "application/octet-stream"),
        MIMEENTRY(L"bin", "application/octet-stream"),
        MIMEENTRY(L"class", "application/octet-stream"),
        MIMEENTRY(L"iso", "application/octet-stream"),
        MIMEENTRY(L"zip", "application/zip"),
        MIMEENTRY(L"pdf", "application/pdf"),
        MIMEENTRY(L"ps", "application/postscript"),
        MIMEENTRY(L"ai", "application/postscript"),
        MIMEENTRY(L"eps", "application/postscript"),
        MIMEENTRY(L"pac", "application/x-ns-proxy-autoconfig"),
        MIMEENTRY(L"dwg", "application/acad"),
        MIMEENTRY(L"dxf", "application/dxf"),
        MIMEENTRY(L"mif", "application/mif"),
        MIMEENTRY(L"doc", "application/msword"),
        MIMEENTRY(L"dot", "application/msword"),
        MIMEENTRY(L"ppt", "application/mspowerpoint"),
        MIMEENTRY(L"ppz", "application/mspowerpoint"),
        MIMEENTRY(L"pps", "application/mspowerpoint"),
        MIMEENTRY(L"pot", "application/mspowerpoint"),
        MIMEENTRY(L"xls", "application/msexcel"),
        MIMEENTRY(L"xla", "application/msexcel"),
        MIMEENTRY(L"hlp", "application/mshelp"),
        MIMEENTRY(L"chm", "application/mshelp"),
        MIMEENTRY(L"sh", "application/x-sh"),
        MIMEENTRY(L"csh", "application/x-csh"),
        MIMEENTRY(L"latex", "application/x-latex"),
        MIMEENTRY(L"tar", "application/x-tar"),
        MIMEENTRY(L"bcpio", "application/x-bcpio"),
        MIMEENTRY(L"cpio", "application/x-cpio"),
        MIMEENTRY(L"sv4cpio", "application/x-sv4cpio"),
        MIMEENTRY(L"sv4crc", "application/x-sv4crc"),
        MIMEENTRY(L"hdf", "application/x-hdf"),
        MIMEENTRY(L"ustar", "application/x-ustar"),
        MIMEENTRY(L"shar", "application/x-shar"),
        MIMEENTRY(L"tcl", "application/x-tcl"),
        MIMEENTRY(L"dvi", "application/x-dvi"),
        MIMEENTRY(L"texinfo", "application/x-texinfo"),
        MIMEENTRY(L"texi", "application/x-texinfo"),
        MIMEENTRY(L"t", "application/x-troff"),
        MIMEENTRY(L"tr", "application/x-troff"),
        MIMEENTRY(L"roff", "application/x-troff"),
        MIMEENTRY(L"man", "application/x-troff-man"),
        MIMEENTRY(L"me", "application/x-troff-me"),
        MIMEENTRY(L"ms", "application/x-troff-ms"),
        MIMEENTRY(L"nc", "application/x-netcdf"),
        MIMEENTRY(L"cdf", "application/x-netcdf"),
        MIMEENTRY(L"src", "application/x-wais-source"),
        MIMEENTRY(L"au", "audio/basic"),
        MIMEENTRY(L"snd", "audio/basic"),
        MIMEENTRY(L"aif", "audio/x-aiff"),
        MIMEENTRY(L"aiff", "audio/x-aiff"),
        MIMEENTRY(L"aifc", "audio/x-aiff"),
        MIMEENTRY(L"dus", "audio/x-dspeeh"),
        MIMEENTRY(L"cht", "audio/x-dspeeh"),
        MIMEENTRY(L"midi", "audio/x-midi"),
        MIMEENTRY(L"mid", "audio/x-midi"),
        MIMEENTRY(L"ram", "audio/x-pn-realaudio"),
        MIMEENTRY(L"ra", "audio/x-pn-realaudio"),
        MIMEENTRY(L"rpm", "audio/x-pn-realaudio-plugin"),
        MIMEENTRY(L"mpeg", "video/mpeg"),
        MIMEENTRY(L"mpg", "video/mpeg"),
        MIMEENTRY(L"mpe", "video/mpeg"),
        MIMEENTRY(L"qt", "video/quicktime"),
        MIMEENTRY(L"mov", "video/quicktime"),
        MIMEENTRY(L"avi", "video/x-msvideo"),
        MIMEENTRY(L"movie", "video/x-sgi-movie"),
        MIMEENTRY(L"wrl", "x-world/x-vrml"),
        MIMEENTRY(L"jar", "application/x-jar"),
        MIMEENTRY(L"jnlp", "application/x-java-jnlp-file"),
        MIMEENTRY(L"jad", "text/vnd.sun.j2me.app-descriptor"),
        MIMEENTRY(L"wml", "text/vnd.wap.wml"),
        MIMEENTRY(L"wmlc", "application/vnd.wap.wmlc"),
        MIMEENTRY(L"wbmp", "image/vnd.wap.wbmp")
    } };

    map<uint32_t, string> RespText = {
        {100, "Continue"},
        {101, "Switching Protocols"},
        {200, "OK"},
        {201, "Created"},
        {202, "Accepted"},
        {203, "Non-Authoritative Information"},
        {204, "No Content"},
        {205, "Reset Content"},
        {206, "Partial Content"},
        {207, "Multi-Status"},
        {300, "Multiple Choices"},
        {301, "Moved Permanently"},
        {302, "Moved Temporarily"},
        {303, "See Other"},
        {304, "Not Modified"},
        {305, "Use Proxy"},
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {402, "Payment Required"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {406, "Not Acceptable"},
        {407, "Proxy Authentication Required"},
        {408, "Request Timeout"},
        {409, "Conflict"},
        {410, "Gone"},
        {411, "Length Required"},
        {412, "Precondition Failed"},
        {413, "Request Entity Too Large"},
        {414, "Request-URI Too Long"},
        {415, "Unsupported Media Type"},
        {416, "Requested Range Not Satisfiable"},
        {417, "Expectation Failed"},
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
        {502, "Bad Gateway"},
        {503, "Service Unavailable"},
        {504, "Gateway Timeout"},
        {505, "HTTP Version Not Supported"}
    };
};
