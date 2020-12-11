/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#pragma once

#ifndef GZIP
#define GZIP

#include "zlib/zlib.h"

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
        const int iRet = inflateInit2(&m_strm, 15 + 32);
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

        const int iRet = inflate(&m_strm, Z_NO_FLUSH);

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

    int Init(bool bUseDeflate = false)
    {
        /* allocate inflate state */
        const int iRet = deflateInit2(&m_strm, Z_BEST_SPEED/*Z_DEFAULT_COMPRESSION*//*Z_BEST_COMPRESSION*/, Z_DEFLATED, 15 + (bUseDeflate == false ? 16 : 0), 9, Z_DEFAULT_STRATEGY);
        //deflateReset(&m_strm);
        //int iRet = deflateInit(&m_strm, Z_BEST_COMPRESSION);

        return iRet;
    }

    void InitBuffer(unsigned char* const pIn, uint32_t nInCount)
    {
        m_strm.avail_in = nInCount;
        m_strm.next_in = pIn;
    }

    int Enflate(unsigned char* pOut, size_t* pnOutCount, int nFlush)
    {
        m_strm.avail_out = static_cast<uint32_t>(*pnOutCount);
        m_strm.next_out = pOut;

        const int iRet = deflate(&m_strm, nFlush);

        *pnOutCount = m_strm.avail_out;

        return iRet;
    }

private:
    z_stream m_strm;
};

#endif // GZIP
