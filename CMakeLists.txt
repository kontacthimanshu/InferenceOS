cmake_minimum_required(VERSION 4.1.1)

project(
    InferenceOS
    VERSION 0.1.0
    DESCRIPTION "InferenceOS CUI/GUI filesystem demonstrator"
    LANGUAGES C ASM
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "InferenceOS requires an out-of-source build (for example, -B build/gcc-debug).")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(INFERENCEOS_WARNINGS_AS_ERRORS "Treat warnings in project-owned targets as errors" ON)
option(INFERENCEOS_BUILD_HOST_TESTS "Build hosted test executables and support libraries" OFF)

find_program(
    INFERENCEOS_POWERSHELL_EXECUTABLE
    NAMES pwsh powershell powershell.exe
    DOC "PowerShell executable used by InferenceOS image and QEMU targets"
)
if(NOT INFERENCEOS_POWERSHELL_EXECUTABLE)
    set(
        INFERENCEOS_POWERSHELL_EXECUTABLE
        pwsh
        CACHE FILEPATH
        "PowerShell executable used by InferenceOS image and QEMU targets"
        FORCE
    )
endif()

function(inferenceos_powershell_path output_name input_path)
    if(input_path STREQUAL "")
        set("${output_name}" "" PARENT_SCOPE)
        return()
    endif()
    # Cross presets target Generic, so path translation must follow the build host.
    if(CMAKE_HOST_UNIX AND INFERENCEOS_POWERSHELL_EXECUTABLE MATCHES "[.]exe$")
        execute_process(
            COMMAND wslpath -w -a "${input_path}"
            RESULT_VARIABLE _inferenceos_wslpath_result
            OUTPUT_VARIABLE _inferenceos_wslpath_output
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT _inferenceos_wslpath_result EQUAL 0)
            message(FATAL_ERROR "Cannot translate '${input_path}' for Windows PowerShell.")
        endif()
        set("${output_name}" "${_inferenceos_wslpath_output}" PARENT_SCOPE)
    else()
        set("${output_name}" "${input_path}" PARENT_SCOPE)
    endif()
endfunction()

set(
    INFERENCEOS_ARTIFACT_DIRECTORY
    "${CMAKE_BINARY_DIR}/artifacts"
    CACHE PATH
    "Directory for packaged InferenceOS images"
)
# These are outputs of project-owned targets. They deliberately are not cache
# inputs: a release image must never be assembled from prebuilt external files.
set(INFERENCEOS_UEFI_LOADER_FILE "${INFERENCEOS_ARTIFACT_DIRECTORY}/BOOTX64.EFI")
set(INFERENCEOS_KERNEL_ELF_FILE "${INFERENCEOS_ARTIFACT_DIRECTORY}/kernel.elf")
set(
    INFERENCEOS_SYSTEM_MODULE_DEFINITION
    "${INFERENCEOS_ARTIFACT_DIRECTORY}/system_modules.json"
)
set(
    INFERENCEOS_OVMF_CODE_FILE
    "$ENV{INFERENCEOS_OVMF_CODE}"
    CACHE FILEPATH
    "Pinned read-only OVMF code image"
)
set(
    INFERENCEOS_OVMF_VARS_FILE
    "$ENV{INFERENCEOS_OVMF_VARS}"
    CACHE FILEPATH
    "Pinned OVMF variable-store template"
)
set(
    INFERENCEOS_QEMU_EXECUTABLE
    ""
    CACHE FILEPATH
    "Pinned qemu-system-x86_64 executable; empty uses launcher discovery"
)
if(INFERENCEOS_QEMU_EXECUTABLE STREQUAL "")
    find_program(_inferenceos_detected_qemu NAMES qemu-system-x86_64)
    if(_inferenceos_detected_qemu)
        set(
            INFERENCEOS_QEMU_EXECUTABLE
            "${_inferenceos_detected_qemu}"
            CACHE FILEPATH
            "Pinned qemu-system-x86_64 executable; empty uses launcher discovery"
            FORCE
        )
    endif()
    unset(_inferenceos_detected_qemu)
endif()
set(
    INFERENCEOS_QEMU_TIMEOUT_SECONDS
    60
    CACHE STRING
    "Foundational QEMU marker-test timeout"
)
set(
    INFERENCEOS_REGISTRY_BENCHMARK_TRACE
    ""
    CACHE FILEPATH
    "QEMU TCG registry benchmark marker/counter trace"
)
set(
    INFERENCEOS_REGISTRY_BENCHMARK_IMAGE_SHA256
    ""
    CACHE STRING
    "SHA-256 of the deterministic image used for the registry benchmark"
)
set(
    INFERENCEOS_REGISTRY_BENCHMARK_HARDWARE_COUNTERS
    ""
    CACHE FILEPATH
    "Optional separately qualified pinned-host hardware-cycle report"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Freestanding)
include(CTest)

add_library(inferenceos_kernel_headers INTERFACE)
add_library(InferenceOS::KernelHeaders ALIAS inferenceos_kernel_headers)
target_include_directories(
    inferenceos_kernel_headers
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/user/include"
)
target_link_libraries(inferenceos_kernel_headers INTERFACE InferenceOS::Freestanding)

add_library(
    inferenceos_arch_x86_64
    STATIC
        src/arch/x86_64/entry.S
        src/arch/x86_64/context.S
        src/arch/x86_64/acpi.c
        src/arch/x86_64/apic_timer.c
        src/arch/x86_64/gdt.c
        src/arch/x86_64/hyperv.c
        src/arch/x86_64/hypercall.S
        src/arch/x86_64/interrupts.c
        src/arch/x86_64/io.c
        src/arch/x86_64/pci.c
        src/arch/x86_64/paging.c
        src/arch/x86_64/pic.c
        src/arch/x86_64/syscall.c
)
add_library(InferenceOS::ArchX86_64 ALIAS inferenceos_arch_x86_64)
target_include_directories(
    inferenceos_arch_x86_64
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/include"
)
target_link_libraries(
    inferenceos_arch_x86_64
    PUBLIC
        InferenceOS::KernelHeaders
)
inferenceos_configure_freestanding_target(inferenceos_arch_x86_64)

add_library(inferenceos_serial STATIC src/drivers/serial/serial.c)
add_library(InferenceOS::Serial ALIAS inferenceos_serial)
target_include_directories(
    inferenceos_serial
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/serial/include"
)
target_link_libraries(
    inferenceos_serial
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::KernelHeaders
)
inferenceos_configure_freestanding_target(inferenceos_serial)

add_library(inferenceos_kernel_diagnostics STATIC src/kernel/panic.c)
add_library(InferenceOS::KernelDiagnostics ALIAS inferenceos_kernel_diagnostics)
target_link_libraries(
    inferenceos_kernel_diagnostics
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::KernelHeaders
        InferenceOS::Serial
)
inferenceos_configure_freestanding_target(inferenceos_kernel_diagnostics)

add_library(
    inferenceos_runtime
    STATIC
        src/runtime/memory.c
        src/runtime/string.c
)
add_library(InferenceOS::Runtime ALIAS inferenceos_runtime)
target_include_directories(
    inferenceos_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include"
)
inferenceos_configure_freestanding_target(inferenceos_runtime)

add_library(
    inferenceos_user_runtime
    STATIC
        src/runtime/user/crt0.S
        src/runtime/user/runtime.c
        src/runtime/user/syscall.S
        src/runtime/user/syscall.c
)
add_library(InferenceOS::UserRuntime ALIAS inferenceos_user_runtime)
target_include_directories(
    inferenceos_user_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/user/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include"
)
target_compile_options(
    inferenceos_user_runtime
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-mcmodel=large>
        $<$<COMPILE_LANGUAGE:C>:-fno-unwind-tables>
        $<$<COMPILE_LANGUAGE:C>:-fno-asynchronous-unwind-tables>
)
target_link_libraries(inferenceos_user_runtime PUBLIC InferenceOS::Runtime)
inferenceos_configure_freestanding_target(inferenceos_user_runtime)

