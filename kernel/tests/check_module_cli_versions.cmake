if(NOT DEFINED DAO_KU OR NOT DEFINED DAO_MODULE OR NOT DEFINED WORK)
    message(FATAL_ERROR "requires DAO_KU, DAO_MODULE and WORK")
endif()

file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/answer.ku" "思 main() { 返 42 }\n")
execute_process(
    COMMAND "${DAO_KU}" --recovery --store "${WORK}/store"
        --identity ku:agent/version-test 1.0.0 "${WORK}/answer.ku"
    RESULT_VARIABLE save_result ERROR_VARIABLE save_error)
if(NOT save_result STREQUAL "0")
    message(FATAL_ERROR "could not create version fixture: ${save_error}")
endif()

execute_process(
    COMMAND "${DAO_MODULE}" verify "${WORK}/store" ku:agent/version-test 1.0.0
    RESULT_VARIABLE valid_result ERROR_VARIABLE valid_error)
if(NOT valid_result STREQUAL "0")
    message(FATAL_ERROR "valid version rejected: ${valid_error}")
endif()

foreach(invalid_version IN ITEMS "1.0.0junk" "1junk.0.0" "1.0junk.0"
        "1.0.0.1" "1.0" "1..0" "1.0." "-1.0.0" "+1.0.0" " 1.0.0"
        "4294967296.0.0" "1.4294967296.0" "1.0.4294967296")
    execute_process(
        COMMAND "${DAO_MODULE}" verify "${WORK}/store" ku:agent/version-test "${invalid_version}"
        RESULT_VARIABLE invalid_result ERROR_VARIABLE invalid_error)
    if(invalid_result STREQUAL "0" OR NOT invalid_error MATCHES "invalid version")
        message(FATAL_ERROR "invalid version '${invalid_version}' was not rejected: ${invalid_error}")
    endif()
endforeach()
