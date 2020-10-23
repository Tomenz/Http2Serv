/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#include <deque>
#include <memory>
#include <algorithm>
#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <string.h>
#define _stricmp strcasecmp
#endif

#include "HPack.h"

HPack::HPack() 
{
}

string HPack::HufmanDecode(const char* szBuf, size_t nLen, int& iError) const noexcept
{
    string strRet;
    const static array<uint32_t, 21> BITNUMBERS = { 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30 };
    uint32_t nAnzBit = 0;

    while (nLen > (nAnzBit != 0 && (8 - nAnzBit) < 5 ? 1 : 0))   // Wenn wir als Rest kleiner 5 Bits haben brauchen wir mehr als 1 Byte
    {
        if (nLen == 1 && (((uint8_t)*szBuf << nAnzBit) & 0xff) == ((0xff << nAnzBit) & 0xff))   // Wenn wir noch 1 Byt haben und der Rest des Bytes ist alles 1 haben wir Füll Bits
            break;

        size_t nIndex = HUFFCODES.size();
        for (uint32_t i = 0; i < BITNUMBERS.size(); ++i)
        {
            const size_t iGesBits = nAnzBit + BITNUMBERS[i];  // Bereits geschrieben Bits des Momentanen Byts + die neuen Bits
            const size_t iAnzByte = (iGesBits / 8) + ((iGesBits % 8) != 0 ? 1 : 0);  // Ganze Bytes + wenn rest noch eins
            if (iAnzByte > nLen)        // Wenn wir mehr Byts benötigen als Bytes im Buffer
                break;

            uint64_t n1 = 0 ;
            for (size_t e = 0; e < iAnzByte; ++e)
                n1 |= static_cast<uint64_t>(*(szBuf + e) & 0xff) << (56 - (e * 8));
            n1 >>= 64 - iGesBits;
            uint32_t n = static_cast<uint32_t>(n1 & (0xffffffff >> (32 - BITNUMBERS[i])));
            if (n == (0xffffffff >> (32 - BITNUMBERS[i])))  // If we have only 1, we need more bits
                continue;

            if (nIndex = distance(begin(HUFFCODES), find_if(begin(HUFFCODES), end(HUFFCODES), [&](auto prItem) { return prItem.first == n && prItem.second == BITNUMBERS[i] ? true : false; })), nIndex < HUFFCODES.size())
            {
                if (nIndex < HUFFCODES.size() - 1)  // EOS nicht in die Ausgabe schreiben
                    strRet += static_cast<char>(nIndex);
                nAnzBit += BITNUMBERS[i];
                szBuf += nAnzBit / 8;
                nLen -= nAnzBit / 8;
                nAnzBit -= 8 * (nAnzBit / 8);
                break;
            }
        }
        if (nIndex == HUFFCODES.size() || nIndex == HUFFCODES.size() - 1)
            break;
    }

    if (nLen == 1 && (((uint8_t)*szBuf << nAnzBit) & 0xff) != ((0xff << nAnzBit) & 0xff))   // Der Rest des letzten Byts sollte nur 1 sein
        iError = 1;            //OutputDebugString(L"Padded with 0 Bits\r\n");   // Padding with 0
    else if (nLen > 1)
        iError = 2;            //OutputDebugString(L"Padding with unused Bits\r\n");   // Padding with unused Bits

    return strRet;
}


uint32_t HPack::DecodeInteger(const char** pszBuf, size_t* const pnLen) const
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

string HPack::DecodeString(const char** pszBuf, size_t* const pnLen) const
{
    if (*pnLen == 0)
        throw H2ProtoException(H2ProtoException::BUFFSIZE_ERROR);

    bool bEncoded = (*(*pszBuf) & 0x80) == 0x80 ? true : false;
    size_t nStrLen = (*((*pszBuf)++) & 0x7f);
    (*pnLen)--;

    if ((nStrLen & 0x7f) == 0x7f)    // 7 Bit prefix
        nStrLen += DecodeInteger(pszBuf, pnLen);// , ::OutputDebugString(L"Integer decodieren\r\n");

    if (*pnLen < nStrLen)
        throw H2ProtoException(H2ProtoException::BUFFSIZE_ERROR);

    int iHufmanError = 0;
    string strReturn = (bEncoded == true ? HufmanDecode(*pszBuf, nStrLen, iHufmanError) : string(*pszBuf, nStrLen));
    (*pnLen) -= nStrLen, (*pszBuf) += nStrLen;

    if (iHufmanError == 1)
        throw H2ProtoException(H2ProtoException::H2ProtoException::COMPRESSION_ERROR);
    else if (iHufmanError == 2)
        throw H2ProtoException(H2ProtoException::H2ProtoException::COMPRESSION_ERROR);

    return strReturn;
}

