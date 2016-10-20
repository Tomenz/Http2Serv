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

typedef tuple<uint32_t, int32_t, uint32_t, uint32_t> STREAMSETTINGS;
#define MAXSTREAMCOUNT(x) get<0>(x)
#define INITWINDOWSIZE(x) get<1>(x)
#define MAXFRAMESIZE(x) get<2>(x)
#define MAXHEADERSIZE(x) get<3>(x)

typedef tuple<shared_ptr<char>, size_t> DATAITEM;
#define BUFFER(x) get<0>(x)
#define BUFLEN(x) get<1>(x)

#if !defined(_WIN32) && !defined(_WIN64)
typedef atomic<size_t> atomic_size_t;
#endif
typedef tuple<uint32_t, deque<DATAITEM>, HEADERLIST, uint64_t, uint64_t, shared_ptr<atomic_size_t>> STREAMITEM;
#define STREAMSTATE(x) get<0>(x->second)
#define DATALIST(x) get<1>(x->second)
#define GETHEADERLIST(x) get<2>(x->second)
#define CONTENTLENGTH(x) get<3>(x->second)
#define CONTENTRESCIV(x) get<4>(x->second)
#define WINDOWSIZE(x) *get<5>(x->second).get()

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
    function<size_t(const void*, size_t)> fSocketWrite;
    function<void()> fSocketClose;
    function<uint32_t()> fSockGetOutBytesInQue;
    function<void()> fResetTimer;
}MetaSocketData;

class Http2Protocol : public HPack
{
    enum HTTP2FLAGS : int
    {
        END_OF_STREAM = 0x1,
        END_OF_HEADER = 0x4,
        PADDED        = 0x8,
        PRIORITY      = 0x20,
        ACK_SETTINGS  = 0x1
    };

    enum STREAMFLAGS : uint32_t
    {
        HEADER_RECEIVED = 0x1,
        STREAM_END      = 0x2,
        HEADER_END      = 0x4,
        RESET_STREAM    = 0x8
    };

public:
    Http2Protocol() {}
    virtual ~Http2Protocol() {}

    void Http2WindowUpdate(function<size_t(const void*, size_t)> Write, unsigned long ulStreamID, unsigned long ulStreamSize) const noexcept
    {
        char caBuffer[20];
        unsigned long ulSizeStream = htonl(ulStreamSize);
        BuildHttp2Frame(caBuffer, 4, 8, 0, ulStreamID); // 8 = WINDOW_UPDATE
        ::memcpy(&caBuffer[9], &ulSizeStream, 4);
        Write(caBuffer, 9 + 4);
    }

    void Http2StreamError(function<size_t(const void*, size_t)> Write, unsigned long ulStreamID, unsigned long ulErrorCode) const noexcept
    {
        char caBuffer[20];
        unsigned long ulErrCode = htonl(ulErrorCode);
        BuildHttp2Frame(caBuffer, 4, 3, 0, ulStreamID); // 3 = RST_STREAM
        ::memcpy(&caBuffer[9], &ulErrCode, 4);
        Write(caBuffer, 9 + 4);

        MyTrace("HTTP/2 Error, Code: ", ulErrorCode, ", StreamID = 0x", hex, ulStreamID);
    }

    void Http2Goaway(function<size_t(const void*, size_t)> Write, unsigned long ulStreamID, unsigned long ulLastStreamID, unsigned long ulErrorCode) const noexcept
    {
        char caBuffer[20];
        unsigned long ulLastStream = htonl(ulLastStreamID);
        unsigned long ulErrCode = htonl(ulErrorCode);
        BuildHttp2Frame(caBuffer, 8, 7, 0, ulStreamID); // GOAWAY frame (7) streamID = 0, LastStreamId, error
        ::memcpy(&caBuffer[9], &ulLastStream, 4);
        ::memcpy(&caBuffer[13], &ulErrCode, 4);
        Write(caBuffer, 9 + 8);

        if (ulErrorCode != 0)
            MyTrace("HTTP/2 Error, Code: ", ulErrorCode, ", StreamID = 0x", hex, ulStreamID);
    }

