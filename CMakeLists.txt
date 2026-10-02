cmake_minimum_required(VERSION 3.12)
project(NJEMU)

include(CTest)

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
    "PSVITA"  # 3
)
list(FIND PLATFORM_VALUES ${PLATFORM} PLATFORM_INDEX)
if (PLATFORM_INDEX EQUAL -1)
    message(FATAL_ERROR "Invalid PLATFORM. Please choose from the allowed values: ${PLATFORM_VALUES}")
endif()

# Get platform name lower case, used to include platform specific files
string(TOLOWER ${PLATFORM} PLATFORM_LOWER)

# ps2dev.cmake injects SDK/gsKit headers into CMAKE_C_FLAGS with -I. Treat
# those third-party headers as system headers so -Werror remains strict for
# NJEMU without promoting dependency warnings to build failures.
if (PLATFORM STREQUAL "PS2")
    foreach(PS2_SYSTEM_INCLUDE
            "$ENV{PS2SDK}/ee/include"
            "$ENV{PS2SDK}/common/include"
            "$ENV{PS2DEV}/gsKit/include"
            "$ENV{PS2SDK}/ports/include")
        string(REPLACE "-I${PS2_SYSTEM_INCLUDE}" "-isystem ${PS2_SYSTEM_INCLUDE}"
               CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    endforeach()
endif()

# Add platform to compiler definitions
add_definitions(-D${PLATFORM})
add_definitions(-DPLATFORM_STR="${PLATFORM}")


# VitaSDK helpers (vita_create_self, vita_create_vpk). The toolchain file is
# passed on the command line: -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
if (PLATFORM STREQUAL "PSVITA")
    if (NOT DEFINED ENV{VITASDK})
        message(FATAL_ERROR "PLATFORM=PSVITA requires the VITASDK environment variable")
    endif()
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
endif()

# Different options availables
# Memory sizing is runtime-driven. PSP packaging explicitly requests the
# largest user-memory partition and the planner adapts to what the device
# actually exposes.
option(KERNEL_MODE "Kernel Mode" OFF)
option(COMMAND_LIST "Command List" OFF)
option(ADHOC "Ad Hoc" OFF)
option(GUI "GUI" OFF)
option(SAVE_STATE "Save State" OFF)
option(RELEASE "Release" OFF)
option(USE_ASAN "Use ASAN" OFF)
option(USE_UBSAN "Use UBSAN" OFF)
option(USE_PG "Use Performance Graph" OFF)
option(PSP_ME_AUDIO "PSP: enable experimental Media Engine audio producer" OFF)
option(PSP_AUDIO_PROFILE "PSP: log audio producer/chip/output timing" OFF)
if((USE_ASAN OR USE_UBSAN OR USE_PG) AND NOT PLATFORM STREQUAL "DESKTOP")
    message(FATAL_ERROR "USE_ASAN, USE_UBSAN, and USE_PG are desktop-only diagnostic capabilities")
endif()
if(PSP_ME_AUDIO AND NOT PLATFORM STREQUAL "PSP")
    message(FATAL_ERROR "PSP_ME_AUDIO requires PLATFORM=PSP")
endif()
if(PSP_AUDIO_PROFILE AND NOT PLATFORM STREQUAL "PSP")
    message(FATAL_ERROR "PSP_AUDIO_PROFILE requires PLATFORM=PSP")
endif()

# Streaming cache is a core capability. Keep the compatibility matrix here so
# C sources only need to ask whether this build has cache support.
if((PLATFORM STREQUAL "PSP" OR PLATFORM STREQUAL "PS2") AND
   ("${TARGET}" STREQUAL "MVS" OR "${TARGET}" STREQUAL "CPS2"))
    set(USE_CACHE_DEFAULT ON)
else()
    set(USE_CACHE_DEFAULT OFF)
endif()
option(USE_CACHE "Enable streaming cache support (MVS/CPS2 only)" ${USE_CACHE_DEFAULT})
if(USE_CACHE AND NOT ("${TARGET}" STREQUAL "MVS" OR "${TARGET}" STREQUAL "CPS2"))
    message(FATAL_ERROR "USE_CACHE is only supported for TARGET=MVS or TARGET=CPS2")
endif()
if(USE_CACHE)
    add_definitions(-DUSE_CACHE=1)
else()
    add_definitions(-DUSE_CACHE=0)
endif()
if(USE_CACHE AND PLATFORM STREQUAL "PS2" AND "${TARGET}" STREQUAL "MVS")
    add_definitions(-DCACHE_DEFAULT_READ_SIZE_KB=16)
else()
    add_definitions(-DCACHE_DEFAULT_READ_SIZE_KB=64)
endif()
if(USE_CACHE AND "${TARGET}" STREQUAL "MVS")
    add_definitions(-DMVS_PCM_CACHE=1)
else()
    add_definitions(-DMVS_PCM_CACHE=0)
endif()

option(CACHE_IO_PROFILE "Profile MVS cache I/O" OFF)
option(CACHE_IO_FORCE_SEEK "Force legacy seek-before-read MVS cache behavior" OFF)
option(CACHE_IO_VALIDATE_ACCELERATED "Validate accelerated MVS cache reads against POSIX" OFF)
if(CACHE_IO_PROFILE AND NOT (USE_CACHE AND "${TARGET}" STREQUAL "MVS"))
    message(FATAL_ERROR "CACHE_IO_PROFILE requires TARGET=MVS and USE_CACHE=ON")
endif()
if(CACHE_IO_FORCE_SEEK AND NOT (USE_CACHE AND "${TARGET}" STREQUAL "MVS"))
    message(FATAL_ERROR "CACHE_IO_FORCE_SEEK requires TARGET=MVS and USE_CACHE=ON")
endif()
if(PLATFORM STREQUAL "PS2")
    set(PS2_FAST_CACHE_DEFAULT ON)
else()
    set(PS2_FAST_CACHE_DEFAULT OFF)
endif()
option(PS2_FAST_CACHE "PS2: enable accelerated cache I/O for cached cores" ${PS2_FAST_CACHE_DEFAULT})
if(CACHE_IO_VALIDATE_ACCELERATED AND NOT (PLATFORM STREQUAL "PS2" AND "${TARGET}" STREQUAL "MVS" AND USE_CACHE AND PS2_FAST_CACHE))
    message(FATAL_ERROR "CACHE_IO_VALIDATE_ACCELERATED requires PS2 MVS with USE_CACHE=ON and PS2_FAST_CACHE=ON")
endif()
if(PLATFORM STREQUAL "PS2" AND "${TARGET}" STREQUAL "MVS")
    set(PS2_DIRTY_SPRITE_UPLOADS_DEFAULT ON)
else()
    set(PS2_DIRTY_SPRITE_UPLOADS_DEFAULT OFF)
endif()
option(PS2_DIRTY_SPRITE_UPLOADS "PS2 MVS: upload sprite atlases only when their pixels change" ${PS2_DIRTY_SPRITE_UPLOADS_DEFAULT})
if(PS2_DIRTY_SPRITE_UPLOADS AND NOT (PLATFORM STREQUAL "PS2" AND "${TARGET}" STREQUAL "MVS"))
    message(FATAL_ERROR "PS2_DIRTY_SPRITE_UPLOADS requires PLATFORM=PS2 and TARGET=MVS")
endif()
if(PS2_DIRTY_SPRITE_UPLOADS)
    add_definitions(-DMVS_DIRTY_SPRITE_UPLOADS=1)
endif()
set(PS2_CACHE_RESERVE_KB "1024" CACHE STRING "PS2: heap reserve for LOW/MEDIUM/HIGH cache tiers (1024 or 2048 KiB)")
set_property(CACHE PS2_CACHE_RESERVE_KB PROPERTY STRINGS 1024 2048)
if(NOT PS2_CACHE_RESERVE_KB MATCHES "^(1024|2048)$")
    message(FATAL_ERROR "PS2_CACHE_RESERVE_KB must be 1024 or 2048")
endif()
if(NOT PLATFORM STREQUAL "PS2" AND NOT PS2_CACHE_RESERVE_KB STREQUAL "1024")
    message(FATAL_ERROR "PS2_CACHE_RESERVE_KB is only configurable for PLATFORM=PS2")
endif()
if(PLATFORM STREQUAL "PS2")
    add_definitions(-DPS2_CACHE_RESERVE_KB=${PS2_CACHE_RESERVE_KB})
endif()
option(PS2_EXTERNAL_IRX_IMAGE "PS2: load ps2_drivers IRXs from an external image" OFF)
set(PS2_VIDEO_MODE "480i" CACHE STRING "PS2 video output mode (240p, 480i, or 480p)")
set_property(CACHE PS2_VIDEO_MODE PROPERTY STRINGS 240p 480i 480p)
if(NOT PS2_VIDEO_MODE MATCHES "^(240p|480i|480p)$")
    message(FATAL_ERROR "PS2_VIDEO_MODE must be 240p, 480i, or 480p")
endif()
if(NOT PLATFORM STREQUAL "PS2" AND NOT PS2_VIDEO_MODE STREQUAL "480i")
    message(FATAL_ERROR "PS2_VIDEO_MODE is only configurable for PLATFORM=PS2")
endif()
if(PLATFORM STREQUAL "PS2")
    if(PS2_VIDEO_MODE STREQUAL "240p")
		add_definitions(-DDEFAULT_VIDEO_OUTPUT_MODE=VIDEO_OUTPUT_240P)
    elseif(PS2_VIDEO_MODE STREQUAL "480p")
		add_definitions(-DDEFAULT_VIDEO_OUTPUT_MODE=VIDEO_OUTPUT_480P)
    else()
		add_definitions(-DDEFAULT_VIDEO_OUTPUT_MODE=VIDEO_OUTPUT_480I)
    endif()
endif()
option(PSVITA_VIDEO_STATS "PS Vita: log scenes/draws/quads per frame to ux0:data/njemu_video.log" OFF)
if(PLATFORM STREQUAL "DESKTOP")
    set(USE_DESKTOP_GL_DEFAULT ON)
else()
    set(USE_DESKTOP_GL_DEFAULT OFF)
endif()
option(USE_DESKTOP_GL "Desktop: compile the OpenGL 3.3 video backend alongside SDL" ${USE_DESKTOP_GL_DEFAULT})

# Add options to compiler definitions
if (GUI)
    add_definitions(-DGUI)
endif()

if(KERNEL_MODE AND NOT PLATFORM STREQUAL "PSP")
    message(FATAL_ERROR "KERNEL_MODE requires PLATFORM=PSP")
endif()
if(KERNEL_MODE)
    add_definitions(-DKERNEL_MODE)
endif()

if (COMMAND_LIST)
    add_definitions(-DCOMMAND_LIST)
endif()

if(ADHOC AND NOT (PLATFORM STREQUAL "PSP" AND "${TARGET}" STREQUAL "MVS"))
    message(FATAL_ERROR "ADHOC requires PLATFORM=PSP and TARGET=MVS")
endif()
if(ADHOC)
    add_definitions(-DADHOC)
endif()

if (SAVE_STATE)
    add_definitions(-DSAVE_STATE)
endif()

if (RELEASE)
    add_definitions(-DRELEASE=1)
endif()

if (CACHE_IO_PROFILE)
    add_definitions(-DCACHE_IO_PROFILE=1)
endif()

if (CACHE_IO_FORCE_SEEK)
    add_definitions(-DCACHE_IO_FORCE_SEEK=1)
endif()

if (CACHE_IO_VALIDATE_ACCELERATED)
    add_definitions(-DCACHE_IO_VALIDATE_ACCELERATED=1)
endif()

if(PS2_FAST_CACHE AND NOT PLATFORM STREQUAL "PS2")
    message(FATAL_ERROR "PS2_FAST_CACHE requires PLATFORM=PS2")
endif()
if(PS2_FAST_CACHE AND NOT USE_CACHE)
    message(FATAL_ERROR "PS2_FAST_CACHE requires USE_CACHE=ON")
endif()
if(PS2_FAST_CACHE)
    add_definitions(-DPS2_FAST_CACHE=1)
endif()

if(PSVITA_VIDEO_STATS AND NOT PLATFORM STREQUAL "PSVITA")
    message(FATAL_ERROR "PSVITA_VIDEO_STATS requires PLATFORM=PSVITA")
endif()
if(USE_DESKTOP_GL AND NOT PLATFORM STREQUAL "DESKTOP")
    message(FATAL_ERROR "USE_DESKTOP_GL requires PLATFORM=DESKTOP")
endif()
if(PLATFORM STREQUAL "DESKTOP" AND USE_DESKTOP_GL)
    add_definitions(-DHAVE_VIDEO_BACKEND_OPENGL=1)
endif()
if(PS2_EXTERNAL_IRX_IMAGE AND NOT PLATFORM STREQUAL "PS2")
    message(FATAL_ERROR "PS2_EXTERNAL_IRX_IMAGE requires PLATFORM=PS2")
endif()
if(PSVITA_VIDEO_STATS)
    add_definitions(-DPSVITA_VIDEO_STATS=1)
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
if (PLATFORM STREQUAL "PSP" AND NOT USE_ASAN AND NOT USE_UBSAN)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

# Common Flags options
if (USE_ASAN OR USE_UBSAN)
    # Sanitizer builds are diagnostic builds.  Keep enough optimization to
    # exercise realistic code paths while preserving useful stack traces and
    # avoiding the very expensive sanitizer instrumentation generated for the
    # large CPU cores at -O3.
    set(COMMON_FLAGS
        -O1
        -fno-omit-frame-pointer
    )
else()
    set(COMMON_FLAGS
        -O3
        -ffast-math
    )
endif()

# Keep functions and data in independent sections on every platform.  Linkers
# that support dead-section elimination can then discard unused code/data, while
# platforms that do not currently enable it still keep the objects ready for
# that optimization without changing runtime semantics.
list(APPEND COMMON_FLAGS
    -ffunction-sections
    -fdata-sections
)

# PSP runs on the Allegrex CPU. Target it explicitly instead of GCC's MIPS-I
# default so instruction selection and scheduling match the real hardware.
if (PLATFORM STREQUAL "PSP")
    list(APPEND COMMON_FLAGS
        -march=allegrex
        -mtune=allegrex
        -mno-check-zero-division
    )
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND COMMON_FLAGS
        -fsingle-precision-constant
        -funroll-loops
        -fipa-pta
    )