size_t HPack::DecodeIndex(const char** pszBuf, size_t* const pnLen, uint32_t nBitMask) const
{
    if (*pnLen == 0)
        throw H2ProtoException(H2ProtoException::BUFFSIZE_ERROR);

    size_t iIndex = *((*pszBuf)++) & nBitMask;
    (*pnLen)--;

    if ((iIndex & nBitMask) == nBitMask)
        iIndex += DecodeInteger(pszBuf, pnLen);// , ::OutputDebugString(L"Integer decodieren\r\n");

    return iIndex;
}

size_t HPack::HPackDecode(const char* szBuf, size_t nLen, deque<HEADERENTRY>& qDynTable, string& strHeaderName, string& strHeaderValue, uint32_t& nHeaderCount, STREAMSETTINGS& tuStreamSettings) const
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
        if (iIndex == 0)
            throw H2ProtoException(H2ProtoException::H2ProtoException::COMPRESSION_ERROR);

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
        if (iIndex > HEADER_TABLE_SIZE(tuStreamSettings))
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
            qDynTable.emplace_front(static_cast<uint8_t>(0), strHeaderName, strHeaderValue);
    }

    return (szBuf - szStart);
}

size_t HPack::HufmanEncode(uint8_t* szBuf, size_t nBufSize, const char* const szString, size_t nStrLen) const noexcept
{
    ::memset(szBuf, 0, nBufSize);   // clear buffer with all 0 because of the "or" operation
    uint8_t* szEnd = szBuf + nBufSize;
    size_t nTotalBits = 0;

    for (size_t n = 0; n < nStrLen; ++n)
    {
        // Get the Index of the character
        auto itCode = HUFFCODES.begin() + static_cast<uint8_t>(szString[n]);

        // Check if enough room is in the output buffer to write the next x bits
        if (((nTotalBits + itCode->second) / 8) + (((nTotalBits + itCode->second) % 8) != 0 ? 1 : 0) > nBufSize)
            return SIZE_MAX;

        int iUsedBits = nTotalBits % 8;
        int iGesBits = iUsedBits + itCode->second;  // Bereits geschrieben Bits des momentanen Bytes + die neuen Bits
        int iAnzByte = (iGesBits / 8) + ((iGesBits % 8) != 0 ? 1 : 0);  // Ganze Bytes + wenn rest noch eins
        uint64_t nBitField = static_cast<uint64_t>(itCode->first) << ((64 - itCode->second) - iUsedBits);
        for (int i = 0; i < iAnzByte; ++i)
        {
            if (szBuf + i >= szEnd)
                return SIZE_MAX;
            *(szBuf + i) |= static_cast<uint8_t>(nBitField >> (56 - (i * 8)) & 0xff);
        }

        szBuf += iGesBits / 8;  // Bits bereits benutzt + neue Bits / 8 ist die Anzahl der fertigen Bytes
        nTotalBits += itCode->second;
    }

    // Fill the rest of the Bits with 1 Bits
    if ((nTotalBits % 8) != 0)
    {
        *szBuf |= 0xff >> (nTotalBits % 8);
        nTotalBits += 8 - (nTotalBits % 8);
    }

    return (nTotalBits / 8);
}

size_t HPack::EncodeInteger(uint8_t* const szBuf, size_t nBufSize, size_t nIndex, uint8_t nBitMask, uint8_t nRepBits) const noexcept
{
    size_t nRet = 1;    // 1 Byte is allays written

    if (nBufSize < nRet)
        return SIZE_MAX;

    if (nIndex < nBitMask)
        *szBuf = static_cast<uint8_t>(nIndex | nRepBits);
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
        *(szBuf + nRet++) = static_cast<uint8_t>(nIndex);
    }

    return nRet;
}

