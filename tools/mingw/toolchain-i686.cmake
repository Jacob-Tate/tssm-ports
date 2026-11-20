# Cross-compiling the PC port for 32-bit Windows with MinGW-w64 GCC from Linux
# (Debian/Ubuntu: apt install mingw-w64; Fedora: dnf install mingw32-gcc-c++):
#
#   cmake -S . -B build-mingw32-cross -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=tools/mingw/toolchain-i686.cmake
#   cmake --build build-mingw32-cross
#
# toolchain-common.cmake has the rest.
set(TSSM_MINGW_TRIPLET i686-w64-mingw32)
include(${CMAKE_CURRENT_LIST_DIR}/toolchain-common.cmake)
