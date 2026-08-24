#include <inferenceos/fs/file.h>

#include <inferenceos/runtime.h>

static ios_status validate_store(const struct ios_fs_file_store *store)
{
    if (store == NULL || store->fat == NULL || store->fat_entry_count <= 2
        || store->cluster_bytes != IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER
        || store->operations.read == NULL || store->operations.write == NULL
        || store->operations.zero == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return IOS_OK;
}

static ios_status load_chain(struct ios_fs_file *file, ios_size *count)
{
    if (file->primary.file_size == 0) {
        *count = 0;
        return file->primary.first_cluster == 0 ? IOS_OK : IOS_ERROR(IOS_E_CORRUPT);
    }
    return ios_fs_fat_traverse(
        file->store->fat, file->store->fat_entry_count, file->primary.first_cluster,
        file->chain_workspace, file->chain_capacity, count
    );
}

static ios_status ensure_clusters(struct ios_fs_file *file, ios_size required, ios_size *chain_count)
{
    ios_size existing;
    ios_size allocated_count;
    ios_size additional;
    ios_status status = load_chain(file, &existing);
    if (IOS_FAILED(status)) return status;
    if (required <= existing) {
        *chain_count = existing;
        return IOS_OK;
    }
    additional = required - existing;
    if (required > file->chain_capacity) return IOS_ERROR(IOS_E_NO_SPACE);
    status = ios_fs_fat_allocate(
        file->store->fat, file->store->fat_entry_count, additional,
        file->chain_workspace + existing, file->chain_capacity - existing, &allocated_count
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < allocated_count; ++index) {
        status = file->store->operations.zero(
            file->store->io_context, file->chain_workspace[existing + index]
        );
        if (IOS_FAILED(status)) {
            for (ios_size rollback = 0; rollback < allocated_count; ++rollback) {
                file->store->fat[file->chain_workspace[existing + rollback]] = IOS_FS_FAT_FREE;
            }
            return status;
        }
    }
    if (existing == 0) file->primary.first_cluster = *file->chain_workspace;
    else file->store->fat[file->chain_workspace[existing - 1]] = file->chain_workspace[existing];
    *chain_count = required;
    return IOS_OK;
}

ios_status ios_fs_file_open(
    struct ios_fs_file *file,
    struct ios_fs_file_store *store,
    const struct ios_fs_companion_disk *companion,
    const struct ios_fs_primary_disk *primary_disk,
    bool writable,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u8 *cluster_scratch,
    ios_size scratch_size
)
{
    struct ios_fs_primary primary;
    ios_size validated_chain_count;
    ios_status status;
    if (file == NULL || companion == NULL || primary_disk == NULL || chain_workspace == NULL
        || chain_capacity == 0 || cluster_scratch == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = validate_store(store);
    if (IOS_FAILED(status)) return status;
    if (scratch_size < store->cluster_bytes) return IOS_ERROR(IOS_E_NO_SPACE);
    if (writable && !store->writable) return IOS_ERROR(IOS_E_READ_ONLY);
    status = ios_fs_record_pair_validate(companion, primary_disk);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_primary_decode(primary_disk, &primary);
    if (IOS_FAILED(status)) return status;
    if (primary.file_size != 0) {
        status = ios_fs_fat_validate_file_capacity(
            store->fat, store->fat_entry_count, primary.first_cluster,
            primary.file_size, store->cluster_bytes
        );
        if (IOS_FAILED(status)) return status;
        status = ios_fs_fat_traverse(
            store->fat, store->fat_entry_count, primary.first_cluster,
            chain_workspace, chain_capacity, &validated_chain_count
        );
        if (IOS_FAILED(status)) return status;
    }
    memset(file, 0, sizeof(*file));
    file->store = store;
    file->primary = primary;
    file->chain_workspace = chain_workspace;
    file->chain_capacity = chain_capacity;
    file->cluster_scratch = cluster_scratch;
    file->scratch_size = scratch_size;
    file->writable = writable;
    file->open = true;
    return IOS_OK;
}

ios_status ios_fs_file_close(struct ios_fs_file *file)
{
    if (file == NULL || !file->open) return IOS_ERROR(IOS_E_INVALID_STATE);
    memset(file, 0, sizeof(*file));
    return IOS_OK;
}

ios_status ios_fs_file_read(
    struct ios_fs_file *file, void *buffer, ios_size length, ios_size *transferred
)
{
    ios_u8 *output = buffer;
    ios_size remaining;
    ios_size chain_count;
    ios_status status;
    if (file == NULL || !file->open || transferred == NULL || (buffer == NULL && length != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *transferred = 0;
    if (length == 0 || file->position == file->primary.file_size) return IOS_OK;
    remaining = length;
    if (remaining > file->primary.file_size - file->position) {
        remaining = file->primary.file_size - file->position;
    }
    status = load_chain(file, &chain_count);
    if (IOS_FAILED(status)) return status;
    while (remaining != 0) {
        const ios_size chain_index = file->position / file->store->cluster_bytes;
        const ios_size cluster_offset = file->position % file->store->cluster_bytes;
        ios_size chunk = file->store->cluster_bytes - cluster_offset;
        if (chain_index >= chain_count) return IOS_ERROR(IOS_E_CORRUPT);
        if (chunk > remaining) chunk = remaining;
        status = file->store->operations.read(
            file->store->io_context, file->chain_workspace[chain_index], file->cluster_scratch
        );
        if (IOS_FAILED(status)) return status;
        memcpy(output + *transferred, file->cluster_scratch + cluster_offset, chunk);
        file->position += (ios_u32)chunk;
        *transferred += chunk;
        remaining -= chunk;
    }
    return IOS_OK;
}

ios_status ios_fs_file_write(
    struct ios_fs_file *file, const void *buffer, ios_size length, ios_size *transferred
)
{
    const ios_u8 *input = buffer;
    ios_u64 end;
    ios_size required_clusters;
    ios_size chain_count;
    ios_size remaining = length;
    ios_status status;
    if (file == NULL || !file->open || transferred == NULL || (buffer == NULL && length != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *transferred = 0;
    if (!file->writable || !file->store->writable) return IOS_ERROR(IOS_E_READ_ONLY);
    if (file->position > file->primary.file_size) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (length == 0) return IOS_OK;
    if (length > UINT32_MAX - file->position) return IOS_ERROR(IOS_E_OVERFLOW);
    end = (ios_u64)file->position + length;
    required_clusters = ((ios_size)end + file->store->cluster_bytes - 1) / file->store->cluster_bytes;
    status = ensure_clusters(file, required_clusters, &chain_count);
    if (IOS_FAILED(status)) return status;
    while (remaining != 0) {
        const ios_size chain_index = file->position / file->store->cluster_bytes;
        const ios_size cluster_offset = file->position % file->store->cluster_bytes;
        ios_size chunk = file->store->cluster_bytes - cluster_offset;
        if (chain_index >= chain_count) return IOS_ERROR(IOS_E_CORRUPT);
        if (chunk > remaining) chunk = remaining;
        status = file->store->operations.read(
            file->store->io_context, file->chain_workspace[chain_index], file->cluster_scratch
        );
        if (IOS_FAILED(status)) return status;
        memcpy(file->cluster_scratch + cluster_offset, input + *transferred, chunk);
        status = file->store->operations.write(
            file->store->io_context, file->chain_workspace[chain_index], file->cluster_scratch
        );
        if (IOS_FAILED(status)) return status;
        file->position += (ios_u32)chunk;
        *transferred += chunk;
        remaining -= chunk;
    }
    if (file->position > file->primary.file_size) file->primary.file_size = file->position;
    return IOS_OK;
}

ios_status ios_fs_file_seek(
    struct ios_fs_file *file, enum ios_fs_seek_origin origin, ios_i64 offset, ios_u32 *position
)
{
    ios_i64 base;
    ios_i64 next;
    if (file == NULL || !file->open || position == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (origin == IOS_FS_SEEK_SET) base = 0;
    else if (origin == IOS_FS_SEEK_CURRENT) base = file->position;
    else if (origin == IOS_FS_SEEK_END) base = file->primary.file_size;
    else return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if ((offset > 0 && base > INT64_MAX - offset) || (offset < 0 && base < INT64_MIN - offset)) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    next = base + offset;
    if (next < 0 || (ios_u64)next > UINT32_MAX) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    file->position = (ios_u32)next;
    *position = file->position;
    return IOS_OK;
}

ios_status ios_fs_file_append(
    struct ios_fs_file *file, const void *buffer, ios_size length, ios_size *transferred
)
{
    ios_u32 ignored;
    ios_status status = ios_fs_file_seek(file, IOS_FS_SEEK_END, 0, &ignored);
    return IOS_FAILED(status) ? status : ios_fs_file_write(file, buffer, length, transferred);
}

ios_status ios_fs_file_metadata(const struct ios_fs_file *file, struct ios_fs_primary *primary)
{
    if (file == NULL || !file->open || primary == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *primary = file->primary;
    return IOS_OK;
}
