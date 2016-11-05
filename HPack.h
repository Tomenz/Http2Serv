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

#include <array>
#include <tuple>
#include <vector>

#include "Trace.h"

using namespace std;

#if !defined (_WIN32) && !defined (_WIN64)
#include <arpa/inet.h>
#define _stricmp strcasecmp
#endif

typedef tuple<uint8_t, string, string> HEADERENTRY;
#define HEADERINDEX(x) get<0>(x)
#define HEADERNAME(x) get<1>(x)
#define HEADERVALUE(x) get<2>(x)

class HeadList : public vector<pair<string, string>>
{
public:
    HeadList() : vector<pair<string, string>>() {}
    HeadList(pair<string, string> p) : vector<pair<string, string>>()
    {
        push_back(p);
    }
    HeadList::iterator find(string strSearch)
    {   // Search strings as of now are always lower case
        return find_if(begin(), end(), [&](auto item) { return strSearch == item.first ? true : false; });
    }
    HeadList::iterator ifind(string strSearch)
    {
        transform(strSearch.begin(), strSearch.end(), strSearch.begin(), ::tolower);
        return find_if(begin(), end(), [&](auto item) { string strTmp(item.first.size(), 0);  transform(item.first.begin(), item.first.end(), strTmp.begin(), ::tolower);  return strSearch == strTmp ? true : false; });
    }
};

class HPack
{
public:

    string HufmanDecode(const char* szBuf, size_t nLen) const noexcept
    {
        string strRet;
        size_t TotalBits = 0;

        while (nLen * 8 >= TotalBits + 5 && nLen > 0)
        {
            uint8_t b1 = (uint8_t)*szBuf << (TotalBits % 8) | ((TotalBits % 8) > 0 && nLen > 1 ? ((uint8_t)*(szBuf + 1)) >> (8 - (TotalBits % 8)) : 0);
            uint8_t b2 = 0xfd, b3 = 0xfd;
            if (b1 >= 0xFE && nLen > 1) // More than 8 Bit
                b2 = (uint8_t)*(szBuf + 1) << (TotalBits % 8) | ((TotalBits % 8) > 0 ? ((uint8_t)*(szBuf + 2)) >> (8 - (TotalBits % 8)) : 0);
            if (b2 >= 0xFE && nLen > 2) // More than 16 Bit
                b3 = (uint8_t)*(szBuf + 2) << (TotalBits % 8) | ((TotalBits % 8) > 0 ? ((uint8_t)*(szBuf + 3)) >> (8 - (TotalBits % 8)) : 0);

            int iCode = -1;

            if (b1 < 0x50)  // 5 Bit
                iCode = (b1 >> 3), TotalBits += 5;
            else if (b1 < 0xB8)  // 6 Bit
                iCode = (b1 >> 2) - 0xa, TotalBits += 6;
            else if (b1 < 0xF8)  // 7 Bit
                iCode = (b1 >> 1) - 0x38, TotalBits += 7;
            else if (b1 < 0xFE)  // 8 Bit
                iCode = b1 - 0xB4, TotalBits += 8;
            else if (b1 < 0xFF || b2 < 0x40)  // 10 Bit
                iCode = ((b1 << 2) | (b2 >> 6)) - 0x3AE, TotalBits += 10;
            else if (/*b1 == 0xFF &&*/ b2 < 0xA0)  // 11 Bit
                iCode = ((b1 << 3) | (b2 >> 5)) - 0x7AB, TotalBits += 11;
            else if (/*b1 == 0xFF &&*/ b2 < 0xC8)  // 12 Bit
                iCode = ((b1 << 4) | (b2 >> 4)) - 0xFA8, TotalBits += 12;
            else if (/*b1 == 0xFF &&*/ b2 < 0xF0)  // 13 Bit
                iCode = ((b1 << 5) | (b2 >> 3)) - 0x1FA5, TotalBits += 13;
            else if (/*b1 == 0xFF &&*/ b2 < 0xF8)  // 14 Bit
                iCode = ((b1 << 6) | (b2 >> 2)) - 0x3FA3, TotalBits += 14;
            else if (/*b1 == 0xFF &&*/ b2 < 0xFE)  // 15 Bit
                iCode = ((b1 << 7) | (b2 >> 1)) - 0x7FA1, TotalBits += 15;
            else if (/*b1 == 0xFF &&*/ b2 < 0xFF && b3 < 0x20)  // 19 Bit
                iCode = ((b1 << 11) | (b2 << 3) | (b3 >> 5)) - 0x7FF92, TotalBits += 19;
            else
                break;

            if (nLen * 8 < TotalBits)    // Das letzte Byte ist über unserer Länge des Bitstreams
                break;

            if (iCode != -1 && iCode < static_cast<int>(sizeof(SYMTABLE)))
                strRet += SYMTABLE[iCode];

            size_t AnzByt = TotalBits / 8;
            if (AnzByt != 0)
            {
                TotalBits -= 8 * AnzByt;
                szBuf += AnzByt;
                nLen -= AnzByt;
            }
        }
        return strRet;
    }

