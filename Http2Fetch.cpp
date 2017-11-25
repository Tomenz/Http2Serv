// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//
#include <iostream>
#include <conio.h>

#include "HttpFetch.h"
#include "CommonLib/Base64.h"

int main(int argc, const char* argv[])
{
#if defined(_WIN32) || defined(_WIN64)
    // Detect Memory Leaks
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG));

    //_setmode(_fileno(stdout), _O_U16TEXT);
#endif

    //locale::global(std::locale(""));
    bool bFertig = false;
    HttpFetch fetch([&](HttpFetch* pFetch, void* vpUserData) { bFertig = true; }, 0);

//    fetch.AddToHeader("User-Agent", "http2 Util, webdav 0.1");
    fetch.AddToHeader("User-Agent", "http2fetch version 0.9 beta");
//    fetch.AddToHeader("Upgrade", "h2c");
//    fetch.AddToHeader("HTTP2-Settings", Base64::Encode("\x0\x0\xc\x4\x0\x0\x0\x0\x0\x0\x3\x0\x0\x3\x38\x0\x4\x0\x60\x0\x0", 21, true));
    fetch.AddToHeader("Accept", "*/*");
    fetch.AddToHeader("Accept-Encoding", "br;q=1.0, gzip;q=0.8, deflate;q=0.7, identity;q=0.5, *;q=0");

//    fetch.AddToHeader("Authorization", "Basic " + Base64::Encode("Tomenz@gmx.net:mazda123", 23));
//    fetch.AddToHeader("Depth", "1");    // ("0" | "1" | "1,noroot" | "infinity" | "infinity,noroot")
//    fetch.AddToHeader("Content-Length", "0");
//    fetch.AddContent("Hallo Welt", 10);
//    fetch.AddToHeader("Content-Length", "10");
//    fetch.AddToHeader("Content-Type", "application/octet-stream");
//    fetch.AddContent("<?xml version=\"1.0\" encoding=\"UTF-8\" ?><D:propertyupdate xmlns:D=\"DAV:\" xmlns:Z=\"urn:schemas-microsoft-com:\"><D:set><D:prop><Z:Win32CreationTime>Sat, 19 Oct 2013 09:17:33 GMT</Z:Win32CreationTime><Z:Win32LastAccessTime>Sat, 19 Oct 2013 09:17:33 GMT</Z:Win32LastAccessTime><Z:Win32LastModifiedTime>Mon, 19 Oct 2015 13:36:03 GMT</Z:Win32LastModifiedTime></D:prop></D:set></D:propertyupdate>", 388);
//    fetch.AddToHeader("Content-Length", "388");
//    fetch.AddToHeader("Content-Type", "text/xml; charset=\"utf-8\"");
//    fetch.AddToHeader("Expect", "100-continue");
    fetch.AddToHeader("Content-Type", "application/json");
    fetch.AddToHeader("Authorization", "key=AAAAlg2diDc:APA91bEQZ3CKLWnYA35_5sBR-RzOgtJ0NEapM4C1u3x0gO6fyNdZ5CfmaQ-ASR7uKGe9_9WLPwqWjiaYmsKKlC2QXBDst5GLnzJBszegKoSKn79x6v21i0JUSK7giNvmFzVIs6J-SKjs");
    fetch.AddToHeader("Content-Length", "119");
    fetch.AddContent("{\"notification\":{\"title\": \"Firebase -  Test\",\"text\" : \"Firebase Test from Advanced Rest Client\" },\"to\" : \"/topics/all\"}", 119);
    //fetch.AddToHeader("Content-Length", "114");
    //fetch.AddContent("{\"data\":{\"title\": \"Firebase -  Test\",\"message\" : \"Firebase Test from Advanced Rest Client\" },\"to\" : \"/topics/all\"}", 114);

	//fetch.Fetch("https://twitter.com/");
    //fetch.Fetch("https://www.microsoft.com/de-de");
//    fetch.Fetch("http://192.168.161.1/index.htm");
    //fetch.Fetch("https://192.66.65.226/");
    //fetch.Fetch("https://www.google.de/");
    //fetch.Fetch("https://www.heise.de/");
    //fetch.Fetch("https://avm.de/");
    //fetch.Fetch("https://www.elumatec.de/");
    //fetch.Fetch("https://http2.golang.org/gophertiles?latency=0");
    //fetch.Fetch("https://www.httpwatch.com/httpgallery/chunked/chunkedimage.aspx");
    //fetch.Fetch("https://tools.keycdn.com/brotli-test");
    //fetch.Fetch("https://tools.keycdn.com/http2-test");
    fetch.Fetch("https://fcm.googleapis.com/fcm/send", "POST");


//    fetch.Fetch("https://webdav.magentacloud.de/hallowelt.txt", "PUT");
//    fetch.Fetch("https://webdav.magentacloud.de/hallowelt.txt", "PROPPATCH");
//    fetch.Fetch("https://webdav.magentacloud.de/", "PROPFIND");
    //fetch.Fetch("http://192.66.65.226/", "POST");

    //while (fetch.RequestFinished() == false)
    while (bFertig == false)
        this_thread::sleep_for(chrono::milliseconds(10));

#ifdef _DEBUG
    if (fetch.GetStatus() == 200)
    {
        HeadList umRespHeader = fetch.GetHeaderList();
        auto contenttype = umRespHeader.find("content-type");
        if (contenttype != umRespHeader.end() && (contenttype->second.find("text/") != string::npos || contenttype->second.find("xml") != string::npos))
        {
            cout << (TempFile&)fetch << flush;
        }
    }
#endif

    wcerr << L"\r\nRequest beendet!" << endl;

#if defined(_WIN32) || defined(_WIN64)
    //while (::_kbhit() == 0)
    //    this_thread::sleep_for(chrono::milliseconds(1));
    _getch();
#else
    getchar();
#endif

//	fetch.Stop();

    return 0;
}

