#pragma once

#include <string>

using namespace std;

#define base64_chars "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
#define base64url_chars "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"

class Base64
{
public:
    static string Encode(const char *szInput, size_t nLen, bool bUrlSave = false)
    {
        string strRet;
        const string& base64chr = bUrlSave == false ? base64_chars : base64url_chars;

        size_t nIndex = 0, nPos = 0;
        while (nLen--)
        {
            if (++nPos, ++nIndex == 3)
            {
                nIndex = 0;
                strRet += base64chr[(*(szInput + nPos - 3) & 0xfc) >> 2];
                strRet += base64chr[((*(szInput + nPos - 3) & 0x03) << 4) + ((*(szInput + nPos - 2) & 0xf0) >> 4)];
                strRet += base64chr[((*(szInput + nPos - 2) & 0x0f) << 2) + ((*(szInput + nPos - 1) & 0xc0) >> 6)];
                strRet += base64chr[(*(szInput + nPos - 1) & 0x3f)];
            }
        }

        if (nIndex)
        {
            const size_t nOffset = 3 - nIndex;

            strRet += base64chr[(*(szInput + nPos + nOffset - 3) & 0xfc) >> 2];
            strRet += base64chr[((*(szInput + nPos + nOffset - 3) & 0x03) << 4) + (nIndex > 1 ? ((*(szInput + nPos + nOffset - 2) & 0xf0) >> 4) : 0)];
            if (nIndex > 1 && ((*(szInput + nPos + nOffset - 2) & 0x0f) << 2) != 0)
                strRet += base64chr[((*(szInput + nPos + nOffset - 2) & 0x0f) << 2) + (nIndex > 2 ? ((*(szInput + nPos + nOffset - 1) & 0xc0) >> 6) : 0)];
            if (nIndex > 2 && (*(szInput + nPos + nOffset - 1) & 0x3f) != 0)
                strRet += base64chr[(*(szInput + nPos + nOffset - 1) & 0x3f)];

            while ((nIndex++ < 3))
                strRet += '=';
        }

        return strRet;
    }

    static string Decode(string const& strInput, bool bUrlSave = false)
    {
        size_t nLen = strInput.size(), nPos = 0;
        string strRet;
        const string& base64chr = bUrlSave == false ? base64_chars : base64url_chars;

        size_t nIndex = 0;
        while (nLen && strInput[nPos] != '=')
        {
            nLen--;
            if (base64chr.find(strInput[nPos]) == string::npos)
                return string();

            if (++nPos, ++nIndex == 4)
            {
                nIndex = 0;
                char c1 = static_cast<char>(base64chr.find(strInput[nPos - 4]));
                char c2 = static_cast<char>(base64chr.find(strInput[nPos - 3]));
                char c3 = static_cast<char>(base64chr.find(strInput[nPos - 2]));
                char c4 = static_cast<char>(base64chr.find(strInput[nPos - 1]));

                strRet += (c1 << 2) + ((c2 & 0x30) >> 4);
                strRet += ((c2 & 0xf) << 4) + ((c3 & 0x3c) >> 2);
                strRet += ((c3 & 0x3) << 6) + c4;
            }
        }

        if (nLen > 0 && nLen - (4 - nIndex) != 0)
            return string();

        if (nIndex)
        {
            size_t nOffset = 4 - nIndex;
            char c1 = static_cast<char>(base64chr.find(strInput[nPos + nOffset - 4]));
            char c2 = static_cast<char>(nIndex > 1 ? base64chr.find(strInput[nPos + nOffset - 3]) : 0);
            char c3 = static_cast<char>(nIndex > 2 ? base64chr.find(strInput[nPos + nOffset - 2]) : 0);
            char c4 = static_cast<char>(0);

            strRet += (c1 << 2) + ((c2 & 0x30) >> 4);
            if (((c2 & 0xf) << 4) + ((c3 & 0x3c) >> 2) != 0)
                strRet += ((c2 & 0xf) << 4) + ((c3 & 0x3c) >> 2);
            if (((c3 & 0x3) << 6) + c4 != 0)
                strRet += ((c3 & 0x3) << 6) + c4;
        }

        return strRet;
    }
};