    size_t Http2StreamProto(MetaSocketData soMetaDa, char* szBuf, size_t& nLen, deque<HEADERENTRY>& qDynTable, STREAMSETTINGS& tuStreamSettings, STREAMLIST& umStreamCache, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop)
    {
        size_t nReturn = 0;

        try
        {
            char* szBufStart = szBuf;
            map<uint32_t, uint32_t> mapWindUodate;

            while (nLen >= 9)   // Settings ACK Frame is 9 Bytes long
            {
                H2FRAME h2f = { 0 };
                ::memcpy(((char*)&h2f.size) + 1, szBuf, 9);
                h2f.size = ntohl(h2f.size) & 0x00ffffff;
                h2f.streamId = ntohl(h2f.streamId);
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
                if (h2f.streamId > 0 && (h2f.streamId % 2) == 0)    // Stream ID must by a odd number
                    throw H2ProtoException(H2ProtoException::WRONG_STREAM_ID);

                auto streamData = umStreamCache.find(h2f.streamId);
                if (streamData == end(umStreamCache) && umStreamCache.size() == 0)  // First call we not have any stream 0 object, so we make it
                {
                    umStreamCache.insert(make_pair(0, STREAMITEM(0, deque<DATAITEM>(), HEADERLIST(), 0, 0, make_shared<atomic_size_t>(INITWINDOWSIZE(tuStreamSettings)))));
                    streamData = umStreamCache.find(h2f.streamId);
                }

                if (streamData == end(umStreamCache) && (STREAMSTATE(umStreamCache.find(0)) & HEADER_RECEIVED) == HEADER_RECEIVED)
                    throw H2ProtoException(H2ProtoException::HEADER_OTHER_STREAM);  // We had a HEADER before, on a different stream

                switch (h2f.typ)
                {
                case 0: // DATA frame
                    MyTrace("DATA frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        uint8_t PadLen = 0;
                        if ((h2f.flag & PADDED) == PADDED)
                            PadLen = szBuf++[0], h2f.size--, nLen--;

                        if (streamData == end(umStreamCache) || h2f.streamId == 0 || (STREAMSTATE(umStreamCache.find(0)) & HEADER_RECEIVED) == HEADER_RECEIVED)
                            throw H2ProtoException(H2ProtoException::DATA_WITHOUT_STREAM);
                        if ((STREAMSTATE(streamData) & STREAM_END) == STREAM_END)
                            throw H2ProtoException(H2ProtoException::STREAM_HALF_CLOSED, h2f.streamId);
                        if (h2f.size > 16375)
                            throw H2ProtoException(H2ProtoException::MAX_FRAME_SIZE, h2f.streamId);
                        if (h2f.size < PadLen)
                            throw H2ProtoException(H2ProtoException::FRAME_SIZE_VALUE, h2f.streamId);

                        if (pTmpFile.get() == 0)    //if (DATALIST(streamData->second).empty() == true)    // First DATA frame
                        {
                            auto contentLength = GETHEADERLIST(streamData).find("content-length");

                            if (contentLength != end(GETHEADERLIST(streamData)))
                            {
                                CONTENTLENGTH(streamData) = stoull(contentLength->second);

                                Http2WindowUpdate(soMetaDa.fSocketWrite, 0, static_cast<unsigned long>(CONTENTLENGTH(streamData))); // ! if really big data a 32 Bit Number maybe to small
                                //Http2WindowUpdate(soMetaDa.fSocketWrite, h2f.streamId, static_cast<unsigned long>(CONTENTLENGTH(streamData->second)));
                            }

                            pTmpFile = make_shared<TempFile>();
                            pTmpFile.get()->Open();
                        }

                        pTmpFile.get()->Write(szBuf, min(static_cast<size_t>(h2f.size), nLen) - PadLen);
                        CONTENTRESCIV(streamData) += min(static_cast<size_t>(h2f.size), nLen);

                        mapWindUodate[h2f.streamId] += static_cast<size_t>(h2f.size) - PadLen;

                        if ((h2f.flag & END_OF_STREAM) == END_OF_STREAM)    // END_STREAM
                        {
                            pTmpFile.get()->Close();

                            if (CONTENTLENGTH(streamData) > 0 && CONTENTLENGTH(streamData) != CONTENTRESCIV(streamData))
                                throw H2ProtoException(H2ProtoException::DATASIZE_MISSMATCH, h2f.streamId);

                            STREAMSTATE(streamData) |= STREAM_END;
                            EndOfStreamAction(soMetaDa, h2f.streamId, umStreamCache, tuStreamSettings, pmtxStream, pTmpFile, patStop);
                        }
                    }
                    pmtxStream->unlock();
                    break;
                case 1: // HEADERS frame
                    MyTrace("HEADERS frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        if (h2f.streamId == 0)
                            throw H2ProtoException(H2ProtoException::MISSING_STREAMID);

                        uint8_t PadLen = 0;
                        if ((h2f.flag & PADDED) == PADDED)    // PADDED
                            PadLen = szBuf++[0], h2f.size--, nLen--;

                        if (h2f.size < PadLen)
                            throw H2ProtoException(H2ProtoException::WRONG_PAD_LENGTH);

                        unsigned long lStremId = 0;
                        bool E = false;
                        uint8_t Weight = 16;
                        if ((h2f.flag & PRIORITY) == PRIORITY)  // PRIORITY
                        {
                            ::memcpy(&lStremId, szBuf, 4);
                            lStremId = ntohl(lStremId);
                            E = (lStremId & 0x80000000) == 0x80000000 ? true : false;
                            lStremId &= 0x7fffffff;
                            Weight = szBuf[4];
                            szBuf += 5, h2f.size -= 5, nLen -= 5;

                            if (h2f.streamId == lStremId)
                                throw H2ProtoException(H2ProtoException::SAME_STREAMID, h2f.streamId);
                        }
                        MyTrace("    Pad Length = 0x", static_cast<unsigned long>(PadLen), " StreamId = 0x", hex, lStremId, " E = ", E, " Weight = ", dec, static_cast<unsigned long>(Weight));

                        if (streamData != end(umStreamCache) && (STREAMSTATE(streamData) & STREAM_END) == STREAM_END)
                            throw H2ProtoException(H2ProtoException::STREAM_HALF_CLOSED, h2f.streamId);
                        if (streamData != end(umStreamCache) && (STREAMSTATE(streamData) & HEADER_END) == HEADER_END && (h2f.flag & END_OF_STREAM) == 0)
                            throw H2ProtoException(H2ProtoException::HEADER_NO_STREAMEND);

                        if ((h2f.flag & END_OF_HEADER) == END_OF_HEADER)    // END_HEADERS
                        {
                            HEADERLIST lstHeaderFields;
                            if (streamData != end(umStreamCache))
                                lstHeaderFields = GETHEADERLIST(streamData);

                            if (Http2DecodeHeader(szBuf, h2f.size - PadLen, qDynTable, lstHeaderFields) != 0)
                                throw H2ProtoException(H2ProtoException::COMMON_DECODE_ERROR);

                            // The header is finished, we safe it for later, but there must come some DATA frames for this request
                            if (streamData == end(umStreamCache))
                            {
                                auto insert = umStreamCache.insert(make_pair(h2f.streamId, STREAMITEM(HEADER_END, deque<DATAITEM>(), move(lstHeaderFields), 0, 0, make_shared<atomic_size_t>(INITWINDOWSIZE(tuStreamSettings)))));
                                if (insert.second == false)
                                    throw H2ProtoException(H2ProtoException::INERNAL_ERROR, h2f.streamId);
                                else
                                    streamData = insert.first;
                            }
                            else
                            {
                                STREAMSTATE(streamData) |= HEADER_END;
                                GETHEADERLIST(streamData) = lstHeaderFields;
                            }

                            // We mark in Stream 0 that the Header is received
                            auto itStream0 = umStreamCache.find(0);
                            if (itStream0 != end(umStreamCache))
                                STREAMSTATE(itStream0) &= ~HEADER_RECEIVED;

                            if ((h2f.flag & END_OF_STREAM) == END_OF_STREAM)    // END_STREAM
                            {
                                STREAMSTATE(streamData) |= STREAM_END;
                                if (GETHEADERLIST(streamData).find(":scheme") == end(GETHEADERLIST(streamData)))
                                    throw H2ProtoException(H2ProtoException::WRONG_HEADER);
                                EndOfStreamAction(soMetaDa, h2f.streamId, umStreamCache, tuStreamSettings, pmtxStream, pTmpFile, patStop);
                            }
                        }
                        else
                        {   // Save the Data. The next frame must be a CONTINUATION (9) frame
                            auto insert = umStreamCache.insert(make_pair(h2f.streamId, STREAMITEM(HEADER_RECEIVED, deque<DATAITEM>(), HEADERLIST(), 0, 0, make_shared<atomic_size_t>(INITWINDOWSIZE(tuStreamSettings)))));
                            if (insert.second == true)
                            {
                                auto data = shared_ptr<char>(new char[h2f.size - PadLen]);
                                copy(&szBuf[0], &szBuf[h2f.size - PadLen], data.get());
                                DATALIST(insert.first).emplace_back(data, (h2f.size - PadLen));

                                if ((h2f.flag & END_OF_STREAM) == END_OF_STREAM)    // END_STREAM
                                    STREAMSTATE(insert.first) |= STREAM_END;

                                // We mark in Stream 0 that we in the middle of a Header receiving
                                auto itStream0 = umStreamCache.find(0);
                                if (itStream0 != end(umStreamCache))
                                    STREAMSTATE(itStream0) |= HEADER_RECEIVED;
                            }
                        }
                    }
                    pmtxStream->unlock();
                    break;
                case 2: // PRIORITY  frame
                    MyTrace("PRIORITY frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        if (h2f.streamId == 0)
                            throw H2ProtoException(H2ProtoException::MISSING_STREAMID);
                        if (h2f.size != 5)
                            throw H2ProtoException(H2ProtoException::FRAME_SIZE_ERROR);

                        unsigned long lStremId;
                        ::memcpy(&lStremId, szBuf, 4);
                        lStremId = ntohl(lStremId);
                        bool E = (lStremId & 0x80000000) == 0x80000000 ? true : false;
                        lStremId &= 0x7fffffff;
                        uint8_t Weight = szBuf[4];
                        MyTrace("    StreamId = 0x", hex, lStremId, " E = ", E, " Weight = ", dec, static_cast<unsigned long>(Weight));

                        if (h2f.streamId == lStremId)
                            throw H2ProtoException(H2ProtoException::SAME_STREAMID, h2f.streamId);
                    }
                    pmtxStream->unlock();
                    break;
                case 3: // RST_STREAM frame
                    MyTrace("RST_STREAM frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        if (streamData == end(umStreamCache) || h2f.streamId == 0)
                            throw H2ProtoException(H2ProtoException::MISSING_STREAMID);
                        if (h2f.size != 4)
                            throw H2ProtoException(H2ProtoException::FRAME_SIZE_ERROR);

                        unsigned long lError;
                        ::memcpy(&lError, szBuf, 4);
                        lError = ntohl(lError);
                        MyTrace("    Error = 0x", hex, lError, "");
                        pTmpFile.reset();

                        if ((STREAMSTATE(streamData) & STREAM_END) == 0)
                            umStreamCache.erase(streamData);
                        else
                            STREAMSTATE(streamData) |= RESET_STREAM;
                    }
                    pmtxStream->unlock();
                    break;
                case 4: // SETTINGS frame
                    MyTrace("SETTINGS frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        if ((h2f.flag & ACK_SETTINGS) == ACK_SETTINGS)    // ACK
                        {
                            if (h2f.size != 0)
                                throw H2ProtoException(H2ProtoException::FRAME_SIZE_ERROR);
                            pmtxStream->unlock();
                            break;
                        }

                        if (h2f.streamId != 0)
                            throw H2ProtoException(H2ProtoException::STREAMID_MUST_NULL);
                        if ((h2f.size % 6) != 0)
                            throw H2ProtoException(H2ProtoException::FRAME_SIZE_ERROR);

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
                            case 1: //  SETTINGS_HEADER_TABLE_SIZE
                                break;
                            case 2: //  SETTINGS_ENABLE_PUSH
                                if (lValue > 1)
                                    throw H2ProtoException(H2ProtoException::INVALID_PUSH_SET);
                                break;
                            case 3: // SETTINGS_MAX_CONCURRENT_STREAMS
                                MAXSTREAMCOUNT(tuStreamSettings) = lValue;
                                break;
                            case 4: // SETTINGS_INITIAL_WINDOW_SIZE
                                if (lValue < 2147483647)
                                    INITWINDOWSIZE(tuStreamSettings) = lValue;
                                else
                                    throw H2ProtoException(H2ProtoException::WINDOW_SIZE_SETTING);
                                break;
                            case 5: // SETTINGS_MAX_FRAME_SIZE
                                if (lValue >= 16384 && lValue <= 16777215)
                                    MAXFRAMESIZE(tuStreamSettings) = lValue;
                                else
                                    throw H2ProtoException(H2ProtoException::MAX_FRAME_SIZE_SET);
                                break;
                            case 6: // SETTINGS_MAX_HEADER_LIST_SIZE
                                MAXHEADERSIZE(tuStreamSettings) = lValue;
                                break;
                            }
                        }
                        soMetaDa.fSocketWrite("\x0\x0\x0\x4\x1\x0\x0\x0\x0", 9);    // ACK SETTINGS
                    }
                    pmtxStream->unlock();
                    break;
                case 5: // PUSH_PROMISE frame
                    MyTrace("PUSH_PROMISE frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        uint8_t PadLen = 0;
                        if ((h2f.flag & PADDED) == PADDED)
                            PadLen = szBuf++[0], h2f.size--;
                        MyTrace("    Pad Length = 0x", static_cast<unsigned long>(PadLen));

                        Http2Goaway(soMetaDa.fSocketWrite, 0, 0, 1);    // 1 = PROTOCOL_ERROR
                        nReturn = SIZE_MAX;
                    }
                    pmtxStream->unlock();
                    break;
                case 6: // PING frame
                    MyTrace("PING frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        if (h2f.streamId != 0)
                            throw H2ProtoException(H2ProtoException::STREAMID_MUST_NULL);
                        if (h2f.size != 8)
                            throw H2ProtoException(H2ProtoException::FRAME_SIZE_ERROR);

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
                        if (h2f.streamId != 0)
                            throw H2ProtoException(H2ProtoException::STREAMID_MUST_NULL);

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
                        if (h2f.size != 4)
                            throw H2ProtoException(H2ProtoException::FRAME_SIZE_ERROR);

                        long lValue;
                        ::memcpy(&lValue, szBuf, 4);
                        lValue = ntohl(lValue);
                        bool R = (lValue & 0x80000000) == 0x80000000 ? true : false;
                        lValue &= 0x7fffffff;
                        MyTrace("    Value = ", lValue, " R = ", R);
                        if (streamData != end(umStreamCache) && lValue >= 1 && lValue <= 2147483647)    // 2^31 - 1
                        {
                            if ((WINDOWSIZE(streamData) + lValue) > 2147483647)  // 2^31 - 1
                                throw H2ProtoException(H2ProtoException::WINDOW_SIZE_TO_HIGH);
                            WINDOWSIZE(streamData) += lValue;
                        }
                        else if (streamData == end(umStreamCache))
                            throw H2ProtoException(H2ProtoException::MISSING_STREAMID);
                        else
                        {   // Decode error send RST_STREAM with error code: PROTOCOL_ERROR
                            if (lValue == 0 || lValue > 2147483647)
                                throw H2ProtoException(H2ProtoException::INVALID_WINDOW_SIZE);
                            umStreamCache.erase(streamData);
                        }
                    }
                    pmtxStream->unlock();
                    break;
                case 9: // CONTINUATION frame
                    MyTrace("CONTINUATION frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    {
                        if (streamData == end(umStreamCache) || h2f.streamId == 0)
                            throw H2ProtoException(H2ProtoException::MISSING_STREAMID);
                        if ((STREAMSTATE(streamData) & HEADER_RECEIVED) == 0)    // We had a Header frame before
                            throw H2ProtoException(H2ProtoException::CONT_WITHOUT_HEADER);

                        auto data = shared_ptr<char>(new char[h2f.size]);
                        copy(&szBuf[0], &szBuf[h2f.size], data.get());
                        DATALIST(streamData).emplace_back(data, h2f.size);

                        if ((h2f.flag & END_OF_HEADER) == END_OF_HEADER)    // END_HEADERS
                        {
                            size_t nHeaderLen = 0;
                            for (DATAITEM chunk : DATALIST(streamData))
                                nHeaderLen += BUFLEN(chunk);

                            auto ptHeaderBuf = make_unique<char[]>(nHeaderLen);
                            size_t nOffset = 0;
                            for (DATAITEM chunk : DATALIST(streamData))
                                copy(BUFFER(chunk).get(), &BUFFER(chunk).get()[BUFLEN(chunk)], &ptHeaderBuf.get()[nOffset]), nOffset += BUFLEN(chunk); // ::memcpy(ptHeaderBuf.get() + nOffset, BUFFER(chunk).get(), BUFLEN(chunk)), nOffset += BUFLEN(chunk);

                            if (Http2DecodeHeader(ptHeaderBuf.get(), nHeaderLen, qDynTable, GETHEADERLIST(streamData)) != 0)
                                throw H2ProtoException(H2ProtoException::COMMON_DECODE_ERROR);

                            DATALIST(streamData).clear();

                            // We mark in Stream 0 that the Header is received
                            auto itStream0 = umStreamCache.find(0);
                            if (itStream0 != end(umStreamCache))
                                STREAMSTATE(itStream0) &= ~HEADER_RECEIVED;

                            STREAMSTATE(streamData) |= (h2f.flag & END_OF_STREAM) == END_OF_STREAM ? STREAM_END : 0; // END_STREAM
                            STREAMSTATE(streamData) |= HEADER_END;

                            if ((STREAMSTATE(streamData) & STREAM_END) == STREAM_END)
                                EndOfStreamAction(soMetaDa, h2f.streamId, umStreamCache, tuStreamSettings, pmtxStream, pTmpFile, patStop);
                        }
                    }
                    pmtxStream->unlock();
                    break;
                default:
                    MyTrace("Undefined frame with ", h2f.size, " Bytes. StreamID = 0x", hex, h2f.streamId, " and Flag = 0x", h2f.flag);
                    auto itStream0 = umStreamCache.find(0);
                    if (itStream0 != end(umStreamCache) && (STREAMSTATE(itStream0) & HEADER_RECEIVED) == HEADER_RECEIVED)
                        throw H2ProtoException(H2ProtoException::UNDEF_AFTER_HEADER);
                    pmtxStream->unlock();
                }

                szBuf += min(static_cast<size_t>(h2f.size), nLen);
                nLen -= min(static_cast<size_t>(h2f.size), nLen);
            }
#ifdef _DEBUG
            if (nLen != 0)
                MyTrace("Protocol Error");
#endif // DEBUG

