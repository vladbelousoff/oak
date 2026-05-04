# Runs the oak interpreter on a script that is expected to fail (non-zero exit)
# and checks that the combined stderr output contains a given substring.
#
# Required -D variables:
#   OAK_BIN       - path to the oak executable
#   SCRIPT        - path to the .oak source
#   EXPECTED_ERR  - path to a file whose single line is the expected substring
#   WORKDIR       - working directory in which to run oak

if(NOT OAK_BIN OR NOT SCRIPT OR NOT EXPECTED_ERR OR NOT WORKDIR)
    message(FATAL_ERROR
        "run_error_script_test.cmake: OAK_BIN, SCRIPT, EXPECTED_ERR and WORKDIR are required")
endif()

execute_process(
    COMMAND ${OAK_BIN} ${SCRIPT}
    WORKING_DIRECTORY ${WORKDIR}
    OUTPUT_VARIABLE stdout_var
    ERROR_VARIABLE  stderr_var
    RESULT_VARIABLE rc
)

if(rc EQUAL 0)
    message(FATAL_ERROR
        "Expected oak to exit non-zero, but it succeeded.\n--- stdout ---\n${stdout_var}")
endif()

file(READ ${EXPECTED_ERR} expected_substr)
string(STRIP "${expected_substr}" expected_substr)

set(combined "${stdout_var}${stderr_var}")
string(FIND "${combined}" "${expected_substr}" found_pos)
if(found_pos EQUAL -1)
    message(FATAL_ERROR
        "Expected error output to contain: '${expected_substr}'\n--- actual stderr ---\n${stderr_var}\n--- actual stdout ---\n${stdout_var}")
endif()
