cmake_minimum_required(VERSION 3.22)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(DSP-SW LANGUAGES C CXX)

find_package(spdlog REQUIRED)

option(DSP_BUILD_TESTS "Build unit tests" OFF)
option(DSP_BUILD_DOC "Build documentation" OFF)

add_compile_options(
  -Wall -Werror -Wextra $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-field-initializers>)

add_subdirectory(libhailodsp)
add_subdirectory(cli)
add_subdirectory(xrp)
if(DSP_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(DSP_BUILD_DOC)
  add_subdirectory(doc)
endif()