endif()

# Common Warning options
set(WARNING_OPTIONS
    -Wall
    -Wextra
    -Werror
)

# Common source files
set(COMMON_SRC
    emumain.c
    common/zip_archive.h
    sound/sndintrf.h
    sound/sndintrf.c
    common/cache.h
    common/cache.c
    common/cache_storage_driver.h
    common/filer.h
    common/filer.c
    common/loadrom.h
    common/loadrom.c
    common/thread_driver.h
    common/audio_driver.h
    common/power_driver.h
    common/power_driver.c
    common/ticker_driver.h
    common/ticker_driver.c
    common/input_driver.h
    common/input_driver.c
    common/input_state.h
    common/input_state.c
    common/video_driver.h
    common/video_driver.c
	common/video_geometry.h
	common/runtime_paths.h
	common/emulator_runtime.h
	common/emulator_options.h
	common/emulator_video.h
	common/capcom_driver_info.h
	common/neogeo_sound_io.h
	common/ui_defs.h
	    common/ui_draw_driver.h
	    common/ui_draw_driver.c
	    common/ui_layout.h
	    common/ui_layout.c
	    common/ui_text_driver.h
    common/ui_text_driver.c
	    common/ui_language.h
	    common/ui_text_catalog.h
	    common/ui_text_catalog.c
	    common/ui_utf8.h
	    common/ui_utf8.c
	    common/ui_unicode_glyph.h
    common/font_t.h
    common/platform_driver.h
	common/platform_memory_info.h
	common/platform_memory_info.c
	common/memory_plan.h
	common/memory_plan.c
	    common/sound.h
	    common/sound.c
	    common/audio_profile.h
	    common/audio_producer_driver.h
	    common/audio_producer_cpu.c
)

