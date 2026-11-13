# tssm-memtest: a standalone libnx .nro that checks, on a real console, that
# the port can get read/write memory below 4 GB (see source/main.c).
# Not part of the port's build. Configure with devkitPro's Switch.cmake:
#   cmake -S . -B build -G "Unix Makefiles" \
#       -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake
#   cmake --build build
cmake_minimum_required(VERSION 3.13)
project(tssm_memtest C)

add_executable(tssm-memtest source/main.c)
target_compile_options(tssm-memtest PRIVATE -O2 -Wall -Wextra)
target_link_libraries(tssm-memtest PRIVATE nx)

nx_generate_nacp(tssm-memtest.nacp
    NAME "TSSM memory test"
    AUTHOR "TSSM port"
    VERSION "0.1.0")
nx_create_nro(tssm-memtest NACP tssm-memtest.nacp)
