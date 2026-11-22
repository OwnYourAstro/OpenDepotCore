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