    uint32_t DecodeInteger(const char** pszBuf, size_t* const pnLen) const
    {
        uint32_t nRet = 0;
        uint32_t nBitCount = 0; // M = 0
        uint8_t b;
        do
        {
            if (*pnLen == 0)
                throw H2ProtoException(H2ProtoException::BUFFSIZE_ERROR);
            b = *((*pszBuf)++), (*pnLen)--; // B = next octet
            nRet += (b & 0x7f) * (1 << nBitCount); // I = I + (B & 127) * 2 ^ M
            nBitCount += 7; // M = M + 7
        } while ((b & 0x80) == 0x80); //while B & 128 == 128

        return nRet;
    }

    string DecodeString(const char** pszBuf, size_t* const pnLen) const
    {
        if (*pnLen == 0)
            throw H2ProtoException(H2ProtoException::BUFFSIZE_ERROR);

        bool bEncoded = (*(*pszBuf) & 0x80) == 0x80 ? true : false;
        size_t nStrLen = (*((*pszBuf)++) & 0x7f);
        (*pnLen)--;

        if ((nStrLen & 0x7f) == 0x7f)    // 7 Bit prefix
            nStrLen += DecodeInteger(pszBuf, pnLen);// , ::OutputDebugString(L"Integer decoden\r\n");

        if (*pnLen < nStrLen)
            throw H2ProtoException(H2ProtoException::BUFFSIZE_ERROR);

        string strReturn = bEncoded == true ? HufmanDecode(*pszBuf, nStrLen) : string(*pszBuf, nStrLen);
        (*pnLen) -= nStrLen, (*pszBuf) += nStrLen;

        return strReturn;
    }

    size_t DecodeIndex(const char** pszBuf, size_t* const pnLen, uint32_t nBitMask) const
    {
        if (*pnLen == 0)
            throw H2ProtoException(H2ProtoException::BUFFSIZE_ERROR);

        size_t iIndex = *((*pszBuf)++) & nBitMask;
        (*pnLen)--;

        if ((iIndex & nBitMask) == nBitMask)
            iIndex += DecodeInteger(pszBuf, pnLen);// , ::OutputDebugString(L"Integer decoden\r\n");

        return iIndex;
    }

