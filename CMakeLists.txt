cmake_minimum_required(VERSION 3.21)
project(Luxon)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add compile options for better error messages and warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Functionality checks
include(CheckSymbolExists)
include(CheckCSourceCompiles)

if (WIN32)
    set(CMAKE_REQUIRED_LIBRARIES ws2_32)
    check_symbol_exists(inet_pton "winsock2.h;ws2tcpip.h" HAS_PTON)
endif()

check_symbol_exists(inet_pton "sys/types.h;sys/socket.h;netinet/in.h;arpa/inet.h" HAS_PTON)

check_c_source_compiles("
    #ifdef _WIN32
        #include <winsock2.h>
        #include <ws2tcpip.h>
    #else
        #include <sys/types.h>
        #include <sys/socket.h>
        #include <netdb.h>
    #endif

    int main() {
        struct sockaddr sa;
        char host[1025];
        char service[32];

        // We explicitly use the flags here.
        // If they are missing, compilation fails and HAS_GETNAMEINFO becomes false.
        getnameinfo(&sa, sizeof(sa), host, sizeof(host), service, sizeof(service),
                    NI_NUMERICHOST | NI_NUMERICSERV);

        return 0;
    }
" HAS_GETNAMEINFO)

# Encryption library
option(LUXON_USE_TOMCRYPT "Use bundled tomcrypt for cryptography within Luxon" OFF)
if (LUXON_USE_TOMCRYPT)
    set(libtommath_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dummy" CACHE PATH "Dummy libtommath dir")
    set(WITH_LTM ON CACHE BOOL "Force LTM" FORCE)
    set(LTC_CFLAGS "-DLTC_RIJNDAEL -DLTC_SHA256 -DLTC_DH -DLTC_CBC_MODE -DLTC_SPRNG" CACHE STRING "Force LibTomCrypt flags" FORCE)
    add_subdirectory(libtommath)
    add_subdirectory(libtomcrypt)
else()
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(mbedtls)
endif()

# Platform socket libs
set(LUXON_PLATFORM_LIBS "")
if(WIN32)
    list(APPEND LUXON_PLATFORM_LIBS ws2_32)
endif()

# luxon_ser library
add_library(luxon_ser
    include/luxon/ser_buffer.hpp
    include/luxon/ser_encryption.hpp
    include/luxon/ser_types.hpp src/ser/ser_types.cpp
    include/luxon/ser_interface.hpp src/ser/ser_interface.cpp
    include/luxon/ser_gp_binary_v18.hpp src/ser/ser_gp_binary_v18.cpp
    include/luxon/ser_gp_binary_v16.hpp src/ser/ser_gp_binary_v16.cpp
)
if (LUXON_USE_TOMCRYPT)
    target_sources(luxon_ser PRIVATE src/ser/ser_encryption_tomcrypt.cpp)
    target_link_libraries(luxon_ser PRIVATE libtomcrypt)
else()
    target_sources(luxon_ser PRIVATE src/ser/ser_encryption.cpp)
    target_link_libraries(luxon_ser PRIVATE mbedtls)
endif()

target_precompile_headers(luxon_ser PRIVATE
    <luxon/ser_types.hpp>
)

target_include_directories(luxon_ser
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/luxon
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# luxon_enet library
add_library(luxon_enet
    include/luxon/enet_peer.hpp

    include/luxon/enet_protocol.hpp src/enet/enet_protocol.cpp
    src/enet/enet_endpoint.cpp
    src/enet/udp_socket.cpp
    src/enet/enet_channel.cpp
    src/enet/enet_peer_core.cpp
    src/enet/enet_server.cpp
)

target_link_libraries(luxon_enet PUBLIC
    ${LUXON_PLATFORM_LIBS}
)

target_include_directories(luxon_enet
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/luxon
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(HAS_PTON)
    target_compile_definitions(luxon_enet PUBLIC LUXON_ENET_HAS_PTON PRIVATE HAS_PTON)
endif()

if(HAS_GETNAMEINFO)
    target_compile_definitions(luxon_enet PUBLIC LUXON_ENET_HAS_GETNAMEINFO PRIVATE HAS_GETNAMEINFO)
endif()

# luxon_http_parser library
add_library(luxon_http_parser
    include/luxon/http_parser.hpp src/http_parser.cpp
)

target_include_directories(luxon_http_parser
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/luxon
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# luxon_visualizer library
add_library(luxon_visualizer
    include/luxon/visualizer.hpp
    src/visualizer.cpp
)

target_include_directories(luxon_visualizer
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/luxon
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(luxon_visualizer PRIVATE
    luxon_enet
    luxon_ser
    luxon_http_parser
)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Executables
    add_executable(luxon_parser
        apps/luxon_parser.cpp
    )
    target_link_libraries(luxon_parser
        luxon_visualizer
    )

    add_executable(luxon_ser_test
        apps/luxon_ser_test.cpp
    )
    target_link_libraries(luxon_ser_test
        luxon_ser luxon_visualizer
    )

    add_executable(luxon_test_client
        apps/luxon_test_client.cpp
    )
    target_link_libraries(luxon_test_client
        luxon_enet luxon_ser
    )

    add_executable(luxon_test_server
        apps/luxon_test_server.cpp
    )
    target_link_libraries(luxon_test_server
        luxon_enet luxon_ser luxon_visualizer
    )
endif()
