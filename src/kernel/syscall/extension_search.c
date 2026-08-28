#include <inferenceos/extension_search.h>

#include <inferenceos/runtime.h>

static bool request_padding_is_zero(const struct ios_extension_search_request *request)
{
    for (ios_size index = request->extension_length;
         index < sizeof(request->extension); ++index) {
        if (request->extension[index] != 0) return false;
    }
    return true;
}

static ios_status canonicalize_extension(
    const struct ios_extension_search_request *request,
    char output[3],
    ios_size *output_length
)
{
    ios_size input = request->extension[0] == '.' ? 1U : 0U;
    ios_size count = 0;
    if (input == request->extension_length) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    while (input < request->extension_length) {
        ios_u8 value = (ios_u8)request->extension[input++];
        if (value >= 'a' && value <= 'z') value = (ios_u8)(value - ('a' - 'A'));
        if (!((value >= 'A' && value <= 'Z')
              || (value >= '0' && value <= '9')
              || value == '_' || value == '-')
            || count == 3) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        output[count++] = (char)value;
    }
    *output_length = count;
    return IOS_OK;
}

ios_status ios_extension_search_service_initialize(
    struct ios_extension_search_service *service,
    struct ios_vfs_mount_registry *mount_registry
)
{
    if (service == NULL || mount_registry == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    service->mount_registry = mount_registry;
    return IOS_OK;
}

ios_status ios_extension_search_dispatch(
    struct ios_extension_search_service *service,
    const struct ios_process *caller,
    const struct ios_extension_search_request *request,
    struct ios_extension_search_reply *reply
)
{
    struct ios_vfs_search_result results[IOS_EXTENSION_SEARCH_RESULT_CAPACITY];
    char canonical_extension[3];
    ios_size canonical_length;
    struct ios_vfs_mount *mount;
    ios_size item_count = 0;
    bool truncated = false;
    ios_status status;

    if (reply != NULL) memset(reply, 0, sizeof(*reply));
    if (service == NULL || caller == NULL || request == NULL || reply == NULL
        || service->mount_registry == NULL
        || caller->process_id == 0 || caller->application_identity == 0
        || request->size != sizeof(*request)
        || request->version != IOS_EXTENSION_SEARCH_ABI_VERSION
        || request->flags != 0 || request->reserved != 0
        || request->extension_length == 0
        || request->extension_length > sizeof(request->extension)
        || !request_padding_is_zero(request)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }

    status = canonicalize_extension(request, canonical_extension, &canonical_length);
    if (IOS_FAILED(status)) return status;
    mount = vfs_root_mount(service->mount_registry);
    if (mount == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);
    memset(results, 0, sizeof(results));
    status = vfs_search_extension(
        mount, canonical_extension, canonical_length,
        results, IOS_ARRAY_COUNT(results), &item_count, &truncated
    );
    if (IOS_FAILED(status)) return status;
    if (item_count > IOS_ARRAY_COUNT(results)) return IOS_ERROR(IOS_E_PROTOCOL);

    reply->size = sizeof(*reply);
    reply->version = IOS_EXTENSION_SEARCH_ABI_VERSION;
    reply->flags = truncated ? IOS_EXTENSION_SEARCH_REPLY_TRUNCATED : 0;
    reply->status = IOS_OK;
    reply->item_count = (ios_u32)item_count;
    for (ios_size index = 0; index < item_count; ++index) {
        struct ios_extension_search_result *destination = &reply->entries[index];
        destination->object_identity = results[index].object_identity;
        destination->location_length = (ios_u16)results[index].display_path_length;
        memcpy(
            destination->location, results[index].display_path,
            results[index].display_path_length + 1U
        );
    }
    return IOS_OK;
}
