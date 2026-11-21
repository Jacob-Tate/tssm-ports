# The PC port's build. Nothing else in this repository reads
# this file.
#
# The GameCube build is configure.py -> build.ninja -> decomp-toolkit and is
# scored by build/GGVE78/main.dol coming out byte-identical to retail. That
# build lists every object it compiles explicitly, so nothing this file adds can
# enter it, and this build never runs ninja at the root, configure.py or
# CodeWarrior. The two share source and nothing else:
#
#   * the include path decides the platform layer. Game code says
#     #include "iTime.h" and gets src/SB/Core/gc/iTime.h on the console and
#     src/SB/Core/pc/iTime.h here; neither directory is on the other's path.
#   * PLATFORM_PC is defined only here; GAMECUBE only by configure.py.
#   * an edit to shared source goes in only once the GameCube build
#     (configure.py, ninja) still makes the DOL with the retail sha1.
#
# Based on treedome's CMakeLists.txt (the BfBB PC port, CC0), with TSSM's
# differences: the game code is compiled as the same unity "wad" units the
# GameCube build compiles (read out of configure.py below), the RenderWare
# headers come from src/SB/Core/pc/rwsdk, and 64-bit is the default on Windows.
# src/SB/Core/pc/PORTING.md says where every platform file came from.

cmake_minimum_required(VERSION 3.16)

# Word size. 64-bit is the default everywhere: the game allocator's arena is
# reserved below 4 GB (src/SB/Core/pc/iMemMgr.cpp, treedome's design), so
# pointers the game keeps in 32-bit fields still fit. TSSM_BUILD_32BIT=ON
# builds the configuration treedome plays BfBB in. Read through
# CMAKE_<LANG>_FLAGS_INIT, so it has to come before project() and a build
# directory cannot be switched: configure a fresh one. (treedome explains why
# at length; the short version is that project() links a probe program.)
if(DEFINED ANDROID_ABI OR CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(TSSM_ANDROID_EARLY ON)
else()
  set(TSSM_ANDROID_EARLY OFF)
endif()

option(TSSM_BUILD_32BIT "Build 32-bit (Windows/Linux x86) instead of 64-bit" OFF)

if(TSSM_BUILD_32BIT AND (CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR TSSM_ANDROID_EARLY))
  message(FATAL_ERROR "TSSM_BUILD_32BIT is not available on this target.")
endif()

if(TSSM_BUILD_32BIT)
  set(CMAKE_C_FLAGS_INIT "-m32")
  set(CMAKE_CXX_FLAGS_INIT "-m32")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
  set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")
  set(CMAKE_MODULE_LINKER_FLAGS_INIT "-m32")
endif()

project(tssm-pc C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(TSSM_BUILD_32BIT AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "TSSM_BUILD_32BIT is on, but ${CMAKE_CXX_COMPILER_ID} produced a "
    "${CMAKE_SIZEOF_VOID_P}-byte pointer. Enter the x86 MSVC environment first "
    "(MinGW: configure from MSYS2's MINGW32 shell, whose gcc builds 32-bit).")
endif()

# Every warning off for code this project does not own the warnings of: the
# game is decompiled, librw and SDL are third-party. GCC needs -Wno-narrowing on
# top of -w (treedome: xFont's character tables).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(TSSM_NO_WARNINGS -w -Wno-narrowing)
else()
  set(TSSM_NO_WARNINGS -Wno-everything)
endif()

# Microsoft's CRT and STL, which is what decides the defines below -- not the
# OS or the compiler. clang++ targeting the MSVC ABI leaves MSVC false.
if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
  set(TSSM_MSVC_ABI ON)
else()
  set(TSSM_MSVC_ABI OFF)
endif()

set(PC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Core/pc)
set(SB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/SB)

# Mandatory, not style: the source is full of *(U32*)&someFloat.
add_compile_options(-fno-strict-aliasing)

# Also mandatory under GCC: the game clears an object's memory and then
# constructs it in place (zLoadingScreen::Init: memset, then zUIImage_Init's
# placement new), and relies on the fields the constructor leaves alone
# staying zero. GCC treats storage as dead until a constructor runs and drops
# the memset; zUI::custom keeps the allocator's fill, and zUI_Init calls
# through it on the first level load. clang does no such thing.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-lifetime-dse>)
endif()

# 32-bit x86 floating point in SSE2 registers, as clang's code for the MSVC-ABI
# x86 build has it, not in the x87's 80-bit ones, which GCC uses by default
# there: whether -m32 asked for 32-bit (TSSM_BUILD_32BIT) or the compiler only
# builds 32-bit (MSYS2's MINGW32 gcc).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("#ifndef __i386__\n#error not 32-bit x86\n#endif\nint main() { return 0; }"
                            TSSM_GNU_X86_32)
  if(TSSM_GNU_X86_32)
    add_compile_options(-msse2 -mfpmath=sse)
  endif()
endif()

# __FILE__ relative to the checkout (librw's messages and assertions carry
# it), so the executable does not carry the folder it was built in: GCC, and
# clang++ (the MSVC-ABI build and every other).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
   (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=>")
endif()

if(MINGW)
  add_link_options(-static)
  # 32-bit MinGW's time_t is 32 bits unless asked (msvcrt's default), and ends
  # in January 2038; the MSVC x86 build's UCRT has a 64-bit one. Save dates
  # go through it (iHostWin32.cpp, iSaveFormat.cpp).
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_compile_definitions(_TIME_BITS=64)
  endif()
endif()

# The positive marker that selects the port's arms in shared headers and the
# PC RenderWare layouts in src/SB/Core/pc/rwsdk.
add_compile_definitions(PLATFORM_PC)

if(TSSM_MSVC_ABI)
  add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH _CRT_SECURE_NO_WARNINGS)
endif()

if(ANDROID)
  set(TSSM_HOST_TOOLS OFF)
else()
  set(TSSM_HOST_TOOLS ON)
endif()

# --- render backends -----------------------------------------------------------
# A list; one executable carries all of it and config.ini's video.backend picks.
if(WIN32)
  set(TSSM_RENDER_BACKENDS_DEFAULT "D3D9;D3D11;GL3")
else()
  set(TSSM_RENDER_BACKENDS_DEFAULT "GL3")
endif()
set(TSSM_RENDER_BACKENDS "${TSSM_RENDER_BACKENDS_DEFAULT}"
    CACHE STRING "Render backends, semicolon-separated: D3D9, D3D11, GL3, VULKAN, NULL")

foreach(_backend IN LISTS TSSM_RENDER_BACKENDS)
  if(NOT _backend MATCHES "^(D3D9|D3D11|GL3|VULKAN|NULL)$")
    message(FATAL_ERROR "unknown render backend '${_backend}' in TSSM_RENDER_BACKENDS")
  endif()
  set(TSSM_BACKEND_${_backend} ON)
endforeach()
if(NOT WIN32 AND (TSSM_BACKEND_D3D9 OR TSSM_BACKEND_D3D11))
  message(FATAL_ERROR "TSSM_RENDER_BACKENDS names Direct3D on ${CMAKE_SYSTEM_NAME}.")
endif()
message(STATUS "tssm: render backends: ${TSSM_RENDER_BACKENDS}")

set(TSSM_NEEDS_SDL OFF)

# --- the GUI ---------------------------------------------------------------------
# The Dear ImGui overlay and launcher (src/SB/Core/pc/gui, docs/GUI.md). It
# draws through ImGui's backend for whichever librw device runs, and needs SDL
# for the window, its events and the launcher's renderer and folder picker.
option(TSSM_GUI "Build the Dear ImGui overlay and launcher (third_party/imgui)" ON)
if(TSSM_GUI)
  set(TSSM_NEEDS_SDL ON)
endif()

# --- input -----------------------------------------------------------------------
set(TSSM_INPUT_BACKEND "sdl" CACHE STRING "Host input backend: sdl, null")
set_property(CACHE TSSM_INPUT_BACKEND PROPERTY STRINGS sdl null)
set(INPUT_LIBS)
if(TSSM_INPUT_BACKEND STREQUAL "null")
  set(INPUT_SOURCES ${PC_DIR}/iPadHostNull.cpp)
elseif(TSSM_INPUT_BACKEND STREQUAL "sdl")
  set(INPUT_SOURCES ${PC_DIR}/iPadHostSDL.cpp ${PC_DIR}/iPadKeyboardSDL.cpp
                    ${PC_DIR}/iPadTokens.cpp ${PC_DIR}/iPadTouch.cpp ${PC_DIR}/iMouseLook.cpp
                    ${PC_DIR}/iPadPrompts.cpp ${PC_DIR}/iPadTest.cpp ${PC_DIR}/iPadControls.cpp)
  set(INPUT_LIBS tssm_sdl)
  set(TSSM_NEEDS_SDL ON)
else()
  message(FATAL_ERROR "unknown TSSM_INPUT_BACKEND '${TSSM_INPUT_BACKEND}'")
endif()

# --- audio device ------------------------------------------------------------
# The device end only: pc/iSnd.cpp (the game's half) and iSndData/iSndXbox (the
# Xbox sound data) are in tssm_platform whichever backend is chosen.
set(TSSM_AUDIO_BACKEND "sdl" CACHE STRING "Host audio backend: sdl, null")
set_property(CACHE TSSM_AUDIO_BACKEND PROPERTY STRINGS sdl null)
set(AUDIO_LIBS)
if(TSSM_AUDIO_BACKEND STREQUAL "null")
  set(AUDIO_SOURCES ${PC_DIR}/iSndHostNull.cpp)
elseif(TSSM_AUDIO_BACKEND STREQUAL "sdl")
  set(AUDIO_SOURCES ${PC_DIR}/iSndHostSDL.cpp ${PC_DIR}/iAudioSDL.cpp)
  set(AUDIO_LIBS tssm_sdl)
  set(TSSM_NEEDS_SDL ON)
else()
  message(FATAL_ERROR "unknown TSSM_AUDIO_BACKEND '${TSSM_AUDIO_BACKEND}'")
endif()
list(APPEND AUDIO_SOURCES ${PC_DIR}/iSndReverb.cpp)

# --- movies (FFmpeg, optional) ------------------------------------------------
# TSSM's Xbox movies are Bink 1 (FMV/*.bik), which FFmpeg decodes. Point
# CMAKE_PREFIX_PATH at vcpkg's installed/<arch>-windows (the build scripts do),
# or under MinGW at what tools/mingw/build_ffmpeg.sh installed (static
# libraries, linked into tssm.exe). Without FFmpeg the null decoder is built
# and movies report "ran to the end".
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h)
find_library(FFMPEG_AVCODEC_LIBRARY avcodec)
find_library(FFMPEG_AVFORMAT_LIBRARY avformat)
find_library(FFMPEG_AVUTIL_LIBRARY avutil)
find_library(FFMPEG_SWSCALE_LIBRARY swscale)
find_library(FFMPEG_SWRESAMPLE_LIBRARY swresample)

