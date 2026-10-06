if(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
    set(_temp_base "$ENV{TMPDIR}")
elseif(DEFINED ENV{TEMP} AND NOT "$ENV{TEMP}" STREQUAL "")
    set(_temp_base "$ENV{TEMP}")
elseif(UNIX)
    set(_temp_base "/tmp")
else()
    set(_temp_base "${CMAKE_CURRENT_BINARY_DIR}")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _suffix)
set(_root "${_temp_base}/njemu-release-tests-${_suffix}")
set(_packager "${CMAKE_CURRENT_LIST_DIR}/NJEMUReleasePackage.cmake")
file(MAKE_DIRECTORY "${_root}/source")
file(WRITE "${_root}/source/README.md" "NJEMU\n")
file(WRITE "${_root}/source/Licence.txt" "GPL\n")

function(test_fail message_text)
    file(REMOVE_RECURSE "${_root}")
    message(FATAL_ERROR "release_packaging_tests: ${message_text}")
endfunction()

function(make_installs root platform version)
    foreach(_core IN ITEMS CPS1 CPS2 MVS NCDZ)
        set(_dir "${root}/${_core}")
        file(MAKE_DIRECTORY "${_dir}")
        file(WRITE "${_dir}/version.txt" "${version}\n")
        if(platform STREQUAL "psp")
            file(WRITE "${_dir}/EBOOT.PBP" "${_core}${platform}")
        elseif(platform STREQUAL "ps2")
            file(WRITE "${_dir}/${_core}.ELF" "${_core}${platform}")
        elseif(platform STREQUAL "psvita")
            file(WRITE "${_dir}/${_core}.vpk" "${_core}${platform}")
        else()
            file(WRITE "${_dir}/${_core}" "${_core}${platform}")
        endif()
        if(NOT platform STREQUAL "psvita")
            file(MAKE_DIRECTORY "${_dir}/lang" "${_dir}/roms")
            file(WRITE "${_dir}/lang/en.lng" "catalog")
            file(WRITE "${_dir}/roms/_placeholder" "")
        endif()
    endforeach()
endfunction()

function(run_package installs platform version output_dir result_var log_var)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DNJEMU_SOURCE_DIR=${_root}/source
            -DNJEMU_RELEASE_PLATFORM=${platform}
            -DNJEMU_RELEASE_VERSION=${version}
            -DNJEMU_INSTALL_CPS1=${installs}/CPS1
            -DNJEMU_INSTALL_CPS2=${installs}/CPS2
            -DNJEMU_INSTALL_MVS=${installs}/MVS
            -DNJEMU_INSTALL_NCDZ=${installs}/NCDZ
            -DNJEMU_RELEASE_OUTPUT_DIR=${output_dir}
            -P "${_packager}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    set(${result_var} "${_result}" PARENT_SCOPE)
    set(${log_var} "${_stdout}${_stderr}" PARENT_SCOPE)
endfunction()

function(assert_contains text needle context)
    string(FIND "${text}" "${needle}" _position)
    if(_position EQUAL -1)
        test_fail("${context}: missing '${needle}'")
    endif()
endfunction()

function(assert_not_contains text needle context)
    string(FIND "${text}" "${needle}" _position)
    if(NOT _position EQUAL -1)
        test_fail("${context}: unexpectedly contains '${needle}'")
    endif()
endfunction()

set(_psp_installs "${_root}/psp-installs")
make_installs("${_psp_installs}" psp 1.2.3)
set(_psp_output "${_root}/psp-output")
run_package("${_psp_installs}" psp 1.2.3 "${_psp_output}" _result _log)
if(NOT _result EQUAL 0)
    test_fail("PSP package failed: ${_log}")
endif()
set(_psp_archive "${_psp_output}/njemu-1.2.3-psp.zip")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${_psp_archive}"
    RESULT_VARIABLE _list_result
    OUTPUT_VARIABLE _listing
    ERROR_VARIABLE _list_error
)
if(NOT _list_result EQUAL 0)
    test_fail("could not list PSP archive: ${_list_error}")
endif()
assert_contains("${_listing}" "NJEMU/CPS1/EBOOT.PBP" "PSP archive")
assert_contains("${_listing}" "NJEMU/NCDZ/lang/en.lng" "PSP archive")
assert_contains("${_listing}" "NJEMU/release-manifest.json" "PSP archive")

