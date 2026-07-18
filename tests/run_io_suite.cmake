if(NOT DEFINED CHOMPO_EXECUTABLE)
    message(FATAL_ERROR "CHOMPO_EXECUTABLE is required")
endif()
if(NOT DEFINED TEST_TEMP_DIR)
    message(FATAL_ERROR "TEST_TEMP_DIR is required")
endif()

file(REMOVE_RECURSE "${TEST_TEMP_DIR}")
file(MAKE_DIRECTORY "${TEST_TEMP_DIR}")

set(source_file "${TEST_TEMP_DIR}/io_streams.chmp")
set(stdin_file "${TEST_TEMP_DIR}/stdin.txt")
set(input_file "${TEST_TEMP_DIR}/input.txt")
set(output_file "${TEST_TEMP_DIR}/output.txt")
set(combined_output_file "${TEST_TEMP_DIR}/combined.txt")

file(WRITE "${stdin_file}" "standard-one\nstandard-two\n")
file(WRITE "${input_file}" "file-one\nfile-two\n")

file(WRITE "${source_file}" [=[
print(input(), "\n");

ostream("output.txt", "rewrite");
print("first");

ostream("output.txt", "append");
print("+second");

iostream("input.txt", "combined.txt", "create");
print(input(), "\n");
print(input(), "\n");
print(input(), "\n");

ostream("standart");
print("console\n");

istream("standart");
print(input(), "\n");
print(input(), "\n");
]=])

execute_process(
        COMMAND "${CHOMPO_EXECUTABLE}" "${source_file}"
        WORKING_DIRECTORY "${TEST_TEMP_DIR}"
        INPUT_FILE "${stdin_file}"
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr)

if(NOT actual_exit EQUAL 0)
    message(FATAL_ERROR "I/O suite failed with exit code ${actual_exit}.\nstdout:\n${actual_stdout}\nstderr:\n${actual_stderr}")
endif()

string(REPLACE "\r\n" "\n" actual_stdout "${actual_stdout}")
set(expected_stdout "standard-one\nconsole\nstandard-two\nNULL\n")
if(NOT actual_stdout STREQUAL expected_stdout)
    message(FATAL_ERROR "Unexpected standard output.\nExpected:\n${expected_stdout}\nActual:\n${actual_stdout}")
endif()

file(READ "${output_file}" actual_output_file)
if(NOT actual_output_file STREQUAL "first+second")
    message(FATAL_ERROR "rewrite/append output mismatch: ${actual_output_file}")
endif()

file(READ "${combined_output_file}" actual_combined_output)
string(REPLACE "\r\n" "\n" actual_combined_output "${actual_combined_output}")
set(expected_combined_output "file-one\nfile-two\nNULL\n")
if(NOT actual_combined_output STREQUAL expected_combined_output)
    message(FATAL_ERROR "iostream output mismatch.\nExpected:\n${expected_combined_output}\nActual:\n${actual_combined_output}")
endif()
