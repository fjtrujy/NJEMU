# Experimental R5900 LLVM toolchain for NJEMU PS2 release builds.
# Only NJEMU translation units use Clang. The linker, PS2SDK, gsKit, and
# ps2_drivers must remain GCC-built. The normal PS2 CI toolchain is unchanged.
#
# Prerequisites:
#   PS2DEV, PS2SDK, and a preinstalled $PS2DEV/llvm/bin/clang.
#   NJEMU_PS2_LLVM_AS_DIR containing a symlink named 'as' to the
#   GCC R5900 assembler, created by the local release wrapper.
# This is intentionally opt-in until LLVM is integrated into PS2 CI.
cmake_minimum_required(VERSION 3.0...3.12)

if(NOT DEFINED ENV{PS2DEV} OR NOT DEFINED ENV{PS2SDK})
    message(FATAL_ERROR "PS2DEV and PS2SDK must be set for the R5900 LLVM toolchain")
endif()
set(PS2DEV "$ENV{PS2DEV}")
set(PS2SDK "$ENV{PS2SDK}")
set(_llvm_bin "${PS2DEV}/llvm/bin")
set(_gnu_bin "${PS2DEV}/ee/bin")
set(_gnu_prefix "mips64r5900el-ps2-elf")

foreach(_required IN ITEMS
        "${_llvm_bin}/clang" "${_llvm_bin}/clang++"
        "${_gnu_bin}/${_gnu_prefix}-gcc"
        "${_gnu_bin}/${_gnu_prefix}-g++"
        "${_gnu_bin}/${_gnu_prefix}-as")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Missing LLVM/PS2 toolchain executable: ${_required}")
    endif()
endforeach()

if(NOT DEFINED ENV{NJEMU_PS2_LLVM_AS_DIR}
   OR NOT EXISTS "$ENV{NJEMU_PS2_LLVM_AS_DIR}/as")
    message(FATAL_ERROR
        "NJEMU_PS2_LLVM_AS_DIR must point to a directory containing an 'as' symlink to the PS2 GNU assembler")
endif()
set(_assembler_dir "$ENV{NJEMU_PS2_LLVM_AS_DIR}")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR mips)
set(CMAKE_C_COMPILER "${_llvm_bin}/clang")
set(CMAKE_CXX_COMPILER "${_llvm_bin}/clang++")
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

# The EE archives/startup files are built with GCC, so use the GCC driver
# for the final link. The compiler and linker are deliberately different.
set(CMAKE_C_LINK_EXECUTABLE
    "${_gnu_bin}/${_gnu_prefix}-gcc <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE
    "${_gnu_bin}/${_gnu_prefix}-g++ <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir
    OUTPUT_VARIABLE _clang_resource
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _clang_resource_result)
execute_process(
    COMMAND "${_gnu_bin}/${_gnu_prefix}-gcc" -print-file-name=include
    OUTPUT_VARIABLE _gcc_include
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _gcc_include_result)
if(NOT _clang_resource_result EQUAL 0 OR
   NOT EXISTS "${_clang_resource}/include" OR
   NOT _gcc_include_result EQUAL 0 OR
   NOT EXISTS "${_gcc_include}")
    message(FATAL_ERROR "Could not identify LLVM/GCC builtin include directories")
endif()

set(_ee_target "--target=mips64el -mcpu=r5900 -mabi=n32 -fno-pic -mno-abicalls -G0 -mno-gpopt -fomit-frame-pointer")
set(_ee_includes
    "-isystem ${_clang_resource}/include -I${PS2DEV}/ee/${_gnu_prefix}/include -I${_gcc_include}")
set(EE_CFLAGS
    "${_ee_target} ${_ee_includes} -I${PS2SDK}/ee/include -I${PS2SDK}/common/include -I${PS2DEV}/gsKit/include -I${PS2SDK}/ports/include -D_EE -DPS2 -D__PS2__ -O3 -ffast-math -Wall -fno-lto -Wno-logical-not-parentheses -fno-integrated-as -B${_assembler_dir} -fno-addrsig"
    CACHE STRING "LLVM R5900 EE compiler flags" FORCE)
set(EE_LDFLAGS
    "-L${PS2SDK}/ee/lib -L${PS2DEV}/gsKit/lib -L${PS2SDK}/ports/lib -Wl,-zmax-page-size=128 -Wl,--no-warn-mismatch -T${PS2SDK}/ee/startup/linkfile"
    CACHE STRING "EE linker flags" FORCE)

set(CMAKE_TARGET_INSTALL_PREFIX "${PS2DEV}/ports")
set(CMAKE_FIND_ROOT_PATH "${PS2DEV}" "${PS2DEV}/ee" "${PS2DEV}/ee/ee"
    "${PS2SDK}" "${PS2SDK}/ports")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

set(CMAKE_C_FLAGS_INIT "${EE_CFLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${EE_CFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${EE_LDFLAGS}")
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-nostartfiles -Wl,-r -Wl,-d")

find_program(PKG_CONFIG_EXECUTABLE
    NAMES ${_gnu_prefix}-pkg-config
    HINTS "${PS2SDK}/bin")
if(NOT PKG_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "Could not find ${_gnu_prefix}-pkg-config")
endif()

set(PS2 TRUE)
set(PLATFORM_PS2 TRUE)
set(EE TRUE)
# LLVM bitcode LTO is not compatible with the GCC linker used for the SDK.
# Do not affect the existing GCC/PSP LTO release policy.
set(NJEMU_PS2_LLVM TRUE CACHE BOOL "Compile NJEMU with Clang and link with PS2 GCC" FORCE)
