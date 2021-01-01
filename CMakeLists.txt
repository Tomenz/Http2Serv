cmake_minimum_required(VERSION 3.10)

project(Http2Serv VERSION 1.0.0)

ADD_DEFINITIONS(-DUNICODE)
ADD_DEFINITIONS(-D_UNICODE)
ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)

if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC") OR WIN32)
    ADD_DEFINITIONS(-DZLIB_WINAPI)
endif()

# specify the C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(BROTLI_BUNDLED_MODE ON)
set(BROTLI_DISABLE_TESTS TRUE)

set(SKIP_INSTALL_ALL TRUE)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/SocketLib)
include_directories(${CMAKE_SOURCE_DIR}/brotli/c/include)
include_directories(${CMAKE_SOURCE_DIR}/zlib)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/zlib)
include_directories(${CMAKE_SOURCE_DIR}/SrvLib)

add_subdirectory(brotli)
add_subdirectory(CommonLib)
add_subdirectory(FastCgi)
add_subdirectory(SocketLib)
add_subdirectory(SrvLib)
add_subdirectory(zlib)

if(UA_ENABLE_ENCRYPTION_OPENSSL)
    link_directories(${OPENSSL_LIB_DIR})
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_compile_options(/EHa)
endif()

set(targetSrc
        ${CMAKE_CURRENT_LIST_DIR}/ConfFile.cpp
        ${CMAKE_CURRENT_LIST_DIR}/HPack.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Http2Serv.cpp
        ${CMAKE_CURRENT_LIST_DIR}/HttpServ.cpp
        ${CMAKE_CURRENT_LIST_DIR}/LogFile.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SpawnProcess.cpp
        ${CMAKE_CURRENT_LIST_DIR}/Trace.cpp
)
add_executable(${PROJECT_NAME} ${targetSrc})
target_link_libraries(${PROJECT_NAME} CommonLib)
target_link_libraries(${PROJECT_NAME} srvlib)
target_link_libraries(${PROJECT_NAME} socketlib)
target_link_libraries(${PROJECT_NAME} FastCgi)
target_link_libraries(${PROJECT_NAME} brotlienc-static)
target_link_libraries(${PROJECT_NAME} zlibstatic)
if(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC") OR WIN32)
if(UA_ENABLE_ENCRYPTION_OPENSSL)
    target_link_libraries(${PROJECT_NAME} libcrypto)
    target_link_libraries(${PROJECT_NAME} libssl)
endif()
else()
if(UA_ENABLE_ENCRYPTION_OPENSSL)
    target_link_libraries(${PROJECT_NAME} crypto)
    target_link_libraries(${PROJECT_NAME} ssl)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY msg certs DESTINATION bin)
install(FILES server-dist.cfg DESTINATION bin RENAME server.cfg)
