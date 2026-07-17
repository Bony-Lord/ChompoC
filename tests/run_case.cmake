execute_process(
        COMMAND
        "${CHOMPO_EXECUTABLE}"
        "${SOURCE_FILE}"
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr
)

file(READ
        "${EXPECTED_FILE}"
        expected_output)

if(NOT actual_exit EQUAL EXPECTED_EXIT_CODE)
    message(FATAL_ERROR
            "Wrong exit code.\n"
            "Expected: ${EXPECTED_EXIT_CODE}\n"
            "Actual: ${actual_exit}\n"
            "stdout:\n${actual_stdout}\n"
            "stderr:\n${actual_stderr}"
    )
endif()

if(USE_STDERR)
    set(actual_output "${actual_stderr}")
else()
    set(actual_output "${actual_stdout}")
endif()

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
            "Output mismatch.\n"
            "Expected:\n${expected_output}\n"
            "Actual:\n${actual_output}"
    )
endif()