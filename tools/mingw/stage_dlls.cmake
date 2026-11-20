# Copies beside a MinGW-built executable the DLLs it needs from the folders a
# DLL-built FFmpeg or SDL came from (MSYS2's /mingw64/bin, say), following the
# imports of the executable and of every DLL it loads, and nothing else: those
# folders hold every package's DLLs, and copying them all is hundreds of files.
# A DLL already beside the executable, or in Windows' own folders, is left
# where it is, as the loader would find it there first. Run by CMakeLists.txt
# after each link of tssm.exe:
#
#   cmake -DEXE=<tssm.exe> -DDIRS=<dir>|<dir>... -DOBJDUMP=<objdump> -P stage_dlls.cmake

cmake_minimum_required(VERSION 3.16)
# Windows' own paths come back with backslashes; match them normalized.
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

if(NOT EXE OR NOT DIRS OR NOT OBJDUMP)
  message(FATAL_ERROR "usage: cmake -DEXE=<exe> -DDIRS=<dir>|<dir> -DOBJDUMP=<objdump> -P stage_dlls.cmake")
endif()
string(REPLACE "|" ";" DIRS "${DIRS}")
get_filename_component(DEST "${EXE}" DIRECTORY)

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${EXE}"
  DIRECTORIES ${DIRS}
  RESOLVED_DEPENDENCIES_VAR _found
  UNRESOLVED_DEPENDENCIES_VAR _missing
  # The API sets resolve inside Windows, not to files.
  PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
  POST_EXCLUDE_REGEXES "^[A-Za-z]:[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]")

string(TOLOWER "${DEST}" _dest_lower)
set(_staged 0)
foreach(_dll IN LISTS _found)
  get_filename_component(_dir "${_dll}" DIRECTORY)
  get_filename_component(_name "${_dll}" NAME)
  string(TOLOWER "${_dir}" _dir_lower)
  if(NOT _dir_lower STREQUAL _dest_lower)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "${DEST}/${_name}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "could not copy ${_dll} to ${DEST}")
    endif()
  endif()
  math(EXPR _staged "${_staged} + 1")
endforeach()
message(STATUS "${_staged} DLLs beside ${EXE}")
if(_missing)
  message(WARNING "tssm.exe imports DLLs found nowhere: ${_missing}")
endif()
