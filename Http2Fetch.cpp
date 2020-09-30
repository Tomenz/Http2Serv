// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//
#include <iostream>
#include <unordered_map>
#include <codecvt>
#include <regex>
#include <fcntl.h>
#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
#include <io.h>
#endif

#include "HttpFetch.h"
#include "CommonLib/Base64.h"

int main(int argc, const char* argv[], char **envp)
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));

    //_setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), O_BINARY);
    _setmode(_fileno(stdout), O_BINARY);
#endif

    map<wstring, wstring> mapEnvList;
    for (char **env = envp; *env != 0; ++env)
    {
        string strTmp = *env;
        wstring wstrTmp = wstring(begin(strTmp), end(strTmp));

        size_t nPos = wstrTmp.find_first_of('=');
        mapEnvList.emplace(wstrTmp.substr(0, nPos), wstrTmp.substr(nPos + 1));
    }

    map<wstring, wstring>::iterator itFound;
    string strProxyMark;
    if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"PROXYMARK") ? true : false;  })) != end(mapEnvList))
        strProxyMark = wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(itFound->second);

//this_thread::sleep_for(chrono::milliseconds(20000));
    //locale::global(std::locale(""));
    function<string&(string&)> fnContentFilter;
    bool               bFertig = false;
    mutex              m_mxStop;
    condition_variable m_cvStop;
    HttpFetch fetch([&](HttpFetch* pFetch, void* vpUserData, uint32_t nlen)
    {
        if (vpUserData == nullptr)
        {
            bFertig = true;
            m_cvStop.notify_all();
        }
        else if (pFetch->HasOutRespHeader() == true)
        {
            // Header die der Zielserver sendet, aber nicht an den Klient weitergegeben werden, oder vom Reverse Proxy gesetzt werden
            static const vector<string> vecNoGoHeader = { {"connection"}, {"transfer-encoding" }, { "keep-alive" }, { "date"}, { "server"}, { "content-encoding" }, { "content-length" } };
            HeadList umRespHeader = pFetch->GetHeaderList();
            for (auto& itHeader : umRespHeader)
            {
                if (find_if(begin(vecNoGoHeader), end(vecNoGoHeader), [&](const auto& strNoGo) { return itHeader.first.compare(strNoGo) == 0 ? true : false; }) == end(vecNoGoHeader))
                    cout << itHeader.first << ": " << itHeader.second << "\r\n";

                string strHeader(itHeader.first);
                transform(begin(strHeader), end(strHeader), begin(strHeader), ::tolower);
                if (strHeader.compare("content-type") == 0)
                {
                    string strValue(itHeader.second);
                    transform(begin(strValue), end(strValue), begin(strValue), ::tolower);
                    if (itHeader.second.find("text/html") != string::npos || itHeader.second.find("application/javascript") != string::npos || itHeader.second.find("text/css") != string::npos)
                    {
                        //size_t nPos = strPath.find_last_of(L".");
                        //if (nPos == string::npos || strPath.substr(nPos + 1).compare(L"woff") != 0)

                        fnContentFilter = [&](string& strBuf) -> string&
                        {
                            const static regex rx("(src=\\\"|src=\\\'|url\\(\\\'|url\\(\\\"|href=\\\"|href=\\\'|url:\\\"|url:\\\')[/]?([^\\\"\\\'h])");
                            strBuf = regex_replace(strBuf, rx, ("$1" + strProxyMark + "/$2").c_str());
                            size_t nPos = strBuf.find("\'/\'");
                            if (nPos != string::npos)
                                strBuf = strBuf.replace(nPos + 1, 1, strProxyMark + "/");
                            return strBuf;
                        };
                    }
                }
                    
            }
            cout << "\r\n";
            
            pFetch->OutRespHeader(false);
        }
        
        if (vpUserData != nullptr && nlen > 0)
        {
            if (fnContentFilter != nullptr)
            {
                string strContent(reinterpret_cast<char*>(vpUserData), nlen);
                cout << fnContentFilter(strContent);
            }
            else
                cout << string(reinterpret_cast<char*>(vpUserData), nlen);
        }
    }, nullptr);

    wstring strMethode;
    if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [&](auto pr) { return pr.first == L"REQUEST_METHOD" ? true : false;  })) != end(mapEnvList))
    {
        strMethode = itFound->second;

        wstring strPath;
        if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"PATH_INFO") ? true : false;  })) != end(mapEnvList))
            strPath = itFound->second;

        wstring strContentLength;
        if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"CONTENT_LENGTH") ? true : false;  })) != end(mapEnvList))
            strContentLength = itFound->second;

        wstring strContentTyp;
        if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"CONTENT_TYPE") ? true : false;  })) != end(mapEnvList))
            strContentTyp = itFound->second;

        wstring strHost;
        if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"HTTP_HOST") ? true : false; })) != end(mapEnvList))
            strHost = itFound->second;

        wstring strHttps;
        if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"HTTPS") ? true : false;  })) != end(mapEnvList))
            strHttps = itFound->second;

        wstring strQuery;
        if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"QUERY_STRING") ? true : false;  })) != end(mapEnvList))
            strQuery = itFound->second;

        wstring strUrl;
        if ((itFound = find_if(begin(mapEnvList), end(mapEnvList), [](auto pr) { return (pr.first == L"PROXYURL") ? true : false;  })) != end(mapEnvList))
            strUrl = itFound->second;

        fetch.OutRespHeader();

        if (strContentLength.empty() == false)
        {
//this_thread::sleep_for(chrono::milliseconds(30000));
            fetch.AddToHeader("Content-Length", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strContentLength));
            if (strContentTyp.empty() == false)
                fetch.AddToHeader("Content-Type", wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strContentTyp));
        }

        for (auto& itEnv : mapEnvList)
        {
            if (itEnv.first.find(L"HTTP_") == 0 && itEnv.first.compare(L"HTTP_HOST") != 0)
            {
                string strHeader = wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(itEnv.first.substr(5));
                transform(begin(strHeader), end(strHeader), begin(strHeader), ::tolower);
                replace(begin(strHeader), end(strHeader), '_', '-');
                strHeader[0] = toupper(strHeader[0]);

                size_t nPos = strHeader.find('-');
                while (nPos != string::npos && strHeader.size() > nPos)
                {
                    strHeader[nPos + 1] = toupper(strHeader[nPos + 1]);
                    nPos = strHeader.find('-', nPos + 1);
                }
                fetch.AddToHeader(strHeader, wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(itEnv.second));
            }
        }

        if (strUrl.back() == L'/' && strPath.front() == L'/')
            strUrl.erase(strUrl.size() - 1);
        fetch.Fetch(/*strHttps.compare(L"on") == 0 ? string("https://") : string("http://") + */wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strUrl) + wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strPath), wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(strMethode));

        while (!cin.eof())
        {
            char caBuffer[4096];
            cin.read(caBuffer, 4096);
            size_t nRead = cin.gcount();
            fetch.AddContent(caBuffer, nRead);
        }
        fetch.AddContent(nullptr, 0);

        unique_lock<mutex> lock(m_mxStop);
        m_cvStop.wait(lock, [&]() { return bFertig; });
