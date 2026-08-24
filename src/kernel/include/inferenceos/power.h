#ifndef INFERENCEOS_POWER_H
#define INFERENCEOS_POWER_H

#include <inferenceos/block.h>

enum { IOS_POWER_MAX_DEVICES = 8 };

enum ios_power_action {
    IOS_POWER_REBOOT,
    IOS_POWER_SHUTDOWN
};

enum ios_power_state {
    IOS_POWER_READY,
    IOS_POWER_FLUSHING,
    IOS_POWER_TRANSITIONING
};

typedef ios_status (*ios_power_sync_function)(void *context);
typedef ios_status (*ios_power_transition_function)(
    void *context, enum ios_power_action action
);

struct ios_power_controller {
    void *sync_context;
    ios_power_sync_function sync;
    struct ios_block_device *devices[IOS_POWER_MAX_DEVICES];
    ios_size device_count;
    void *transition_context;
    ios_power_transition_function transition;
    enum ios_power_state state;
};

ios_status ios_power_initialize(
    struct ios_power_controller *controller,
    void *transition_context,
    ios_power_transition_function transition
);
ios_status ios_power_set_filesystem_sync(
    struct ios_power_controller *controller,
    void *sync_context,
    ios_power_sync_function sync
);
ios_status ios_power_add_device(
    struct ios_power_controller *controller, struct ios_block_device *device
);
ios_status ios_power_request(
    struct ios_power_controller *controller, enum ios_power_action action
);

#endif
