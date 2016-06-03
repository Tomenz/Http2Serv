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

#include <algorithm>
#include <map>
#include "HPack.h"
#include "TempFile.h"

typedef tuple<uint32_t, int32_t, uint32_t> STREAMSETTINGS;
#define MAXSTREAMCOUNT(x) get<0>(x)
#define INITWINDOWSIZE(x) get<1>(x)
#define MAXFRAMESIZE(x) get<2>(x)

typedef tuple<shared_ptr<char>, size_t> DATAITEM;
#define BUFFER(x) get<0>(x)
#define BUFLEN(x) get<1>(x)

typedef tuple<uint32_t, deque<DATAITEM>, HEADERLIST, size_t, size_t, shared_ptr<atomic<int32_t>>> STREAMITEM;
#define STREAMSTATE(x) get<0>(x)
#define DATALIST(x) get<1>(x)
#define GETHEADERLIST(x) get<2>(x)
#define CONTENTLENGTH(x) get<3>(x)
#define CONTENTRESCIV(x) get<4>(x)
#define WINDOWSIZE(x) get<5>(x)

typedef map<unsigned long, STREAMITEM> STREAMLIST;

//typedef tuple<string, short, string, short, bool, function<uint32_t(const void*, uint32_t)>, function<void()>, function<uint32_t()>> MetaSocketData;
//#define SocketWrite(x) get<5>(x)
//#define SocketClose(x) get<6>(x)
//#define FnSockGetOutBytesInQue(x) get<7>(x)
typedef struct
{
    string strIpClient;
    short sPortClient;
    string strIpInterface;
    short sPortInterFace;
    bool bIsSsl;
    function<uint32_t(const void*, uint32_t)> fSocketWrite;
    function<void()> fSocketClose;
    function<uint32_t()> fSockGetOutBytesInQue;
    function<void()> fResetTimer;
}MetaSocketData;

class Http2Protocol : public HPack
{
public:
    Http2Protocol() {}
    virtual ~Http2Protocol() {}

    void Http2WindowUpdate(function<uint32_t(const void*, uint32_t)> Write, unsigned long ulStreamID, unsigned long ulStreamSize)
    {
        char caBuffer[20];
        unsigned long ulSizeStream = htonl(ulStreamSize);
        BuildHttp2Frame(caBuffer, 4, 8, 0, ulStreamID); // 8 = WINDOW_UPDATE
        ::memcpy(&caBuffer[9], &ulSizeStream, 4);
        Write(caBuffer, 9 + 4);
    }

    void Http2StreamError(function<uint32_t(const void*, uint32_t)> Write, unsigned long ulStreamID, unsigned long ulErrorCode)
    {
        char caBuffer[20];
        unsigned long ulErrCode = htonl(ulErrorCode);
        BuildHttp2Frame(caBuffer, 4, 3, 0, ulStreamID); // 3 = RST_STREAM
        ::memcpy(&caBuffer[9], &ulErrCode, 4);
        Write(caBuffer, 9 + 4);

        MyTrace("HTTP/2 Error, Code: ", ulErrorCode, ", StreamID = 0x", hex, ulStreamID);
    }

    void Http2Goaway(function<uint32_t(const void*, uint32_t)> Write, unsigned long ulStreamID, unsigned long ulLastStreamID, unsigned long ulErrorCode)
    {
        char caBuffer[20];
        unsigned long ulLastStream = htonl(ulLastStreamID);
        unsigned long ulErrCode = htonl(ulErrorCode);
        BuildHttp2Frame(caBuffer, 8, 7, 0, ulStreamID); // GOAWAY frame (7) streamID = 0, LastStreamId, error
        ::memcpy(&caBuffer[9], &ulLastStream, 4);
        ::memcpy(&caBuffer[13], &ulErrCode, 4);
        Write(caBuffer, 9 + 8);
    }