/*
        HeadList umRespHeader = fetch.GetHeaderList();
        if (strProxyMark.empty() == false && fetch.GetContentSize() > 0)
        {
            for (auto& itHeader : umRespHeader)
            {
                string strHeader(itHeader.first);
                transform(begin(strHeader), end(strHeader), begin(strHeader), ::tolower);
                if (strHeader.compare("content-type") == 0)
                {
                    string strValue(itHeader.second);
                    transform(begin(strValue), end(strValue), begin(strValue), ::tolower);
                    if (itHeader.second.find("text/html") != string::npos || itHeader.second.find("application/javascript") != string::npos || itHeader.second.find("text/css") != string::npos)
                    {
                        size_t nPos = strPath.find_last_of(L".");
                        if (nPos == string::npos || strPath.substr(nPos + 1).compare(L"woff") != 0)
                        {
                            shared_ptr<TempFile> pNewFile = make_shared<TempFile>();
                            pNewFile->Open();

                            ((TempFile&)fetch).Rewind();
                            //const static regex rx("(href=\\\"/|src=\\\"/|url\\(\\\'/)");
                            //const static regex rx("(\\\"/|\\\'/|url\\(/)([^>\\s])");
                            const static regex rx("(src=\\\"|src=\\\'|url\\(\\\'|url\\(\\\"|href=\\\"|href=\\\'|url:\\\"|url:\\\')[/]?([^\\\"\\\'h])");
                            size_t nRead;
                            do
                            {
                                string strBuffer(2048, 0);
                                nRead = ((TempFile&)fetch).Read(reinterpret_cast<unsigned char*>(&strBuffer[0]), 2048);
                                strBuffer.resize(nRead);
                                strBuffer = regex_replace(strBuffer, rx, ("$1" + strProxyMark + "/$2").c_str());
                                nPos = strBuffer.find("\'/\'");
                                if (nPos != string::npos)
                                    strBuffer = strBuffer.replace(nPos + 1, 1, strProxyMark + "/");
                                pNewFile->Write(strBuffer.c_str(), strBuffer.size());
                            } while (nRead == 2048);

                            pNewFile->Close();
                            ((TempFile&)fetch).Rewind();
//                            fetch.ExchangeTmpFile(pNewFile);
                        }
                    }
                }
            }
        }*/
