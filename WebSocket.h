
#include <unordered_map>
#include <map>
#include <mutex>
#include <functional>

#include "SocketLib/SocketLib.h"

class WebSocket
{
     typedef struct tagHeader
    {
        uint8_t OpCode : 4;
        uint8_t RSV3 : 1;
        uint8_t RSV2 : 1;
        uint8_t RSV1 : 1;
        uint8_t FIN : 1;

        uint8_t PLoad : 7;
        uint8_t Mask : 1;
    }HEADER;

    typedef struct tagSocketParam
    {
        HEADER stHeader{0,0,0,0,0,0,0};
        uint64_t nLen{0};
        uint32_t uiMask{0};
        uint64_t nReceived{0};
        std::string strPath;
    }SOCKETPARAM;

public:
    WebSocket(const std::string& strPath, TcpSocket* pTcpSocket);
    virtual ~WebSocket();

    void OnDataReceivedWebSocket(TcpSocket* pTcpSocket, uint8_t* pData, size_t nDataLen);

    virtual void TextDataReceived(const void* /*pId*/, const std::string /*strPath*/, uint8_t* /*szData*/, uint32_t /*nDataLen*/) { ; }
    virtual void BinaryDataReceived(const void* /*pId*/, const std::string /*strPath*/, uint8_t* /*szData*/, uint32_t /*nDataLen*/, bool /*bIsLast*/) { ; }
    virtual void PongReceived(const void* /*pId*/) { ; }
    size_t WriteData(void* pId, const uint8_t* szData, uint32_t nDataLen);
    size_t SendPing(TcpSocket* pTcpSocket);

    static void staticWriteBack(void* pId, const uint8_t* value, uint32_t nDataLen) {
        if (WriteBackInstance) WriteBackInstance(pId, value, nDataLen);
    }

private:
    SOCKETPARAM m_soSocketParam;
    void* m_LibHandle;
    void (*pSetWriteCallback)(void(*callback)(void* /*pId*/, const uint8_t* /*szData*/, uint32_t /*nDataLen*/), void* /*pId*/);
    void (*pTextDataReceived)(void* /*pId*/, const char* /*strPath*/, uint8_t* /*szData*/, uint32_t /*nDataLen*/);

    static std::function<size_t(void*, const uint8_t*, uint32_t)> WriteBackInstance;
};
