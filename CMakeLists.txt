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
    "${CMAKE_SIZEOF_VOID_P}-byte pointer. Enter the x86 MSVC environment first.")
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

if(TSSM_BUILD_32BIT AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-msse2 -mfpmath=sse)
endif()

if(MINGW)
  add_link_options(-static)
endif()

# The positive marker that selects the port's arms in shared headers and the
# PC RenderWare layouts in src/SB/Core/pc/rwsdk.
add_compile_definitions(PLATFORM_PC)

if(TSSM_MSVC_ABI)
  add_compile_definitions(_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH _CRT_SECURE_NO_WARNINGS)
endif()

# Nintendo Switch homebrew (devkitPro's Switch.cmake; docs/SWITCH.md): a third
# target next to Windows and Android. Everything it changes is behind
# TSSM_SWITCH here and __SWITCH__ in the source.
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  set(TSSM_SWITCH ON)
else()
  set(TSSM_SWITCH OFF)
endif()

# Nintendo 3DS homebrew (devkitPro's 3DS.cmake; docs/3DS.md): a fourth target,
# New 3DS only. Everything it changes is behind TSSM_3DS here and __3DS__ in
# the source. It draws with citro3d (rw::c3d, src/SB/Core/pc/3ds/c3d); the
# speed test (TSSM_3DS_SPIKE) keeps librw's NULL backend, which draws
# nothing, because it times the game's own work.
if(CMAKE_SYSTEM_NAME STREQUAL "Nintendo3DS")
  set(TSSM_3DS ON)
else()
  set(TSSM_3DS OFF)
endif()

if(ANDROID OR TSSM_SWITCH OR TSSM_3DS)
  set(TSSM_HOST_TOOLS OFF)
else()
  set(TSSM_HOST_TOOLS ON)
endif()

if(TSSM_3DS)
  # devkitARM sizes an enum to its values by default (the ARM EABI's
  # bare-metal rule), so an enum field in an asset or a save would shrink; the
  # GameCube build compiles with `-enum int`, as every other port target does
  # by default. Everything this project builds gets int-sized enums: the game,
  # the platform layer and librw (whose layouts rw/layout*.cpp mirrors).
  # libctru and SDL stay as devkitPro builds them (short enums), so the file
  # that includes libctru's headers (3ds/i3DS.cpp) is compiled their way and
  # shares no enum with the rest (3ds/i3DS.h). The SDL structs the port fills
  # in lay out the same both ways. 3ds/iLayout3DS.cpp asserts all of it. GNU
  # ld warns about every object that mixes the two conventions; that is the
  # mix described here, so the warning is off.
  add_compile_options(-fno-short-enums)
  add_link_options(-Wl,--no-enum-size-warning)

  # GCC treats whatever an object's storage held before its constructor ran
  # as dead, and deletes the stores (-flifetime-dse). The game zeroes memory
  # and then placement-news an object into it, relying on the zeros for the
  # members the constructor leaves alone: zLoadingScreen::Init memsets each
  # element before zUIImage_Init's `new (ui) zUIImage`, and GCC dropped every
  # one of those memsets, so zUI::custom held garbage and the first level load
  # jumped through it (seen in Azahar). Clang does not do this, which is why
  # the PC builds never saw it.
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-lifetime-dse>)

  # The speed test (src/SB/Core/pc/iSpike.h, docs/3DS.md): a build that
  # measures the console and the game by itself and writes a report, packaged
  # as tssm-spike.3dsx. Its own build directory; the game is the same code.
  option(TSSM_3DS_SPIKE "Build the 3DS speed test (tssm-spike.3dsx) instead of the game" OFF)
  if(TSSM_3DS_SPIKE)
    add_compile_definitions(TSSM_3DS_SPIKE=1)
  endif()

  # A console probe (docs/3DS.md, "Console probes"): the game with one effect
  # not yet shown to work on a console turned on, everything it needs compiled
  # into i3DS.cpp alone, so that the probes share one build directory.
  set(TSSM_3DS_PROBE "0" CACHE STRING "A console probe build, 1-11, 14-16 (docs/3DS.md); 0: the game")
endif()

# --- render backends -----------------------------------------------------------
# A list; one executable carries all of it and config.ini's video.backend picks.
if(WIN32)
  set(TSSM_RENDER_BACKENDS_DEFAULT "D3D9;D3D11;GL3")
elseif(TSSM_3DS)
  # citro3d on the PICA200 (rw::c3d), which lives in the port's tree
  # (src/SB/Core/pc/3ds/c3d) and is added to librw below.
  set(TSSM_RENDER_BACKENDS_DEFAULT "C3D")
else()
  set(TSSM_RENDER_BACKENDS_DEFAULT "GL3")
endif()
set(TSSM_RENDER_BACKENDS "${TSSM_RENDER_BACKENDS_DEFAULT}"
    CACHE STRING "Render backends, semicolon-separated: D3D9, D3D11, GL3, VULKAN, C3D, NULL")
if(TSSM_3DS AND TSSM_3DS_SPIKE)
  # The spike measures the game with nothing drawn, whatever the cache says.
  set(TSSM_RENDER_BACKENDS "NULL")
endif()

foreach(_backend IN LISTS TSSM_RENDER_BACKENDS)
  if(NOT _backend MATCHES "^(D3D9|D3D11|GL3|VULKAN|C3D|NULL)$")
    message(FATAL_ERROR "unknown render backend '${_backend}' in TSSM_RENDER_BACKENDS")
  endif()
  set(TSSM_BACKEND_${_backend} ON)
endforeach()
if(NOT WIN32 AND (TSSM_BACKEND_D3D9 OR TSSM_BACKEND_D3D11))
  message(FATAL_ERROR "TSSM_RENDER_BACKENDS names Direct3D on ${CMAKE_SYSTEM_NAME}.")
endif()
if(TSSM_3DS AND NOT TSSM_RENDER_BACKENDS MATCHES "^(NULL|C3D)$")
  message(FATAL_ERROR "The 3DS build draws with C3D, or nothing with NULL (docs/3DS.md).")
endif()
if(TSSM_BACKEND_C3D AND NOT TSSM_3DS)
  message(FATAL_ERROR "The C3D render backend is the Nintendo 3DS's.")
endif()
message(STATUS "tssm: render backends: ${TSSM_RENDER_BACKENDS}")

set(TSSM_NEEDS_SDL OFF)

