#include <inferenceos/fault_injector.h>

#include <string.h>

static bool operation_is_valid(enum ios_fault_operation operation)
{
    return operation >= IOS_FAULT_ALLOCATE && operation <= IOS_FAULT_GUI;
}

void fault_injector_initialize(struct ios_fault_injector *injector)
{
    if (injector != NULL) {
        memset(injector, 0, sizeof(*injector));
    }
}

ios_status fault_injector_add(
    struct ios_fault_injector *injector,
    enum ios_fault_operation operation,
    ios_u64 first_occurrence,
    ios_u64 interval,
    ios_u64 failure_count,
    ios_status status
)
{
    if (injector == NULL || !operation_is_valid(operation) || first_occurrence == 0
        || interval == 0 || failure_count == 0 || status >= IOS_OK) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < IOS_FAULT_MAX_RULES; ++index) {
        if (!injector->rules[index].enabled) {
            injector->rules[index] = (struct ios_fault_rule){
                .operation = operation,
                .first_occurrence = first_occurrence,
                .interval = interval,
                .remaining_failures = failure_count,
                .status = status,
                .enabled = true
            };
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NO_SPACE);
}

ios_status fault_injector_fail_once(
    struct ios_fault_injector *injector,
    enum ios_fault_operation operation,
    ios_u64 occurrence,
    ios_status status
)
{
    return fault_injector_add(injector, operation, occurrence, 1, 1, status);
}

ios_status fault_injector_check(
    struct ios_fault_injector *injector,
    enum ios_fault_operation operation
)
{
    ios_u64 occurrence;
    if (injector == NULL) {
        return IOS_OK;
    }
    if (!operation_is_valid(operation)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    occurrence = ++injector->occurrences[(ios_size)operation];
    for (ios_size index = 0; index < IOS_FAULT_MAX_RULES; ++index) {
        struct ios_fault_rule *rule = &injector->rules[index];
        if (rule->enabled && rule->operation == operation
            && occurrence >= rule->first_occurrence
            && (occurrence - rule->first_occurrence) % rule->interval == 0) {
            ios_status status = rule->status;
            if (--rule->remaining_failures == 0) {
                rule->enabled = false;
            }
            return status;
        }
    }
    return IOS_OK;
}

ios_u64 fault_injector_occurrences(
    const struct ios_fault_injector *injector,
    enum ios_fault_operation operation
)
{
    return injector != NULL && operation_is_valid(operation)
        ? injector->occurrences[(ios_size)operation] : 0;
}

void fault_injector_clear(struct ios_fault_injector *injector)
{
    fault_injector_initialize(injector);
}
