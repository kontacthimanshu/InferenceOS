add_subdirectory(support)

function(inferenceos_add_host_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(
        ${target}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src/kernel/include"
            "${PROJECT_SOURCE_DIR}/src/runtime/include"
            "${PROJECT_SOURCE_DIR}/src/runtime/user/include"
            "${PROJECT_SOURCE_DIR}/src/arch/x86_64/include"
            "${PROJECT_SOURCE_DIR}/src/boot/uefi"
            "${PROJECT_SOURCE_DIR}/src/drivers/ps2/include"
            "${PROJECT_SOURCE_DIR}/src/drivers/framebuffer/include"
            "${PROJECT_SOURCE_DIR}/src/gui/input/include"
            "${PROJECT_SOURCE_DIR}/src/gui/graphics/include"
            "${PROJECT_SOURCE_DIR}/src/gui/window/include"
            "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/include"
            "${PROJECT_SOURCE_DIR}/src/cui/include"
            "${PROJECT_SOURCE_DIR}/src/shell/include"
            "${PROJECT_SOURCE_DIR}/src/gui/desktop/include"
            "${PROJECT_SOURCE_DIR}/src/gui/terminal/include"
    )
    target_link_libraries(${target} PRIVATE InferenceOS::TestMain)
    target_compile_features(${target} PRIVATE c_std_17)
    target_compile_options(
        ${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            $<$<BOOL:${INFERENCEOS_WARNINGS_AS_ERRORS}>:-Werror>
    )
    set_target_properties(
        ${target}
        PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
    )
endfunction()

inferenceos_add_host_test(
    inferenceos_user_runtime_test
    unit/user_runtime_test.c
    "${PROJECT_SOURCE_DIR}/src/runtime/user/syscall.c"
)
add_test(NAME user-runtime-unit COMMAND inferenceos_user_runtime_test)
set_tests_properties(
    user-runtime-unit PROPERTIES LABELS "host;unit;runtime;syscall;ipc;application"
)

inferenceos_add_host_test(
    inferenceos_test_control_protocol_test
    unit/test_control_protocol_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/test_control.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/string.c"
)
add_test(NAME test-control-protocol-unit COMMAND inferenceos_test_control_protocol_test)
set_tests_properties(
    test-control-protocol-unit PROPERTIES LABELS "host;unit;qemu;test-control;protocol"
)

inferenceos_add_host_test(
    inferenceos_user_ipc_syscall_test
    contract/user_ipc_syscall_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/syscall/ipc.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/ipc/ipc.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/scheduler/wait_queue.c"
)
add_test(NAME user-ipc-syscall-contract COMMAND inferenceos_user_ipc_syscall_test)
set_tests_properties(
    user-ipc-syscall-contract
    PROPERTIES LABELS "host;contract;syscall;ipc;usercopy;application"
)

inferenceos_add_host_test(
    inferenceos_scheduler_test
    unit/scheduler_test.c
    "${PROJECT_SOURCE_DIR}/src/arch/x86_64/acpi.c"
    "${PROJECT_SOURCE_DIR}/src/arch/x86_64/apic_timer.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/scheduler/scheduler.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/scheduler/wait_queue.c"
)
add_test(NAME scheduler-unit COMMAND inferenceos_scheduler_test)
set_tests_properties(scheduler-unit PROPERTIES LABELS "host;unit")

inferenceos_add_host_test(
    inferenceos_runtime_test
    unit/runtime_test.c
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/string.c"
)
target_compile_options(inferenceos_runtime_test PRIVATE -fno-builtin)
add_test(NAME runtime-unit COMMAND inferenceos_runtime_test)
set_tests_properties(runtime-unit PROPERTIES LABELS "host;unit;foundation")

inferenceos_add_host_test(
    inferenceos_power_test
    unit/power_test.c
    "${PROJECT_SOURCE_DIR}/src/block/device.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/power.c"
)
target_include_directories(
    inferenceos_power_test PRIVATE "${PROJECT_SOURCE_DIR}/src/block/include"
)
add_test(NAME power-unit COMMAND inferenceos_power_test)
set_tests_properties(power-unit PROPERTIES LABELS "host;unit;kernel;power;durability")

inferenceos_add_host_test(
    inferenceos_vfs_path_test
    unit/vfs_path_test.c
    "${PROJECT_SOURCE_DIR}/src/vfs/path.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
)
target_include_directories(
    inferenceos_vfs_path_test PRIVATE "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME vfs-path-unit COMMAND inferenceos_vfs_path_test)
set_tests_properties(
    vfs-path-unit PROPERTIES LABELS "host;unit;vfs;path;bounds"
)

inferenceos_add_host_test(
    inferenceos_vfs_directory_test
    integration/vfs_directory_test.c
    "${PROJECT_SOURCE_DIR}/src/vfs/path.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/directory.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
)
target_include_directories(
    inferenceos_vfs_directory_test PRIVATE "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME vfs-directory-integration COMMAND inferenceos_vfs_directory_test)
set_tests_properties(
    vfs-directory-integration
    PROPERTIES LABELS "host;integration;vfs;directory;path;coherence;bounds"
)

inferenceos_add_host_test(
    inferenceos_fs_geometry_test
    unit/fs_geometry_test.c
)
target_sources(
    inferenceos_fs_geometry_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/device.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/superblock.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/formatter.c"
)
target_include_directories(
    inferenceos_fs_geometry_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME fs-geometry-unit COMMAND inferenceos_fs_geometry_test)
set_tests_properties(fs-geometry-unit PROPERTIES LABELS "host;unit;filesystem;geometry")

inferenceos_add_host_test(
    inferenceos_superblock_test
    unit/superblock_test.c
)
target_sources(
    inferenceos_superblock_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/superblock.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
)
target_include_directories(
    inferenceos_superblock_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME superblock-unit COMMAND inferenceos_superblock_test)
set_tests_properties(superblock-unit PROPERTIES LABELS "host;unit;filesystem;superblock;codec")

inferenceos_add_host_test(
    inferenceos_fs_metadata_test
    unit/fs_metadata_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
)
target_include_directories(
    inferenceos_fs_metadata_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME fs-metadata-unit COMMAND inferenceos_fs_metadata_test)
set_tests_properties(
    fs-metadata-unit PROPERTIES LABELS "host;unit;filesystem;metadata;codec"
)

inferenceos_add_host_test(
    inferenceos_directory_record_test
    unit/directory_record_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/directory.c"
)
target_include_directories(
    inferenceos_directory_record_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME directory-record-unit COMMAND inferenceos_directory_record_test)
set_tests_properties(
    directory-record-unit PROPERTIES LABELS "host;unit;filesystem;metadata;codec;collision"
)

set(_inferenceos_extension_registry_test_sources
    unit/extension_registry_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/directory.c"
)
foreach(_registry_source registry_record.c registry.c)
    if(EXISTS "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}")
        list(APPEND
            _inferenceos_extension_registry_test_sources
            "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}"
        )
    endif()
endforeach()
inferenceos_add_host_test(
    inferenceos_extension_registry_test
    ${_inferenceos_extension_registry_test_sources}
)
unset(_registry_source)
unset(_inferenceos_extension_registry_test_sources)
target_include_directories(
    inferenceos_extension_registry_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME extension-registry-unit COMMAND inferenceos_extension_registry_test)
set_tests_properties(
    extension-registry-unit
    PROPERTIES LABELS "host;unit;filesystem;registry;metadata;codec;rebuild"
)

inferenceos_add_host_test(
    inferenceos_directory_growth_test
    integration/directory_growth_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/directory.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/subdirectory.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
)
target_include_directories(
    inferenceos_directory_growth_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_compile_options(inferenceos_directory_growth_test PRIVATE -fno-builtin)
add_test(NAME directory-growth-integration COMMAND inferenceos_directory_growth_test)
set_tests_properties(
    directory-growth-integration
    PROPERTIES LABELS "host;integration;filesystem;directory;growth;bounds;metadata"
)

inferenceos_add_host_test(
    inferenceos_fat_chain_test
    unit/fat_chain_test.c
    "${PROJECT_SOURCE_DIR}/src/block/device.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/validator.c"
)
target_include_directories(
    inferenceos_fat_chain_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME fat-chain-unit COMMAND inferenceos_fat_chain_test)
set_tests_properties(
    fat-chain-unit PROPERTIES LABELS "host;unit;filesystem;fat;allocation;corruption"
)

inferenceos_add_host_test(
    inferenceos_file_test
    unit/file_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/file.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
)
target_include_directories(
    inferenceos_file_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME file-unit COMMAND inferenceos_file_test)
set_tests_properties(
    file-unit PROPERTIES LABELS "host;unit;filesystem;file;fat;io"
)

inferenceos_add_host_test(
    inferenceos_file_commit_fault_test
    fault/file_commit_fault_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/transaction.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/registry.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/registry_record.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
)
target_include_directories(
    inferenceos_file_commit_fault_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_link_libraries(inferenceos_file_commit_fault_test PRIVATE InferenceOS::TestSupport)
add_test(NAME file-commit-fault COMMAND inferenceos_file_commit_fault_test)
set_tests_properties(
    file-commit-fault PROPERTIES LABELS "host;fault;filesystem;durability;transaction"
)

inferenceos_add_host_test(
    inferenceos_boot_info_test
    unit/boot_info_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/boot_info.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
)
target_compile_options(inferenceos_boot_info_test PRIVATE -fno-builtin)
add_test(NAME boot-info-unit COMMAND inferenceos_boot_info_test)
set_tests_properties(boot-info-unit PROPERTIES LABELS "host;unit;boot")

inferenceos_add_host_test(
    inferenceos_system_module_manifest_test
    unit/system_module_manifest_test.c
    "${PROJECT_SOURCE_DIR}/src/boot/system_modules/descriptor.c"
    "${PROJECT_SOURCE_DIR}/src/boot/uefi/sha256.c"
)
add_test(NAME system-module-manifest-unit COMMAND inferenceos_system_module_manifest_test)
set_tests_properties(system-module-manifest-unit PROPERTIES LABELS "host;unit;boot")

inferenceos_add_host_test(
    inferenceos_ps2_input_test
    unit/ps2_input_test.c
    "${PROJECT_SOURCE_DIR}/src/drivers/ps2/keyboard.c"
    "${PROJECT_SOURCE_DIR}/src/drivers/ps2/mouse.c"
    "${PROJECT_SOURCE_DIR}/src/gui/input/events.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
)
add_test(NAME ps2-input-unit COMMAND inferenceos_ps2_input_test)
set_tests_properties(ps2-input-unit PROPERTIES LABELS "host;unit;input;gui")

inferenceos_add_host_test(
    inferenceos_graphics_test
    unit/graphics_test.c
    "${PROJECT_SOURCE_DIR}/src/drivers/framebuffer/gop.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/primitives.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/text.c"
)
add_test(NAME graphics-unit COMMAND inferenceos_graphics_test)
set_tests_properties(graphics-unit PROPERTIES LABELS "host;unit;graphics;gui")

inferenceos_add_host_test(
    inferenceos_window_manager_test
    unit/window_manager_test.c
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/primitives.c"
    "${PROJECT_SOURCE_DIR}/src/gui/window/window_manager.c"
    "${PROJECT_SOURCE_DIR}/src/gui/window/compositor.c"
)
add_test(NAME window-manager-unit COMMAND inferenceos_window_manager_test)
set_tests_properties(window-manager-unit PROPERTIES LABELS "host;unit;window;gui")

inferenceos_add_host_test(
    inferenceos_cui_parser_test
    unit/cui_parser_test.c
    "${PROJECT_SOURCE_DIR}/src/cui/parser.c"
    "${PROJECT_SOURCE_DIR}/src/cui/commands.c"
    "${PROJECT_SOURCE_DIR}/src/cui/console.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/string.c"
)
add_test(NAME cui-parser-unit COMMAND inferenceos_cui_parser_test)
set_tests_properties(cui-parser-unit PROPERTIES LABELS "host;unit;cui")

inferenceos_add_host_test(
    inferenceos_process_lifecycle_test
    integration/process_lifecycle_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/process.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/scheduler/scheduler.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/scheduler/wait_queue.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/syscall/dispatch.c"
)
add_test(NAME process-lifecycle-integration COMMAND inferenceos_process_lifecycle_test)
set_tests_properties(process-lifecycle-integration PROPERTIES LABELS "host;integration")

inferenceos_add_host_test(
    inferenceos_gui_runtime_test
    integration/gui_runtime_test.c
)
add_test(NAME gui-runtime-integration COMMAND inferenceos_gui_runtime_test)
set_tests_properties(gui-runtime-integration PROPERTIES LABELS "host;integration;gui")

inferenceos_add_host_test(
    inferenceos_shell_bootstrap_test
    integration/shell_bootstrap_test.c
    "${PROJECT_SOURCE_DIR}/src/shell/bootstrap.c"
    "${PROJECT_SOURCE_DIR}/src/gui/desktop/desktop.c"
    "${PROJECT_SOURCE_DIR}/src/gui/terminal/terminal.c"
    "${PROJECT_SOURCE_DIR}/src/gui/window/window_manager.c"
    "${PROJECT_SOURCE_DIR}/src/gui/window/compositor.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/primitives.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/text.c"
    "${PROJECT_SOURCE_DIR}/src/drivers/framebuffer/gop.c"
    "${PROJECT_SOURCE_DIR}/src/cui/parser.c"
    "${PROJECT_SOURCE_DIR}/src/cui/commands.c"
    "${PROJECT_SOURCE_DIR}/src/cui/console.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/string.c"
)
add_test(NAME shell-bootstrap-integration COMMAND inferenceos_shell_bootstrap_test)
set_tests_properties(shell-bootstrap-integration PROPERTIES LABELS "host;integration;shell;gui;cui")

inferenceos_add_host_test(
    inferenceos_virtio_block_test
    integration/virtio_block_test.c
)
target_sources(
    inferenceos_virtio_block_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/arch/x86_64/pci.c"
        "${PROJECT_SOURCE_DIR}/src/block/device.c"
        "${PROJECT_SOURCE_DIR}/src/drivers/virtio_blk/virtio_blk.c"
        "${PROJECT_SOURCE_DIR}/src/drivers/virtio_blk/transport_pci.c"
)
target_include_directories(
    inferenceos_virtio_block_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/drivers/virtio_blk/include"
)
target_link_libraries(inferenceos_virtio_block_test PRIVATE InferenceOS::TestSupport)
add_test(NAME virtio-block-integration COMMAND inferenceos_virtio_block_test)
set_tests_properties(virtio-block-integration PROPERTIES LABELS "host;integration;block;virtio;storage")

inferenceos_add_host_test(
    inferenceos_block_cache_test
    integration/block_cache_test.c
)
target_sources(
    inferenceos_block_cache_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/device.c"
        "${PROJECT_SOURCE_DIR}/src/block/cache.c"
)
target_include_directories(
    inferenceos_block_cache_test PRIVATE "${PROJECT_SOURCE_DIR}/src/block/include"
)
target_link_libraries(inferenceos_block_cache_test PRIVATE InferenceOS::TestSupport)
add_test(NAME block-cache-integration COMMAND inferenceos_block_cache_test)
set_tests_properties(
    block-cache-integration PROPERTIES LABELS "host;integration;block;cache;storage;durability"
)

inferenceos_add_host_test(
    inferenceos_fs_sync_test
    integration/fs_sync_test.c
    "${PROJECT_SOURCE_DIR}/src/block/device.c"
    "${PROJECT_SOURCE_DIR}/src/block/cache.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/sync.c"
)
target_include_directories(
    inferenceos_fs_sync_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_link_libraries(inferenceos_fs_sync_test PRIVATE InferenceOS::TestSupport)
add_test(NAME fs-sync-integration COMMAND inferenceos_fs_sync_test)
set_tests_properties(
    fs-sync-integration PROPERTIES LABELS "host;integration;filesystem;sync;storage;durability;fault"
)

inferenceos_add_host_test(
    inferenceos_display_safe_entry_test
    contract/display_safe_entry_test.c
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
)
target_include_directories(
    inferenceos_display_safe_entry_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/shell/include"
)
add_test(NAME display-safe-entry-contract COMMAND inferenceos_display_safe_entry_test)
set_tests_properties(
    display-safe-entry-contract PROPERTIES LABELS "host;contract;shell;filesystem;metadata-hiding"
)

inferenceos_add_host_test(
    inferenceos_proprietary_routing_test
    contract/proprietary_routing_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/application_bindings.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_capability.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_catalog.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
)
target_compile_options(inferenceos_proprietary_routing_test PRIVATE -fno-builtin)
add_test(NAME proprietary-routing-contract COMMAND inferenceos_proprietary_routing_test)
set_tests_properties(
    proprietary-routing-contract
    PROPERTIES LABELS "host;contract;kernel;application;capability;metadata-hiding"
)

inferenceos_add_host_test(
    inferenceos_custom_application_test
    contract/custom_application_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/application_bindings.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_capability.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_catalog.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/ipc/ipc.c"
    "${PROJECT_SOURCE_DIR}/src/shell/service.c"
    "${PROJECT_SOURCE_DIR}/src/shell/adapter_service.c"
    "${PROJECT_SOURCE_DIR}/src/applications/custom_test/main.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
)
target_include_directories(
    inferenceos_custom_application_test
    PRIVATE "${PROJECT_SOURCE_DIR}/src/applications/custom_test/include"
)
target_compile_options(inferenceos_custom_application_test PRIVATE -fno-builtin)
add_test(NAME custom-application-contract COMMAND inferenceos_custom_application_test)
set_tests_properties(
    custom-application-contract
    PROPERTIES LABELS "host;contract;shell;application;capability;metadata-hiding;adapter"
)

inferenceos_add_host_test(
    inferenceos_diagnostic_authority_test
    contract/diagnostic_authority_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
)
add_test(NAME diagnostic-authority-contract COMMAND inferenceos_diagnostic_authority_test)
set_tests_properties(
    diagnostic-authority-contract
    PROPERTIES LABELS "host;contract;kernel;filesystem;capability;diagnostic;metadata-hiding"
)

inferenceos_add_host_test(
    inferenceos_diagnostic_bounds_test
    fault/diagnostic_bounds_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
)
target_include_directories(
    inferenceos_diagnostic_bounds_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_compile_options(inferenceos_diagnostic_bounds_test PRIVATE -fno-builtin)
add_test(NAME diagnostic-bounds-fault COMMAND inferenceos_diagnostic_bounds_test)
set_tests_properties(
    diagnostic-bounds-fault
    PROPERTIES LABELS "host;fault;filesystem;diagnostic;bounds;corruption"
)

inferenceos_add_host_test(
    inferenceos_fs_diagnostic_format_test
    unit/fs_diagnostic_format_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/diagnostics.c"
)
target_include_directories(
    inferenceos_fs_diagnostic_format_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
        "${PROJECT_SOURCE_DIR}/src/block/include"
)
add_test(NAME fs-diagnostic-format-unit COMMAND inferenceos_fs_diagnostic_format_test)
set_tests_properties(
    fs-diagnostic-format-unit
    PROPERTIES LABELS "host;unit;filesystem;diagnostic;serial;bounds"
)

inferenceos_add_host_test(
    inferenceos_fs_diagnostic_service_test
    contract/fs_diagnostic_service_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/syscall/fs_diagnostic.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/diagnostic_mount.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/string.c"
)
target_include_directories(
    inferenceos_fs_diagnostic_service_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
target_compile_options(inferenceos_fs_diagnostic_service_test PRIVATE -fno-builtin)
add_test(NAME fs-diagnostic-service-contract COMMAND inferenceos_fs_diagnostic_service_test)
set_tests_properties(
    fs-diagnostic-service-contract
    PROPERTIES LABELS "host;contract;kernel;filesystem;capability;diagnostic;bounds"
)

set(_inferenceos_registry_diagnostics_test_sources
    contract/registry_diagnostics_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/directory.c"
)
foreach(_registry_source registry_record.c registry.c registry_diagnostics.c)
    if(EXISTS "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}")
        list(APPEND
            _inferenceos_registry_diagnostics_test_sources
            "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}"
        )
    endif()
endforeach()
inferenceos_add_host_test(
    inferenceos_registry_diagnostics_test
    ${_inferenceos_registry_diagnostics_test_sources}
)
unset(_registry_source)
unset(_inferenceos_registry_diagnostics_test_sources)
target_include_directories(
    inferenceos_registry_diagnostics_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME registry-diagnostics-contract COMMAND inferenceos_registry_diagnostics_test)
set_tests_properties(
    registry-diagnostics-contract
    PROPERTIES LABELS "host;contract;filesystem;registry;capability;diagnostic;non-mutating"
)

inferenceos_add_host_test(
    inferenceos_shell_ipc_test
    contract/shell_ipc_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/ipc/ipc.c"
    "${PROJECT_SOURCE_DIR}/src/shell/service.c"
)
add_test(NAME shell-ipc-contract COMMAND inferenceos_shell_ipc_test)
set_tests_properties(
    shell-ipc-contract PROPERTIES LABELS "host;contract;shell;ipc;bounds;restart"
)

inferenceos_add_host_test(
    inferenceos_proprietary_viewer_test
    integration/proprietary_viewer_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/application_bindings.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_capability.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_catalog.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/ipc/ipc.c"
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
    "${PROJECT_SOURCE_DIR}/src/shell/proprietary_service.c"
    "${PROJECT_SOURCE_DIR}/src/shell/service.c"
    "${PROJECT_SOURCE_DIR}/src/applications/proprietary_test/main.c"
)
target_include_directories(
    inferenceos_proprietary_viewer_test
    PRIVATE "${PROJECT_SOURCE_DIR}/src/applications/proprietary_test/include"
)
add_test(NAME proprietary-viewer-integration COMMAND inferenceos_proprietary_viewer_test)
set_tests_properties(
    proprietary-viewer-integration
    PROPERTIES LABELS "host;integration;shell;ipc;application;capability;metadata-hiding"
)

inferenceos_add_host_test(
    inferenceos_proprietary_adapter_test
    integration/proprietary_adapter_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/shell/adapter_service.c"
)
add_test(NAME proprietary-adapter-integration COMMAND inferenceos_proprietary_adapter_test)
set_tests_properties(
    proprietary-adapter-integration
    PROPERTIES LABELS "host;integration;shell;application;capability;metadata-hiding;adapter"
)

inferenceos_add_host_test(
    inferenceos_file_view_model_test
    unit/file_view_model_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_catalog.c"
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/primitives.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/text.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/model.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/controller.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/view_provider.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/window.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/properties.c"
)
target_include_directories(
    inferenceos_file_view_model_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/shell/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME file-view-model-unit COMMAND inferenceos_file_view_model_test)
set_tests_properties(
    file-view-model-unit PROPERTIES LABELS "host;unit;gui;file-explorer;metadata-hiding"
)

inferenceos_add_host_test(
    inferenceos_file_explorer_diagnostic_inspector_test
    unit/file_explorer_diagnostic_inspector_test.c
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/model.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/view_provider.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/diagnostic_inspector.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/primitives.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/text.c"
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/syscall/fs_diagnostic.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/diagnostic_mount.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
    "${PROJECT_SOURCE_DIR}/src/runtime/string.c"
)
target_include_directories(
    inferenceos_file_explorer_diagnostic_inspector_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/include"
        "${PROJECT_SOURCE_DIR}/src/shell/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
target_compile_options(
    inferenceos_file_explorer_diagnostic_inspector_test PRIVATE -fno-builtin
)
add_test(
    NAME file-explorer-diagnostic-inspector-unit
    COMMAND inferenceos_file_explorer_diagnostic_inspector_test
)
set_tests_properties(
    file-explorer-diagnostic-inspector-unit
    PROPERTIES LABELS "host;unit;gui;file-explorer;capability;diagnostic;bounds;metadata-hiding"
)

inferenceos_add_host_test(
    inferenceos_shell_file_explorer_test
    integration/shell_file_explorer_test.c
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/model.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/view_provider.c"
)
target_include_directories(
    inferenceos_shell_file_explorer_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME shell-file-explorer-integration COMMAND inferenceos_shell_file_explorer_test)
set_tests_properties(
    shell-file-explorer-integration
    PROPERTIES LABELS "host;integration;shell;file-explorer;vfs;ipc;metadata-hiding;trace"
)

inferenceos_add_host_test(
    inferenceos_file_view_service_test
    integration/file_view_service_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/type_catalog.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/syscall/file_view.c"
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/unmount.c"
)
target_include_directories(
    inferenceos_file_view_service_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME file-view-service-integration COMMAND inferenceos_file_view_service_test)
set_tests_properties(
    file-view-service-integration
    PROPERTIES LABELS "host;integration;kernel;vfs;file-view;metadata-hiding;bounds;collision"
)

inferenceos_add_host_test(
    inferenceos_gui_view_service_test
    integration/gui_view_service_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/syscall/gui_view.c"
    "${PROJECT_SOURCE_DIR}/src/gui/window/window_manager.c"
    "${PROJECT_SOURCE_DIR}/src/gui/window/compositor.c"
    "${PROJECT_SOURCE_DIR}/src/gui/graphics/primitives.c"
)
add_test(NAME gui-view-service-integration COMMAND inferenceos_gui_view_service_test)
set_tests_properties(
    gui-view-service-integration
    PROPERTIES LABELS "host;integration;kernel;gui;window;render;capability;bounds"
)

inferenceos_add_host_test(
    inferenceos_file_explorer_client_test
    integration/file_explorer_client_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/ipc/ipc.c"
    "${PROJECT_SOURCE_DIR}/src/shell/service.c"
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/client.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/model.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/view_provider.c"
)
target_include_directories(
    inferenceos_file_explorer_client_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME file-explorer-client-integration COMMAND inferenceos_file_explorer_client_test)
set_tests_properties(
    file-explorer-client-integration
    PROPERTIES LABELS "host;integration;shell;ipc;file-explorer;refresh;restart;metadata-hiding"
)

set(_inferenceos_registry_file_view_test_sources
    integration/registry_file_view_test.c
    "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
    "${PROJECT_SOURCE_DIR}/src/kernel/ipc/ipc.c"
    "${PROJECT_SOURCE_DIR}/src/shell/service.c"
    "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/client.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/model.c"
    "${PROJECT_SOURCE_DIR}/src/gui/file_explorer/view_provider.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/directory.c"
)
foreach(_registry_source registry_record.c registry.c)
    if(EXISTS "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}")
        list(APPEND
            _inferenceos_registry_file_view_test_sources
            "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}"
        )
    endif()
endforeach()
inferenceos_add_host_test(
    inferenceos_registry_file_view_test
    ${_inferenceos_registry_file_view_test_sources}
)
unset(_registry_source)
unset(_inferenceos_registry_file_view_test_sources)
target_include_directories(
    inferenceos_registry_file_view_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
)
add_test(NAME registry-file-view-integration COMMAND inferenceos_registry_file_view_test)
set_tests_properties(
    registry-file-view-integration
    PROPERTIES LABELS "host;integration;shell;ipc;file-explorer;filesystem;registry;metadata-hiding"
)

set(_inferenceos_registry_benchmark_test_sources
    benchmarks/registry_benchmark.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/directory.c"
)
foreach(_registry_source registry_record.c registry.c)
    if(EXISTS "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}")
        list(APPEND
            _inferenceos_registry_benchmark_test_sources
            "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}"
        )
    endif()
endforeach()
inferenceos_add_host_test(
    inferenceos_registry_benchmark_test
    ${_inferenceos_registry_benchmark_test_sources}
)
unset(_registry_source)
unset(_inferenceos_registry_benchmark_test_sources)
target_include_directories(
    inferenceos_registry_benchmark_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME registry-benchmark-contract COMMAND inferenceos_registry_benchmark_test)
set_tests_properties(
    registry-benchmark-contract
    PROPERTIES LABELS "host;benchmark;filesystem;registry;reproducibility;matched"
)

inferenceos_add_host_test(
    inferenceos_mount_test
    integration/mount_test.c
)
target_sources(
    inferenceos_mount_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/device.c"
        "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
        "${PROJECT_SOURCE_DIR}/src/vfs/unmount.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/superblock.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/formatter.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/validator.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/diagnostic_mount.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/mount.c"
)
target_include_directories(
    inferenceos_mount_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_link_libraries(inferenceos_mount_test PRIVATE InferenceOS::TestSupport)
add_test(NAME mount-integration COMMAND inferenceos_mount_test)
set_tests_properties(
    mount-integration PROPERTIES LABELS "host;integration;filesystem;mount;vfs;storage"
)

inferenceos_add_host_test(
    inferenceos_mount_validation_test
    fault/mount_validation_test.c
    "${PROJECT_SOURCE_DIR}/src/block/device.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/unmount.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/superblock.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/formatter.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/validator.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/diagnostic_mount.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/mount.c"
)
target_include_directories(
    inferenceos_mount_validation_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_link_libraries(inferenceos_mount_validation_test PRIVATE InferenceOS::TestSupport)
add_test(NAME mount-validation-fault COMMAND inferenceos_mount_validation_test)
set_tests_properties(
    mount-validation-fault
    PROPERTIES LABELS "host;fault;filesystem;mount;validation;corruption;storage"
)

set(_inferenceos_registry_fallback_test_sources
    fault/registry_fallback_test.c
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/directory.c"
)
foreach(_registry_source registry_record.c registry.c)
    if(EXISTS "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}")
        list(APPEND
            _inferenceos_registry_fallback_test_sources
            "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/${_registry_source}"
        )
    endif()
endforeach()
inferenceos_add_host_test(
    inferenceos_registry_fallback_test
    ${_inferenceos_registry_fallback_test_sources}
)
unset(_registry_source)
unset(_inferenceos_registry_fallback_test_sources)
target_include_directories(
    inferenceos_registry_fallback_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
add_test(NAME registry-fallback-fault COMMAND inferenceos_registry_fallback_test)
set_tests_properties(
    registry-fallback-fault
    PROPERTIES LABELS "host;fault;filesystem;registry;fallback;reboot;authoritative"
)

inferenceos_add_host_test(
    inferenceos_sync_unmount_test
    fault/sync_unmount_test.c
    "${PROJECT_SOURCE_DIR}/src/block/device.c"
    "${PROJECT_SOURCE_DIR}/src/block/cache.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
    "${PROJECT_SOURCE_DIR}/src/vfs/unmount.c"
    "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/sync.c"
)
target_include_directories(
    inferenceos_sync_unmount_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
)
target_link_libraries(inferenceos_sync_unmount_test PRIVATE InferenceOS::TestSupport)
add_test(NAME sync-unmount-fault COMMAND inferenceos_sync_unmount_test)
set_tests_properties(
    sync-unmount-fault
    PROPERTIES LABELS "host;fault;filesystem;sync;unmount;write;flush;durability;storage"
)

inferenceos_add_host_test(
    inferenceos_fs_commands_test
    integration/fs_commands_test.c
)
target_sources(
    inferenceos_fs_commands_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/cui/parser.c"
        "${PROJECT_SOURCE_DIR}/src/cui/commands.c"
        "${PROJECT_SOURCE_DIR}/src/cui/fs_commands.c"
        "${PROJECT_SOURCE_DIR}/src/kernel/power.c"
        "${PROJECT_SOURCE_DIR}/src/cui/file_commands.c"
        "${PROJECT_SOURCE_DIR}/src/cui/directory_commands.c"
        "${PROJECT_SOURCE_DIR}/src/cui/diagnostic_commands.c"
        "${PROJECT_SOURCE_DIR}/src/kernel/syscall/fs_diagnostic.c"
        "${PROJECT_SOURCE_DIR}/src/kernel/process/handle_table.c"
        "${PROJECT_SOURCE_DIR}/src/shell/display_safe_entry.c"
        "${PROJECT_SOURCE_DIR}/src/block/device.c"
        "${PROJECT_SOURCE_DIR}/src/vfs/mount.c"
        "${PROJECT_SOURCE_DIR}/src/vfs/unmount.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/superblock.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/crc32.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/formatter.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/fat.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/validator.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/diagnostic_mount.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/mount.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/records.c"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/hash.c"
        "${PROJECT_SOURCE_DIR}/src/runtime/memory.c"
        "${PROJECT_SOURCE_DIR}/src/runtime/string.c"
)
target_include_directories(
    inferenceos_fs_commands_test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/cui/include"
        "${PROJECT_SOURCE_DIR}/src/block/include"
        "${PROJECT_SOURCE_DIR}/src/vfs/include"
        "${PROJECT_SOURCE_DIR}/src/filesystems/inferenceos_fs/include"
        "${PROJECT_SOURCE_DIR}/src/shell/include"
)
add_test(NAME fs-commands-integration COMMAND inferenceos_fs_commands_test)
set_tests_properties(
    fs-commands-integration PROPERTIES LABELS "host;integration;cui;filesystem;mount;vfs;storage"
)
