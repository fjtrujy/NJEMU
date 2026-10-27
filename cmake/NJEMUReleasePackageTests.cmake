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
            if(_core STREQUAL "CPS2" OR _core STREQUAL "MVS")
                file(WRITE "${_dir}/BOOT.ELF" "bootstrap")
                file(WRITE "${_dir}/${_core}" "${_core}${platform}")
                file(WRITE "${_dir}/ps2_drivers.irximg" "drivers")
                file(WRITE "${_dir}/elf_path.ini" "${_core}\n")
            else()
                file(WRITE "${_dir}/${_core}.ELF" "${_core}${platform}")
            endif()
        elseif(platform STREQUAL "psvita")
            file(WRITE "${_dir}/${_core}.vpk" "${_core}${platform}")
        else()
            file(WRITE "${_dir}/${_core}" "${_core}${platform}")
        endif()
        if(NOT platform STREQUAL "psvita")
            file(MAKE_DIRECTORY "${_dir}/lang" "${_dir}/roms")
            file(WRITE "${_dir}/lang/en.lng" "catalog")
            file(WRITE "${_dir}/roms/_placeholder" "")
            if(_core STREQUAL "MVS")
                file(MAKE_DIRECTORY "${_dir}/processed")
                file(WRITE "${_dir}/processed/_placeholder" "")
            endif()
            if((platform STREQUAL "psp" OR platform STREQUAL "ps2") AND
               (_core STREQUAL "CPS2" OR _core STREQUAL "MVS"))
                file(MAKE_DIRECTORY "${_dir}/cache")
                file(WRITE "${_dir}/cache/_placeholder" "")
            endif()
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
foreach(_core IN ITEMS CPS2 MVS)
    assert_contains("${_listing}" "NJEMU/${_core}/cache/\n" "PSP cache directory entry")
    assert_contains("${_listing}" "NJEMU/${_core}/cache/_placeholder\n" "PSP cache placeholder")
endforeach()
assert_contains("${_listing}" "NJEMU/MVS/processed/\n" "PSP processed directory entry")
foreach(_core IN ITEMS CPS1 NCDZ)
    assert_not_contains("${_listing}" "NJEMU/${_core}/cache/" "PSP uncached core")
endforeach()
assert_not_contains("${_listing}" "NJEMU/CPS2/processed/" "PSP CPS2 processed directory")

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
foreach(_core IN ITEMS CPS1 NCDZ)
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/${_core}.ELF" "PS2 classic archive")
endforeach()
foreach(_core IN ITEMS CPS2 MVS)
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/BOOT.ELF" "PS2 external-IRX archive")
    assert_not_contains("${_ps2_listing}" "NJEMU/${_core}/BOOT_HDD.ELF" "PS2 obsolete HDD bootstrap")
    assert_not_contains("${_ps2_listing}" "NJEMU/${_core}/BOOT_ATA.ELF" "PS2 obsolete ATA bootstrap")
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/${_core}" "PS2 external-IRX archive")
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/ps2_drivers.irximg" "PS2 external-IRX archive")
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/elf_path.ini" "PS2 external-IRX archive")
endforeach()
# Missing the universal BOOT.ELF is a broken canonical external-IRX package.
file(REMOVE "${_ps2_installs}/MVS/BOOT.ELF")
run_package("${_ps2_installs}" ps2 1.2.3 "${_root}/missing-bootstrap-output" _result _log)
if(_result EQUAL 0)
    test_fail("missing canonical MVS BOOT.ELF was accepted")
endif()
assert_contains("${_log}" "external-IRX release file is missing: BOOT.ELF"
    "missing universal bootstrap rejection")
file(WRITE "${_ps2_installs}/MVS/BOOT.ELF" "bootstrap")

# Stale install trees must not accidentally publish a second device-specific ELF.
foreach(_obsolete IN ITEMS BOOT_HDD.ELF BOOT_ATA.ELF)
    file(WRITE "${_ps2_installs}/MVS/${_obsolete}" "obsolete-bootstrap")
    run_package("${_ps2_installs}" ps2 1.2.3 "${_root}/obsolete-bootstrap-output" _result _log)
    if(_result EQUAL 0)
        test_fail("obsolete MVS ${_obsolete} was accepted")
    endif()
    assert_contains("${_log}" "obsolete storage bootstrap must not be distributed:"
        "obsolete bootstrap rejection")
    file(REMOVE "${_ps2_installs}/MVS/${_obsolete}")