if(FFMPEG_INCLUDE_DIR AND FFMPEG_AVCODEC_LIBRARY AND FFMPEG_AVFORMAT_LIBRARY
   AND FFMPEG_AVUTIL_LIBRARY AND FFMPEG_SWSCALE_LIBRARY AND FFMPEG_SWRESAMPLE_LIBRARY)
  set(FMV_SOURCES ${PC_DIR}/iFMVDecoderFFmpeg.cpp)
  set(FMV_LIBRARIES ${FFMPEG_AVFORMAT_LIBRARY} ${FFMPEG_AVCODEC_LIBRARY}
                    ${FFMPEG_SWSCALE_LIBRARY} ${FFMPEG_SWRESAMPLE_LIBRARY}
                    ${FFMPEG_AVUTIL_LIBRARY})
  set(FMV_INCLUDES ${FFMPEG_INCLUDE_DIR})
  set(FMV_DEFINES TSSM_HAVE_FFMPEG)
  # A GNU static archive (libavcodec.a, not the import library libavcodec.dll.a):
  # FFmpeg is linked in and no DLL ships. Static FFmpeg leaves the Windows
  # libraries it calls to the program (its .pc files' Libs: bcrypt for
  # libavutil's random seed, ole32 and user32).
  set(TSSM_FFMPEG_STATIC OFF)
  set(FFMPEG_SYSTEM_LIBS)
  if(FFMPEG_AVCODEC_LIBRARY MATCHES "\\.a$" AND NOT FFMPEG_AVCODEC_LIBRARY MATCHES "\\.dll\\.a$")
    set(TSSM_FFMPEG_STATIC ON)
    if(WIN32)
      set(FFMPEG_SYSTEM_LIBS bcrypt ole32 user32)
      list(APPEND FMV_LIBRARIES ${FFMPEG_SYSTEM_LIBS})
    endif()
    message(STATUS "FMV decoder: ffmpeg, static (${FFMPEG_INCLUDE_DIR})")
  else()
    message(STATUS "FMV decoder: ffmpeg (${FFMPEG_INCLUDE_DIR})")
  endif()
  # The license this FFmpeg was configured under, for the About tab: under
  # MinGW read from libavutil (the string avutil_license() returns), since the
  # FFmpeg found there may be MSYS2's package, which is configured
  # --enable-gpl --enable-version3 and links x264 and x265. What
  # tools/mingw/build_ffmpeg.sh builds is LGPL 2.1 or later, as vcpkg's is.
  set(TSSM_FFMPEG_LICENSE "LGPL 2.1 or later")
  if(MINGW)
    if(TSSM_FFMPEG_STATIC)
      set(_avutil_files "${FFMPEG_AVUTIL_LIBRARY}")
    else()
      file(GLOB _avutil_files "${FFMPEG_INCLUDE_DIR}/../bin/avutil-*.dll")
    endif()
    set(_avutil_license)
    foreach(_f IN LISTS _avutil_files)
      file(STRINGS "${_f}" _avutil_license REGEX "libavutil license: " LIMIT_COUNT 1)
      if(_avutil_license)
        break()
      endif()
    endforeach()
    if(_avutil_license)
      string(REGEX REPLACE "^.*libavutil license: " "" _avutil_license "${_avutil_license}")
      string(REPLACE "version " "" TSSM_FFMPEG_LICENSE "${_avutil_license}")
    else()
      message(WARNING "Could not read which license this FFmpeg was built under (no libavutil "
                      "license string found); the About tab says ${TSSM_FFMPEG_LICENSE}.")
    endif()
    if(NOT TSSM_FFMPEG_LICENSE MATCHES "^LGPL")
      message(WARNING
              "This FFmpeg (${FFMPEG_INCLUDE_DIR}) is licensed ${TSSM_FFMPEG_LICENSE}, not LGPL "
              "(MSYS2's FFmpeg package is GPL). A tssm.exe built with it is for your own use: handing it "
              "out would put the whole program under that license's terms. "
              "tools/mingw/build_ffmpeg.sh builds an LGPL FFmpeg that links into tssm.exe.")
    endif()
  endif()
else()
  set(FMV_SOURCES ${PC_DIR}/iFMVDecoderNull.cpp)
  set(FMV_LIBRARIES)
  set(FMV_INCLUDES)
  set(FMV_DEFINES)
  message(STATUS "FMV decoder: none (FFmpeg not found; movies will be skipped)")
endif()

if(TSSM_AUDIO_BACKEND STREQUAL "null")
  list(APPEND FMV_SOURCES ${PC_DIR}/iFMVAudioNull.cpp)
else()
  list(APPEND FMV_SOURCES ${PC_DIR}/iFMVAudioSDL.cpp)
  list(APPEND FMV_LIBRARIES tssm_sdl)
  set(TSSM_NEEDS_SDL ON)
endif()

# --- host seam ---------------------------------------------------------------------
if(WIN32)
  set(HOST_SOURCES ${PC_DIR}/iHostWin32.cpp)
else()
  set(HOST_SOURCES ${PC_DIR}/iHostPosix.cpp)
endif()

# --- window ----------------------------------------------------------------------
if(TSSM_BACKEND_D3D9 OR TSSM_BACKEND_D3D11 OR TSSM_BACKEND_GL3 OR TSSM_BACKEND_VULKAN)
  set(WINDOW_SOURCES ${PC_DIR}/iWindowSDL.cpp)
  set(WINDOW_LIBS tssm_sdl)
  set(TSSM_NEEDS_SDL ON)
else()
  set(WINDOW_SOURCES ${PC_DIR}/iWindowNull.cpp)
  set(WINDOW_LIBS)
endif()

# --- SDL3 --------------------------------------------------------------------------
# auto: an installed SDL3 if one links, third_party/SDL otherwise. MinGW
# defaults to third_party/SDL: MSYS2's SDL3 is a DLL that needs MSYS2's own
# DLLs beside it, a different version from the pinned one, and has no
# TSSM_TEST_AUDIO_RESTART hooks (below); the vendored one links into tssm.exe.
if(ANDROID OR MINGW)
  set(TSSM_SDL_DEFAULT "vendored")
else()
  set(TSSM_SDL_DEFAULT "auto")
endif()
set(TSSM_SDL "${TSSM_SDL_DEFAULT}" CACHE STRING "Where SDL3 comes from: auto, system, vendored")
set_property(CACHE TSSM_SDL PROPERTY STRINGS auto system vendored)

if(TSSM_NEEDS_SDL)
  if(NOT TSSM_SDL STREQUAL "vendored")
    find_package(SDL3 CONFIG QUIET)
  endif()

  if(TARGET SDL3::SDL3)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES SDL3::SDL3)
    set(CMAKE_REQUIRED_QUIET ON)
    check_cxx_source_compiles("#include <SDL3/SDL.h>\nint main() { SDL_Quit(); return 0; }"
                              TSSM_SDL_LINKS)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_QUIET)
    if(NOT TSSM_SDL_LINKS)
      if(TSSM_SDL STREQUAL "system")
        message(FATAL_ERROR "The SDL3 at ${SDL3_DIR} does not link with this compiler.")
      endif()
      message(STATUS "SDL3: ${SDL3_DIR} does not link (word size or ABI?); building third_party/SDL")
      set(_tssm_sdl_unusable ON)
    endif()
  endif()

  add_library(tssm_sdl INTERFACE)

  if(TARGET SDL3::SDL3 AND NOT _tssm_sdl_unusable)
    message(STATUS "SDL3: installed (${SDL3_DIR})")
    target_link_libraries(tssm_sdl INTERFACE SDL3::SDL3)
    get_target_property(_sdl_aliased SDL3::SDL3 ALIASED_TARGET)
    if(NOT _sdl_aliased)
      set(_sdl_aliased SDL3::SDL3)
    endif()
    get_target_property(_sdl_type ${_sdl_aliased} TYPE)
    if(WIN32 AND _sdl_type STREQUAL "SHARED_LIBRARY")
      set(TSSM_SDL_RUNTIME_DLL $<TARGET_FILE:${_sdl_aliased}>)
    endif()
  elseif(TSSM_SDL STREQUAL "system")
    message(FATAL_ERROR "TSSM_SDL=system, and no installed SDL3 was found.")
  else()
    message(STATUS "SDL3: building third_party/SDL")
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt)
      message(FATAL_ERROR "third_party/SDL is empty -- run: git submodule update --init --depth 1")
    endif()
    # As treedome: static, video and audio on (video also gates SDL's
    # DirectInput joystick driver), the parts nothing here uses off. The GUI
    # adds two: SDL's renderer, which the launcher draws with before the game
    # has a device, and its dialogs, for the native folder picker.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_AUDIO ON CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER ${TSSM_GUI} CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG ${TSSM_GUI} CACHE BOOL "" FORCE)
    set(SDL_POWER OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
    target_compile_options(SDL3-static PRIVATE ${TSSM_NO_WARNINGS})
    # SDL's configure finds itoa, which the Microsoft C runtime only has under
    # its old name, in oldnames.lib. Clang's driver adds that library by
    # default, but CMake's -nostdlib link for this ABI does not, so an
    # executable whose other objects do not happen to pull it in fails to link
    # once SDL is rebuilt (tssm_sndtest).
    if(WIN32 AND NOT MINGW)
      target_link_libraries(SDL3-static INTERFACE oldnames)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      set_target_properties(SDL3-static PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
    endif()
    # librw's GL3/SDL3 arm calls find_package(SDL3 CONFIG REQUIRED) unguarded;
    # an empty config file satisfies it and leaves this target in charge.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/sdl3-redirect/SDL3Config.cmake
         "# SDL3 is already a target in this build. Written by CMake.\n")
    set(SDL3_DIR ${CMAKE_CURRENT_BINARY_DIR}/sdl3-redirect CACHE PATH "" FORCE)
    target_link_libraries(tssm_sdl INTERFACE SDL3::SDL3-static)

    # TSSM_TEST_AUDIO_RESTART's lost, swap, swapfail, default, other and
    # alllost (iAudioSDLTest.h) make the calls SDL's WASAPI backend makes when
    # Windows reports a device gone or a new default. They need SDL's private
    # structures, so that part is compiled into SDL itself. It does nothing
    # unless a test calls it.
    if(WIN32 AND TSSM_AUDIO_BACKEND STREQUAL "sdl")
      target_sources(SDL3-static PRIVATE ${PC_DIR}/iAudioSDLTestSDL.c)
      set(TSSM_AUDIO_SDL_INTERNALS ON)
    endif()
  endif()
endif()

# --- librw ---------------------------------------------------------------------------
# The renderer: treedome's librw fork (branch bfbb-port), pinned.
# src/SB/Core/pc/rw mirrors its object layouts, and rw/layout*.cpp asserts them.
set(LIBRW_PLATFORM_LIST ${TSSM_RENDER_BACKENDS})
list(REMOVE_ITEM LIBRW_PLATFORM_LIST "NULL")
if(NOT LIBRW_PLATFORM_LIST)
  set(LIBRW_PLATFORM_LIST "NULL")
endif()
set(LIBRW_PLATFORM "${LIBRW_PLATFORM_LIST}" CACHE STRING "librw render backends" FORCE)
set(LIBRW_GL3_GFXLIB "SDL3" CACHE STRING "librw's GL3 windowing library" FORCE)
set(LIBRW_TOOLS OFF CACHE BOOL "" FORCE)
set(LIBRW_INSTALL OFF CACHE BOOL "" FORCE)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/CMakeLists.txt)
  message(FATAL_ERROR "third_party/librw is empty -- run: git submodule update --init --depth 1")
endif()

