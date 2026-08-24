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

set(
    INFERENCEOS_ARTIFACT_DIRECTORY
    "${CMAKE_BINARY_DIR}/artifacts"
    CACHE PATH
    "Directory for packaged InferenceOS images"
)
set(
    INFERENCEOS_UEFI_LOADER_FILE
    "${INFERENCEOS_ARTIFACT_DIRECTORY}/BOOTX64.EFI"
    CACHE FILEPATH
    "Project-owned PE32+ UEFI loader supplied to the ESP builder"
)
set(
    INFERENCEOS_KERNEL_ELF_FILE
    "${INFERENCEOS_ARTIFACT_DIRECTORY}/kernel.elf"
    CACHE FILEPATH
    "InferenceOS kernel ELF supplied to the ESP builder"
)
set(
    INFERENCEOS_SYSTEM_MODULE_DEFINITION
    "${INFERENCEOS_ARTIFACT_DIRECTORY}/system_modules.json"
    CACHE FILEPATH
    "Version-1 system-module packaging definition"
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
set(
    INFERENCEOS_QEMU_TIMEOUT_SECONDS
    60
    CACHE STRING
    "Foundational QEMU marker-test timeout"
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
)
target_link_libraries(inferenceos_kernel_headers INTERFACE InferenceOS::Freestanding)

add_library(
    inferenceos_arch_x86_64
    STATIC
        src/arch/x86_64/entry.S
        src/arch/x86_64/acpi.c
        src/arch/x86_64/apic_timer.c
        src/arch/x86_64/gdt.c
        src/arch/x86_64/interrupts.c
        src/arch/x86_64/io.c
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
            "/out:${INFERENCEOS_UEFI_LOADER_FILE}"
            ${_inferenceos_efi_coff_objects}
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

add_library(inferenceos_virtio_blk STATIC src/drivers/virtio_blk/virtio_blk.c)
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

add_library(inferenceos_block STATIC src/block/device.c src/block/cache.c)
add_library(InferenceOS::Block ALIAS inferenceos_block)
target_include_directories(
    inferenceos_block PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/block/include"
)
target_link_libraries(
    inferenceos_block PUBLIC InferenceOS::KernelHeaders InferenceOS::Runtime
)
inferenceos_configure_freestanding_target(inferenceos_block)

add_library(inferenceos_power STATIC src/kernel/power.c)
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
        src/filesystems/inferenceos_fs/fat.c
        src/filesystems/inferenceos_fs/directory.c
        src/filesystems/inferenceos_fs/subdirectory.c
        src/filesystems/inferenceos_fs/file.c
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

add_library(
    inferenceos_syscall
    STATIC
        src/kernel/syscall/entry.S
        src/kernel/syscall/dispatch.c
        src/kernel/syscall/file_view.c
        src/kernel/syscall/fs_diagnostic.c
        src/kernel/syscall/gui_view.c
        src/kernel/syscall/usercopy.c
)
add_library(InferenceOS::Syscall ALIAS inferenceos_syscall)
target_link_libraries(
    inferenceos_syscall
    PUBLIC
        InferenceOS::ArchX86_64
        InferenceOS::DisplaySafe
        InferenceOS::Filesystem
        InferenceOS::KernelHeaders
        InferenceOS::Memory
        InferenceOS::Process
        InferenceOS::Runtime
        InferenceOS::Scheduler
        InferenceOS::Vfs
        InferenceOS::Window
)
inferenceos_configure_freestanding_target(inferenceos_syscall)

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
set(_inferenceos_esp_image "${INFERENCEOS_ARTIFACT_DIRECTORY}/inferenceos-esp.img")
set(_inferenceos_test_disk "${INFERENCEOS_ARTIFACT_DIRECTORY}/inferenceos-persistent.raw")
set(_inferenceos_qemu_artifacts "${CMAKE_BINARY_DIR}/qemu-tests")

function(inferenceos_powershell_path output_name input_path)
    if(input_path STREQUAL "")
        set("${output_name}" "" PARENT_SCOPE)
        return()
    endif()
    if(UNIX AND INFERENCEOS_POWERSHELL_EXECUTABLE MATCHES "[.]exe$")
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
    _inferenceos_ps_bootstrap_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/system/bootstrap_test.ps1"
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
    _inferenceos_ps_repository_root
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
inferenceos_powershell_path(_inferenceos_ps_module_definition "${INFERENCEOS_SYSTEM_MODULE_DEFINITION}")
inferenceos_powershell_path(_inferenceos_ps_module_directory "${_inferenceos_module_directory}")
inferenceos_powershell_path(_inferenceos_ps_loader "${INFERENCEOS_UEFI_LOADER_FILE}")
inferenceos_powershell_path(_inferenceos_ps_kernel "${INFERENCEOS_KERNEL_ELF_FILE}")
inferenceos_powershell_path(_inferenceos_ps_esp "${_inferenceos_esp_image}")
inferenceos_powershell_path(_inferenceos_ps_disk "${_inferenceos_test_disk}")
inferenceos_powershell_path(_inferenceos_ps_qemu_artifacts "${_inferenceos_qemu_artifacts}")
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
    COMMENT "Validating VFS and storage dependency boundaries"
    VERBATIM
)
if(INFERENCEOS_BUILD_HOST_TESTS)
    add_test(
        NAME dependency-validation
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_validate_dependencies}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -SelfTest
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
        NAME artifact-manifest-system
        COMMAND
            "${INFERENCEOS_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -File "${_inferenceos_ps_artifact_manifest_test}"
            -RepositoryRoot "${_inferenceos_ps_repository_root}"
            -ArtifactDirectory "${CMAKE_BINARY_DIR}/system/artifact-manifest"
    )
    set_tests_properties(
        artifact-manifest-system
        PROPERTIES LABELS "host;system;artifact;manifest;reproducibility"
    )