/*
        // Header die der Zielserver sendet, aber nicht an den Klient weitergegeben werden, oder vom Reverse Proxy gesetzt werden
        static const vector<string> vecNoGoHeader = { {"connection"}, {"transfer-encoding" }, { "keep-alive" }, { "date"} };
        for (auto& itHeader : umRespHeader)
        {
            if (find_if(begin(vecNoGoHeader), end(vecNoGoHeader), [&](const auto& strNoGo) { return itHeader.first.compare(strNoGo) == 0 ? true : false; }) == end(vecNoGoHeader))
            cout << itHeader.first << ": " << itHeader.second << "\r\n";
        }
        cout << "\r\n";
        if (fetch.GetContentSize() > 0)
            cout << (TempFile&)fetch << flush;
/*
        size_t nSize = ((TempFile&)fetch).GetFileLength();
        OutputDebugString(wstring(L"CGI output: " + strPath + L", Bytes: " + to_wstring(nSize) + L"\r\n").c_str());

        while (nSize == 0)
            this_thread::sleep_for(chrono::microseconds(100));

size_t nPos = strPath.find_last_of(L"/\\");
if (nPos != string::npos)
{
    ((TempFile&)fetch).Close();

    strPath.erase(0, nPos + 1);
    nPos = strPath.find(L'.');
    if (nPos != string::npos)
    {
        strPath.replace(nPos, 1, L"(" + to_wstring(0) + L").");
        string str(((TempFile&)fetch).GetFileName());
        wstring strTmp(str.begin(), str.end());
        if (CopyFile(strTmp.c_str(), (L"temp\\" + strPath).c_str(), FALSE) == 0)
            OutputDebugString(wstring(L"Error bei CopyFile: " + to_wstring(GetLastError()) + L"\r\n").c_str());
    }
}*/
        return 0;
    }

    if (argc > 1)
    {
        string strUrl;
        while (++argv, --argc)
        {
            if (argv[0][0] == '-')
            {
                switch ((argv[0][1] & 0xdf))
                {
                }
            }
            else
                strUrl = argv[0];
        }

        if (strUrl.empty() == false)
        {
            fetch.Fetch(strUrl);

            unique_lock<mutex> lock(m_mxStop);
            m_cvStop.wait(lock, [&]() { return bFertig; });

            //cout << (TempFile&)fetch << flush;
            return 0;
        }
        return -1;
    }

//    fetch.AddToHeader("User-Agent", "http2 Util, webdav 0.1");
    fetch.AddToHeader("User-Agent", "http2fetch/0.9-beta (Windows NT 10.0; Win64; x64)");
//    fetch.AddToHeader("Upgrade", "h2c");
//    fetch.AddToHeader("HTTP2-Settings", Base64::Encode("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x3\x0\x0\x3\x38\x0\x4\x0\x60\x0\x0", 21, true));
    fetch.AddToHeader("Accept", "*/*");
    //fetch.AddToHeader("Accept-Encoding", "br;q=1.0, gzip;q=0.8, deflate;q=0.7, identity;q=0.5, *;q=0");
    fetch.AddToHeader("Accept-Encoding", "gzip, deflate, br");
    fetch.AddToHeader("Cache-Control", "no-cache");

    fetch.AddToHeader("Accept-Language", "de,en-US;q=0.7,en;q=0.3");    // wird für elumatec.com benötigt
    //fetch.AddToHeader("DNT", "1");
    //fetch.AddToHeader("Connection", "keep-alive");
    //fetch.AddToHeader("Connection", "close");
    //fetch.AddToHeader("Upgrade-Insecure-Requests", "1");
    //fetch.AddToHeader("Pragma", "no-cache");


    //fetch.AddToHeader("Authorization", "Basic " + Base64::Encode("pi:raspberry", 12));
//    fetch.AddToHeader("Authorization", "Basic " + Base64::Encode("Tomenz@gmx.net:mazda123", 23));
//    fetch.AddToHeader("Depth", "1");    // ("0" | "1" | "1,noroot" | "infinity" | "infinity,noroot")
//    fetch.AddToHeader("Content-Length", "81612");
    fetch.AddToHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundarydNro4YJvDSiGw1qA");