endforeach()
# Both external cores must ship the same universal executable.
file(WRITE "${_ps2_installs}/MVS/BOOT.ELF" "mismatched-bootstrap")
run_package("${_ps2_installs}" ps2 1.2.3 "${_root}/mismatched-bootstrap-output" _result _log)
if(_result EQUAL 0)
    test_fail("mismatched MVS/CPS2 BOOT.ELF was accepted")
endif()
assert_contains("${_log}" "BOOT.ELF binaries must be identical"
    "universal bootstrap identity rejection")
file(WRITE "${_ps2_installs}/MVS/BOOT.ELF" "bootstrap")
foreach(_core IN ITEMS CPS2 MVS)
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/cache/\n" "PS2 cache directory entry")
    assert_contains("${_ps2_listing}" "NJEMU/${_core}/cache/_placeholder\n" "PS2 cache placeholder")
endforeach()
assert_contains("${_ps2_listing}" "NJEMU/MVS/processed/\n" "PS2 canonical MVS processed directory")
foreach(_core IN ITEMS CPS1 NCDZ)
    assert_not_contains("${_ps2_listing}" "NJEMU/${_core}/cache/" "PS2 uncached core")
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
foreach(_core IN ITEMS CPS2 MVS)
    if(NOT EXISTS "${_extract}/NJEMU/${_core}/cache/_placeholder")
        test_fail("PSP extraction lost ${_core} cache directory")
    endif()
endforeach()
if(NOT EXISTS "${_extract}/NJEMU/MVS/processed/_placeholder")
    test_fail("PSP extraction lost MVS processed directory")
endif()
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

set(_desktop_installs "${_root}/desktop-installs")
make_installs("${_desktop_installs}" desktop-linux 1.2.3)
run_package("${_desktop_installs}" desktop-linux 1.2.3 "${_root}/desktop-output" _result _log)
if(NOT _result EQUAL 0)
    test_fail("Desktop package failed: ${_log}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${_root}/desktop-output/njemu-1.2.3-desktop-linux.zip"
    OUTPUT_VARIABLE _desktop_listing
    RESULT_VARIABLE _list_result
)
if(NOT _list_result EQUAL 0)
    test_fail("could not list Desktop archive")
endif()
assert_contains("${_desktop_listing}" "NJEMU/MVS/processed/\n" "Desktop MVS processed directory")
foreach(_core IN ITEMS CPS1 CPS2 MVS NCDZ)
    assert_not_contains("${_desktop_listing}" "NJEMU/${_core}/cache/" "Desktop uncached core")
endforeach()

# A stale or incomplete canonical install must fail, not ship the wrong tree.
file(REMOVE_RECURSE "${_psp_installs}/CPS2/cache")
run_package("${_psp_installs}" psp 1.2.3 "${_root}/missing-cache-output" _result _log)
if(_result EQUAL 0)
    test_fail("missing canonical CPS2 cache directory was accepted")
endif()
assert_contains("${_log}" "missing required cache/_placeholder" "missing cache rejection")
file(MAKE_DIRECTORY "${_psp_installs}/CPS2/cache")
file(WRITE "${_psp_installs}/CPS2/cache/_placeholder" "")
file(MAKE_DIRECTORY "${_psp_installs}/CPS1/cache")
file(WRITE "${_psp_installs}/CPS1/cache/_placeholder" "")
run_package("${_psp_installs}" psp 1.2.3 "${_root}/unexpected-cache-output" _result _log)
if(_result EQUAL 0)
    test_fail("unexpected CPS1 cache directory was accepted")
endif()
assert_contains("${_log}" "unexpected cache/" "unexpected cache rejection")
file(REMOVE_RECURSE "${_psp_installs}/CPS1/cache")

# Directory archive entries must not pull private dotfiles into the release.
file(WRITE "${_psp_installs}/MVS/cache/.private" "must not ship")
run_package("${_psp_installs}" psp 1.2.3 "${_root}/hidden-runtime-output" _result _log)
if(_result EQUAL 0)
    test_fail("hidden runtime file was included by directory recursion")
endif()
if(NOT "${_log}" MATCHES "unexpected runtime directory content|non-placeholder runtime data")
    test_fail("hidden runtime rejection did not explain the failure: ${_log}")
endif()
file(REMOVE "${_psp_installs}/MVS/cache/.private")

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