    size_t HPackDecode(const char* szBuf, size_t nLen, deque<HEADERENTRY>& qDynTable, string& strHeaderName, string& strHeaderValue, uint32_t& nHeaderCount) const
    {
        //basic_fstream<char> fout("header.bin", ios_base::out | ios_base::binary);
        //if (fout.is_open() == true)
        //{
        //    fout.write(szBuf, nLen);
        //    fout.close();
        //}

        const char* szStart = szBuf;
        uint8_t cBitMask = 0;
        uint8_t cByte = *szBuf;

        if ((cByte & 0x80) == 0x80)         // Indexed Header Field Representation
            cBitMask = 0x7f;
        else if ((cByte & 0xC0) == 0x40)    // Literal Header Field
            cBitMask = 0x3f;
        else if ((cByte & 0xf0) == 0x0)     // Literal Header Field without Indexing
            cBitMask = 0x0f;
        else if ((cByte & 0xf0) == 0x10)    // Literal Header Field Never Indexed
            cBitMask = 0x0f;
        else if ((cByte & 0xE0) == 0x20)    // Dynamic Table Size Update
            cBitMask = 0x1f;
        else
            return SIZE_MAX;

        size_t iIndex = DecodeIndex(&szBuf, &nLen, cBitMask);
        bool bUseDynTbl = false;
        if (iIndex > StaticHeaderListe.size() && cBitMask != 0x1f)
            iIndex -= StaticHeaderListe.size(), bUseDynTbl = true;

        if (cBitMask == 0x7f)       // Indexed Header Field Representation
        {
            if (bUseDynTbl == false && HEADERINDEX(StaticHeaderListe[iIndex - 1]) == iIndex)
            {
                strHeaderName = HEADERNAME(StaticHeaderListe[iIndex - 1]);
                strHeaderValue = HEADERVALUE(StaticHeaderListe[iIndex - 1]);
            }
            else if (bUseDynTbl == true && qDynTable.size() >= iIndex)
            {
                strHeaderName = HEADERNAME(qDynTable[iIndex - 1]);
                strHeaderValue = HEADERVALUE(qDynTable[iIndex - 1]);
            }
            else
                return SIZE_MAX;
        }
        else if (cBitMask == 0x1f)  // Dynamic Table Size Update
        {
            if (nHeaderCount-- != 0)
                throw H2ProtoException(H2ProtoException::DYNTABLE_UPDATE);
            if (iIndex == 0)
                qDynTable.clear();
#if defined(_WIN32) || defined(_WIN64)
            ::OutputDebugString(L"*** Dynamic Table Size Update not yet implemented ***\r\n");
#endif
        }
        else                        // Literal Header Field
        {
            strHeaderValue = DecodeString(&szBuf, &nLen);
            if (iIndex == 0)    // without Indexing
            {
                strHeaderName = strHeaderValue;
                if (strHeaderName[0] == ':' && find_if(begin(StaticHeaderListe), end(StaticHeaderListe), [&](HEADERENTRY he) { return strHeaderName == get<1>(he); }) == end(StaticHeaderListe))
                    throw H2ProtoException(H2ProtoException::FALSE_PSEUDO_HEADER);
                if (none_of(begin(strHeaderName), end(strHeaderName), [](char c) { return islower(c); }))
                    throw H2ProtoException(H2ProtoException::UPPERCASE_HEADER);
                if (strHeaderName == "connection")
                    throw H2ProtoException(H2ProtoException::INVALID_HEADER);
                strHeaderValue = DecodeString(&szBuf, &nLen);
            }
            else
            {
                if (bUseDynTbl == false && HEADERINDEX(StaticHeaderListe[iIndex - 1]) == iIndex)
                    strHeaderName = HEADERNAME(StaticHeaderListe[iIndex - 1]);
                else if (bUseDynTbl == true && qDynTable.size() >= iIndex)
                    strHeaderName = HEADERNAME(qDynTable[iIndex - 1]);
                else
                    return SIZE_MAX;
            }

            if (cBitMask == 0x3f)
                qDynTable.emplace_front(0, strHeaderName, strHeaderValue);
        }

        return (szBuf - szStart);
    }