//    fetch.AddContent("Hallo Welt", 10);
//    fetch.AddToHeader("Content-Length", "10");
//    fetch.AddToHeader("Content-Type", "application/octet-stream");
//    fetch.AddContent("<?xml version=\"1.0\" encoding=\"UTF-8\" ?><D:propertyupdate xmlns:D=\"DAV:\" xmlns:Z=\"urn:schemas-microsoft-com:\"><D:set><D:prop><Z:Win32CreationTime>Sat, 19 Oct 2013 09:17:33 GMT</Z:Win32CreationTime><Z:Win32LastAccessTime>Sat, 19 Oct 2013 09:17:33 GMT</Z:Win32LastAccessTime><Z:Win32LastModifiedTime>Mon, 19 Oct 2015 13:36:03 GMT</Z:Win32LastModifiedTime></D:prop></D:set></D:propertyupdate>", 388);
//    fetch.AddToHeader("Content-Length", "388");
//    fetch.AddToHeader("Content-Type", "text/xml; charset=\"utf-8\"");
//    fetch.AddToHeader("Expect", "100-continue");
    //fetch.AddToHeader("Content-Type", "application/json");
    //fetch.AddToHeader("Authorization", "key=AAAAlg2diDc:APA91bEQZ3CKLWnYA35_5sBR-RzOgtJ0NEapM4C1u3x0gO6fyNdZ5CfmaQ-ASR7uKGe9_9WLPwqWjiaYmsKKlC2QXBDst5GLnzJBszegKoSKn79x6v21i0JUSK7giNvmFzVIs6J-SKjs");
    //fetch.AddToHeader("Content-Length", "119");
    //fetch.AddContent("{\"notification\":{\"title\": \"Firebase -  Test\",\"text\" : \"Firebase Test from Advanced Rest Client\" },\"to\" : \"/topics/all\"}", 119);

    //fetch.AddToHeader("Content-Length", "114");
    //fetch.AddContent("{\"data\":{\"title\": \"Firebase -  Test\",\"message\" : \"Firebase Test from Advanced Rest Client\" },\"to\" : \"/topics/all\"}", 114);

	//fetch.Fetch("https://twitter.com/");
    //fetch.Fetch("https://www.microsoft.com/de-de");
//    fetch.Fetch("http://192.168.161.1/index.htm");
//    fetch.Fetch("http://192.168.161.5/");
    //fetch.Fetch("https://192.66.65.226/");
//    fetch.Fetch("https://www.google.de/");
    //fetch.Fetch("https://www.heise.de/");
    //fetch.Fetch("https://avm.de/");
    //fetch.Fetch("https://www.elumatec.com/de/start");
    //fetch.Fetch("https://http2.golang.org/gophertiles?latency=0");
    //fetch.Fetch("https://www.httpwatch.com/httpgallery/chunked/chunkedimage.aspx");
    //fetch.Fetch("https://tools.keycdn.com/brotli-test");
    //fetch.Fetch("https://tools.keycdn.com/http2-test");
  //fetch.Fetch("https://fcm.googleapis.com/fcm/send", "POST");
    //fetch.Fetch("http://192.168.161.181:9981/extjs.html");
    fetch.Fetch("https://192.168.161.1/upload.php", "POST");

//    fetch.Fetch("https://webdav.magentacloud.de/hallowelt.txt", "PUT");
//    fetch.Fetch("https://webdav.magentacloud.de/hallowelt.txt", "PROPPATCH");
//    fetch.Fetch("https://webdav.magentacloud.de/", "PROPFIND");
    //fetch.Fetch("http://192.66.65.226/", "POST");

    while (!cin.eof() && bFertig == false)
    {
        std::streampos pos = cin.tellg();
        cin.seekg(0, std::ios::end);
        std::streamsize len = cin.tellg() - pos;
        cin.seekg(pos);
        if (len > 0)
        {
            char caBuffer[0x4000];
            cin.read(caBuffer, 0x4000);
            size_t nRead = cin.gcount();
            fetch.AddContent(caBuffer, nRead);
        }
    }
    fetch.AddContent(nullptr, 0);

    unique_lock<mutex> lock(m_mxStop);
    m_cvStop.wait(lock, [&]() { return bFertig; });

#ifdef _DEBUG
/*    if (fetch.GetStatus() == 200)
    {
        HeadList umRespHeader = fetch.GetHeaderList();
        auto contenttype = umRespHeader.find("content-type");
        if (contenttype != umRespHeader.end() && (contenttype->second.find("text/") != string::npos || contenttype->second.find("xml") != string::npos))
        {
            cout << (TempFile&)fetch << flush;
        }
    }*/
#endif

    wcerr << L"\r\nRequest beendet mit StatusCode: " << to_wstring(fetch.GetStatus()) << endl;

#if defined(_WIN32) || defined(_WIN64)
    _getch();
#else
    getchar();
#endif

    return 0;
}

