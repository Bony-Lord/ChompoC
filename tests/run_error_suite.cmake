if(NOT DEFINED CHOMPO_EXECUTABLE)
    message(FATAL_ERROR "CHOMPO_EXECUTABLE is required")
endif()

if(NOT DEFINED TEST_TEMP_DIR)
    message(FATAL_ERROR "TEST_TEMP_DIR is required")
endif()

file(MAKE_DIRECTORY "${TEST_TEMP_DIR}")

function(run_error_case name source expected_fragment)
    set(source_file "${TEST_TEMP_DIR}/${name}.chmp")
    file(WRITE "${source_file}" "${source}")

    execute_process(
            COMMAND "${CHOMPO_EXECUTABLE}" "${source_file}"
            RESULT_VARIABLE actual_exit
            OUTPUT_VARIABLE actual_stdout
            ERROR_VARIABLE actual_stderr
    )

    string(REPLACE "\r\n" "\n" actual_stdout "${actual_stdout}")
    string(REPLACE "\r\n" "\n" actual_stderr "${actual_stderr}")
    string(FIND "${actual_stderr}" "${expected_fragment}" fragment_position)

    if(actual_exit EQUAL 0)
        message(FATAL_ERROR
                "${name}: expected failure, but program exited successfully.\n"
                "stdout:\n${actual_stdout}\n"
                "stderr:\n${actual_stderr}"
        )
    endif()

    if(fragment_position EQUAL -1)
        message(FATAL_ERROR
                "${name}: expected error fragment was not found.\n"
                "Expected: ${expected_fragment}\n"
                "stdout:\n${actual_stdout}\n"
                "stderr:\n${actual_stderr}"
        )
    endif()

    file(REMOVE "${source_file}")
    message(STATUS "${name}: passed")
endfunction()

run_error_case(for_in_invalid_iterable [=[
for (var value in 42) {
    print(value);
}
]=] [=[for-in requires array or string]=])

run_error_case(for_in_missing_var [=[
for (value in Array{1, 2}) {
    print(value);
}
]=] [=[expected 'var' after '(' in for-in loop]=])

run_error_case(for_in_scope [=[
for (var value in Array{1}) {
    print(value);
}

print(value);
]=] [=[undefined variable 'value']=])

run_error_case(function_arity [=[
fun add(left, right) {
    return left + right;
}

print(add(1));
]=] [=[expects 2 argument(s), got 1]=])

run_error_case(return_outside_function [=[
return 1;
]=] [=[cannot return from top-level code]=])

run_error_case(duplicate_variable [=[
{
    var value = 1;
    var value = 2;
}
]=] [=[is already declared in this scope]=])

run_error_case(index_out_of_range [=[
print(Array{1}[1]);
]=] [=[is out of range for sequence of size 1]=])

run_error_case(invalid_index_type [=[
print(Array{1}["0"]);
]=] [=[sequence index must be an integer]=])

run_error_case(string_element_type [=[
var text = "a";
text[0] = 1;
]=] [=[string element must be char]=])

run_error_case(division_by_zero [=[
print(10 / 0);
]=] [=[division by zero]=])

run_error_case(conversion_error [=[
print(Int("abc"));
]=] [=[cannot convert 'abc' to integer]=])

run_error_case(break_inside_for_function [=[
for (var value in Array{1}) {
    fun invalid() {
        break;
    }
}
]=] [=['break' can only be used inside a loop]=])

run_error_case(continue_inside_nested_function [=[
while (false) {
    fun invalid() {
        continue;
    }
}
]=] [=['continue' can only be used inside a loop]=])

run_error_case(cats_element_type [=[
print(CATS(Array{'a', 1}));
]=] [=[CATS requires an array of char]=])

run_error_case(char_range [=[
print(Char(256));
]=] [=[integer is outside the char range]=])

run_error_case(non_callable [=[
var value = 1;
value();
]=] [=[is not callable]=])
