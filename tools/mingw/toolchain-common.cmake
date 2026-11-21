# Shared by toolchain-x86_64.cmake and toolchain-i686.cmake: cross-compiling
# the PC port for Windows with MinGW-w64 GCC from another system (Linux).
# TSSM_MINGW_TRIPLET is set by the file that includes this one.

set(CMAKE_SYSTEM_NAME Windows)
string(REGEX REPLACE "-.*" "" CMAKE_SYSTEM_PROCESSOR "${TSSM_MINGW_TRIPLET}")

# Debian and Ubuntu install each compiler twice, -win32 and -posix (the thread
# model); the plain name is whichever the alternatives system points at.
# iLowHeap.cpp's std::mutex needs the posix one under GCC before 13, so it is
# asked for first. Fedora's and Arch's compilers are posix under the plain name.
find_program(CMAKE_C_COMPILER NAMES ${TSSM_MINGW_TRIPLET}-gcc-posix ${TSSM_MINGW_TRIPLET}-gcc)
find_program(CMAKE_CXX_COMPILER NAMES ${TSSM_MINGW_TRIPLET}-g++-posix ${TSSM_MINGW_TRIPLET}-g++)
find_program(CMAKE_RC_COMPILER NAMES ${TSSM_MINGW_TRIPLET}-windres windres)
if(NOT CMAKE_C_COMPILER OR NOT CMAKE_CXX_COMPILER)
  message(FATAL_ERROR "no ${TSSM_MINGW_TRIPLET}-gcc/g++ on PATH (Debian/Ubuntu: apt install mingw-w64)")
endif()

# Libraries and headers for the target only (an FFmpeg prefix goes in with
# -DCMAKE_FIND_ROOT_PATH=<prefix>, which this appends to); programs from the
# build machine.
list(APPEND CMAKE_FIND_ROOT_PATH /usr/${TSSM_MINGW_TRIPLET} /usr/${TSSM_MINGW_TRIPLET}/sys-root/mingw)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
