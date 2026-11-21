if(NOT DEFINED DAO_KU OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_ku_check.cmake requires DAO_KU and SOURCE")
endif()

execute_process(
    COMMAND "${DAO_KU}" --check "${SOURCE}"
    RESULT_VARIABLE default_result
    OUTPUT_VARIABLE default_stdout
    ERROR_VARIABLE default_stderr)

if(NOT default_result STREQUAL "0")
    message(FATAL_ERROR
        "dao-ku --check failed (${default_result})\nstdout:\n${default_stdout}\nstderr:\n${default_stderr}")
endif()

string(CONCAT default_diagnostic "${default_stdout}" "${default_stderr}")
string(FIND "${default_diagnostic}" "checked ${SOURCE}" default_match)
if(default_match EQUAL -1)
    message(FATAL_ERROR "dao-ku --check did not report success: ${default_diagnostic}")
endif()

execute_process(
    COMMAND "${DAO_KU}" --check --recovery "${SOURCE}"
    RESULT_VARIABLE recovery_result
    OUTPUT_VARIABLE recovery_stdout
    ERROR_VARIABLE recovery_stderr)

if(NOT recovery_result STREQUAL "0")
    message(FATAL_ERROR
        "dao-ku --check --recovery failed (${recovery_result})\nstdout:\n${recovery_stdout}\nstderr:\n${recovery_stderr}")
endif()

string(CONCAT recovery_diagnostic "${recovery_stdout}" "${recovery_stderr}")
string(FIND "${recovery_diagnostic}" "checked ${SOURCE}" recovery_match)
if(recovery_match EQUAL -1)
    message(FATAL_ERROR "dao-ku --check --recovery did not report success: ${recovery_diagnostic}")
endif()
