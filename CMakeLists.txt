cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0091 NEW)

project(Http2Serv VERSION 1.0.0)

enable_language(CXX)

ADD_DEFINITIONS(-DUNICODE)
ADD_DEFINITIONS(-D_UNICODE)
if (WIN32)
    # Turn off Microsofts "security" warnings.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CONSOLE _SCL_SECURE_NO_WARNINGS NOGDICAPMASKS NOVIRTUALKEYCODES NOWINMESSAGES NOWINSTYLES NOSYSMETRICS NOMENUS NOICONS NOKEYSTATES NOSYSCOMMANDS NORASTEROPS NOSHOWWINDOW OEMRESOURCE NOATOM NOCLIPBOARD NOCOLOR NOCTLMGR NODRAWTEXT NOGDI NOKERNEL NOUSER NONLS NOMB NOMEMMGR NOMETAFILE NOMINMAX NOMSG NOOPENFILE NOSCROLL NOSOUND NOTEXTMETRIC NOWH NOWINOFFSETS NOCOMM NOKANJI NOHELP NOPROFILER NODEFERWINDOWPOS NOMCX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHa")
    #set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_compile_options("/EHa")
else()
    SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -ggdb -D_DEBUG -DDEBUG")
    add_compile_options("-Wall" "-Wpedantic" "-Wextra" "-fexceptions")
endif()

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(BROTLI_BUNDLED_MODE ON)
set(BROTLI_DISABLE_TESTS TRUE)
set(BUILD_SHARED_LIBS OFF)
mark_as_advanced(BUILD_SHARED_LIBS)


set(SKIP_INSTALL_ALL TRUE)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/SocketLib)
include_directories(${CMAKE_SOURCE_DIR}/brotli/c/include)
include_directories(${CMAKE_SOURCE_DIR}/zlib)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/zlib)
include_directories(${CMAKE_SOURCE_DIR}/SrvLib)

add_subdirectory(brotli EXCLUDE_FROM_ALL)
add_subdirectory(CommonLib EXCLUDE_FROM_ALL)
add_subdirectory(FastCgi EXCLUDE_FROM_ALL)
add_subdirectory(SocketLib EXCLUDE_FROM_ALL)
add_subdirectory(SrvLib EXCLUDE_FROM_ALL)
add_subdirectory(zlib EXCLUDE_FROM_ALL)

if(UA_ENABLE_ENCRYPTION_OPENSSL)
  link_directories(${OPENSSL_LIB_DIR})

  include_directories(${OPENSSL_INCLUDE_DIR})
endif()

if(UA_ENABLE_ENCRYPTION_OPENSSL)
  set(LIBCRYPTO "crypto")
  set(LIBSSL "ssl")
  if (MSVC)
    set(LIBCRYPTO "libcrypto")
    set(LIBSSL "libssl")
  endif()
else()
  set(LIBCRYPTO "")
  set(LIBSSL "")
endif()

set(targetSrc
        ConfFile.cpp
        HPack.cpp
        Http2Serv.cpp
        HttpServ.cpp
        LogFile.cpp
        SpawnProcess.cpp
        Trace.cpp
)

add_executable(${PROJECT_NAME} ${targetSrc})

target_link_libraries(${PROJECT_NAME} CommonLib)
target_link_libraries(${PROJECT_NAME} srvlib)
target_link_libraries(${PROJECT_NAME} socketlib ${LIBCRYPTO} ${LIBSSL})
target_link_libraries(${PROJECT_NAME} FastCgi)
target_link_libraries(${PROJECT_NAME} brotlicommon brotlienc zlibstatic)

add_executable(Http2Proxy Http2Proxy.cpp ConfFile.cpp)
target_link_libraries(Http2Proxy socketlib srvlib ${LIBCRYPTO} ${LIBSSL})

add_executable(Http2Fetch Http2Fetch.cpp HttpFetch.cpp HPack.cpp ConfFile.cpp)
target_link_libraries(Http2Fetch socketlib srvlib ${LIBCRYPTO} ${LIBSSL} zlibstatic brotlicommon brotlidec)

if(NOT MSVC AND UA_ENABLE_ENCRYPTION_OPENSSL)
  target_link_libraries(${PROJECT_NAME} pthread)
  target_link_libraries(Http2Proxy pthread)
  target_link_libraries(Http2Fetch pthread)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  file(READ SrvLib/init.d/examplesrv FILE_CONTENTS)
  string(REPLACE "ExampleSrv" ${PROJECT_NAME} FILE_CONTENTS ${FILE_CONTENTS})
  string(REPLACE "=~" "=${CMAKE_CURRENT_BINARY_DIR}" FILE_CONTENTS ${FILE_CONTENTS})
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/init.d/http2servsrv ${FILE_CONTENTS})

  file(READ SrvLib/example.service FILE_CONTENTS)
  string(REPLACE "ExampleSrv" ${PROJECT_NAME} FILE_CONTENTS ${FILE_CONTENTS})
  string(REPLACE "=~" "=${CMAKE_CURRENT_BINARY_DIR}" FILE_CONTENTS ${FILE_CONTENTS})
  string(REPLACE "Some Example server" "Http Server by T.Hauck" FILE_CONTENTS ${FILE_CONTENTS})
  string(REPLACE "example" "http2serv" FILE_CONTENTS ${FILE_CONTENTS})
  string(REPLACE "# User=root" "User=www-data" FILE_CONTENTS ${FILE_CONTENTS})
  string(REPLACE "# Group=root" "Group=www-data" FILE_CONTENTS ${FILE_CONTENTS})
  string(REPLACE "# AmbientCapabilities" "AmbientCapabilities" FILE_CONTENTS ${FILE_CONTENTS})
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/http2serv.service ${FILE_CONTENTS})
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY msg certs DESTINATION bin)
install(FILES server-dist.cfg DESTINATION bin RENAME server.cfg)
