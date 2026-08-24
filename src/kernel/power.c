#include <inferenceos/power.h>

#include <inferenceos/runtime.h>

ios_status ios_power_initialize(
    struct ios_power_controller *controller,
    void *transition_context,
    ios_power_transition_function transition)
{
    if (controller == NULL || transition == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(controller, 0, sizeof(*controller));
    controller->transition_context = transition_context;
    controller->transition = transition;
    controller->state = IOS_POWER_READY;
    return IOS_OK;
}

ios_status ios_power_set_filesystem_sync(
    struct ios_power_controller *controller,
    void *sync_context,
    ios_power_sync_function sync)
{
    if (controller == NULL || sync_context == NULL || sync == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (controller->state != IOS_POWER_READY) return IOS_ERROR(IOS_E_BUSY);
    controller->sync_context = sync_context;
    controller->sync = sync;
    return IOS_OK;
}

ios_status ios_power_add_device(
    struct ios_power_controller *controller, struct ios_block_device *device)
{
    if (controller == NULL || device == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (controller->state != IOS_POWER_READY) return IOS_ERROR(IOS_E_BUSY);
    for (ios_size index = 0; index < controller->device_count; ++index) {
        if (controller->devices[index] == device) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    }
    if (controller->device_count == IOS_POWER_MAX_DEVICES) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    controller->devices[controller->device_count++] = device;
    return IOS_OK;
}

ios_status ios_power_request(
    struct ios_power_controller *controller, enum ios_power_action action)
{
    ios_status status;

    if (controller == NULL || controller->transition == NULL
        || (action != IOS_POWER_REBOOT && action != IOS_POWER_SHUTDOWN)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (controller->state != IOS_POWER_READY) return IOS_ERROR(IOS_E_BUSY);

    controller->state = IOS_POWER_FLUSHING;
    if (controller->sync != NULL) {
        status = controller->sync(controller->sync_context);
        if (IOS_FAILED(status)) {
            controller->state = IOS_POWER_READY;
            return status;
        }
    }
    for (ios_size index = 0; index < controller->device_count; ++index) {
        status = block_device_flush(controller->devices[index]);
        if (IOS_FAILED(status)) {
            controller->state = IOS_POWER_READY;
            return status;
        }
    }

    controller->state = IOS_POWER_TRANSITIONING;
    status = controller->transition(controller->transition_context, action);
    if (IOS_FAILED(status)) controller->state = IOS_POWER_READY;
    return status;
}