# --- the GUI ---------------------------------------------------------------------
# The Dear ImGui overlay and launcher (src/SB/Core/pc/gui, docs/GUI.md). It
# draws through ImGui's backend for whichever librw device runs, and needs SDL
# for the window, its events and the launcher's renderer and folder picker.
# On the 3DS it is the overlay alone, on the bottom screen, drawn by rw::c3d;
# the spike draws nothing and has none.
if(TSSM_3DS AND (TSSM_3DS_SPIKE OR NOT TSSM_BACKEND_C3D))
  set(_tssm_gui_default OFF)
else()
  set(_tssm_gui_default ON)
endif()
option(TSSM_GUI "Build the Dear ImGui overlay and launcher (third_party/imgui)" ${_tssm_gui_default})
if(TSSM_GUI AND TSSM_3DS AND (TSSM_3DS_SPIKE OR NOT TSSM_BACKEND_C3D))
  message(FATAL_ERROR "On the 3DS the overlay needs the C3D backend and is not part of the spike.")
endif()
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
                    ${PC_DIR}/iPadTokens.cpp ${PC_DIR}/iPadTouch.cpp
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
list(APPEND AUDIO_SOURCES ${PC_DIR}/iSndReverb.cpp ${PC_DIR}/iSndMeter.cpp)

# --- movies (FFmpeg, optional) ------------------------------------------------
# TSSM's Xbox movies are Bink 1 (FMV/*.bik), which FFmpeg decodes. Point
# CMAKE_PREFIX_PATH at vcpkg's installed/<arch>-windows (the build scripts do).
# Without FFmpeg the null decoder is built and movies report "ran to the end".
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
  message(STATUS "FMV decoder: ffmpeg (${FFMPEG_INCLUDE_DIR})")
else()
  set(FMV_SOURCES ${PC_DIR}/iFMVDecoderNull.cpp)
  set(FMV_LIBRARIES)
  set(FMV_INCLUDES)
  set(FMV_DEFINES)
  message(STATUS "FMV decoder: none (FFmpeg not found; movies will be skipped)")
endif()

if(TSSM_3DS)
  # FFmpeg is built the devkitARM way (short enums; tools/3ds/build_ffmpeg.sh). With int enums
  # AVCodecContext's audio fields and AVFrame's timestamps sit further on, so the two files that
  # read FFmpeg's structs are compiled its way. Neither includes a game header with an enum.
  set_source_files_properties(${PC_DIR}/iFMVDecoderFFmpeg.cpp ${PC_DIR}/iModAudio.cpp
                              PROPERTIES COMPILE_OPTIONS "-fshort-enums")
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
elseif(TSSM_SWITCH)
  # libnx under newlib: no mmap, fork, flock or signals (iHostSwitch.cpp says
  # what each becomes), plus the start-up, applet and crash code (switch/).
  set(HOST_SOURCES ${PC_DIR}/iHostSwitch.cpp ${PC_DIR}/switch/iSwitch.cpp
                   ${PC_DIR}/switch/iSwitchLowMem.cpp)
elseif(TSSM_3DS)
  # libctru under newlib, as the Switch: iHost3DS.cpp is the seam, 3ds/i3DS.cpp
  # the start-up, model check, lifecycle and crash code (libctru's side).
  set(HOST_SOURCES ${PC_DIR}/iHost3DS.cpp ${PC_DIR}/3ds/i3DS.cpp ${PC_DIR}/3ds/i3DSSpike.cpp
                   ${PC_DIR}/3ds/iLayout3DS.cpp)
  # libctru's headers are compiled the way libctru was (short enums; see the
  # TSSM_3DS block above). Nothing of the game's is included there.
  set_source_files_properties(${PC_DIR}/3ds/i3DS.cpp ${PC_DIR}/3ds/i3DSSpike.cpp PROPERTIES
                              COMPILE_OPTIONS "-fshort-enums")
else()
  set(HOST_SOURCES ${PC_DIR}/iHostPosix.cpp)
endif()

# --- window ----------------------------------------------------------------------
if(TSSM_3DS)
  # Nothing is drawn yet, but SDL's 3DS video driver is what scans the buttons
  # and reports HOME's "close", so the window seam pumps it (iWindow3DS.cpp).
  set(WINDOW_SOURCES ${PC_DIR}/iWindow3DS.cpp)
  set(WINDOW_LIBS tssm_sdl)
  set(TSSM_NEEDS_SDL ON)
elseif(TSSM_BACKEND_D3D9 OR TSSM_BACKEND_D3D11 OR TSSM_BACKEND_GL3 OR TSSM_BACKEND_VULKAN)
  set(WINDOW_SOURCES ${PC_DIR}/iWindowSDL.cpp)
  set(WINDOW_LIBS tssm_sdl)
  set(TSSM_NEEDS_SDL ON)
else()
  set(WINDOW_SOURCES ${PC_DIR}/iWindowNull.cpp)
  set(WINDOW_LIBS)
endif()

# --- SDL3 --------------------------------------------------------------------------
# auto: an installed SDL3 if one links, third_party/SDL otherwise.
if(ANDROID)
  set(TSSM_SDL_DEFAULT "vendored")
elseif(TSSM_SWITCH)
  # devkitPro's switch-sdl-3.4 fork, built by tools/switch/build_sdl.sh into a
  # prefix that CMAKE_PREFIX_PATH names; upstream SDL has no homebrew Switch port.
  set(TSSM_SDL_DEFAULT "system")
elseif(TSSM_3DS)
  # Upstream SDL's 3DS port, built from third_party/SDL by
  # tools/3ds/build_sdl.sh into a prefix that CMAKE_PREFIX_PATH names.
  set(TSSM_SDL_DEFAULT "system")
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
if(TSSM_BACKEND_C3D)
  # NULL alongside: RW_NULL keeps out the parts of librw that want a device
  # namespace of librw's own (charset.cpp); the device is rw::c3d.
  set(LIBRW_PLATFORM_LIST "NULL;C3D")
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
  foreach(_patch IN LISTS _librw_patches)
    get_filename_component(_name ${_patch} NAME)
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${_patch}
                    WORKING_DIRECTORY ${_librw_dir} RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL 0)
      execute_process(COMMAND ${GIT_EXECUTABLE} apply ${_patch}
                      WORKING_DIRECTORY ${_librw_dir} RESULT_VARIABLE _rc ERROR_VARIABLE _err)
      # Check it took.
      if(_rc EQUAL 0)
        execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${_patch}
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

