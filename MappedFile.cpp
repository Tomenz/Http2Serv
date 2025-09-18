
#include <stdexcept>
#include <string>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   define FN_CA(x) x.c_str()
#else
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#   include <sys/mman.h>
#   include <fcntl.h>
#   include <unistd.h>
#   include <locale>
#   include <codecvt>
#   define FN_CA(x) std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(x).c_str()
#endif

#include "MappedFile.h"

MappedFile::MappedFile() : mappedData(nullptr), fileSize(0), offset(0)
{
}

MappedFile::~MappedFile()
{
    if (mappedData) {
#ifdef _WIN32
        UnmapViewOfFile(mappedData);
#else
        munmap(const_cast<uint8_t*>(mappedData), fileSize);
#endif
    }
}

bool MappedFile::open(const std::wstring& fileName)
{
    filePath = fileName;

#ifdef _WIN32
    HANDLE hFile = CreateFile(FN_CA(filePath), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0)
    {
        CloseHandle(hFile);
        return false;
    }

    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, fileSize, NULL);
    if (!hMap)
    {
        CloseHandle(hFile);
        fileSize = 0;
        return false;
    }

    mappedData = (const uint8_t *)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, fileSize);

    /* We can call CloseHandle here, but it will not be closed until
     * we unmap the view */
    CloseHandle(hMap);
    CloseHandle(hFile);

#else
    int fd = ::open(FN_CA(filePath), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;

    fileSize = lseek64(fd, 0, SEEK_END);
    if (fileSize > INT64_MAX) {
        close(fd);
        return false;
    }

    lseek64(fd, 0, SEEK_SET);

    mappedData = static_cast<const uint8_t*>(mmap(nullptr, fileSize, PROT_READ, MAP_SHARED, fd, 0));

    close(fd);

    if (mappedData == MAP_FAILED) {
        mappedData = nullptr;
        fileSize = 0;
        return false;
    }

#endif
    return true;
}

const uint8_t* MappedFile::data() const
{
    return mappedData + offset;
}

uint64_t MappedFile::size() const
{
    return fileSize - offset;
}

void MappedFile::setOffset(const uint64_t off)
 {
    offset = off;
 }