size_t HPack::EncodeString(uint8_t* const szBuf, size_t nBufSize, const char* szString, size_t nStrLen) const noexcept
{
    size_t nRet = 0;

    auto caBuffer = make_unique<uint8_t[]>(nStrLen);
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

size_t HPack::HPackEncode(uint8_t* const szBuf, size_t nLen, const char* const strHeaderId, const char* const strHeaderValue) const noexcept
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
            return SIZE_MAX;
        size_t nTmp = EncodeString(szBuf + nBytesWritten, nLen - nBytesWritten, strHeaderValue, ::strlen(strHeaderValue));
        if (nTmp == SIZE_MAX)
            return SIZE_MAX;
        nBytesWritten += nTmp;
    }
    else    // Header not in the static list
    {
        nBytesWritten = EncodeInteger(szBuf, nLen, 0, 0x0f, 0x00);
        if (nBytesWritten == SIZE_MAX)
            return SIZE_MAX;
        size_t nTmp = EncodeString(szBuf + nBytesWritten, nLen - nBytesWritten, strHeaderId, ::strlen(strHeaderId));
        if (nTmp == SIZE_MAX)
            return SIZE_MAX;
        nBytesWritten += nTmp;
        nTmp = EncodeString(szBuf + nBytesWritten, nLen - nBytesWritten, strHeaderValue, ::strlen(strHeaderValue));
        if (nTmp == SIZE_MAX)
            return SIZE_MAX;
        nBytesWritten += nTmp;
    }

    return nBytesWritten;
}

size_t HPack::Http2DecodeHeader(const char* szHeaderStart, size_t nHeaderLen, deque<HEADERENTRY>& qDynTable, HeadList& lstHeaderFields, STREAMSETTINGS& tuStreamSettings) const
{
    uint32_t nHeaderCount = 0;
    while (nHeaderLen != 0)
    {
        string strHeaderName;
        string strHeaderValue;
        size_t nRet = HPackDecode(szHeaderStart, nHeaderLen, qDynTable, strHeaderName, strHeaderValue, nHeaderCount, tuStreamSettings);
        if (nRet == SIZE_MAX)
            return nRet;
        MyTrace("    ", strHeaderName.c_str(), " : ", (strHeaderValue.empty() == false ? strHeaderValue.c_str() : ""));
        HeadList::iterator iter;
        if (strHeaderName == "cookie" && (iter = lstHeaderFields.find("cookie")) != end(lstHeaderFields))
            iter->second += "; " + strHeaderValue;
        else if (strHeaderName.empty() == false)    // if no Header name is returned, and no error, we had a dynamic table size update
        {
            if (strHeaderName[0] == ':')
            {
                // pseudo header can only be once in a header, no doubles allowed
                if (lstHeaderFields.find(strHeaderName) != end(lstHeaderFields))
                    throw H2ProtoException(H2ProtoException::PROTOCOL_ERROR);
                // pseudo header fields must be the first one in the header
                if (find_if(begin(lstHeaderFields), end(lstHeaderFields), [&](HeadList::const_reference item) { return item.first[0] != ':'; }) != end(lstHeaderFields))
                    throw H2ProtoException(H2ProtoException::WRONG_HEADER);
            }
            if (strHeaderName == "te" && strHeaderValue != "trailers")
                throw H2ProtoException(H2ProtoException::INVALID_TE_HEADER);
            lstHeaderFields.emplace_back(make_pair(strHeaderName, strHeaderValue));
        }

        szHeaderStart += nRet, nHeaderLen -= nRet;
        ++nHeaderCount;
    }

    return nHeaderLen;
}