add_library(inferenceos_boot STATIC src/kernel/boot_info.c)
add_library(InferenceOS::Boot ALIAS inferenceos_boot)
target_link_libraries(
    inferenceos_boot
    PUBLIC
        InferenceOS::KernelHeaders
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_boot)

add_library(
    inferenceos_input
    STATIC
        src/drivers/ps2/keyboard.c
        src/drivers/ps2/mouse.c
        src/gui/input/events.c
)
add_library(InferenceOS::Input ALIAS inferenceos_input)
target_include_directories(
    inferenceos_input
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/ps2/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/input/include"
)
target_link_libraries(
    inferenceos_input
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::KernelHeaders
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_input)

add_library(
    inferenceos_graphics
    STATIC
        src/drivers/framebuffer/gop.c
        src/gui/graphics/primitives.c
        src/gui/graphics/text.c
        src/gui/graphics/text_console.c
)
add_library(InferenceOS::Graphics ALIAS inferenceos_graphics)
target_include_directories(
    inferenceos_graphics
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/framebuffer/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/graphics/include"
)
target_link_libraries(
    inferenceos_graphics
    PUBLIC
        InferenceOS::KernelHeaders
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_graphics)

add_library(
    inferenceos_window
    STATIC
        src/gui/window/window_manager.c
        src/gui/window/compositor.c
)
add_library(InferenceOS::Window ALIAS inferenceos_window)
target_include_directories(
    inferenceos_window
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/window/include"
)
target_link_libraries(
    inferenceos_window
    PUBLIC
        InferenceOS::Graphics
        InferenceOS::KernelHeaders
)
inferenceos_configure_freestanding_target(inferenceos_window)

add_library(inferenceos_display_safe STATIC src/shell/display_safe_entry.c)
add_library(InferenceOS::DisplaySafe ALIAS inferenceos_display_safe)
target_include_directories(
    inferenceos_display_safe
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include"
)
target_link_libraries(
    inferenceos_display_safe
    PUBLIC
        InferenceOS::KernelHeaders
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_display_safe)

add_library(
    inferenceos_file_explorer
    STATIC
        src/gui/file_explorer/client.c
        src/gui/file_explorer/controller.c
        src/gui/file_explorer/model.c
        src/gui/file_explorer/view_provider.c
        src/gui/file_explorer/window.c
        src/gui/file_explorer/properties.c
        src/gui/file_explorer/diagnostic_inspector.c
)
add_library(InferenceOS::FileExplorer ALIAS inferenceos_file_explorer)
target_include_directories(
    inferenceos_file_explorer
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/file_explorer/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/include"
)
target_link_libraries(
    inferenceos_file_explorer
    PUBLIC
        InferenceOS::Input
        InferenceOS::DisplaySafe
        InferenceOS::KernelHeaders
        InferenceOS::Runtime
        InferenceOS::IPC
        InferenceOS::ShellRuntime
        InferenceOS::Window
)
inferenceos_configure_freestanding_target(inferenceos_file_explorer)

add_library(
    inferenceos_doc_viewer
    STATIC
        src/applications/doc_viewer/main.c
)
add_library(InferenceOS::DocViewer ALIAS inferenceos_doc_viewer)
target_include_directories(
    inferenceos_doc_viewer
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/applications/doc_viewer/include"
)
target_link_libraries(
    inferenceos_doc_viewer
    PUBLIC InferenceOS::FileExplorer
)
inferenceos_configure_freestanding_target(inferenceos_doc_viewer)

add_library(
    inferenceos_txt_viewer
    STATIC
        src/applications/txt_viewer/main.c
)
add_library(InferenceOS::TxtViewer ALIAS inferenceos_txt_viewer)
target_include_directories(
    inferenceos_txt_viewer
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/applications/txt_viewer/include"
)
target_link_libraries(
    inferenceos_txt_viewer
    PUBLIC InferenceOS::FileExplorer
)
inferenceos_configure_freestanding_target(inferenceos_txt_viewer)