if(PLATFORM STREQUAL "PSP" AND PSP_ME_AUDIO)
    list(APPEND COMMON_SRC psp/psp_audio_producer.c)
    add_definitions(-DPSP_ME_AUDIO=1)
else()
    list(APPEND COMMON_SRC common/audio_producer_binding_cpu.c)
endif()

if(PLATFORM STREQUAL "PSP" AND PSP_AUDIO_PROFILE)
    list(APPEND COMMON_SRC psp/psp_audio_profile.c)
    add_definitions(-DPSP_AUDIO_PROFILE=1)
endif()

find_package(miniz 3.1.2 CONFIG QUIET)
if (TARGET miniz::miniz)
    set(NJEMU_MINIZ_TARGET miniz::miniz)
elseif (PLATFORM STREQUAL "DESKTOP")
    # Desktop development/CI can obtain the pinned dependency directly when
    # the host does not provide a miniz CMake package. Console toolchains must
    # provide their native miniz package instead.
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build miniz examples" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "Build miniz tests" FORCE)
    set(INSTALL_PROJECT OFF CACHE BOOL "Install miniz" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build miniz shared" FORCE)
    FetchContent_Declare(miniz_upstream
        GIT_REPOSITORY https://github.com/richgel999/miniz.git
        GIT_TAG 3.1.2
        GIT_SHALLOW TRUE
    )
    set(NJEMU_BUILD_TYPE_BEFORE_MINIZ "${CMAKE_BUILD_TYPE}")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.14")
        FetchContent_MakeAvailable(miniz_upstream)
    else()
        FetchContent_GetProperties(miniz_upstream)
        if(NOT miniz_upstream_POPULATED)
            FetchContent_Populate(miniz_upstream)
            add_subdirectory(${miniz_upstream_SOURCE_DIR} ${miniz_upstream_BINARY_DIR} EXCLUDE_FROM_ALL)
        endif()
    endif()
    # miniz 3.1.2 forces an unspecified top-level build type to Release.
    # Restore NJEMU's setting so FetchContent cannot silently add NDEBUG to
    # our targets/tests or otherwise change the requested configuration.
    if(NJEMU_BUILD_TYPE_BEFORE_MINIZ STREQUAL "")
        set(CMAKE_BUILD_TYPE "" CACHE STRING "Choose the type of build." FORCE)
    endif()
    set(NJEMU_MINIZ_TARGET miniz)
else()
    message(FATAL_ERROR "miniz 3.1.2 is required by NJEMU on ${PLATFORM}")
endif()

get_target_property(MINIZ_INCLUDE_DIRS ${NJEMU_MINIZ_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(MINIZ_COMPILE_DEFINITIONS ${NJEMU_MINIZ_TARGET} INTERFACE_COMPILE_DEFINITIONS)
if (NOT MINIZ_INCLUDE_DIRS)
    set(MINIZ_INCLUDE_DIRS "")
endif()
if (NOT MINIZ_COMPILE_DEFINITIONS)
    set(MINIZ_COMPILE_DEFINITIONS "")
endif()
list(APPEND COMMON_SRC
    common/zip_archive.c
)

# Additional source files based on options
if (ADHOC)
    set(COMMON_SRC ${COMMON_SRC}
        common/adhoc.h
        common/adhoc_transport.h
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

# Configuration and drawing are runtime services, not menu ownership.  NO_GUI
# only bypasses the interactive menus; games still use their normal INI settings
# and overlays such as ShowFPS still use the regular UI renderer.
set(COMMON_SRC ${COMMON_SRC}
    common/font/graphic.c
    common/font/ascii_14p.c
    common/font/font_s.c
    common/font/bshadow.c
    common/font/command.c
    common/font/ascii_14.c
    common/font/latin1_14.c
    common/font/gbk_s14_runtime.h
    common/font/gbk_s14_runtime.c
    common/ui_draw.c
    common/config.c
)
set(OS_SRC ${OS_SRC}
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_ui_draw.c
)

# Icon glyphs are also consumed by target loading/progress paths.
if (${TARGET} STREQUAL "CPS1" OR ${TARGET} STREQUAL "CPS2")
    set(ICON_PREFIX "cps")
elseif (${TARGET} STREQUAL "MVS")
    set(ICON_PREFIX "mvs")
elseif (${TARGET} STREQUAL "NCDZ")
    set(ICON_PREFIX "ncdz")
endif()
set(COMMON_SRC ${COMMON_SRC}
    common/icon/${ICON_PREFIX}_s.c
    common/icon/${ICON_PREFIX}_l.c
)

if (NOT GUI)
    set(COMMON_SRC ${COMMON_SRC}
        common/no_gui.c
    )
else()
    # Interactive GUI sources.
    set(COMMON_SRC ${COMMON_SRC}
        common/ui.c
        common/ui_menu.c
        common/png.c
    )
endif()


set(OS_SRC ${OS_SRC}
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_drivers.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_platform.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_ticker.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_input.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_video.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_audio.c
    ${PLATFORM_LOWER}/${PLATFORM_LOWER}_thread.c
)

if (PLATFORM STREQUAL "PS2")
    list(APPEND OS_SRC ps2/ps2_cache_storage.c)
endif()

if (PLATFORM STREQUAL "PSP")
    list(APPEND OS_SRC psp/psp_power.c)
endif()

# GPU backends replaying a recorded work frame (see common/hw_recorder.h)
if (PLATFORM STREQUAL "PSVITA")
    # Vita always ships both renderers. GXM is the compatibility/default backend
    # and vitaGL is selected at runtime through the global video backend setting.
    list(APPEND OS_SRC
        psvita/psvita_video_gl.c
        psvita/psvita_power.c
        common/hw_recorder.c
    )
elseif (PLATFORM STREQUAL "DESKTOP")
    # Desktop backends share the platform-neutral linear UI texture store.
    # Rendering those texels belongs to the selected video backend.
    list(REMOVE_ITEM OS_SRC desktop/desktop_ui_draw.c)
    # Desktop keeps SDL as the baseline backend. OpenGL is compiled alongside
    # it when requested so backend selection can become runtime policy.
    if (USE_DESKTOP_GL)
        list(APPEND OS_SRC
            desktop/desktop_video_gl.c
            common/hw_recorder.c
        )
    endif()
    list(APPEND OS_SRC
        desktop/desktop_frame_dump.c
        common/linear_ui_draw.c
    )
endif()

if(ADHOC)
    set(OS_SRC ${OS_SRC}
        psp/adhoc.c
    )
endif()

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
    set(TARGET_SRC ${TARGET_SRC}
        mvs/mvs.h
        mvs/mvs.c
        mvs/driver.h
        mvs/driver.c
        mvs/memintrf.h
        mvs/memintrf.c
        mvs/processed_assets.h
        mvs/processed_assets.c
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
        mvs/sprite.c
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
    set(TARGET_SRC ${TARGET_SRC}
        common/coin.h
        common/coin.c
        cps1/cps1.h
        cps1/sound_io.h
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
        cps1/sprite.c
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
if (${TARGET} STREQUAL "CPS2")
    set(TARGET_SRC ${TARGET_SRC}
        common/coin.h
        common/coin.c
        cps2/cps2.h
        cps2/cps2.c
        cps2/cps2crpt.c
        cps2/driver.h
        cps2/driver.c
        cps2/memintrf.h
        cps2/memintrf.c
        cps2/inptport.h
        cps2/inptport.c
        cps2/timer.h
        cps2/timer.c
        cps2/vidhrdw.h
        cps2/vidhrdw.c
        cps2/sprite.h
        cps2/sprite_common.h
        cps2/sprite_common.c
        cps2/sprite.c
        cps2/eeprom.h
        cps2/eeprom.c
    )

    # Set SOUND_QSOUND flag for CPS2
    set(SOUND_QSOUND ON)
endif()

# Add specific target source files
if (${TARGET} STREQUAL "NCDZ")
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
        ncdz/resource_source.c
        ncdz/resource_source.h
        ncdz/sprite_common.h
        ncdz/sprite_common.c
        ncdz/sprite.c
        ncdz/sprite.h
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

# Every NJEMU source includes project headers from the source root (for example
# "common/video_driver.h" or "mvs/driver.h").  Keep that search path local to
# the executable instead of mutating directory-wide include state. Quoted
# same-directory headers (such as "mvs.h" from src/mvs/) continue to resolve
# through the compiler's normal current-file lookup, so platform/CPU/target
# subdirectories do not need separate search paths.
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(PLATFORM STREQUAL "PSP" AND PSP_ME_AUDIO)
    find_path(PSP_ME_CORE_INCLUDE_DIR
        NAMES me-core-mapper/me-core-mapper.h
        HINTS "$ENV{PSPDEV}/psp/include"
    )
    find_path(PSP_ME_SAFE_TASK_INCLUDE_DIR
        NAMES me-safe-task/me-stask.h
        HINTS "$ENV{PSPDEV}/psp/include"
    )
    find_library(PSP_ME_CORE_MAPPER_LIBRARY
        NAMES me-core-mapper
        HINTS "$ENV{PSPDEV}/psp/lib"
    )
    find_library(PSP_ME_SAFE_TASK_LIBRARY
        NAMES me-stask
        HINTS "$ENV{PSPDEV}/psp/lib"
    )

    if(NOT PSP_ME_CORE_INCLUDE_DIR OR NOT PSP_ME_SAFE_TASK_INCLUDE_DIR OR
       NOT PSP_ME_CORE_MAPPER_LIBRARY OR NOT PSP_ME_SAFE_TASK_LIBRARY)
        message(FATAL_ERROR
            "PSP_ME_AUDIO=ON requires psp-media-engine-custom-core and "
            "psp-media-engine-safe-task installed in PSPDEV")
    endif()

    target_include_directories(${TARGET} SYSTEM PRIVATE
        "${PSP_ME_CORE_INCLUDE_DIR}"
        "${PSP_ME_SAFE_TASK_INCLUDE_DIR}"
    )
endif()

# Keep miniz's usage requirements local to implementation files that consume
# compression/CRC APIs. Public domain headers deliberately do not expose miniz
# types.
set_source_files_properties(
    src/common/zip_archive.c
    src/common/png.c
    src/common/state.c
    src/common/filer.c
    src/ncdz/memintrf.c
    PROPERTIES
    INCLUDE_DIRECTORIES "${MINIZ_INCLUDE_DIRS}"
    COMPILE_DEFINITIONS "${MINIZ_COMPILE_DEFINITIONS}"
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TRANSLATION_PACK_DIR "${CMAKE_BINARY_DIR}/lang")
set(UI_UNICODE_GLYPH_SOURCE "${CMAKE_BINARY_DIR}/generated/ui_unicode_glyph.c")
set(TRANSLATION_PACKS
    "${TRANSLATION_PACK_DIR}/en.lng"
    "${TRANSLATION_PACK_DIR}/ja.lng"
    "${TRANSLATION_PACK_DIR}/es.lng"
    "${TRANSLATION_PACK_DIR}/zh-Hans.lng"
    "${TRANSLATION_PACK_DIR}/zh-Hant.lng"
)
add_custom_command(
    OUTPUT ${TRANSLATION_PACKS} "${UI_UNICODE_GLYPH_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TRANSLATION_PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/build_translations.py"
        --build
        --output-dir "${TRANSLATION_PACK_DIR}"
        --unicode-map-output "${UI_UNICODE_GLYPH_SOURCE}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/build_translations.py"
        "${CMAKE_SOURCE_DIR}/src/common/font/gbk_tbl.c"
        "${CMAKE_SOURCE_DIR}/translations/messages.def"
        "${CMAKE_SOURCE_DIR}/translations/en.lang"
        "${CMAKE_SOURCE_DIR}/translations/ja.lang"
        "${CMAKE_SOURCE_DIR}/translations/es.lang"
        "${CMAKE_SOURCE_DIR}/translations/zh-Hans.lang"
        "${CMAKE_SOURCE_DIR}/translations/zh-Hant.lang"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)
add_custom_target(translation_packs ALL DEPENDS ${TRANSLATION_PACKS} "${UI_UNICODE_GLYPH_SOURCE}")
add_dependencies(${TARGET} translation_packs)
set_source_files_properties("${UI_UNICODE_GLYPH_SOURCE}" PROPERTIES GENERATED TRUE)
target_sources(${TARGET} PRIVATE "${UI_UNICODE_GLYPH_SOURCE}")

set(GBK_FONT_ASSET "${CMAKE_BINARY_DIR}/font/gbk_s14.bin")
add_custom_command(
    OUTPUT "${GBK_FONT_ASSET}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/font"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tools/build_font_asset.py"
        --source "${CMAKE_SOURCE_DIR}/src/common/font/gbk_s14.c"
        --table-source "${CMAKE_SOURCE_DIR}/src/common/font/gbk_tbl.c"
        --output "${GBK_FONT_ASSET}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/build_font_asset.py"
        "${CMAKE_SOURCE_DIR}/src/common/font/gbk_s14.c"
        "${CMAKE_SOURCE_DIR}/src/common/font/gbk_tbl.c"
    VERBATIM
)
add_custom_target(font_assets ALL DEPENDS "${GBK_FONT_ASSET}")
add_dependencies(${TARGET} font_assets)

# Sanitizers are intentionally selectable independently. AddressSanitizer is
# useful for lifecycle/ownership testing, while UndefinedBehaviorSanitizer can
# make the large emulated CPU translation units substantially more expensive to
# compile and is better run as a separate diagnostic pass.
set(SANITIZER_OPTIONS)
if (USE_ASAN)
    list(APPEND SANITIZER_OPTIONS -fsanitize=address)
endif()
if (USE_UBSAN)
    list(APPEND SANITIZER_OPTIONS -fsanitize=undefined)

    # c68k.c is a small source file built around macros that expand into a very
    # large interpreter.  Instrumenting that expansion with UBSan takes many
    # minutes per target with AppleClang and makes a cross-core diagnostic sweep
    # impractical.  Keep the surrounding CPU interface and the rest of NJEMU
    # instrumented; c68k can still be audited separately when required.
    set_source_files_properties(src/cpu/m68000/c68k.c PROPERTIES
        COMPILE_OPTIONS "-fno-sanitize=undefined"
    )
endif()


# Define the source directory (resources folder)
string(TOLOWER ${TARGET} TARGET_LOWER)
set(RESOURCE_DIR "${CMAKE_SOURCE_DIR}/resources/${TARGET_LOWER}/")

# Set the destination directory to the build directory (CMAKE_BINARY_DIR)
set(DESTINATION_DIR "${CMAKE_BINARY_DIR}")

# Keep release/install contents independent from whatever local validation data
# happens to exist under resources/<target>. The source resource tree is also
# used as a convenient local ROM/BIOS/cache root, so package discovery must
# never be a directory-wide glob/copy.
set(NJEMU_DISTRIBUTED_RESOURCE_FILES
    README.md
)
if("${TARGET}" STREQUAL "CPS1")
    list(APPEND NJEMU_DISTRIBUTED_RESOURCE_FILES
        gamelist_cps1.txt
        rominfo.cps1
        zipname.cps1
        zipnamej.cps1
        zipnamech1.cps1
        zipnamech2.cps1
    )
elseif("${TARGET}" STREQUAL "CPS2")
    list(APPEND NJEMU_DISTRIBUTED_RESOURCE_FILES
        gamelist_cps2.txt
        rominfo.cps2
        zipname.cps2
        zipnamej.cps2
        zipnamech1.cps2
        zipnamech2.cps2
    )
elseif("${TARGET}" STREQUAL "MVS")
    list(APPEND NJEMU_DISTRIBUTED_RESOURCE_FILES
        gamelist_mvs.txt
        rominfo.mvs
        zipname.mvs
        zipnamej.mvs
        zipnamech1.mvs
        zipnamech2.mvs
    )
endif()

foreach(NJEMU_RESOURCE_FILE IN LISTS NJEMU_DISTRIBUTED_RESOURCE_FILES)
    if(NOT EXISTS "${RESOURCE_DIR}${NJEMU_RESOURCE_FILE}")
        message(FATAL_ERROR
            "Distributed resource is missing: ${RESOURCE_DIR}${NJEMU_RESOURCE_FILE}")
    endif()
endforeach()

# Package only the empty runtime directories the selected binary can actually
# use. Generate private placeholders instead of borrowing placeholders from
# resources/, because the latter historically contain directories that are not
# meaningful to every core.
set(NJEMU_RUNTIME_DATA_DIRS
    roms
    config
)
if(GUI)
    list(APPEND NJEMU_RUNTIME_DATA_DIRS cheats)
endif()
if(SAVE_STATE)
    list(APPEND NJEMU_RUNTIME_DATA_DIRS state)
endif()
if("${TARGET}" STREQUAL "CPS1")
    list(APPEND NJEMU_RUNTIME_DATA_DIRS nvram)
elseif("${TARGET}" STREQUAL "CPS2")
    list(APPEND NJEMU_RUNTIME_DATA_DIRS nvram)
    if(USE_CACHE)
        list(APPEND NJEMU_RUNTIME_DATA_DIRS cache)
    endif()
elseif("${TARGET}" STREQUAL "MVS")
    # processed/ is canonical even for full-resident builds because encrypted
    # sets may require offline-converted C/S/V data. cache/ remains only a
    # compatibility fallback for existing USE_CACHE installs.
    list(APPEND NJEMU_RUNTIME_DATA_DIRS processed memcard nvram)
elseif("${TARGET}" STREQUAL "NCDZ")
    if(GUI)
        list(APPEND NJEMU_RUNTIME_DATA_DIRS data)
    endif()
endif()

set(NJEMU_RUNTIME_LAYOUT_DIR "${CMAKE_BINARY_DIR}/generated/runtime_layout")
set(NJEMU_DEFAULT_GAME_SELECTOR "${NJEMU_RUNTIME_LAYOUT_DIR}/game_name.ini")
file(MAKE_DIRECTORY "${NJEMU_RUNTIME_LAYOUT_DIR}")
file(WRITE "${NJEMU_DEFAULT_GAME_SELECTOR}" "")

set(NJEMU_RUNTIME_PLACEHOLDER_FILES)
foreach(NJEMU_RUNTIME_DATA_DIR IN LISTS NJEMU_RUNTIME_DATA_DIRS)
    set(NJEMU_RUNTIME_PLACEHOLDER
        "${NJEMU_RUNTIME_LAYOUT_DIR}/${NJEMU_RUNTIME_DATA_DIR}/_placeholder")
    file(MAKE_DIRECTORY "${NJEMU_RUNTIME_LAYOUT_DIR}/${NJEMU_RUNTIME_DATA_DIR}")
    file(WRITE "${NJEMU_RUNTIME_PLACEHOLDER}" "")
    list(APPEND NJEMU_RUNTIME_PLACEHOLDER_FILES "${NJEMU_RUNTIME_PLACEHOLDER}")
endforeach()

# Keep development staging explicit for the same reason release packaging is
# explicit: resources/<target> is a validation tree and may contain unrelated
# local files. Known read-only inputs are linked by default, while mutable
# inputs are copied once so running a build never modifies resources/.
set(NJEMU_BUILD_RESOURCE_ENTRIES
    ${NJEMU_DISTRIBUTED_RESOURCE_FILES}
    game_name.ini
    njemu.ini
    roms
    config
)
if(GUI)
    list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES cheats)
endif()
if(SAVE_STATE)
    list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES state)
endif()
if(COMMAND_LIST)
    list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES command.dat)
endif()
if("${TARGET}" STREQUAL "CPS1")
    list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES nvram)
elseif("${TARGET}" STREQUAL "CPS2")
    list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES nvram)
    if(USE_CACHE)
        list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES cache)
    endif()
