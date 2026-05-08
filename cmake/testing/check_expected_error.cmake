execute_process(
    COMMAND "${OAK_BIN}" "${SCRIPT}"
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_QUIET
    ERROR_VARIABLE actual
    RESULT_VARIABLE result)

if(result EQUAL 0)
    message(FATAL_ERROR "Expected oak to fail")
endif()

file(READ "${EXPECTED_ERR}" expected)
string(STRIP "${expected}" expected)
string(FIND "${actual}" "${expected}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Expected error output to contain: '${expected}'\n${actual}")
endif()
