# CMake project for the native code (hook / injector / decoder).
#
# Its purpose is IDE indexing: it lets Android Studio (via a Gradle :native
# module's externalNativeBuild) resolve the NDK headers and autocomplete the C.
# The authoritative build is still the top-level Makefile; this just mirrors the
# source layout so the IDE understands it. Files that don't exist yet are simply
# not globbed — re-sync Gradle after adding a new .c file.

cmake_minimum_required(VERSION 3.22)
project(bindersniffer_native C)

set(CMAKE_C_STANDARD 11)

file(GLOB HOOK_SRC    ${CMAKE_SOURCE_DIR}/hook/*.c)
file(GLOB INJ_SRC     ${CMAKE_SOURCE_DIR}/injector/*.c)
file(GLOB DECODER_SRC ${CMAKE_SOURCE_DIR}/decoder/src/*.c)

include_directories(${CMAKE_SOURCE_DIR}/decoder/include)

# Hook library: decoder core + the in-app hook (matches `make hook`).
if(HOOK_SRC OR DECODER_SRC)
    add_library(bshook SHARED ${HOOK_SRC} ${DECODER_SRC})
    target_link_libraries(bshook log)
endif()

# Standalone injector executable (matches `make injector`).
if(INJ_SRC)
    add_executable(injector ${INJ_SRC})
endif()
