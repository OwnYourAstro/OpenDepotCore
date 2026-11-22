# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT

add_executable(opendepot_version_test version_test.cpp)
target_link_libraries(opendepot_version_test PRIVATE opendepot_core)
if(TARGET opendepot_c)
    target_link_libraries(opendepot_version_test PRIVATE opendepot_c)
endif()
add_test(NAME version COMMAND opendepot_version_test)

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
