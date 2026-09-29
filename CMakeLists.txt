cmake_minimum_required(VERSION 3.21)
project(NextGenDiskCache VERSION 2.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(NextGenDiskCache SHARED
  src/main.cpp
  src/DirectStorageBackend.cpp
  src/IatHook.cpp
)

include(CTest)
if(BUILD_TESTING)
  add_executable(CachePolicyChecks tests/cache_policy.cpp src/DirectStorageBackend.cpp src/IatHook.cpp)
  target_include_directories(CachePolicyChecks PRIVATE deps/include deps/directstorage/native/include)
  target_link_directories(CachePolicyChecks PRIVATE deps/libs/detours)
  target_link_libraries(CachePolicyChecks PRIVATE detours.lib setupapi.lib cfgmgr32.lib)
  target_compile_definitions(CachePolicyChecks PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
  add_test(NAME cache_policy COMMAND CachePolicyChecks)

  add_library(CacheOtherModule SHARED tests/other_module.cpp)
  add_executable(CacheHost tests/cache_host.cpp src/IatHook.cpp)
  target_include_directories(CacheHost PRIVATE src deps/include)
  target_compile_definitions(CacheHost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  foreach(mode observed mapped strided pressure memfail minimal directory late pacing threads inflight wide)
    add_test(NAME host_${mode} COMMAND CacheHost
      "${CMAKE_SOURCE_DIR}/package/SKSE/Plugins/NextGenDiskCache.dll"
      "$<TARGET_FILE:CacheOtherModule>" ${mode})
  endforeach()
endif()

target_include_directories(NextGenDiskCache PRIVATE
  ${CMAKE_SOURCE_DIR}/deps/include
  ${CMAKE_SOURCE_DIR}/deps/directstorage/native/include
)

target_link_directories(NextGenDiskCache PRIVATE
  ${CMAKE_SOURCE_DIR}/deps/libs/detours
)

# setupapi + cfgmgr32: GPU BAR enumeration for Resizable BAR detection
target_link_libraries(NextGenDiskCache PRIVATE detours.lib setupapi.lib cfgmgr32.lib)

# Static CRT to avoid redistributable dependency on user machines
set_property(TARGET NextGenDiskCache PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_definitions(NextGenDiskCache PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
  target_compile_options(NextGenDiskCache PRIVATE /W4 /EHsc /Zi /Zc:__cplusplus)
  target_link_options(NextGenDiskCache PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
  # Keep release symbols outside the Nexus payload. CI archives them separately.
  set_target_properties(NextGenDiskCache PROPERTIES COMPILE_PDB_NAME NextGenDiskCache)
endif()

set(OUT_DIR "${CMAKE_SOURCE_DIR}/package/SKSE/Plugins")
set_target_properties(NextGenDiskCache PROPERTIES
  OUTPUT_NAME "NextGenDiskCache"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUT_DIR}"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}"
  PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/symbols"
  COMPILE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/symbols"
)