elseif("${TARGET}" STREQUAL "MVS")
    list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES processed memcard nvram)
    if(USE_CACHE)
        list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES cache)
    endif()
elseif("${TARGET}" STREQUAL "NCDZ")
    list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES neocd.bin 000-lo.lo backup.bin)
    if(GUI)
        list(APPEND NJEMU_BUILD_RESOURCE_ENTRIES data)
    endif()
endif()
list(REMOVE_DUPLICATES NJEMU_BUILD_RESOURCE_ENTRIES)

# COPY_RESOURCES makes the explicit development staging set self-contained on
# platforms where symlinks are problematic; it does not broaden the set.
option(COPY_RESOURCES "Copy resources into the build directory instead of linking" OFF)

set(MUTABLE_RESOURCE_ENTRIES
    "config"
    "memcard"
    "nvram"
    "state"
    "game_name.ini"
    "njemu.ini"
    "command.dat"
    "backup.bin"
)

foreach(RESOURCE_ENTRY IN LISTS NJEMU_BUILD_RESOURCE_ENTRIES)
    set(RESOURCE_SOURCE "${RESOURCE_DIR}${RESOURCE_ENTRY}")
    set(RESOURCE_LINK "${DESTINATION_DIR}/${RESOURCE_ENTRY}")

    if(NOT EXISTS "${RESOURCE_SOURCE}" AND NOT IS_SYMLINK "${RESOURCE_SOURCE}")
        continue()
    endif()

    if(COPY_RESOURCES)
        if(NOT EXISTS "${RESOURCE_LINK}" AND NOT IS_SYMLINK "${RESOURCE_LINK}")
            file(COPY "${RESOURCE_SOURCE}" DESTINATION "${DESTINATION_DIR}")
        endif()
    else()
        string(TOLOWER "${RESOURCE_ENTRY}" RESOURCE_ENTRY_LOWER)
        list(FIND MUTABLE_RESOURCE_ENTRIES "${RESOURCE_ENTRY_LOWER}" MUTABLE_RESOURCE_INDEX)

        if(NOT MUTABLE_RESOURCE_INDEX EQUAL -1)
            # Migrate a build produced by the old layout without touching the
            # linked source. Existing real files/directories are intentionally
            # left alone so re-configuring never overwrites runtime saves.
            if(IS_SYMLINK "${RESOURCE_LINK}")
                file(REMOVE "${RESOURCE_LINK}")
            endif()
            if(NOT EXISTS "${RESOURCE_LINK}")
                file(COPY "${RESOURCE_SOURCE}" DESTINATION "${DESTINATION_DIR}")
            endif()
        elseif(NOT EXISTS "${RESOURCE_LINK}" AND NOT IS_SYMLINK "${RESOURCE_LINK}")
            file(CREATE_LINK "${RESOURCE_SOURCE}" "${RESOURCE_LINK}" SYMBOLIC)
        endif()
    endif()
