# The co-op version's fingerprint (docs/MULTIPLAYER.md 14.1), run at every
# build by CMakeLists.txt and by hand on any checkout:
#
#   cmake -DSRC=<repository> [-DOUT=<coop_id.h>] [-DGIT=<git>] -P tools/coop_id.cmake
#
# It hashes the git object IDs of what both games must agree on: the game
# code's trees and the co-op and protocol files. Not the commit, so the PC,
# Android and Switch branches give one ID when that code is the same; not the
# renderer (librw is patched at every PC configure), the socket backends, the
# GUI or the platform folders, which do not change what the two games agree
# on. A working-copy change to a covered file is mixed in by content, so a
# changed copy gets its own ID and two identical changes still match.
#
# Prints the ID and the version code the Co-op tab shows. With OUT, writes the
# header iCoop.cpp includes, only when its text changes.

cmake_minimum_required(VERSION 3.16)

if(NOT SRC)
  message(FATAL_ERROR "coop_id.cmake: pass -DSRC=<repository>")
endif()
if(NOT GIT)
  find_program(GIT git)
endif()

# Covered: these trees whole, and in src/SB/Core/pc the files matching
# kPcFiles but not kPcSkip. Local co-op's files are left out: they are the
# pipe between two copies of one program on one PC, never between devices.
set(kTrees src/SB/Game src/SB/Core/x src/SB/Core/gc include src/rwsdk src/dolphin)
set(kPcDir src/SB/Core/pc)
set(kPcFiles "^src/SB/Core/pc/(iCoop[A-Za-z0-9_]*\\.(cpp|h)|iNetProto\\.(cpp|h)|iNetSession\\.(cpp|h)|iNetWire\\.h|iNetDisc\\.(cpp|h)|iCutscene\\.cpp)$")
set(kPcSkip "^src/SB/Core/pc/iCoopLocal")

function(coop_covered path out)
  set(hit FALSE)
  foreach(t ${kTrees})
    string(LENGTH "${t}/" n)
    string(SUBSTRING "${path}" 0 ${n} head)
    if(head STREQUAL "${t}/")
      set(hit TRUE)
    endif()
  endforeach()
  if(path MATCHES "${kPcFiles}" AND NOT path MATCHES "${kPcSkip}")
    set(hit TRUE)
  endif()
  set(${out} ${hit} PARENT_SCOPE)
endfunction()

function(coop_git out ok)
  execute_process(COMMAND "${GIT}" -C "${SRC}" ${ARGN}
                  RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_QUIET)
  set(${out} "${o}" PARENT_SCOPE)
  if(r EQUAL 0)
    set(${ok} TRUE PARENT_SCOPE)
  else()
    set(${ok} FALSE PARENT_SCOPE)
  endif()
endfunction()

set(text "tssm-coop id/1\n")
set(good FALSE)
if(GIT)
  set(good TRUE)
  foreach(t ${kTrees})
    coop_git(id ok rev-parse --verify -q "HEAD:${t}")
    string(STRIP "${id}" id)
    if(NOT ok OR id STREQUAL "")
      set(good FALSE)
    endif()
    string(APPEND text "tree ${t} ${id}\n")
  endforeach()
  coop_git(listing ok ls-tree HEAD "${kPcDir}/")
  if(NOT ok)
    set(good FALSE)
  endif()
  string(REPLACE "\n" ";" lines "${listing}")
  set(blobs 0)
  foreach(l ${lines})
    # "<mode> blob <id>\t<path>"
    if(l MATCHES "^[0-7]+ blob ([0-9a-f]+)\t(.+)$")
      set(id "${CMAKE_MATCH_1}")
      set(p "${CMAKE_MATCH_2}")
      coop_covered("${p}" hit)
      if(hit)
        string(APPEND text "blob ${p} ${id}\n")
        math(EXPR blobs "${blobs} + 1")
      endif()
    endif()
  endforeach()
  if(blobs EQUAL 0)
    set(good FALSE)
  endif()

  # Working-copy changes: status refreshes the index's file times first, so
  # a file only touched is not counted.
  coop_git(status ok -c core.quotepath=off status --porcelain=v1 --untracked-files=all --no-renames
           -- ${kTrees} "${kPcDir}")
  if(NOT ok)
    set(good FALSE)
  endif()
  string(REPLACE "\n" ";" lines "${status}")
  set(changed)
  foreach(l ${lines})
    if(l MATCHES "^...(.+)$")
      set(p "${CMAKE_MATCH_1}")
      if(p MATCHES "^\"")
        set(good FALSE) # a name git had to quote: not one of ours
      endif()
      coop_covered("${p}" hit)
      if(hit)
        list(APPEND changed "${p}")
      endif()
    endif()
  endforeach()
  if(changed)
    list(SORT changed)
    list(REMOVE_DUPLICATES changed)
  endif()
  foreach(p ${changed})
    if(EXISTS "${SRC}/${p}" AND NOT IS_DIRECTORY "${SRC}/${p}")
      coop_git(id ok hash-object -- "${p}")
      string(STRIP "${id}" id)
      if(NOT ok)
        set(good FALSE)
      endif()
    else()
      set(id "deleted")
    endif()
    string(APPEND text "changed ${p} ${id}\n")
  endforeach()
endif()

if(good)
  string(SHA256 coop_id "${text}")
else()
  set(coop_id "unknown")
  message(WARNING "coop_id.cmake: no git checkout at ${SRC}: the co-op version is \"unknown\" and plays only "
                  "with other \"unknown\" builds")
endif()

# The version code: iNet::build_id's text, so this matches the Co-op tab.
file(STRINGS "${SRC}/${kPcDir}/iNetProto.h" proto REGEX "kProtoVersion = [0-9]+")
string(REGEX REPLACE ".*kProtoVersion = ([0-9]+).*" "\\1" proto "${proto}")
string(SHA256 full "tssm-coop build/2 ${coop_id} proto ${proto}")
string(SUBSTRING "${full}" 0 8 code)

list(LENGTH changed nchanged)
if(nchanged GREATER 0)
  message(STATUS "co-op version ${code} (protocol ${proto}, id ${coop_id}, ${nchanged} changed files)")
else()
  message(STATUS "co-op version ${code} (protocol ${proto}, id ${coop_id})")
endif()
if(VERBOSE)
  message(STATUS "co-op id text:\n${text}")
endif()

if(OUT)
  set(header "// Generated by tools/coop_id.cmake at build time. Do not edit.\n"
             "#define TSSM_COOP_ID \"${coop_id}\"\n"
             "#define TSSM_COOP_CODE \"${code}\"\n"
             "#define TSSM_COOP_PROTO ${proto}\n")
  string(CONCAT header ${header})
  set(old "")
  if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
  endif()
  if(NOT old STREQUAL header)
    file(WRITE "${OUT}" "${header}")
  endif()
endif()
