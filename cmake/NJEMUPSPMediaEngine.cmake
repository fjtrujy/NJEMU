# PSP Media Engine build policy and integration.
#
# PSP_ME_AUDIO is the only public capability switch. This module maps that
# switch to the validated target-specific implementation and keeps all ME-only
# dependencies, sources, flags, hardware oracles, and packaging isolated from
# non-PSP builds.

function(njemu_psp_me_declare_options)
    option(PSP_ME_AUDIO "PSP: enable Media Engine audio acceleration" OFF)
    option(PSP_AUDIO_PROFILE "PSP: log audio producer/chip/output timing" OFF)
    option(PSP_ME_SOUND_PROFILE "PSP MVS: log 68000/Z80/scheduler/sound-island timing" OFF)
    option(PSP_ME_RING_SELFTEST "PSP MVS: run the synthetic ME shared-ring self-test" OFF)

    # This was an intermediate bring-up switch. Keep command lines that still
    # provide it harmless, but never let it select a build mode.
    if(DEFINED PSP_ME_SOUND_COPROCESSOR)
        message(STATUS
            "PSP_ME_SOUND_COPROCESSOR is obsolete and ignored; "
            "PSP_ME_AUDIO now selects the target-specific ME implementation")
        unset(PSP_ME_SOUND_COPROCESSOR CACHE)
        unset(PSP_ME_SOUND_COPROCESSOR PARENT_SCOPE)
    endif()
endfunction()

function(njemu_psp_me_configure_policy)
    if(PSP_ME_AUDIO AND NOT PLATFORM STREQUAL "PSP")
        message(FATAL_ERROR "PSP_ME_AUDIO requires PLATFORM=PSP")
    endif()
    if(PSP_AUDIO_PROFILE AND NOT PLATFORM STREQUAL "PSP")
        message(FATAL_ERROR "PSP_AUDIO_PROFILE requires PLATFORM=PSP")
    endif()
    if(PSP_ME_SOUND_PROFILE AND
       NOT (PLATFORM STREQUAL "PSP" AND "${TARGET}" STREQUAL "MVS"))
        message(FATAL_ERROR "PSP_ME_SOUND_PROFILE requires PLATFORM=PSP and TARGET=MVS")
    endif()
    if(PSP_ME_RING_SELFTEST AND
       NOT (PLATFORM STREQUAL "PSP" AND "${TARGET}" STREQUAL "MVS" AND PSP_ME_AUDIO))
        message(FATAL_ERROR
            "PSP_ME_RING_SELFTEST requires PLATFORM=PSP, TARGET=MVS, and PSP_ME_AUDIO=ON")
    endif()

    set(NJEMU_PSP_ME_BOUNDED_JOBS OFF)
    set(NJEMU_PSP_ME_PERSISTENT_SOUND OFF)
    set(NJEMU_PSP_ME_AUDIO_MODE "CPU")

    if(PLATFORM STREQUAL "PSP" AND PSP_ME_AUDIO)
        if("${TARGET}" STREQUAL "CPS1")
            set(NJEMU_PSP_ME_BOUNDED_JOBS ON)
            set(NJEMU_PSP_ME_AUDIO_MODE "BOUNDED_JOBS")
        elseif("${TARGET}" STREQUAL "CPS2" OR
               "${TARGET}" STREQUAL "MVS" OR
               "${TARGET}" STREQUAL "NCDZ")
            set(NJEMU_PSP_ME_PERSISTENT_SOUND ON)
            set(NJEMU_PSP_ME_AUDIO_MODE "PERSISTENT_SOUND_OFFLOAD")
        else()
            message(FATAL_ERROR
                "PSP_ME_AUDIO has no validated implementation for TARGET=${TARGET}")
        endif()
    endif()

    if(PLATFORM STREQUAL "PSP")
        message(STATUS
            "PSP ME audio mode for ${TARGET}: ${NJEMU_PSP_ME_AUDIO_MODE}")
    endif()

    set(NJEMU_PSP_ME_BOUNDED_JOBS "${NJEMU_PSP_ME_BOUNDED_JOBS}" PARENT_SCOPE)
    set(NJEMU_PSP_ME_PERSISTENT_SOUND "${NJEMU_PSP_ME_PERSISTENT_SOUND}" PARENT_SCOPE)
    set(NJEMU_PSP_ME_AUDIO_MODE "${NJEMU_PSP_ME_AUDIO_MODE}" PARENT_SCOPE)
endfunction()

