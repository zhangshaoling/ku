if(NOT DEFINED DAO_KU OR NOT DEFINED SOURCE OR NOT DEFINED WORK)
    message(FATAL_ERROR "check_ku_fmt.cmake requires DAO_KU, SOURCE, and WORK")
endif()

execute_process(
    COMMAND "${DAO_KU}" --fmt "${SOURCE}"
    RESULT_VARIABLE first_result
    OUTPUT_FILE "${WORK}.once"
    ERROR_VARIABLE first_stderr)
if(NOT first_result STREQUAL "0")
    message(FATAL_ERROR "dao-ku --fmt failed (${first_result}): ${first_stderr}")
endif()

execute_process(
    COMMAND "${DAO_KU}" --fmt "${WORK}.once"
    RESULT_VARIABLE second_result
    OUTPUT_FILE "${WORK}.twice"
    ERROR_VARIABLE second_stderr)
if(NOT second_result STREQUAL "0")
    message(FATAL_ERROR "second dao-ku --fmt failed (${second_result}): ${second_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK}.once" "${WORK}.twice"
    RESULT_VARIABLE compare_result)
if(NOT compare_result STREQUAL "0")
    message(FATAL_ERROR "ku fmt is not idempotent")
endif()

execute_process(
    COMMAND "${DAO_KU}" --check "${WORK}.once"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_stdout
    ERROR_VARIABLE check_stderr)
if(NOT check_result STREQUAL "0")
    message(FATAL_ERROR "formatted source rejected by default checker:\n${check_stdout}\n${check_stderr}")
endif()

execute_process(
    COMMAND "${DAO_KU}" --check --recovery "${WORK}.once"
    RESULT_VARIABLE recovery_result
    OUTPUT_VARIABLE recovery_stdout
    ERROR_VARIABLE recovery_stderr)
if(NOT recovery_result STREQUAL "0")
    message(FATAL_ERROR "formatted source rejected by recovery checker:\n${recovery_stdout}\n${recovery_stderr}")
endif()