add_library(
    inferenceos_cui
    STATIC
        src/cui/parser.c
        src/cui/commands.c
        src/cui/console.c
)
add_library(InferenceOS::Cui ALIAS inferenceos_cui)
target_include_directories(inferenceos_cui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/cui/include")
target_link_libraries(inferenceos_cui PUBLIC InferenceOS::KernelHeaders InferenceOS::Runtime)
inferenceos_configure_freestanding_target(inferenceos_cui)

add_library(
    inferenceos_shell_runtime
    STATIC
        src/shell/bootstrap.c
        src/shell/adapter_service.c
        src/shell/proprietary_service.c
        src/shell/service.c
        src/gui/desktop/desktop.c
        src/gui/terminal/terminal.c
)
add_library(InferenceOS::ShellRuntime ALIAS inferenceos_shell_runtime)
target_include_directories(
    inferenceos_shell_runtime
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shell/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/desktop/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/terminal/include"
)
target_link_libraries(
    inferenceos_shell_runtime
    PUBLIC
        InferenceOS::Cui
        InferenceOS::DisplaySafe
        InferenceOS::Input
        InferenceOS::IPC
        InferenceOS::Process
        InferenceOS::Window
)
inferenceos_configure_freestanding_target(inferenceos_shell_runtime)

add_library(
    inferenceos_proprietary_test
    STATIC
        src/applications/proprietary_test/main.c
)
add_library(InferenceOS::ProprietaryTest ALIAS inferenceos_proprietary_test)
target_include_directories(
    inferenceos_proprietary_test
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/applications/proprietary_test/include"
)
target_link_libraries(
    inferenceos_proprietary_test
    PUBLIC InferenceOS::ShellRuntime
)
inferenceos_configure_freestanding_target(inferenceos_proprietary_test)

add_library(inferenceos_custom_test STATIC src/applications/custom_test/main.c)
add_library(InferenceOS::CustomTest ALIAS inferenceos_custom_test)
target_include_directories(
    inferenceos_custom_test
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/applications/custom_test/include"
)
target_link_libraries(inferenceos_custom_test PUBLIC InferenceOS::ShellRuntime)
inferenceos_configure_freestanding_target(inferenceos_custom_test)

if(CMAKE_CROSSCOMPILING)
    find_program(
        INFERENCEOS_EFI_CLANG
        NAMES clang
        HINTS "$ENV{INFERENCEOS_TOOL_ROOT}/bin"
        DOC "Pinned Clang used to compile the PE32+ UEFI loader"
        REQUIRED
    )
    find_program(
        INFERENCEOS_EFI_LLD_LINK
        NAMES lld-link
        HINTS "$ENV{INFERENCEOS_TOOL_ROOT}/bin"
        DOC "Pinned LLD COFF linker used to produce the PE32+ UEFI loader"
        REQUIRED
    )
    execute_process(COMMAND "${INFERENCEOS_EFI_CLANG}" --version OUTPUT_VARIABLE _efi_clang_version)
    execute_process(COMMAND "${INFERENCEOS_EFI_LLD_LINK}" --version OUTPUT_VARIABLE _efi_lld_version)
    if(NOT _efi_clang_version MATCHES "clang version 22[.]1[.]8"
       OR NOT _efi_lld_version MATCHES "LLD 22[.]1[.]8")
        message(FATAL_ERROR "PE32+ loader packaging requires pinned Clang/LLD 22.1.8.")
    endif()
    add_executable(
        inferenceos_uefi_loader
            src/boot/uefi/main.c
            src/boot/uefi/elf.c
            src/boot/uefi/modules.c
            src/boot/uefi/sha256.c
            src/arch/x86_64/io.c
    )
    target_include_directories(
        inferenceos_uefi_loader
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/include"
    )
    target_link_libraries(
        inferenceos_uefi_loader
        PRIVATE
            InferenceOS::KernelHeaders
            InferenceOS::Runtime
    )
    inferenceos_configure_freestanding_target(inferenceos_uefi_loader)
    if(INFERENCEOS_DIRECT_LLD_LINK)
        target_link_options(
            inferenceos_uefi_loader
            PRIVATE
                -T "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/loader.ld"
                -e efi_main
        )
    else()
        target_link_options(
            inferenceos_uefi_loader
            PRIVATE
                "LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/loader.ld"
                LINKER:-e,efi_main
        )
    endif()
    set_target_properties(
        inferenceos_uefi_loader
        PROPERTIES
            OUTPUT_NAME uefi-loader
            SUFFIX .elf
            RUNTIME_OUTPUT_DIRECTORY "${INFERENCEOS_ARTIFACT_DIRECTORY}"
            LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/loader.ld"
    )
    set(_inferenceos_efi_coff_directory "${CMAKE_CURRENT_BINARY_DIR}/uefi-coff")
    set(_inferenceos_efi_coff_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/main.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/elf.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/modules.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/sha256.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/io.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/memory.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/string.c"
    )
    set(_inferenceos_efi_coff_objects)
    foreach(_efi_source IN LISTS _inferenceos_efi_coff_sources)
        get_filename_component(_efi_name "${_efi_source}" NAME_WE)
        set(_efi_object "${_inferenceos_efi_coff_directory}/${_efi_name}.obj")
        add_custom_command(
            OUTPUT "${_efi_object}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_inferenceos_efi_coff_directory}"
            COMMAND
                "${INFERENCEOS_EFI_CLANG}"
                --target=x86_64-pc-windows-msvc
                -std=c17 -ffreestanding -fno-builtin -fno-stack-protector -fshort-wchar
                -mno-red-zone -mno-mmx -mno-sse -mno-sse2
                -Wall -Wextra -Wpedantic -Werror
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include"
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/include"
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/include"
                -I "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi"
                -c "${_efi_source}" -o "${_efi_object}"
            DEPENDS
                "${_efi_source}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/loader.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi/uefi.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include/inferenceos/boot_info.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include/inferenceos/compiler.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include/inferenceos/system_module.h"
            COMMENT "Compiling ${_efi_name}.c for the PE32+ UEFI ABI"
            VERBATIM
        )
        list(APPEND _inferenceos_efi_coff_objects "${_efi_object}")
    endforeach()
    add_custom_command(
        OUTPUT "${INFERENCEOS_UEFI_LOADER_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${INFERENCEOS_ARTIFACT_DIRECTORY}"
        COMMAND
            "${INFERENCEOS_EFI_LLD_LINK}"
            /subsystem:efi_application
            /entry:efi_main
            /nodefaultlib
            /machine:x64
            /base:0x10000000
            /dynamicbase
            "/map:${INFERENCEOS_ARTIFACT_DIRECTORY}/BOOTX64.map"
            "/out:${INFERENCEOS_UEFI_LOADER_FILE}"
            ${_inferenceos_efi_coff_objects}
        BYPRODUCTS "${INFERENCEOS_ARTIFACT_DIRECTORY}/BOOTX64.map"
        DEPENDS inferenceos_uefi_loader ${_inferenceos_efi_coff_objects}
        COMMENT "Linking the relocatable PE32+ InferenceOS EFI application"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    add_custom_target(inferenceos-uefi-image DEPENDS "${INFERENCEOS_UEFI_LOADER_FILE}")
endif()

add_library(
    inferenceos_memory
    STATIC
        src/kernel/memory/physical.c
        src/kernel/memory/virtual.c
)
add_library(InferenceOS::Memory ALIAS inferenceos_memory)
target_link_libraries(
    inferenceos_memory
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::KernelHeaders
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_memory)

add_library(
    inferenceos_process
    STATIC
        src/boot/system_modules/descriptor.c
        src/kernel/process/application_bindings.c
        src/kernel/process/handle_table.c
        src/kernel/process/loader.c
        src/kernel/process/process.c
        src/kernel/process/type_capability.c
        src/kernel/process/type_catalog.c
)
add_library(InferenceOS::Process ALIAS inferenceos_process)
target_link_libraries(
    inferenceos_process
    PUBLIC
        InferenceOS::KernelHeaders
        InferenceOS::Memory
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_process)

add_library(
    inferenceos_scheduler
    STATIC
        src/kernel/scheduler/scheduler.c
        src/kernel/scheduler/wait_queue.c
)
add_library(InferenceOS::Scheduler ALIAS inferenceos_scheduler)
target_link_libraries(
    inferenceos_scheduler
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::KernelHeaders
        InferenceOS::Process
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_scheduler)

add_library(
    inferenceos_virtio_blk STATIC
        src/drivers/virtio_blk/virtio_blk.c
        src/drivers/virtio_blk/transport_pci.c
)
add_library(InferenceOS::VirtioBlock ALIAS inferenceos_virtio_blk)
target_include_directories(
    inferenceos_virtio_blk
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/virtio_blk/include"
)
target_link_libraries(
    inferenceos_virtio_blk
    PUBLIC InferenceOS::KernelHeaders InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_virtio_blk)

add_library(
    inferenceos_block STATIC
        src/block/device.c
        src/block/cache.c
        src/block/disk_safety.c
        src/block/platform.c
        src/block/platform_q35.c
        src/block/platform_hyperv.c
        src/drivers/hyperv/protocol.c
        src/drivers/hyperv/ring.c
        src/drivers/hyperv/vmbus.c
        src/drivers/hyperv/storvsc.c
        src/drivers/hyperv/input.c
)
add_library(InferenceOS::Block ALIAS inferenceos_block)
target_include_directories(
    inferenceos_block
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/block/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/hyperv/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/virtio_blk/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/input/include"
)
target_link_libraries(
    inferenceos_block
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::KernelHeaders
        InferenceOS::Memory
        InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_block)
target_link_libraries(
    inferenceos_virtio_blk
    PUBLIC InferenceOS::ArchX86_64 InferenceOS::Block
)

add_library(inferenceos_power STATIC src/kernel/power.c src/kernel/power_uefi.c)
add_library(InferenceOS::Power ALIAS inferenceos_power)
target_link_libraries(
    inferenceos_power
    PUBLIC InferenceOS::Block InferenceOS::KernelHeaders InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_power)

add_library(
    inferenceos_vfs STATIC
        src/vfs/mount.c
        src/vfs/path.c
        src/vfs/directory.c
        src/vfs/file.c
        src/vfs/unmount.c
)
add_library(InferenceOS::Vfs ALIAS inferenceos_vfs)
target_include_directories(
    inferenceos_vfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/vfs/include"
)
target_link_libraries(
    inferenceos_vfs PUBLIC InferenceOS::Block InferenceOS::KernelHeaders InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_vfs)

add_library(
    inferenceos_fs STATIC
        src/filesystems/inferenceos_fs/superblock.c
        src/filesystems/inferenceos_fs/crc32.c
        src/filesystems/inferenceos_fs/hash.c
        src/filesystems/inferenceos_fs/records.c
        src/filesystems/inferenceos_fs/registry_record.c
        src/filesystems/inferenceos_fs/registry.c
        src/filesystems/inferenceos_fs/registry_diagnostics.c
        src/filesystems/inferenceos_fs/fat.c
        src/filesystems/inferenceos_fs/directory.c
        src/filesystems/inferenceos_fs/subdirectory.c
        src/filesystems/inferenceos_fs/file.c
        src/filesystems/inferenceos_fs/file_service.c
        src/filesystems/inferenceos_fs/transaction.c
        src/filesystems/inferenceos_fs/sync.c
        src/filesystems/inferenceos_fs/formatter.c
        src/filesystems/inferenceos_fs/validator.c
        src/filesystems/inferenceos_fs/diagnostic_mount.c
        src/filesystems/inferenceos_fs/diagnostics.c
        src/filesystems/inferenceos_fs/mount.c
)
add_library(InferenceOS::Filesystem ALIAS inferenceos_fs)
target_include_directories(
    inferenceos_fs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_link_libraries(
    inferenceos_fs PUBLIC InferenceOS::Block InferenceOS::Vfs InferenceOS::KernelHeaders InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_fs)

add_library(
    inferenceos_cui_fs
    STATIC
        src/cui/fs_commands.c
        src/cui/file_commands.c
        src/cui/directory_commands.c
        src/cui/diagnostic_commands.c
)
add_library(InferenceOS::CuiFilesystem ALIAS inferenceos_cui_fs)
target_link_libraries(
    inferenceos_cui_fs PUBLIC
        InferenceOS::Cui
        InferenceOS::Filesystem
        InferenceOS::KernelHeaders
        InferenceOS::Runtime
        InferenceOS::DisplaySafe
        InferenceOS::Power
)
inferenceos_configure_freestanding_target(inferenceos_cui_fs)

if(BUILD_TESTING)
    add_library(
        inferenceos_foundational_layout_test
        OBJECT
            tests/unit/layout_assertions_test.c
    )
    target_include_directories(
        inferenceos_foundational_layout_test
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/include"
    )
    target_link_libraries(
        inferenceos_foundational_layout_test
        PRIVATE
            InferenceOS::KernelHeaders
    )
    inferenceos_configure_freestanding_target(inferenceos_foundational_layout_test)
endif()

add_library(inferenceos_ipc STATIC src/kernel/ipc/ipc.c)
add_library(InferenceOS::IPC ALIAS inferenceos_ipc)
target_link_libraries(
    inferenceos_ipc
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::KernelHeaders
        InferenceOS::Process
        InferenceOS::Runtime
        InferenceOS::Scheduler
)
inferenceos_configure_freestanding_target(inferenceos_ipc)

add_library(
    inferenceos_syscall
    STATIC
        src/kernel/syscall/entry.S
        src/kernel/syscall/dispatch.c
        src/kernel/syscall/extension_search.c
        src/kernel/syscall/file_command.c
        src/kernel/syscall/file_view.c
        src/kernel/syscall/fs_diagnostic.c
        src/kernel/syscall/gui_view.c
        src/kernel/syscall/ipc.c
        src/kernel/syscall/usercopy.c
)
add_library(InferenceOS::Syscall ALIAS inferenceos_syscall)
target_link_libraries(
    inferenceos_syscall
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::DisplaySafe
        InferenceOS::Filesystem
        InferenceOS::IPC
        InferenceOS::KernelHeaders
        InferenceOS::Memory
        InferenceOS::Process
        InferenceOS::Runtime
        InferenceOS::Scheduler
        InferenceOS::Vfs
        InferenceOS::Window
)
inferenceos_configure_freestanding_target(inferenceos_syscall)

if(CMAKE_CROSSCOMPILING)
    set(_inferenceos_application_linker_script
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/user/application.ld")
    set(_inferenceos_application_directory
        "${INFERENCEOS_ARTIFACT_DIRECTORY}/applications")
    set(_inferenceos_font_directory "${INFERENCEOS_ARTIFACT_DIRECTORY}/fonts")
    set(_inferenceos_font_file
        "${_inferenceos_font_directory}/inferenceos-console-12x24.alpha4")
    set(_inferenceos_font_c_source "${CMAKE_CURRENT_BINARY_DIR}/generated/inferenceos_font.c")
    set(_inferenceos_font_license "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE.txt")
    set(_inferenceos_font_source
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/inferenceos-console-alpha4.rle.b64")
    inferenceos_powershell_path(
        _inferenceos_ps_build_font
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_alpha4_font.ps1"
    )
    inferenceos_powershell_path(_inferenceos_ps_font_source "${_inferenceos_font_source}")
    inferenceos_powershell_path(_inferenceos_ps_font_file "${_inferenceos_font_file}")
    inferenceos_powershell_path(_inferenceos_ps_font_c_source "${_inferenceos_font_c_source}")
    inferenceos_powershell_path(_inferenceos_ps_font_license "${_inferenceos_font_license}")
    add_custom_command(
        OUTPUT "${_inferenceos_font_file}" "${_inferenceos_font_c_source}"
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_build_font}"
            -SourcePath "${_inferenceos_ps_font_source}"
            -OutputPath "${_inferenceos_ps_font_file}"
            -CSourcePath "${_inferenceos_ps_font_c_source}"
            -LicensePath "${_inferenceos_ps_font_license}"
            -Force
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_alpha4_font.ps1"
            "${_inferenceos_font_source}"
            "${_inferenceos_font_license}"
        COMMENT "Generating the licensed antialiased 12x24 console font asset"
        VERBATIM
    )
    set_source_files_properties("${_inferenceos_font_c_source}" PROPERTIES GENERATED TRUE)

    function(inferenceos_add_system_application target_name output_name source_file)
        add_executable("${target_name}" "${source_file}")
        target_link_libraries("${target_name}" PRIVATE InferenceOS::UserRuntime)
        target_compile_options(
            "${target_name}"
            PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-mcmodel=large>
                $<$<COMPILE_LANGUAGE:C>:-fno-unwind-tables>
                $<$<COMPILE_LANGUAGE:C>:-fno-asynchronous-unwind-tables>
        )
        inferenceos_configure_freestanding_target("${target_name}")
        if(INFERENCEOS_DIRECT_LLD_LINK)
            target_link_options(
                "${target_name}"
                PRIVATE
                    -T "${_inferenceos_application_linker_script}"
                    "-Map=${_inferenceos_application_directory}/${output_name}.map"
                    "SHELL:-z max-page-size=0x1000"
                    "SHELL:-z common-page-size=0x1000"
                    "SHELL:-z noexecstack"
            )
        else()
            target_link_options(
                "${target_name}"
                PRIVATE
                    "LINKER:-T,${_inferenceos_application_linker_script}"
                    "LINKER:-Map,${_inferenceos_application_directory}/${output_name}.map"
                    LINKER:-z,max-page-size=0x1000
                    LINKER:-z,common-page-size=0x1000
                    LINKER:-z,noexecstack
            )
        endif()
        set_target_properties(
            "${target_name}"
            PROPERTIES
                OUTPUT_NAME "${output_name}"
                SUFFIX .elf
                RUNTIME_OUTPUT_DIRECTORY "${_inferenceos_application_directory}"
                LINK_DEPENDS "${_inferenceos_application_linker_script}"
        )
        add_custom_command(
            TARGET "${target_name}"
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}"
                "-DAPPLICATION=$<TARGET_FILE:${target_name}>"
                "-DREADELF=${CMAKE_READELF}"
                "-DNM=${CMAKE_NM}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/validate_user_elf.cmake"
            COMMENT "Validating ${output_name}.elf as a static ring-3 application"
            VERBATIM
        )
        add_custom_command(
            TARGET "${target_name}"
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}"
                "-DNM=${CMAKE_NM}"
                "-DINPUT=$<TARGET_FILE:${target_name}>"
                "-DOUTPUT=${_inferenceos_application_directory}/${output_name}.sym"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/write_symbols.cmake"
            BYPRODUCTS "${_inferenceos_application_directory}/${output_name}.sym"
            COMMENT "Writing sorted symbols for ${output_name}.elf"
            VERBATIM
        )
    endfunction()

    inferenceos_add_system_application(
        inferenceos_shell_application shell src/shell/user_entry.c)
    inferenceos_add_system_application(
        inferenceos_gui_desktop_application gui-desktop src/gui/desktop/user_entry.c)
    inferenceos_add_system_application(
        inferenceos_gui_terminal_application gui-terminal src/gui/terminal/user_entry.c)
    inferenceos_add_system_application(
        inferenceos_file_explorer_application file-explorer src/gui/file_explorer/user_entry.c)
    inferenceos_add_system_application(
        inferenceos_doc_viewer_application doc-viewer
        src/applications/doc_viewer/user_entry.c)
    inferenceos_add_system_application(
        inferenceos_txt_viewer_application txt-viewer
        src/applications/txt_viewer/user_entry.c)
    inferenceos_add_system_application(
        inferenceos_proprietary_test_application proprietary-test
        src/applications/proprietary_test/user_entry.c)
    inferenceos_add_system_application(
        inferenceos_custom_test_application custom-test
        src/applications/custom_test/user_entry.c)
    add_custom_target(
        inferenceos-system-applications
        DEPENDS
            inferenceos_shell_application
            inferenceos_gui_desktop_application
            inferenceos_gui_terminal_application
            inferenceos_file_explorer_application
            inferenceos_doc_viewer_application
            inferenceos_txt_viewer_application
            inferenceos_proprietary_test_application
            inferenceos_custom_test_application
    )

    add_executable(
        inferenceos_kernel
            src/kernel/main.c
            src/kernel/runtime.c
            src/kernel/test_control.c
            src/boot/uefi/sha256.c
            src/arch/x86_64/entry.S
            "${_inferenceos_font_c_source}"
    )
    target_include_directories(
        inferenceos_kernel
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/uefi"
    )
    target_link_libraries(
        inferenceos_kernel
        PRIVATE
            InferenceOS::Boot
            InferenceOS::Block
            InferenceOS::CuiFilesystem
            InferenceOS::DocViewer
            InferenceOS::FileExplorer
            InferenceOS::Filesystem
            InferenceOS::Input
            InferenceOS::IPC
            InferenceOS::KernelDiagnostics
            InferenceOS::Memory
            InferenceOS::Power
            InferenceOS::Process
            InferenceOS::Runtime
            InferenceOS::Scheduler
            InferenceOS::Serial
            InferenceOS::ShellRuntime
            InferenceOS::Syscall
            InferenceOS::TxtViewer
            InferenceOS::Vfs
            InferenceOS::VirtioBlock
            InferenceOS::ArchX86_64
    )
    inferenceos_configure_freestanding_target(inferenceos_kernel)
    if(INFERENCEOS_DIRECT_LLD_LINK)
        target_link_options(
            inferenceos_kernel
            PRIVATE
                -T "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/kernel.ld"
                "-Map=${INFERENCEOS_ARTIFACT_DIRECTORY}/kernel.map"
                "SHELL:-z max-page-size=0x1000"
                "SHELL:-z common-page-size=0x1000"
                "SHELL:-z noexecstack"
        )
    else()
        target_link_options(
            inferenceos_kernel
            PRIVATE
                "LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/kernel.ld"
                "LINKER:-Map,${INFERENCEOS_ARTIFACT_DIRECTORY}/kernel.map"
                LINKER:-z,max-page-size=0x1000
                LINKER:-z,common-page-size=0x1000
                LINKER:-z,noexecstack
        )
    endif()
    set_target_properties(
        inferenceos_kernel
        PROPERTIES
            OUTPUT_NAME kernel
            SUFFIX .elf
            RUNTIME_OUTPUT_DIRECTORY "${INFERENCEOS_ARTIFACT_DIRECTORY}"
            LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/arch/x86_64/kernel.ld"
    )
    add_custom_command(
        TARGET inferenceos_kernel
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}"
            "-DNM=${CMAKE_NM}"
            "-DINPUT=$<TARGET_FILE:inferenceos_kernel>"
            "-DOUTPUT=${INFERENCEOS_ARTIFACT_DIRECTORY}/kernel.sym"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/write_symbols.cmake"
        BYPRODUCTS "${INFERENCEOS_ARTIFACT_DIRECTORY}/kernel.sym"
        COMMENT "Writing sorted kernel symbols"
        VERBATIM
    )

    file(GENERATE OUTPUT "${INFERENCEOS_SYSTEM_MODULE_DEFINITION}" CONTENT [=[
{
  "schema_version": 1,
  "generated_from_targets": true,
  "modules": [
    { "application_identity": 4097, "role": "shell", "required": true, "entry_abi_version": 1, "producer_target": "inferenceos_shell_application", "source": "applications/shell.elf", "esp_path": "/InferenceOS/System/shell.elf" },
    { "application_identity": 4098, "role": "gui_desktop", "required": false, "entry_abi_version": 1, "producer_target": "inferenceos_gui_desktop_application", "source": "applications/gui-desktop.elf", "esp_path": "/InferenceOS/System/gui-desktop.elf" },
    { "application_identity": 4099, "role": "gui_terminal", "required": false, "entry_abi_version": 1, "producer_target": "inferenceos_gui_terminal_application", "source": "applications/gui-terminal.elf", "esp_path": "/InferenceOS/System/gui-terminal.elf" },
    { "application_identity": 4100, "role": "file_explorer", "required": false, "entry_abi_version": 1, "producer_target": "inferenceos_file_explorer_application", "source": "applications/file-explorer.elf", "esp_path": "/InferenceOS/System/file-explorer.elf" },
    { "application_identity": 4101, "role": "test_application", "required": false, "entry_abi_version": 1, "producer_target": "inferenceos_proprietary_test_application", "source": "applications/proprietary-test.elf", "esp_path": "/InferenceOS/System/proprietary-test.elf" },
    { "application_identity": 4102, "role": "test_application", "required": false, "entry_abi_version": 1, "producer_target": "inferenceos_custom_test_application", "source": "applications/custom-test.elf", "esp_path": "/InferenceOS/System/custom-test.elf" },
    { "application_identity": 4103, "role": "test_application", "required": false, "entry_abi_version": 1, "producer_target": "inferenceos_doc_viewer_application", "source": "applications/doc-viewer.elf", "esp_path": "/InferenceOS/System/doc-viewer.elf" },
    { "application_identity": 4104, "role": "test_application", "required": false, "entry_abi_version": 1, "producer_target": "inferenceos_txt_viewer_application", "source": "applications/txt-viewer.elf", "esp_path": "/InferenceOS/System/txt-viewer.elf" }
  ],
  "assets": [
    { "kind": "alpha4_font", "source": "fonts/inferenceos-console-12x24.alpha4", "esp_path": "/InferenceOS/System/inferenceos-console-12x24.alpha4", "license": "OFL-1.1", "license_source": "assets/fonts/LICENSE.txt" }
  ]
}
]=])
endif()

if(INFERENCEOS_BUILD_HOST_TESTS)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Hosted tests require a native compiler configuration.")
    endif()
    add_subdirectory(tests)
endif()

set(_inferenceos_module_directory "${INFERENCEOS_ARTIFACT_DIRECTORY}/system-modules")
set(
    _inferenceos_module_manifest
    "${_inferenceos_module_directory}/InferenceOS/System/modules.manifest"
)
set(
    _inferenceos_module_hashes
    "${_inferenceos_module_directory}/InferenceOS/System/modules.sha256"
)
set(_inferenceos_esp_image "${INFERENCEOS_ARTIFACT_DIRECTORY}/inferenceos-esp.img")
set(_inferenceos_test_disk "${INFERENCEOS_ARTIFACT_DIRECTORY}/inferenceos-persistent.raw")
set(_inferenceos_hyperv_boot_vhdx "${INFERENCEOS_ARTIFACT_DIRECTORY}/inferenceos-hyperv-boot.vhdx")
set(_inferenceos_hyperv_data_vhdx "${INFERENCEOS_ARTIFACT_DIRECTORY}/inferenceos-hyperv-data.vhdx")
set(_inferenceos_qemu_artifacts "${CMAKE_BINARY_DIR}/qemu-tests")
set(_inferenceos_artifact_manifest_artifacts "${CMAKE_BINARY_DIR}/system/artifact-manifest")

inferenceos_powershell_path(
    _inferenceos_ps_build_modules
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_system_modules.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_build_esp
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_esp.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_create_disk
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/create_persistent_disk.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_build_hyperv_boot
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_hyperv_boot_vhdx.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_create_hyperv_data
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/create_hyperv_data_vhdx.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_run
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_run_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_boot_gui_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/boot_gui_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_format_mount_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/format_mount_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_file_explorer_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/file_explorer_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_reboot_persistence_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/reboot_persistence_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_directory_interop_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/directory_interop_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_display_safe_file_commands_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/display_safe_file_commands_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_bootstrap_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/bootstrap_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_qemu_profile_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/qemu_profile_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_artifact_manifest_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/artifact_manifest_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_validate_dependencies
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/validate_dependencies.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_registry_benchmark
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_registry_benchmark.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_registry_benchmark_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/registry_benchmark_report_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_registry_benchmark_qemu_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/registry_benchmark_qemu_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_generate_validation_report
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/generate_validation_report.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_validation_report_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/validation_report_test.ps1"
)
inferenceos_powershell_path(
    _inferenceos_ps_repository_root
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
inferenceos_powershell_path(
    _inferenceos_ps_dependency_validation_evidence
    "${CMAKE_BINARY_DIR}/validation/dependency-boundaries.json"
)
inferenceos_powershell_path(
    _inferenceos_ps_validation_evidence
    "${CMAKE_BINARY_DIR}/validation"
)
inferenceos_powershell_path(
    _inferenceos_ps_validation_report_contract_artifacts
    "${CMAKE_BINARY_DIR}/system/validation-report-contract"
)
inferenceos_powershell_path(
    _inferenceos_ps_qemu_profile_contract_artifacts
    "${CMAKE_BINARY_DIR}/system/qemu-profile-contract"
)
inferenceos_powershell_path(_inferenceos_ps_module_definition "${INFERENCEOS_SYSTEM_MODULE_DEFINITION}")
inferenceos_powershell_path(_inferenceos_ps_module_directory "${_inferenceos_module_directory}")
inferenceos_powershell_path(_inferenceos_ps_loader "${INFERENCEOS_UEFI_LOADER_FILE}")
inferenceos_powershell_path(_inferenceos_ps_kernel "${INFERENCEOS_KERNEL_ELF_FILE}")
inferenceos_powershell_path(_inferenceos_ps_esp "${_inferenceos_esp_image}")
inferenceos_powershell_path(_inferenceos_ps_disk "${_inferenceos_test_disk}")
inferenceos_powershell_path(_inferenceos_ps_hyperv_boot "${_inferenceos_hyperv_boot_vhdx}")
inferenceos_powershell_path(_inferenceos_ps_hyperv_data "${_inferenceos_hyperv_data_vhdx}")
inferenceos_powershell_path(_inferenceos_ps_qemu_artifacts "${_inferenceos_qemu_artifacts}")
inferenceos_powershell_path(
    _inferenceos_ps_registry_benchmark_artifacts
    "${CMAKE_BINARY_DIR}/registry-benchmark"
)
inferenceos_powershell_path(
    _inferenceos_ps_registry_benchmark_contract_artifacts
    "${CMAKE_BINARY_DIR}/system/registry-benchmark-contract"
)
inferenceos_powershell_path(
    _inferenceos_ps_registry_benchmark_trace
    "${INFERENCEOS_REGISTRY_BENCHMARK_TRACE}"
)
inferenceos_powershell_path(
    _inferenceos_ps_registry_benchmark_hardware_counters
    "${INFERENCEOS_REGISTRY_BENCHMARK_HARDWARE_COUNTERS}"
)
inferenceos_powershell_path(
    _inferenceos_ps_artifact_manifest_artifacts
    "${_inferenceos_artifact_manifest_artifacts}"
)
inferenceos_powershell_path(_inferenceos_ps_ovmf_code "${INFERENCEOS_OVMF_CODE_FILE}")
inferenceos_powershell_path(_inferenceos_ps_ovmf_vars "${INFERENCEOS_OVMF_VARS_FILE}")
inferenceos_powershell_path(_inferenceos_ps_qemu "${INFERENCEOS_QEMU_EXECUTABLE}")

add_custom_target(
    inferenceos-validate-dependencies
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_validate_dependencies}"
        -RepositoryRoot "${_inferenceos_ps_repository_root}"
        -SelfTest
        -EvidencePath "${_inferenceos_ps_dependency_validation_evidence}"
    COMMENT "Validating VFS and storage dependency boundaries"
    VERBATIM
)
add_custom_target(
    inferenceos-validation-report
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_generate_validation_report}"
        -RepositoryRoot "${_inferenceos_ps_repository_root}"
        -EvidenceDirectory "${_inferenceos_ps_validation_evidence}"
        -OutputDirectory "${_inferenceos_ps_validation_evidence}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/specs/001-inferenceos/spec.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/specs/001-inferenceos/tasks.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/generate_validation_report.ps1"
    COMMENT "Generating SC-001-SC-020 validation traceability"
    VERBATIM
)
add_custom_target(
    inferenceos-artifact-manifest
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_artifact_manifest_test}"
        -RepositoryRoot "${_inferenceos_ps_repository_root}"
        -ArtifactDirectory "${_inferenceos_ps_artifact_manifest_artifacts}"
    COMMENT "Validating deterministic artifact manifests and reproducibility checks"
    VERBATIM
)
add_custom_target(
    benchmark-registry-report-contract
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_registry_benchmark_test}"
        -RepositoryRoot "${_inferenceos_ps_repository_root}"
        -ArtifactDirectory "${_inferenceos_ps_registry_benchmark_contract_artifacts}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/registry_benchmark_report_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_registry_benchmark.ps1"
    COMMENT "Validating the registry TCG marker/report contract"
    VERBATIM
)
if(NOT INFERENCEOS_REGISTRY_BENCHMARK_TRACE STREQUAL ""
   AND NOT INFERENCEOS_REGISTRY_BENCHMARK_IMAGE_SHA256 STREQUAL "")
    set(_inferenceos_registry_benchmark_arguments
        -TracePath "${_inferenceos_ps_registry_benchmark_trace}"
        -OutputDirectory "${_inferenceos_ps_registry_benchmark_artifacts}"
        -BuildVersion "${PROJECT_VERSION}"
        -CompilerVersion "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
        -QemuVersion "11.1.0"
        -ImageChecksum "${INFERENCEOS_REGISTRY_BENCHMARK_IMAGE_SHA256}"
    )
    if(NOT INFERENCEOS_REGISTRY_BENCHMARK_HARDWARE_COUNTERS STREQUAL "")
        list(APPEND
            _inferenceos_registry_benchmark_arguments
            -HardwareCounterPath "${_inferenceos_ps_registry_benchmark_hardware_counters}"
        )
    endif()
    add_custom_target(
        benchmark-registry
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_registry_benchmark}"
            ${_inferenceos_registry_benchmark_arguments}
        DEPENDS
            "${INFERENCEOS_REGISTRY_BENCHMARK_TRACE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_registry_benchmark.ps1"
        COMMENT "Generating the matched registry benchmark report"
        VERBATIM
    )