# --- the 3DS's renderer: rw::c3d ------------------------------------------------
# librw's backend for citro3d, in the port's tree because it is ours and under
# constant edit; tools/librw/c3d_platform.patch adds only the hooks librw calls
# (PLATFORM_C3D, the device, the skin and MatFX pipelines). Compiled into the
# librw target, with libctru's short enums: citro3d's structs have enum
# members, and src/SB/Core/pc/3ds/c3d/rwc3d.h says why librw's do not mind.
#
# The vertex shaders: picasso has no .include, so each program's .v.pica is
# the shared register map (c3d_uniforms.pinc) followed by its .vsh, and one
# picasso run links them into one SHBIN (one code blob, a DVLE per program, in
# the order c3dimpl.h's ProgramId expects), embedded as a byte array.
if(TSSM_BACKEND_C3D)
  set(_c3d_dir ${PC_DIR}/3ds/c3d)
  set(_c3d_gen ${CMAKE_CURRENT_BINARY_DIR}/c3d)
  file(MAKE_DIRECTORY ${_c3d_gen})
  file(GLOB TSSM_C3D_SOURCES CONFIGURE_DEPENDS ${_c3d_dir}/*.cpp)
  target_sources(librw PRIVATE ${TSSM_C3D_SOURCES})
  set_source_files_properties(${TSSM_C3D_SOURCES}
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/src
    PROPERTIES COMPILE_OPTIONS "-fshort-enums")
  target_include_directories(librw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/src
    ${_c3d_dir} ${_c3d_gen})
  target_link_libraries(librw PRIVATE citro3d)

  file(READ ${_c3d_dir}/shaders/c3d_uniforms.pinc _c3d_head)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_c3d_dir}/shaders/c3d_uniforms.pinc)
  set(_c3d_programs vs_world vs_lit vs_im2d vs_im3d vs_skin vs_env vs_skinenv)
  set(_c3d_picas)
  foreach(_p IN LISTS _c3d_programs)
    file(READ ${_c3d_dir}/shaders/${_p}.vsh _body)
    file(WRITE ${_c3d_gen}/${_p}.v.pica.tmp "${_c3d_head}${_body}")
    configure_file(${_c3d_gen}/${_p}.v.pica.tmp ${_c3d_gen}/${_p}.v.pica COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_c3d_dir}/shaders/${_p}.vsh)
    list(APPEND _c3d_picas ${_c3d_gen}/${_p}.v.pica)
  endforeach()
  if(NOT CTR_PICASSO_EXE)
    message(FATAL_ERROR "picasso was not found (devkitPro's 3ds-tools)")
  endif()
  add_custom_command(
    OUTPUT ${_c3d_gen}/c3d.shbin
    COMMAND ${CTR_PICASSO_EXE} -o ${_c3d_gen}/c3d.shbin ${_c3d_picas}
    DEPENDS ${_c3d_picas}
    WORKING_DIRECTORY ${_c3d_gen}
    COMMENT "Assembling the citro3d vertex shaders"
    VERBATIM)
  add_custom_command(
    OUTPUT ${_c3d_gen}/c3d_shbin.h
    COMMAND ${CMAKE_COMMAND} -DIN=${_c3d_gen}/c3d.shbin -DOUT=${_c3d_gen}/c3d_shbin.h
            -P ${_c3d_dir}/shaders/shbin_embed.cmake
    DEPENDS ${_c3d_gen}/c3d.shbin ${_c3d_dir}/shaders/shbin_embed.cmake
    COMMENT "Embedding the citro3d shader binary"
    VERBATIM)
  add_custom_target(tssm_c3d_shaders DEPENDS ${_c3d_gen}/c3d_shbin.h)
  add_dependencies(librw tssm_c3d_shaders)
  message(STATUS "tssm: 3DS renderer: rw::c3d (${CTR_PICASSO_EXE})")
endif()

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
  )
  if(NOT TSSM_3DS)
    # The launcher's renderer; the 3DS has no launcher (gui/iGuiLauncher.cpp).
    target_sources(tssm_imgui PRIVATE ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
  endif()
  target_include_directories(tssm_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_compile_definitions(tssm_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
  if(TSSM_SWITCH OR TSSM_3DS)
    # ImGui's "open in shell" default forks and execs, which libnx and libctru
    # have not.
    target_compile_definitions(tssm_imgui PUBLIC IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
  endif()
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
    elseif(TSSM_SWITCH)
      # No dlopen for ImGui's own GL loader. librw's glad already holds every
      # entry point, loaded from SDL_GL_GetProcAddress when the device opened,
      # so the backend is compiled against glad's header and shares them. ImGui
      # tells a desktop GL context from an ES one by its version string at run
      # time, so either context librw ends up with works.
      set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp PROPERTIES
        COMPILE_DEFINITIONS "IMGUI_IMPL_OPENGL_LOADER_CUSTOM"
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/src/gl/glad/glad.h")
    endif()
  endif()

  set(GUI_SOURCES
    ${PC_DIR}/gui/iGui.cpp
    ${PC_DIR}/gui/iGuiControls.cpp
    ${PC_DIR}/gui/iGuiFilePicker.cpp
    ${PC_DIR}/gui/iGuiLauncher.cpp
    ${PC_DIR}/gui/iGuiMods.cpp
    ${PC_DIR}/gui/iGuiRender.cpp
    ${PC_DIR}/gui/iGuiSaves.cpp
    ${PC_DIR}/gui/iGuiSelfTest.cpp
    ${PC_DIR}/gui/iGuiSettings.cpp
    ${PC_DIR}/gui/iGuiTabs.cpp
    ${PC_DIR}/gui/iGuiUtil.cpp
  )

  # The About tab's version line: the branch and `git describe` at configure
  # time (re-run CMake for a newer one). "-dirty" leaves out changes inside
  # submodules: the librw patches above are in every build's checkout.
  execute_process(COMMAND git describe --always --abbrev=10
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE _tssm_describe OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  execute_process(COMMAND git diff --quiet --ignore-submodules=dirty HEAD --
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  RESULT_VARIABLE _tssm_dirty OUTPUT_QUIET ERROR_QUIET)
  if(_tssm_describe AND _tssm_dirty EQUAL 1)
    set(_tssm_describe "${_tssm_describe}-dirty")
  endif()
  execute_process(COMMAND git rev-parse --abbrev-ref HEAD
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE _tssm_branch OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT _tssm_describe)
    set(_tssm_describe "unknown")
  endif()
  set(TSSM_GUI_VERSION "${_tssm_describe}")
  if(_tssm_branch)
    set(TSSM_GUI_VERSION "${_tssm_branch} ${_tssm_describe}")
  endif()
  set_source_files_properties(${PC_DIR}/gui/iGuiTabs.cpp PROPERTIES
    COMPILE_DEFINITIONS "TSSM_GUI_VERSION=\"${TSSM_GUI_VERSION}\"")

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
  tssm_gui_notice("The SpongeBob SquarePants Movie PC port" "CC0 1.0" "This program's own code."
                  "" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  tssm_gui_notice("Dear ImGui" "MIT" "The settings overlay and the launcher."
                  "https://github.com/ocornut/imgui" "${IMGUI_DIR}/LICENSE.txt")
  tssm_gui_notice("SDL" "zlib" "Windows, input, audio output, dialogs."
                  "https://libsdl.org" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/LICENSE.txt")
  tssm_gui_notice("librw" "MIT" "The RenderWare graphics layer the game draws through."
                  "" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/librw/LICENSE")
  if(FMV_INCLUDES AND TSSM_SWITCH)
    # A .nro is one static image. TODO(switch): the LGPL's relinking terms for a
    # static link (offer the object files) before any release.
    tssm_gui_notice("FFmpeg" "LGPL 2.1 or later"
                    "Movie decoding. Linked statically into tssm.nro, built by tools/switch/build_ffmpeg.sh from the source at ffmpeg.org."
                    "https://ffmpeg.org" "${FFMPEG_INCLUDE_DIR}/../share/ffmpeg/copyright")
  elseif(FMV_INCLUDES)
    tssm_gui_notice("FFmpeg" "LGPL 2.1 or later" "Movie decoding. Linked dynamically; its source is at ffmpeg.org."
                    "https://ffmpeg.org" "${FFMPEG_INCLUDE_DIR}/../share/ffmpeg/copyright")
  endif()
  if(TSSM_SWITCH)
    # The Switch's system libraries, linked statically into the .nro. devkitPro
    # installs no license files for them, so dist/switch/licenses carries the
    # notes (TODO(switch) there: the exact copyright lines before any release).
    tssm_gui_notice("Mesa" "MIT" "OpenGL on the Switch (EGL and the nouveau driver). Linked statically."
                    "https://mesa3d.org" "${CMAKE_CURRENT_SOURCE_DIR}/dist/switch/licenses/mesa.txt")
    tssm_gui_notice("libdrm_nouveau" "MIT" "The GPU interface Mesa uses on the Switch. Linked statically."
                    "" "${CMAKE_CURRENT_SOURCE_DIR}/dist/switch/licenses/libdrm_nouveau.txt")
    tssm_gui_notice("libnx" "ISC" "The Switch homebrew system library. Linked statically."
                    "https://github.com/switchbrew/libnx" "${CMAKE_CURRENT_SOURCE_DIR}/dist/switch/licenses/libnx.txt")
  endif()
  if(TSSM_3DS)
    # Linked statically into tssm.3dsx (dist/3ds/licenses; TODO(3ds) there: the
    # exact copyright lines before any release).
    tssm_gui_notice("libctru" "zlib" "The 3DS homebrew system library. Linked statically."
                    "https://github.com/devkitPro/libctru" "${CMAKE_CURRENT_SOURCE_DIR}/dist/3ds/licenses/libctru.txt")
    tssm_gui_notice("citro3d" "zlib" "The 3DS's GPU library, which draws the game. Linked statically."
                    "https://github.com/devkitPro/citro3d" "${CMAKE_CURRENT_SOURCE_DIR}/dist/3ds/licenses/citro3d.txt")
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
  ${PC_DIR}/iD0Stats.cpp
  ${PC_DIR}/iSpike.cpp
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
  # The load screen's "no saves" message, made to name the slot (game only:
  # it rewrites a loaded TEXT asset).
  ${PC_DIR}/iSaveText.cpp
  ${PC_DIR}/iSaveZip.cpp
  ${PC_DIR}/iSnd.cpp
  ${PC_DIR}/iSndData.cpp
  ${PC_DIR}/iSndXbox.cpp
  ${PC_DIR}/iSystem.cpp
  ${PC_DIR}/iTime.cpp

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
if(TSSM_GUI)
  target_link_libraries(tssm_platform PRIVATE tssm_imgui)
  target_include_directories(tssm_platform PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gui)
  if(TSSM_3DS)
    # The bottom screen's window, which the overlay draws on (iWindow3DS.cpp).
    target_compile_definitions(tssm_rw PRIVATE TSSM_3DS_OVERLAY)
  endif()
endif()
target_compile_options(tssm_platform PRIVATE ${TSSM_NO_WARNINGS})
if(FMV_INCLUDES)
  target_include_directories(tssm_platform PRIVATE ${FMV_INCLUDES})
endif()
if(FMV_DEFINES)
  target_compile_definitions(tssm_platform PRIVATE ${FMV_DEFINES})
endif()
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
# The widening reads the game's asset structs, so it sees them as the game
# code does: the game's flags and its prefix header.
set_source_files_properties(${PC_DIR}/iAssetWiden.cpp ${PC_DIR}/iAssetHooks.cpp PROPERTIES
  COMPILE_OPTIONS "${TSSM_GAME_OPTIONS};-include;${SB_DIR}/SBPrefix.h"
  COMPILE_DEFINITIONS "NON_MATCHING")
if(TSSM_3DS)
  # The enum-size checks read the game's asset structs the same way.
  set_source_files_properties(${PC_DIR}/3ds/iLayout3DS.cpp PROPERTIES
    COMPILE_OPTIONS "${TSSM_GAME_OPTIONS};-include;${SB_DIR}/SBPrefix.h"
    COMPILE_DEFINITIONS "NON_MATCHING")
endif()

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
  # Port-only, but it calls game code, so it is compiled as a game unit: the
  # debug events of unattended runs (iTestEvents.h).
  ${PC_DIR}/iTestEvents.cpp)
target_link_libraries(tssm_game PUBLIC tssm_platform tssm_rw)
target_compile_definitions(tssm_game PRIVATE NON_MATCHING)
# TSSM_TEST_SEND, an event to a scene object at a frame (iTestSend.h): a game
# unit too, for the same reason.
target_sources(tssm_game PRIVATE ${PC_DIR}/iTestSend.cpp)
if(TSSM_3DS AND TSSM_BACKEND_C3D)
  # The 3DS renderer's stereo hook (3ds/iStereo3DS.h): it reads the game's camera
  # and player, so it is a game unit. The speed test (NULL backend) has none.
  target_sources(tssm_game PRIVATE ${PC_DIR}/3ds/iStereo3DS.cpp)
endif()
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
if(NOT ANDROID AND NOT TSSM_SWITCH AND NOT TSSM_3DS)
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
target_link_libraries(${TSSM_GAME_TARGET} PRIVATE tssm_game tssm_platform tssm_rw)
if(TSSM_SWITCH OR TSSM_3DS)
  # The platform layer calls back into the game library (iCapture ->
  # iTestEvents), and GNU ld reads each archive once, in order. Declaring the
  # cycle makes CMake list the pair twice.
  target_link_libraries(tssm_platform INTERFACE tssm_game)
endif()
target_compile_options(${TSSM_GAME_TARGET} PRIVATE ${TSSM_NO_WARNINGS})
target_compile_definitions(${TSSM_GAME_TARGET} PRIVATE ${FMV_DEFINES})

if(NOT WIN32)
  if(NOT ANDROID AND NOT TSSM_SWITCH AND NOT TSSM_3DS)
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

# --- the app icon ----------------------------------------------------------------
# The repository carries no icon art. TSSM_ICON_DIR names a folder that
# tools/make_icons.ps1 made from a picture, outside the repository
# (docs/ICON.md). On Windows, tssm.exe then carries its tssm.ico. SDL registers
# its window class with the executable's first icon, so the game's window and
# the launcher's use it too (title bar, taskbar, Alt+Tab), at the size the
# display's DPI asks for. Left empty, nothing changes: no icon resource.
set(TSSM_ICON_DIR "" CACHE PATH
  "Folder made by tools/make_icons.ps1 (docs/ICON.md); empty keeps the default icon")
if(TSSM_ICON_DIR AND WIN32 AND NOT ANDROID)
  if(NOT EXISTS "${TSSM_ICON_DIR}/tssm.ico")
    message(FATAL_ERROR "TSSM_ICON_DIR is ${TSSM_ICON_DIR}, which has no tssm.ico. "
      "Make the folder with tools/make_icons.ps1 (docs/ICON.md).")
  endif()
  enable_language(RC)
  # Copied into the build directory (a changed icon reconfigures), and the
  # resource script is written there too: neither enters the source tree.
  set(TSSM_ICON_BUILD "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon")
  configure_file("${TSSM_ICON_DIR}/tssm.ico" "${TSSM_ICON_BUILD}/tssm.ico" COPYONLY)
  file(GENERATE OUTPUT "${TSSM_ICON_BUILD}/tssm_icon.rc"
    CONTENT "1 ICON \"${TSSM_ICON_BUILD}/tssm.ico\"\n")
  target_sources(tssm PRIVATE "${TSSM_ICON_BUILD}/tssm_icon.rc")
  set_source_files_properties("${TSSM_ICON_BUILD}/tssm_icon.rc" PROPERTIES
    GENERATED ON OBJECT_DEPENDS "${TSSM_ICON_BUILD}/tssm.ico")
  message(STATUS "App icon: ${TSSM_ICON_DIR}/tssm.ico")
endif()

# --- the Switch package -------------------------------------------------------------
# tssm.elf -> tssm.nro: the executable, the NACP (the name and version hbmenu
# shows), a 256x256 JPEG icon and a RomFS. docs/SWITCH.md has the SD card
# layout. The game's files are never in it: players copy their own.
if(TSSM_SWITCH)
  # libnx's default pthread stack is small, and the game's worker threads
  # (SDL's audio thread, std::thread users) were written against desktop
  # defaults. iHostSwitch.cpp's __wrap_pthread_create gives every thread
  # created without an explicit, larger size 2 MB.
  target_link_options(tssm PRIVATE -Wl,--wrap=pthread_create)

  # The RomFS: only the port's own files, assembled in the build directory
  # (dist/switch/romfs plus the license texts the About tab also shows).
  set(TSSM_SWITCH_ROMFS ${CMAKE_CURRENT_BINARY_DIR}/romfs)
  file(MAKE_DIRECTORY ${TSSM_SWITCH_ROMFS}/licenses)
  file(GLOB _romfs_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dist/switch/romfs/*)
  foreach(_f IN LISTS _romfs_files)
    get_filename_component(_n ${_f} NAME)
    configure_file(${_f} ${TSSM_SWITCH_ROMFS}/${_n} COPYONLY)
  endforeach()
  foreach(_lic
      "LICENSE|tssm-port.txt"
      "third_party/SDL/LICENSE.txt|SDL.txt"
      "third_party/librw/LICENSE|librw.txt"
      "third_party/imgui/LICENSE.txt|imgui.txt"
      "dist/switch/licenses/mesa.txt|mesa.txt"
      "dist/switch/licenses/libdrm_nouveau.txt|libdrm_nouveau.txt"
      "dist/switch/licenses/libnx.txt|libnx.txt")
    string(REPLACE "|" ";" _pair "${_lic}")
    list(GET _pair 0 _from)
    list(GET _pair 1 _to)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_from})
      configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${_from} ${TSSM_SWITCH_ROMFS}/licenses/${_to} COPYONLY)
    endif()
  endforeach()
  if(FMV_INCLUDES AND EXISTS "${FFMPEG_INCLUDE_DIR}/../share/ffmpeg/copyright")
    configure_file("${FFMPEG_INCLUDE_DIR}/../share/ffmpeg/copyright"
                   ${TSSM_SWITCH_ROMFS}/licenses/FFmpeg-LGPL-2.1.txt COPYONLY)
  endif()

  # A picture from TSSM_ICON_DIR when one was made (docs/ICON.md), else the
  # port's own default: a plain sponge block, drawn for the port.
  if(TSSM_ICON_DIR AND EXISTS "${TSSM_ICON_DIR}/icon.jpg")
    set(_nro_icon "${TSSM_ICON_DIR}/icon.jpg")
  else()
    set(_nro_icon "${CMAKE_CURRENT_SOURCE_DIR}/dist/switch/default_icon.jpg")
  endif()
  message(STATUS "Switch: icon ${_nro_icon}")

  set(TSSM_SWITCH_VERSION "0.1.0" CACHE STRING "The version the Switch's NACP shows")
  nx_generate_nacp(tssm.nacp NAME "TSSM (port)" AUTHOR "TSSM port" VERSION "${TSSM_SWITCH_VERSION}")
  nx_create_nro(tssm NACP tssm.nacp ICON "${_nro_icon}" ROMFS "${TSSM_SWITCH_ROMFS}")
endif()

# --- the 3DS package ----------------------------------------------------------------
# tssm.elf -> tssm.3dsx: the executable, the SMDH (the name, description and
# 48x48 icon the Homebrew Launcher shows) and a RomFS. docs/3DS.md has the SD
# card layout. The game's files are never in it: players copy their own.
if(TSSM_3DS)
  # Every exit() goes through 3ds/i3DS.cpp's quit, which stops the threads
  # and services that would outlive libctru's freeing of the heaps; every
  # libctru thread is logged as it is made.
  # The DSP's and GSP's cache calls are made by the game itself, so that
  # no service is given a handle to the game's process (the exit fix).
  target_link_options(tssm PRIVATE -Wl,--wrap=exit -Wl,--wrap=threadCreate
    -Wl,--wrap=DSP_FlushDataCache -Wl,--wrap=DSP_InvalidateDataCache
    -Wl,--wrap=GSPGPU_FlushDataCache -Wl,--wrap=GSPGPU_InvalidateDataCache)

  # The RomFS: only the port's own files, assembled in the build directory
  # (the license texts of what is linked in).
  set(TSSM_3DS_ROMFS ${CMAKE_CURRENT_BINARY_DIR}/romfs)
  file(MAKE_DIRECTORY ${TSSM_3DS_ROMFS}/licenses)
  foreach(_lic
      "LICENSE|tssm-port.txt"
      "third_party/SDL/LICENSE.txt|SDL.txt"
      "third_party/librw/LICENSE|librw.txt"
      "dist/3ds/licenses/libctru.txt|libctru.txt"
      "dist/3ds/licenses/citro3d.txt|citro3d.txt")
    string(REPLACE "|" ";" _pair "${_lic}")
    list(GET _pair 0 _from)
    list(GET _pair 1 _to)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_from})
      configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${_from} ${TSSM_3DS_ROMFS}/licenses/${_to} COPYONLY)
    endif()
  endforeach()
  if(FMV_INCLUDES AND EXISTS "${FFMPEG_INCLUDE_DIR}/../share/ffmpeg/copyright")
    configure_file("${FFMPEG_INCLUDE_DIR}/../share/ffmpeg/copyright"
                   ${TSSM_3DS_ROMFS}/licenses/FFmpeg-LGPL-2.1.txt COPYONLY)
  endif()
  if(TSSM_GUI)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/LICENSE.txt
                   ${TSSM_3DS_ROMFS}/licenses/imgui.txt COPYONLY)
  endif()

  # The icon: TSSM_3DS_ICON, an icon made from the player's own game files
  # (tools/3ds/icon_from_game_files.py; the .cia gets it too, below); else
  # TSSM_ICON_DIR's icon-48.png (the SMDH's size; smdhtool takes nothing
  # else), made from the picture by tools/make_icons.ps1 like the Switch's
  # icon.jpg (docs/ICON.md); else libctru's default icon.
  set(TSSM_3DS_ICON "" CACHE PATH
    "The 3DS icon: a folder with icon-48.png (and icon-24.png) or a 48x48 PNG (tools/3ds/icon_from_game_files.py); empty keeps the usual icons")
  set(_3ds_icon48)
  set(_3ds_icon24)
  if(TSSM_3DS_ICON)
    if(IS_DIRECTORY "${TSSM_3DS_ICON}")
      set(_3ds_icon48 "${TSSM_3DS_ICON}/icon-48.png")
      if(EXISTS "${TSSM_3DS_ICON}/icon-24.png")
        set(_3ds_icon24 "${TSSM_3DS_ICON}/icon-24.png")
      endif()
    else()
      set(_3ds_icon48 "${TSSM_3DS_ICON}")
    endif()
    if(NOT EXISTS "${_3ds_icon48}")
      message(FATAL_ERROR "3DS: TSSM_3DS_ICON is ${TSSM_3DS_ICON}, but ${_3ds_icon48} does not exist. "
        "Make the icon with tools/3ds/icon_from_game_files.py (docs/3DS.md).")
    endif()
    # A PNG's width and height are bytes 16 to 23; the SMDH's icons are 48x48
    # and 24x24 exactly.
    foreach(_pair "${_3ds_icon48}|48" "${_3ds_icon24}|24")
      string(REPLACE "|" ";" _pair "${_pair}")
      list(GET _pair 0 _png)
      list(GET _pair 1 _side)
      if(_png)
        file(READ "${_png}" _png_head LIMIT 24 HEX)
        string(SUBSTRING "${_png_head}" 0 16 _png_magic)
        string(SUBSTRING "${_png_head}" 32 16 _png_size)
        math(EXPR _want "${_side}" OUTPUT_FORMAT HEXADECIMAL)
        string(REPLACE "0x" "" _want "${_want}")
        string(LENGTH "${_want}" _len)
        while(_len LESS 8)
          string(PREPEND _want "0")
          math(EXPR _len "${_len} + 1")
        endwhile()
        if(NOT _png_magic STREQUAL "89504e470d0a1a0a" OR NOT _png_size STREQUAL "${_want}${_want}")
          message(FATAL_ERROR "3DS: ${_png} is not a ${_side}x${_side} PNG picture. "
            "Make the icon with tools/3ds/icon_from_game_files.py (docs/3DS.md).")
        endif()
      endif()
    endforeach()
    # Copied into the build directory, so a changed icon rebuilds the SMDHs.
    configure_file("${_3ds_icon48}" "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon/3ds-icon-48.png" COPYONLY)
    set(_3ds_icon48 "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon/3ds-icon-48.png")
    if(_3ds_icon24)
      configure_file("${_3ds_icon24}" "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon/3ds-icon-24.png" COPYONLY)
      set(_3ds_icon24 "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon/3ds-icon-24.png")
    endif()
  endif()
  set(_smdh_icon_args)
  if(_3ds_icon48)
    set(_smdh_icon_args ICON "${_3ds_icon48}")
    message(STATUS "3DS: icon ${TSSM_3DS_ICON} (made from your own game files: do not share the .3dsx or .cia)")
  elseif(TSSM_ICON_DIR)
    if(NOT EXISTS "${TSSM_ICON_DIR}/icon-48.png")
      message(FATAL_ERROR "TSSM_ICON_DIR is ${TSSM_ICON_DIR}, which has no icon-48.png. "
        "Make the folder again with tools/make_icons.ps1 (docs/ICON.md).")
    endif()
    # Copied into the build directory, so a changed icon rebuilds the SMDH.
    configure_file("${TSSM_ICON_DIR}/icon-48.png" "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon/icon-48.png"
                   COPYONLY)
    set(_smdh_icon_args ICON "${CMAKE_CURRENT_BINARY_DIR}/tssm_icon/icon-48.png")
    message(STATUS "3DS: icon ${TSSM_ICON_DIR}/icon-48.png")
  else()
    message(STATUS "3DS: icon: libctru's default")
  endif()

  set(TSSM_3DS_VERSION "0.1.0" CACHE STRING "The version in the 3DS's SMDH description")
  # The author the Homebrew Launcher shows (the .3dsx) and the publisher the
  # HOME Menu shows (the .cia).
  set(TSSM_3DS_AUTHOR "aidp2" CACHE STRING "The author and publisher in the 3DS's SMDH")
  if(TSSM_GUI)
    # The overlay's About tab gives the same number.
    set_property(SOURCE ${PC_DIR}/gui/iGuiTabs.cpp APPEND PROPERTY
                 COMPILE_DEFINITIONS "TSSM_3DS_VERSION_TEXT=\"${TSSM_3DS_VERSION}\"")
  endif()
  if(TSSM_3DS_SPIKE)
    # tssm-spike.elf and tssm-spike.3dsx, so the two programs can sit side by
    # side and a crash address is looked up in the right ELF.
    set_target_properties(tssm PROPERTIES OUTPUT_NAME tssm-spike)
    ctr_generate_smdh(tssm.smdh
      NAME "TSSM 3DS spike"
      DESCRIPTION "Speed test ${TSSM_3DS_VERSION}: runs by itself"
      AUTHOR "${TSSM_3DS_AUTHOR}"
      ${_smdh_icon_args})
  elseif(NOT TSSM_3DS_PROBE STREQUAL "0")
    set_property(SOURCE ${PC_DIR}/3ds/i3DS.cpp APPEND PROPERTY
                 COMPILE_DEFINITIONS "TSSM_3DS_PROBE=${TSSM_3DS_PROBE}")
    if(TSSM_3DS_PROBE STREQUAL "11")
      set(_probe_desc "Console probe 11: restarts itself")
    else()
      set(_probe_desc "Console probe ${TSSM_3DS_PROBE}: plays by itself")
    endif()
    ctr_generate_smdh(tssm.smdh
      NAME "TSSM probe ${TSSM_3DS_PROBE}"
      DESCRIPTION "${_probe_desc}"
      AUTHOR "${TSSM_3DS_AUTHOR}"
      ${_smdh_icon_args})
    message(STATUS "3DS: console probe ${TSSM_3DS_PROBE}")
  else()
    ctr_generate_smdh(tssm.smdh
      NAME "The SpongeBob SquarePants Movie Videogame"
      DESCRIPTION "Homebrew port ${TSSM_3DS_VERSION}"
      AUTHOR "${TSSM_3DS_AUTHOR}"
      ${_smdh_icon_args})
  endif()
  ctr_create_3dsx(tssm SMDH tssm.smdh ROMFS "${TSSM_3DS_ROMFS}")

  # tssm.cia beside tssm.3dsx, from the same ELF and RomFS, to install with a
  # CIA installer and start from the HOME Menu (docs/3DS.md). makerom
  # (Project_CTR) and bannertool are not devkitPro's: found on the PATH or in
  # TSSM_CTR_TOOLS; without them only the .3dsx is made. The banner and icon
  # are the port's own (dist/3ds, drawn by tools/3ds/draw_cia_art.ps1), or the
  # banner is TSSM_3DS_BANNER: a .bnr the player made from their own game
  # files (tools/3ds/banner_from_game_files.py), never one in the repository;
  # the icon likewise TSSM_3DS_ICON (tools/3ds/icon_from_game_files.py).
  if(NOT TSSM_3DS_SPIKE)
    set(TSSM_CTR_TOOLS "$ENV{TSSM_CTR_TOOLS}" CACHE PATH "Folder with makerom and bannertool, for tssm.cia")
    set(TSSM_3DS_BANNER "" CACHE FILEPATH "A .bnr for tssm.cia instead of the port's plain banner (tools/3ds/banner_from_game_files.py)")
    find_program(TSSM_MAKEROM NAMES makerom HINTS "${TSSM_CTR_TOOLS}" NO_CMAKE_FIND_ROOT_PATH)
    find_program(TSSM_BANNERTOOL NAMES bannertool HINTS "${TSSM_CTR_TOOLS}" NO_CMAKE_FIND_ROOT_PATH)
    if(TSSM_MAKEROM AND TSSM_BANNERTOOL)
      string(REPLACE "." ";" _ver "${TSSM_3DS_VERSION}.0.0")
      list(GET _ver 0 _major)
      list(GET _ver 1 _minor)
      list(GET _ver 2 _micro)
      # Relative paths only: the tools are Windows programs under MSYS2's make.
      set(_cia ${CMAKE_CURRENT_BINARY_DIR}/cia)
      foreach(_f cia_banner.png cia_icon.png cia_chime.wav)
        configure_file(dist/3ds/${_f} ${_cia}/${_f} COPYONLY)
      endforeach()
      # The icon: the port's own, or TSSM_3DS_ICON's (above), with its 24x24
      # one when there is one (bannertool makes the small one otherwise).
      if(_3ds_icon48)
        configure_file("${_3ds_icon48}" ${_cia}/icon-48.png COPYONLY)
        set(_cia_icon_args -i cia/icon-48.png)
        set(_cia_icon_deps ${_cia}/icon-48.png)
        if(_3ds_icon24)
          configure_file("${_3ds_icon24}" ${_cia}/icon-24.png COPYONLY)
          list(APPEND _cia_icon_args -si cia/icon-24.png)
          list(APPEND _cia_icon_deps ${_cia}/icon-24.png)
        endif()
      else()
        set(_cia_icon_args -i cia/cia_icon.png)
        set(_cia_icon_deps ${_cia}/cia_icon.png)
      endif()
      configure_file(tools/3ds/tssm.rsf ${_cia}/tssm.rsf COPYONLY)
      # The banner: the port's picture and chime made into cia/tssm.bnr, or the
      # player's own .bnr copied to cia/banner.bnr (copied when it changes).
      if(TSSM_3DS_BANNER)
        if(NOT EXISTS "${TSSM_3DS_BANNER}")
          message(FATAL_ERROR "3DS: TSSM_3DS_BANNER ${TSSM_3DS_BANNER} does not exist")
        endif()
        configure_file("${TSSM_3DS_BANNER}" ${_cia}/banner.bnr COPYONLY)
        set(_cia_bnr cia/banner.bnr)
        set(_cia_bnr_make)
        set(_cia_bnr_deps ${_cia}/banner.bnr)
      else()
        set(_cia_bnr cia/tssm.bnr)
        set(_cia_bnr_make COMMAND "${TSSM_BANNERTOOL}" makebanner -i cia/cia_banner.png -a cia/cia_chime.wav -o cia/tssm.bnr)
        set(_cia_bnr_deps ${_cia}/cia_banner.png ${_cia}/cia_chime.wav)
      endif()
      # The licence texts, so that a changed one remakes the .cia too.
      file(GLOB_RECURSE _cia_romfs ${TSSM_3DS_ROMFS}/*)
      # bannertool cuts the short name at 32 characters (the SMDH holds 64), so
      # tools/3ds/smdh_names.py writes the whole name back when Python is found.
      set(_cia_name "The SpongeBob SquarePants Movie Videogame")
      find_program(TSSM_PYTHON NAMES python3 python NO_CMAKE_FIND_ROOT_PATH)
      set(_cia_name_fix)
      if(TSSM_PYTHON)
        configure_file(tools/3ds/smdh_names.py ${_cia}/smdh_names.py COPYONLY)
        set(_cia_name_fix COMMAND "${TSSM_PYTHON}" cia/smdh_names.py cia/tssm.icn --short "${_cia_name}")
      else()
        message(WARNING "3DS: no Python (TSSM_PYTHON): the .cia's short name stays cut at 32 characters")
      endif()
      add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/tssm.cia
        ${_cia_bnr_make}
        COMMAND "${TSSM_BANNERTOOL}" makesmdh -s "${_cia_name}" -l "${_cia_name}"
                -p "${TSSM_3DS_AUTHOR}" ${_cia_icon_args} -f visible,recordusage,nosavebackups,new3ds
                -r regionfree -o cia/tssm.icn
        ${_cia_name_fix}
        COMMAND "${TSSM_MAKEROM}" -f cia -o tssm.cia -target t -rsf cia/tssm.rsf -exefslogo
                -elf $<TARGET_FILE_NAME:tssm> -icon cia/tssm.icn -banner ${_cia_bnr} -DROMFS=romfs
                -major ${_major} -minor ${_minor} -micro ${_micro}
        DEPENDS tssm ${_cia}/tssm.rsf ${_cia_icon_deps} ${_cia_bnr_deps}
                ${_cia_romfs} ${CMAKE_CURRENT_SOURCE_DIR}/tools/3ds/smdh_names.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "tssm.cia"
        VERBATIM)
      add_custom_target(tssm_cia ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/tssm.cia)
      message(STATUS "3DS: tssm.cia with ${TSSM_MAKEROM} and ${TSSM_BANNERTOOL}")
      if(TSSM_3DS_BANNER)
        message(STATUS "3DS: tssm.cia's banner: ${TSSM_3DS_BANNER} (made from your own game files: do not share that .cia)")
      endif()
      if(_3ds_icon48)
        message(STATUS "3DS: tssm.cia's icon: ${TSSM_3DS_ICON}")
      endif()
    else()
      message(STATUS "3DS: no makerom or bannertool (TSSM_CTR_TOOLS); tssm.3dsx only")
    endif()
  endif()
endif()

# --- tests ----------------------------------------------------------------------
if(TSSM_SWITCH OR TSSM_3DS)
  set(_tssm_tests_default OFF)
else()
  set(_tssm_tests_default ON)
endif()
option(TSSM_BUILD_TESTS "Build the platform layer's unit tests" ${_tssm_tests_default})

# --- the headless input test -------------------------------------------------------
# tssm_input_test drives SDL's virtual controllers and a stand-in keyboard
# through the game's own xPadUpdate (src/SB/Core/x/xPad.cpp, compiled in with
# the game's flags) and checks what the game sees. No game data, no window.
#   build-*/tssm_input_test [scratch folder]
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
    ${PC_DIR}/iSaveStore.cpp
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

# --- tssm_modtest ----------------------------------------------------------------
# tssm_modtest: the mod system run on its own -- INI overlays, sound decoding,
# package views checked against real packages, the mod manager. No game code,
# renderer or window. Run it as `tssm_modtest <asset folder> <scratch folder>`
# (src/SB/Core/pc/tests/modtest.cpp, docs/MODDING.md).
if(TSSM_BUILD_TESTS AND NOT ANDROID AND NOT TSSM_SWITCH AND NOT TSSM_3DS)
  add_executable(tssm_modtest ${PC_DIR}/tests/modtest.cpp
    ${PC_DIR}/iMod.cpp ${PC_DIR}/iModAudio.cpp ${PC_DIR}/iModIni.cpp ${PC_DIR}/iModPackage.cpp
    ${HOST_SOURCES})
  target_include_directories(tssm_modtest PRIVATE ${PC_DIR}/compat ${PC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  if(FMV_INCLUDES)
    target_include_directories(tssm_modtest PRIVATE ${FMV_INCLUDES})
    target_compile_definitions(tssm_modtest PRIVATE ${FMV_DEFINES})
    target_link_libraries(tssm_modtest PRIVATE ${FFMPEG_AVFORMAT_LIBRARY}
      ${FFMPEG_AVCODEC_LIBRARY} ${FFMPEG_SWRESAMPLE_LIBRARY} ${FFMPEG_AVUTIL_LIBRARY})
  endif()
  if(WIN32)
    target_link_libraries(tssm_modtest PRIVATE user32 dbghelp)
  elseif(NOT APPLE)
    target_link_libraries(tssm_modtest PRIVATE dl)
  endif()
endif()
