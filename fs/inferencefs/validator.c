#include <inferencefs/companion.h>
#include <inferencefs/directory.h>
#include <inferencefs/validator.h>
#include <inferenceos/memory.h>

typedef struct validation_context {
    inferencefs_fat fat;
    inferencefs_validator_workspace *workspace;
    inferencefs_validation_error error;
    inferenceos_u32 cluster;
} validation_context;

typedef struct chain_context {
    validation_context *validation;
} chain_context;

static inferencefs_validation_outcome validation_outcome(
    inferenceos_result result,
    inferencefs_validation_error error,
    inferenceos_u32 cluster
)
{
    const inferencefs_validation_outcome value = {
        .result = result,
        .error = error,
        .cluster = cluster
    };
    return value;
}

static bool claim_cluster(validation_context *context, inferenceos_u32 cluster)
{
    const inferenceos_u32 index = cluster - 2U;
    const inferenceos_size byte_index = (inferenceos_size)(index / 8U);
    const inferenceos_u8 mask = (inferenceos_u8)(1U << (index % 8U));

    if (!inferencefs_fat_cluster_is_valid(&context->fat, cluster)
        || byte_index >= INFERENCEFS_VALIDATOR_OWNERSHIP_BYTES) {
        context->error = INFERENCEFS_VALIDATION_ERROR_FAT_VALUE;
        context->cluster = cluster;
        return false;
    }
    if ((context->workspace->ownership[byte_index] & mask) != 0U) {
        context->error = INFERENCEFS_VALIDATION_ERROR_FAT_CROSS_LINK;
        context->cluster = cluster;
        return false;
    }
    context->workspace->ownership[byte_index] |= mask;
    return true;
}

static inferenceos_result claim_visited_cluster(
    void *opaque,
    inferenceos_u32 cluster,
    inferenceos_u32 chain_index
)
{
    chain_context *chain = opaque;
    (void)chain_index;
    return claim_cluster(chain->validation, cluster)
        ? INFERENCEOS_RESULT_OK : INFERENCEOS_RESULT_INCONSISTENT;
}

static inferenceos_result read_sector(
    validation_context *context,
    inferenceos_u64 lba,
    inferenceos_u8 sector[INFERENCEFS_SUPERBLOCK_SIZE]
)
{
    const inferenceos_block_outcome read = inferenceos_block_read(
        context->fat.device, lba, 1U, sector);

    if (!inferenceos_block_outcome_is_success(read)
        || read.sectors_completed != 1U) {
        context->error = INFERENCEFS_VALIDATION_ERROR_IO;
        return inferenceos_result_is_success(read.result)
            ? INFERENCEOS_RESULT_IO_ERROR : read.result;
    }
    return INFERENCEOS_RESULT_OK;
}

static inferenceos_result validate_file_chain(
    validation_context *context,
    const inferencefs_primary_record *record
)
{
    chain_context chain = { .validation = context };
    inferenceos_u32 count;
    inferenceos_u64 capacity;
    inferenceos_u64 previous_capacity;
    inferenceos_result result;

    if (record->file_size == 0U) {
        if (record->first_cluster != 0U) {
            context->error = INFERENCEFS_VALIDATION_ERROR_CHAIN_SIZE;
            context->cluster = record->first_cluster;
            return INFERENCEOS_RESULT_CORRUPT;
        }
        return INFERENCEOS_RESULT_OK;
    }
    if (!inferencefs_fat_cluster_is_valid(&context->fat, record->first_cluster)) {
        context->error = INFERENCEFS_VALIDATION_ERROR_FAT_VALUE;
        context->cluster = record->first_cluster;
        return INFERENCEOS_RESULT_CORRUPT;
    }
    result = inferencefs_fat_walk(
        &context->fat, record->first_cluster,
        claim_visited_cluster, &chain, &count);
    if (!inferenceos_result_is_success(result)) {
        if (context->error == INFERENCEFS_VALIDATION_ERROR_NONE) {
            context->error = result == INFERENCEOS_RESULT_CORRUPT
                ? INFERENCEFS_VALIDATION_ERROR_FAT_LOOP
                : INFERENCEFS_VALIDATION_ERROR_IO;
            context->cluster = record->first_cluster;
        }
        return result;
    }
    if (!inferenceos_checked_mul_u64(
            count,
            (inferenceos_u64)INFERENCEFS_LOGICAL_SECTOR_SIZE
                * INFERENCEFS_SECTORS_PER_CLUSTER,
            &capacity)) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    previous_capacity = capacity
        - ((inferenceos_u64)INFERENCEFS_LOGICAL_SECTOR_SIZE
            * INFERENCEFS_SECTORS_PER_CLUSTER);
    if (record->file_size > capacity || record->file_size <= previous_capacity) {
        context->error = INFERENCEFS_VALIDATION_ERROR_CHAIN_SIZE;
        context->cluster = record->first_cluster;
        return INFERENCEOS_RESULT_CORRUPT;
    }
    return INFERENCEOS_RESULT_OK;
}

