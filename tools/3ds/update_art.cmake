# Writes the 3DS updater's romfs:/update/art.txt (CMakeLists.txt, TSSM_UPDATE):
# the SHA-256 of what this build packs as the .3dsx's SMDH and the .cia's icon
# and banner, one "<what> <hex>" line each. src/SB/Core/pc/3ds/i3DSUpdate.cpp
# compares the running copy's own with these before it replaces anything.
#
#   cmake -DOUT=<art.txt> -DSMDH=<tssm.smdh> [-DICON=<tssm.icn> -DBANNER=<.bnr>] -P update_art.cmake

if(NOT OUT OR NOT SMDH)
  message(FATAL_ERROR "update_art.cmake: OUT and SMDH are needed")
endif()
file(SHA256 "${SMDH}" _smdh)
set(_text "smdh ${_smdh}\n")
if(ICON)
  file(SHA256 "${ICON}" _icon)
  string(APPEND _text "icon ${_icon}\n")
endif()
if(BANNER)
  file(SHA256 "${BANNER}" _banner)
  string(APPEND _text "banner ${_banner}\n")
endif()
file(WRITE "${OUT}" "${_text}")