            for (auto item : mapWindUodate)
                Http2WindowUpdate(soMetaDa.fSocketWrite, item.first, item.second);
        }

        catch (H2ProtoException& ex)
        {
            pmtxStream->unlock();
            nReturn = SIZE_MAX;

            switch (ex.GetCode())
            {
            case H2ProtoException::BUFFSIZE_ERROR:
            case H2ProtoException::DYNTABLE_UPDATE:
            case H2ProtoException::COMMON_DECODE_ERROR:
                Http2Goaway(soMetaDa.fSocketWrite, 0, 0, 9);    // 9 = COMPRESSION_ERROR
                break;
            case H2ProtoException::FRAME_SIZE_ERROR:
                Http2Goaway(soMetaDa.fSocketWrite, 0, 0, 6);   // 6 = FRAME_SIZE_ERROR
                break;
            case H2ProtoException::WINDOW_SIZE_SETTING:
            case H2ProtoException::WINDOW_SIZE_TO_HIGH:
                Http2Goaway(soMetaDa.fSocketWrite, 0, 0, 3);    // 3 = FLOW_CONTROL_ERROR
                break;
            case H2ProtoException::DOUBLE_HEADER:
            case H2ProtoException::UPPERCASE_HEADER:
            case H2ProtoException::WRONG_HEADER:
            case H2ProtoException::FALSE_PSEUDO_HEADER:
            case H2ProtoException::INVALID_HEADER:
            case H2ProtoException::INVALID_TE_HEADER:
            case H2ProtoException::WRONG_STREAM_ID:     // Stream ID is not a odd number
            case H2ProtoException::HEADER_OTHER_STREAM: // Header on other stream without Header-End Flag
            case H2ProtoException::DATA_WITHOUT_STREAM:
            case H2ProtoException::MISSING_STREAMID:
            case H2ProtoException::WRONG_PAD_LENGTH:
            case H2ProtoException::HEADER_NO_STREAMEND:
            case H2ProtoException::CONT_WITHOUT_HEADER:
            case H2ProtoException::UNDEF_AFTER_HEADER:
            case H2ProtoException::STREAMID_MUST_NULL:
            case H2ProtoException::INVALID_PUSH_SET:
            case H2ProtoException::MAX_FRAME_SIZE_SET:
            case H2ProtoException::INVALID_WINDOW_SIZE:
                Http2Goaway(soMetaDa.fSocketWrite, 0, 0, 1);    // 1 = PROTOCOL_ERROR
                break;
            case H2ProtoException::FRAME_SIZE_VALUE:
            case H2ProtoException::SAME_STREAMID:
            case H2ProtoException::DATASIZE_MISSMATCH:
                Http2StreamError(soMetaDa.fSocketWrite, ex.GetStreamId(), 1);   // 1 = PROTOCOL_ERROR
                nReturn = 0;
                break;
            case H2ProtoException::STREAM_HALF_CLOSED:
                Http2StreamError(soMetaDa.fSocketWrite, ex.GetStreamId(), 5);   // 5 = STREAM_CLOSED
                nReturn = 0;
                break;
            case H2ProtoException::MAX_FRAME_SIZE:
                Http2StreamError(soMetaDa.fSocketWrite, ex.GetStreamId(), 6);   // 6 = FRAME_SIZE_ERROR
                nReturn = 0;
                break;
            case H2ProtoException::INERNAL_ERROR:
                Http2StreamError(soMetaDa.fSocketWrite, ex.GetStreamId(), 2);   // 2 = INTERNAL_ERROR
                nReturn = 0;
                break;
            }
        }

        return nReturn;
    }

private:
    virtual void EndOfStreamAction(MetaSocketData soMetaDa, uint32_t streamId, STREAMLIST& StreamList, STREAMSETTINGS& tuStreamSettings, mutex* pmtxStream, shared_ptr<TempFile>& pTmpFile, atomic<bool>* patStop) = 0;
};
