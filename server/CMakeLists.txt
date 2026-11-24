# TSSM co-op relay server. Builds on its own (no game sources or GPU libraries):
#   cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/server
cmake_minimum_required(VERSION 3.20)
project(tssm_server CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(enet
        GIT_REPOSITORY https://github.com/lsalzman/enet.git
        GIT_TAG v1.3.18
        GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(enet)

# The repository's include/ also holds game headers (intrin.h, std/...) that would shadow the compiler's own
# headers, so only the netplay protocol header is exposed: a copy kept in sync at configure time.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../include/netplay/protocol.h
        ${CMAKE_CURRENT_BINARY_DIR}/netplay_include/netplay/protocol.h COPYONLY)

foreach (tgt tssm-server tssm-server-test)
    if (tgt STREQUAL "tssm-server")
        add_executable(${tgt} main.cpp)
    else ()
        add_executable(${tgt} test_client.cpp)
    endif ()
    target_include_directories(${tgt} PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/netplay_include
            ${enet_SOURCE_DIR}/include)
    target_link_libraries(${tgt} PRIVATE enet)
    if (NOT MSVC)
        target_compile_options(${tgt} PRIVATE -Wall -Wextra)
    endif ()
    if (WIN32)
        target_link_libraries(${tgt} PRIVATE ws2_32 winmm)
    endif ()
endforeach ()

install(TARGETS tssm-server RUNTIME DESTINATION bin)