static bool dot_name(const inferenceos_u8 name[INFERENCEFS_SHORT_NAME_SIZE])
{
    return name[0] == (inferenceos_u8)'.'
        && (name[1] == INFERENCEFS_SHORT_NAME_PADDING
            || name[1] == (inferenceos_u8)'.');
}

static inferenceos_result validate_directory(
    validation_context *context,
    inferenceos_u32 first_cluster,
    inferenceos_u32 depth
)
{
    inferenceos_u32 current = first_cluster;
    bool reached_end = false;

    if (depth > INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS) {
        context->error = INFERENCEFS_VALIDATION_ERROR_DEPTH;
        context->cluster = first_cluster;
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }
    for (inferenceos_u32 chain_index = 0U;
         chain_index < context->fat.geometry.data_cluster_count;
         ++chain_index) {
        inferenceos_u64 first_lba;
        inferencefs_companion_record_disk pending;
        bool has_pending = false;
        inferenceos_result result;

        if (!claim_cluster(context, current)) {
            return INFERENCEOS_RESULT_INCONSISTENT;
        }
        result = inferencefs_cluster_first_lba(&context->fat, current, &first_lba);
        if (!inferenceos_result_is_success(result)) {
            context->error = INFERENCEFS_VALIDATION_ERROR_FAT_VALUE;
            context->cluster = current;
            return result;
        }
        for (inferenceos_u32 sector_index = 0U;
             sector_index < INFERENCEFS_SECTORS_PER_CLUSTER;
             ++sector_index) {
            inferenceos_u8 sector[INFERENCEFS_SUPERBLOCK_SIZE];

            result = read_sector(context, first_lba + sector_index, sector);
            if (!inferenceos_result_is_success(result)) {
                return result;
            }
            if (reached_end) {
                continue;
            }
            for (inferenceos_size offset = 0U;
                 offset < sizeof(sector);
                 offset += INFERENCEFS_DIRECTORY_RECORD_SIZE) {
                const void *slot = sector + offset;
                const inferencefs_directory_slot_kind kind =
                    inferencefs_directory_classify_slot(slot);

                if (kind == INFERENCEFS_DIRECTORY_SLOT_KIND_END) {
                    if (has_pending) {
                        context->error = INFERENCEFS_VALIDATION_ERROR_COMPANION;
                        context->cluster = current;
                        return INFERENCEOS_RESULT_CORRUPT;
                    }
                    reached_end = true;
                    break;
                }
                if (kind == INFERENCEFS_DIRECTORY_SLOT_KIND_DELETED) {
                    if (has_pending) {
                        context->error = INFERENCEFS_VALIDATION_ERROR_COMPANION;
                        context->cluster = current;
                        return INFERENCEOS_RESULT_CORRUPT;
                    }
                    continue;
                }
                if (kind == INFERENCEFS_DIRECTORY_SLOT_KIND_COMPANION) {
                    if (has_pending) {
                        context->error = INFERENCEFS_VALIDATION_ERROR_COMPANION;
                        context->cluster = current;
                        return INFERENCEOS_RESULT_CORRUPT;
                    }
                    (void)memcpy(&pending, slot, sizeof(pending));
                    has_pending = true;
                    continue;
                }
                if (kind == INFERENCEFS_DIRECTORY_SLOT_KIND_REGULAR) {
                    inferencefs_primary_record primary;
                    inferencefs_companion companion;
                    inferencefs_companion_outcome companion_result;

                    if (!has_pending
                        || !inferenceos_result_is_success(
                            inferencefs_directory_decode_primary(slot, &primary))) {
                        context->error = INFERENCEFS_VALIDATION_ERROR_COMPANION;
                        context->cluster = current;
                        return INFERENCEOS_RESULT_CORRUPT;
                    }
                    companion_result = inferencefs_companion_decode(
                        &pending, primary.name, true, &companion);
                    has_pending = false;
                    if (!inferenceos_result_is_success(companion_result.result)) {
                        context->error = INFERENCEFS_VALIDATION_ERROR_COMPANION;
                        context->cluster = current;
                        return companion_result.result;
                    }
                    result = validate_file_chain(context, &primary);
                    if (!inferenceos_result_is_success(result)) {
                        return result;
                    }
                    continue;
                }
                if (kind == INFERENCEFS_DIRECTORY_SLOT_KIND_DIRECTORY) {
                    inferencefs_primary_record primary;

                    if (has_pending
                        || !inferenceos_result_is_success(
                            inferencefs_directory_decode_primary(slot, &primary))
                        || !inferencefs_fat_cluster_is_valid(
                            &context->fat, primary.first_cluster)) {
                        context->error = INFERENCEFS_VALIDATION_ERROR_DIRECTORY_SLOT;
                        context->cluster = current;
                        return INFERENCEOS_RESULT_CORRUPT;
                    }
                    if (!dot_name(primary.name)) {
                        result = validate_directory(
                            context, primary.first_cluster, depth + 1U);
                        if (!inferenceos_result_is_success(result)) {
                            return result;
                        }
                    }
                    continue;
                }
                context->error = INFERENCEFS_VALIDATION_ERROR_DIRECTORY_SLOT;
                context->cluster = current;
                return INFERENCEOS_RESULT_UNSUPPORTED;
            }
        }
        if (has_pending) {
            context->error = INFERENCEFS_VALIDATION_ERROR_COMPANION;
            context->cluster = current;
            return INFERENCEOS_RESULT_CORRUPT;
        }
        {
            inferenceos_u32 next;
            inferencefs_fat_value_kind kind;
            result = inferencefs_fat_read(&context->fat, current, &next);
            if (!inferenceos_result_is_success(result)) {
                context->error = INFERENCEFS_VALIDATION_ERROR_IO;
                context->cluster = current;
                return result;
            }
            kind = inferencefs_fat_classify(&context->fat, next);
            if (kind == INFERENCEFS_FAT_VALUE_END) {
                return INFERENCEOS_RESULT_OK;
            }
            if (kind != INFERENCEFS_FAT_VALUE_NEXT) {
                context->error = INFERENCEFS_VALIDATION_ERROR_FAT_VALUE;
                context->cluster = current;
                return INFERENCEOS_RESULT_CORRUPT;
            }
            current = next;
        }
    }
    context->error = INFERENCEFS_VALIDATION_ERROR_FAT_LOOP;
    context->cluster = current;
    return INFERENCEOS_RESULT_CORRUPT;
}

