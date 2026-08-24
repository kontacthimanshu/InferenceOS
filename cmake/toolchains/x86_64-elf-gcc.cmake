set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(INFERENCEOS_GCC_VERSION "16.2.0")
set(INFERENCEOS_BINUTILS_VERSION "2.45")

set(_inferenceos_tool_hints)
if(DEFINED ENV{INFERENCEOS_TOOL_ROOT} AND NOT "$ENV{INFERENCEOS_TOOL_ROOT}" STREQUAL "")
    list(APPEND _inferenceos_tool_hints "$ENV{INFERENCEOS_TOOL_ROOT}/bin")
endif()

find_program(
    INFERENCEOS_GCC
    NAMES x86_64-elf-gcc
    HINTS ${_inferenceos_tool_hints}
    REQUIRED
)
find_program(INFERENCEOS_AR NAMES x86_64-elf-ar HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_LD NAMES x86_64-elf-ld HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_NM NAMES x86_64-elf-nm HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_OBJCOPY NAMES x86_64-elf-objcopy HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_OBJDUMP NAMES x86_64-elf-objdump HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_RANLIB NAMES x86_64-elf-ranlib HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_READELF NAMES x86_64-elf-readelf HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_SIZE NAMES x86_64-elf-size HINTS ${_inferenceos_tool_hints} REQUIRED)
find_program(INFERENCEOS_STRIP NAMES x86_64-elf-strip HINTS ${_inferenceos_tool_hints} REQUIRED)

execute_process(
    COMMAND "${INFERENCEOS_GCC}" -dumpfullversion
    RESULT_VARIABLE _inferenceos_gcc_result
    OUTPUT_VARIABLE _inferenceos_gcc_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _inferenceos_gcc_result EQUAL 0 OR
   NOT _inferenceos_gcc_version STREQUAL INFERENCEOS_GCC_VERSION)
    message(
        FATAL_ERROR
        "InferenceOS requires x86_64-elf-gcc ${INFERENCEOS_GCC_VERSION}; "
        "found '${_inferenceos_gcc_version}' at '${INFERENCEOS_GCC}'."
    )
endif()

execute_process(
    COMMAND "${INFERENCEOS_LD}" --version
    RESULT_VARIABLE _inferenceos_ld_result
    OUTPUT_VARIABLE _inferenceos_ld_version_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" _inferenceos_ld_version "${_inferenceos_ld_version_output}")
if(NOT _inferenceos_ld_result EQUAL 0 OR
   NOT _inferenceos_ld_version STREQUAL INFERENCEOS_BINUTILS_VERSION)
    message(
        FATAL_ERROR
        "InferenceOS requires GNU binutils ${INFERENCEOS_BINUTILS_VERSION}; "
        "found '${_inferenceos_ld_version}' at '${INFERENCEOS_LD}'."
    )
endif()

set(CMAKE_C_COMPILER "${INFERENCEOS_GCC}" CACHE FILEPATH "InferenceOS C compiler" FORCE)
set(CMAKE_ASM_COMPILER "${INFERENCEOS_GCC}" CACHE FILEPATH "InferenceOS assembler driver" FORCE)
set(CMAKE_AR "${INFERENCEOS_AR}" CACHE FILEPATH "InferenceOS archiver" FORCE)
set(CMAKE_LINKER "${INFERENCEOS_LD}" CACHE FILEPATH "InferenceOS linker" FORCE)
set(CMAKE_NM "${INFERENCEOS_NM}" CACHE FILEPATH "InferenceOS symbol tool" FORCE)
set(CMAKE_OBJCOPY "${INFERENCEOS_OBJCOPY}" CACHE FILEPATH "InferenceOS object copier" FORCE)
set(CMAKE_OBJDUMP "${INFERENCEOS_OBJDUMP}" CACHE FILEPATH "InferenceOS object dumper" FORCE)
set(CMAKE_RANLIB "${INFERENCEOS_RANLIB}" CACHE FILEPATH "InferenceOS archive indexer" FORCE)
set(CMAKE_READELF "${INFERENCEOS_READELF}" CACHE FILEPATH "InferenceOS ELF inspector" FORCE)
set(CMAKE_SIZE "${INFERENCEOS_SIZE}" CACHE FILEPATH "InferenceOS size tool" FORCE)
set(CMAKE_STRIP "${INFERENCEOS_STRIP}" CACHE FILEPATH "InferenceOS strip tool" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

unset(_inferenceos_tool_hints)
unset(_inferenceos_gcc_result)
unset(_inferenceos_gcc_version)
unset(_inferenceos_ld_result)
unset(_inferenceos_ld_version)
unset(_inferenceos_ld_version_output)
