include_guard(GLOBAL)
include(CMakeParseArguments)
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)

set(INFERENCEOS_BOOT_IMAGE_SIZE "67108864" CACHE STRING
    "Deterministic standard FAT32 UEFI boot-image size in bytes")
set(INFERENCEOS_BOOT_VOLUME_ID "0x494E464F" CACHE STRING
    "Explicit deterministic FAT32 boot volume identifier")
set(INFERENCEOS_SOURCE_DATE_EPOCH "$ENV{SOURCE_DATE_EPOCH}" CACHE STRING
    "Epoch used for deterministic FAT directory timestamps")
if(INFERENCEOS_SOURCE_DATE_EPOCH STREQUAL "")
    set(INFERENCEOS_SOURCE_DATE_EPOCH "0")
endif()

function(inferenceos_add_boot_image)
    set(_one_value TARGET LOADER KERNEL OUTPUT)
    cmake_parse_arguments(BOOT "" "${_one_value}" "" ${ARGN})
    if(NOT BOOT_TARGET OR NOT BOOT_LOADER OR NOT BOOT_KERNEL OR NOT BOOT_OUTPUT)
        message(FATAL_ERROR
            "inferenceos_add_boot_image requires TARGET, LOADER, KERNEL, and OUTPUT")
    endif()

    add_custom_command(
        OUTPUT "${BOOT_OUTPUT}"
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/create-uefi-boot-image.py"
            --output "${BOOT_OUTPUT}"
            --loader "$<TARGET_FILE:${BOOT_LOADER}>"
            --kernel "$<TARGET_FILE:${BOOT_KERNEL}>"
            --size "${INFERENCEOS_BOOT_IMAGE_SIZE}"
            --volume-id "${INFERENCEOS_BOOT_VOLUME_ID}"
            --epoch "${INFERENCEOS_SOURCE_DATE_EPOCH}"
        DEPENDS
            "${BOOT_LOADER}"
            "${BOOT_KERNEL}"
            "${PROJECT_SOURCE_DIR}/tools/create-uefi-boot-image.py"
        COMMENT "Creating deterministic FAT32 UEFI boot image ${BOOT_OUTPUT}"
        VERBATIM
    )
    add_custom_target("${BOOT_TARGET}" ALL DEPENDS "${BOOT_OUTPUT}")
endfunction()

function(inferenceos_add_qemu_launch_target)
    set(_one_value TARGET BOOT_IMAGE DATA_IMAGE QEMU OVMF_CODE OVMF_VARS)
    cmake_parse_arguments(RUN "" "${_one_value}" "" ${ARGN})
    if(NOT RUN_TARGET OR NOT RUN_BOOT_IMAGE OR NOT RUN_QEMU OR NOT RUN_OVMF_CODE)
        message(FATAL_ERROR
            "inferenceos_add_qemu_launch_target requires TARGET, BOOT_IMAGE, QEMU, and OVMF_CODE")
    endif()
    set(_command
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run-qemu.py"
        --qemu "${RUN_QEMU}" --firmware-code "${RUN_OVMF_CODE}"
        --boot-image "${RUN_BOOT_IMAGE}")
    if(RUN_OVMF_VARS)
        list(APPEND _command --firmware-vars "${RUN_OVMF_VARS}")
    endif()
    if(RUN_DATA_IMAGE)
        list(APPEND _command --data-image "${RUN_DATA_IMAGE}")
    endif()
    add_custom_target("${RUN_TARGET}"
        COMMAND ${_command}
        DEPENDS "${RUN_BOOT_IMAGE}"
        USES_TERMINAL
        VERBATIM)
endfunction()