endforeach()

# Fresh no-GUI builds do not run the GUI directory bootstrap, so ensure every
# selected runtime directory exists even when the validation tree has no local
# data for it (for example a clean MVS checkout has no processed/ directory).
foreach(NJEMU_RUNTIME_DATA_DIR IN LISTS NJEMU_RUNTIME_DATA_DIRS)
    set(NJEMU_RUNTIME_DATA_PATH "${DESTINATION_DIR}/${NJEMU_RUNTIME_DATA_DIR}")
    if(NOT EXISTS "${NJEMU_RUNTIME_DATA_PATH}" AND NOT IS_SYMLINK "${NJEMU_RUNTIME_DATA_PATH}")
        file(MAKE_DIRECTORY "${NJEMU_RUNTIME_DATA_PATH}")
    endif()
endforeach()

message(STATUS "Resources staged from ${RESOURCE_DIR} into: ${DESTINATION_DIR}")

target_compile_options(${TARGET} PUBLIC ${COMMON_FLAGS} ${WARNING_OPTIONS} ${SANITIZER_OPTIONS})
target_link_options(${TARGET} PUBLIC ${SANITIZER_OPTIONS})

# Add platform specific libraries
if (PLATFORM STREQUAL "DESKTOP")
    find_package(SDL2 REQUIRED)
    target_include_directories(${TARGET} PRIVATE
        ${SDL2_INCLUDE_DIRS}
    )
    if(TARGET SDL2::SDL2-static)
        set(NJEMU_SDL2_TARGET SDL2::SDL2-static)
    elseif(TARGET SDL2::SDL2)
        set(NJEMU_SDL2_TARGET SDL2::SDL2)
    else()
        set(NJEMU_SDL2_TARGET ${SDL2_LIBRARIES})
    endif()
    target_link_libraries(${TARGET} PUBLIC ${NJEMU_SDL2_TARGET})
