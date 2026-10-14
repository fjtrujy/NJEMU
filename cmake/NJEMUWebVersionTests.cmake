set(_source "${CMAKE_CURRENT_LIST_DIR}/..")
set(_output "${CMAKE_CURRENT_BINARY_DIR}/njemu-web-version-tests.js")

include("${CMAKE_CURRENT_LIST_DIR}/NJEMUVersion.cmake")
njemu_derive_version("${_source}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DNJEMU_SOURCE_DIR=${_source}"
        "-DNJEMU_WEB_VERSION_OUTPUT=${_output}"
        -P "${CMAKE_CURRENT_LIST_DIR}/NJEMUWebVersion.cmake"
    RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "web version generation failed")
endif()

file(READ "${_output}" _generated)
string(FIND "${_generated}"
    "window.ROMCNV_VERSION = \"${NJEMU_DISPLAY_VERSION}\";"
    _match_position)
if(_match_position EQUAL -1)
    message(FATAL_ERROR "web converter version diverges from NJEMU's display version")
endif()
if(NOT "${_generated}" MATCHES
    "window\\.ROMCNV_BUILD_DATE = \"[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]\";")
    message(FATAL_ERROR "web converter build date missing")
endif()
file(REMOVE "${_output}")
message(STATUS "web_version_generation_tests: PASS")