    size_t Http2StreamProto(MetaSocketData soMetaDa, char* szBuf, size_t& nLen, deque<HEADERENTRY>& qDynTable, STREAMSETTINGS& tuStreamSettings, STREAMLIST& umStreamCache, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop)
    {
        size_t nReturn = 0;
        char* szBufStart = szBuf;

        while (nLen >= 9)   // Settings ACK Frame is 9 Bytes long
        {
            H2FRAME h2f = { 0 };
            ::memcpy(((char*)&h2f.size) + 1, szBuf, 9);
            h2f.size = ::ntohl(h2f.size) & 0x00ffffff;
            h2f.streamId = ::ntohl(h2f.streamId);
            h2f.R = (h2f.streamId & 0x80000000) == 0x80000000 ? true : false;
            h2f.streamId &= 0x7fffffff;

            if (h2f.size > nLen - 9)
            {   // We have less Data in the buffer than the next frame needs to be processed, we put it back into the revive Que
                ::memmove(szBufStart, szBuf, nLen);
                return nLen;
            }

            nLen -= 9;
            szBuf += 9;

            pmtxStream->lock();
            auto streamData = umStreamCache.find(h2f.streamId);
            if (streamData == end(umStreamCache) && umStreamCache.size() == 0)  // First call we not have any stream 0 object, so we make it
            {
                umStreamCache.insert(make_pair(0, STREAMITEM(0, deque<DATAITEM>(), HEADERLIST(), 0, 0, make_shared<atomic<int32_t>>(INITWINDOWSIZE(tuStreamSettings)))));
                streamData = umStreamCache.find(h2f.streamId);
            }

            switch (h2f.typ)
            {
            case 0: // DATA frame
                MyTrace("DATA frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    uint8_t PadLen = 0;
                    if ((h2f.flag & 0x8) == 0x8)
                        PadLen = szBuf++[0], h2f.size--, nLen--;

                    if (streamData == end(umStreamCache))
                    {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                        pmtxStream->unlock();
                        Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR, a HEADER should be already there
                        break;
                    }

                    if (pTmpFile.get() == 0)    //if (DATALIST(streamData->second).empty() == true)    // First DATA frame
                    {
                        auto contentLength = GETHEADERLIST(streamData->second).find("content-length");
                        //if (contentLength == end(GETHEADERLIST(streamData->second)))
                        //{   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                        //    Http2StreamError(stSocketData, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR
                        //    umStreamCache.erase(streamData);
                        //    break;
                        //}

                        if (contentLength != end(GETHEADERLIST(streamData->second)))
                        {
                            //stringstream ssTmp(contentLength->second);
                            //ssTmp >> CONTENTLENGTH(streamData->second);
                            CONTENTLENGTH(streamData->second) = stoi(contentLength->second);

                            Http2WindowUpdate(soMetaDa.fSocketWrite, 0, static_cast<unsigned long>(CONTENTLENGTH(streamData->second)));
                            if ((h2f.flag & 0x1) == 0x0)    // No END_STREAM
                                Http2WindowUpdate(soMetaDa.fSocketWrite, h2f.streamId, static_cast<unsigned long>(CONTENTLENGTH(streamData->second)));
                        }

                        pTmpFile = make_shared<TempFile>();
                        pTmpFile.get()->Open();
                    }

                    pTmpFile.get()->Write(szBuf, min(static_cast<size_t>(h2f.size), nLen) - PadLen);
                    CONTENTRESCIV(streamData->second) += min(static_cast<size_t>(h2f.size), nLen);

                    if ((h2f.flag & 0x1) == 0x1)    // END_STREAM
                    {
                        pTmpFile.get()->Close();

                        STREAMSTATE(streamData->second) |= 2;
                        EndOfStreamAction(soMetaDa, h2f.streamId, umStreamCache, tuStreamSettings, pmtxStream, pTmpFile, patStop);
                    }
                }
                pmtxStream->unlock();
                break;
            case 1: // HEADERS frame
                MyTrace("HEADERS frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    uint8_t PadLen = 0;
                    if ((h2f.flag & 0x8) == 0x8)    // PADDED
                        PadLen = szBuf++[0], h2f.size--, nLen--;

                    unsigned long lStremId = 0;
                    bool E = false;
                    uint8_t Weight = 16;
                    if ((h2f.flag & 0x20) == 0x20)  // PRIORITY
                    {
                        ::memcpy(&lStremId, szBuf, 4);
                        lStremId = ::ntohl(lStremId);
                        E = (lStremId & 0x80000000) == 0x80000000 ? true : false;
                        lStremId &= 0x7fffffff;
                        Weight = szBuf[4];
                        szBuf += 5, h2f.size -= 5, nLen -= 5;
                    }
                    MyTrace("    Pad Length = 0x", static_cast<unsigned long>(PadLen), " StreamId = 0x", hex, lStremId, " E = ", E, " Weight = ", dec, static_cast<unsigned long>(Weight));

                    if ((h2f.flag & 0x4) == 0x4)    // END_HEADERS
                    {
                        HEADERLIST lstHeaderFields;

                        if (Http2DecodeHeader(szBuf, h2f.size - PadLen, qDynTable, lstHeaderFields) != 0)
                        {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                            Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR
                            if (streamData != end(umStreamCache))
                                umStreamCache.erase(streamData);
                            pmtxStream->unlock();
                            break;
                        }

                        // The header is finished, we safe it for later, but there must come some DATA frames for this request
                        if (streamData == end(umStreamCache))
                        {
                            auto insert = umStreamCache.insert(make_pair(h2f.streamId, STREAMITEM(0, deque<DATAITEM>(), move(lstHeaderFields), 0, 0, make_shared<atomic<int32_t>>(INITWINDOWSIZE(tuStreamSettings)))));
                            if (insert.second == false)
                            {
                                // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                                Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 2); // 2 = // INTERNAL_ERROR
                                if (streamData != end(umStreamCache))
                                    umStreamCache.erase(streamData);
                                pmtxStream->unlock();
                                break;
                            }
                        }
                        else
                        {
                            GETHEADERLIST(streamData->second) = lstHeaderFields;
                        }

                        if ((h2f.flag & 0x1) == 0x1)    // END_STREAM
                        {
                            STREAMSTATE(umStreamCache.find(h2f.streamId)->second) |= 2;
                            EndOfStreamAction(soMetaDa, h2f.streamId, umStreamCache, tuStreamSettings, pmtxStream, pTmpFile, patStop);
                        }
                    }
                    else
                    {   // Save the Data. The next frame must be a CONTINUATION (9) frame
                        auto insert = umStreamCache.insert(make_pair(h2f.streamId, STREAMITEM(1, deque<DATAITEM>(), HEADERLIST(), 0, 0, make_shared<atomic<int32_t>>(INITWINDOWSIZE(tuStreamSettings)))));
                        if (insert.second == true)
                        {
                            auto data = shared_ptr<char>(new char[h2f.size - PadLen]);
                            ::memcpy(data.get(), szBuf, h2f.size - PadLen);
                            DATALIST(streamData->second).emplace_back(data, (h2f.size - PadLen));
                        }
                    }
                }
                pmtxStream->unlock();
                break;
            case 2: // PRIORITY  frame
                MyTrace("PRIORITY frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    unsigned long lStremId;
                    ::memcpy(&lStremId, szBuf, 4);
                    lStremId = ::ntohl(lStremId);
                    bool E = (lStremId & 0x80000000) == 0x80000000 ? true : false;
                    lStremId &= 0x7fffffff;
                    uint8_t Weight = szBuf[4];
                    MyTrace("    StreamId = 0x", hex, lStremId, " E = ", E, " Weight = ", dec, static_cast<unsigned long>(Weight));
                }
                pmtxStream->unlock();
                break;
            case 3: // RST_STREAM frame
                MyTrace("RST_STREAM frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    unsigned long lError;
                    ::memcpy(&lError, szBuf, 4);
                    lError = ::ntohl(lError);
                    MyTrace("    Error = 0x", hex, lError, "");
                    pTmpFile.reset();
                    if (streamData != end(umStreamCache))
                    {
                        if ((STREAMSTATE(streamData->second) & 2) == 0)
                            umStreamCache.erase(streamData);
                        else
                            STREAMSTATE(streamData->second) |= 4;
                    }
                }
                pmtxStream->unlock();
                break;
            case 4: // SETTINGS frame
                MyTrace("SETTINGS frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                if ((h2f.flag & 0x1) == 0x1)    // ACK
                {
                    pmtxStream->unlock();
                    break;
                }
                else if (h2f.streamId != 0 || (h2f.size % 6) != 0)
                {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                    Http2StreamError(soMetaDa.fSocketWrite, 0/*h2f.streamId*/, 1); // 1 = // PROTOCOL_ERROR
                    umStreamCache.erase(streamData);
                    pmtxStream->unlock();
                    break;
                }
                else
                {
                    unsigned short sIdent;
                    unsigned long lValue;
                    for (size_t n = 0; n < h2f.size; n += 6)
                    {
                        ::memcpy(&sIdent, &szBuf[n], 2);
                        ::memcpy(&lValue, &szBuf[n + 2], 4);
                        sIdent = ntohs(sIdent);
                        lValue = ntohl(lValue);
                        MyTrace("    Ident = 0x", hex, sIdent, " Value = ", dec, lValue, "");
                        switch (sIdent)
                        {
                        case 3: // SETTINGS_MAX_CONCURRENT_STREAMS
                            MAXSTREAMCOUNT(tuStreamSettings) = lValue;
                            break;
                        case 4: // SETTINGS_INITIAL_WINDOW_SIZE
                            if (lValue < 2147483647)
                                INITWINDOWSIZE(tuStreamSettings) = lValue;
                            else
                            {   // Decode error send RST_STREAM with error code: FLOW_CONTROL_ERROR
                                Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 3); // 3 = // FLOW_CONTROL_ERROR
                                umStreamCache.erase(streamData);
                                break;
                            }
                            break;
                        case 5: // SETTINGS_MAX_FRAME_SIZE
                            if (lValue >= 16384  && lValue <= 16777215)
                                MAXFRAMESIZE(tuStreamSettings) = lValue;
                            else
                            {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                                Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR
                                umStreamCache.erase(streamData);
                                break;
                            }
                            break;
                        }
                    }
                    soMetaDa.fSocketWrite("\x0\x0\x0\x4\x1\x0\x0\x0\x0", 9);
                }
                pmtxStream->unlock();
                break;
            case 5: // PUSH_PROMISE frame
                MyTrace("PUSH_PROMISE frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    uint8_t PadLen = 0;
                    if ((h2f.flag & 0x8) == 0x8)
                        PadLen = szBuf++[0], h2f.size--;
                    MyTrace("    Pad Length = 0x", static_cast<unsigned long>(PadLen));
                }
                pmtxStream->unlock();
                break;
            case 6: // PING frame
                MyTrace("PING frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    szBuf -= 9;
                    szBuf[4] |= 0x1;
                    soMetaDa.fSocketWrite(szBuf, 9 + h2f.size);
                    szBuf += 9;
                }
                pmtxStream->unlock();
                break;
            case 7: // GOAWAY frame
                MyTrace("GOAWAY frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    long lLastStrId, lError;
                    ::memcpy(&lLastStrId, szBuf, 4);
                    ::memcpy(&lError, &szBuf[4], 4);
                    lLastStrId = ::ntohl(lLastStrId);
                    lError = ::ntohl(lError);
                    bool R = (lLastStrId & 0x80000000) == 0x80000000 ? true : false;
                    lLastStrId &= 0x7fffffff;
                    MyTrace("    LastStreamId = 0x", hex, lLastStrId, " Error = 0x", lError, " R = ", R);
                    nReturn = SIZE_MAX;

                    // After a GOAWAY we terminate the connection
                    Http2Goaway(soMetaDa.fSocketWrite, 0, umStreamCache.rbegin()->first, 0);  // GOAWAY
                    soMetaDa.fSocketClose();
                }
                pmtxStream->unlock();
                break;
            case 8: // WINDOW_UPDATE frame
                MyTrace("WINDOW_UPDATE frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                {
                    long lValue;
                    ::memcpy(&lValue, szBuf, 4);
                    lValue = ::ntohl(lValue);
                    bool R = (lValue & 0x80000000) == 0x80000000 ? true : false;
                    lValue &= 0x7fffffff;
                    MyTrace("    Value = ", lValue, " R = ", R);
                    if (streamData != end(umStreamCache) && lValue >= 1 && lValue <= 2147483647)
                        (*WINDOWSIZE(streamData->second).get()) += lValue;
                    else
                    {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                        Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR
                        umStreamCache.erase(streamData);
                        break;
                    }
                }
                pmtxStream->unlock();
                break;
            case 9: // CONTINUATION frame
                MyTrace("CONTINUATION frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);

                if (streamData == end(umStreamCache))
                {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                    pmtxStream->unlock();
                    Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR, a HEADER should be already there
                    break;
                }

                if (STREAMSTATE(streamData->second) == 1)    // We had a Header frame before
                {
                    auto data = shared_ptr<char>(new char[h2f.size]);
                    ::memcpy(data.get(), szBuf, h2f.size);
                    DATALIST(streamData->second).emplace_back(data, h2f.size);
                }
                else
                {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                    Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR
                    umStreamCache.erase(streamData);
                    pmtxStream->unlock();
                    break;
                }

                if ((h2f.flag & 0x4) == 0x4)    // END_HEADERS
                {
                    size_t nHeaderLen = 0;
                    for (DATAITEM chunk : DATALIST(streamData->second))
                        nHeaderLen += BUFLEN(chunk);

                    auto ptHeaderBuf = make_unique<char[]>(nHeaderLen);
                    size_t nOffset = 0;
                    for (DATAITEM chunk : DATALIST(streamData->second))
                        ::memcpy(ptHeaderBuf.get() + nOffset, BUFFER(chunk).get(), BUFLEN(chunk)), nOffset += BUFLEN(chunk);

                    if (Http2DecodeHeader(ptHeaderBuf.get(), nHeaderLen, qDynTable, GETHEADERLIST(streamData->second)) != 0)
                    {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                        Http2StreamError(soMetaDa.fSocketWrite, h2f.streamId, 1); // 1 = // PROTOCOL_ERROR
                        umStreamCache.erase(streamData);
                        pmtxStream->unlock();
                        break;
                    }

                    DATALIST(streamData->second).clear();
                    STREAMSTATE(streamData->second) = 0;

                    if ((h2f.flag & 0x1) == 0x1)    // END_STREAM
                    {
                        STREAMSTATE(streamData->second) |= 2;
                        EndOfStreamAction(soMetaDa, h2f.streamId, umStreamCache, tuStreamSettings, pmtxStream, pTmpFile, patStop);
                    }
                }
                pmtxStream->unlock();
                break;
            default:
                MyTrace("Undefined frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
            }

            szBuf += min(static_cast<size_t>(h2f.size), nLen);
            nLen -= min(static_cast<size_t>(h2f.size), nLen);
        }
#ifdef _DEBUG
        if (nLen != 0)
            MyTrace("Protocol Error");
#endif // DEBUG

        return nReturn;
    }

private:
    virtual void EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop) = 0;
};