elseif(PLATFORM STREQUAL "PS2")
    # PS2SDK/gsKit headers are external dependencies. Keep -Werror strict for
    # NJEMU sources without promoting warnings inside third-party headers.
    target_include_directories(${TARGET} SYSTEM BEFORE PRIVATE
        "$ENV{PS2SDK}/ee/include"
        "$ENV{PS2SDK}/common/include"
        "$ENV{PS2SDK}/ee/libcglue/include"
        "$ENV{PS2DEV}/gsKit/include"
    )

    # The classic PS2 executable is directly launchable, so identify it as an
    # ELF.  External-image builds deliberately keep the real engine extensionless
    # so BOOT.ELF remains the obvious entry point for users and launchers.
    if(PS2_EXTERNAL_IRX_IMAGE)
        set_target_properties(${TARGET} PROPERTIES SUFFIX "")
    else()
        set_target_properties(${TARGET} PROPERTIES SUFFIX ".ELF")
    endif()

    target_link_libraries(${TARGET} PUBLIC
        patches
        gskit
        dmakit
        "-ldebug"
    )
    if(PS2_EXTERNAL_IRX_IMAGE)
        find_file(PS2_DRIVERS_IRX_IMAGE
            NAMES ps2_drivers.irximg
            HINTS "$ENV{PS2SDK}/ports/share/ps2_drivers"
            NO_DEFAULT_PATH
        )
        if(NOT PS2_DRIVERS_IRX_IMAGE)
            message(FATAL_ERROR
                "ps2_drivers.irximg was not found under the installed PS2SDK ports tree")
        endif()
        find_file(PS2_DRIVERS_IRXIMG_BOOTSTRAP
            NAMES ps2_drivers_irximg_bootstrap.elf
            HINTS "$ENV{PS2SDK}/ports/share/ps2_drivers"
            NO_DEFAULT_PATH
        )
        if(NOT PS2_DRIVERS_IRXIMG_BOOTSTRAP)
            message(FATAL_ERROR
                "ps2_drivers_irximg_bootstrap.elf was not found under the installed PS2SDK ports tree")
        endif()

        target_compile_definitions(${TARGET} PRIVATE PS2_EXTERNAL_IRX_IMAGE=1)
        target_link_libraries(${TARGET} PUBLIC ps2_drivers_img)
        file(GENERATE
            OUTPUT "$<TARGET_FILE_DIR:${TARGET}>/elf_path.ini"
            CONTENT "$<TARGET_FILE_NAME:${TARGET}>\n"
        )
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PS2_DRIVERS_IRX_IMAGE}"
                "$<TARGET_FILE_DIR:${TARGET}>/ps2_drivers.irximg"
            COMMENT "Staging ps2_drivers.irximg beside the PS2 ELF"
        )
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PS2_DRIVERS_IRXIMG_BOOTSTRAP}"
                "$<TARGET_FILE_DIR:${TARGET}>/BOOT.ELF"
            COMMENT "Staging BOOT.ELF and target configuration beside the PS2 ELF"
        )
    else()
        target_link_libraries(${TARGET} PUBLIC ps2_drivers)
    endif()
elseif(PLATFORM STREQUAL "PSVITA")
    # GCC's <limits.h> in the VitaSDK toolchain does not reach newlib's, which
    # defines PATH_MAX as 4096: provide the same value to the whole build.
    target_compile_definitions(${TARGET} PRIVATE PATH_MAX=4096)
    target_link_libraries(${TARGET} PUBLIC
        vita2d
        vitaGL
        vitashark
        SceShaccCgExt
        taihen_stub
        SceShaccCg_stub
        SceKernelDmacMgr_stub
        mathneon
        stdc++
    )
    target_link_libraries(${TARGET} PUBLIC
        SceDisplay_stub
        SceGxm_stub
        SceSysmodule_stub
        SceCtrl_stub
        SceAudio_stub
        ScePower_stub
        SceCommonDialog_stub
        SceAppMgr_stub
        SceAppUtil_stub
        m
    )
elseif(PLATFORM STREQUAL "PSP")
    target_link_libraries(${TARGET} PUBLIC
        pspdisplay
        pspge 
        pspctrl
        pspaudio
        pspgu
        psppower
        pspwlan
        pspdebug
    )
    if(PSP_ME_AUDIO)
        target_link_libraries(${TARGET} PRIVATE
            "${PSP_ME_SAFE_TASK_LIBRARY}"
            "${PSP_ME_CORE_MAPPER_LIBRARY}"
            pspaudiocodec
        )
    endif()
    if(ADHOC)
        target_link_libraries(${TARGET} PUBLIC
            pspnet
            pspnet_adhoc
            pspnet_adhocctl
            pspnet_adhocmatching
        )
    endif()
endif()

# Link only miniz itself here: zip_archive.c receives the dependency's compile
# usage requirements above, while the rest of NJEMU remains independent of
# miniz headers and definitions. LINK_ONLY also preserves CMake's target-level
# build dependency, unlike spelling the archive path with TARGET_FILE.
target_link_libraries(${TARGET} PRIVATE "$<LINK_ONLY:${NJEMU_MINIZ_TARGET}>")
if (PLATFORM STREQUAL "PS2")
    # The PS2 miniz package should be compiled with -ffunction-sections and
    # -fdata-sections so this can discard unused writer/deflate APIs while
    # keeping the upstream library otherwise unmodified.
    target_link_options(${TARGET} PRIVATE -Wl,--gc-sections)
endif()

# Add libmad library if needed
if (LIB_MAD)
    # Find the libmad library
    find_path(LIBMAD_INCLUDE_DIR mad.h)
    find_library(LIBMAD_LIBRARY mad)

    # Get the directory of the library for -L
    get_filename_component(LIBMAD_LIBRARY_DIR ${LIBMAD_LIBRARY} DIRECTORY)
    
    target_include_directories(${TARGET} PRIVATE
        ${LIBMAD_INCLUDE_DIR}
    )
    target_link_directories(${TARGET} PUBLIC
        ${LIBMAD_LIBRARY_DIR}
    )
    target_link_libraries(${TARGET} PUBLIC
        mad
    )
endif()

# The GUI PNG implementation uses libm functions such as ceil().  Darwin
# provides them through libSystem, but Linux requires an explicit -lm.
if(PLATFORM STREQUAL "DESKTOP" AND NOT APPLE)
    target_link_libraries(${TARGET} PUBLIC
        m
    )
endif()
       
