#
# Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

add_library(
  hailodsp SHARED
  src/device.cpp
  src/resize.cpp
  src/send_command.cpp
  src/image_utils.cpp
  src/buffer.cpp
  src/blend.cpp
  src/blur.cpp
  src/convert_format.cpp
  src/dewarp.cpp
  src/logger.cpp
  src/hailodsp_driver.cpp
  src/utilization.cpp)

set_target_properties(hailodsp PROPERTIES VERSION 1.2.2)

# cmake-lint: disable=E1120
# https://github.com/cheshirekow/cmake_format/issues/218
separate_arguments(LIB_EXTRA_DEFS)
target_compile_definitions(hailodsp PRIVATE ${LIB_EXTRA_DEFS})
target_compile_options(hailodsp
                       PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)

target_link_libraries(hailodsp PRIVATE spdlog::spdlog)

target_include_directories(hailodsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(hailodsp PRIVATE ${PROJECT_SOURCE_DIR}/common)

install(TARGETS hailodsp DESTINATION lib)
install(DIRECTORY include/hailo/ DESTINATION include/hailo/)

add_library(hailodsp-internal INTERFACE)
target_include_directories(hailodsp-internal
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
