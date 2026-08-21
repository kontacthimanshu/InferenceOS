# Validation cross-toolchain for the freestanding InferenceOS x86-64 kernel.
#
# This file owns LLVM tool discovery, exact version validation, target identity,
# and the minimum C17/freestanding language mode. Shared warnings, ABI flags,
# deterministic-path flags, and the extension allowlist are defined by T004 in
# cmake/InferenceCompilerPolicy.cmake.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(INFERENCEOS_LLVM_VERSION "22.1.8" CACHE STRING
    "Exact Clang/LLVM release required by the validation profile")
set(INFERENCEOS_LLVM_TOOLCHAIN_ROOT "" CACHE PATH
    "Optional root of a Clang/LLVM toolchain installation")
set(INFERENCEOS_LLVM_TARGET "x86_64-unknown-none-elf" CACHE STRING
    "Clang target triple for the freestanding InferenceOS kernel")

function(_inferenceos_find_llvm_tool output_variable)
    set(_candidate_names ${ARGN})

    if(INFERENCEOS_LLVM_TOOLCHAIN_ROOT)
        find_program(
            _program
            NAMES ${_candidate_names}
            HINTS
                "${INFERENCEOS_LLVM_TOOLCHAIN_ROOT}/bin"
                "${INFERENCEOS_LLVM_TOOLCHAIN_ROOT}"
            NO_DEFAULT_PATH
        )
    else()
        find_program(_program NAMES ${_candidate_names})
    endif()

    if(NOT _program)
        list(JOIN _candidate_names ", " _candidate_list)
        message(FATAL_ERROR
            "Required LLVM ${INFERENCEOS_LLVM_VERSION} tool was not found "
            "(tried: ${_candidate_list}). Install the pinned LLVM release or "
            "set INFERENCEOS_LLVM_TOOLCHAIN_ROOT to its installation root.")
    endif()

    set("${output_variable}" "${_program}" PARENT_SCOPE)
    unset(_program CACHE)
endfunction()

_inferenceos_find_llvm_tool(_clang clang clang-22)
_inferenceos_find_llvm_tool(_lld ld.lld ld.lld-22)
_inferenceos_find_llvm_tool(_ar llvm-ar llvm-ar-22)
_inferenceos_find_llvm_tool(_ranlib llvm-ranlib llvm-ranlib-22)
_inferenceos_find_llvm_tool(_nm llvm-nm llvm-nm-22)
_inferenceos_find_llvm_tool(_objcopy llvm-objcopy llvm-objcopy-22)
_inferenceos_find_llvm_tool(_objdump llvm-objdump llvm-objdump-22)
_inferenceos_find_llvm_tool(_readelf llvm-readelf llvm-readelf-22)
_inferenceos_find_llvm_tool(_size llvm-size llvm-size-22)
_inferenceos_find_llvm_tool(_strip llvm-strip llvm-strip-22)

execute_process(
    COMMAND "${_clang}" --version
    RESULT_VARIABLE _clang_version_result
    OUTPUT_VARIABLE _clang_version_output
    ERROR_VARIABLE _clang_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _clang_version_result EQUAL 0)
    message(FATAL_ERROR
        "Unable to query ${_clang}: ${_clang_version_error}")
endif()
string(REPLACE "." "[.]" _llvm_version_regex "${INFERENCEOS_LLVM_VERSION}")
if(NOT _clang_version_output MATCHES
        "clang version ${_llvm_version_regex}([ -]|$)")
    message(FATAL_ERROR
        "Validation Clang version mismatch: expected ${INFERENCEOS_LLVM_VERSION}, "
        "found:\n${_clang_version_output}")
endif()

execute_process(
    COMMAND "${_lld}" --version
    RESULT_VARIABLE _lld_version_result
    OUTPUT_VARIABLE _lld_version_output
    ERROR_VARIABLE _lld_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _lld_version_result EQUAL 0)
    message(FATAL_ERROR
        "Unable to query ${_lld}: ${_lld_version_error}")
endif()
if(NOT _lld_version_output MATCHES
        "LLD ${_llvm_version_regex}([ -]|$)")
    message(FATAL_ERROR
        "Validation LLD version mismatch: expected ${INFERENCEOS_LLVM_VERSION}, "
        "found:\n${_lld_version_output}")
endif()

set(CMAKE_C_COMPILER "${_clang}" CACHE FILEPATH
    "InferenceOS validation C compiler" FORCE)
set(CMAKE_ASM_COMPILER "${_clang}" CACHE FILEPATH
    "InferenceOS validation assembler driver" FORCE)
set(CMAKE_LINKER "${_lld}" CACHE FILEPATH
    "InferenceOS validation linker" FORCE)
set(CMAKE_AR "${_ar}" CACHE FILEPATH "LLVM archiver" FORCE)
set(CMAKE_RANLIB "${_ranlib}" CACHE FILEPATH "LLVM archive indexer" FORCE)
set(CMAKE_NM "${_nm}" CACHE FILEPATH "LLVM symbol tool" FORCE)
set(CMAKE_OBJCOPY "${_objcopy}" CACHE FILEPATH "LLVM object converter" FORCE)
set(CMAKE_OBJDUMP "${_objdump}" CACHE FILEPATH "LLVM object inspector" FORCE)
set(CMAKE_READELF "${_readelf}" CACHE FILEPATH "LLVM ELF inspector" FORCE)
set(CMAKE_SIZE "${_size}" CACHE FILEPATH "LLVM size tool" FORCE)
set(CMAKE_STRIP "${_strip}" CACHE FILEPATH "LLVM strip tool" FORCE)

set(CMAKE_C_COMPILER_TARGET "${INFERENCEOS_LLVM_TARGET}")
set(CMAKE_ASM_COMPILER_TARGET "${INFERENCEOS_LLVM_TARGET}")
set(CMAKE_C_STANDARD 17 CACHE STRING "InferenceOS C language standard" FORCE)
set(CMAKE_C_STANDARD_REQUIRED ON CACHE BOOL "Require ISO C17" FORCE)
set(CMAKE_C_EXTENSIONS OFF CACHE BOOL "Disable GNU language dialects" FORCE)
set(CMAKE_C_FLAGS_INIT
    "--target=${INFERENCEOS_LLVM_TARGET} -std=c17 -ffreestanding")
set(CMAKE_ASM_FLAGS_INIT
    "--target=${INFERENCEOS_LLVM_TARGET} -ffreestanding")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "--target=${INFERENCEOS_LLVM_TARGET} -fuse-ld=lld -nostdlib")
set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING
    "No hosted C runtime is linked into InferenceOS" FORCE)

# Target headers, libraries, and packages must never resolve from the host.
# Host-native tests use their own configure preset without this toolchain file.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

unset(_clang_version_result)
unset(_clang_version_output)
unset(_clang_version_error)
unset(_lld_version_result)
unset(_lld_version_output)
unset(_lld_version_error)
unset(_llvm_version_regex)