const vector<pair<uint32_t, uint32_t>> HPack::HUFFCODES = { { 0x1ff8, 13 },
                                                            { 0x7fffd8, 23 },
                                                            { 0xfffffe2, 28 },
                                                            { 0xfffffe3, 28 },
                                                            { 0xfffffe4, 28 },
                                                            { 0xfffffe5, 28 },
                                                            { 0xfffffe6, 28 },
                                                            { 0xfffffe7, 28 },
                                                            { 0xfffffe8, 28 },
                                                            { 0xffffea, 24 },
                                                            { 0x3ffffffc, 30 },
                                                            { 0xfffffe9, 28 },
                                                            { 0xfffffea, 28 },
                                                            { 0x3ffffffd, 30 },
                                                            { 0xfffffeb, 28 },
                                                            { 0xfffffec, 28 },
                                                            { 0xfffffed, 28 },
                                                            { 0xfffffee, 28 },
                                                            { 0xfffffef, 28 },
                                                            { 0xffffff0, 28 },
                                                            { 0xffffff1, 28 },
                                                            { 0xffffff2, 28 },
                                                            { 0x3ffffffe, 30 },
                                                            { 0xffffff3, 28 },
                                                            { 0xffffff4, 28 },
                                                            { 0xffffff5, 28 },
                                                            { 0xffffff6, 28 },
                                                            { 0xffffff7, 28 },
                                                            { 0xffffff8, 28 },
                                                            { 0xffffff9, 28 },
                                                            { 0xffffffa, 28 },
                                                            { 0xffffffb, 28 },
                                                            { 0x14, 6 },
                                                            { 0x3f8, 10 },
                                                            { 0x3f9, 10 },
                                                            { 0xffa, 12 },
                                                            { 0x1ff9, 13 },
                                                            { 0x15, 6 },
                                                            { 0xf8, 8 },
                                                            { 0x7fa, 11 },
                                                            { 0x3fa, 10 },
                                                            { 0x3fb, 10 },
                                                            { 0xf9, 8 },
                                                            { 0x7fb, 11 },
                                                            { 0xfa, 8 },
                                                            { 0x16, 6 },
                                                            { 0x17, 6 },
                                                            { 0x18, 6 },
                                                            { 0x0, 5 },
                                                            { 0x1, 5 },
                                                            { 0x2, 5 },
                                                            { 0x19, 6 },
                                                            { 0x1a, 6 },
                                                            { 0x1b, 6 },
                                                            { 0x1c, 6 },
                                                            { 0x1d, 6 },
                                                            { 0x1e, 6 },
                                                            { 0x1f, 6 },
                                                            { 0x5c, 7 },
                                                            { 0xfb, 8 },
                                                            { 0x7ffc, 15 },
                                                            { 0x20, 6 },
                                                            { 0xffb, 12 },
                                                            { 0x3fc, 10 },
                                                            { 0x1ffa, 13 },
                                                            { 0x21, 6 },
                                                            { 0x5d, 7 },
                                                            { 0x5e, 7 },
                                                            { 0x5f, 7 },
                                                            { 0x60, 7 },
                                                            { 0x61, 7 },
                                                            { 0x62, 7 },
                                                            { 0x63, 7 },
                                                            { 0x64, 7 },
                                                            { 0x65, 7 },
                                                            { 0x66, 7 },
                                                            { 0x67, 7 },
                                                            { 0x68, 7 },
                                                            { 0x69, 7 },
                                                            { 0x6a, 7 },
                                                            { 0x6b, 7 },
                                                            { 0x6c, 7 },
                                                            { 0x6d, 7 },
                                                            { 0x6e, 7 },
                                                            { 0x6f, 7 },
                                                            { 0x70, 7 },
                                                            { 0x71, 7 },
                                                            { 0x72, 7 },
                                                            { 0xfc, 8 },
                                                            { 0x73, 7 },
                                                            { 0xfd, 8 },
                                                            { 0x1ffb, 13 },
                                                            { 0x7fff0, 19 },
                                                            { 0x1ffc, 13 },
                                                            { 0x3ffc, 14 },
                                                            { 0x22, 6 },
                                                            { 0x7ffd, 15 },
                                                            { 0x3, 5 },
                                                            { 0x23, 6 },
                                                            { 0x4, 5 },
                                                            { 0x24, 6 },
                                                            { 0x5, 5 },
                                                            { 0x25, 6 },
                                                            { 0x26, 6 },
                                                            { 0x27, 6 },
                                                            { 0x6, 5 },
                                                            { 0x74, 7 },
                                                            { 0x75, 7 },
                                                            { 0x28, 6 },
                                                            { 0x29, 6 },
                                                            { 0x2a, 6 },
                                                            { 0x7, 5 },
                                                            { 0x2b, 6 },
                                                            { 0x76, 7 },
                                                            { 0x2c, 6 },
                                                            { 0x8, 5 },
                                                            { 0x9, 5 },
                                                            { 0x2d, 6 },
                                                            { 0x77, 7 },
                                                            { 0x78, 7 },
                                                            { 0x79, 7 },
                                                            { 0x7a, 7 },
                                                            { 0x7b, 7 },
                                                            { 0x7ffe, 15 },
                                                            { 0x7fc, 11 },
                                                            { 0x3ffd, 14 },
                                                            { 0x1ffd, 13 },
                                                            { 0xffffffc, 28 },
                                                            { 0xfffe6, 20 },
                                                            { 0x3fffd2, 22 },
                                                            { 0xfffe7, 20 },
                                                            { 0xfffe8, 20 },
                                                            { 0x3fffd3, 22 },
                                                            { 0x3fffd4, 22 },
                                                            { 0x3fffd5, 22 },
                                                            { 0x7fffd9, 23 },
                                                            { 0x3fffd6, 22 },
                                                            { 0x7fffda, 23 },
                                                            { 0x7fffdb, 23 },
                                                            { 0x7fffdc, 23 },
                                                            { 0x7fffdd, 23 },
                                                            { 0x7fffde, 23 },
                                                            { 0xffffeb, 24 },
                                                            { 0x7fffdf, 23 },
                                                            { 0xffffec, 24 },
                                                            { 0xffffed, 24 },
                                                            { 0x3fffd7, 22 },
                                                            { 0x7fffe0, 23 },
                                                            { 0xffffee, 24 },
                                                            { 0x7fffe1, 23 },
                                                            { 0x7fffe2, 23 },
                                                            { 0x7fffe3, 23 },
                                                            { 0x7fffe4, 23 },
                                                            { 0x1fffdc, 21 },
                                                            { 0x3fffd8, 22 },
                                                            { 0x7fffe5, 23 },
                                                            { 0x3fffd9, 22 },
                                                            { 0x7fffe6, 23 },
                                                            { 0x7fffe7, 23 },
                                                            { 0xffffef, 24 },
                                                            { 0x3fffda, 22 },
                                                            { 0x1fffdd, 21 },
                                                            { 0xfffe9, 20 },
                                                            { 0x3fffdb, 22 },
                                                            { 0x3fffdc, 22 },
                                                            { 0x7fffe8, 23 },
                                                            { 0x7fffe9, 23 },
                                                            { 0x1fffde, 21 },
                                                            { 0x7fffea, 23 },
                                                            { 0x3fffdd, 22 },
                                                            { 0x3fffde, 22 },
                                                            { 0xfffff0, 24 },
                                                            { 0x1fffdf, 21 },
                                                            { 0x3fffdf, 22 },
                                                            { 0x7fffeb, 23 },
                                                            { 0x7fffec, 23 },
                                                            { 0x1fffe0, 21 },
                                                            { 0x1fffe1, 21 },
                                                            { 0x3fffe0, 22 },
                                                            { 0x1fffe2, 21 },
                                                            { 0x7fffed, 23 },
                                                            { 0x3fffe1, 22 },
                                                            { 0x7fffee, 23 },
                                                            { 0x7fffef, 23 },
                                                            { 0xfffea, 20 },
                                                            { 0x3fffe2, 22 },
                                                            { 0x3fffe3, 22 },
                                                            { 0x3fffe4, 22 },
                                                            { 0x7ffff0, 23 },
                                                            { 0x3fffe5, 22 },
                                                            { 0x3fffe6, 22 },
                                                            { 0x7ffff1, 23 },
                                                            { 0x3ffffe0, 26 },
                                                            { 0x3ffffe1, 26 },
                                                            { 0xfffeb, 20 },
                                                            { 0x7fff1, 19 },
                                                            { 0x3fffe7, 22 },
                                                            { 0x7ffff2, 23 },
                                                            { 0x3fffe8, 22 },
                                                            { 0x1ffffec, 25 },
                                                            { 0x3ffffe2, 26 },
                                                            { 0x3ffffe3, 26 },
                                                            { 0x3ffffe4, 26 },
                                                            { 0x7ffffde, 27 },
                                                            { 0x7ffffdf, 27 },
                                                            { 0x3ffffe5, 26 },
                                                            { 0xfffff1, 24 },
                                                            { 0x1ffffed, 25 },
                                                            { 0x7fff2, 19 },
                                                            { 0x1fffe3, 21 },
                                                            { 0x3ffffe6, 26 },
                                                            { 0x7ffffe0, 27 },
                                                            { 0x7ffffe1, 27 },
                                                            { 0x3ffffe7, 26 },
                                                            { 0x7ffffe2, 27 },
                                                            { 0xfffff2, 24 },
                                                            { 0x1fffe4, 21 },
                                                            { 0x1fffe5, 21 },
                                                            { 0x3ffffe8, 26 },
                                                            { 0x3ffffe9, 26 },
                                                            { 0xffffffd, 28 },
                                                            { 0x7ffffe3, 27 },
                                                            { 0x7ffffe4, 27 },
                                                            { 0x7ffffe5, 27 },
                                                            { 0xfffec, 20 },
                                                            { 0xfffff3, 24 },
                                                            { 0xfffed, 20 },
                                                            { 0x1fffe6, 21 },
                                                            { 0x3fffe9, 22 },
                                                            { 0x1fffe7, 21 },
                                                            { 0x1fffe8, 21 },
                                                            { 0x7ffff3, 23 },
                                                            { 0x3fffea, 22 },
                                                            { 0x3fffeb, 22 },
                                                            { 0x1ffffee, 25 },
                                                            { 0x1ffffef, 25 },
                                                            { 0xfffff4, 24 },
                                                            { 0xfffff5, 24 },
                                                            { 0x3ffffea, 26 },
                                                            { 0x7ffff4, 23 },
                                                            { 0x3ffffeb, 26 },
                                                            { 0x7ffffe6, 27 },
                                                            { 0x3ffffec, 26 },
                                                            { 0x3ffffed, 26 },
                                                            { 0x7ffffe7, 27 },
                                                            { 0x7ffffe8, 27 },
                                                            { 0x7ffffe9, 27 },
                                                            { 0x7ffffea, 27 },
                                                            { 0x7ffffeb, 27 },
                                                            { 0xffffffe, 28 },
                                                            { 0x7ffffec, 27 },
                                                            { 0x7ffffed, 27 },
                                                            { 0x7ffffee, 27 },
                                                            { 0x7ffffef, 27 },
                                                            { 0x7fffff0, 27 },
                                                            { 0x3ffffee, 26 },
                                                            { 0x3fffffff, 30 } };