    size_t HufmanEncode(char* const szBuf, size_t nBufSize, const char* const szString, size_t nStrLen) const noexcept
    {
        ::memset(szBuf, 0, nBufSize);   // clear buffer with all 0 because of the "or" operation

        size_t nTotalBits = 0;

        for (size_t n = 0; n < nStrLen; ++n)
        {
            // Get the Index of the character
            uint32_t nIndex = 0;
            while (nIndex < sizeof(SYMTABLE) && szString[n] != SYMTABLE[nIndex]) nIndex++;

            if (nIndex == sizeof(SYMTABLE))
                return SIZE_MAX;

            uint8_t nBits = 0;
            uint8_t b1, b2 = 0, b3 = 0;

            // Encode the character index into the bit sequence
            if (nIndex < 0xa)
                b1 = nIndex << 3, nBits = 5;
            else if (nIndex < 0x24)
                b1 = (nIndex + 0x0a) << 2, nBits = 6;
            else if (nIndex < 0x44)
                b1 = (nIndex + 0x38) << 1, nBits = 7;
            else if (nIndex < 0x4a)
                b1 = (nIndex + 0xb4), nBits = 8;
            else if (nIndex < 0x4f)
                b1 = (nIndex + 0x3AE) >> 2, b2 = (nIndex + 0x3AE) << 6, nBits = 10;
            else if (nIndex < 0x52)
                b1 = (nIndex + 0x7AB) >> 3, b2 = (nIndex + 0x7AB) << 5, nBits = 11;
            else if (nIndex < 0x54)
                b1 = (nIndex + 0xFA8) >> 4, b2 = (nIndex + 0xFA8) << 4, nBits = 12;
            else if (nIndex < 0x59)
                b1 = (nIndex + 0x1FA5) >> 5, b2 = (nIndex + 0x1FA5) << 3, nBits = 13;
            else if (nIndex < 0x5b)
                b1 = (nIndex + 0x3FA3) >> 6, b2 = (nIndex + 0x3FA3) << 2, nBits = 14;
            else if (nIndex < 0x5e)
                b1 = (nIndex + 0x7FA1) >> 7, b2 = (nIndex + 0x7FA1) << 1, nBits = 15;
            else if (nIndex == 0x5e)
                b1 = (nIndex + 0x7FF92) >> 11, b2 = (nIndex + 0x7FF92) >> 3, b3 = (nIndex + 0x7FF92) << 5, nBits = 19;
            else
                return SIZE_MAX;

            // Check if enough room is in the output buffer to write the next x bits
            if (((nTotalBits + nBits) + (8 - ((nTotalBits + nBits) % 8))) / 8 > nBufSize)
                return SIZE_MAX;

            *(szBuf + (nTotalBits / 8)) |= b1 >> (nTotalBits % 8);
            *(szBuf + (nTotalBits / 8) + 1) |= b1 << (8 - (nTotalBits % 8));

            if (nBits > 8)
            {
                *(szBuf + (nTotalBits / 8) + 1) |= b2 >> (nTotalBits % 8);
                *(szBuf + (nTotalBits / 8) + 2) |= b2 << (8 - (nTotalBits % 8));
            }
            if (nBits > 15)
            {
                *(szBuf + (nTotalBits / 8) + 2) |= b3 >> (nTotalBits % 8);
                *(szBuf + (nTotalBits / 8) + 3) |= b3 << (8 - (nTotalBits % 8));
            }

            nTotalBits += nBits;
        }

        // Fill the rest of the Bits with 1 Bits
        if ((nTotalBits % 8) != 0)
        {
            *(szBuf + (nTotalBits / 8)) |= 0xff >> (nTotalBits % 8);
            nTotalBits += 8 - (nTotalBits % 8);
        }

        return (nTotalBits / 8);
    }

