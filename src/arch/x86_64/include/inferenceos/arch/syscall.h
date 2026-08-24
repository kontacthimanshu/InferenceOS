#ifndef INFERENCEOS_ARCH_SYSCALL_H
#define INFERENCEOS_ARCH_SYSCALL_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

ios_status x86_64_syscall_configure(ios_uptr entry_point);

#endif
