#include <inferenceos/ipc.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/runtime.h>

struct ios_ipc_endpoint {
    bool allocated;
    bool revoked;
    ios_u16 capacity;
    ios_u16 head;
    ios_u16 tail;
    ios_u16 count;
    ios_u32 reference_count;
    ios_u64 owner_process_id;
    ios_u64 owner_application_identity;
    struct ios_wait_queue receivers;
    struct ios_wait_queue senders;
    struct ios_ipc_message messages[IOS_IPC_MAX_QUEUE_DEPTH];
};

struct trusted_service_entry {
    ios_u64 trusted_application_identity;
    ios_u64 generation;
    struct ios_ipc_endpoint *endpoint;
    ios_u64 provider_process_id;
};

static struct ios_ipc_endpoint endpoints[IOS_IPC_MAX_ENDPOINTS];
static struct trusted_service_entry services[IOS_IPC_MAX_SERVICES];
static bool ipc_ready;

IOS_STATIC_ASSERT(sizeof(struct ios_ipc_message_header) == 40,
                  "IPC version-1 header size");
IOS_STATIC_ASSERT(sizeof(struct ios_ipc_message) == 552,
                  "IPC version-1 bounded message size");

static bool service_is_valid(enum ios_trusted_service service)
{
    return service >= IOS_SERVICE_SHELL && service <= IOS_SERVICE_PROPRIETARY_ADAPTER;
}

static void endpoint_retain(void *object)
{
    struct ios_ipc_endpoint *endpoint = object;
    ios_u64 interrupt_flags = x86_64_interrupt_save_disable();
    IOS_ASSERT(endpoint != NULL && endpoint->allocated && endpoint->reference_count != UINT32_MAX);
    ++endpoint->reference_count;
    x86_64_interrupt_restore(interrupt_flags);
}

static void endpoint_release(void *object)
{
    struct ios_ipc_endpoint *endpoint = object;
    ios_u64 interrupt_flags = x86_64_interrupt_save_disable();
    IOS_ASSERT(endpoint != NULL && endpoint->allocated && endpoint->reference_count != 0);
    --endpoint->reference_count;
    if (endpoint->reference_count == 0) {
        (void)wait_queue_wake_all(&endpoint->receivers);
        (void)wait_queue_wake_all(&endpoint->senders);
        memset(endpoint, 0, sizeof(*endpoint));
    }
    x86_64_interrupt_restore(interrupt_flags);
}

