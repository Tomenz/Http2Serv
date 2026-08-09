
#include <regex>
#include <sstream>
#include <codecvt>

#include "WebSocket.h"

#if defined (_WIN32) || defined (_WIN64)
#include <Ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <dlfcn.h>
#define ConvertToByte(x) wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(x)
extern void OutputDebugString(const wchar_t* pOut);
// {   // mkfifo /tmp/dbgout
    // int fdPipe = open("/tmp/dbgout", O_WRONLY | O_NONBLOCK);
    // if (fdPipe >= 0)
    // {
        // wstring strTmp(pOut);
        // write(fdPipe, ConvertToByte(strTmp).c_str(), strTmp.size());
        // close(fdPipe);
    // }
// }
extern void OutputDebugStringA(const char* pOut);
#endif

using namespace std::placeholders;

#define ntohll(x) ( ( (uint64_t)(ntohl( (uint32_t)((x << 32) >> 32) )) << 32) | ntohl(((uint32_t)(x >> 32))))
#define htonll(x) ntohll(x)

std::function<size_t(void*, const uint8_t*, uint32_t)> WebSocket::WriteBackInstance;

WebSocket::WebSocket(const std::string& strPath, TcpSocket* pTcpSocket) : m_soSocketParam({0,0,0,0,0,0,0, 0, 0, 0, ""}), m_LibHandle(nullptr), pTextDataReceived(nullptr)
{
    m_soSocketParam.strPath = strPath;

#if defined(_WIN32) || defined(_WIN64)
#else
    m_LibHandle = dlopen("/home/c++/ModbusTCP/build/libWsLog.so", RTLD_LAZY);
    if (m_LibHandle)
    {
        pSetWriteCallback = (void (*)(void(*callback)(void* /*pId*/, const uint8_t* /*szData*/, uint32_t /*nDataLen*/), void* pId))dlsym(m_LibHandle, "SetWriteCallback");
        pTextDataReceived = (void (*)(void*, const char*, uint8_t*, uint32_t))dlsym(m_LibHandle, "TextDataReceived");
        if (!pSetWriteCallback || !pTextDataReceived) {
            OutputDebugStringA(std::string("Error: " + std::string(dlerror()) + "\r\n").c_str());
            dlclose(m_LibHandle);
            m_LibHandle = nullptr;
            return;
        }

        WebSocket::WriteBackInstance = std::bind(&WebSocket::WriteData, this, _1, _2, _3);
        pSetWriteCallback(&WebSocket::staticWriteBack, reinterpret_cast<void*>(pTcpSocket));
    }
#endif
}

WebSocket::~WebSocket()
{
#if defined(_WIN32) || defined(_WIN64)
#else
    if (m_LibHandle != nullptr)
        dlclose(m_LibHandle);
#endif
}