else()
    add_custom_target(
        benchmark-registry
        COMMAND
            "${CMAKE_COMMAND}" -E echo
            "Configure INFERENCEOS_REGISTRY_BENCHMARK_TRACE and INFERENCEOS_REGISTRY_BENCHMARK_IMAGE_SHA256 with matched QEMU TCG evidence."
        COMMAND "${CMAKE_COMMAND}" -E false
        COMMENT "Registry benchmark evidence is not configured"
        VERBATIM
    )
endif()
if(INFERENCEOS_BUILD_HOST_TESTS)
    add_test(
        NAME dependency-validation
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_validate_dependencies}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -SelfTest
            -EvidencePath "${_inferenceos_ps_dependency_validation_evidence}"
    )
    set_tests_properties(
        dependency-validation PROPERTIES LABELS "host;contract;architecture;vfs;storage"
    )
    add_test(
        NAME bootstrap-system
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_bootstrap_test}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -ArtifactDirectory "${CMAKE_BINARY_DIR}/system/bootstrap-contract"
    )
    set_tests_properties(
        bootstrap-system
        PROPERTIES LABELS "host;system;bootstrap;toolchain;reproducibility"
    )
    add_test(
        NAME qemu-profile-system
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_qemu_profile_test}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -ArtifactDirectory "${_inferenceos_ps_qemu_profile_contract_artifacts}"
    )
    set_tests_properties(
        qemu-profile-system
        PROPERTIES LABELS "host;system;qemu;q35;test-control;wsl"
    )
    add_test(
        NAME artifact-manifest-system
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_artifact_manifest_test}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -ArtifactDirectory "${_inferenceos_ps_artifact_manifest_artifacts}"
    )
    set_tests_properties(
        artifact-manifest-system
        PROPERTIES LABELS "host;system;artifact;manifest;reproducibility"
    )
    add_test(
        NAME registry-benchmark-report-system
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_registry_benchmark_test}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -ArtifactDirectory "${_inferenceos_ps_registry_benchmark_contract_artifacts}"
    )
    set_tests_properties(
        registry-benchmark-report-system
        PROPERTIES LABELS "host;system;benchmark;registry;tcg;report;reproducibility"
    )
    add_test(
        NAME validation-report-system
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_validation_report_test}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -ArtifactDirectory "${_inferenceos_ps_validation_report_contract_artifacts}"
    )
    set_tests_properties(
        validation-report-system
        PROPERTIES LABELS "host;system;validation;traceability;report;reproducibility"
    )