# The port's fixes to librw. The submodule stays pinned at the upstream commit;
# each fix is a patch in tools/librw/, applied here to the checkout. Every build
# configures this file, the Gradle/Android one included, so every build gets
# them. Applying is idempotent: a patch that reverses cleanly is already in.
# A patched source that changes on disk (a checkout in the submodule that
# reverts it) re-runs the configure, which applies the patch again. The
# superproject's `git status` shows the submodule as having modified content.
# (tools/librw_gles_on_desktop.patch is a desktop test aid and is not applied.)
option(TSSM_LIBRW_PATCHES "Apply tools/librw/*.patch to third_party/librw" ON)
if(TSSM_LIBRW_PATCHES)
  find_package(Git QUIET)
  if(NOT GIT_FOUND)
    message(FATAL_ERROR "git is needed to apply tools/librw/*.patch to third_party/librw")
  endif()
  set(_librw_dir ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw)
  file(GLOB _librw_patches ${CMAKE_CURRENT_SOURCE_DIR}/tools/librw/*.patch)
  list(SORT _librw_patches)
  # Gradle configures one tree per ABI, possibly at once; one patcher at a time.
  file(LOCK ${CMAKE_CURRENT_SOURCE_DIR}/tools/librw DIRECTORY GUARD FILE TIMEOUT 600)
  # Keep git from finding the superproject from inside librw. A librw without
  # a .git of its own (a copied tree) would otherwise be taken for a
  # subdirectory of the port's checkout, where git apply skips every path and
  # still succeeds, so the reverse check below would call each patch already in.
  # Bounded at third_party/, git sees either librw's own checkout or none.
  set(_librw_ceiling "$ENV{GIT_CEILING_DIRECTORIES}")
  set(ENV{GIT_CEILING_DIRECTORIES} ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  # Line endings, whichever git this is. The patches are LF (.gitattributes),
  # and git apply reads the files as LF only when core.autocrlf says so. Git
  # for Windows sets it to true and checks out CRLF; MSYS2's, Linux's and
  # macOS's gits leave it unset, and then no patch applies to a CRLF checkout,
  # and matfx_uvtransform.patch to none (librw stores
  # src/d3d/shaders/make_shaders.cmd with CRLF). true for a CRLF checkout and
  # input for an LF one both read the files as LF, and each writes the patched
  # files back with the checkout's own line endings.
  # (In hex: a plain file(READ) on Windows turns CRLF into LF.)
  file(READ ${_librw_dir}/CMakeLists.txt _librw_probe LIMIT 4096 HEX)
  string(REGEX MATCH "^(..)*0d0a" _librw_crlf "${_librw_probe}")
  if(NOT _librw_crlf)
    set(_librw_git ${GIT_EXECUTABLE} -c core.autocrlf=input)
  else()
    set(_librw_git ${GIT_EXECUTABLE} -c core.autocrlf=true)
  endif()
  foreach(_patch IN LISTS _librw_patches)
    get_filename_component(_name ${_patch} NAME)
    execute_process(COMMAND ${_librw_git} apply --reverse --check ${_patch}
                    WORKING_DIRECTORY ${_librw_dir} RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL 0)
      execute_process(COMMAND ${_librw_git} apply ${_patch}
                      WORKING_DIRECTORY ${_librw_dir} RESULT_VARIABLE _rc ERROR_VARIABLE _err)
      # Check it took.
      if(_rc EQUAL 0)
        execute_process(COMMAND ${_librw_git} apply --reverse --check ${_patch}
                        WORKING_DIRECTORY ${_librw_dir} RESULT_VARIABLE _rc ERROR_VARIABLE _err)
      endif()
      if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
                "tools/librw/${_name} neither applies to third_party/librw nor is in it already.\n"
                "${_err}"
                "If the submodule moved, regenerate the patch. If the patch itself changed since this "
                "checkout applied it, start from a clean librw: run git -C third_party/librw checkout -- . "
                "and git -C third_party/librw clean -fd (the second removes the files the patches add), "
                "then configure again. Both throw away edits in third_party/librw that are not in a "
                "patch yet.")
      endif()
      message(STATUS "librw: applied tools/librw/${_name}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_patch})
    file(STRINGS ${_patch} _targets REGEX "^\\+\\+\\+ b/")
    foreach(_target IN LISTS _targets)
      string(REGEX REPLACE "^\\+\\+\\+ b/([^\t]*).*$" "\\1" _target "${_target}")
      string(STRIP "${_target}" _target)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_librw_dir}/${_target})
    endforeach()
  endforeach()
  set(ENV{GIT_CEILING_DIRECTORIES} "${_librw_ceiling}")
  file(LOCK ${CMAKE_CURRENT_SOURCE_DIR}/tools/librw DIRECTORY RELEASE)
endif()

if(TSSM_BACKEND_VULKAN)
  foreach(_dep Vulkan-Headers volk VulkanMemoryAllocator)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_dep}/CMakeLists.txt)
      message(FATAL_ERROR "third_party/${_dep} is empty -- run: git submodule update --init --depth 1")
    endif()
  endforeach()
  set(VULKAN_HEADERS_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers
      CACHE PATH "" FORCE)
  add_subdirectory(third_party/Vulkan-Headers EXCLUDE_FROM_ALL)
  add_subdirectory(third_party/volk EXCLUDE_FROM_ALL)
  set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/VulkanMemoryAllocator EXCLUDE_FROM_ALL)
  target_compile_options(volk PRIVATE ${TSSM_NO_WARNINGS})
endif()

add_subdirectory(third_party/librw)
target_compile_options(librw PRIVATE ${TSSM_NO_WARNINGS} -Werror=switch)

# librw's Debug configuration defines DEBUG, and under DEBUG its Direct3D 11
# device asks for the D3D11 debug layer (D3D11_CREATE_DEVICE_DEBUG in
# d3d11device.cpp). That layer exists only where Windows' optional "Graphics
# Tools" feature is installed; without it no device is made, librw does not
# notice, and a Debug build faults at startup in the first shader it creates.
# So that one file is compiled without DEBUG unless the layer is asked for.
option(TSSM_D3D11_DEBUG_LAYER
       "Debug builds: ask Direct3D 11 for its debug layer (needs Windows' Graphics Tools)" OFF)
if(TSSM_BACKEND_D3D11 AND NOT TSSM_D3D11_DEBUG_LAYER)
  if(CMAKE_VERSION VERSION_LESS 3.18)
    message(WARNING "CMake ${CMAKE_VERSION} cannot set a librw source's options from here; a Debug "
                    "build with D3D11 will need Windows' Graphics Tools (CMake 3.18 or later "
                    "does not)")
  else()
    set_source_files_properties(
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/src/d3d/d3d11device.cpp
      DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/src
      PROPERTIES COMPILE_OPTIONS "-UDEBUG")
  endif()
endif()

# librw asks for libm whenever the compiler is GNU or Clang; under the MSVC ABI
# there is no m.lib (the maths is in the CRT).
if(TSSM_MSVC_ABI)
  get_target_property(_librw_link_interface librw INTERFACE_LINK_LIBRARIES)
  if(_librw_link_interface)
    list(REMOVE_ITEM _librw_link_interface "$<LINK_ONLY:m>")
    set_target_properties(librw PROPERTIES INTERFACE_LINK_LIBRARIES "${_librw_link_interface}")
  endif()
endif()

# --- include path ----------------------------------------------------------------
# compat FIRST: it shadows include/types.h, the MSL headers the game includes
# by their CodeWarrior paths, the Dolphin headers (deliberately empty) and the
# C library headers that need CodeWarrior's extensions (include_next chains).
# Then the platform layer and its RenderWare headers, then configure.py's game
# order (include, bink, inline, x, Game). include/rwsdk, src/SB/Core/gc,
# MSL and the Dolphin SDK are never on this path.
set(TSSM_INCLUDES
  ${PC_DIR}/compat
  ${PC_DIR}
  ${PC_DIR}/rwsdk
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/bink
  ${CMAKE_CURRENT_SOURCE_DIR}/include/inline
  ${SB_DIR}/Core/x
  ${SB_DIR}/Game
)

# --- the RenderWare C API on librw ----------------------------------------------
# CONFIGURE_DEPENDS here and on the stubs below: a file added to or removed from
# either folder is picked up by the next build of an existing build directory,
# instead of failing on a source that is no longer there.
file(GLOB RW_SHIM_SOURCES CONFIGURE_DEPENDS ${PC_DIR}/rw/*.cpp)
add_library(tssm_rw STATIC ${RW_SHIM_SOURCES} ${WINDOW_SOURCES} ${PC_DIR}/iScreen.cpp
  ${PC_DIR}/iTouchOverlay.cpp ${PC_DIR}/iPerfSampler.cpp)
target_include_directories(tssm_rw PUBLIC ${TSSM_INCLUDES})
target_link_libraries(tssm_rw PUBLIC librw ${WINDOW_LIBS})
target_compile_options(tssm_rw PRIVATE ${TSSM_NO_WARNINGS})
# TSSM_TEST_HANG_DUMP's heartbeat (iHangDump.h; the dump is in tssm_main.cpp).
target_sources(tssm_rw PRIVATE ${PC_DIR}/iHangDump.cpp)
# The loading screen's pump and its frame flag (iLoadFrame.h; drawn by iAsync.cpp).
target_sources(tssm_rw PRIVATE ${PC_DIR}/iLoadFrame.cpp)
# The Xbox water's tiles and strips (iXboxWaterGrid.h), for rw/xboxwater.cpp and the
# game half alike.
target_sources(tssm_rw PRIVATE ${PC_DIR}/iXboxWaterGrid.cpp)

# --- the program's name and version ------------------------------------------------
# TSSM_PC_TITLE is what the program calls itself: the launcher's and the
# game's window titles, the launcher's heading, the overlay's title, the About
# tab's heading, every message box's caption, the first line of config.ini and
# of the save tools' help, and the name in tssm.exe's version information
# (Explorer's Properties, Task Manager, the volume mixer; with TSSM_PC_VERSION).
# The game's own screens come from the player's game files and are not
# touched. TSSM_VERSION_LINE is the About tab's version line. The TSSM
# Dehydrated release sets all three (src/SB/Core/pc/README.md, "Building the
# TSSM Dehydrated release").
set(TSSM_PC_TITLE "The SpongeBob SquarePants Movie Video Game" CACHE STRING
  "The program's name: window titles, the launcher, the overlay, message boxes, tssm.exe's version information")
set(TSSM_PC_VERSION "0.9.5" CACHE STRING
  "The version in tssm.exe's version information: up to four numbers, such as 0.9.1")
set(TSSM_VERSION_LINE "" CACHE STRING
  "The About tab's version line; empty: the branch and the build's commit")
if(TSSM_PC_TITLE STREQUAL "" OR TSSM_PC_TITLE MATCHES "[\"\\\\%]")
  message(FATAL_ERROR "TSSM_PC_TITLE is \"${TSSM_PC_TITLE}\": give a name without double quotes, "
    "backslashes or percent signs.")
endif()
if(NOT TSSM_PC_VERSION MATCHES "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?(\\.[0-9]+)?$")
  message(FATAL_ERROR "TSSM_PC_VERSION is \"${TSSM_PC_VERSION}\": give up to four numbers, such as 0.9.1.")
endif()
if(TSSM_VERSION_LINE MATCHES "[\"\\\\]")
  message(FATAL_ERROR "TSSM_VERSION_LINE has a double quote or a backslash.")
endif()
message(STATUS "Program name: \"${TSSM_PC_TITLE}\", version ${TSSM_PC_VERSION}")

# What this build is (src/SB/Core/pc/iBuildInfo.h): its time, version and git
# commit, written by tools/build_info.cmake on every build, so crash.log's and
# last_run.log's "built" stamp and the About tab name the build they are in.
find_package(Git QUIET)
set(TSSM_BUILD_INFO_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/iBuildInfo.cpp)
add_custom_target(tssm_buildinfo_stamp
  COMMAND ${CMAKE_COMMAND} "-DOUT=${TSSM_BUILD_INFO_SOURCE}" "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}"
          "-DVERSION=${TSSM_PC_VERSION}" "-DGIT=${GIT_EXECUTABLE}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_info.cmake
  BYPRODUCTS ${TSSM_BUILD_INFO_SOURCE}
  VERBATIM)
add_library(tssm_buildinfo STATIC ${TSSM_BUILD_INFO_SOURCE})
add_dependencies(tssm_buildinfo tssm_buildinfo_stamp)
target_include_directories(tssm_buildinfo PRIVATE ${PC_DIR})

# --- Dear ImGui and the GUI -----------------------------------------------------------
# third_party/imgui is Dear ImGui (MIT), pinned to a release tag. One library
# carries it and the backends this build can use: SDL3 for the platform side,
# SDL_Renderer for the launcher, and the renderer that matches each librw device
# in TSSM_RENDER_BACKENDS (DX9, DX11, OpenGL 3 / OpenGL ES 3 on Android).
# Vulkan has no ImGui renderer here yet; the overlay reports that and stays off.
if(TSSM_GUI)
  set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
  if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR "third_party/imgui is empty -- run: git submodule update --init --depth 1")
  endif()

  add_library(tssm_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  )
  target_include_directories(tssm_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_compile_definitions(tssm_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  target_compile_options(tssm_imgui PRIVATE ${TSSM_NO_WARNINGS})
  target_link_libraries(tssm_imgui PUBLIC tssm_sdl)

  if(TSSM_BACKEND_D3D9)
    target_sources(tssm_imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp)
    target_link_libraries(tssm_imgui PUBLIC d3d9)
  endif()
  if(TSSM_BACKEND_D3D11)
    target_sources(tssm_imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
    target_link_libraries(tssm_imgui PUBLIC d3d11 dxgi d3dcompiler)
  endif()
  if(TSSM_BACKEND_GL3)
    target_sources(tssm_imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    if(ANDROID)
      # librw's GL3 device asks SDL for an OpenGL ES 3 context there.
      target_compile_definitions(tssm_imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
      target_link_libraries(tssm_imgui PUBLIC GLESv3)
    endif()
  endif()

  set(GUI_SOURCES
    ${PC_DIR}/gui/iGui.cpp
    ${PC_DIR}/gui/iGuiCheats.cpp
    ${PC_DIR}/gui/iGuiControls.cpp
    ${PC_DIR}/gui/iGuiLauncher.cpp
    ${PC_DIR}/gui/iGuiMods.cpp
    ${PC_DIR}/gui/iGuiRender.cpp
    ${PC_DIR}/gui/iGuiSaves.cpp
    ${PC_DIR}/gui/iGuiSelfTest.cpp
    ${PC_DIR}/gui/iGuiSettings.cpp
    ${PC_DIR}/gui/iGuiTabs.cpp
    ${PC_DIR}/gui/iGuiUtil.cpp
  )

  # The About tab's version line: TSSM_VERSION_LINE when given (a release
  # build's version), else the branch at configure time and the build stamp's
  # commit (iBuildCommit), so it matches the "Built ... from" line under it.
  if(TSSM_VERSION_LINE)
    set_source_files_properties(${PC_DIR}/gui/iGuiTabs.cpp PROPERTIES
      COMPILE_DEFINITIONS "TSSM_GUI_VERSION=\"${TSSM_VERSION_LINE}\"")
  else()
    execute_process(COMMAND git rev-parse --abbrev-ref HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE _tssm_branch OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_tssm_branch MATCHES "[\"\\\\]")
      set(_tssm_branch "")
    endif()
    set_source_files_properties(${PC_DIR}/gui/iGuiTabs.cpp PROPERTIES
      COMPILE_DEFINITIONS "TSSM_GUI_BRANCH=\"${_tssm_branch}\"")
  endif()

  # The About tab's third-party notices, each component's own license file
  # embedded as written, so what the game shows cannot drift from what ships.
  set(_notices ${CMAKE_CURRENT_BINARY_DIR}/gui/iGuiNotices.inc)
  set(_notice_text "// Generated by CMakeLists.txt from the license files named below. Do not edit.\n")
  string(APPEND _notice_text "struct GuiNotice { const char* name; const char* license; const char* what; const char* url; const char* text; };\n")
  string(APPEND _notice_text "static const GuiNotice kGuiNotices[] = {\n")
  macro(tssm_gui_notice name license what url file)
    if(EXISTS "${file}")
      file(READ "${file}" _body)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    else()
      set(_body "(license file not found at configure time: ${file})")
    endif()
    string(APPEND _notice_text "  { \"${name}\", \"${license}\", \"${what}\", \"${url}\",\n    R\"tssm_notice(${_body})tssm_notice\" },\n")
  endmacro()
  tssm_gui_notice("${TSSM_PC_TITLE}, PC port" "CC0 1.0" "This program's own code."
                  "" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  tssm_gui_notice("Dear ImGui" "MIT" "The settings overlay and the launcher."
                  "https://github.com/ocornut/imgui" "${IMGUI_DIR}/LICENSE.txt")
  tssm_gui_notice("SDL" "zlib" "Windows, input, audio output, dialogs."
                  "https://libsdl.org" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/LICENSE.txt")
  tssm_gui_notice("librw" "MIT" "The RenderWare graphics layer the game draws through."
                  "" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/LICENSE")
  # A notice whose license file is not on this machine (MSYS2's FFmpeg package
  # installs none; a Linux cross toolchain keeps the MinGW-w64 ones elsewhere)
  # gets a short text saying where the license is, not the path looked in.
  macro(tssm_gui_notice_text var name)
    set(${var} ${CMAKE_CURRENT_BINARY_DIR}/gui/notice-${name}.txt)
    string(CONCAT _notice_body ${ARGN})
    file(WRITE ${${var}}.tmp "${_notice_body}")
    configure_file(${${var}}.tmp ${${var}} COPYONLY)
  endmacro()
  set(_ffmpeg_notice "${FFMPEG_INCLUDE_DIR}/../share/ffmpeg/copyright")
  if(MINGW AND FMV_INCLUDES AND NOT EXISTS "${_ffmpeg_notice}")
    tssm_gui_notice_text(_ffmpeg_notice ffmpeg
      "This build of FFmpeg is licensed under the ${TSSM_FFMPEG_LICENSE} (no license file was "
      "installed with it). FFmpeg's license texts and source: https://ffmpeg.org/legal.html\n")
  endif()
  if(FMV_INCLUDES AND TSSM_FFMPEG_STATIC)
    tssm_gui_notice("FFmpeg" "${TSSM_FFMPEG_LICENSE}" "Movie decoding. Linked statically; its source is at ffmpeg.org."
                    "https://ffmpeg.org" "${_ffmpeg_notice}")
  elseif(FMV_INCLUDES)
    tssm_gui_notice("FFmpeg" "${TSSM_FFMPEG_LICENSE}" "Movie decoding. Linked dynamically; its source is at ffmpeg.org."
                    "https://ffmpeg.org" "${_ffmpeg_notice}")
  endif()
  if(MINGW)
    # -static links in two parts of the MinGW-w64 runtime whose licenses ask
    # for their notices in binary copies: winpthreads (GCC's threads: std::mutex,
    # std::thread) and the CRT's startup and support code. libgcc and libstdc++
    # are under GCC's runtime library exception, which asks for none.
    get_filename_component(_mingw_prefix "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_mingw_prefix "${_mingw_prefix}/.." ABSOLUTE)
    set(_winpthreads_notice "${_mingw_prefix}/share/licenses/winpthreads/COPYING")
    set(_mingw_crt_notice "${_mingw_prefix}/share/licenses/crt/COPYING.MinGW-w64-runtime.txt")
    if(NOT EXISTS "${_winpthreads_notice}" OR NOT EXISTS "${_mingw_crt_notice}")
      message(WARNING "The MinGW-w64 runtime's license files are not under ${_mingw_prefix}/share/licenses "
                      "(MSYS2 has them there); the About tab names those licenses without their text.")
    endif()
    if(NOT EXISTS "${_winpthreads_notice}")
      tssm_gui_notice_text(_winpthreads_notice winpthreads
        "winpthreads is part of the MinGW-w64 project and under the MIT license; its text is in "
        "mingw-w64-libraries/winpthreads/COPYING of the MinGW-w64 source (https://www.mingw-w64.org).\n")
    endif()
    if(NOT EXISTS "${_mingw_crt_notice}")
      tssm_gui_notice_text(_mingw_crt_notice mingw-crt
        "The MinGW-w64 runtime's licenses (the Zope Public License 2.1 and others) are in "
        "COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt of the MinGW-w64 source "
        "(https://www.mingw-w64.org).\n")
    endif()
    tssm_gui_notice("MinGW-w64 winpthreads" "MIT" "Threads for the GCC runtime. Linked statically."
                    "https://www.mingw-w64.org" "${_winpthreads_notice}")
    tssm_gui_notice("MinGW-w64 runtime" "ZPL 2.1 and others" "The C runtime's startup and support code. Linked statically."
                    "https://www.mingw-w64.org" "${_mingw_crt_notice}")
  endif()
  if(TSSM_BACKEND_VULKAN)
    tssm_gui_notice("Vulkan-Headers" "Apache 2.0" "The Vulkan renderer's API headers."
                    "https://github.com/KhronosGroup/Vulkan-Headers" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers/LICENSE.md")
    tssm_gui_notice("volk" "MIT" "The Vulkan renderer's loader."
                    "" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk/LICENSE.md")
    tssm_gui_notice("Vulkan Memory Allocator" "MIT" "The Vulkan renderer's memory allocator."
                    "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator/LICENSE.txt")
  endif()
  string(APPEND _notice_text "};\n")
  # Through a scratch file so the real one is only touched when it changes.
  file(WRITE ${_notices}.tmp "${_notice_text}")
  configure_file(${_notices}.tmp ${_notices} COPYONLY)
else()
  set(GUI_SOURCES ${PC_DIR}/gui/iGuiNull.cpp)
endif()

# --- the platform layer ------------------------------------------------------------
# src/SB/Core/pc/PORTING.md lists every file here with its origin.
#
# stubs/: the Dolphin SDK, Bink and FMOD calls shared code still makes, one file
# per module. Most are stubs that report themselves once; stubs/STUBS.md
# (tools/pcstubs.py) lists them all.
file(GLOB TSSM_STUB_SOURCES CONFIGURE_DEPENDS ${PC_DIR}/stubs/*.cpp)

add_library(tssm_platform STATIC
  # TSSM's unity build of the modules copied from src/SB/Core/gc
  # (iAnim, iAnimSKB, iAsync, iCutscene, iEnv, iLight, iMath, iMath3, iModel,
  # iMorph, iParMgr), as gc/iWad.cpp compiles them on the console.
  ${PC_DIR}/iWad.cpp

  # The console's standalone platform units.
  ${PC_DIR}/iTRC.cpp
  ${PC_DIR}/iException.cpp
  ${PC_DIR}/iScrFX.cpp
  ${PC_DIR}/iARAMTmp.cpp

  # Written for the host (from treedome, adapted to TSSM's interfaces).
  ${PC_DIR}/iCapture.cpp
  ${PC_DIR}/iDraw.cpp
  ${PC_DIR}/iFile.cpp
  ${PC_DIR}/iFMV.cpp
  ${PC_DIR}/iFX.cpp
  ${PC_DIR}/iLowHeap.cpp
  ${PC_DIR}/iMemMgr.cpp
  ${PC_DIR}/iPad.cpp
  ${PC_DIR}/isavegame.cpp
  # The save store under it: formats, import/export, backups, the command line
  # (docs/SAVES.md).
  ${PC_DIR}/iSaveCLI.cpp
  ${PC_DIR}/iSaveFormat.cpp
  ${PC_DIR}/iSaveStore.cpp
  # The other consoles' saves: the little-endian payload the PlayStation 2 and
  # Xbox share, the Xbox's save folders and their signature, and the
  # PlayStation 2's folder in a .psu, a memory card image or PCSX2's folder
  # card (docs/SAVES.md sections 6 to 8), and a PlayStation 3's .psv with its
  # signature (AES-128 for that).
  ${PC_DIR}/iSaveAES.cpp
  ${PC_DIR}/iSaveLE.cpp
  ${PC_DIR}/iSaveSHA1.cpp
  ${PC_DIR}/iSavePS2.cpp
  ${PC_DIR}/iSaveStorePS2.cpp
  ${PC_DIR}/iSaveStoreXbox.cpp
  ${PC_DIR}/iSaveXbox.cpp
  # The load screen's "no saves" message, made to name the slot, and the save
  # screens' warning as a setting ([game] save_text): game only, it rewrites
  # loaded TEXT assets. The table of lines and words is on its own, for
  # tssm_savetexttest.
  ${PC_DIR}/iSaveText.cpp
  ${PC_DIR}/iSaveTextTable.cpp
  ${PC_DIR}/iSaveZip.cpp
  ${PC_DIR}/iSnd.cpp
  ${PC_DIR}/iSndData.cpp
  # The GameCube release's sound tables and DSP-ADPCM (iSndGC.h).
  ${PC_DIR}/iSndDsp.cpp
  ${PC_DIR}/iSndGC.cpp
  ${PC_DIR}/iSndXbox.cpp
  ${PC_DIR}/iSystem.cpp
  ${PC_DIR}/iTime.cpp
  # The Xbox release's features beside the GameCube's ([extras], iExtras.h).
  ${PC_DIR}/iExtras.cpp
  # The Cheats tab's tables and the port's own cheats ([cheats], iCheats.h).
  ${PC_DIR}/iCheats.cpp

  # The host layer under them (treedome).
  ${PC_DIR}/iBoot.cpp
  ${PC_DIR}/iCameraLook.cpp
  ${PC_DIR}/iConfig.cpp
  ${PC_DIR}/iConfigEdit.cpp
  ${PC_DIR}/iConfigTable.cpp
  # The game's own log (last_run.log) and where crash.log goes (iLog.h).
  ${PC_DIR}/iLog.cpp
  ${PC_DIR}/iPadBind.cpp
  ${PC_DIR}/iPadBindTable.cpp
  ${PC_DIR}/iPadLayout.cpp
  ${PC_DIR}/iPadSettings.cpp
  ${PC_DIR}/iPadStick.cpp
  ${PC_DIR}/intrin.cpp
  ${PC_DIR}/mwcplusplus.cpp

  # The game's linker-COMMON globals, which no source file defines.
  ${PC_DIR}/common.cpp

  # Copy-at-load widening of the assets that keep pointers in 4-byte slots,
  # on 64-bit hosts (iAssetWiden.h).
  ${PC_DIR}/iAssetWiden.cpp
  ${PC_DIR}/iAssetHooks.cpp

  # Mods (docs/MODDING.md): the mods/ folder, modded package views, INI
  # overlays, replacement sounds and textures.
  ${PC_DIR}/iMod.cpp
  ${PC_DIR}/iModAudio.cpp
  ${PC_DIR}/iModIni.cpp
  ${PC_DIR}/iModPackage.cpp
  ${PC_DIR}/iModTexture.cpp

  # GameCube packages: which console wrote a package (iGCPackage.h), and the
  # per-type byte swaps into the Xbox layout (iGCSwap.h, tools/gcswap).
  ${PC_DIR}/iGCPackage.cpp
  ${PC_DIR}/iGCSwap.cpp

  ${TSSM_STUB_SOURCES}

  ${INPUT_SOURCES}
  ${AUDIO_SOURCES}
  ${HOST_SOURCES}
  ${FMV_SOURCES}

  # The overlay and launcher (gui/iGuiNull.cpp with TSSM_GUI=OFF).
  ${GUI_SOURCES}
)
target_include_directories(tssm_platform PUBLIC ${TSSM_INCLUDES})
target_link_libraries(tssm_platform PUBLIC tssm_rw ${INPUT_LIBS} ${AUDIO_LIBS})
target_link_libraries(tssm_platform PRIVATE ${FMV_LIBRARIES})
# The About tab's build stamp (iBuildInfo.h).
target_link_libraries(tssm_platform PRIVATE tssm_buildinfo)
if(TSSM_GUI)
  target_link_libraries(tssm_platform PRIVATE tssm_imgui)
  target_include_directories(tssm_platform PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gui)
endif()
target_compile_options(tssm_platform PRIVATE ${TSSM_NO_WARNINGS})
if(FMV_INCLUDES)
  target_include_directories(tssm_platform PRIVATE ${FMV_INCLUDES})
endif()
if(FMV_DEFINES)
  target_compile_definitions(tssm_platform PRIVATE ${FMV_DEFINES})
endif()
# The program's name (TSSM_PC_TITLE, above), as iHost.h's TSSM_GAME_TITLE.
target_compile_definitions(tssm_platform PRIVATE "TSSM_GAME_TITLE=\"${TSSM_PC_TITLE}\"")
if(ANDROID)
  target_link_libraries(tssm_platform PUBLIC log)
endif()
# TSSM_TEST_AUDIO_RESTART (iAudioSDLTest.h); empty without SDL audio.
target_sources(tssm_platform PRIVATE ${PC_DIR}/iAudioSDLTest.cpp)
if(TSSM_AUDIO_BACKEND STREQUAL "sdl")
  set(_tssm_audio_test_defines TSSM_AUDIO_SDL_TEST)
  if(TSSM_AUDIO_SDL_INTERNALS)
    list(APPEND _tssm_audio_test_defines TSSM_AUDIO_SDL_INTERNALS)
  endif()
  set_source_files_properties(${PC_DIR}/iAudioSDLTest.cpp PROPERTIES
    COMPILE_DEFINITIONS "${_tssm_audio_test_defines}")
endif()

# The modules copied from gc are game-side code, compiled on the console with
# the game's flags (configure.py cflags_tssm): unsigned char, and the game's
# defines.
set(TSSM_GAME_OPTIONS
  -funsigned-char
  # C++14 sized deallocation makes `operator new(size_t, U32 heapID)` a
  # placement form whose matching delete is the usual one (32-bit hosts).
  -fno-sized-deallocation
  # `register` locals are the asm operands of the GameCube math inlines;
  # an error in clang's C++17.
  -Wno-register
)
set_source_files_properties(${PC_DIR}/iWad.cpp ${PC_DIR}/common.cpp PROPERTIES
  COMPILE_OPTIONS "${TSSM_GAME_OPTIONS}"
  COMPILE_DEFINITIONS "NON_MATCHING")

# The sound decoders (Xbox ADPCM, GameCube DSP-ADPCM) optimised in a Debug build
# too. They run on the game thread when a sound or a stream starts -- a stream
# decodes its first seconds at once -- and unoptimised that was 7 to 22 ms in one
# frame, a visible hitch at 120 fps (docs/PACING.md). Integer code: the samples
# are the same either way.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set_property(SOURCE ${PC_DIR}/iSndXbox.cpp ${PC_DIR}/iSndDsp.cpp APPEND PROPERTY
    COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()
# The widening reads the game's asset structs, so it sees them as the game
# code does: the game's flags and its prefix header.
set_source_files_properties(${PC_DIR}/iAssetWiden.cpp ${PC_DIR}/iAssetHooks.cpp PROPERTIES
  COMPILE_OPTIONS "${TSSM_GAME_OPTIONS};-include;${SB_DIR}/SBPrefix.h"
  COMPILE_DEFINITIONS "NON_MATCHING")

# --- the game code ---------------------------------------------------------------
# The SB units of configure.py -- the same unity "wad" files the GameCube build
# compiles, plus its standalone units -- read out of configure.py itself so the
# two lists cannot disagree. A wad #includes its .cpp files and they rely on
# each other's statics and includes, so they are compiled as wads here too.
#
# The SB/Core/gc units are the console's platform layer; their PC
# counterparts are in tssm_platform above.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/configure.py _cfg_lines
     REGEX "Object\\((Matching|NonMatching), \"SB/[^\"]+\\.cpp\"")
set(TSSM_GAME_SOURCES)
set(TSSM_GC_UNITS)
foreach(_line IN LISTS _cfg_lines)
  string(REGEX REPLACE ".*\"(SB/[^\"]+\\.cpp)\".*" "\\1" _unit "${_line}")
  if(_unit MATCHES "^SB/Core/gc/")
    list(APPEND TSSM_GC_UNITS ${_unit})
  else()
    list(APPEND TSSM_GAME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/${_unit})
  endif()
endforeach()
list(LENGTH TSSM_GAME_SOURCES _n_game)
list(LENGTH TSSM_GC_UNITS _n_gc)
message(STATUS "tssm: ${_n_game} game units from configure.py (+ ${_n_gc} gc units, replaced by src/SB/Core/pc)")

add_library(tssm_game STATIC ${TSSM_GAME_SOURCES}
  # Port-only, but they call game code, so they are compiled as game units: the
  # debug events of unattended runs (iTestEvents.h), the mouse's camera orbit
  # (iCameraOrbit.h) and its pointer in the game's menus (iMenuMouse.h).
  ${PC_DIR}/iTestEvents.cpp ${PC_DIR}/iCameraOrbit.cpp ${PC_DIR}/iMenuMouse.cpp)
target_link_libraries(tssm_game PUBLIC tssm_platform tssm_rw)
# The platform layer calls back into this library too: iCapture.cpp calls
# iTestEventsFrame and iTestSendFrame, which are game units. lld and link.exe
# find a symbol in any archive on the line; GNU ld (MinGW, Linux) reads each
# archive once, in order. Declaring the cycle makes CMake repeat the two.
target_link_libraries(tssm_platform INTERFACE tssm_game)
target_compile_definitions(tssm_game PRIVATE NON_MATCHING)
# TSSM_TEST_SEND, an event to a scene object at a frame (iTestSend.h): a game
# unit too, for the same reason.
target_sources(tssm_game PRIVATE ${PC_DIR}/iTestSend.cpp)
# The HUD at the screen's real edges (iHudAnchor.h): a game unit too.
target_sources(tssm_game PRIVATE ${PC_DIR}/iHudAnchor.cpp)
# The Xbox release's water (iXboxWater.h): a game unit too. Its arithmetic
# (iXboxWaterGrid.h, no game code) is in tssm_rw with the surface draw that uses it
# (rw/xboxwater.cpp). Without it, the Xbox files' water bodies look as the GameCube
# files' water does (iWaterBody.h), a game unit as well.
target_sources(tssm_game PRIVATE ${PC_DIR}/iXboxWater.cpp ${PC_DIR}/iXboxWaterPass.cpp
  ${PC_DIR}/iWaterBody.cpp)
# The Xbox extras from a level's GameCube files (iExtrasGC.h): a game unit too.
target_sources(tssm_game PRIVATE ${PC_DIR}/iExtrasGC.cpp)
# The Cheats tab's game side (iCheats.h): a game unit too.
target_sources(tssm_game PRIVATE ${PC_DIR}/iCheatsGame.cpp)
# The Sonic Wave Guitar's screen distortion (iXboxDistort.h): the amount and the
# swirl map's lookup, a game unit; the pass is rw/xboxdistort.cpp in tssm_rw.
target_sources(tssm_game PRIVATE ${PC_DIR}/iXboxDistort.cpp)
# SBPrefix.h is the console's precompiled prefix (-prefix SBPrefix.mch): every
# game unit sees it first.
target_compile_options(tssm_game PRIVATE ${TSSM_NO_WARNINGS} ${TSSM_GAME_OPTIONS}
  "SHELL:-include \"${SB_DIR}/SBPrefix.h\"")

# --- the sound test ----------------------------------------------------------------
# tssm_sndtest: the Xbox sound data and the mixer, without the game. It reads a
# package from the asset folder, decodes sounds and cutscene tracks with the
# code the game uses (iSndXbox.cpp) and renders them through the audio backend's
# offline mix to WAV files (src/SB/Core/pc/tests/sndtest.cpp). Nothing from the
# game's data is part of the build; the test reads the user's own copy.
if(NOT ANDROID)
  add_executable(tssm_sndtest ${PC_DIR}/tests/sndtest.cpp ${PC_DIR}/iSndXbox.cpp
    ${AUDIO_SOURCES} ${HOST_SOURCES})
  target_include_directories(tssm_sndtest PRIVATE ${TSSM_INCLUDES})
  target_link_libraries(tssm_sndtest PRIVATE ${AUDIO_LIBS})
  target_compile_options(tssm_sndtest PRIVATE ${TSSM_NO_WARNINGS})
  if(TSSM_AUDIO_BACKEND STREQUAL "null")
    target_compile_definitions(tssm_sndtest PRIVATE TSSM_SNDTEST_NULL)
  endif()
  if(WIN32)
    target_link_libraries(tssm_sndtest PRIVATE dbghelp ole32)
  elseif(NOT ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(tssm_sndtest PRIVATE Threads::Threads)
  endif()
endif()

# --- the executable --------------------------------------------------------------
# main() is the game's own (src/SB/Game/zMain.cpp).
set(TSSM_EXE_SOURCES ${PC_DIR}/tssm_main.cpp)

if(ANDROID)
  add_library(main SHARED ${TSSM_EXE_SOURCES})
  set(TSSM_GAME_TARGET main)
else()
  add_executable(tssm ${TSSM_EXE_SOURCES})
  set(TSSM_GAME_TARGET tssm)
endif()
target_link_libraries(${TSSM_GAME_TARGET} PRIVATE tssm_game tssm_platform tssm_rw tssm_buildinfo)
target_compile_options(${TSSM_GAME_TARGET} PRIVATE ${TSSM_NO_WARNINGS})
target_compile_definitions(${TSSM_GAME_TARGET} PRIVATE ${FMV_DEFINES})

if(NOT WIN32)
  if(NOT ANDROID)
    set_target_properties(${TSSM_GAME_TARGET} PROPERTIES ENABLE_EXPORTS ON)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(${TSSM_GAME_TARGET} PRIVATE Threads::Threads)
endif()

if(WIN32)
  # dbghelp: the crash handler in tssm_main.cpp. ole32: the audio device.
  target_link_libraries(tssm PRIVATE user32 gdi32 dbghelp ole32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(MINGW)
      target_link_options(tssm PRIVATE -Wl,--large-address-aware)
    else()
      target_link_options(tssm PRIVATE -Xlinker /LARGEADDRESSAWARE)
    endif()
  endif()

  # FFmpeg's and SDL's DLLs beside the executable.
  if(MINGW)
    # Under MinGW those come from a folder of every package's DLLs (MSYS2's
    # /mingw64/bin), so only the ones tssm.exe imports, directly or through
    # another DLL, are copied (tools/mingw/stage_dlls.cmake). -static above
    # already links in the GCC runtime; a static FFmpeg and the vendored SDL
    # need nothing.
    set(_tssm_dll_dirs)
    if(FMV_INCLUDES AND NOT TSSM_FFMPEG_STATIC)
      get_filename_component(_ffmpeg_bin "${FFMPEG_INCLUDE_DIR}/../bin" ABSOLUTE)
      list(APPEND _tssm_dll_dirs "${_ffmpeg_bin}")
    endif()
    if(TSSM_SDL_RUNTIME_DLL)
      list(APPEND _tssm_dll_dirs "$<TARGET_FILE_DIR:${_sdl_aliased}>")
    endif()
    if(_tssm_dll_dirs)
      string(REPLACE ";" "|" _tssm_dll_dirs "${_tssm_dll_dirs}")
      add_custom_command(TARGET tssm POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DEXE=$<TARGET_FILE:tssm>" "-DDIRS=${_tssm_dll_dirs}"
                "-DOBJDUMP=${CMAKE_OBJDUMP}" -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/mingw/stage_dlls.cmake
        COMMENT "Staging the DLLs tssm.exe imports"
        VERBATIM)
    endif()
  else()
    if(FMV_INCLUDES)
      file(GLOB FMV_RUNTIME_DLLS "${FFMPEG_INCLUDE_DIR}/../bin/*.dll")
      if(FMV_RUNTIME_DLLS)
        add_custom_command(TARGET tssm POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FMV_RUNTIME_DLLS} $<TARGET_FILE_DIR:tssm>
          COMMENT "Staging the movie decoder's DLLs")
      endif()
    endif()
    if(TSSM_SDL_RUNTIME_DLL)
      add_custom_command(TARGET tssm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TSSM_SDL_RUNTIME_DLL} $<TARGET_FILE_DIR:tssm>
        COMMENT "Staging SDL")
    endif()
  endif()
endif()

# --- tssm.exe's version information ----------------------------------------------
# The name and version Windows shows for tssm.exe (Explorer's Properties,
# Task Manager, the volume mixer): TSSM_PC_TITLE and TSSM_PC_VERSION, above.
# The resource script is written into the build directory.
if(WIN32 AND NOT ANDROID)
  enable_language(RC)
  string(REPLACE "." ";" _tssm_version_numbers "${TSSM_PC_VERSION}")
  list(LENGTH _tssm_version_numbers _tssm_version_count)
  while(_tssm_version_count LESS 4)
    list(APPEND _tssm_version_numbers 0)
    list(LENGTH _tssm_version_numbers _tssm_version_count)
  endwhile()
  string(REPLACE ";" "," _tssm_version_numbers "${_tssm_version_numbers}")
  set(TSSM_VERSION_BUILD "${CMAKE_CURRENT_BINARY_DIR}/tssm_version")
  file(GENERATE OUTPUT "${TSSM_VERSION_BUILD}/tssm_version.rc" CONTENT
"1 VERSIONINFO
FILEVERSION ${_tssm_version_numbers}
PRODUCTVERSION ${_tssm_version_numbers}
FILEFLAGSMASK 0x3F
FILEFLAGS 0x0
FILEOS 0x40004
FILETYPE 0x1
FILESUBTYPE 0x0
BEGIN
  BLOCK \"StringFileInfo\"
  BEGIN
    BLOCK \"040904B0\"
    BEGIN
      VALUE \"FileDescription\", \"${TSSM_PC_TITLE}\"
      VALUE \"FileVersion\", \"${TSSM_PC_VERSION}\"
      VALUE \"InternalName\", \"tssm\"
      VALUE \"OriginalFilename\", \"tssm.exe\"
      VALUE \"ProductName\", \"${TSSM_PC_TITLE}\"
      VALUE \"ProductVersion\", \"${TSSM_PC_VERSION}\"
    END
  END
  BLOCK \"VarFileInfo\"
  BEGIN
    VALUE \"Translation\", 0x409, 1200
  END
END
")
  target_sources(tssm PRIVATE "${TSSM_VERSION_BUILD}/tssm_version.rc")
  set_source_files_properties("${TSSM_VERSION_BUILD}/tssm_version.rc" PROPERTIES GENERATED ON)
endif()

# --- the app icon ----------------------------------------------------------------
# By default the sponge block drawn for the port, the release builds' icon:
# dist/pc/default_icon.ico and default_icon-64.png, which tools/make_icons.ps1
# made from dist/switch/default_icon.jpg (docs/ICON.md). TSSM_ICON_DIR names a
# folder of your own that tools/make_icons.ps1 made from a picture, outside
# the repository, and wins. The icon goes to two places:
#  * Windows: tssm.exe carries the .ico as its first icon resource, which
#    Explorer shows, and iWindowSetIcon sends each window its sizes for the
#    title bar, the taskbar and Alt+Tab.
#  * Every SDL window, on every platform: the 64-pixel PNG is compiled in
#    (tssm_window_icon.inc) and set with SDL_SetWindowIcon, on the launcher's
#    window and the game's.
set(TSSM_ICON_DIR "" CACHE PATH
  "Folder made by tools/make_icons.ps1 (docs/ICON.md); empty uses the sponge block in dist/pc")
if(TSSM_ICON_DIR)
  if(WIN32 AND NOT ANDROID AND NOT EXISTS "${TSSM_ICON_DIR}/tssm.ico")
    message(FATAL_ERROR "TSSM_ICON_DIR is ${TSSM_ICON_DIR}, which has no tssm.ico. "
      "Make the folder with tools/make_icons.ps1 (docs/ICON.md).")
  endif()
  set(TSSM_ICON_ICO "${TSSM_ICON_DIR}/tssm.ico")
  set(TSSM_ICON_PNG "${TSSM_ICON_DIR}/icon-64.png")
  message(STATUS "App icon: ${TSSM_ICON_DIR}")
else()
  set(TSSM_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/dist/pc/default_icon.ico")
  set(TSSM_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/dist/pc/default_icon-64.png")
  message(STATUS "App icon: the default (dist/pc)")
endif()
set(TSSM_ICON_BUILD "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon")
if(WIN32 AND NOT ANDROID)
  enable_language(RC)
  # Copied into the build directory (a changed icon reconfigures), and the
  # resource script is written there too: neither enters the source tree.
  configure_file("${TSSM_ICON_ICO}" "${TSSM_ICON_BUILD}/tssm.ico" COPYONLY)
  file(GENERATE OUTPUT "${TSSM_ICON_BUILD}/tssm_icon.rc"
    CONTENT "1 ICON \"${TSSM_ICON_BUILD}/tssm.ico\"\n")
  target_sources(tssm PRIVATE "${TSSM_ICON_BUILD}/tssm_icon.rc")
  set_source_files_properties("${TSSM_ICON_BUILD}/tssm_icon.rc" PROPERTIES
    GENERATED ON OBJECT_DEPENDS "${TSSM_ICON_BUILD}/tssm.ico")
endif()
# The window icon's PNG as bytes. A folder of your own without icon-64.png
# gives the windows none of their own (on Windows they still get the .ico).
set(_tssm_icon_bytes "")
if(EXISTS "${TSSM_ICON_PNG}")
  file(READ "${TSSM_ICON_PNG}" _tssm_icon_hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _tssm_icon_bytes "${_tssm_icon_hex}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TSSM_ICON_PNG}")
endif()
file(GENERATE OUTPUT "${TSSM_ICON_BUILD}/tssm_window_icon.inc" CONTENT
  "// Generated by CMakeLists.txt (\"the app icon\") from ${TSSM_ICON_PNG}.\n\
static const unsigned char kWindowIconPng[] = { ${_tssm_icon_bytes} 0 };\n\
static const unsigned kWindowIconPngSize = (unsigned)sizeof(kWindowIconPng) - 1;\n")
target_include_directories(tssm_rw PRIVATE "${TSSM_ICON_BUILD}")

# --- tests ----------------------------------------------------------------------
option(TSSM_BUILD_TESTS "Build the platform layer's unit tests" ON)

# --- the headless input test -------------------------------------------------------
# tssm_input_test drives SDL's virtual controllers and a stand-in keyboard
# through the game's own xPadUpdate (src/SB/Core/x/xPad.cpp, compiled in with
# the game's flags) and checks what the game sees. No game data, no window.
#   build-*/tssm_input_test [scratch folder]      (or `ctest -R input`)
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS AND TSSM_INPUT_BACKEND STREQUAL "sdl")
  # gui/iGuiNull.cpp stands in for the overlay: iPad.cpp asks it for the input
  # filter, and the real one would bring the GUI's RenderWare self-test (and
  # the game's camera code behind it) into a program that has no game.
  add_executable(tssm_input_test ${PC_DIR}/tests/input_test.cpp ${PC_DIR}/tests/input_test_xpad.cpp
    ${PC_DIR}/gui/iGuiNull.cpp)
  target_link_libraries(tssm_input_test PRIVATE tssm_platform tssm_rw)
  target_compile_options(tssm_input_test PRIVATE ${TSSM_NO_WARNINGS})
  set_source_files_properties(${PC_DIR}/tests/input_test_xpad.cpp TARGET_DIRECTORY tssm_input_test
    PROPERTIES COMPILE_OPTIONS "${TSSM_GAME_OPTIONS};-include;${SB_DIR}/SBPrefix.h"
               COMPILE_DEFINITIONS "NON_MATCHING")
  if(WIN32)
    target_link_libraries(tssm_input_test PRIVATE user32 gdi32 ole32 dbghelp)
  endif()
  enable_testing()
  add_test(NAME tssm_input_test
           COMMAND tssm_input_test ${CMAKE_CURRENT_BINARY_DIR}/tssm_input_test_run)
