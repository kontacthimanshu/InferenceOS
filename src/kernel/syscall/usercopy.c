#include <inferenceos/syscall.h>

#include <inferenceos/runtime.h>

static ios_status copy_user_pages(
    const struct ios_process *process,
    ios_uptr user_address,
    void *kernel_buffer,
    ios_size byte_count,
    bool copy_to_user,
    bool clear_user
)
{
    ios_u8 *kernel_bytes = kernel_buffer;
    ios_size copied = 0;

    if (byte_count == 0) {
        return IOS_OK;
    }
    if (process == NULL || kernel_buffer == NULL
        || !virtual_user_range_is_valid(user_address, byte_count)) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    while (copied < byte_count) {
        ios_uptr physical_address;
        ios_u32 flags;
        const ios_size page_remaining = IOS_PAGE_SIZE
            - (ios_size)((user_address + copied) & (IOS_PAGE_SIZE - 1U));
        const ios_size chunk = byte_count - copied < page_remaining
            ? byte_count - copied : page_remaining;
        ios_status status = virtual_query(
            &process->address_space, user_address + copied, &physical_address, &flags
        );
        if (IOS_FAILED(status) || (flags & IOS_VM_USER) == 0
            || (copy_to_user && (flags & IOS_VM_WRITE) == 0)) {
            return IOS_ERROR(IOS_E_BAD_ADDRESS);
        }
        if (clear_user) {
            memset((void *)physical_address, 0, chunk);
        } else if (copy_to_user) {
            memcpy((void *)physical_address, kernel_bytes + copied, chunk);
        } else {
            memcpy(kernel_bytes + copied, (const void *)physical_address, chunk);
        }
        copied += chunk;
    }
    return IOS_OK;
}

ios_status user_copy_from(
    const struct ios_process *process,
    void *kernel_destination,
    ios_uptr user_source,
    ios_size byte_count
)
{
    return copy_user_pages(
        process, user_source, kernel_destination, byte_count, false, false
    );
}

ios_status user_copy_to(
    const struct ios_process *process,
    ios_uptr user_destination,
    const void *kernel_source,
    ios_size byte_count
)
{
    return copy_user_pages(
        process, user_destination, (void *)kernel_source, byte_count, true, false
    );
}

ios_status user_clear(
    const struct ios_process *process,
    ios_uptr user_destination,
    ios_size byte_count
)
{
    ios_u8 zero = 0;
    return copy_user_pages(process, user_destination, &zero, byte_count, true, true);
}