inferencefs_validation_outcome inferencefs_validate_namespace(
    const inferenceos_block_device *device,
    const inferencefs_superblock *superblock,
    inferencefs_validator_workspace *workspace
)
{
    validation_context context;
    inferenceos_u32 reserved_zero;
    inferenceos_u32 reserved_one;
    inferenceos_result result;

    if (device == NULL || superblock == NULL || workspace == NULL) {
        return validation_outcome(INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEFS_VALIDATION_ERROR_ARGUMENT, 0U);
    }
    (void)memset(workspace, 0, sizeof(*workspace));
    context.workspace = workspace;
    context.error = INFERENCEFS_VALIDATION_ERROR_NONE;
    context.cluster = 0U;
    result = inferencefs_fat_initialize(
        &context.fat, device, &superblock->geometry);
    if (!inferenceos_result_is_success(result)) {
        return validation_outcome(result,
            INFERENCEFS_VALIDATION_ERROR_ARGUMENT, 0U);
    }
    result = inferencefs_fat_read(&context.fat, 0U, &reserved_zero);
    if (inferenceos_result_is_success(result)) {
        result = inferencefs_fat_read(&context.fat, 1U, &reserved_one);
    }
    if (!inferenceos_result_is_success(result)) {
        return validation_outcome(result,
            INFERENCEFS_VALIDATION_ERROR_IO, 0U);
    }
    if (reserved_zero != UINT32_C(0x0FFFFFF8)
        || reserved_one != UINT32_C(0x0FFFFFFF)) {
        return validation_outcome(INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_VALIDATION_ERROR_RESERVED_FAT, 0U);
    }
    result = validate_directory(&context, superblock->root_cluster, 0U);
    return validation_outcome(result, context.error, context.cluster);
}