endif()

# --- save_selftest ---------------------------------------------------------------
# The save path end to end, through the game's own save code: xsavegame,
# xserializer, xutil and xordarray compiled on their own (out of their wads,
# with the game's flags), the port's isavegame and save store, and stubs for the
# allocator and the settings. src/SB/Core/pc/tests/save_selftest.cpp says what
# it checks; docs/SAVES.md says why. `ctest -R save` or run it with a scratch
# folder.
if(TSSM_HOST_TOOLS)
  enable_testing()

  # As their wads compile them: the game's flags and prefix, plus the headers
  # the wads give them (tests/save_selftest_prefix.h).
  add_library(save_selftest_game OBJECT
    ${SB_DIR}/Core/x/xsavegame.cpp
    ${SB_DIR}/Core/x/xserializer.cpp
    ${SB_DIR}/Core/x/xutil.cpp
    ${SB_DIR}/Core/x/xordarray.cpp
  )
  target_include_directories(save_selftest_game PRIVATE ${TSSM_INCLUDES})
  target_compile_definitions(save_selftest_game PRIVATE NON_MATCHING)
  target_compile_options(save_selftest_game PRIVATE ${TSSM_NO_WARNINGS} ${TSSM_GAME_OPTIONS}
    "SHELL:-include \"${SB_DIR}/SBPrefix.h\""
    "SHELL:-include \"${PC_DIR}/tests/save_selftest_prefix.h\"")

  add_executable(save_selftest
    ${PC_DIR}/tests/save_selftest.cpp
    ${PC_DIR}/tests/save_selftest_stubs.cpp
    ${PC_DIR}/isavegame.cpp
    ${PC_DIR}/iSaveCLI.cpp
    ${PC_DIR}/iSaveFormat.cpp
    ${PC_DIR}/iSaveAES.cpp
    ${PC_DIR}/iSaveLE.cpp
    ${PC_DIR}/iSaveSHA1.cpp
    ${PC_DIR}/iSaveStore.cpp
    ${PC_DIR}/iSavePS2.cpp
    ${PC_DIR}/iSaveStorePS2.cpp
    ${PC_DIR}/iSaveStoreXbox.cpp
    ${PC_DIR}/iSaveXbox.cpp
    ${PC_DIR}/iSaveZip.cpp
    ${HOST_SOURCES}
    $<TARGET_OBJECTS:save_selftest_game>
  )
  target_include_directories(save_selftest PRIVATE ${TSSM_INCLUDES})
  target_compile_options(save_selftest PRIVATE ${TSSM_NO_WARNINGS})
  if(WIN32)
    target_link_libraries(save_selftest PRIVATE dbghelp)
  elseif(NOT ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(save_selftest PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  add_test(NAME save_selftest
           COMMAND save_selftest ${CMAKE_CURRENT_BINARY_DIR}/save_selftest_run)
endif()

# --- xsave_selftest ----------------------------------------------------------------
# The other consoles' saves (docs/SAVES.md sections 6 to 8), without the game:
# SHA-1 and HMAC known answers, the Xbox folder names, the signature with a
# made-up keys file against an independent computation, the little-endian
# conversion against a reference encoder on made-up saves and packages, and
# Xbox export and import through the store and the command line. Everything it
# reads is made in the test; with the Xbox and GameCube folders
# (TSSM_TEST_ASSETS, TSSM_TEST_GC_ASSETS) it also reads the player's own XBE
# and packages, and with a folder of real saves (TSSM_TEST_SAVES) converts
# each there and back. The PlayStation 2 side is in it too: the card's
# error-correcting code, .psu files and a card image formatted by the test;
# with a PlayStation 2 disc's files (TSSM_TEST_PS2_ASSETS) the game's icon is
# found in its PLAT.HIP. And the PlayStation 3's: AES-128, the .psv's layout
# and its two signatures with made-up key files, .VM2 cards, and a real .psv
# among the saves written again byte for byte but for its signature.
#   build-*/xsave_selftest <scratch folder> [<Xbox files> [<GameCube files> [<saves>
#                          [<PlayStation 2 files>]]]]
if(TSSM_HOST_TOOLS)
  add_executable(xsave_selftest
    ${PC_DIR}/tests/xsave_selftest.cpp
    ${PC_DIR}/iSaveCLI.cpp
    ${PC_DIR}/iSaveFormat.cpp
    ${PC_DIR}/iSaveAES.cpp
    ${PC_DIR}/iSaveLE.cpp
    ${PC_DIR}/iSaveSHA1.cpp
    ${PC_DIR}/iSaveStore.cpp
    ${PC_DIR}/iSavePS2.cpp
    ${PC_DIR}/iSaveStorePS2.cpp
    ${PC_DIR}/iSaveStoreXbox.cpp
    ${PC_DIR}/iSaveXbox.cpp
    ${PC_DIR}/iSaveZip.cpp
    ${HOST_SOURCES}
  )
  target_include_directories(xsave_selftest PRIVATE ${TSSM_INCLUDES})
  target_compile_options(xsave_selftest PRIVATE ${TSSM_NO_WARNINGS})
  if(WIN32)
    target_link_libraries(xsave_selftest PRIVATE dbghelp)
  elseif(NOT ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(xsave_selftest PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  enable_testing()
  add_test(NAME xsave_selftest
           COMMAND xsave_selftest ${CMAKE_CURRENT_BINARY_DIR}/xsave_selftest_run
                   "${TSSM_TEST_ASSETS}" "${TSSM_TEST_GC_ASSETS}" "${TSSM_TEST_SAVES}"
                   "${TSSM_TEST_PS2_ASSETS}")

  # The PlayStation 3 .psv's signature and layout against an implementation of
  # their public description in tests/psv_sign_check.py (its own AES-128 and
  # Python's HMAC), with made-up keys only; when there is a Python 3.7 or later.
  find_package(Python3 3.7 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME psv_sign_check
             COMMAND ${Python3_EXECUTABLE} -I ${PC_DIR}/tests/psv_sign_check.py
                     $<TARGET_FILE:xsave_selftest> ${CMAKE_CURRENT_BINARY_DIR}/psv_sign_check_run)
  endif()
endif()

# --- tssm_widentest -----------------------------------------------------------------
# tssm_widentest: the copy-at-load widening (src/SB/Core/pc/iAssetWiden.h) run
# over every package of an asset folder, without the game: each widened asset
# is walked the way the game walks it and checked against the bytes on disk.
#   build-*/tssm_widentest <asset folder> [--verbose]
# On a 32-bit host nothing is widened and only iPtr32 is checked.
# TSSM_TEST_ASSETS (an asset folder) also registers it with ctest.
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS)
  add_executable(tssm_widentest ${PC_DIR}/tests/widentest.cpp ${PC_DIR}/iAssetWiden.cpp
    ${PC_DIR}/iSndXbox.cpp)
  target_include_directories(tssm_widentest PRIVATE ${TSSM_INCLUDES})
  target_compile_options(tssm_widentest PRIVATE ${TSSM_NO_WARNINGS})
  set_source_files_properties(${PC_DIR}/tests/widentest.cpp TARGET_DIRECTORY tssm_widentest
    PROPERTIES COMPILE_OPTIONS "${TSSM_GAME_OPTIONS};-include;${SB_DIR}/SBPrefix.h"
               COMPILE_DEFINITIONS "NON_MATCHING")
  set(TSSM_TEST_ASSETS "" CACHE PATH "An asset folder for the tests that need the game's data")
  if(TSSM_TEST_ASSETS)
    add_test(NAME tssm_widentest COMMAND tssm_widentest ${TSSM_TEST_ASSETS})
  endif()
endif()

# --- tssm_watertest ---------------------------------------------------------------
# The Xbox water's tiles, strips and bump clock (iXboxWaterGrid.h) on made-up
# meshes: no game, no data. Run it as `tssm_watertest`.
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS)
  add_executable(tssm_watertest ${PC_DIR}/tests/watertest.cpp ${PC_DIR}/iXboxWaterGrid.cpp)
  target_include_directories(tssm_watertest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME tssm_watertest COMMAND tssm_watertest)
endif()

# --- tssm_steadyframetest ---------------------------------------------------------
# The game's steady frame time (iSteadyFrame.h) on made-up frame times: the
# enemies' end-of-animation check at 60, 120 and the GameCube's 30, the clock
# against the wall clock, late frames, scenes over budget; no game, no data.
# Run it as `tssm_steadyframetest`.
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS)
  add_executable(tssm_steadyframetest ${PC_DIR}/tests/steadyframetest.cpp)
  target_include_directories(tssm_steadyframetest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME tssm_steadyframetest COMMAND tssm_steadyframetest)
endif()

# --- tssm_hudtest -----------------------------------------------------------------
# The HUD at the screen's edges ([video] ui, iScreen.h's anchor and safe margin) on
# made-up screen shapes: no game, no data. Run it as `tssm_hudtest`.
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS)
  add_executable(tssm_hudtest ${PC_DIR}/tests/hudtest.cpp ${PC_DIR}/iScreen.cpp)
  target_include_directories(tssm_hudtest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME tssm_hudtest COMMAND tssm_hudtest)
