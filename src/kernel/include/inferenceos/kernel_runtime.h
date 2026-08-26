#ifndef INFERENCEOS_KERNEL_RUNTIME_H
#define INFERENCEOS_KERNEL_RUNTIME_H

#include <inferenceos/boot_info.h>
#include <inferenceos/errors.h>

ios_status ios_kernel_runtime_initialize(const struct ios_boot_info *boot_info);
void ios_kernel_runtime_step(void);
_Noreturn void ios_kernel_runtime_run(void);

#endif