endif()

set(_inferenceos_module_dependencies
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_system_modules.ps1"
    "${INFERENCEOS_SYSTEM_MODULE_DEFINITION}"
)
if(TARGET inferenceos-system-applications)
    list(APPEND
        _inferenceos_module_dependencies
        inferenceos-system-applications
        "${_inferenceos_font_file}"
        "${_inferenceos_application_directory}/shell.elf"
        "${_inferenceos_application_directory}/gui-desktop.elf"
        "${_inferenceos_application_directory}/gui-terminal.elf"
        "${_inferenceos_application_directory}/file-explorer.elf"
        "${_inferenceos_application_directory}/doc-viewer.elf"
        "${_inferenceos_application_directory}/txt-viewer.elf"
        "${_inferenceos_application_directory}/proprietary-test.elf"
        "${_inferenceos_application_directory}/custom-test.elf"
    )
endif()
set(_inferenceos_esp_dependencies
    "${_inferenceos_module_manifest}"
    "${_inferenceos_module_hashes}"
    "${INFERENCEOS_UEFI_LOADER_FILE}"
    "${INFERENCEOS_KERNEL_ELF_FILE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_esp.ps1"
)
if(TARGET inferenceos_kernel)
    list(APPEND _inferenceos_esp_dependencies inferenceos_kernel)
