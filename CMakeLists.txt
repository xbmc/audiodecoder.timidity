project(audiodecoder.timidity)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(kodi REQUIRED)
find_package(kodiplatform REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}
                    ${PROJECT_SOURCE_DIR}/lib/timidity)

add_subdirectory(lib/timidity)

set(TIMIDITY_SOURCES src/TimidityCodec.cpp)

set(DEPLIBS ${kodiplatform_LIBRARIES} timidity)

build_addon(audiodecoder.timidity TIMIDITY DEPLIBS)

include(CPack)
