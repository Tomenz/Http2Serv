// SockTest.cpp : Definiert den Einstiegspunkt für die Konsolenanwendung.
//
#include <iostream>
#include <conio.h>

#include "HttpFetch.h"
#include "Base64.h"

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
	//fetch.Fetch("https://twitter.com/");
    //fetch.Fetch("https://www.microsoft.com/de-de");
    //fetch.Fetch("https://192.168.161.1/index.htm");
    //fetch.Fetch("https://192.66.65.226/");
    //fetch.Fetch("https://www.google.de/");
    //fetch.Fetch("http://www.heise.de/");
    //fetch.Fetch("https://avm.de/");
    //fetch.Fetch("https://www.elumatec.de/");
    //fetch.Fetch("https://http2.golang.org/gophertiles?latency=0");
    //fetch.Fetch("https://www.httpwatch.com/httpgallery/chunked/chunkedimage.aspx");

    string strBase64 = Base64::Encode("Tomenz@gmx.net:mazda123", 23);
    fetch.AddToHeader("Authorization", "Basic " + strBase64);
    fetch.AddToHeader("Depth", "1");
    //fetch.AddContent("Hallo Welt", 10);
    //fetch.AddToHeader("Content-Length", "10");
    fetch.Fetch("https://webdav.magentacloud.de/", "PROPFIND");
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