endif()

add_custom_command(
    OUTPUT "${_inferenceos_module_manifest}"
    BYPRODUCTS
        "${_inferenceos_module_hashes}"
        "${_inferenceos_module_directory}/InferenceOS/System/inferenceos-console-12x24.alpha4"
        "${_inferenceos_module_directory}/InferenceOS/System/shell.elf"
        "${_inferenceos_module_directory}/InferenceOS/System/gui-desktop.elf"
        "${_inferenceos_module_directory}/InferenceOS/System/gui-terminal.elf"
        "${_inferenceos_module_directory}/InferenceOS/System/file-explorer.elf"
        "${_inferenceos_module_directory}/InferenceOS/System/doc-viewer.elf"
        "${_inferenceos_module_directory}/InferenceOS/System/txt-viewer.elf"
        "${_inferenceos_module_directory}/InferenceOS/System/proprietary-test.elf"
        "${_inferenceos_module_directory}/InferenceOS/System/custom-test.elf"
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_build_modules}"
        -DefinitionPath "${_inferenceos_ps_module_definition}"
        -OutputDirectory "${_inferenceos_ps_module_directory}"
        -Force
    DEPENDS ${_inferenceos_module_dependencies}
    COMMENT "Packaging version-1 InferenceOS system modules"
    VERBATIM
)
add_custom_target(inferenceos-system-modules DEPENDS "${_inferenceos_module_manifest}")