set(_ps2_installs "${_root}/ps2-installs")
make_installs("${_ps2_installs}" ps2 1.2.3)
set(_ps2_output "${_root}/ps2-output")
run_package("${_ps2_installs}" ps2 1.2.3 "${_ps2_output}" _result _log)
if(NOT _result EQUAL 0)
    test_fail("PS2 package failed: ${_log}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${_ps2_output}/njemu-1.2.3-ps2.zip"
    OUTPUT_VARIABLE _ps2_listing
    RESULT_VARIABLE _list_result
)
if(NOT _list_result EQUAL 0)
    test_fail("could not list PS2 archive")
endif()
foreach(_core IN ITEMS CPS1 CPS2 MVS NCDZ)
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/${_core}.ELF" "PS2 archive")
endforeach()

set(_extract "${_root}/extract")
file(MAKE_DIRECTORY "${_extract}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${_psp_archive}"
    WORKING_DIRECTORY "${_extract}"
    RESULT_VARIABLE _extract_result
    ERROR_VARIABLE _extract_error
)
if(NOT _extract_result EQUAL 0)
    test_fail("could not extract PSP archive: ${_extract_error}")
endif()
file(READ "${_extract}/NJEMU/release-manifest.json" _manifest)
assert_contains("${_manifest}" "\"version\": \"1.2.3\"" "release manifest")
assert_contains("${_manifest}" "\"platform\": \"psp\"" "release manifest")
file(SHA256 "${_psp_archive}" _archive_sha)
file(SIZE "${_psp_archive}" _archive_size)
file(READ "${_psp_archive}.json" _sidecar)
assert_contains("${_sidecar}" "\"sha256\": \"${_archive_sha}\"" "release sidecar")
assert_contains("${_sidecar}" "\"size\": ${_archive_size}" "release sidecar")

set(_psp_output_second "${_root}/psp-output-second")
run_package("${_psp_installs}" psp 1.2.3 "${_psp_output_second}" _result _log)
if(NOT _result EQUAL 0)
    test_fail("second PSP package failed: ${_log}")
endif()
set(_extract_second "${_root}/extract-second")
file(MAKE_DIRECTORY "${_extract_second}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${_psp_output_second}/njemu-1.2.3-psp.zip"
    WORKING_DIRECTORY "${_extract_second}"
    RESULT_VARIABLE _extract_result
)
file(READ "${_extract_second}/NJEMU/release-manifest.json" _manifest_second)
if(NOT "${_manifest}" STREQUAL "${_manifest_second}")
    test_fail("release manifest is not reproducible")
endif()

set(_vita_installs "${_root}/vita-installs")
make_installs("${_vita_installs}" psvita 1.2.3)
set(_vita_output "${_root}/vita-output")
run_package("${_vita_installs}" psvita 1.2.3 "${_vita_output}" _result _log)
if(NOT _result EQUAL 0)
    test_fail("Vita package failed: ${_log}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${_vita_output}/njemu-1.2.3-psvita.zip"
    OUTPUT_VARIABLE _vita_listing
    RESULT_VARIABLE _list_result
)
if(NOT _list_result EQUAL 0)
    test_fail("could not list Vita archive")
endif()
foreach(_core IN ITEMS CPS1 CPS2 MVS NCDZ)
    assert_contains("${_vita_listing}" "NJEMU/${_core}.vpk" "Vita archive")
    assert_not_contains("${_vita_listing}" "NJEMU/${_core}/version.txt" "Vita archive")
endforeach()
assert_contains("${_vita_listing}" "NJEMU/version.txt" "Vita archive")

set(_bad_version_installs "${_root}/bad-version-installs")
make_installs("${_bad_version_installs}" desktop-linux 1.2.2)
run_package("${_bad_version_installs}" desktop-linux 1.2.3 "${_root}/bad-version-output" _result _log)
if(_result EQUAL 0)
    test_fail("version mismatch was accepted")
endif()
assert_contains("${_log}" "version mismatch" "version rejection")

file(WRITE "${_psp_installs}/MVS/roms/neogeo.zip" "bios")
run_package("${_psp_installs}" psp 1.2.3 "${_root}/runtime-output" _result _log)
if(_result EQUAL 0)
    test_fail("runtime payload was accepted")
endif()
if(NOT "${_log}" MATCHES "runtime data|ROM/cache")
    test_fail("runtime payload rejection did not explain the failure: ${_log}")
endif()

file(REMOVE_RECURSE "${_root}")
message(STATUS "release_packaging_tests: PASS")