    size_t EncodeInteger(char* const szBuf, size_t nBufSize, size_t nIndex, uint8_t nBitMask, uint8_t nRepBits) const noexcept
    {
        size_t nRet = 1;    // 1 Byte is allays written

        if (nBufSize < nRet)
            return SIZE_MAX;

        if (nIndex < nBitMask)
            *szBuf = static_cast<char>(nIndex | nRepBits);
        else
        {
            nIndex -= nBitMask;
            *szBuf = nBitMask | nRepBits;

            while (nIndex > 0x7f)
            {
                if (nBufSize < nRet + 1)
                    return SIZE_MAX;

                *(szBuf + nRet++) = (nIndex % 0x80) | 0x80;
                nIndex /= 0x80;
            }

            if (nBufSize < nRet + 1)
                return SIZE_MAX;
            *(szBuf + nRet++) = static_cast<char>(nIndex);
        }

        return nRet;
    }

    size_t EncodeString(char* const szBuf, size_t nBufSize, const char* szString, size_t nStrLen) const noexcept
    {
        size_t nRet = 0;

        auto caBuffer = make_unique<char[]>(nStrLen);
        if (caBuffer.get() == 0)
            return SIZE_MAX;
        size_t nEncLen = HufmanEncode(caBuffer.get(), nStrLen, szString, nStrLen);

        if (nEncLen < nStrLen)
        {
            nRet = EncodeInteger(szBuf, nBufSize, nEncLen, 0x7f, 0x80);
            if (nRet == SIZE_MAX || nBufSize < nRet + nEncLen)
                return SIZE_MAX;
            ::memcpy(szBuf + nRet, caBuffer.get(), nEncLen);
            nRet += nEncLen;
        }
        else
        {
            nRet = EncodeInteger(szBuf, nBufSize, nStrLen, 0x7f, 0x00);
            if (nRet == SIZE_MAX || nBufSize < nRet + nStrLen)
                return SIZE_MAX;
            ::memcpy(szBuf + nRet, szString, nStrLen);
            nRet += nStrLen;
        }

        return nRet;
    }

    size_t HPackEncode(char* const szBuf, size_t nLen, const char* const strHeaderId, const char* const strHeaderValue) const noexcept
    {
        uint8_t nIndex = 0;
        string strValue;

        for (HEADERENTRY HeItem : StaticHeaderListe)
        {
            if (::_stricmp(HEADERNAME(HeItem).c_str(), strHeaderId) == 0 && (HEADERVALUE(HeItem).empty() == true || HEADERVALUE(HeItem) == strHeaderValue))
            {
                nIndex = HEADERINDEX(HeItem);
                strValue = HEADERVALUE(HeItem);
                break;
            }
        }

        size_t nBytesWritten = 0;
        if (nIndex != 0 && strValue.empty() == false)
            nBytesWritten = EncodeInteger(szBuf, nLen, nIndex, 0x7f, 0x80);
        else if (nIndex != 0 && strValue.empty() == true)
        {
            nBytesWritten = EncodeInteger(szBuf, nLen, nIndex, 0x0f, 0x00);
            if (nBytesWritten == SIZE_MAX)
                return nBytesWritten;
            nBytesWritten += EncodeString(szBuf + nBytesWritten, nLen - nBytesWritten, strHeaderValue, ::strlen(strHeaderValue));
        }
        else    // Header not in the static list
        {
            nBytesWritten = EncodeInteger(szBuf, nLen, 0, 0x0f, 0x00);
            if (nBytesWritten == SIZE_MAX)
                return nBytesWritten;
            nBytesWritten += EncodeString(szBuf + nBytesWritten, nLen - nBytesWritten, strHeaderId, ::strlen(strHeaderId));
            if (nBytesWritten == SIZE_MAX)
                return nBytesWritten;
            nBytesWritten += EncodeString(szBuf + nBytesWritten, nLen - nBytesWritten, strHeaderValue, ::strlen(strHeaderValue));
        }

        return nBytesWritten;
    }

