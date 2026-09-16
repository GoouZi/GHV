cmake_minimum_required(VERSION 3.12)
project(ghv_native LANGUAGES CXX)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_version_header.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    RESULT_VARIABLE GHV_VERSION_RESULT
  )
  if(NOT GHV_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate native/ghv_version.h")
  endif()
endif()
find_package(OpenMP QUIET)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libghv ${CMAKE_CURRENT_BINARY_DIR}/libghv)
add_executable(ghvcore ghvcore.cpp)
add_executable(ghvdecode ghvdecode.cpp)
target_compile_features(ghvcore PRIVATE cxx_std_17)
target_compile_features(ghvdecode PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(ghvcore PRIVATE /O2)
  target_compile_options(ghvdecode PRIVATE /O2)
else()
  target_compile_options(ghvcore PRIVATE -O3)
  target_compile_options(ghvdecode PRIVATE -O3)
endif()
if(OpenMP_CXX_FOUND)
  target_link_libraries(ghvcore PRIVATE OpenMP::OpenMP_CXX)
  target_link_libraries(ghvdecode PRIVATE OpenMP::OpenMP_CXX)
endif()
add_executable(ghvlibtest ../tests/native_libghv_smoke.cpp)
target_compile_features(ghvlibtest PRIVATE cxx_std_17)
target_link_libraries(ghvlibtest PRIVATE libghv)
if(WIN32)
  add_executable(GHVPlayer WIN32
    ../apps/ghv_player/win_main.cpp
    ../apps/ghv_player/player_core.cpp
    ../apps/ghv_player/d3d11_renderer.cpp
    ../apps/ghv_player/wasapi_audio.cpp
  )
  target_compile_features(GHVPlayer PRIVATE cxx_std_17)
  target_include_directories(GHVPlayer PRIVATE ../apps/ghv_player)
  target_link_libraries(GHVPlayer PRIVATE libghv d3d11 dxgi d3dcompiler ole32 uuid avrt comctl32 comdlg32 shell32 psapi)
  target_compile_definitions(GHVPlayer PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MSVC)
    target_compile_options(GHVPlayer PRIVATE /O2 /W4 /utf-8)
  endif()
  set_target_properties(GHVPlayer PROPERTIES OUTPUT_NAME "GHV Player")
  add_executable(ghvplayertest
    ../tests/native_player_core_smoke.cpp
    ../apps/ghv_player/player_core.cpp
    ../apps/ghv_player/wasapi_audio.cpp
  )
  target_compile_features(ghvplayertest PRIVATE cxx_std_17)
  target_include_directories(ghvplayertest PRIVATE ../apps/ghv_player)
  target_link_libraries(ghvplayertest PRIVATE libghv ole32 uuid avrt)
  if(MSVC)
    target_compile_options(ghvplayertest PRIVATE /O2 /W4 /utf-8)
  endif()
endif()
set(runtime_targets ghvcore ghvdecode ghvlibtest)
if(WIN32)
  list(APPEND runtime_targets GHVPlayer ghvplayertest)
endif()
set_target_properties(${runtime_targets} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set_target_properties(${runtime_targets} PROPERTIES
    "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()
