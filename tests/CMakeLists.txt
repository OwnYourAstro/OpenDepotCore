# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT

add_executable(opendepot_version_test version_test.cpp)
target_link_libraries(opendepot_version_test PRIVATE opendepot_core)
if(TARGET opendepot_c)
    target_link_libraries(opendepot_version_test PRIVATE opendepot_c)
endif()
add_test(NAME version COMMAND opendepot_version_test)

add_executable(opendepot_crypto_test crypto_test.cpp)
target_link_libraries(opendepot_crypto_test PRIVATE opendepot_core)
add_test(NAME crypto COMMAND opendepot_crypto_test)

add_executable(opendepot_cfb_test cfb_test.cpp)
target_link_libraries(opendepot_cfb_test PRIVATE opendepot_core)
target_include_directories(opendepot_cfb_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
if(WIN32)
    target_link_libraries(opendepot_cfb_test PRIVATE ole32)
endif()
add_test(NAME cfb_xts2500
    COMMAND opendepot_cfb_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xts2500-800.cpg)
add_test(NAME cfb_xts5000
    COMMAND opendepot_cfb_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xts5000-800.cpg)

if(TARGET opendepot_c)
    add_executable(opendepot_capi_test capi_test.cpp)
    target_link_libraries(opendepot_capi_test PRIVATE opendepot_c)
    set(OPENDEPOT_TEST_PROFILE_DB
        "${CMAKE_CURRENT_SOURCE_DIR}/../../OpenDepot/data/depot_profiles.bin")
    if(EXISTS "${OPENDEPOT_TEST_PROFILE_DB}")
        add_test(NAME capi
            COMMAND opendepot_capi_test
                ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xts2500-800.cpg
                ${OPENDEPOT_TEST_PROFILE_DB})
    else()
        add_test(NAME capi
            COMMAND opendepot_capi_test
                ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xts2500-800.cpg)
    endif()
endif()

add_executable(opendepot_conventional_test conventional_test.cpp)
target_link_libraries(opendepot_conventional_test PRIVATE opendepot_core)
add_test(NAME conventional
    COMMAND opendepot_conventional_test
        ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/h46-conv-clean.cpg)

add_executable(opendepot_validate_test validate_test.cpp)
target_link_libraries(opendepot_validate_test PRIVATE opendepot_core)
add_test(NAME validate
    COMMAND opendepot_validate_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xts2500-800.cpg)

add_executable(opendepot_model_test model_test.cpp)
target_link_libraries(opendepot_model_test PRIVATE opendepot_core)
add_test(NAME model
    COMMAND opendepot_model_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/xts2500-800.cpg)

add_test(
    NAME fixtures
    COMMAND ${CMAKE_COMMAND}
        -DFIXTURE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_fixtures.cmake
)

if(TARGET opendepot-cli)
    add_test(
        NAME parity
        COMMAND ${CMAKE_COMMAND}
            "-DCLI=$<TARGET_FILE:opendepot-cli>"
            -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures
            -DEXPECTED_DIR=${CMAKE_CURRENT_SOURCE_DIR}/expected
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_parity.cmake
    )
endif()