    void BuildHttp2Frame(char* const szBuf, size_t nDataLen, uint8_t cTyp, uint8_t cFlag, uint32_t nStreamId) const noexcept
    {
        H2FRAME h2f = { static_cast<unsigned long>(nDataLen), cTyp, cFlag, nStreamId, false };
        h2f.size = htonl(h2f.size) & 0xffffff00;
        ::memcpy(szBuf, ((char*)&h2f.size) + 1, 3);
        szBuf[3] = h2f.typ;
        szBuf[4] = h2f.flag;
        h2f.streamId = htonl(h2f.streamId) & 0xfffffff7;
        ::memcpy(&szBuf[5], &h2f.streamId, 4);
    }

    size_t Http2DecodeHeader(const char* szHeaderStart, size_t nHeaderLen, deque<HEADERENTRY>& qDynTable, HeadList& lstHeaderFields) const
    {
        uint32_t nHeaderCount = 0;
        while (nHeaderLen != 0)
        {
            string strHeaderName;
            string strHeaderValue;
            size_t nRet = HPackDecode(szHeaderStart, nHeaderLen, qDynTable, strHeaderName, strHeaderValue, nHeaderCount);
            if (nRet == SIZE_MAX)
                return nRet;
            MyTrace("    ", strHeaderName.c_str(), " : ", (strHeaderValue.empty() == false ? strHeaderValue.c_str() : ""));
            HeadList::iterator iter;
            if (strHeaderName == "cookie" && (iter = lstHeaderFields.find("cookie")) != end(lstHeaderFields))
                iter->second += "; " + strHeaderValue;
            else if (strHeaderName.empty() == false)    // if no Header name is returned, and no error, we had a dynamic table size update
            {
                if (lstHeaderFields.find(strHeaderName) != end(lstHeaderFields))
                    throw H2ProtoException(H2ProtoException::DOUBLE_HEADER);
                if (strHeaderName[0] == ':' && find_if(begin(lstHeaderFields), end(lstHeaderFields), [&](HeadList::const_reference item) { return item.first[0] != ':'; }) != end(lstHeaderFields))
                    throw H2ProtoException(H2ProtoException::WRONG_HEADER);
                if (strHeaderName == "te" && strHeaderValue != "trailers")
                    throw H2ProtoException(H2ProtoException::INVALID_TE_HEADER);
                lstHeaderFields.emplace_back(make_pair(strHeaderName, strHeaderValue));
            }

            szHeaderStart += nRet, nHeaderLen -= nRet;
            ++nHeaderCount;
        }

        return nHeaderLen;
    }

    class H2ProtoException : exception
    {
    public:
        enum HPACKEXCODE : uint32_t
        {
            BUFFSIZE_ERROR      = 1,
            DYNTABLE_UPDATE     = 2,
            DOUBLE_HEADER       = 3,
            UPPERCASE_HEADER    = 4,
            WRONG_HEADER        = 5,
            FALSE_PSEUDO_HEADER = 6,
            INVALID_HEADER      = 7,
            INVALID_TE_HEADER   = 8,
            WRONG_STREAM_ID     = 9,
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
            INERNAL_ERROR       = 30
        };

        H2ProtoException(HPACKEXCODE eCode) : m_eCode(eCode) {}
        H2ProtoException(HPACKEXCODE eCode, uint32_t nStreamId) : m_eCode(eCode), m_nStreamId(nStreamId) {}
        virtual ~H2ProtoException() throw() {}
        const char* what() const throw() {
            return m_strError.c_str();
        }
        const HPACKEXCODE GetCode() { return m_eCode; }
        const uint32_t GetStreamId() { return m_nStreamId; }

