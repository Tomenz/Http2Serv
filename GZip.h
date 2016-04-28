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

#ifndef GZIP
#define GZIP

#include "zlib/zlib.h"

#ifdef _DEBUG
#ifdef _WIN64
#pragma comment(lib, "x64/Debug/zlib")
#else
#pragma comment(lib, "Debug/zlib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "x64/Release/zlib")
#else
#pragma comment(lib, "Release/zlib")
#endif
#endif

class GZipUnpack
{
public:
    GZipUnpack() : m_strm({ 0 })
    {
    }

    virtual ~GZipUnpack() {
        inflateEnd(&m_strm);
    }

    int Init()
    {
        /* allocate inflate state */
        int iRet = inflateInit2(&m_strm, 15 + 16);
        inflateReset(&m_strm);

        return iRet;
    }

    void InitBuffer(unsigned char* const pIn, uint32_t nInCount)
    {
        m_strm.avail_in = nInCount;
        m_strm.next_in = pIn;
    }

    int Deflate(unsigned char* pOut, uint32_t* pnOutCount)
    {
        m_strm.avail_out = *pnOutCount;
        m_strm.next_out  = pOut;

        int iRet = inflate(&m_strm, Z_NO_FLUSH);
        
        *pnOutCount -= m_strm.avail_out;

        return iRet;
    }

private:
    z_stream m_strm;
};

class GZipPack
{
public:
    GZipPack() : m_strm({ 0 })
    {
    }

    virtual ~GZipPack() {
        deflateEnd(&m_strm);
    }

    int Init()
    {
        /* allocate inflate state */
        int iRet = deflateInit2(&m_strm, Z_BEST_SPEED/*Z_DEFAULT_COMPRESSION/*Z_BEST_COMPRESSION*/, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
        //deflateReset(&m_strm);
        //int iRet = deflateInit(&m_strm, Z_BEST_COMPRESSION);

        return iRet;
    }

    void InitBuffer(unsigned char* const pIn, uint32_t nInCount)
    {
        m_strm.avail_in = nInCount;
        m_strm.next_in = pIn;
    }

    int Enflate(unsigned char* pOut, uint32_t* pnOutCount, int nFlush)
    {
        m_strm.avail_out = *pnOutCount;
        m_strm.next_out = pOut;

        int iRet = deflate(&m_strm, nFlush);

        *pnOutCount = m_strm.avail_out;

        return iRet;
    }

private:
    z_stream m_strm;
};

#endif // GZIP
