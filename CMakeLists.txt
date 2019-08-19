cmake_minimum_required(VERSION 3.5)
project(audiodecoder.timidity)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(p8-platform REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways (becomes done in future)
                    ${p8-platform_INCLUDE_DIRS}
                    ${PROJECT_SOURCE_DIR}/lib/timidity)

add_subdirectory(lib/timidity)

set(TIMIDITY_SOURCES src/TimidityCodec.cpp)

set(DEPLIBS ${p8-platform_LIBRARIES})

add_definitions(-DLIBRARY_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}"
                -DLIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")

set(TIMIDITY_ADDITIONAL_BINARY $<TARGET_FILE:timidity>)

if(WIN32)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
endif()

build_addon(audiodecoder.timidity TIMIDITY DEPLIBS)

include(CPack)