const uint8_t HPack::SYMTABLE[95] = { '0', '1', '2', 'a', 'c', 'e', 'i', 'o', 's', 't', ' ', '%', '-', '.', '/', '3', '4', '5', '6', '7', '8', '9', '=', 'A', '_', 'b', 'd', 'f', 'g', 'h', 'l', 'm', 'n', 'p', 'r', 'u', ':', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'Y', 'j', 'k', 'q', 'v', 'w', 'x', 'y', 'z', '&', '*', ',', ';', 'X', 'Z', '!', '"', '(', ')', '?', '\'', '+', '|', '#', '>', '$', '@', '[', ']', '~', '^', '}', '<', '`', '{', '\\' };
const array<HEADERENTRY, 61>  HPack::StaticHeaderListe = { {
    HEADERENTRY(static_cast<uint8_t>(1), ":authority", ""),
    HEADERENTRY(static_cast<uint8_t>(2), ":method", "GET"),
    HEADERENTRY(static_cast<uint8_t>(3), ":method", "POST"),
    HEADERENTRY(static_cast<uint8_t>(4), ":path", "/"),
    HEADERENTRY(static_cast<uint8_t>(5), ":path", "/index.html"),
    HEADERENTRY(static_cast<uint8_t>(6), ":scheme", "http"),
    HEADERENTRY(static_cast<uint8_t>(7), ":scheme", "https"),
    HEADERENTRY(static_cast<uint8_t>(8), ":status", "200"),
    HEADERENTRY(static_cast<uint8_t>(9), ":status", "204"),
    HEADERENTRY(static_cast<uint8_t>(10), ":status", "206"),
    HEADERENTRY(static_cast<uint8_t>(11), ":status", "304"),
    HEADERENTRY(static_cast<uint8_t>(12), ":status", "400"),
    HEADERENTRY(static_cast<uint8_t>(13), ":status", "404"),
    HEADERENTRY(static_cast<uint8_t>(14), ":status", "500"),
    HEADERENTRY(static_cast<uint8_t>(15), "accept-charset", ""),
    HEADERENTRY(static_cast<uint8_t>(16), "accept-encoding", "gzip, deflate"),
    HEADERENTRY(static_cast<uint8_t>(17), "accept-language", ""),
    HEADERENTRY(static_cast<uint8_t>(18), "accept-ranges", ""),
    HEADERENTRY(static_cast<uint8_t>(19), "accept", ""),
    HEADERENTRY(static_cast<uint8_t>(20), "access-control-allow-origin", ""),
    HEADERENTRY(static_cast<uint8_t>(21), "age", ""),
    HEADERENTRY(static_cast<uint8_t>(22), "allow", ""),
    HEADERENTRY(static_cast<uint8_t>(23), "authorization", ""),
    HEADERENTRY(static_cast<uint8_t>(24), "cache-control", ""),
    HEADERENTRY(static_cast<uint8_t>(25), "content-disposition", ""),
    HEADERENTRY(static_cast<uint8_t>(26), "content-encoding", ""),
    HEADERENTRY(static_cast<uint8_t>(27), "content-language", ""),
    HEADERENTRY(static_cast<uint8_t>(28), "content-length", ""),
    HEADERENTRY(static_cast<uint8_t>(29), "content-location", ""),
    HEADERENTRY(static_cast<uint8_t>(30), "content-range", ""),
    HEADERENTRY(static_cast<uint8_t>(31), "content-type", ""),
    HEADERENTRY(static_cast<uint8_t>(32), "cookie", ""),
    HEADERENTRY(static_cast<uint8_t>(33), "date", ""),
    HEADERENTRY(static_cast<uint8_t>(34), "etag", ""),
    HEADERENTRY(static_cast<uint8_t>(35), "expect", ""),
    HEADERENTRY(static_cast<uint8_t>(36), "expires", ""),
    HEADERENTRY(static_cast<uint8_t>(37), "from", ""),
    HEADERENTRY(static_cast<uint8_t>(38), "host", ""),
    HEADERENTRY(static_cast<uint8_t>(39), "if-match", ""),
    HEADERENTRY(static_cast<uint8_t>(40), "if-modified-since", ""),
    HEADERENTRY(static_cast<uint8_t>(41), "if-none-match", ""),
    HEADERENTRY(static_cast<uint8_t>(42), "if-range", ""),
    HEADERENTRY(static_cast<uint8_t>(43), "if-unmodified-since", ""),
    HEADERENTRY(static_cast<uint8_t>(44), "last-modified", ""),
    HEADERENTRY(static_cast<uint8_t>(45), "link", ""),
    HEADERENTRY(static_cast<uint8_t>(46), "location", ""),
    HEADERENTRY(static_cast<uint8_t>(47), "max-forwards", ""),
    HEADERENTRY(static_cast<uint8_t>(48), "proxy-authenticate", ""),
    HEADERENTRY(static_cast<uint8_t>(49), "proxy-authorization", ""),
    HEADERENTRY(static_cast<uint8_t>(50), "range", ""),
    HEADERENTRY(static_cast<uint8_t>(51), "referer", ""),
    HEADERENTRY(static_cast<uint8_t>(52), "refresh", ""),
    HEADERENTRY(static_cast<uint8_t>(53), "retry-after", ""),
    HEADERENTRY(static_cast<uint8_t>(54), "server", ""),
    HEADERENTRY(static_cast<uint8_t>(55), "set-cookie", ""),
    HEADERENTRY(static_cast<uint8_t>(56), "strict-transport-security", ""),
    HEADERENTRY(static_cast<uint8_t>(57), "transfer-encoding", ""),
    HEADERENTRY(static_cast<uint8_t>(58), "user-agent", ""),
    HEADERENTRY(static_cast<uint8_t>(59), "vary", ""),
    HEADERENTRY(static_cast<uint8_t>(60), "via", ""),
    HEADERENTRY(static_cast<uint8_t>(61), "www-authenticate", "")
} };
