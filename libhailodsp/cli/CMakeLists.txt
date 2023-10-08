add_library(cli-common OBJECT image_args.c)
target_link_libraries(cli-common PUBLIC hailodsp hailodsp-internal)
target_include_directories(cli-common PUBLIC ${PROJECT_SOURCE_DIR}/common)

link_libraries(cli-common)

add_executable(dsp-resize resize_cli.c)
add_executable(dsp-blend blend_cli.c)
add_executable(dsp-blur blur_cli.c)
add_executable(dsp-convert-format convert_format_cli.c)
add_executable(dsp-dewarp dewarp_cli.c bgr_yuv.c)
add_executable(dsp-multi-resize multi_resize_cli.c bgr_yuv.c)

target_link_libraries(dsp-dewarp PRIVATE m)

install(TARGETS dsp-resize DESTINATION bin)
install(TARGETS dsp-blend DESTINATION bin)
install(TARGETS dsp-blur DESTINATION bin)
install(TARGETS dsp-convert-format DESTINATION bin)
install(TARGETS dsp-dewarp DESTINATION bin)
install(TARGETS dsp-multi-resize DESTINATION bin)
