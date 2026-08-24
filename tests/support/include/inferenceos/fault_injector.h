#ifndef INFERENCEOS_FAULT_INJECTOR_H
#define INFERENCEOS_FAULT_INJECTOR_H

#include <inferenceos/errors.h>

enum {
    IOS_FAULT_MAX_RULES = 32,
    IOS_FAULT_MAX_OPERATIONS = 32
};

enum ios_fault_operation {
    IOS_FAULT_ALLOCATE = 1,
    IOS_FAULT_BLOCK_READ = 2,
    IOS_FAULT_BLOCK_WRITE = 3,
    IOS_FAULT_BLOCK_FLUSH = 4,
    IOS_FAULT_PERSIST_CONTENT = 5,
    IOS_FAULT_PERSIST_ALLOCATION = 6,
    IOS_FAULT_PERSIST_PRIMARY = 7,
    IOS_FAULT_PERSIST_COMPANION = 8,
    IOS_FAULT_METADATA = 9,
    IOS_FAULT_SYSCALL = 10,
    IOS_FAULT_IPC = 11,
    IOS_FAULT_GUI = 12
};

struct ios_fault_rule {
    enum ios_fault_operation operation;
    ios_u64 first_occurrence;
    ios_u64 interval;
    ios_u64 remaining_failures;
    ios_status status;
    bool enabled;
};

struct ios_fault_injector {
    struct ios_fault_rule rules[IOS_FAULT_MAX_RULES];
    ios_u64 occurrences[IOS_FAULT_MAX_OPERATIONS];
};

void fault_injector_initialize(struct ios_fault_injector *injector);
ios_status fault_injector_add(
    struct ios_fault_injector *injector,
    enum ios_fault_operation operation,
    ios_u64 first_occurrence,
    ios_u64 interval,
    ios_u64 failure_count,
    ios_status status
);
ios_status fault_injector_fail_once(
    struct ios_fault_injector *injector,
    enum ios_fault_operation operation,
    ios_u64 occurrence,
    ios_status status
);
ios_status fault_injector_check(
    struct ios_fault_injector *injector,
    enum ios_fault_operation operation
);
ios_u64 fault_injector_occurrences(
    const struct ios_fault_injector *injector,
    enum ios_fault_operation operation
);
void fault_injector_clear(struct ios_fault_injector *injector);

#endif
