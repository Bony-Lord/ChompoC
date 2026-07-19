if(NOT DEFINED CHOMPO_EXECUTABLE)
    message(FATAL_ERROR "CHOMPO_EXECUTABLE is required")
endif()
if(NOT DEFINED TEST_TEMP_DIR)
    message(FATAL_ERROR "TEST_TEMP_DIR is required")
endif()

file(REMOVE_RECURSE "${TEST_TEMP_DIR}")
file(MAKE_DIRECTORY "${TEST_TEMP_DIR}")
set(source_file "${TEST_TEMP_DIR}/system_args.chmp")

file(WRITE "${source_file}" [=[
var values = args();
print(len(values), "\n");
for (var value in values)
    print(value, "\n");
]=])

execute_process(
        COMMAND "${CHOMPO_EXECUTABLE}" "${source_file}" alpha 42 omega
        WORKING_DIRECTORY "${TEST_TEMP_DIR}"
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr)

if(NOT actual_exit EQUAL 0)
    message(FATAL_ERROR "args suite failed with exit code ${actual_exit}.\nstdout:\n${actual_stdout}\nstderr:\n${actual_stderr}")
endif()

string(REPLACE "\r\n" "\n" actual_stdout "${actual_stdout}")
set(expected_stdout "3\nalpha\n42\nomega\n")
if(NOT actual_stdout STREQUAL expected_stdout)
    message(FATAL_ERROR "Unexpected args output.\nExpected:\n${expected_stdout}\nActual:\n${actual_stdout}")
endif()
