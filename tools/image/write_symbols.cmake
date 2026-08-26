if(NOT DEFINED NM OR NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "NM, INPUT, and OUTPUT are required.")
endif()
if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Cannot write symbols for missing ELF '${INPUT}'.")
endif()

get_filename_component(_output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
execute_process(
    COMMAND "${NM}" --defined-only --numeric-sort "${INPUT}"
    RESULT_VARIABLE _nm_result
    OUTPUT_FILE "${OUTPUT}"
    ERROR_VARIABLE _nm_error
)
if(NOT _nm_result EQUAL 0)
    file(REMOVE "${OUTPUT}")
    message(FATAL_ERROR "Symbol extraction failed for '${INPUT}': ${_nm_error}")
endif()
