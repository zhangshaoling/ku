if(NOT DEFINED DAO_KU OR NOT DEFINED SOURCE OR
   NOT DEFINED DEFAULT_OUTPUT OR NOT DEFINED RECOVERY_OUTPUT)
    message(FATAL_ERROR
        "check_ku_canonical_identity.cmake requires DAO_KU, SOURCE, DEFAULT_OUTPUT, and RECOVERY_OUTPUT")
endif()

set(identity_args)
if(DEFINED IDENTITY_NAME OR DEFINED IDENTITY_VERSION)
    if(NOT DEFINED IDENTITY_NAME OR NOT DEFINED IDENTITY_VERSION)
        message(FATAL_ERROR "IDENTITY_NAME and IDENTITY_VERSION must be provided together")
    endif()
    list(APPEND identity_args --identity "${IDENTITY_NAME}" "${IDENTITY_VERSION}")
endif()

execute_process(
    COMMAND "${DAO_KU}" ${identity_args} "${SOURCE}" "${DEFAULT_OUTPUT}"
    RESULT_VARIABLE default_result
    OUTPUT_VARIABLE default_stdout
    ERROR_VARIABLE default_stderr)

if(NOT default_result STREQUAL "0")
    message(FATAL_ERROR
        "dao-ku default path failed (${default_result})\nstdout:\n${default_stdout}\nstderr:\n${default_stderr}")
endif()

execute_process(
    COMMAND "${DAO_KU}" --recovery ${identity_args} "${SOURCE}" "${RECOVERY_OUTPUT}"
    RESULT_VARIABLE recovery_result
    OUTPUT_VARIABLE recovery_stdout
    ERROR_VARIABLE recovery_stderr)

if(NOT recovery_result STREQUAL "0")
    message(FATAL_ERROR
        "dao-ku --recovery failed (${recovery_result})\nstdout:\n${recovery_stdout}\nstderr:\n${recovery_stderr}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${DEFAULT_OUTPUT}" "${RECOVERY_OUTPUT}"
    RESULT_VARIABLE compare_result)

if(NOT compare_result STREQUAL "0")
    message(FATAL_ERROR "canonical identity mismatch")
endif()
