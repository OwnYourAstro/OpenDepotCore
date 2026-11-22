if(NOT DEFINED FIXTURE_ROOT)
    message(FATAL_ERROR "FIXTURE_ROOT is not defined")
endif()

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
    if(NOT EXISTS "${FIXTURE_ROOT}/fixtures/${fixture}.cpg")
        message(FATAL_ERROR "Missing fixture: ${fixture}.cpg")
    endif()
    foreach(view IN LISTS views)
        if(NOT EXISTS "${FIXTURE_ROOT}/expected/${fixture}.${view}")
            message(FATAL_ERROR "Missing expected output: ${fixture}.${view}")
        endif()
    endforeach()
endforeach()

foreach(extra diff-xts2500-xts5000.txt diff-xts2500-xts5000.json models.txt)
    if(NOT EXISTS "${FIXTURE_ROOT}/expected/${extra}")
        message(FATAL_ERROR "Missing expected output: ${extra}")
    endif()
endforeach()

message(STATUS "Fixture set present")
