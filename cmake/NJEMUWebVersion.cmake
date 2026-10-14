# Generate the web ROM converter's version from NJEMU's central Git identity.
# The converter shares the application version, not the cache format version.
if(NOT DEFINED NJEMU_WEB_VERSION_OUTPUT OR
    "${NJEMU_WEB_VERSION_OUTPUT}" STREQUAL "")
    message(FATAL_ERROR "NJEMU_WEB_VERSION_OUTPUT is required")
endif()
if(NOT DEFINED NJEMU_SOURCE_DIR OR "${NJEMU_SOURCE_DIR}" STREQUAL "")
    set(NJEMU_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/NJEMUVersion.cmake")
njemu_derive_version("${NJEMU_SOURCE_DIR}")
string(TIMESTAMP _build_date "%Y-%m-%d" UTC)
file(WRITE "${NJEMU_WEB_VERSION_OUTPUT}"
    "window.ROMCNV_VERSION = \"${NJEMU_DISPLAY_VERSION}\";\n"
    "window.ROMCNV_BUILD_DATE = \"${_build_date}\";\n")
message(STATUS "ROMCNV web version: ${NJEMU_DISPLAY_VERSION}")
