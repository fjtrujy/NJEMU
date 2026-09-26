cmake_minimum_required(VERSION 3.12)
project(SystemButtons C ASM)

if(NOT COMMAND add_prx_module)
    message(FATAL_ERROR "SystemButtons requires the PSPDEV CMake toolchain")
endif()

add_prx_module(SystemButtons
    main.c
    sceCtrl_driver_371.S
    exports.exp
)

target_compile_options(SystemButtons PRIVATE
    -Os
    -G0
    -Wall
    -ffunction-sections
    -fdata-sections
)

target_link_options(SystemButtons PRIVATE
    -nostdlib
)

target_link_libraries(SystemButtons PRIVATE
    pspdebug
    pspdisplay_driver
    pspctrl_driver
    pspmodinfo
    pspsdk
    pspkernel
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SystemButtons.prx" DESTINATION .)
