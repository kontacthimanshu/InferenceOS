# Reference cross-toolchain for the freestanding InferenceOS x86-64 kernel.
#
# This file intentionally owns only tool discovery, version pinning, target
# identity, and the minimum C17/freestanding language mode. Shared warnings,
# ABI flags, deterministic-path flags, and the compiler-extension allowlist
# belong to cmake/InferenceCompilerPolicy.cmake (T004).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(INFERENCEOS_GCC_VERSION "16.2.0" CACHE STRING
    "Exact GCC release required by the InferenceOS reference profile")
set(INFERENCEOS_BINUTILS_VERSION "2.45" CACHE STRING
    "GNU binutils release required by the InferenceOS reference profile")
set(INFERENCEOS_GNU_TOOLCHAIN_ROOT "" CACHE PATH
    "Optional root of a GCC x86_64-elf cross-toolchain installation")

function(_inferenceos_find_gnu_tool output_variable program_name)
    if(INFERENCEOS_GNU_TOOLCHAIN_ROOT)
        find_program(
            _program
            NAMES "${program_name}"
            HINTS
                "${INFERENCEOS_GNU_TOOLCHAIN_ROOT}/bin"
                "${INFERENCEOS_GNU_TOOLCHAIN_ROOT}"
            NO_DEFAULT_PATH
        )
    else()
        find_program(_program NAMES "${program_name}")
    endif()

    if(NOT _program)
        message(FATAL_ERROR
            "Required reference tool '${program_name}' was not found. "
            "Install GCC ${INFERENCEOS_GCC_VERSION} and GNU binutils "
            "${INFERENCEOS_BINUTILS_VERSION} for target x86_64-elf, or set "
            "INFERENCEOS_GNU_TOOLCHAIN_ROOT to their installation root.")
    endif()

    set("${output_variable}" "${_program}" PARENT_SCOPE)
    unset(_program CACHE)
endfunction()

_inferenceos_find_gnu_tool(_gcc x86_64-elf-gcc)
_inferenceos_find_gnu_tool(_ar x86_64-elf-ar)
_inferenceos_find_gnu_tool(_ranlib x86_64-elf-ranlib)
_inferenceos_find_gnu_tool(_ld x86_64-elf-ld)
_inferenceos_find_gnu_tool(_nm x86_64-elf-nm)
_inferenceos_find_gnu_tool(_objcopy x86_64-elf-objcopy)
_inferenceos_find_gnu_tool(_objdump x86_64-elf-objdump)
_inferenceos_find_gnu_tool(_readelf x86_64-elf-readelf)
_inferenceos_find_gnu_tool(_size x86_64-elf-size)
_inferenceos_find_gnu_tool(_strip x86_64-elf-strip)

execute_process(
    COMMAND "${_gcc}" -dumpfullversion -dumpversion
    RESULT_VARIABLE _gcc_version_result
    OUTPUT_VARIABLE _gcc_version
    ERROR_VARIABLE _gcc_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _gcc_version_result EQUAL 0)
    message(FATAL_ERROR
        "Unable to query ${_gcc}: ${_gcc_version_error}")
endif()
if(NOT _gcc_version STREQUAL INFERENCEOS_GCC_VERSION)
    message(FATAL_ERROR
        "Reference GCC version mismatch: expected ${INFERENCEOS_GCC_VERSION}, "
        "found ${_gcc_version} at ${_gcc}.")
endif()

execute_process(
    COMMAND "${_ld}" --version
    RESULT_VARIABLE _ld_version_result
    OUTPUT_VARIABLE _ld_version_output
    ERROR_VARIABLE _ld_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _ld_version_result EQUAL 0)
    message(FATAL_ERROR
        "Unable to query ${_ld}: ${_ld_version_error}")
endif()
if(NOT _ld_version_output MATCHES "GNU ld[^\n]* ${INFERENCEOS_BINUTILS_VERSION}([.]|[ -]|$)")
    message(FATAL_ERROR
        "Reference binutils version mismatch: expected GNU ld "
        "${INFERENCEOS_BINUTILS_VERSION}, found:\n${_ld_version_output}")
endif()

set(CMAKE_C_COMPILER "${_gcc}" CACHE FILEPATH "InferenceOS C compiler" FORCE)
set(CMAKE_ASM_COMPILER "${_gcc}" CACHE FILEPATH "InferenceOS assembler driver" FORCE)
set(CMAKE_AR "${_ar}" CACHE FILEPATH "InferenceOS archiver" FORCE)
set(CMAKE_RANLIB "${_ranlib}" CACHE FILEPATH "InferenceOS archive indexer" FORCE)
set(CMAKE_LINKER "${_ld}" CACHE FILEPATH "InferenceOS linker" FORCE)
set(CMAKE_NM "${_nm}" CACHE FILEPATH "InferenceOS symbol tool" FORCE)
set(CMAKE_OBJCOPY "${_objcopy}" CACHE FILEPATH "InferenceOS object converter" FORCE)
set(CMAKE_OBJDUMP "${_objdump}" CACHE FILEPATH "InferenceOS object inspector" FORCE)
set(CMAKE_READELF "${_readelf}" CACHE FILEPATH "InferenceOS ELF inspector" FORCE)
set(CMAKE_SIZE "${_size}" CACHE FILEPATH "InferenceOS size tool" FORCE)
set(CMAKE_STRIP "${_strip}" CACHE FILEPATH "InferenceOS strip tool" FORCE)

set(CMAKE_C_COMPILER_TARGET x86_64-elf)
set(CMAKE_ASM_COMPILER_TARGET x86_64-elf)
set(CMAKE_C_STANDARD 17 CACHE STRING "InferenceOS C language standard" FORCE)
set(CMAKE_C_STANDARD_REQUIRED ON CACHE BOOL "Require ISO C17" FORCE)
set(CMAKE_C_EXTENSIONS OFF CACHE BOOL "Disable GNU language dialects" FORCE)
set(CMAKE_C_FLAGS_INIT "-std=c17 -ffreestanding")
set(CMAKE_ASM_FLAGS_INIT "-ffreestanding")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib")
set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING
    "No hosted C runtime is linked into InferenceOS" FORCE)

# Never satisfy target headers or libraries from the build host. Host-native
# tests use a separate, non-toolchain configure preset.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

unset(_gcc_version_result)
unset(_gcc_version)
unset(_gcc_version_error)
unset(_ld_version_result)
unset(_ld_version_output)
unset(_ld_version_error)