add_custom_command(
    OUTPUT "${_inferenceos_esp_image}"
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_build_esp}"
        -LoaderPath "${_inferenceos_ps_loader}"
        -KernelPath "${_inferenceos_ps_kernel}"
        -ModuleDirectory "${_inferenceos_ps_module_directory}"
        -OutputPath "${_inferenceos_ps_esp}"
        -Force
    DEPENDS ${_inferenceos_esp_dependencies}
    COMMENT "Building the minimum FAT32 InferenceOS ESP"
    VERBATIM
)
add_custom_target(inferenceos-image DEPENDS "${_inferenceos_esp_image}")
if(TARGET inferenceos-uefi-image)
    add_dependencies(inferenceos-image inferenceos-uefi-image)
endif()
if(TARGET inferenceos_kernel)
    add_dependencies(
        inferenceos-image
        inferenceos_kernel
        inferenceos-system-applications
        inferenceos-system-modules
    )
endif()

add_custom_command(
    OUTPUT "${_inferenceos_test_disk}"
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_create_disk}"
        -OutputPath "${_inferenceos_ps_disk}"
        -Force
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/create_persistent_disk.ps1"
    COMMENT "Creating the sparse 64 GiB InferenceOS persistent test disk"
    VERBATIM
)
add_custom_target(inferenceos-test-disk DEPENDS "${_inferenceos_test_disk}")

add_custom_command(
    OUTPUT "${_inferenceos_hyperv_boot_vhdx}"
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_build_hyperv_boot}"
        -LoaderPath "${_inferenceos_ps_loader}"
        -KernelPath "${_inferenceos_ps_kernel}"
        -ModuleDirectory "${_inferenceos_ps_module_directory}"
        -OutputPath "${_inferenceos_ps_hyperv_boot}"
        -Force
    DEPENDS
        inferenceos-image
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_hyperv_boot_vhdx.ps1"
    COMMENT "Building the Generation 2 Hyper-V GPT/ESP boot VHDX (requires elevated Windows PowerShell)"
    VERBATIM
)
add_custom_command(
    OUTPUT "${_inferenceos_hyperv_data_vhdx}"
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_create_hyperv_data}"
        -OutputPath "${_inferenceos_ps_hyperv_data}"
        -Force
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/create_hyperv_data_vhdx.ps1"
    COMMENT "Creating the unpartitioned 64 GiB Hyper-V data VHDX"
    VERBATIM
)
add_custom_target(
    inferenceos-hyperv-images
    DEPENDS "${_inferenceos_hyperv_boot_vhdx}" "${_inferenceos_hyperv_data_vhdx}"
)

