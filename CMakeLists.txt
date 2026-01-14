cmake_minimum_required(VERSION 3.12)
project(NJEMU)

# Configuration

# Different target availables
set(TARGET_VALUES 
    "CPS1"  # 0
    "CPS2"  # 1
    "MVS"   # 2
    "NCDZ"  # 3
)

# Ensure TARGET is set or provide a default
if(NOT DEFINED TARGET)
    message(FATAL_ERROR "TARGET is not defined. Please specify a TARGET (e.g., -DTARGET=CPS1).")
endif()

list(FIND TARGET_VALUES ${TARGET} TARGET_INDEX)
if (TARGET_INDEX EQUAL -1)
    message(FATAL_ERROR "Invalid TARGET. Please choose from the allowed values: ${TARGET_VALUES}")
endif()

# Add target to compiler definitions
add_definitions(-DBUILD_${TARGET})

# Ensure PLATFORM is set or provide a default
if(NOT DEFINED PLATFORM)
    message(FATAL_ERROR "PLATFORM is not defined. Please specify a PLATFORM (e.g., -DPLATFORM=PSP).")
endif()

# Different platform availables
set(PLATFORM_VALUES 
    "PSP"     # 0
    "PS2"     # 1 
    "DESKTOP" # 2
)
list(FIND PLATFORM_VALUES ${PLATFORM} PLATFORM_INDEX)
if (PLATFORM_INDEX EQUAL -1)
    message(FATAL_ERROR "Invalid PLATFORM. Please choose from the allowed values: ${PLATFORM_VALUES}")
endif()

# Get platform name lower case, used to include platform specific files
string(TOLOWER ${PLATFORM} PLATFORM_LOWER)

# Add platform to compiler definitions
add_definitions(-D${PLATFORM})

# Different options availables
option(LARGE_MEMORY "Large Memory" OFF)
option(KERNEL_MODE "Kernel Mode" OFF)
option(COMMAND_LIST "Command List" OFF)
option(ADHOC "Ad Hoc" OFF)
option(NO_GUI "No GUI" ON)
option(SAVE_STATE "Save State" OFF)
option(UI_32BPP "UI 32BPP" ON)
option(RELEASE "Release" OFF)
option(SYSTEM_BUTTONS "System Buttons" OFF)
option(USE_ASAN "Use ASAN" OFF)
option(USE_PG "Use Performance Graph" OFF)

# Add options to compiler definitions
if (NO_GUI)
    add_definitions(-DNO_GUI)
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
if (USE_PG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
endif()

# Enable interprocedural optimization LTO
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# Common Flags options
set(COMMON_FLAGS
    -O3 
    -ffast-math
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND COMMON_FLAGS
        -fsingle-precision-constant
        -funroll-loops
        -fipa-pta
    )
endif()

# Common Warning options
set(WARNING_OPTIONS
    -Werror
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-deprecated-declarations
)

# Disable some warnigns based in platform
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND WARNING_OPTIONS
        -Wno-format-overflow
    )
endif()

if (PLATFORM STREQUAL "PSP")
    list(APPEND WARNING_OPTIONS
        -Wno-unused-variable
    )
elseif (PLATFORM STREQUAL "PS2")
    list(APPEND WARNING_OPTIONS
        -Wno-unused-variable
        -Wno-unknown-pragmas
        -Wno-restrict
        -Wno-strict-aliasing
        -Wno-format
    )
elseif (PLATFORM STREQUAL "DESKTOP")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND WARNING_OPTIONS
            -Wno-unused-result
        )
    else()
        list(APPEND WARNING_OPTIONS
            -Wno-unused-command-line-argument
            -Wno-invalid-source-encoding
            -Wno-shorten-64-to-32
        )
    endif()
endif()

# Include directories
include_directories(
    src
    src/zip
    ${PLATFORM_LOWER}
)

# Common source files
set(COMMON_SRC
    emumain.h
    emumain.c
    zip/zfile.h
    zip/zfile.c
    zip/unzip.h
    zip/unzip.c
    sound/sndintrf.h
    sound/sndintrf.c
    common/cache.h
    common/cache.c
    common/filer.h
    common/filer.c
    common/loadrom.h
    common/loadrom.c
    common/thread_driver.h
    common/thread_driver.c
    common/audio_driver.h
    common/audio_driver.c
    common/power_driver.h
    common/power_driver.c
    common/ticker_driver.h
    common/ticker_driver.c
    common/input_driver.h
    common/input_driver.c
    common/video_driver.h
    common/video_driver.c
    common/ui_text_driver.h
    common/ui_text_driver.c
    common/platform_driver.h
    common/platform_driver.c
    common/sound.h
    common/sound.c
)