endif()

set(_inferenceos_module_dependencies
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_system_modules.ps1"
)
if(EXISTS "${INFERENCEOS_SYSTEM_MODULE_DEFINITION}")
    list(APPEND _inferenceos_module_dependencies "${INFERENCEOS_SYSTEM_MODULE_DEFINITION}")
endif()
set(_inferenceos_esp_dependencies
    "${_inferenceos_module_manifest}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/image/build_esp.ps1"
)
if(EXISTS "${INFERENCEOS_UEFI_LOADER_FILE}")
    list(APPEND _inferenceos_esp_dependencies "${INFERENCEOS_UEFI_LOADER_FILE}")
endif()
if(EXISTS "${INFERENCEOS_KERNEL_ELF_FILE}")
    list(APPEND _inferenceos_esp_dependencies "${INFERENCEOS_KERNEL_ELF_FILE}")
endif()

add_custom_command(
    OUTPUT "${_inferenceos_module_manifest}"
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
    inferenceos-qemu-tests
    DEPENDS test-gui test-format-mount test-reboot-persistence test-directory-interop
)

unset(_inferenceos_module_directory)
unset(_inferenceos_module_manifest)
unset(_inferenceos_esp_image)
unset(_inferenceos_test_disk)
unset(_inferenceos_qemu_artifacts)
unset(_inferenceos_qemu_common_arguments)
unset(_inferenceos_ps_directory_interop_test)
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
unset(_inferenceos_ps_artifact_manifest_test)
unset(_inferenceos_ps_reboot_persistence_test)
unset(_inferenceos_ps_module_definition)
unset(_inferenceos_ps_module_directory)
unset(_inferenceos_ps_loader)
unset(_inferenceos_ps_kernel)
unset(_inferenceos_ps_esp)
unset(_inferenceos_ps_disk)
unset(_inferenceos_ps_qemu_artifacts)
unset(_inferenceos_ps_ovmf_code)
unset(_inferenceos_ps_ovmf_vars)
unset(_inferenceos_ps_qemu)
