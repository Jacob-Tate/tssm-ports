# The co-op .nro's NACP (CMakeLists.txt): the name, and a version ending in
# the co-op version code from coop_id.h, so the Homebrew Menu shows the code
# the Co-op tab shows on every device.
#
#   cmake -DNACPTOOL=<nacptool> -DCOOP_H=<coop_id.h> -DNAME=<name> -DAUTHOR=<author>
#         -DVERSION=<version> -DOUT=<file.nacp> -P tools/switch/coop_nacp.cmake

cmake_minimum_required(VERSION 3.16)

set(code "unknown")
if(EXISTS "${COOP_H}")
  file(STRINGS "${COOP_H}" line REGEX "^#define TSSM_COOP_CODE \"[0-9a-z]+\"")
  if(line MATCHES "\"([0-9a-z]+)\"")
    set(code "${CMAKE_MATCH_1}")
  endif()
endif()

# The NACP's version field holds 15 characters.
set(version "${VERSION}-${code}")
string(LENGTH "${version}" n)
if(n GREATER 15)
  message(FATAL_ERROR "coop_nacp.cmake: version \"${version}\" is longer than 15 characters")
endif()

execute_process(COMMAND "${NACPTOOL}" --create "${NAME}" "${AUTHOR}" "${version}" "${OUT}"
                RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "coop_nacp.cmake: nacptool failed (${r})")
endif()
message(STATUS "NACP: ${NAME}, version ${version}")
