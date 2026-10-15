cmake_minimum_required(VERSION 3.12)
project(NJEMUHostTools C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_library(njemu_host_tool_lib STATIC
    c_source.c
    crc32.c
    dip_metadata.c
    elf_footprint.c
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
    command_release_notes.c
    command_stats_snapshot.c
    command_dip_metadata.c
    command_elf_footprint.c
    command_font.c
    command_game_database.c
    command_game_metadata.c
    command_romcnv_translations.c
    command_rominfo.c
    command_translations.c
    command_validate_cps2_cache.c
)
target_include_directories(njemu_host_tool_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Artwork authoring is optional; console builds and other host commands never
# require libpng, and all generated binary images are checked into the repo.
find_package(PNG QUIET)
if(PNG_FOUND)
    target_sources(njemu_host_tool_lib PRIVATE command_artwork.c)
    target_link_libraries(njemu_host_tool_lib PUBLIC PNG::PNG)
endif()

add_executable(njemu-tool main.c)
target_link_libraries(njemu-tool PRIVATE njemu_host_tool_lib)
if(PNG_FOUND)
    target_compile_definitions(njemu-tool PRIVATE NJEMU_HAS_PNG_ARTWORK=1)
    add_test(NAME artwork_generate
        COMMAND njemu-tool artwork "${CMAKE_CURRENT_SOURCE_DIR}/../../data/artwork"
                "${CMAKE_CURRENT_BINARY_DIR}/generated-artwork")
    add_test(NAME artwork_output_valid
        COMMAND ${CMAKE_COMMAND}
            -DNJEMU_ARTWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/generated-artwork
            -DNJEMU_ARTWORK_GENERATED_ONLY=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/NJEMUArtworkValidation.cmake")
    set_tests_properties(artwork_output_valid PROPERTIES DEPENDS artwork_generate)
endif()

add_test(NAME release_notes_format
    COMMAND ${CMAKE_COMMAND}
        -DNJEMU_TOOL=$<TARGET_FILE:njemu-tool>
        -DNJEMU_TEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/release-notes-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/NJEMUReleaseNotesTests.cmake)

add_executable(njemu-host-tests host_tests.c)
target_link_libraries(njemu-host-tests PRIVATE njemu_host_tool_lib)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(njemu_host_tool_lib PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(njemu-tool PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(njemu-host-tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

install(TARGETS njemu-tool njemu-host-tests RUNTIME DESTINATION bin)
