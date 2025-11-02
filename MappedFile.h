/* Copyright (C) 2016-2020 Thomas Hauck - All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

   The author would be happy if changes and
   improvements were reported back to him.

   Author:  Thomas Hauck
   Email:   Thomas@fam-hauck.de
*/

#ifndef MAPPEDFILE_H
#define MAPPEDFILE_H

#include <string>

class MappedFile
{
public:
    MappedFile();
    ~MappedFile();

    bool open(const std::wstring& filePath);
    const uint8_t* data() const;
    uint64_t size() const;
    void setOffset(const uint64_t offset);
    void addOffset(const uint64_t offset);
    uint64_t getOffset() const { return offset; }

private:
    std::wstring filePath;
    const uint8_t* mappedData;
    uint64_t fileSize;
    uint64_t offset;
};

#endif // MAPPEDFILE_H
