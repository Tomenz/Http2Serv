cmake_minimum_required(VERSION 3.10)

project(CommonLib)

ADD_DEFINITIONS(-DUNICODE)
ADD_DEFINITIONS(-D_UNICODE)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(targetSrc
        ${CMAKE_CURRENT_LIST_DIR}/md5.cpp
        ${CMAKE_CURRENT_LIST_DIR}/sha256.cpp
        ${CMAKE_CURRENT_LIST_DIR}/UrlCode.cpp
)

add_library(CommonLib STATIC ${targetSrc})

install(TARGETS CommonLib DESTINATION lib)
#install(FILES SocketLib.h DESTINATION include)
