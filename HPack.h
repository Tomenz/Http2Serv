/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#pragma once

#include <array>
#include <tuple>
#include <vector>
#include <deque>

#include "Trace.h"

using namespace std;

typedef tuple<uint32_t, int32_t, uint32_t, uint32_t, uint32_t> STREAMSETTINGS;
#define MAXSTREAMCOUNT(x) get<0>(x)
#define INITWINDOWSIZE(x) get<1>(x)
#define MAXFRAMESIZE(x) get<2>(x)
#define MAXHEADERSIZE(x) get<3>(x)
#define HEADER_TABLE_SIZE(x) get<4>(x)

typedef tuple<uint8_t, string, string> HEADERENTRY;
#define HEADERINDEX(x) get<0>(x)
#define HEADERNAME(x) get<1>(x)
#define HEADERVALUE(x) get<2>(x)

class HeadList : public vector<pair<string, string>>
{
public:
    HeadList() : vector<pair<string, string>>() {}
    explicit HeadList(const vector<pair<string, string>>& v) : vector<pair<string, string>>(v) {}
    HeadList::iterator find(const string& strSearch)
    {   // Search strings as of now are always lower case
        return find_if(begin(), end(), [&](auto item) noexcept { return strSearch == item.first ? true : false; });
    }
    HeadList::iterator ifind(string strSearch)
    {
        transform(strSearch.begin(), strSearch.end(), strSearch.begin(), [](char c) noexcept { return static_cast<char>(::tolower(c)); });
        return find_if(begin(), end(), [&](auto item) { string strTmp(item.first.size(), 0);  transform(item.first.begin(), item.first.end(), strTmp.begin(), [](char c) noexcept { return static_cast<char>(::tolower(c)); });  return strSearch == strTmp ? true : false; });
    }
};

class HPack
{
public:
    HPack();

    string HufmanDecode(const char* szBuf, size_t nLen, int& iError) const noexcept;
    uint32_t DecodeInteger(const char** pszBuf, size_t* const pnLen) const;
    string DecodeString(const char** pszBuf, size_t* const pnLen) const;
    size_t DecodeIndex(const char** pszBuf, size_t* const pnLen, uint32_t nBitMask) const;
    size_t HPackDecode(const char* szBuf, size_t nLen, deque<HEADERENTRY>& qDynTable, string& strHeaderName, string& strHeaderValue, uint32_t& nHeaderCount, STREAMSETTINGS& tuStreamSettings) const;
    size_t HufmanEncode(uint8_t* szBuf, size_t nBufSize, const char* const szString, size_t nStrLen) const noexcept;
    size_t EncodeInteger(uint8_t* const szBuf, size_t nBufSize, size_t nIndex, uint8_t nBitMask, uint8_t nRepBits) const noexcept;
    size_t EncodeString(uint8_t* const szBuf, size_t nBufSize, const char* szString, size_t nStrLen) const noexcept;
    size_t HPackEncode(uint8_t* const szBuf, size_t nLen, const char* const strHeaderId, const char* const strHeaderValue) const noexcept;
    size_t Http2DecodeHeader(const char* szHeaderStart, size_t nHeaderLen, deque<HEADERENTRY>& qDynTable, HeadList& lstHeaderFields, STREAMSETTINGS& tuStreamSettings) const;

    class H2ProtoException : public exception
    {
    public:
        enum HPACKEXCODE : uint32_t
        {
            PROTOCOL_ERROR      = 1,
            INTERNAL_ERROR      = 2,
            FLOW_CONTROL_ERROR  = 3,
            UPPERCASE_HEADER    = 4,
            STREAM_CLOSED       = 5,
            FALSE_PSEUDO_HEADER = 6,
            INVALID_HEADER      = 7,
            INVALID_TE_HEADER   = 8,
            COMPRESSION_ERROR   = 9,
            HEADER_OTHER_STREAM = 10,
            DATA_WITHOUT_STREAM = 11,
            STREAM_HALF_CLOSED  = 12,
            MAX_FRAME_SIZE      = 13,
            FRAME_SIZE_VALUE    = 14,
            MISSING_STREAMID    = 15,
            WRONG_PAD_LENGTH    = 16,
            SAME_STREAMID       = 17,
            COMMON_DECODE_ERROR = 18,
            HEADER_NO_STREAMEND = 19,
            CONT_WITHOUT_HEADER = 20,
            UNDEF_AFTER_HEADER  = 21,
            FRAME_SIZE_ERROR    = 22,
            STREAMID_MUST_NULL  = 23,
            DATASIZE_MISSMATCH  = 24,
            INVALID_PUSH_SET    = 25,
            WINDOW_SIZE_SETTING = 26,
            MAX_FRAME_SIZE_SET  = 27,
            WINDOW_SIZE_TO_HIGH = 28,
            INVALID_WINDOW_SIZE = 29,
            BUFFSIZE_ERROR      = 31,
            DYNTABLE_UPDATE     = 32,
            WRONG_STREAM_ID     = 33,
            WRONG_HEADER        = 34
        };

        explicit H2ProtoException(HPACKEXCODE eCode) : m_eCode(eCode), m_nStreamId(0) {}
        explicit H2ProtoException(HPACKEXCODE eCode, uint32_t nStreamId) : m_eCode(eCode), m_nStreamId(nStreamId) {}
        ~H2ProtoException() noexcept {}
        const char* what() const throw() {
            return m_strError.c_str();
        }
        const HPACKEXCODE GetCode() noexcept { return m_eCode; }
        const uint32_t GetStreamId() noexcept { return m_nStreamId; }

    private:
        string      m_strError;
        HPACKEXCODE m_eCode;
        uint32_t    m_nStreamId;
    };

protected:
        typedef struct h2Frame
        {
            unsigned long size;
            uint8_t typ;
            uint8_t flag;
            uint32_t streamId;
            bool R;
        }H2FRAME;

private:
    static const vector<pair<uint32_t, uint32_t>> HUFFCODES;
    static const uint8_t SYMTABLE[95];
    static const array<HEADERENTRY, 61>  StaticHeaderListe;
};