endif()

# --- tssm_savetexttest ---------------------------------------------------------------
# The save screens' warning as a setting ([game] save_text, iSaveText.h): every
# line of both discs with every choice, and what is never rewritten; no game.
# With the Xbox and GameCube folders (TSSM_TEST_ASSETS, TSSM_TEST_GC_ASSETS)
# every TEXT asset of both discs is checked against the table too.
#   build-*/tssm_savetexttest [<Xbox files> [<GameCube files>]]
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS)
  add_executable(tssm_savetexttest ${PC_DIR}/tests/savetexttest.cpp ${PC_DIR}/iSaveTextTable.cpp)
  target_include_directories(tssm_savetexttest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  enable_testing()
  add_test(NAME tssm_savetexttest COMMAND tssm_savetexttest "${TSSM_TEST_ASSETS}"
    "${TSSM_TEST_GC_ASSETS}")
endif()

# --- tssm_gcswaptest -------------------------------------------------------------
# tssm_gcswaptest: the GameCube byte swaps (src/SB/Core/pc/iGCSwap.h) run over
# every package of an extracted GameCube disc, without the game: each package's
# platform, each asset swapped in a buffer that ends at an inaccessible page (so
# an overrun stops the test where it happens), cut-short and damaged copies, and
# with an Xbox folder each result against the Xbox asset of the same id.
#   build-*/tssm_gcswaptest <GameCube files> [<Xbox files>] [--fuzz N] [--type XXXX]
# tools/gcswap/README.md says how to read its report.
if(TSSM_BUILD_TESTS AND NOT ANDROID)
  add_executable(tssm_gcswaptest ${PC_DIR}/tests/gcswaptest.cpp ${PC_DIR}/iGCSwap.cpp
    ${PC_DIR}/iGCPackage.cpp ${PC_DIR}/iModPackage.cpp)
  target_include_directories(tssm_gcswaptest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_options(tssm_gcswaptest PRIVATE ${TSSM_NO_WARNINGS})
  # TSSM_TEST_GC_ASSETS (an extracted GameCube disc's files/ folder) registers
  # it with ctest, against TSSM_TEST_ASSETS (the Xbox folder) when that is set.
  set(TSSM_TEST_GC_ASSETS "" CACHE PATH "An extracted GameCube disc, for tssm_gcswaptest")
  if(TSSM_TEST_GC_ASSETS)
    enable_testing()
    add_test(NAME tssm_gcswaptest COMMAND tssm_gcswaptest ${TSSM_TEST_GC_ASSETS} ${TSSM_TEST_ASSETS})
  endif()
endif()

# --- tssm_ordertest --------------------------------------------------------------
# The creation numbers on RenderWare's atomics that put the opaque models in the
# console's draw order (iModelOrder.h, rw/atomic_order.cpp), over librw's NULL
# platform: no game, no data, no device. Run it as `tssm_ordertest`.
if(TSSM_BUILD_TESTS AND NOT ANDROID)
  add_executable(tssm_ordertest ${PC_DIR}/tests/ordertest.cpp ${PC_DIR}/rw/atomic_order.cpp)
  target_include_directories(tssm_ordertest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(tssm_ordertest PRIVATE librw)
  target_compile_options(tssm_ordertest PRIVATE ${TSSM_NO_WARNINGS})
  enable_testing()
  add_test(NAME tssm_ordertest COMMAND tssm_ordertest)
endif()

# --- tssm_gcrwtest ---------------------------------------------------------------
# tssm_gcrwtest: the GameCube RenderWare data -- native textures, geometry and
# skins -- read with the port's librw (tools/librw/gc_*.patch) over every package
# of an extracted GameCube disc, without the game or a render device, and each
# compared with the Xbox texture or atomic of the same id (tests/gcrwtest.cpp).
#   build-*/tssm_gcrwtest <GameCube files> [<Xbox files>] [--fuzz N] [--only tex|geo]
if(TSSM_BUILD_TESTS AND NOT ANDROID)
  add_executable(tssm_gcrwtest ${PC_DIR}/tests/gcrwtest.cpp ${PC_DIR}/iGCPackage.cpp
    ${PC_DIR}/iModPackage.cpp)
  target_include_directories(tssm_gcrwtest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(tssm_gcrwtest PRIVATE librw)
  target_compile_options(tssm_gcrwtest PRIVATE ${TSSM_NO_WARNINGS})
  if(TSSM_TEST_GC_ASSETS)
    enable_testing()
    add_test(NAME tssm_gcrwtest COMMAND tssm_gcrwtest ${TSSM_TEST_GC_ASSETS} ${TSSM_TEST_ASSETS})
  endif()
endif()

# --- tssm_gcsndtest --------------------------------------------------------------
# tssm_gcsndtest: the GameCube sound data -- the sound tables, their FSB3 banks
# of DSP-ADPCM and the cutscene tracks -- read and decoded with the code the
# game uses (iSndGC.cpp, iSndDsp.cpp) over every package of an extracted
# GameCube disc, and each sound compared with the Xbox sound of the same id
# (tests/gcsndtest.cpp).
#   build-*/tssm_gcsndtest <GameCube files> [<Xbox files>] [--fuzz N] [--dump DIR]
if(TSSM_BUILD_TESTS AND NOT ANDROID)
  add_executable(tssm_gcsndtest ${PC_DIR}/tests/gcsndtest.cpp ${PC_DIR}/iSndGC.cpp
    ${PC_DIR}/iSndDsp.cpp ${PC_DIR}/iSndXbox.cpp ${PC_DIR}/iGCPackage.cpp ${PC_DIR}/iModPackage.cpp)
  target_include_directories(tssm_gcsndtest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_options(tssm_gcsndtest PRIVATE ${TSSM_NO_WARNINGS})
  if(TSSM_TEST_GC_ASSETS)
    enable_testing()
    add_test(NAME tssm_gcsndtest COMMAND tssm_gcsndtest ${TSSM_TEST_GC_ASSETS} ${TSSM_TEST_ASSETS})
  endif()
endif()

# --- tssm_extrastest ----------------------------------------------------------------
# tssm_extrastest: the Xbox extras' decisions (src/SB/Core/pc/iExtras.h) and
# their [extras] settings through the real config layer, without the game; the
# pacer, the renderer and the pad table are stand-ins in the test itself.
#   build-*/tssm_extrastest <scratch folder>
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS)
  add_executable(tssm_extrastest ${PC_DIR}/tests/extrastest.cpp ${PC_DIR}/iExtras.cpp
    ${PC_DIR}/iConfig.cpp ${PC_DIR}/iConfigEdit.cpp ${PC_DIR}/iConfigTable.cpp ${HOST_SOURCES})
  target_include_directories(tssm_extrastest PRIVATE ${TSSM_INCLUDES})
  target_compile_options(tssm_extrastest PRIVATE ${TSSM_NO_WARNINGS})
  if(WIN32)
    target_link_libraries(tssm_extrastest PRIVATE dbghelp)
  endif()
  enable_testing()
  add_test(NAME tssm_extrastest
           COMMAND tssm_extrastest ${CMAKE_CURRENT_BINARY_DIR}/tssm_extrastest_run)
endif()

# --- tssm_cheattest -----------------------------------------------------------------
# tssm_cheattest: the Cheats tab's tables and state (src/SB/Core/pc/iCheats.h)
# and the [cheats] settings through the real config layer, without the game.
#   build-*/tssm_cheattest <scratch folder>
if(TSSM_BUILD_TESTS AND TSSM_HOST_TOOLS)
  add_executable(tssm_cheattest ${PC_DIR}/tests/cheattest.cpp ${PC_DIR}/iCheats.cpp
    ${PC_DIR}/iConfig.cpp ${PC_DIR}/iConfigEdit.cpp ${PC_DIR}/iConfigTable.cpp ${HOST_SOURCES})
  target_include_directories(tssm_cheattest PRIVATE ${TSSM_INCLUDES})
  target_compile_options(tssm_cheattest PRIVATE ${TSSM_NO_WARNINGS})
  if(WIN32)
    target_link_libraries(tssm_cheattest PRIVATE dbghelp)
  endif()
  enable_testing()
  add_test(NAME tssm_cheattest
           COMMAND tssm_cheattest ${CMAKE_CURRENT_BINARY_DIR}/tssm_cheattest_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/src/SB/Game/zGameExtras.cpp)
endif()

# --- tssm_modtest ----------------------------------------------------------------
# tssm_modtest: the mod system run on its own -- INI overlays, sound decoding,
# package views checked against real packages, tables of contents, GameCube
# packages, the mod manager. No game code, renderer or window. Run it as
# `tssm_modtest <asset folder> <scratch folder> [<GameCube files>]`
# (src/SB/Core/pc/tests/modtest.cpp, docs/MODDING.md). TSSM_TEST_ASSETS
# registers it with ctest, with TSSM_TEST_GC_ASSETS when that is set.
if(TSSM_BUILD_TESTS AND NOT ANDROID)
  add_executable(tssm_modtest ${PC_DIR}/tests/modtest.cpp
    ${PC_DIR}/iMod.cpp ${PC_DIR}/iModAudio.cpp ${PC_DIR}/iModIni.cpp ${PC_DIR}/iModPackage.cpp
    ${PC_DIR}/iGCPackage.cpp ${PC_DIR}/iGCSwap.cpp ${HOST_SOURCES})
  target_include_directories(tssm_modtest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  if(FMV_INCLUDES)
    target_include_directories(tssm_modtest PRIVATE ${FMV_INCLUDES})
    target_compile_definitions(tssm_modtest PRIVATE ${FMV_DEFINES})
    target_link_libraries(tssm_modtest PRIVATE ${FFMPEG_AVFORMAT_LIBRARY}
      ${FFMPEG_AVCODEC_LIBRARY} ${FFMPEG_SWRESAMPLE_LIBRARY} ${FFMPEG_AVUTIL_LIBRARY}
      ${FFMPEG_SYSTEM_LIBS})
  endif()
  if(WIN32)
    target_link_libraries(tssm_modtest PRIVATE user32 dbghelp)
  elseif(NOT APPLE)
    target_link_libraries(tssm_modtest PRIVATE dl)
  endif()
  if(TSSM_TEST_ASSETS)
    enable_testing()
    add_test(NAME tssm_modtest COMMAND tssm_modtest ${TSSM_TEST_ASSETS}
      ${CMAKE_CURRENT_BINARY_DIR}/modtest-scratch ${TSSM_TEST_GC_ASSETS})
  endif()
endif()
