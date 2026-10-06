include("${CMAKE_CURRENT_LIST_DIR}/NJEMUVersion.cmake")

find_program(NJEMU_TEST_GIT NAMES git REQUIRED)

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
set(_root "${_temp_base}/njemu-version-tests-${_suffix}")
file(MAKE_DIRECTORY "${_root}")

function(test_fail message_text)
    file(REMOVE_RECURSE "${_root}")
    message(FATAL_ERROR "version_derivation_tests: ${message_text}")
endfunction()

function(test_git repo)
    execute_process(
        COMMAND "${NJEMU_TEST_GIT}" -C "${repo}" ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _result EQUAL 0)
        test_fail("git ${ARGN} failed: ${_error}")
    endif()
endfunction()

function(test_commit repo name)
    file(APPEND "${repo}/content.txt" "${name}\n")
    test_git("${repo}" add content.txt)
    test_git("${repo}" commit -m "${name}")
endfunction()

function(assert_equal actual expected context)
    if(NOT "${actual}" STREQUAL "${expected}")
        test_fail("${context}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

function(assert_matches actual pattern context)
    if(NOT "${actual}" MATCHES "${pattern}")
        test_fail("${context}: '${actual}' does not match '${pattern}'")
    endif()
endfunction()

unset(ENV{NJEMU_VERSION_OVERRIDE})
unset(NJEMU_VERSION_OVERRIDE)
unset(ENV{NJEMU_GIT_SHA_OVERRIDE})
unset(NJEMU_GIT_SHA_OVERRIDE)
unset(ENV{GITHUB_SHA})
set(_sha8 "[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]")

set(_archive "${_root}/archive")
file(MAKE_DIRECTORY "${_archive}")
njemu_derive_version("${_archive}")
assert_equal("${NJEMU_VERSION}" "2.4.0+source" "source archive version")
assert_equal("${NJEMU_DISPLAY_VERSION}" "2.4.0 (unknown)" "source archive display version")
assert_equal("${NJEMU_RELEASE_VERSION}" "2.4.0" "source archive release version")
assert_equal("${NJEMU_GIT_SHA}" "unknown" "source archive SHA")
assert_equal("${NJEMU_BUILD_NUMBER}" "unknown" "source archive build number")
assert_equal("${NJEMU_VERSION_SOURCE}" "archive" "source archive identity")

set(NJEMU_GIT_SHA_OVERRIDE "0123456789abcdef0123456789abcdef01234567")
njemu_derive_version("${_archive}")
assert_equal("${NJEMU_DISPLAY_VERSION}" "2.4.0 (01234567)" "source archive display version with SHA override")
assert_equal("${NJEMU_BUILD_NUMBER}" "01234567" "source archive overridden build number")
unset(NJEMU_GIT_SHA_OVERRIDE)

set(_repo "${_root}/repo")
file(MAKE_DIRECTORY "${_repo}")
test_git("${_repo}" init)
test_git("${_repo}" config user.name "NJEMU Test")
test_git("${_repo}" config user.email "njemu@example.invalid")
test_commit("${_repo}" initial)

njemu_derive_version("${_repo}")
assert_matches("${NJEMU_VERSION}" "^2\\.4\\.0-pre\\.1\\+g${_sha8}$" "no-tag version")
assert_matches("${NJEMU_DISPLAY_VERSION}" "^2\\.4\\.0 \\(${_sha8}\\)$" "no-tag display version")
assert_matches("${NJEMU_BUILD_NUMBER}" "^${_sha8}$" "no-tag build number")
assert_equal("${NJEMU_COMMITS_SINCE_TAG}" "1" "no-tag commit count")
assert_equal("${NJEMU_VERSION_EXACT_TAG}" "FALSE" "no-tag exact state")
assert_equal("${NJEMU_VERSION_SOURCE}" "git-no-semver-tag" "no-tag source")

test_git("${_repo}" tag v1.2.3)
njemu_derive_version("${_repo}")
assert_equal("${NJEMU_VERSION}" "1.2.3" "exact tag version")
assert_matches("${NJEMU_DISPLAY_VERSION}" "^1\\.2\\.3 \\(${_sha8}\\)$" "exact tag display version")
assert_equal("${NJEMU_RELEASE_VERSION}" "1.2.3" "exact tag release version")
assert_equal("${NJEMU_VERSION_MAJOR}" "1" "exact tag major")
assert_equal("${NJEMU_VERSION_MINOR}" "2" "exact tag minor")
assert_equal("${NJEMU_VERSION_PATCH}" "3" "exact tag patch")
assert_equal("${NJEMU_COMMITS_SINCE_TAG}" "0" "exact tag commit count")
assert_equal("${NJEMU_VERSION_EXACT_TAG}" "TRUE" "exact tag state")

test_commit("${_repo}" one)
test_commit("${_repo}" two)
njemu_derive_version("${_repo}")
assert_matches("${NJEMU_VERSION}" "^1\\.2\\.3\\+2\\.g${_sha8}$" "post-tag version")
assert_matches("${NJEMU_DISPLAY_VERSION}" "^1\\.2\\.3 \\(${_sha8}\\)$" "post-tag display version")
assert_equal("${NJEMU_COMMITS_SINCE_TAG}" "2" "post-tag commit count")

test_git("${_repo}" tag latest-test)
njemu_derive_version("${_repo}")
assert_matches("${NJEMU_VERSION}" "^1\\.2\\.3\\+2\\.g${_sha8}$" "non-SemVer tag")

file(WRITE "${_repo}/content.txt" "dirty\n")
file(WRITE "${_repo}/untracked.bin" "ignored for dirty identity")
njemu_derive_version("${_repo}")
assert_matches("${NJEMU_VERSION}" "^1\\.2\\.3\\+2\\.g${_sha8}\\.dirty$" "dirty version")
assert_matches("${NJEMU_DISPLAY_VERSION}" "^1\\.2\\.3 \\(${_sha8}\\)$" "dirty display version")
assert_equal("${NJEMU_VERSION_DIRTY}" "TRUE" "dirty state")

test_git("${_repo}" checkout -- content.txt)
njemu_derive_version("${_repo}")
assert_matches("${NJEMU_VERSION}" "^1\\.2\\.3\\+2\\.g${_sha8}$" "untracked-only version")
assert_equal("${NJEMU_VERSION_DIRTY}" "FALSE" "untracked-only dirty state")

set(NJEMU_VERSION_OVERRIDE "3.1.4-rc.2+ci.7")
njemu_derive_version("${_archive}")
assert_equal("${NJEMU_VERSION}" "3.1.4-rc.2+ci.7" "override version")
assert_equal("${NJEMU_DISPLAY_VERSION}" "3.1.4 (unknown)" "override display version without SHA")
assert_equal("${NJEMU_RELEASE_VERSION}" "3.1.4" "override release version")
assert_equal("${NJEMU_VERSION_MAJOR}" "3" "override major")
assert_equal("${NJEMU_VERSION_MINOR}" "1" "override minor")
assert_equal("${NJEMU_VERSION_PATCH}" "4" "override patch")
unset(NJEMU_VERSION_OVERRIDE)

set(NJEMU_VERSION_OVERRIDE "3.1.4-pre.27+g01234567")
set(NJEMU_GIT_SHA_OVERRIDE "0123456789abcdef0123456789abcdef01234567")
njemu_derive_version("${_archive}")
assert_equal("${NJEMU_VERSION}" "3.1.4-pre.27+g01234567" "container override version")
assert_equal("${NJEMU_DISPLAY_VERSION}" "3.1.4 (01234567)" "container override display version")
assert_equal("${NJEMU_RELEASE_VERSION}" "3.1.4" "container override release version")
assert_equal("${NJEMU_BUILD_NUMBER}" "01234567" "container override build number")
assert_equal("${NJEMU_VERSION_SOURCE}" "override" "container override identity")
unset(NJEMU_GIT_SHA_OVERRIDE)
unset(NJEMU_VERSION_OVERRIDE)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -DNJEMU_SOURCE_DIR=${_archive}
        -DNJEMU_VERSION_OVERRIDE=3.1
        -DNJEMU_VERSION_OUTPUT_FILE=${_root}/invalid.txt
        -P "${CMAKE_CURRENT_LIST_DIR}/NJEMUVersion.cmake"
    RESULT_VARIABLE _invalid_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(_invalid_result EQUAL 0)
    test_fail("invalid override was accepted")
endif()

file(REMOVE_RECURSE "${_root}")
message(STATUS "version_derivation_tests: PASS")