# Create the VPK for PS Vita: the executable, the target resources and the
# translation packs, laid out like the install tree of the other platforms.
if(PLATFORM STREQUAL "PSVITA")
    # Title IDs: NJMU0000n, one per target so they can be installed side by side
    set(VITA_TITLEID_CPS1 "NJMU00003")
    set(VITA_TITLEID_CPS2 "NJMU00004")
    set(VITA_TITLEID_MVS "NJMU00001")
    set(VITA_TITLEID_NCDZ "NJMU00002")
    # The SFO version must be "NN.NN"
    math(EXPR VITA_VERSION_MAJOR "${VERSION_MAJOR}")
    math(EXPR VITA_VERSION_MINOR "${VERSION_MINOR}")
    if (VITA_VERSION_MAJOR LESS 10)
        set(VITA_VERSION_MAJOR "0${VITA_VERSION_MAJOR}")
    endif()
    if (VITA_VERSION_MINOR LESS 10)
        set(VITA_VERSION_MINOR "0${VITA_VERSION_MINOR}")
    endif()

    set(VITA_VPK_FILE_ARGS)
    foreach(TRANSLATION_PACK IN LISTS TRANSLATION_PACKS)
        get_filename_component(TRANSLATION_NAME "${TRANSLATION_PACK}" NAME)
        list(APPEND VITA_VPK_FILE_ARGS FILE "${TRANSLATION_PACK}" "lang/${TRANSLATION_NAME}")
    endforeach()
    # Use the same explicit distribution manifest as cmake --install. Never let
    # local ROMs, BIOS files, processed assets, caches, saves or configuration
    # leak into a VPK merely because they exist under resources/<target>.
    foreach(VITA_RESOURCE_FILE IN LISTS NJEMU_DISTRIBUTED_RESOURCE_FILES)
        list(APPEND VITA_VPK_FILE_ARGS
            FILE "${RESOURCE_DIR}${VITA_RESOURCE_FILE}" "${VITA_RESOURCE_FILE}")
    endforeach()
    list(APPEND VITA_VPK_FILE_ARGS
        FILE "${NJEMU_DEFAULT_GAME_SELECTOR}" "game_name.ini")

    foreach(VITA_RUNTIME_PLACEHOLDER IN LISTS NJEMU_RUNTIME_PLACEHOLDER_FILES)
        get_filename_component(VITA_RUNTIME_DIR "${VITA_RUNTIME_PLACEHOLDER}" DIRECTORY)
        get_filename_component(VITA_RUNTIME_DIR "${VITA_RUNTIME_DIR}" NAME)
        list(APPEND VITA_VPK_FILE_ARGS
            FILE "${VITA_RUNTIME_PLACEHOLDER}" "${VITA_RUNTIME_DIR}/_placeholder")
    endforeach()

    # ui_init() is common to GUI and no-GUI builds, so the external GBK font is
    # a runtime asset in both configurations.
    list(APPEND VITA_VPK_FILE_ARGS FILE "${GBK_FONT_ASSET}" "font/gbk_s14.bin")

    # Room after the text segment for the SCE module data that vita-elf-create
    # appends (vitasdk/vita-toolchain#303): recent binutils may place the data
    # segment right behind it.
    target_link_options(${TARGET} PRIVATE "-Wl,--defsym=__sce_headroom=0x10000")

    vita_create_self(${TARGET}.self ${TARGET})
    vita_create_vpk(${TARGET}.vpk ${VITA_TITLEID_${TARGET}} ${TARGET}.self
        VERSION "${VITA_VERSION_MAJOR}.${VITA_VERSION_MINOR}"
        NAME "${TARGET}"
        ${VITA_VPK_FILE_ARGS}
    )
endif()

# Create the PBP file for PSP
if(PLATFORM STREQUAL "PSP")
    set(BUILD_PRX ON)
    set(PSP_EBOOT_ICON "${CMAKE_SOURCE_DIR}/data/${TARGET_LOWER}.png")
    create_pbp_file(
        TARGET ${TARGET}
        ICON_PATH "${PSP_EBOOT_ICON}"
        BACKGROUND_PATH NULL
        PREVIEW_PATH NULL
        TITLE "${TARGET} ${VERSION_STR} for PSP"
        VERSION "${VERSION_STR}"
        MEMSIZE 1
    )
endif()