function(njemu_psp_me_append_sources source_variable)
    set(NJEMU_PSP_ME_SOURCES)

    if(PLATFORM STREQUAL "PSP" AND PSP_ME_AUDIO)
        list(APPEND NJEMU_PSP_ME_SOURCES
            psp/psp_audio_backend.h
            psp/psp_audio_producer.c
            psp/psp_me_dispatch.h
            psp/psp_me_dispatch.c
            psp/psp_me_spsc_ring.h
            psp/psp_me_spsc_ring.c
        )

        if(NJEMU_PSP_ME_BOUNDED_JOBS)
            list(APPEND NJEMU_PSP_ME_SOURCES
                psp/psp_audio_backend_jobs.c
                common/qsound_mix_job.h
                common/qsound_mix_job.c
                common/okim6295_job.h
                common/okim6295_job.c
            )
            add_definitions(-DAUDIO_PRODUCER_JOBS=1)
        elseif(NJEMU_PSP_ME_PERSISTENT_SOUND)
            if("${TARGET}" STREQUAL "CPS2")
                list(APPEND NJEMU_PSP_ME_SOURCES
                    psp/psp_audio_backend_cps2.c
                    common/cps2_sound_offload.h
                    psp/psp_cps2_me_sound.h
                    psp/psp_cps2_me_sound.c
                    psp/psp_me_qsound_worker.h
                    psp/psp_me_qsound_worker.c
                )
                set_source_files_properties(
                    src/psp/psp_me_qsound_worker.c
                    src/cpu/z80/cz80.c
                    src/sound/qsound.c
                    PROPERTIES COMPILE_OPTIONS "-G0;-fno-pic"
                )
            else()
                list(APPEND NJEMU_PSP_ME_SOURCES
                    psp/psp_audio_backend_neogeo.c
                    psp/psp_neogeo_me_sound.h
                    psp/psp_neogeo_me_sound.c
                    psp/psp_me_sound_worker.h
                    psp/psp_me_sound_worker.c
                )
                set_source_files_properties(
                    src/psp/psp_me_sound_worker.c
                    src/cpu/z80/cz80.c
                    src/sound/ym2610.c
                    PROPERTIES COMPILE_OPTIONS "-G0;-fno-pic"
                )
            endif()
            add_definitions(-DNJEMU_SOUND_OFFLOAD=1)
        endif()

        add_definitions(-DPSP_ME_AUDIO=1)

        # Functions entered directly by the Media Engine must not depend on the
        # Allegrex application's small-data/global-pointer convention.
        set_source_files_properties(
            src/psp/psp_me_dispatch.c
            src/psp/psp_me_spsc_ring.c
            src/psp/psp_me_spsc_ring_mist_test.c
            PROPERTIES COMPILE_OPTIONS "-G0;-fno-pic"
        )
        if(NJEMU_PSP_ME_BOUNDED_JOBS)
            set_source_files_properties(
                src/common/qsound_mix_job.c
                src/common/okim6295_job.c
                PROPERTIES COMPILE_OPTIONS "-G0;-fno-pic"
            )
        endif()
    else()
        list(APPEND NJEMU_PSP_ME_SOURCES common/audio_producer_binding_cpu.c)
    endif()

    if(PLATFORM STREQUAL "PSP" AND PSP_AUDIO_PROFILE)
        list(APPEND NJEMU_PSP_ME_SOURCES psp/psp_audio_profile.c)
        add_definitions(-DPSP_AUDIO_PROFILE=1)
    endif()

    if(PLATFORM STREQUAL "PSP" AND "${TARGET}" STREQUAL "MVS" AND
       PSP_ME_SOUND_PROFILE)
        list(APPEND NJEMU_PSP_ME_SOURCES psp/psp_me_sound_profile.c)
        add_definitions(
            -DNJEMU_SOUND_OFFLOAD_PROFILE=1
            -DPSP_ME_SOUND_PROFILE=1
        )
    endif()

    if(PSP_ME_RING_SELFTEST)
        list(APPEND NJEMU_PSP_ME_SOURCES
            psp/psp_me_spsc_ring_mist_test.h
            psp/psp_me_spsc_ring_mist_test.c
        )
        add_definitions(-DPSP_ME_RING_SELFTEST=1)
    endif()

    set(updated_sources "${${source_variable}}")
    list(APPEND updated_sources ${NJEMU_PSP_ME_SOURCES})
    set(${source_variable} "${updated_sources}" PARENT_SCOPE)
endfunction()

function(njemu_psp_me_configure_hardware_target target_name)
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_include_directories(${target_name} SYSTEM PRIVATE
        "${PSP_ME_CORE_INCLUDE_DIR}"
        "${PSP_ME_SAFE_TASK_INCLUDE_DIR}"
    )
    target_compile_options(${target_name} PRIVATE
        ${COMMON_FLAGS}
        ${WARNING_OPTIONS}
        -G0
        -fno-pic
    )
    target_link_libraries(${target_name} PRIVATE
        "${PSP_ME_SAFE_TASK_LIBRARY}"
        "${PSP_ME_CORE_MAPPER_LIBRARY}"
        pspkubridge
    )
endfunction()