# Additional source files based on options
if (ADHOC)
    set(COMMON_SRC ${COMMON_SRC}
        common/adhoc.h
        common/adhoc.c
    )
endif()

if (COMMAND_LIST)
    set(COMMON_SRC ${COMMON_SRC}
        common/cmdlist.h
        common/cmdlist.c
    )
endif()

if (SAVE_STATE)
    set(COMMON_SRC ${COMMON_SRC}
        common/state.h
        common/state.c
    )
endif()

if (NO_GUI)
    set(OS_SRC ${OS_SRC}
        ${PLATFORM_LOWER}/${PLATFORM_LOWER}_no_gui.c
    )
else()
    # Add source files related to GUI if needed
endif()


set(OS_SRC ${OS_SRC}
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}.h
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_platform.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_ticker.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_power.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_ui_text.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_input.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_video.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_audio.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_thread.c
)

# CPU source files
include_directories(
    src/cpu/m68000
    src/cpu/z80
)

set(TARGET_SRC ${TARGET_SRC}
    cpu/m68000/m68000.h
    cpu/m68000/m68000.c
    cpu/m68000/c68k.h
    cpu/m68000/c68k.c
    cpu/m68000/c68kmacro.h
    cpu/z80/z80.h
    cpu/z80/z80.c
    cpu/z80/cz80.h
    cpu/z80/cz80.c
    cpu/z80/cz80macro.h
)

# Add specific target source files
if (${TARGET} STREQUAL "MVS")
    include_directories(
        src/mvs
    )

    set(TARGET_SRC ${TARGET_SRC}
        mvs/mvs.h
        mvs/mvs.c
        mvs/driver.h
        mvs/driver.c
        mvs/memintrf.h
        mvs/memintrf.c
        mvs/inptport.h
        mvs/inptport.c
        mvs/dipsw.h
        mvs/dipsw.c
        mvs/timer.h
        mvs/timer.c
        mvs/vidhrdw.h
        mvs/vidhrdw.c
        mvs/sprite_common.h
        mvs/sprite_common.c
        mvs/${PLATFORM_LOWER}_sprite.c
        mvs/pd4990a.h
        mvs/pd4990a.c
        mvs/neocrypt.c
        mvs/biosmenu.h
        mvs/biosmenu.c
    )

    # Set SOUND_2610 flag
    set(SOUND_2610 ON)
endif()

# Add specific target source files
if (${TARGET} STREQUAL "CPS1")

    include_directories(
        src/cps1
    )

    set(TARGET_SRC ${TARGET_SRC}
        common/coin.h
        common/coin.c
        cps1/cps1.h
        cps1/cps1.c
        cps1/driver.h
        cps1/driver.c
        cps1/memintrf.h
        cps1/memintrf.c
        cps1/inptport.h
        cps1/inptport.c
        cps1/dipsw.h
        cps1/dipsw.c
        cps1/timer.h
        cps1/timer.c
        cps1/vidhrdw.h
        cps1/vidhrdw.c
        cps1/sprite.h
        cps1/sprite_common.h
        cps1/sprite_common.c
        cps1/${PLATFORM_LOWER}_sprite.c
        cps1/eeprom.h
        cps1/eeprom.c
        cps1/kabuki.c
    )

    # Set SOUND_2151 flag for CPS1
    set(SOUND_2151 ON)
    # Set SOUND_QSOUND flag for CPS1
    set(SOUND_QSOUND ON)
endif()

# Add specific target source files
if (${TARGET} STREQUAL "NCDZ")
    include_directories(
        src/ncdz
    )

    set(TARGET_SRC ${TARGET_SRC}
        ncdz/cdda.c
        ncdz/cdda.h
        ncdz/cdrom.c
        ncdz/cdrom.h
        ncdz/driver.c
        ncdz/driver.h
        ncdz/inptport.c
        ncdz/inptport.h
        ncdz/memintrf.c
        ncdz/memintrf.h
        ncdz/ncdz.c
        ncdz/ncdz.h
        ncdz/sprite_common.h
        ncdz/sprite_common.c
        ncdz/${PLATFORM_LOWER}_sprite.c
        ncdz/sprite.h
        ncdz/startup.c
        ncdz/timer.c
        ncdz/timer.h
        ncdz/vidhrdw.c
        ncdz/vidhrdw.h
    )

    # Set SOUND_2610 flag
    set(SOUND_2610 ON)
    # Set MP3 flag
    set(SOUND_MP3 ON)
    # Set LIB_MAD flag
    set(LIB_MAD ON)
endif()

