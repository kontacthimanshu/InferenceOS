#ifndef INFERENCEOS_PANIC_H
#define INFERENCEOS_PANIC_H

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/base.h>

void ios_panic_initialize(void);
_Noreturn void ios_panic(const char *message);
_Noreturn void ios_panic_interrupt(const struct x86_64_interrupt_frame *frame);

#endif
