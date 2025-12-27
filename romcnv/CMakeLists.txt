cmake_minimum_required(VERSION 3.12)
project(ROMCNV)

# Configuration

# Different target availables
set(TARGET_VALUES 
    "CPS2"  # 0
    "MVS"   # 1
)

# Ensure TARGET is set or provide a default
if(NOT DEFINED TARGET)
    message(FATAL_ERROR "TARGET is not defined. Please specify a TARGET (e.g., -DTARGET=CPS2).")
endif()

list(FIND TARGET_VALUES ${TARGET} TARGET_INDEX)
if (TARGET_INDEX EQUAL -1)
    message(FATAL_ERROR "Invalid TARGET. Please choose from the allowed values: ${TARGET_VALUES}")
endif()

# Add target to compiler definitions
add_definitions(-DBUILD_${TARGET})

# Different options availables
option(USE_CHINESE "Use CHINESE" OFF)
option(USE_ASAN "Use ASAN" OFF)

# Add options to compiler definitions
if (USE_CHINESE)
    add_definitions(-DCHINESE)
endif()

# Emscripten/WASM build configuration
if(EMSCRIPTEN)
    message(STATUS "Building for WebAssembly (Emscripten)")
    add_definitions(-DWASM_BUILD)
    
    set(CMAKE_EXECUTABLE_SUFFIX ".js")
    
    # USE_ZLIB needed at compile time for headers
    add_compile_options("-sUSE_ZLIB=1")
endif()

# Version
set(VERSION_MAJOR 2)
set(VERSION_MINOR 4)
set(VERSION_BUILD 0)
add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_BUILD=${VERSION_BUILD})

# Define the version string
if (VERSION_BUILD EQUAL 0)
    set(VERSION_STR "${VERSION_MAJOR}.${VERSION_MINOR}")
else()
    set(VERSION_STR "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUILD}")
endif()
add_definitions(-DVERSION_STR="${VERSION_STR}")

# Define TARGET_STR in the code
add_definitions(-DTARGET_STR="${TARGET}")

# Set compiler flags

# Common Warning options
set(WARNING_OPTIONS
    -Werror
    -Wno-format
    -Wno-unused-value
)

# Disable some warnigns based in platform
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND WARNING_OPTIONS
        -Wno-format-overflow
    )
endif()

# Include directories
include_directories(
    src
    src/zip
)

# Common source files
set(COMMON_SRC
    common.c
    zfile.c
    unzip.c
    zip.c
)

# Add specific target source files
if (${TARGET} STREQUAL "MVS")
    include_directories(
        src/mvs
    )

    set(TARGET_SRC ${TARGET_SRC}
        mvs/romcnv.c
        mvs/neocrypt.c
    )
endif()

if (${TARGET} STREQUAL "CPS2")
    include_directories(
        src/cps2
    )

    set(TARGET_SRC ${TARGET_SRC}
        cps2/romcnv.c
    )
endif()

set(ALL_SRC ${COMMON_SRC} ${OS_SRC} ${TARGET_SRC})

list(TRANSFORM ALL_SRC PREPEND "src/")
# Create the executable
string(TOLOWER ${TARGET} TARGET_LOWER)
string(TOLOWER ${PROJECT_NAME} PROJECT_LOWER)
set(BINARY_TARGET "${PROJECT_LOWER}_${TARGET_LOWER}")
add_executable(${BINARY_TARGET}
    ${ALL_SRC}
)

# Add the warning flags
if (USE_ASAN)
    set(ASAN_OPTIONS
        -fsanitize=address
        -fsanitize=undefined
    )
endif()

# Copy rominfo file from resources folder to the build directory (not needed for Emscripten - uses preload)
set(ROMINFO_SOURCE "${CMAKE_SOURCE_DIR}/../resources/${TARGET_LOWER}/rominfo.${TARGET_LOWER}")
if(NOT EMSCRIPTEN)
    add_custom_command(
        TARGET ${BINARY_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${ROMINFO_SOURCE}
            ${CMAKE_BINARY_DIR}/rominfo.${TARGET_LOWER}
    )
endif()

target_compile_options(${BINARY_TARGET} PUBLIC ${WARNING_OPTIONS} ${ASAN_OPTIONS})

# Platform-specific link options
if(EMSCRIPTEN)
    # Emscripten link flags
    target_link_options(${BINARY_TARGET} PUBLIC
        "SHELL:-s WASM=1"
        "SHELL:-s EXPORTED_FUNCTIONS=['_main','_malloc','_free']"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','callMain']"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=134217728"
        "SHELL:-s FORCE_FILESYSTEM=1"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME='create_${BINARY_TARGET}'"
        "SHELL:-s ENVIRONMENT='web'"
        "SHELL:-s NO_EXIT_RUNTIME=1"
        "SHELL:-s INVOKE_RUN=0"
        "SHELL:-s USE_ZLIB=1"
        "SHELL:--preload-file ${ROMINFO_SOURCE}@/rominfo.${TARGET_LOWER}"
    )
else()
    target_link_options(${BINARY_TARGET} PUBLIC ${ASAN_OPTIONS})
endif()

# Add common libraries (zlib handled by Emscripten port for WASM builds)
if(NOT EMSCRIPTEN)
    target_link_libraries(${BINARY_TARGET} PUBLIC
        z
    )
endif()