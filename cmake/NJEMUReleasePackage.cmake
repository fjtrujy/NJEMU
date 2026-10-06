set(_cores CPS1 CPS2 MVS NCDZ)
set(_runtime_dirs roms cache processed state nvram memcard config cheats data)
set(_forbidden_files neocd.bin 000-lo.lo backup.bin njemu.ini command.dat)

foreach(_required IN ITEMS
        NJEMU_SOURCE_DIR
        NJEMU_RELEASE_PLATFORM
        NJEMU_RELEASE_VERSION
        NJEMU_RELEASE_OUTPUT_DIR
        NJEMU_INSTALL_CPS1
        NJEMU_INSTALL_CPS2
        NJEMU_INSTALL_MVS
        NJEMU_INSTALL_NCDZ)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "release packaging: ${_required} is required")
    endif()
endforeach()

if(NOT "${NJEMU_RELEASE_PLATFORM}" MATCHES "^[a-z0-9][a-z0-9-]*$")
    message(FATAL_ERROR "release packaging: invalid platform slug '${NJEMU_RELEASE_PLATFORM}'")
endif()
if(NOT "${NJEMU_RELEASE_VERSION}" MATCHES "^[0-9A-Za-z.+-]+$")
    message(FATAL_ERROR "release packaging: version is not safe for an archive filename: '${NJEMU_RELEASE_VERSION}'")
endif()

get_filename_component(NJEMU_SOURCE_DIR "${NJEMU_SOURCE_DIR}" ABSOLUTE)
get_filename_component(NJEMU_RELEASE_OUTPUT_DIR "${NJEMU_RELEASE_OUTPUT_DIR}" ABSOLUTE)
file(MAKE_DIRECTORY "${NJEMU_RELEASE_OUTPUT_DIR}")

function(_njemu_json_escape input output_var)
    set(_value "${input}")
    string(REPLACE "\\" "\\\\" _value "${_value}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\n" "\\n" _value "${_value}")
    string(REPLACE "\r" "\\r" _value "${_value}")
    string(REPLACE "\t" "\\t" _value "${_value}")
    set(${output_var} "${_value}" PARENT_SCOPE)
endfunction()

function(_njemu_validate_install core install_dir)
    if(NOT IS_DIRECTORY "${install_dir}")
        message(FATAL_ERROR "release packaging: ${core}: install tree does not exist: ${install_dir}")
    endif()
    if(NOT EXISTS "${install_dir}/version.txt")
        message(FATAL_ERROR "release packaging: ${core}: install tree is missing version.txt")
    endif()
    file(READ "${install_dir}/version.txt" _installed_version)
    string(STRIP "${_installed_version}" _installed_version)
    if(NOT "${_installed_version}" STREQUAL "${NJEMU_RELEASE_VERSION}")
        message(FATAL_ERROR
            "release packaging: ${core}: version mismatch: install has '${_installed_version}', expected '${NJEMU_RELEASE_VERSION}'")
    endif()

    if(NJEMU_RELEASE_PLATFORM STREQUAL "psp")
        set(_required_binary "EBOOT.PBP")
    elseif(NJEMU_RELEASE_PLATFORM STREQUAL "ps2")
        set(_required_binary "${core}.ELF")
    elseif(NJEMU_RELEASE_PLATFORM STREQUAL "psvita")
        set(_required_binary "${core}.vpk")
    else()
        set(_required_binary "${core}")
    endif()
    if(NOT EXISTS "${install_dir}/${_required_binary}" OR IS_DIRECTORY "${install_dir}/${_required_binary}")
        message(FATAL_ERROR
            "release packaging: ${core}: canonical executable/package is missing: ${_required_binary}")
    endif()

    file(GLOB_RECURSE _install_files
        LIST_DIRECTORIES false
        RELATIVE "${install_dir}"
        "${install_dir}/*")
    foreach(_relative IN LISTS _install_files)
        string(REPLACE "\\" "/" _relative "${_relative}")
        get_filename_component(_name "${_relative}" NAME)
        string(TOLOWER "${_name}" _lower_name)
        string(TOLOWER "${_relative}" _lower_relative)

        list(FIND _forbidden_files "${_lower_name}" _forbidden_index)
        if(NOT _forbidden_index EQUAL -1)
            message(FATAL_ERROR
                "release packaging: ${core}: user/runtime file must not be released: ${_relative}")
        endif()

        get_filename_component(_directory "${_lower_relative}" DIRECTORY)
        if(NOT "${_directory}" STREQUAL "" AND NOT "${_name}" STREQUAL "_placeholder")
            string(REPLACE "/" ";" _parts "${_directory}")
            foreach(_part IN LISTS _parts)
                list(FIND _runtime_dirs "${_part}" _runtime_index)
                if(NOT _runtime_index EQUAL -1)
                    message(FATAL_ERROR
                        "release packaging: ${core}: non-placeholder runtime data must not be released: ${_relative}")
                endif()
            endforeach()
        endif()
        if(_lower_name MATCHES "\\.(zip|cache)$")
            message(FATAL_ERROR
                "release packaging: ${core}: ROM/cache-like file must not be released: ${_relative}")
        endif()
        if(_lower_name MATCHES "^.+\\.sv[0-9]$")
            message(FATAL_ERROR
                "release packaging: ${core}: save state must not be released: ${_relative}")
        endif()
    endforeach()
endfunction()

