if(NOT DEFINED DAO_KU OR NOT DEFINED SOURCE OR NOT DEFINED WORK)
    message(FATAL_ERROR "check_ku_fmt_cli.cmake requires DAO_KU, SOURCE, and WORK")
endif()

file(TO_CMAKE_PATH "${DAO_KU}" DAO_KU_CMAKE)

function(expect_failure label)
    set(command_args ${ARGN})
    execute_process(
        COMMAND "${DAO_KU_CMAKE}" ${command_args}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr)
    if(result MATCHES "no such file")
        message(FATAL_ERROR "${label}: cannot execute ${DAO_KU_CMAKE}")
    endif()
    if(result EQUAL 0)
        message(FATAL_ERROR "${label}: dao-ku unexpectedly succeeded")
    endif()
    string(CONCAT diagnostic "${command_stdout}" "${command_stderr}")
    string(FIND "${diagnostic}" "usage: dao-ku --fmt [--recovery] <input.ku>" match)
    if(match EQUAL -1)
        message(FATAL_ERROR "${label}: missing fmt usage diagnostic: ${diagnostic}")
    endif()
endfunction()

expect_failure("missing input" --fmt)
expect_failure("extra output" --fmt "${SOURCE}" "${WORK}.dao")
expect_failure("check conflict" --fmt --check "${SOURCE}")
expect_failure("identity conflict" --fmt --identity ku:test 1.0.0 "${SOURCE}")
expect_failure("store conflict" --fmt --store "${WORK}.store" "${SOURCE}")
expect_failure("parent conflict" --fmt --parent ku:parent 1.0.0 "${SOURCE}")
