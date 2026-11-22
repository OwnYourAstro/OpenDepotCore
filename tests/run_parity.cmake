# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT

if(NOT DEFINED CLI)
    message(FATAL_ERROR "CLI is not defined")
endif()
if(NOT DEFINED FIXTURE_DIR)
    message(FATAL_ERROR "FIXTURE_DIR is not defined")
endif()
if(NOT DEFINED EXPECTED_DIR)
    message(FATAL_ERROR "EXPECTED_DIR is not defined")
endif()

function(expect_output name)
    execute_process(
        COMMAND "${CLI}" ${ARGN}
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE error
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${name}: CLI exited ${result}\n${error}")
    endif()

    string(REPLACE "\r\n" "\n" actual "${actual}")
    file(READ "${EXPECTED_DIR}/${name}" expected)
    string(REPLACE "\r\n" "\n" expected "${expected}")

    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Output mismatch: ${name}")
    endif()
endfunction()

set(fixtures
    xts2500-800
    xts5000-800
    mt1500-800
    xts2500-edited
)
set(views
    verify.txt
    records.txt
    zones.txt
    report.txt
    report.json
    flashcode.txt
    dump.txt
    doctor.txt
)

foreach(fixture IN LISTS fixtures)
    set(cpg "${FIXTURE_DIR}/${fixture}.cpg")

    expect_output("${fixture}.verify.txt"    --verify "${cpg}")
    expect_output("${fixture}.records.txt"   --records "${cpg}")
    expect_output("${fixture}.zones.txt"     --zones "${cpg}")
    expect_output("${fixture}.report.txt"    --report "${cpg}")
    expect_output("${fixture}.report.json"   --report "${cpg}" --json)
    expect_output("${fixture}.flashcode.txt" --flashcode "${cpg}")
    expect_output("${fixture}.dump.txt"      --dump "${cpg}")
    expect_output("${fixture}.doctor.txt"    --doctor "${cpg}")
endforeach()

set(a "${FIXTURE_DIR}/xts2500-800.cpg")
set(b "${FIXTURE_DIR}/xts5000-800.cpg")
expect_output("diff-xts2500-xts5000.txt"  --diff "${a}" "${b}")
expect_output("diff-xts2500-xts5000.json" --diff "${a}" "${b}" --json)
expect_output("models.txt"                --models)

message(STATUS "CLI parity passed")
