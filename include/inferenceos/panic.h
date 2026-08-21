#ifndef INFERENCEOS_PANIC_H
#define INFERENCEOS_PANIC_H

#include <inferenceos/base.h>

#define INFERENCEOS_PANIC_MESSAGE_LIMIT 256U
#define INFERENCEOS_PANIC_EXPRESSION_LIMIT 256U
#define INFERENCEOS_PANIC_FILE_LIMIT 256U

INFERENCEOS_NORETURN void inferenceos_panic(const char *message);

INFERENCEOS_NORETURN void inferenceos_panic_bounded(
    const char *message,
    inferenceos_size message_length
);

#endif
