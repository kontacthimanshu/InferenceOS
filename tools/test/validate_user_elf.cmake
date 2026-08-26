if(NOT DEFINED APPLICATION OR NOT EXISTS "${APPLICATION}")
    message(FATAL_ERROR "APPLICATION must name an existing user ELF image.")
endif()
if(NOT DEFINED READELF OR NOT EXISTS "${READELF}")
    message(FATAL_ERROR "READELF must name the pinned ELF inspection tool.")
endif()
if(NOT DEFINED NM OR NOT EXISTS "${NM}")
    message(FATAL_ERROR "NM must name the pinned symbol inspection tool.")
endif()

execute_process(
    COMMAND "${READELF}" -h -lW -d "${APPLICATION}"
    RESULT_VARIABLE readelf_result
    OUTPUT_VARIABLE readelf_output
    ERROR_VARIABLE readelf_error
)
if(NOT readelf_result EQUAL 0)
    message(FATAL_ERROR "readelf rejected ${APPLICATION}: ${readelf_error}")
endif()
if(NOT readelf_output MATCHES "Class:[ \t]+ELF64"
   OR NOT readelf_output MATCHES "Type:[ \t]+EXEC"
   OR NOT readelf_output MATCHES "Machine:[ \t]+Advanced Micro Devices X86-64")
    message(FATAL_ERROR "${APPLICATION} is not a static executable x86-64 ELF image.")
endif()
if(readelf_output MATCHES "[ \t](INTERP|DYNAMIC)[ \t]")
    message(FATAL_ERROR "${APPLICATION} contains a dynamic-loader program header.")
endif()
string(REGEX MATCH "Entry point address:[ \t]+(0x[0-9A-Fa-f]+)" entry_match "${readelf_output}")
if(NOT entry_match)
    message(FATAL_ERROR "${APPLICATION} has no parseable ELF entry point.")
endif()
set(entry_address "${CMAKE_MATCH_1}")
math(EXPR entry_value "${entry_address}")
if(entry_value LESS 0x0000010000000000 OR entry_value GREATER_EQUAL 0x0000800000000000)
    message(FATAL_ERROR "${APPLICATION} entry ${entry_address} is outside user virtual memory.")
endif()
string(REGEX MATCHALL "(^|\n)[ \t]*LOAD[ \t][^\n]*" load_headers "${readelf_output}")
list(LENGTH load_headers load_count)
if(load_count LESS 2)
    message(FATAL_ERROR "${APPLICATION} must contain separate ELF PT_LOAD ranges.")
endif()
foreach(load_header IN LISTS load_headers)
    if(NOT load_header MATCHES "0x1000[ \t]*$")
        message(FATAL_ERROR "${APPLICATION} has a PT_LOAD range without 4 KiB alignment: ${load_header}")
    endif()
    if(load_header MATCHES "[ \t]W[ \t]+E[ \t]")
        message(FATAL_ERROR "${APPLICATION} has a writable/executable PT_LOAD range.")
    endif()
endforeach()

execute_process(
    COMMAND "${NM}" -u "${APPLICATION}"
    RESULT_VARIABLE undefined_result
    OUTPUT_VARIABLE undefined_symbols
    ERROR_VARIABLE undefined_error
)
if(NOT undefined_result EQUAL 0)
    message(FATAL_ERROR "nm rejected ${APPLICATION}: ${undefined_error}")
endif()
string(STRIP "${undefined_symbols}" undefined_symbols)
if(NOT undefined_symbols STREQUAL "")
    message(FATAL_ERROR "${APPLICATION} is not statically self-contained: ${undefined_symbols}")
endif()

execute_process(
    COMMAND "${NM}" -g --defined-only "${APPLICATION}"
    RESULT_VARIABLE symbols_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE symbols_error
)
if(NOT symbols_result EQUAL 0)
    message(FATAL_ERROR "nm rejected ${APPLICATION}: ${symbols_error}")
endif()
foreach(required_symbol _start ios_user_main ios_user_syscall6)
    if(NOT symbols MATCHES "(^|\n)[^\n]*[ \t]${required_symbol}(\r?\n|$)")
        message(FATAL_ERROR "${APPLICATION} is missing required symbol ${required_symbol}.")
    endif()
endforeach()
if(symbols MATCHES "[ \t](vfs_|ios_fs_|block_|virtio_blk_)[A-Za-z0-9_]*")
    message(FATAL_ERROR "${APPLICATION} links a forbidden filesystem or storage-layer symbol.")
endif()

message(STATUS "Validated static user ELF: ${APPLICATION}")