void WebSocket::OnDataReceivedWebSocket(TcpSocket* pTcpSocket, uint8_t* pData, size_t nDataLen)
{
    if (nDataLen > 0)
    {
        //OutputDebugString(wstring(L"Read:" + to_wstring(nDataLen) + L"\r\n").c_str());

        uint8_t* pBuffer = pData;
        uint8_t* pBufferEnd = pData + nDataLen;

        do
        {
            size_t iOffset = 0;
            if (m_soSocketParam.nLen == 0) // No Header, first call on this socket
            {
                m_soSocketParam.stHeader = *(reinterpret_cast<HEADER*>(pBuffer));
                iOffset = 2;
                m_soSocketParam.nLen = m_soSocketParam.stHeader.PLoad;
                if (m_soSocketParam.nLen == 126)
                    m_soSocketParam.nLen = ntohs(*(reinterpret_cast<short*>(pBuffer + iOffset))), iOffset += 2;
                else if (m_soSocketParam.nLen == 127)
                    m_soSocketParam.nLen = ntohll(*(reinterpret_cast<uint64_t*>(pBuffer + iOffset))), iOffset += 8;
                if (m_soSocketParam.stHeader.Mask == 1)
                    m_soSocketParam.uiMask = *(reinterpret_cast<uint32_t*>(pBuffer + iOffset)), iOffset += 4;
            }

            uint8_t* szData = pBuffer + iOffset;
            size_t nBlockSize = std::min(static_cast<size_t>(m_soSocketParam.nLen - m_soSocketParam.nReceived), nDataLen - iOffset);
#if defined(_WIN32) || defined(_WIN64)
            //OutputDebugString(wstring(L"Read:" + to_wstring(nDataLen) + L", OpCode:" + to_wstring(iter->second.stHeader.OpCode) + L", FIN:" + to_wstring(iter->second.stHeader.FIN) + L", Len:" + to_wstring(iter->second.nLen) + L", BlockSize:" + to_wstring(nBlockSize) + L"\r\n").c_str());
#endif
            if (m_soSocketParam.stHeader.Mask == 1)
            {
                for (size_t n = 0; n < nBlockSize; ++n)
                {
                    size_t m = n % 4;
                    szData[n] = szData[n] ^ reinterpret_cast<char*>(&m_soSocketParam.uiMask)[m];  // original-octet-i XOR masking-key-octet-j
                }
            }

            m_soSocketParam.nReceived += nBlockSize;

            switch (m_soSocketParam.stHeader.OpCode)
            {
            case 0: // continue
#if defined(_WIN32) || defined(_WIN64)
                //OutputDebugString(L"continue frame\r\n");
#endif
                BinaryDataReceived(pTcpSocket, m_soSocketParam.strPath, szData, static_cast<uint32_t>(nBlockSize), m_soSocketParam.stHeader.FIN == 1 ? true : false);

                if (m_soSocketParam.nReceived == m_soSocketParam.nLen)
                {
                    m_soSocketParam.stHeader = { 0, 0, 0, 0, 0, 0, 0 };
                    m_soSocketParam.nReceived = m_soSocketParam.nLen = 0;
                }
                break;

            case 1: // Text
            {
                if (m_soSocketParam.nReceived == m_soSocketParam.nLen)
                {
                    if (m_soSocketParam.stHeader.FIN == 1)
                    {
                        int iHeaderLen = 2;
                        if (m_soSocketParam.nLen > 125)
                            iHeaderLen += 2;
                        if (m_soSocketParam.nLen > 65535)
                            iHeaderLen += 6;

                        OutputDebugStringA(std::string(std::string(reinterpret_cast<char*>(szData), m_soSocketParam.nLen) + "\r\n").c_str());

                        if (pTextDataReceived != nullptr)
                            pTextDataReceived(pTcpSocket, m_soSocketParam.strPath.c_str(), szData, static_cast<uint32_t>(m_soSocketParam.nLen));
                        else
                            TextDataReceived(pTcpSocket, m_soSocketParam.strPath, szData, static_cast<uint32_t>(m_soSocketParam.nLen));
                    }

                    m_soSocketParam.stHeader = { 0, 0, 0, 0, 0, 0, 0 };
                    m_soSocketParam.nReceived = m_soSocketParam.nLen = 0;
                }
            }
            break;

            case 2: // binary
#if defined(_WIN32) || defined(_WIN64)
                //OutputDebugString(L"binary frame\r\n");
#endif
                BinaryDataReceived(pTcpSocket, m_soSocketParam.strPath, szData, static_cast<uint32_t>(nBlockSize), m_soSocketParam.stHeader.FIN == 1 ? true : false);

                if (m_soSocketParam.nReceived == m_soSocketParam.nLen)
                {
                    m_soSocketParam.stHeader = { 0, 0, 0, 0, 0, 0, 0 };
                    m_soSocketParam.nReceived = m_soSocketParam.nLen = 0;
                }
                break;

            case 8: // close
            {
                short sCode;
                if (m_soSocketParam.nLen >= 2)
                    sCode = ntohs(*(reinterpret_cast<short*>(szData)));
                szData += 2;

                std::shared_ptr<uint8_t[]> spOutput(new uint8_t[m_soSocketParam.nLen + 2]);

                HEADER* sHeader = reinterpret_cast<HEADER*>(spOutput.get());
                *sHeader = { 0, 0, 0, 0, 0, 0, 0 };
                sHeader->FIN = 1;
                sHeader->OpCode = 8;
                sHeader->Mask = 0;
                sHeader->PLoad = m_soSocketParam.nLen;

                if (m_soSocketParam.nLen >= 2)
                    *(reinterpret_cast<short*>(spOutput.get() + 2)) = htons(sCode);
                if (m_soSocketParam.nLen > 2)
                    std::copy(szData, szData + m_soSocketParam.nLen - 2, spOutput.get() + 4);
                pTcpSocket->Write(spOutput.get(), m_soSocketParam.nLen + 2);
            }
            pTcpSocket->Close();
            break;

            case 9: // ping
                reinterpret_cast<HEADER*>(pBuffer)->OpCode = 0xA;
                pTcpSocket->Write(pBuffer, nDataLen);
                m_soSocketParam.nReceived = m_soSocketParam.nLen = 0;
                break;

            case 10:// pong
                PongReceived(pTcpSocket);

                m_soSocketParam.nReceived = m_soSocketParam.nLen = 0;
                break;
            }

            pBuffer += nBlockSize + iOffset;
            nDataLen -= static_cast<uint32_t>(nBlockSize + iOffset);
        } while (pBuffer < pBufferEnd);
    }
}

size_t WebSocket::WriteData(void* pId, const uint8_t* szData, uint32_t nDataLen)
{
    TcpSocket* pTcpSocket = reinterpret_cast<TcpSocket*>(pId);

    uint32_t iHeaderLen = 2;
    if (nDataLen > 125)
        iHeaderLen += 2;
    if (nDataLen > 65535)
        iHeaderLen += 6;

    std::unique_ptr<uint8_t[]> spOutput(new uint8_t[nDataLen + iHeaderLen]);
    HEADER* sHeader = reinterpret_cast<HEADER*>(spOutput.get());
    *sHeader = { 0, 0, 0, 0, 0, 0, 0 };
    sHeader->FIN = 1;
    sHeader->OpCode = 1;
    sHeader->Mask = 0;
    sHeader->PLoad = (iHeaderLen > 2 ? (iHeaderLen == 10 ? 127 : 126) : nDataLen);
    if (iHeaderLen == 10)
        *(reinterpret_cast<uint64_t*>(spOutput.get() + 2)) = htonll(static_cast<uint64_t>(nDataLen));
    else if (iHeaderLen > 2)
        *(reinterpret_cast<short*>(spOutput.get() + 2)) = htons(static_cast<short>(nDataLen));
    std::copy(szData, szData + nDataLen, &spOutput.get()[iHeaderLen]);

    return pTcpSocket->Write(spOutput.get(), nDataLen + iHeaderLen);
}

size_t WebSocket::SendPing(TcpSocket* pTcpSocket)
{
    uint32_t iHeaderLen = 2;

    std::unique_ptr<uint8_t[]> spOutput(new uint8_t[iHeaderLen]);
    HEADER* sHeader = reinterpret_cast<HEADER*>(spOutput.get());
    *sHeader = { 0, 0, 0, 0, 0, 0, 0 };
    sHeader->FIN = 1;
    sHeader->OpCode = 9;
    sHeader->Mask = 0;
    sHeader->PLoad = 0;

    return pTcpSocket->Write(spOutput.get(), iHeaderLen);
}
