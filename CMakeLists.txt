cmake_minimum_required(VERSION 3.21)

# VERSION.txt is the only place the version is written. The DLL's SKSE metadata,
# its version resource, the packager and CI all read it from there.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" NGDC_VERSION LIMIT_COUNT 1)
string(STRIP "${NGDC_VERSION}" NGDC_VERSION)
if(NOT NGDC_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION.txt must contain major.minor.patch, found '${NGDC_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt")

project(NextGenDiskCache VERSION ${NGDC_VERSION} LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NGDC_VERSION_DEFINES
  NGDC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  NGDC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  NGDC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
  NGDC_VERSION_STRING="${PROJECT_VERSION}"
)
configure_file(src/version.rc.in "${CMAKE_BINARY_DIR}/generated/version.rc" @ONLY)

add_library(NextGenDiskCache SHARED
  src/main.cpp
  src/HotSet.cpp
  src/IatHook.cpp
  "${CMAKE_BINARY_DIR}/generated/version.rc"
)

include(CTest)
if(BUILD_TESTING)
  add_executable(CachePolicyChecks tests/cache_policy.cpp src/HotSet.cpp src/IatHook.cpp)
  target_include_directories(CachePolicyChecks PRIVATE deps/include)
  target_link_directories(CachePolicyChecks PRIVATE deps/libs/detours)
  target_link_libraries(CachePolicyChecks PRIVATE detours.lib setupapi.lib cfgmgr32.lib)
  target_compile_definitions(CachePolicyChecks PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    ${NGDC_VERSION_DEFINES})
  add_test(NAME cache_policy COMMAND CachePolicyChecks)

  add_library(CacheOtherModule SHARED tests/other_module.cpp)
  add_executable(CacheHost tests/cache_host.cpp src/IatHook.cpp)
  target_include_directories(CacheHost PRIVATE src deps/include)
  target_compile_definitions(CacheHost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  foreach(mode observed mapped strided pressure memfail minimal directory late pacing threads inflight wide hotset hotsetmenu hotsetpressure)
    add_test(NAME host_${mode} COMMAND CacheHost
      "${CMAKE_SOURCE_DIR}/package/SKSE/Plugins/NextGenDiskCache.dll"
      "$<TARGET_FILE:CacheOtherModule>" ${mode})
  endforeach()
endif()

target_include_directories(NextGenDiskCache PRIVATE
  ${CMAKE_SOURCE_DIR}/deps/include
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
  ${NGDC_VERSION_DEFINES}
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
