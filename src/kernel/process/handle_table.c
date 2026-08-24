#include <inferenceos/handle_table.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/runtime.h>

#define HANDLE_SLOT_BITS 16U
#define HANDLE_SLOT_MASK UINT64_C(0xffff)
#define HANDLE_GENERATION_MASK UINT64_C(0x0000ffffffffffff)
#define HANDLE_OWNER_MASK UINT64_C(0x00ffffff)
#define HANDLE_COUNTER_MASK UINT64_C(0x00ffffff)
#define HANDLE_OWNER_SHIFT 24U

static ios_u32 owner_tag_from_identity(ios_u64 identity)
{
    ios_u32 tag = (ios_u32)(identity ^ (identity >> 24) ^ (identity >> 48));
    tag &= (ios_u32)HANDLE_OWNER_MASK;
    return tag == 0 ? 1U : tag;
}

static ios_u64 initial_generation(ios_u32 owner_tag)
{
    return ((ios_u64)owner_tag << HANDLE_OWNER_SHIFT) | UINT64_C(1);
}

static ios_u64 next_generation(ios_u64 generation, ios_u32 owner_tag)
{
    ios_u64 counter = (generation + 1U) & HANDLE_COUNTER_MASK;
    if (counter == 0) {
        counter = 1;
    }
    return ((ios_u64)owner_tag << HANDLE_OWNER_SHIFT) | counter;
}

static ios_handle encode_handle(ios_size index, ios_u64 generation)
{
    return ((generation & HANDLE_GENERATION_MASK) << HANDLE_SLOT_BITS)
        | (ios_u64)(index + 1U);
}

static ios_status decode_handle(
    const struct ios_handle_table *table,
    ios_handle handle,
    ios_size *index
)
{
    const ios_u64 encoded_slot = handle & HANDLE_SLOT_MASK;
    const ios_u64 generation = (handle >> HANDLE_SLOT_BITS) & HANDLE_GENERATION_MASK;

    if (table == NULL || index == NULL || handle == IOS_INVALID_HANDLE
        || encoded_slot == 0 || encoded_slot > IOS_HANDLE_TABLE_CAPACITY) {
        return IOS_ERROR(IOS_E_BAD_HANDLE);
    }
    *index = (ios_size)(encoded_slot - 1U);
    if (table->entries[*index].object == NULL
        || table->entries[*index].generation != generation) {
        return IOS_ERROR(IOS_E_BAD_HANDLE);
    }
    return IOS_OK;
}

