include_guard(GLOBAL)

# Common policy for project-owned freestanding kernel targets. Host-native test
# targets intentionally do not use this policy because they run in a hosted
# environment. Tool discovery and exact version checks remain in the GCC and
# Clang toolchain files.

set(
    INFERENCEOS_APPROVED_COMPILER_EXTENSIONS
    "packed-layout;explicit-alignment;section-placement;noreturn;used-retain;compiler-barrier;architecture-inline-assembly"
    CACHE INTERNAL
    "Constitution-approved compiler extension categories"
)

function(inferenceos_apply_compiler_policy target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "inferenceos_apply_compiler_policy: '${target_name}' is not a CMake target")
    endif()

    if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
        message(FATAL_ERROR
            "InferenceOS kernel targets support only GCC and Clang; found "
            "'${CMAKE_C_COMPILER_ID}'.")
    endif()

    get_target_property(_target_type "${target_name}" TYPE)
    if(_target_type STREQUAL "INTERFACE_LIBRARY")
        set(_policy_scope INTERFACE)
    else()
        set(_policy_scope PRIVATE)
        set_property(TARGET "${target_name}" PROPERTY POSITION_INDEPENDENT_CODE OFF)
    endif()

    target_compile_features("${target_name}" ${_policy_scope} c_std_17)

    # These definitions are consumed by compiler abstraction headers. They do
    # not authorize raw compiler syntax outside those wrappers or the x86-64
    # architecture boundary.
    target_compile_definitions(
        "${target_name}"
        ${_policy_scope}
        INFERENCEOS_FREESTANDING=1
        INFERENCEOS_ALLOW_ATTR_PACKED=1
        INFERENCEOS_ALLOW_ATTR_ALIGNED=1
        INFERENCEOS_ALLOW_ATTR_SECTION=1
        INFERENCEOS_ALLOW_ATTR_NORETURN=1
        INFERENCEOS_ALLOW_ATTR_USED_RETAIN=1
        INFERENCEOS_ALLOW_COMPILER_BARRIER=1
        INFERENCEOS_ALLOW_ARCH_INLINE_ASM=1
    )

    target_compile_options(
        "${target_name}"
        ${_policy_scope}
        "$<$<COMPILE_LANGUAGE:C>:-std=c17>"
        "$<$<COMPILE_LANGUAGE:C>:-ffreestanding>"
        "$<$<COMPILE_LANGUAGE:C>:-fno-builtin>"
        "$<$<COMPILE_LANGUAGE:C>:-fno-stack-protector>"
        "$<$<COMPILE_LANGUAGE:C>:-fno-pic>"
        "$<$<COMPILE_LANGUAGE:C>:-fno-pie>"
        "$<$<COMPILE_LANGUAGE:C>:-fno-asynchronous-unwind-tables>"
        "$<$<COMPILE_LANGUAGE:C>:-fno-unwind-tables>"
        "$<$<COMPILE_LANGUAGE:C>:-mno-red-zone>"
        "$<$<COMPILE_LANGUAGE:C>:-m64>"
        "$<$<COMPILE_LANGUAGE:C>:-Wall>"
        "$<$<COMPILE_LANGUAGE:C>:-Wextra>"
        "$<$<COMPILE_LANGUAGE:C>:-Wpedantic>"
        "$<$<COMPILE_LANGUAGE:C>:-Werror>"
        "$<$<COMPILE_LANGUAGE:C>:-Wshadow>"
        "$<$<COMPILE_LANGUAGE:C>:-Wconversion>"
        "$<$<COMPILE_LANGUAGE:C>:-Wsign-conversion>"
        "$<$<COMPILE_LANGUAGE:C>:-Wundef>"
        "$<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>"
        "$<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>"
        "$<$<COMPILE_LANGUAGE:C>:-Wformat=2>"
        "$<$<COMPILE_LANGUAGE:C>:-Wswitch-enum>"
        "$<$<COMPILE_LANGUAGE:C>:-Wcast-align>"
        "$<$<COMPILE_LANGUAGE:C>:-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.>"
        "$<$<COMPILE_LANGUAGE:C>:-fdebug-prefix-map=${PROJECT_SOURCE_DIR}=.>"
        "$<$<COMPILE_LANGUAGE:C>:-fmacro-prefix-map=${PROJECT_SOURCE_DIR}=.>"
        "$<$<COMPILE_LANGUAGE:C>:-fdebug-prefix-map=${PROJECT_BINARY_DIR}=./build>"
        "$<$<COMPILE_LANGUAGE:ASM>:-ffreestanding>"
        "$<$<COMPILE_LANGUAGE:ASM>:-fno-pic>"
        "$<$<COMPILE_LANGUAGE:ASM>:-fno-pie>"
        "$<$<COMPILE_LANGUAGE:ASM>:-mno-red-zone>"
        "$<$<COMPILE_LANGUAGE:ASM>:-m64>"
        "$<$<COMPILE_LANGUAGE:ASM>:-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.>"
        "$<$<COMPILE_LANGUAGE:ASM>:-fdebug-prefix-map=${PROJECT_SOURCE_DIR}=.>"
        "$<$<COMPILE_LANGUAGE:ASM>:-fdebug-prefix-map=${PROJECT_BINARY_DIR}=./build>"
    )

    if(NOT _target_type STREQUAL "STATIC_LIBRARY"
       AND NOT _target_type STREQUAL "OBJECT_LIBRARY"
       AND NOT _target_type STREQUAL "INTERFACE_LIBRARY")
        target_link_options(
            "${target_name}"
            ${_policy_scope}
            -nostdlib
            -static
            -no-pie
            -Wl,--build-id=none
            -Wl,--fatal-warnings
        )
    endif()

    unset(_policy_scope)
    unset(_target_type)
endfunction()
