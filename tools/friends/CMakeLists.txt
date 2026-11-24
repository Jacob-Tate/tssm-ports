# Standalone project: the single-file "friend build" launcher (see launcher.cpp, tools/package_friends.py).
#   cmake -S tools/friends -B build/friends-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/friends-launcher
cmake_minimum_required(VERSION 3.20)
project(tssm-friend-launcher LANGUAGES CXX)

if (NOT MSVC)
    message(FATAL_ERROR "The friend launcher is Windows/MSVC only.")
endif ()

add_executable(tssm-friend-launcher WIN32 launcher.cpp)
target_compile_features(tssm-friend-launcher PRIVATE cxx_std_17)
target_compile_definitions(tssm-friend-launcher PRIVATE UNICODE _UNICODE)
target_link_libraries(tssm-friend-launcher PRIVATE Cabinet Shell32 Ole32)
set_target_properties(tssm-friend-launcher PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_link_options(tssm-friend-launcher PRIVATE /MANIFEST:NO)
