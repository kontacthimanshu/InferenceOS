set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(INFERENCEOS_DIRECT_LLD_LINK ON CACHE INTERNAL "Link freestanding targets directly with LLD")

set(INFERENCEOS_LLVM_VERSION "22.1.8")
set(INFERENCEOS_CLANG_TARGET "x86_64-none-elf")

set(_inferenceos_tool_hints)
if(DEFINED ENV{INFERENCEOS_TOOL_ROOT} AND NOT "$ENV{INFERENCEOS_TOOL_ROOT}" STREQUAL "")
    list(APPEND _inferenceos_tool_hints "$ENV{INFERENCEOS_TOOL_ROOT}/bin")
endif()

find_program(INFERENCEOS_CLANG NAMES clang HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLD NAMES ld.lld HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_AR NAMES llvm-ar HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_NM NAMES llvm-nm HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_OBJCOPY NAMES llvm-objcopy HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_OBJDUMP NAMES llvm-objdump HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_RANLIB NAMES llvm-ranlib HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_READELF NAMES llvm-readelf HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_SIZE NAMES llvm-size HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LLVM_STRIP NAMES llvm-strip HINTS ${_inferenceos_tool_hints} REQUIRED)

execute_process(
    COMMAND "${INFERENCEOS_CLANG}" --version
    RESULT_VARIABLE _inferenceos_clang_result
    OUTPUT_VARIABLE _inferenceos_clang_version_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(
    REGEX MATCH
    "clang version ([0-9]+\\.[0-9]+\\.[0-9]+)"
    _inferenceos_clang_version_match
    "${_inferenceos_clang_version_output}"
)
set(_inferenceos_clang_version "${CMAKE_MATCH_1}")
if(NOT _inferenceos_clang_result EQUAL 0 OR
   NOT _inferenceos_clang_version STREQUAL INFERENCEOS_LLVM_VERSION)
    message(
        FATAL_ERROR
        "InferenceOS requires Clang ${INFERENCEOS_LLVM_VERSION}; "
        "found '${_inferenceos_clang_version}' at '${INFERENCEOS_CLANG}'."
    )
endif()

execute_process(
    COMMAND "${INFERENCEOS_LLD}" --version
    RESULT_VARIABLE _inferenceos_lld_result
    OUTPUT_VARIABLE _inferenceos_lld_version_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _inferenceos_lld_version "${_inferenceos_lld_version_output}")
if(NOT _inferenceos_lld_result EQUAL 0 OR
   NOT _inferenceos_lld_version STREQUAL INFERENCEOS_LLVM_VERSION)
    message(
        FATAL_ERROR
        "InferenceOS requires LLD ${INFERENCEOS_LLVM_VERSION}; "
        "found '${_inferenceos_lld_version}' at '${INFERENCEOS_LLD}'."
    )
endif()

set(CMAKE_C_COMPILER "${INFERENCEOS_CLANG}" CACHE FILEPATH "InferenceOS C compiler" FORCE)
set(CMAKE_C_COMPILER_TARGET "${INFERENCEOS_CLANG_TARGET}" CACHE STRING "InferenceOS C target" FORCE)
set(CMAKE_ASM_COMPILER "${INFERENCEOS_CLANG}" CACHE FILEPATH "InferenceOS assembler driver" FORCE)
set(CMAKE_ASM_COMPILER_TARGET "${INFERENCEOS_CLANG_TARGET}" CACHE STRING "InferenceOS ASM target" FORCE)
set(CMAKE_AR "${INFERENCEOS_LLVM_AR}" CACHE FILEPATH "InferenceOS archiver" FORCE)
set(CMAKE_LINKER "${INFERENCEOS_LLD}" CACHE FILEPATH "InferenceOS linker" FORCE)
set(CMAKE_NM "${INFERENCEOS_LLVM_NM}" CACHE FILEPATH "InferenceOS symbol tool" FORCE)
set(CMAKE_OBJCOPY "${INFERENCEOS_LLVM_OBJCOPY}" CACHE FILEPATH "InferenceOS object copier" FORCE)
set(CMAKE_OBJDUMP "${INFERENCEOS_LLVM_OBJDUMP}" CACHE FILEPATH "InferenceOS object dumper" FORCE)
set(CMAKE_RANLIB "${INFERENCEOS_LLVM_RANLIB}" CACHE FILEPATH "InferenceOS archive indexer" FORCE)
set(CMAKE_READELF "${INFERENCEOS_LLVM_READELF}" CACHE FILEPATH "InferenceOS ELF inspector" FORCE)
set(CMAKE_SIZE "${INFERENCEOS_LLVM_SIZE}" CACHE FILEPATH "InferenceOS size tool" FORCE)
set(CMAKE_STRIP "${INFERENCEOS_LLVM_STRIP}" CACHE FILEPATH "InferenceOS strip tool" FORCE)
set(
    CMAKE_C_LINK_EXECUTABLE
    "\"${INFERENCEOS_LLD}\" <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
    CACHE STRING
    "InferenceOS direct LLD C link rule"
    FORCE
)
set(
    CMAKE_ASM_LINK_EXECUTABLE
    "\"${INFERENCEOS_LLD}\" <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
    CACHE STRING
    "InferenceOS direct LLD ASM link rule"
    FORCE
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

unset(_inferenceos_tool_hints)
unset(_inferenceos_clang_result)
unset(_inferenceos_clang_version)
unset(_inferenceos_clang_version_match)
unset(_inferenceos_clang_version_output)
unset(_inferenceos_lld_result)
unset(_inferenceos_lld_version)
unset(_inferenceos_lld_version_output)