static ios_status validate_new_entry(
    const struct ios_handle_table *table,
    void *object,
    enum ios_object_kind kind,
    ios_u64 rights,
    ios_handle *handle
)
{
    if (table == NULL || object == NULL || handle == NULL || table->owner_tag == 0
        || kind <= IOS_OBJECT_NONE || kind > IOS_OBJECT_ADAPTER_CAPABILITY
        || (rights & ~IOS_RIGHT_ALL) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return IOS_OK;
}

ios_status handle_table_initialize(struct ios_handle_table *table, ios_u64 owner_identity)
{
    if (table == NULL || owner_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(table, 0, sizeof(*table));
    table->owner_tag = owner_tag_from_identity(owner_identity);
    for (ios_size index = 0; index < IOS_HANDLE_TABLE_CAPACITY; ++index) {
        table->entries[index].generation = initial_generation(table->owner_tag);
    }
    return IOS_OK;
}

ios_status handle_table_insert(
    struct ios_handle_table *table,
    void *object,
    enum ios_object_kind kind,
    ios_u64 rights,
    ios_object_retain_function retain,
    ios_object_release_function release,
    ios_handle *handle
)
{
    ios_u64 flags;
    ios_status status = validate_new_entry(table, object, kind, rights, handle);
    if (IOS_FAILED(status)) {
        return status;
    }
    flags = x86_64_interrupt_save_disable();
    for (ios_size index = 0; index < IOS_HANDLE_TABLE_CAPACITY; ++index) {
        struct ios_handle_entry *entry = &table->entries[index];
        if (entry->object == NULL) {
            entry->object = object;
            entry->kind = kind;
            entry->rights = rights;
            entry->retain = retain;
            entry->release = release;
            ++table->open_count;
            *handle = encode_handle(index, entry->generation);
            x86_64_interrupt_restore(flags);
            return IOS_OK;
        }
    }
    x86_64_interrupt_restore(flags);
    return IOS_ERROR(IOS_E_NO_SPACE);
}

ios_status handle_table_resolve(
    const struct ios_handle_table *table,
    ios_handle handle,
    enum ios_object_kind expected_kind,
    ios_u64 required_rights,
    void **object
)
{
    ios_size index;
    ios_u64 flags;
    ios_status status;

    if (object == NULL || expected_kind == IOS_OBJECT_NONE
        || (required_rights & ~IOS_RIGHT_ALL) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    flags = x86_64_interrupt_save_disable();
    status = decode_handle(table, handle, &index);
    if (IOS_SUCCEEDED(status) && table->entries[index].kind != expected_kind) {
        status = IOS_ERROR(IOS_E_WRONG_HANDLE_TYPE);
    }
    if (IOS_SUCCEEDED(status)
        && (table->entries[index].rights & required_rights) != required_rights) {
        status = IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    if (IOS_SUCCEEDED(status)) {
        *object = table->entries[index].object;
    }
    x86_64_interrupt_restore(flags);
    return status;
}

ios_status handle_table_duplicate(
    struct ios_handle_table *table,
    ios_handle source,
    ios_u64 reduced_rights,
    ios_handle *duplicate
)
{
    ios_size index;
    ios_u64 flags;
    ios_status status;
    struct ios_handle_entry entry;

    flags = x86_64_interrupt_save_disable();
    status = decode_handle(table, source, &index);
    if (IOS_FAILED(status)) {
        x86_64_interrupt_restore(flags);
        return status;
    }
    entry = table->entries[index];
    if ((entry.rights & IOS_RIGHT_DUPLICATE) == 0
        || (reduced_rights & ~entry.rights) != 0) {
        x86_64_interrupt_restore(flags);
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    if (entry.release != NULL && entry.retain == NULL) {
        x86_64_interrupt_restore(flags);
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (entry.retain != NULL) {
        entry.retain(entry.object);
    }
    status = handle_table_insert(
        table, entry.object, entry.kind, reduced_rights, entry.retain, entry.release, duplicate
    );
    x86_64_interrupt_restore(flags);
    if (IOS_FAILED(status) && entry.release != NULL) {
        entry.release(entry.object);
    }
    return status;
}

ios_status handle_table_transfer(
    struct ios_handle_table *source_table,
    struct ios_handle_table *destination_table,
    ios_handle source,
    ios_u64 reduced_rights,
    bool close_source,
    ios_handle *transferred
)
{
    ios_size index;
    ios_u64 flags = x86_64_interrupt_save_disable();
    ios_status status = decode_handle(source_table, source, &index);
    struct ios_handle_entry entry;

    if (IOS_FAILED(status) || destination_table == NULL || transferred == NULL) {
        x86_64_interrupt_restore(flags);
        return IOS_FAILED(status) ? status : IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    entry = source_table->entries[index];
    if ((entry.rights & IOS_RIGHT_TRANSFER) == 0
        || (reduced_rights & ~entry.rights) != 0) {
        x86_64_interrupt_restore(flags);
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    if (entry.release != NULL && entry.retain == NULL) {
        x86_64_interrupt_restore(flags);
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (entry.retain != NULL) {
        entry.retain(entry.object);
    }
    status = handle_table_insert(
        destination_table, entry.object, entry.kind, reduced_rights,
        entry.retain, entry.release, transferred
    );
    if (IOS_FAILED(status)) {
        x86_64_interrupt_restore(flags);
        if (entry.release != NULL) {
            entry.release(entry.object);
        }
        return status;
    }
    if (close_source) {
        (void)handle_table_close(source_table, source);
    }
    x86_64_interrupt_restore(flags);
    return IOS_OK;
}

ios_status handle_table_close(struct ios_handle_table *table, ios_handle handle)
{
    ios_size index;
    ios_u64 flags;
    ios_status status;
    void *object;
    ios_object_release_function release;

    flags = x86_64_interrupt_save_disable();
    status = decode_handle(table, handle, &index);
    if (IOS_FAILED(status)) {
        x86_64_interrupt_restore(flags);
        return status;
    }
    object = table->entries[index].object;
    release = table->entries[index].release;
    table->entries[index].object = NULL;
    table->entries[index].rights = 0;
    table->entries[index].kind = IOS_OBJECT_NONE;
    table->entries[index].retain = NULL;
    table->entries[index].release = NULL;
    table->entries[index].generation = next_generation(
        table->entries[index].generation, table->owner_tag
    );
    --table->open_count;
    x86_64_interrupt_restore(flags);
    if (release != NULL) {
        release(object);
    }
    return IOS_OK;
}

void handle_table_close_all(struct ios_handle_table *table)
{
    if (table == NULL || table->owner_tag == 0) {
        return;
    }
    for (ios_size index = 0; index < IOS_HANDLE_TABLE_CAPACITY; ++index) {
        if (table->entries[index].object != NULL) {
            (void)handle_table_close(table, encode_handle(index, table->entries[index].generation));
        }
    }
}

ios_u32 handle_table_open_count(const struct ios_handle_table *table)
{
    return table == NULL ? 0 : table->open_count;
}
