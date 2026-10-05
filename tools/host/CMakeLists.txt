cmake_minimum_required(VERSION 3.12)
project(NJEMUHostTools C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(njemu_host_tool_lib STATIC
    c_source.c
    crc32.c
    endian.c
    file.c
    text.c
    tsv.c
    utf8.c
    command_compare_frames.c
    command_font.c
    command_validate_cps2_cache.c
)
target_include_directories(njemu_host_tool_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(njemu-tool main.c)
target_link_libraries(njemu-tool PRIVATE njemu_host_tool_lib)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(njemu_host_tool_lib PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(njemu-tool PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

install(TARGETS njemu-tool RUNTIME DESTINATION bin)