if(BUILD_TESTING AND PLATFORM STREQUAL "DESKTOP")
    if("${TARGET}" STREQUAL "CPS2")
        add_test(NAME cps2_partial_cache_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cps2_partial_cache_tests.py
                ${CMAKE_C_COMPILER})
    endif()
    if("${TARGET}" STREQUAL "MVS")
        add_test(NAME mvs_partial_cache_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mvs_partial_cache_tests.py
                ${CMAKE_C_COMPILER})
        add_test(NAME mvs_pcm_partial_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mvs_pcm_partial_tests.py
                ${CMAKE_C_COMPILER})
        foreach(UPLOAD_MODE IN ITEMS dirty legacy)
            add_executable(mvs_sprite_upload_${UPLOAD_MODE}_tests
                tests/mvs_sprite_upload_tests.c
                src/mvs/sprite.c
                src/mvs/sprite_common.c)
            target_include_directories(mvs_sprite_upload_${UPLOAD_MODE}_tests PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src)
            # The test follows the selected build capability. In no-cache builds it
            # verifies the direct full-resident sprite path instead.
            target_compile_definitions(mvs_sprite_upload_${UPLOAD_MODE}_tests PRIVATE
                USE_CACHE=$<BOOL:${USE_CACHE}>)
            if(UPLOAD_MODE STREQUAL "dirty")
                target_compile_definitions(mvs_sprite_upload_${UPLOAD_MODE}_tests PRIVATE
                    MVS_DIRTY_SPRITE_UPLOADS=1)
            endif()
            add_test(NAME mvs_sprite_upload_${UPLOAD_MODE}_tests
                COMMAND mvs_sprite_upload_${UPLOAD_MODE}_tests)
        endforeach()
    endif()
    if(${TARGET} STREQUAL "NCDZ")
        add_executable(resource_source_tests
            tests/resource_source_tests.c
            src/ncdz/resource_source.c
            src/common/zip_archive.c
        )
        target_include_directories(resource_source_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${MINIZ_INCLUDE_DIRS}
        )
        if(MINIZ_COMPILE_DEFINITIONS)
            target_compile_definitions(resource_source_tests PRIVATE
                ${MINIZ_COMPILE_DEFINITIONS}
            )
        endif()
        target_compile_definitions(resource_source_tests PRIVATE
            TEST_RESOURCE_ROOT="${CMAKE_CURRENT_BINARY_DIR}"
        )
        target_link_libraries(resource_source_tests PRIVATE
            "$<LINK_ONLY:${NJEMU_MINIZ_TARGET}>"
        )
        add_test(NAME resource_source_tests COMMAND resource_source_tests)
    endif()

    add_executable(zip_archive_tests
        tests/zip_archive_tests.c
        src/common/zip_archive.c
    )
    target_include_directories(zip_archive_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${MINIZ_INCLUDE_DIRS}
    )
    if(MINIZ_COMPILE_DEFINITIONS)
        target_compile_definitions(zip_archive_tests PRIVATE
            ${MINIZ_COMPILE_DEFINITIONS}
        )
    endif()
    target_compile_definitions(zip_archive_tests PRIVATE
        TEST_ZIP_ARCHIVE_PATH="${CMAKE_CURRENT_BINARY_DIR}/zip_archive_tests.zip"
        TEST_ZIP_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
    )
    target_link_libraries(zip_archive_tests PRIVATE
        "$<LINK_ONLY:${NJEMU_MINIZ_TARGET}>"
    )
    add_test(NAME zip_archive_tests COMMAND zip_archive_tests)

    add_executable(compression_compat_tests
        tests/compression_compat_tests.c
    )
    target_include_directories(compression_compat_tests PRIVATE
        ${MINIZ_INCLUDE_DIRS}
    )
    if(MINIZ_COMPILE_DEFINITIONS)
        target_compile_definitions(compression_compat_tests PRIVATE
            ${MINIZ_COMPILE_DEFINITIONS}
        )
    endif()
    target_link_libraries(compression_compat_tests PRIVATE
        "$<LINK_ONLY:${NJEMU_MINIZ_TARGET}>"
    )
    add_test(NAME compression_compat_tests COMMAND compression_compat_tests)

    add_executable(frame_pacing_tests
        tests/frame_pacing_tests.c
    )
    target_include_directories(frame_pacing_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME frame_pacing_tests COMMAND frame_pacing_tests)

    add_executable(ui_layout_tests
        tests/ui_layout_tests.c
        src/common/ui_layout.c
        src/common/video_driver.c
    )
    target_include_directories(ui_layout_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME ui_layout_tests COMMAND ui_layout_tests)

    add_executable(ui_texture_layout_tests
        tests/ui_texture_layout_tests.c
    )
    target_include_directories(ui_texture_layout_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME ui_texture_layout_tests COMMAND ui_texture_layout_tests)

    add_executable(ui_text_id_tests
        tests/ui_text_id_tests.c
    )
    target_include_directories(ui_text_id_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME ui_text_id_tests COMMAND ui_text_id_tests)

    add_executable(ui_text_catalog_tests
        tests/ui_text_catalog_tests.c
        src/common/ui_text_catalog.c
        src/common/ui_utf8.c
    )
    target_include_directories(ui_text_catalog_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(ui_text_catalog_tests PRIVATE
        TEST_TRANSLATION_ROOT="${CMAKE_BINARY_DIR}"
    )
    add_dependencies(ui_text_catalog_tests translation_packs)
    add_test(NAME ui_text_catalog_tests COMMAND ui_text_catalog_tests)

    add_executable(ui_utf8_tests
        tests/ui_utf8_tests.c
        src/common/ui_utf8.c
    )
    target_include_directories(ui_utf8_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME ui_utf8_tests COMMAND ui_utf8_tests)

    if(GUI)
        add_executable(font_runtime_tests
            tests/font_runtime_tests.c
            src/common/font/gbk_s14_runtime.c
        )
        target_include_directories(font_runtime_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_compile_definitions(font_runtime_tests PRIVATE
            TEST_FONT_ROOT="${CMAKE_BINARY_DIR}"
        )
        add_dependencies(font_runtime_tests font_assets)
        add_test(NAME font_runtime_tests COMMAND font_runtime_tests)
    endif()

		add_executable(platform_memory_info_tests
		tests/platform_memory_info_tests.c
		src/common/platform_memory_info.c
	)
	target_include_directories(platform_memory_info_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
	add_test(NAME platform_memory_info_tests COMMAND platform_memory_info_tests)

	add_executable(memory_plan_tests
		tests/memory_plan_tests.c
		src/common/memory_plan.c
		src/common/platform_memory_info.c
	)
	target_include_directories(memory_plan_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
	add_test(NAME memory_plan_tests COMMAND memory_plan_tests)

	foreach(RESERVE_KB IN ITEMS 1024 2048)
		add_executable(memory_reserve_${RESERVE_KB}_tests
			tests/memory_reserve_tests.c
			src/common/memory_plan.c
			src/common/platform_memory_info.c
		)
		target_include_directories(memory_reserve_${RESERVE_KB}_tests PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/src)
		target_compile_definitions(memory_reserve_${RESERVE_KB}_tests PRIVATE
			PS2_CACHE_RESERVE_KB=${RESERVE_KB})
		add_test(NAME memory_reserve_${RESERVE_KB}_tests
			COMMAND memory_reserve_${RESERVE_KB}_tests)
	endforeach()

	add_executable(palette_convert_tests
		tests/palette_convert_tests.c
	)
	target_include_directories(palette_convert_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
	add_test(NAME palette_convert_tests COMMAND palette_convert_tests)

	add_executable(input_state_tests
		tests/input_state_tests.c
		src/common/input_state.c
	)
	target_include_directories(input_state_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
	add_test(NAME input_state_tests COMMAND input_state_tests)

	add_executable(power_driver_tests
		tests/power_driver_tests.c
		src/common/power_driver.c
	)
	target_include_directories(power_driver_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
	add_test(NAME power_driver_tests COMMAND power_driver_tests)

	if(Python3_Interpreter_FOUND)
        add_test(
            NAME translation_source_validation
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_translations.py
        )
        add_test(
            NAME translation_pack_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/translation_pack_tests.py
        )
        add_test(
            NAME font_asset_validation
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_font_asset.py
                --source ${CMAKE_CURRENT_SOURCE_DIR}/src/common/font/gbk_s14.c
                --table-source ${CMAKE_CURRENT_SOURCE_DIR}/src/common/font/gbk_tbl.c
        )
    endif()
endif()

# ==============================================================================
# Install rules
# ==============================================================================

# Set default install prefix if not specified
# vita.toolchain.cmake presets the prefix to the SDK itself: treat that as the default too.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR
   (PLATFORM STREQUAL "PSVITA" AND CMAKE_INSTALL_PREFIX STREQUAL "${VITASDK}/arm-vita-eabi"))
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path prefix" FORCE)
endif()

# Install the executable
if(PLATFORM STREQUAL "PSP")
    install(FILES ${CMAKE_BINARY_DIR}/EBOOT.PBP DESTINATION .)
elseif(PLATFORM STREQUAL "PS2")
    if(PS2_EXTERNAL_IRX_IMAGE)
        install(PROGRAMS "$<TARGET_FILE:${TARGET}>" DESTINATION .)
        install(FILES "${PS2_DRIVERS_IRX_IMAGE}" DESTINATION .)
        install(PROGRAMS "${PS2_DRIVERS_IRXIMG_BOOTSTRAP}" DESTINATION . RENAME BOOT.ELF)
        install(CODE
            "file(WRITE \"\${CMAKE_INSTALL_PREFIX}/elf_path.ini\" \"${TARGET}\\n\")"
        )
    else()
        install(PROGRAMS "$<TARGET_FILE:${TARGET}>" DESTINATION .)
    endif()
elseif(PLATFORM STREQUAL "PSVITA")
    install(FILES ${CMAKE_BINARY_DIR}/${TARGET}.vpk DESTINATION .)
else()
    install(TARGETS ${TARGET} DESTINATION .)
endif()

# Install only the explicit distribution manifest. resources/<target> is also
# a local validation tree and may contain user ROMs, BIOS files, processed
# assets, caches and runtime-generated data that must never enter an install
# output by accident.
foreach(NJEMU_RESOURCE_FILE IN LISTS NJEMU_DISTRIBUTED_RESOURCE_FILES)
    install(FILES "${RESOURCE_DIR}${NJEMU_RESOURCE_FILE}"
        DESTINATION .
    )
endforeach()
install(FILES "${NJEMU_DEFAULT_GAME_SELECTOR}"
    DESTINATION .
)

foreach(NJEMU_RUNTIME_PLACEHOLDER IN LISTS NJEMU_RUNTIME_PLACEHOLDER_FILES)
    get_filename_component(NJEMU_RUNTIME_DIR "${NJEMU_RUNTIME_PLACEHOLDER}" DIRECTORY)
    get_filename_component(NJEMU_RUNTIME_DIR "${NJEMU_RUNTIME_DIR}" NAME)
    install(FILES "${NJEMU_RUNTIME_PLACEHOLDER}"
        DESTINATION "${NJEMU_RUNTIME_DIR}"
    )
endforeach()

# Translation packs are generated build artifacts shared by every platform.
# Keep them as a separate install component so packaging validation does not
# need to copy the user's potentially large ROM/cache resource tree.
install(FILES ${TRANSLATION_PACKS}
    DESTINATION lang
    COMPONENT translations
)
install(FILES "${GBK_FONT_ASSET}"
    DESTINATION font
    COMPONENT translations
)