foreach(_core IN LISTS _cores)
    _njemu_validate_install("${_core}" "${NJEMU_INSTALL_${_core}}")
endforeach()

set(_archive_name "njemu-${NJEMU_RELEASE_VERSION}-${NJEMU_RELEASE_PLATFORM}.zip")
set(_archive "${NJEMU_RELEASE_OUTPUT_DIR}/${_archive_name}")
set(_sidecar "${_archive}.json")
set(_stage "${NJEMU_RELEASE_OUTPUT_DIR}/.njemu-package-${NJEMU_RELEASE_PLATFORM}")
set(_root "${_stage}/NJEMU")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_root}")

set(_manifest_files "")
set(_manifest_file_count 0)
set(_archive_entries)

macro(_njemu_manifest_add relative_path)
    set(_full_path "${_stage}/${relative_path}")
    file(SIZE "${_full_path}" _file_size)
    file(SHA256 "${_full_path}" _file_sha)
    _njemu_json_escape("${relative_path}" _json_path)
    if(_manifest_file_count GREATER 0)
        string(APPEND _manifest_files ",\n")
    endif()
    string(APPEND _manifest_files
        "    {\n"
        "      \"path\": \"${_json_path}\",\n"
        "      \"sha256\": \"${_file_sha}\",\n"
        "      \"size\": ${_file_size}\n"
        "    }")
    math(EXPR _manifest_file_count "${_manifest_file_count} + 1")
    list(APPEND _archive_entries "${relative_path}")
endmacro()

foreach(_project_file IN ITEMS README.md Licence.txt)
    if(NOT EXISTS "${NJEMU_SOURCE_DIR}/${_project_file}")
        message(FATAL_ERROR "release packaging: project file is missing: ${NJEMU_SOURCE_DIR}/${_project_file}")
    endif()
    file(COPY "${NJEMU_SOURCE_DIR}/${_project_file}" DESTINATION "${_root}")
    _njemu_manifest_add("NJEMU/${_project_file}")
endforeach()

file(WRITE "${_root}/version.txt" "${NJEMU_RELEASE_VERSION}\n")
_njemu_manifest_add("NJEMU/version.txt")

foreach(_core IN LISTS _cores)
    set(_install "${NJEMU_INSTALL_${_core}}")
    if(NJEMU_RELEASE_PLATFORM STREQUAL "psvita")
        file(COPY "${_install}/${_core}.vpk" DESTINATION "${_root}")
        _njemu_manifest_add("NJEMU/${_core}.vpk")
    else()
        set(_core_root "${_root}/${_core}")
        file(MAKE_DIRECTORY "${_core_root}")
        file(COPY "${_install}/" DESTINATION "${_core_root}")
        file(GLOB_RECURSE _core_files
            LIST_DIRECTORIES false
            RELATIVE "${_core_root}"
            "${_core_root}/*")
        list(SORT _core_files)
        foreach(_relative IN LISTS _core_files)
            string(REPLACE "\\" "/" _relative "${_relative}")
            _njemu_manifest_add("NJEMU/${_core}/${_relative}")
        endforeach()
    endif()
endforeach()

_njemu_json_escape("${NJEMU_RELEASE_PLATFORM}" _json_platform)
_njemu_json_escape("${NJEMU_RELEASE_VERSION}" _json_version)
string(CONCAT _manifest
    "{\n"
    "  \"cores\": [\n"
    "    \"CPS1\",\n"
    "    \"CPS2\",\n"
    "    \"MVS\",\n"
    "    \"NCDZ\"\n"
    "  ],\n"
    "  \"files\": [\n"
    "${_manifest_files}\n"
    "  ],\n"
    "  \"name\": \"NJEMU\",\n"
    "  \"platform\": \"${_json_platform}\",\n"
    "  \"schema\": 1,\n"
    "  \"version\": \"${_json_version}\"\n"
    "}\n")
file(WRITE "${_root}/release-manifest.json" "${_manifest}")
list(APPEND _archive_entries "NJEMU/release-manifest.json")

file(REMOVE "${_archive}" "${_sidecar}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${_archive}"
        --format=zip
        --mtime=1980-01-01
        ${_archive_entries}
    WORKING_DIRECTORY "${_stage}"
    RESULT_VARIABLE _archive_result
    ERROR_VARIABLE _archive_error
)
if(NOT _archive_result EQUAL 0)
    message(FATAL_ERROR "release packaging: could not create ${_archive_name}: ${_archive_error}")
endif()

file(SIZE "${_archive}" _archive_size)
file(SHA256 "${_archive}" _archive_sha)
_njemu_json_escape("${_archive_name}" _json_archive_name)
string(CONCAT _sidecar_json
    "{\n"
    "  \"cores\": [\n"
    "    \"CPS1\",\n"
    "    \"CPS2\",\n"
    "    \"MVS\",\n"
    "    \"NCDZ\"\n"
    "  ],\n"
    "  \"name\": \"${_json_archive_name}\",\n"
    "  \"platform\": \"${_json_platform}\",\n"
    "  \"schema\": 1,\n"
    "  \"sha256\": \"${_archive_sha}\",\n"
    "  \"size\": ${_archive_size},\n"
    "  \"version\": \"${_json_version}\"\n"
    "}\n")
file(WRITE "${_sidecar}" "${_sidecar_json}")

file(REMOVE_RECURSE "${_stage}")
message(STATUS "release package: ${_archive}")
message(STATUS "sha256=${_archive_sha}")