function(njemu_psp_me_configure_target emulator_target)
    if(NOT (PLATFORM STREQUAL "PSP" AND PSP_ME_AUDIO))
        return()
    endif()

    find_path(PSP_ME_CORE_INCLUDE_DIR
        NAMES me-core-mapper/me-core-mapper.h
        HINTS "$ENV{PSPDEV}/psp/include"
    )
    find_path(PSP_ME_SAFE_TASK_INCLUDE_DIR
        NAMES me-safe-task/me-stask-mist.h
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

    target_include_directories(${emulator_target} SYSTEM PRIVATE
        "${PSP_ME_CORE_INCLUDE_DIR}"
        "${PSP_ME_SAFE_TASK_INCLUDE_DIR}"
    )
    target_link_libraries(${emulator_target} PRIVATE
        "${PSP_ME_SAFE_TASK_LIBRARY}"
        "${PSP_ME_CORE_MAPPER_LIBRARY}"
        pspkubridge
    )

    if(BUILD_TESTING AND NJEMU_PSP_ME_BOUNDED_JOBS)
        add_executable(psp_me_audio_jobs_hardware_test
            tests/psp_me_audio_jobs_hardware.c
            src/common/qsound_mix_job.c
            src/common/okim6295_job.c
            src/common/ym2610_adpcma_job.c
        )
        njemu_psp_me_configure_hardware_target(psp_me_audio_jobs_hardware_test)
    endif()

    if(BUILD_TESTING AND PSP_ME_RING_SELFTEST)
        add_executable(psp_me_ring_hardware_test
            tests/psp_me_spsc_ring_mist_hardware.c
            src/psp/psp_me_spsc_ring.c
            src/psp/psp_me_spsc_ring_mist_test.c
        )
        njemu_psp_me_configure_hardware_target(psp_me_ring_hardware_test)
    endif()

    if(BUILD_TESTING AND NJEMU_PSP_ME_PERSISTENT_SOUND AND
       "${TARGET}" STREQUAL "MVS")
        add_executable(psp_me_sound_worker_hardware_test
            tests/psp_me_sound_worker_hardware.c
            src/psp/psp_me_spsc_ring.c
            src/psp/psp_me_sound_worker.c
            src/cpu/z80/cz80.c
            src/sound/ym2610.c
        )
        njemu_psp_me_configure_hardware_target(psp_me_sound_worker_hardware_test)
    endif()

    if(BUILD_TESTING AND NJEMU_PSP_ME_PERSISTENT_SOUND AND
       "${TARGET}" STREQUAL "NCDZ")
        add_executable(psp_me_sound_worker_ncdz_hardware_test
            tests/psp_me_sound_worker_ncdz_hardware.c
            src/psp/psp_me_spsc_ring.c
            src/psp/psp_me_sound_worker.c
            src/cpu/z80/cz80.c
            src/sound/ym2610.c
        )
        njemu_psp_me_configure_hardware_target(psp_me_sound_worker_ncdz_hardware_test)
    endif()
endfunction()

function(njemu_psp_me_package_hardware_oracles)
    if(NOT PLATFORM STREQUAL "PSP")
        return()
    endif()

    if(BUILD_TESTING AND TARGET psp_me_audio_jobs_hardware_test)
        create_pbp_file(
            TARGET psp_me_audio_jobs_hardware_test
            BUILD_PRX
            OUTPUT_DIR "${CMAKE_BINARY_DIR}/psp_me_audio_jobs_hardware_package"
            TITLE "NJEMU PSP ME Audio Jobs Test"
            VERSION "${VERSION_MAJOR}.${VERSION_MINOR}"
            MEMSIZE 1
        )
    endif()

    if(BUILD_TESTING AND TARGET psp_me_ring_hardware_test)
        create_pbp_file(
            TARGET psp_me_ring_hardware_test
            BUILD_PRX
            OUTPUT_DIR "${CMAKE_BINARY_DIR}/psp_me_ring_hardware_package"
            TITLE "NJEMU PSP ME Ring Hardware Test"
            VERSION "${VERSION_MAJOR}.${VERSION_MINOR}"
            MEMSIZE 1
        )
    endif()

    if(BUILD_TESTING AND TARGET psp_me_sound_worker_hardware_test)
        create_pbp_file(
            TARGET psp_me_sound_worker_hardware_test
            BUILD_PRX
            OUTPUT_DIR "${CMAKE_BINARY_DIR}/psp_me_sound_worker_hardware_package"
            TITLE "NJEMU PSP ME Sound Worker Test"
            VERSION "${VERSION_MAJOR}.${VERSION_MINOR}"
            MEMSIZE 1
        )
    endif()

    if(BUILD_TESTING AND TARGET psp_me_sound_worker_ncdz_hardware_test)
        create_pbp_file(
            TARGET psp_me_sound_worker_ncdz_hardware_test
            BUILD_PRX
            OUTPUT_DIR "${CMAKE_BINARY_DIR}/psp_me_sound_worker_ncdz_hardware_package"
            TITLE "NJEMU PSP ME NCDZ Worker Test"
            VERSION "${VERSION_MAJOR}.${VERSION_MINOR}"
            MEMSIZE 1
        )
    endif()
endfunction()
