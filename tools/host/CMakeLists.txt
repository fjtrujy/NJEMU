cmake_minimum_required(VERSION 3.12)
project(NJEMUHostTools C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(njemu_host_tool_lib STATIC
    c_source.c
    crc32.c
    dip_metadata.c
    endian.c
    file.c
    game_database.c
    game_metadata.c
    json.c
    rominfo.c
    string_pool.c
    text.c
    translation_source.c
    translations.c
    tsv.c
    utf8.c
    command_compare_frames.c
    command_dip_metadata.c
    command_font.c
    command_game_database.c
    command_game_metadata.c
    command_romcnv_translations.c
    command_rominfo.c
    command_translations.c
    command_validate_cps2_cache.c
)
target_include_directories(njemu_host_tool_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(njemu-tool main.c)
target_link_libraries(njemu-tool PRIVATE njemu_host_tool_lib)

add_executable(njemu-host-tests host_tests.c)
target_link_libraries(njemu-host-tests PRIVATE njemu_host_tool_lib)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(njemu_host_tool_lib PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(njemu-tool PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(njemu-host-tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

install(TARGETS njemu-tool njemu-host-tests RUNTIME DESTINATION bin)
