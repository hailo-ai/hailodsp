# Generate Git commit hash
execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Format the build info string with build type
string(TOLOWER "${CMAKE_BUILD_TYPE}" LOWERCASE_BUILD_TYPE)
set(LIB_BUILD_INFO "${GIT_COMMIT_HASH}-${LOWERCASE_BUILD_TYPE}")

# Define new static library that contains build info
add_library(lib_build_info STATIC ${PROJECT_SOURCE_DIR}/common/lib_build_info.c)

# Set the build info as a compile definition
target_compile_definitions(lib_build_info
                           PRIVATE LIB_BUILD_INFO="${LIB_BUILD_INFO}")

# This function links the lib_build_info library to the specified target. The
# library is linked using --whole-archive and --no-whole-archive linker flags to
# ensure that all symbols from the build info library are included in the final
# binary, even if they are not directly referenced. This is necessary because
# build info symbols are typically not referenced directly in code.
function(link_build_info_to_target target_name)
  target_link_libraries(
    ${target_name} PRIVATE "-Wl,--whole-archive" lib_build_info
                           "-Wl,--no-whole-archive")
  target_link_libraries(${target_name} PUBLIC lib_build_info)
endfunction()