    private:
        string      m_strError;
        HPACKEXCODE m_eCode;
        uint32_t    m_nStreamId;
    };

protected:
#pragma  pack(1)
        typedef struct h2Frame
        {
            unsigned long size;
            uint8_t typ;
            uint8_t flag;
            uint32_t streamId;
            bool R;
        }H2FRAME;
#pragma pack()

private:
    const uint8_t SYMTABLE[95] = { '0', '1', '2', 'a', 'c', 'e', 'i', 'o', 's', 't', ' ', '%', '-', '.', '/', '3', '4', '5', '6', '7', '8', '9', '=', 'A', '_', 'b', 'd', 'f', 'g', 'h', 'l', 'm', 'n', 'p', 'r', 'u', ':', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'Y', 'j', 'k', 'q', 'v', 'w', 'x', 'y', 'z', '&', '*', ',', ';', 'X', 'Z', '!', '"', '(', ')', '?', '\'', '+', '|', '#', '>', '$', '@', '[', ']', '~', '^', '}', '<', '`', '{', '\\' };
    const array<HEADERENTRY, 61>  StaticHeaderListe = { {
        HEADERENTRY(1, ":authority", ""),
        HEADERENTRY(2, ":method", "GET"),
        HEADERENTRY(3, ":method", "POST"),
        HEADERENTRY(4, ":path", "/"),
        HEADERENTRY(5, ":path", "/index.html"),
        HEADERENTRY(6, ":scheme", "http"),
        HEADERENTRY(7, ":scheme", "https"),
        HEADERENTRY(8, ":status", "200"),
        HEADERENTRY(9, ":status", "204"),
        HEADERENTRY(10, ":status", "206"),
        HEADERENTRY(11, ":status", "304"),
        HEADERENTRY(12, ":status", "400"),
        HEADERENTRY(13, ":status", "404"),
        HEADERENTRY(14, ":status", "500"),
        HEADERENTRY(15, "accept-charset", ""),
        HEADERENTRY(16, "accept-encoding", "gzip, deflate"),
        HEADERENTRY(17, "accept-language", ""),
        HEADERENTRY(18, "accept-ranges", ""),
        HEADERENTRY(19, "accept", ""),
        HEADERENTRY(20, "access-control-allow-origin", ""),
        HEADERENTRY(21, "age", ""),
        HEADERENTRY(22, "allow", ""),
        HEADERENTRY(23, "authorization", ""),
        HEADERENTRY(24, "cache-control", ""),
        HEADERENTRY(25, "content-disposition", ""),
        HEADERENTRY(26, "content-encoding", ""),
        HEADERENTRY(27, "content-language", ""),
        HEADERENTRY(28, "content-length", ""),
        HEADERENTRY(29, "content-location", ""),
        HEADERENTRY(30, "content-range", ""),
        HEADERENTRY(31, "content-type", ""),
        HEADERENTRY(32, "cookie", ""),
        HEADERENTRY(33, "date", ""),
        HEADERENTRY(34, "etag", ""),
        HEADERENTRY(35, "expect", ""),
        HEADERENTRY(36, "expires", ""),
        HEADERENTRY(37, "from", ""),
        HEADERENTRY(38, "host", ""),
        HEADERENTRY(39, "if-match", ""),
        HEADERENTRY(40, "if-modified-since", ""),
        HEADERENTRY(41, "if-none-match", ""),
        HEADERENTRY(42, "if-range", ""),
        HEADERENTRY(43, "if-unmodified-since", ""),
        HEADERENTRY(44, "last-modified", ""),
        HEADERENTRY(45, "link", ""),
        HEADERENTRY(46, "location", ""),
        HEADERENTRY(47, "max-forwards", ""),
        HEADERENTRY(48, "proxy-authenticate", ""),
        HEADERENTRY(49, "proxy-authorization", ""),
        HEADERENTRY(50, "range", ""),
        HEADERENTRY(51, "referer", ""),
        HEADERENTRY(52, "refresh", ""),
        HEADERENTRY(53, "retry-after", ""),
        HEADERENTRY(54, "server", ""),
        HEADERENTRY(55, "set-cookie", ""),
        HEADERENTRY(56, "strict-transport-security", ""),
        HEADERENTRY(57, "transfer-encoding", ""),
        HEADERENTRY(58, "user-agent", ""),
        HEADERENTRY(59, "vary", ""),
        HEADERENTRY(60, "via", ""),
        HEADERENTRY(61, "www-authenticate", "")
    } };
};

