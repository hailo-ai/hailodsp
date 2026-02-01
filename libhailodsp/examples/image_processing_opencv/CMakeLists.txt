cmake_minimum_required(VERSION 3.16)
project(dsp_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(dsp-example-opencv main.cpp)

target_compile_options(
  dsp-example-opencv
  PRIVATE -Wall
          -Wextra
          -fconcepts
          -Werror
          -Wno-ignored-qualifiers
          -Wno-stringop-truncation
          -Wno-reorder)

# Link libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(OpenCV REQUIRED opencv4)
target_link_libraries(dsp-example-opencv PRIVATE hailodsp ${OpenCV_LIBRARIES})
target_include_directories(dsp-example-opencv PRIVATE ${OpenCV_INCLUDE_DIRS})

install(TARGETS dsp-example-opencv DESTINATION bin)
