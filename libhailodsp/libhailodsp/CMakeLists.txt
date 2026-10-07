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

# Which driver backend the library talks to. "board" drives the XRP kernel
# driver through /dev/dsp0; "iss" drives firmware in the Xtensa simulator. The
# seam is the driver_* functions, so only this one source file differs.
set(DSP_TARGET
    "board"
    CACHE STRING "Driver backend: board or iss")
set_property(CACHE DSP_TARGET PROPERTY STRINGS board iss)

if(DSP_TARGET STREQUAL "iss")
  set(DSP_DRIVER_SOURCE src/hailodsp_driver_iss.cpp)
else()
  set(DSP_DRIVER_SOURCE src/hailodsp_driver.cpp)
endif()

add_library(
  hailodsp SHARED
  src/device.cpp
  src/send_command.cpp
  src/image_utils.cpp
  src/buffer.cpp
  src/logger.cpp
  ${DSP_DRIVER_SOURCE}
  src/utilization.cpp
  src/dsp_tracer.cpp
  src/resize.cpp
  src/affine_transform.cpp
  src/blend.cpp
  src/blur.cpp
  src/convert_format.cpp
  src/dewarp.cpp
  src/crop.cpp
  src/gaussian.cpp
  src/laplacian.cpp
  src/priority_queue.cpp
  src/privacy_mask.cpp
  src/dsp_direct.cpp)

set_target_properties(hailodsp PROPERTIES VERSION 1.13.0)

# cmake-lint: disable=E1120
# https://github.com/cheshirekow/cmake_format/issues/218
separate_arguments(LIB_EXTRA_DEFS)
target_compile_definitions(hailodsp PRIVATE ${LIB_EXTRA_DEFS})

if(DSP_TARGET STREQUAL "iss")
  # Baked-in defaults so a developer can run without setting anything; both are
  # overridden by HAILODSP_ISS_FW and HAILODSP_ISS_XTRUN at run time.
  #
  # Which SoC's firmware to default to cannot be deduced here: the firmware is
  # built by a separate invocation with its own --target, so this only names the
  # commonest one. Point DSP_ISS_FW_TARGET, or HAILODSP_ISS_FW, at the other.
  set(DSP_ISS_FW_TARGET
      "H15H"
      CACHE STRING "SoC whose dsp-fw-sim.elf the baked-in default names")
  set_property(CACHE DSP_ISS_FW_TARGET PROPERTY STRINGS H15H H15L)
  if(NOT DEFINED DSP_ISS_FIRMWARE)
    set(_iss_fw_dir
        "${PROJECT_SOURCE_DIR}/../bin/Firmware/${DSP_ISS_FW_TARGET}")
    set(DSP_ISS_FIRMWARE "${_iss_fw_dir}/Release/dsp-fw-sim.elf")
  endif()
  if(NOT DEFINED DSP_ISS_XTRUN)
    set(_xtensa_tools_dir "${PROJECT_SOURCE_DIR}/../firmware/.toolchain/tools")
    set(DSP_ISS_XTRUN
        "${_xtensa_tools_dir}/RI-2023.11-linux/XtensaTools/bin/xt-run")
  endif()
  target_compile_definitions(
    hailodsp
    PRIVATE DSP_BACKEND_ISS DSP_ISS_DEFAULT_FIRMWARE="${DSP_ISS_FIRMWARE}"
            DSP_ISS_DEFAULT_XTRUN="${DSP_ISS_XTRUN}")
endif()
target_compile_options(hailodsp
                       PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)

target_link_libraries(hailodsp PRIVATE spdlog::spdlog)
link_build_info_to_target(hailodsp)

target_include_directories(hailodsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(hailodsp PRIVATE ${PROJECT_SOURCE_DIR}/common)

install(TARGETS hailodsp DESTINATION lib)
install(DIRECTORY include/hailo/ DESTINATION include/hailo/)

add_library(hailodsp-internal INTERFACE)
target_include_directories(hailodsp-internal
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(DSP_TARGET STREQUAL "iss")
  # Header-only pieces such as ThreadLocalBuffer are instantiated by anything
  # that includes them, and one definition wins for the whole process. Everyone
  # who sees these headers has to agree on which backend is in use.
  target_compile_definitions(hailodsp-internal INTERFACE DSP_BACKEND_ISS)
endif()
