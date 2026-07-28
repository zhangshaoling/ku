if(NOT DEFINED DAO_KU OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "check_compile_error.cmake requires DAO_KU, SOURCE, OUTPUT, and EXPECTED")
endif()

execute_process(
    COMMAND "${DAO_KU}" "${SOURCE}" "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(result EQUAL 0)
    message(FATAL_ERROR "dao-ku unexpectedly accepted ${SOURCE}")
endif()

string(CONCAT diagnostic "${stdout}" "${stderr}")
string(FIND "${diagnostic}" "${EXPECTED}" match)
if(match EQUAL -1)
    message(FATAL_ERROR "expected '${EXPECTED}', got '${diagnostic}'")
endif()
