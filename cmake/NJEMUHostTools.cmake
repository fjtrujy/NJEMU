include(ExternalProject)

set(_NJEMU_HOST_TOOLS_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(NJEMU_HOST_C_COMPILER "" CACHE FILEPATH
    "Native C compiler used for NJEMU build-time host tools during cross-compilation")

function(njemu_enable_host_tools)
    if(TARGET njemu_host_tools)
        set(NJEMU_HOST_TOOL "${NJEMU_HOST_TOOL}" PARENT_SCOPE)
        set(NJEMU_HOST_TEST "${NJEMU_HOST_TEST}" PARENT_SCOPE)
        set(NJEMU_HOST_TOOL_TARGET njemu_host_tools PARENT_SCOPE)
        return()
    endif()

    set(_host_compiler "${NJEMU_HOST_C_COMPILER}")
    if(CMAKE_CROSSCOMPILING AND NOT _host_compiler)
        find_program(_detected_host_compiler
            NAMES cc clang gcc
            NO_CMAKE_FIND_ROOT_PATH)
        if(NOT _detected_host_compiler)
            message(FATAL_ERROR
                "Cross-building NJEMU requires a native host C compiler for build-time tools. "
                "Set NJEMU_HOST_C_COMPILER to a host compiler executable.")
        endif()
        set(_host_compiler "${_detected_host_compiler}")
    endif()

    if(CMAKE_HOST_WIN32)
        set(_host_executable_suffix ".exe")
    else()
        set(_host_executable_suffix "")
    endif()

    set(_host_root "${CMAKE_BINARY_DIR}/host-tools")
    set(_host_install "${_host_root}/install")
    set(_host_tool "${_host_install}/bin/njemu-tool${_host_executable_suffix}")
    set(_host_test "${_host_install}/bin/njemu-host-tests${_host_executable_suffix}")
    set(_host_cmake_args
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        "-DCMAKE_BUILD_TYPE=Release"
    )
    if(_host_compiler)
        list(APPEND _host_cmake_args "-DCMAKE_C_COMPILER=${_host_compiler}")
    endif()

    ExternalProject_Add(njemu_host_tools
        SOURCE_DIR "${_NJEMU_HOST_TOOLS_MODULE_DIR}/../tools/host"
        BINARY_DIR "${_host_root}/build"
        INSTALL_DIR "${_host_install}"
        CMAKE_ARGS ${_host_cmake_args}
        BUILD_BYPRODUCTS "${_host_tool}" "${_host_test}"
        UPDATE_COMMAND ""
        TEST_COMMAND ""
    )

    set(NJEMU_HOST_TOOL "${_host_tool}" CACHE INTERNAL "NJEMU native host tool executable")
    set(NJEMU_HOST_TEST "${_host_test}" CACHE INTERNAL "NJEMU native host-tool tests executable")
    set(NJEMU_HOST_TOOL_TARGET njemu_host_tools PARENT_SCOPE)
    set(NJEMU_HOST_TOOL "${_host_tool}" PARENT_SCOPE)
    set(NJEMU_HOST_TEST "${_host_test}" PARENT_SCOPE)
endfunction()