static ios_status resolve_endpoint(
    struct ios_process *process,
    ios_handle handle,
    ios_u64 rights,
    struct ios_ipc_endpoint **endpoint
)
{
    void *object;
    ios_status status;
    if (!ipc_ready || process == NULL || endpoint == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = handle_table_resolve(
        &process->handles, handle, IOS_OBJECT_IPC_ENDPOINT, rights, &object
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    *endpoint = object;
    if (!(*endpoint)->allocated || (*endpoint)->revoked) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return IOS_OK;
}

ios_status ipc_initialize(void)
{
    memset(endpoints, 0, sizeof(endpoints));
    memset(services, 0, sizeof(services));
    for (ios_size index = 0; index < IOS_IPC_MAX_SERVICES; ++index) {
        services[index].generation = 1;
    }
    ipc_ready = true;
    return IOS_OK;
}

ios_status ipc_endpoint_create(
    struct ios_process *owner,
    ios_u16 queue_depth,
    ios_handle *handle
)
{
    ios_u64 interrupt_flags;
    if (!ipc_ready || owner == NULL || handle == NULL || owner->process_id == 0
        || queue_depth == 0 || queue_depth > IOS_IPC_MAX_QUEUE_DEPTH) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    interrupt_flags = x86_64_interrupt_save_disable();
    for (ios_size index = 0; index < IOS_IPC_MAX_ENDPOINTS; ++index) {
        struct ios_ipc_endpoint *endpoint = &endpoints[index];
        ios_status status;
        if (endpoint->allocated) {
            continue;
        }
        memset(endpoint, 0, sizeof(*endpoint));
        endpoint->allocated = true;
        endpoint->capacity = queue_depth;
        endpoint->reference_count = 1;
        endpoint->owner_process_id = owner->process_id;
        endpoint->owner_application_identity = owner->application_identity;
        wait_queue_initialize(&endpoint->receivers);
        wait_queue_initialize(&endpoint->senders);
        status = handle_table_insert(
            &owner->handles,
            endpoint,
            IOS_OBJECT_IPC_ENDPOINT,
            IOS_RIGHT_WAIT | IOS_RIGHT_SIGNAL | IOS_RIGHT_DUPLICATE
                | IOS_RIGHT_TRANSFER | IOS_RIGHT_ADMINISTER,
            endpoint_retain,
            endpoint_release,
            handle
        );
        if (IOS_FAILED(status)) {
            memset(endpoint, 0, sizeof(*endpoint));
        }
        x86_64_interrupt_restore(interrupt_flags);
        return status;
    }
    x86_64_interrupt_restore(interrupt_flags);
    return IOS_ERROR(IOS_E_NO_SPACE);
}

ios_status ipc_send(
    struct ios_process *sender,
    ios_handle endpoint_handle,
    ios_u64 request_id,
    ios_u32 operation,
    ios_u32 flags,
    ios_u16 item_count,
    const void *payload,
    ios_u16 payload_size,
    bool block
)
{
    struct ios_ipc_endpoint *endpoint;
    struct ios_ipc_message *message;
    ios_u64 interrupt_flags;
    ios_status status;
    if (request_id == 0 || operation == 0 || flags != 0
        || item_count > IOS_IPC_MAX_ITEM_COUNT || payload_size > IOS_IPC_MAX_PAYLOAD_SIZE
        || (payload_size != 0 && payload == NULL)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    status = resolve_endpoint(sender, endpoint_handle, IOS_RIGHT_SIGNAL, &endpoint);
    if (IOS_FAILED(status)) {
        return status;
    }
    interrupt_flags = x86_64_interrupt_save_disable();
    if (endpoint->count == endpoint->capacity) {
        status = block ? scheduler_block_current(&endpoint->senders)
            : IOS_ERROR(IOS_E_WOULD_BLOCK);
        x86_64_interrupt_restore(interrupt_flags);
        return status;
    }
    message = &endpoint->messages[endpoint->tail];
    memset(message, 0, sizeof(*message));
    message->header.size = (ios_u16)(sizeof(message->header) + payload_size);
    message->header.version = IOS_IPC_ABI_VERSION;
    message->header.operation = operation;
    message->header.request_id = request_id;
    message->header.caller_process_id = sender->process_id;
    message->header.caller_application_identity = sender->application_identity;
    message->header.item_count = item_count;
    message->header.payload_size = payload_size;
    if (payload_size != 0) {
        memcpy(message->payload, payload, payload_size);
    }
    endpoint->tail = (ios_u16)((endpoint->tail + 1U) % endpoint->capacity);
    ++endpoint->count;
    (void)wait_queue_wake_one(&endpoint->receivers);
    x86_64_interrupt_restore(interrupt_flags);
    return IOS_OK;
}

ios_status ipc_receive(
    struct ios_process *receiver,
    ios_handle endpoint_handle,
    struct ios_ipc_message *message,
    bool block
)
{
    struct ios_ipc_endpoint *endpoint;
    ios_u64 interrupt_flags;
    ios_status status;
    if (message == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = resolve_endpoint(receiver, endpoint_handle, IOS_RIGHT_WAIT, &endpoint);
    if (IOS_FAILED(status)) {
        return status;
    }
    if (endpoint->owner_process_id != receiver->process_id) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    interrupt_flags = x86_64_interrupt_save_disable();
    if (endpoint->count == 0) {
        status = block ? scheduler_block_current(&endpoint->receivers)
            : IOS_ERROR(IOS_E_WOULD_BLOCK);
        x86_64_interrupt_restore(interrupt_flags);
        return status;
    }
    *message = endpoint->messages[endpoint->head];
    memset(&endpoint->messages[endpoint->head], 0, sizeof(endpoint->messages[endpoint->head]));
    endpoint->head = (ios_u16)((endpoint->head + 1U) % endpoint->capacity);
    --endpoint->count;
    (void)wait_queue_wake_one(&endpoint->senders);
    x86_64_interrupt_restore(interrupt_flags);
    return IOS_OK;
}

ios_status ipc_trust_service(
    enum ios_trusted_service service,
    ios_u64 application_identity
)
{
    struct trusted_service_entry *entry;
    if (!ipc_ready || !service_is_valid(service) || application_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    entry = &services[(ios_size)service - 1U];
    if (entry->trusted_application_identity != 0
        && entry->trusted_application_identity != application_identity) {
        return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    }
    entry->trusted_application_identity = application_identity;
    return IOS_OK;
}

ios_status ipc_service_register(
    struct ios_process *provider,
    enum ios_trusted_service service,
    ios_handle endpoint_handle
)
{
    struct trusted_service_entry *entry;
    struct ios_ipc_endpoint *endpoint;
    ios_status status;
    if (!ipc_ready || provider == NULL || !service_is_valid(service)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    entry = &services[(ios_size)service - 1U];
    if (entry->trusted_application_identity == 0
        || entry->trusted_application_identity != provider->application_identity) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    status = resolve_endpoint(
        provider, endpoint_handle, IOS_RIGHT_ADMINISTER | IOS_RIGHT_WAIT, &endpoint
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    if (endpoint->owner_process_id != provider->process_id) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    if (entry->endpoint == endpoint) {
        return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    }
    endpoint_retain(endpoint);
    if (entry->endpoint != NULL) {
        entry->endpoint->revoked = true;
        endpoint_release(entry->endpoint);
    }
    entry->endpoint = endpoint;
    entry->provider_process_id = provider->process_id;
    ++entry->generation;
    if (entry->generation == 0) {
        entry->generation = 1;
    }
    return IOS_OK;
}

ios_status ipc_service_connect(
    struct ios_process *client,
    enum ios_trusted_service service,
    ios_handle *endpoint_handle
)
{
    struct trusted_service_entry *entry;
    ios_status status;
    if (!ipc_ready || client == NULL || endpoint_handle == NULL || !service_is_valid(service)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    entry = &services[(ios_size)service - 1U];
    if (entry->endpoint == NULL || entry->endpoint->revoked) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    endpoint_retain(entry->endpoint);
    status = handle_table_insert(
        &client->handles,
        entry->endpoint,
        IOS_OBJECT_IPC_ENDPOINT,
        IOS_RIGHT_SIGNAL | IOS_RIGHT_DUPLICATE | IOS_RIGHT_TRANSFER,
        endpoint_retain,
        endpoint_release,
        endpoint_handle
    );
    if (IOS_FAILED(status)) {
        endpoint_release(entry->endpoint);
    }
    return status;
}

ios_u64 ipc_service_generation(enum ios_trusted_service service)
{
    return ipc_ready && service_is_valid(service)
        ? services[(ios_size)service - 1U].generation : 0;
}

ios_status ipc_service_unregister(
    struct ios_process *provider,
    enum ios_trusted_service service
)
{
    struct trusted_service_entry *entry;
    if (!ipc_ready || provider == NULL || !service_is_valid(service)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    entry = &services[(ios_size)service - 1U];
    if (entry->endpoint == NULL) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    if (entry->provider_process_id != provider->process_id
        || entry->trusted_application_identity != provider->application_identity) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    entry->endpoint->revoked = true;
    endpoint_release(entry->endpoint);
    entry->endpoint = NULL;
    entry->provider_process_id = 0;
    ++entry->generation;
    if (entry->generation == 0) {
        entry->generation = 1;
    }
    return IOS_OK;
}