# Add the mp3 sound files if needed
if (SOUND_MP3)
    set(COMMON_SRC ${COMMON_SRC}
        common/mp3.h
        common/mp3.c
    )
endif()

# Add the 2151 sound files if needed
if (SOUND_2151)
    set(TARGET_SRC ${TARGET_SRC}
        sound/2151intf.h
        sound/2151intf.c
        sound/ym2151.h
        sound/ym2151.c
    )
endif()

# Add the 2610 sound files if needed use SOUND_2610 flag
if (SOUND_2610)
    set(TARGET_SRC ${TARGET_SRC}
        sound/2610intf.h
        sound/2610intf.c
        sound/ym2610.h
        sound/ym2610.c
    )
endif()

# Add the qsound sound files if needed use SOUND_QSOUND flag
if (SOUND_QSOUND)
    set(TARGET_SRC ${TARGET_SRC}
        sound/qsound.h
        sound/qsound.c
    )
endif()

set(ALL_SRC ${COMMON_SRC} ${OS_SRC} ${TARGET_SRC})

list(TRANSFORM ALL_SRC PREPEND "src/")
# # Create the executable
add_executable(${TARGET}
    ${ALL_SRC}
)

# Add the warning flags
if (USE_ASAN)
    set(ASAN_OPTIONS
        -fsanitize=address
        -fsanitize=undefined
    )
endif()


# Define the source directory (resources folder)
string(TOLOWER ${TARGET} TARGET_LOWER)
set(RESOURCE_DIR "${CMAKE_SOURCE_DIR}/resources/${TARGET_LOWER}/")

# Set the destination directory to the build directory (CMAKE_BINARY_DIR)
set(DESTINATION_DIR "${CMAKE_BINARY_DIR}")

# Copy the resources during the configure stage
file(COPY ${RESOURCE_DIR} DESTINATION ${DESTINATION_DIR})

message(STATUS "Resources copied from ${RESOURCE_DIR} to: ${DESTINATION_DIR}")

target_compile_options(${TARGET} PUBLIC ${COMMON_FLAGS} ${WARNING_OPTIONS} ${ASAN_OPTIONS})
target_link_options(${TARGET} PUBLIC ${ASAN_OPTIONS})

# Add platform specific libraries
if (PLATFORM STREQUAL "DESKTOP")
    find_package(SDL2 REQUIRED)
    target_include_directories(${TARGET} PUBLIC 
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(${TARGET} PUBLIC
        SDL2::SDL2-static
    )
elseif(PLATFORM STREQUAL "PS2")
    target_link_libraries(${TARGET} PUBLIC
        patches
        gskit
        dmakit
        ps2_drivers
    )
elseif(PLATFORM STREQUAL "PSP")
    target_link_libraries(${TARGET} PUBLIC
        pspdisplay
        pspge 
        pspctrl
        pspaudio
        pspgu
        psppower
    )
endif()

# Add libmad library if needed
if (LIB_MAD)
    # Find the libmad library
    find_path(LIBMAD_INCLUDE_DIR mad.h)
    find_library(LIBMAD_LIBRARY mad)

    # Get the directory of the library for -L
    get_filename_component(LIBMAD_LIBRARY_DIR ${LIBMAD_LIBRARY} DIRECTORY)
    
    target_include_directories(${TARGET} PUBLIC 
        ${LIBMAD_INCLUDE_DIR}
    )
    target_link_directories(${TARGET} PUBLIC
        ${LIBMAD_LIBRARY_DIR}
    )
    target_link_libraries(${TARGET} PUBLIC
        mad
    )
endif()

# Add common libraries
target_link_libraries(${TARGET} PUBLIC
    z
)
       
# Create the PBP file for PSP
if(PLATFORM STREQUAL "PSP")
    set(BUILD_PRX OFF)
    create_pbp_file(
        TARGET ${TARGET}
        ICON_PATH NULL
        BACKGROUND_PATH NULL
        PREVIEW_PATH NULL
        TITLE "${TARGET}"
    )
endif()

# ==============================================================================
# Install rules
# ==============================================================================

# Set default install prefix if not specified
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path prefix" FORCE)
endif()

# Install the executable
if(PLATFORM STREQUAL "PSP")
    install(FILES ${CMAKE_BINARY_DIR}/EBOOT.PBP DESTINATION .)
elseif(PLATFORM STREQUAL "PS2")
    install(PROGRAMS ${CMAKE_BINARY_DIR}/${TARGET} DESTINATION . RENAME ${TARGET}.elf)
else()
    install(TARGETS ${TARGET} DESTINATION .)
endif()

# Install resource files (copy entire directory structure, including README)
install(DIRECTORY ${RESOURCE_DIR}
    DESTINATION .
)