set(_inferenceos_qemu_common_arguments
    -EspPath "${_inferenceos_ps_esp}"
    -PersistentDiskPath "${_inferenceos_ps_disk}"
)
if(NOT INFERENCEOS_OVMF_CODE_FILE STREQUAL "")
    list(APPEND _inferenceos_qemu_common_arguments -OvmfCodePath "${_inferenceos_ps_ovmf_code}")
endif()
if(NOT INFERENCEOS_OVMF_VARS_FILE STREQUAL "")
    list(APPEND _inferenceos_qemu_common_arguments -OvmfVarsPath "${_inferenceos_ps_ovmf_vars}")
endif()
if(NOT INFERENCEOS_QEMU_EXECUTABLE STREQUAL "")
    list(APPEND _inferenceos_qemu_common_arguments -QemuPath "${_inferenceos_ps_qemu}")
endif()

add_custom_target(
    inferenceos-run
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_run}"
        ${_inferenceos_qemu_common_arguments}
    DEPENDS inferenceos-image inferenceos-test-disk
    USES_TERMINAL
    VERBATIM
)

function(inferenceos_add_qemu_marker_test target_name run_name required_marker)
    add_custom_target(
        "${target_name}"
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_run_tests}"
            ${_inferenceos_qemu_common_arguments}
            -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
            -RunName "${run_name}"
            -TimeoutSeconds "${INFERENCEOS_QEMU_TIMEOUT_SECONDS}"
            -RequiredMarker "${required_marker}"
        DEPENDS
            inferenceos-image
            inferenceos-test-disk
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
        USES_TERMINAL
        VERBATIM
    )
endfunction()

inferenceos_add_qemu_marker_test(test-boot boot INFERENCEOS:CUI_READY)
add_custom_target(
    test-gui
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_boot_gui_tests}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
        -RunCount 20
        -TimeoutSeconds "${INFERENCEOS_QEMU_TIMEOUT_SECONDS}"
    DEPENDS
        inferenceos-image
        inferenceos-test-disk
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/boot_gui_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(
    test-format-mount
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_format_mount_test}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
        -TimeoutSeconds "${INFERENCEOS_QEMU_TIMEOUT_SECONDS}"
    DEPENDS
        inferenceos-image
        inferenceos-test-disk
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/format_mount_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(
    test-file-explorer
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_file_explorer_test}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
        -TimeoutSeconds "${INFERENCEOS_QEMU_TIMEOUT_SECONDS}"
    DEPENDS
        inferenceos-image
        inferenceos-test-disk
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/file_explorer_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(
    test-reboot-persistence
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_reboot_persistence_test}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
        -CycleCount 20
        -TimeoutSeconds "${INFERENCEOS_QEMU_TIMEOUT_SECONDS}"
    DEPENDS
        inferenceos-image
        inferenceos-test-disk
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/reboot_persistence_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(
    test-directory-interop
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_directory_interop_test}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
        -TimeoutSeconds "${INFERENCEOS_QEMU_TIMEOUT_SECONDS}"
    DEPENDS
        inferenceos-image
        inferenceos-test-disk
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/directory_interop_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(
    test-display-safe-file-commands
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_display_safe_file_commands_test}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
        -TimeoutSeconds 120
    DEPENDS
        inferenceos-image
        inferenceos-test-disk
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/display_safe_file_commands_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(
    inferenceos-qemu-tests
    DEPENDS
        test-gui
        test-format-mount
        test-reboot-persistence
        test-directory-interop
        test-display-safe-file-commands
)
add_custom_target(
    benchmark-registry-qemu
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_registry_benchmark_qemu_test}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_registry_benchmark_artifacts}"
    DEPENDS
        inferenceos-image
        inferenceos-test-disk
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/registry_benchmark_qemu_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_registry_benchmark.ps1"
    COMMENT "Running the matched registry benchmark under QEMU TCG"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(
    inferenceos-release-validation
    COMMAND
        "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
        -NoProfile
        -File "${_inferenceos_ps_run_tests}"
        ${_inferenceos_qemu_common_arguments}
        -ArtifactDirectory "${_inferenceos_ps_qemu_artifacts}"
        -ReleaseMatrix
        -RetainSuccessfulArtifacts
    DEPENDS
        inferenceos-artifact-manifest
        inferenceos-image
        inferenceos-test-disk
        inferenceos-validate-dependencies
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/boot_gui_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/directory_interop_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/display_safe_file_commands_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/fault_matrix_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/file_explorer_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/format_mount_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/registry_benchmark_qemu_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/reboot_persistence_test.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_inferenceos.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/test/run_qemu_tests.ps1"
    COMMENT "Running the aggregate InferenceOS release-validation matrix"
    USES_TERMINAL
    VERBATIM
)

unset(_inferenceos_module_directory)
unset(_inferenceos_module_manifest)
unset(_inferenceos_module_hashes)
unset(_inferenceos_esp_image)
unset(_inferenceos_test_disk)
unset(_inferenceos_qemu_artifacts)
unset(_inferenceos_artifact_manifest_artifacts)
unset(_inferenceos_qemu_common_arguments)
unset(_inferenceos_registry_benchmark_arguments)
unset(_inferenceos_ps_directory_interop_test)
unset(_inferenceos_ps_display_safe_file_commands_test)
unset(_inferenceos_module_dependencies)
unset(_inferenceos_esp_dependencies)
unset(_inferenceos_ps_build_modules)
unset(_inferenceos_ps_build_esp)
unset(_inferenceos_ps_create_disk)
unset(_inferenceos_ps_run)
unset(_inferenceos_ps_run_tests)
unset(_inferenceos_ps_boot_gui_tests)
unset(_inferenceos_ps_format_mount_test)
unset(_inferenceos_ps_file_explorer_test)
unset(_inferenceos_ps_bootstrap_test)
unset(_inferenceos_ps_qemu_profile_test)
unset(_inferenceos_ps_qemu_profile_contract_artifacts)
unset(_inferenceos_ps_artifact_manifest_test)
unset(_inferenceos_ps_reboot_persistence_test)
unset(_inferenceos_ps_dependency_validation_evidence)
unset(_inferenceos_ps_validation_evidence)
unset(_inferenceos_ps_validation_report_contract_artifacts)
unset(_inferenceos_ps_generate_validation_report)
unset(_inferenceos_ps_validation_report_test)
unset(_inferenceos_ps_registry_benchmark)
unset(_inferenceos_ps_registry_benchmark_test)
unset(_inferenceos_ps_registry_benchmark_qemu_test)
unset(_inferenceos_ps_module_definition)
unset(_inferenceos_ps_module_directory)
unset(_inferenceos_ps_loader)
unset(_inferenceos_ps_kernel)
unset(_inferenceos_ps_esp)
unset(_inferenceos_ps_disk)
unset(_inferenceos_ps_qemu_artifacts)
unset(_inferenceos_ps_artifact_manifest_artifacts)
unset(_inferenceos_ps_registry_benchmark_artifacts)
unset(_inferenceos_ps_registry_benchmark_contract_artifacts)
unset(_inferenceos_ps_registry_benchmark_trace)
unset(_inferenceos_ps_registry_benchmark_hardware_counters)
unset(_inferenceos_ps_ovmf_code)
unset(_inferenceos_ps_ovmf_vars)
unset(_inferenceos_ps_qemu)
