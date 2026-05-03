# Runs the oak interpreter on a single script, captures stdout, and diffs it
# against a .expected file. Fails the test if the interpreter exits non-zero
# or the output does not match.
#
# Required -D variables:
#   OAK_BIN  - path to the oak executable
#   SCRIPT   - path to the .oak source
#   EXPECTED - path to the matching .expected file
#   WORKDIR  - working directory in which to run oak

if(NOT OAK_BIN OR NOT SCRIPT OR NOT EXPECTED OR NOT WORKDIR)
    message(FATAL_ERROR
        "run_script_test.cmake: OAK_BIN, SCRIPT, EXPECTED and WORKDIR are required")
endif()

execute_process(
    COMMAND ${OAK_BIN} ${SCRIPT}
    WORKING_DIRECTORY ${WORKDIR}
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE  stderr
    RESULT_VARIABLE rc
)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR
        "oak exited with status ${rc}\n--- stdout ---\n${actual}\n--- stderr ---\n${stderr}")
endif()

file(READ ${EXPECTED} expected)

# Normalise trailing newlines on both sides so a final blank line difference
# never trips the comparison.
string(REGEX REPLACE "[\r\n]+$" "" actual   "${actual}")
string(REGEX REPLACE "[\r\n]+$" "" expected "${expected}")
# Also normalise CRLF -> LF for cross-platform safety.
string(REPLACE "\r\n" "\n" actual   "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")

if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR
        "Script output mismatch.\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()
