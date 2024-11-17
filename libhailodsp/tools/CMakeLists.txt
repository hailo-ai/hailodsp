find_package(CLI11 REQUIRED)

include_directories(${PROJECT_SOURCE_DIR}/common)
link_libraries(CLI11::CLI11 spdlog::spdlog hailodsp hailodsp-internal)

add_executable(dsp-utilization utilization_tool.cpp)

install(TARGETS dsp-utilization DESTINATION bin)
