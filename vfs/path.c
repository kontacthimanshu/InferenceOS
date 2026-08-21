#include <inferenceos/memory.h>
#include <inferenceos/vfs.h>

static bool normalized_path_is_valid(
    const inferenceos_vfs_normalized_path *path
)
{
    inferenceos_size expected_length = 1U;

    if (path == NULL || path->length == 0U
        || path->length > INFERENCEOS_VFS_MAX_PATH_LENGTH
        || path->text[0] != INFERENCEOS_VFS_PATH_SEPARATOR
        || path->text[path->length] != '\0'
        || path->component_count > INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS) {
        return false;
    }
    for (inferenceos_size index = 0U;
         index < path->component_count;
         ++index) {
        const inferenceos_size offset = path->component_offsets[index];
        const inferenceos_size length = path->component_lengths[index];

        if (length == 0U || length > INFERENCEOS_VFS_MAX_COMPONENT_LENGTH
            || offset != expected_length
            || offset > path->length
            || length > path->length - offset) {
            return false;
        }
        for (inferenceos_size byte = 0U; byte < length; ++byte) {
            if (path->text[offset + byte] == '\0'
                || path->text[offset + byte]
                    == INFERENCEOS_VFS_PATH_SEPARATOR) {
                return false;
            }
        }
        expected_length = offset + length;
        if (index + 1U < path->component_count) {
            if (expected_length >= path->length
                || path->text[expected_length]
                    != INFERENCEOS_VFS_PATH_SEPARATOR) {
                return false;
            }
            ++expected_length;
        }
    }
    return expected_length == path->length;
}

static void initialize_root(inferenceos_vfs_normalized_path *path)
{
    (void)memset(path, 0, sizeof(*path));
    path->text[0] = INFERENCEOS_VFS_PATH_SEPARATOR;
    path->text[1] = '\0';
    path->length = 1U;
}

static inferenceos_vfs_status push_component(
    inferenceos_vfs_normalized_path *path,
    const char *component,
    inferenceos_size component_length
)
{
    inferenceos_size separator_length;
    inferenceos_size required_length;
    inferenceos_size offset;

    if (component_length == 0U
        || component_length > INFERENCEOS_VFS_MAX_COMPONENT_LENGTH) {
        return INFERENCEOS_VFS_STATUS_OUT_OF_RANGE;
    }
    if (path->component_count == INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS) {
        return INFERENCEOS_VFS_STATUS_OUT_OF_RANGE;
    }
    separator_length = path->component_count == 0U ? 0U : 1U;
    if (!inferenceos_checked_add_size(
            path->length,
            separator_length,
            &required_length
        )
        || !inferenceos_checked_add_size(
            required_length,
            component_length,
            &required_length
        )) {
        return INFERENCEOS_VFS_STATUS_OVERFLOW;
    }
    if (required_length > INFERENCEOS_VFS_MAX_PATH_LENGTH) {
        return INFERENCEOS_VFS_STATUS_OUT_OF_RANGE;
    }

    if (separator_length != 0U) {
        path->text[path->length] = INFERENCEOS_VFS_PATH_SEPARATOR;
        ++path->length;
    }
    offset = path->length;
    (void)memcpy(path->text + offset, component, component_length);
    path->component_offsets[path->component_count] = (inferenceos_u16)offset;
    path->component_lengths[path->component_count]
        = (inferenceos_u16)component_length;
    ++path->component_count;
    path->length += component_length;
    path->text[path->length] = '\0';
    return INFERENCEOS_VFS_STATUS_OK;
}

static void pop_component(inferenceos_vfs_normalized_path *path)
{
    if (path->component_count == 0U) {
        return;
    }
    --path->component_count;
    if (path->component_count == 0U) {
        path->length = 1U;
    } else {
        const inferenceos_size last = path->component_count - 1U;
        path->length = path->component_offsets[last]
            + path->component_lengths[last];
    }
    path->text[path->length] = '\0';
}

static inferenceos_vfs_status copy_base(
    const inferenceos_vfs_normalized_path *base,
    inferenceos_vfs_normalized_path *path
)
{
    if (base == NULL) {
        return INFERENCEOS_VFS_STATUS_OK;
    }
    if (!normalized_path_is_valid(base)) {
        return INFERENCEOS_VFS_STATUS_INVALID_PATH;
    }
    for (inferenceos_size index = 0U;
         index < base->component_count;
         ++index) {
        const inferenceos_vfs_status status = push_component(
            path,
            base->text + base->component_offsets[index],
            base->component_lengths[index]
        );
        if (!inferenceos_vfs_status_is_success(status)) {
            return status;
        }
    }
    return INFERENCEOS_VFS_STATUS_OK;
}

inferenceos_vfs_status inferenceos_vfs_path_normalize(
    const inferenceos_vfs_normalized_path *current_directory,
    inferenceos_vfs_path input,
    inferenceos_vfs_normalized_path *output
)
{
    inferenceos_vfs_normalized_path result;
    inferenceos_size index = 0U;
    inferenceos_vfs_status status;

    if (output == NULL || input.data == NULL) {
        return INFERENCEOS_VFS_STATUS_INVALID_ARGUMENT;
    }
    if (input.length == 0U) {
        return INFERENCEOS_VFS_STATUS_INVALID_PATH;
    }
    if (input.length > INFERENCEOS_VFS_MAX_PATH_LENGTH) {
        return INFERENCEOS_VFS_STATUS_OUT_OF_RANGE;
    }
    for (inferenceos_size byte = 0U; byte < input.length; ++byte) {
        if (input.data[byte] == '\0') {
            return INFERENCEOS_VFS_STATUS_INVALID_PATH;
        }
    }

    initialize_root(&result);
    if (input.data[0] == INFERENCEOS_VFS_PATH_SEPARATOR) {
        while (index < input.length
            && input.data[index] == INFERENCEOS_VFS_PATH_SEPARATOR) {
            ++index;
        }
    } else {
        status = copy_base(current_directory, &result);
        if (!inferenceos_vfs_status_is_success(status)) {
            return status;
        }
    }

    while (index < input.length) {
        const inferenceos_size component_start = index;
        inferenceos_size component_length;

        while (index < input.length
            && input.data[index] != INFERENCEOS_VFS_PATH_SEPARATOR) {
            ++index;
        }
        component_length = index - component_start;
        if (component_length == 1U && input.data[component_start] == '.') {
            /* The current-directory component has no effect. */
        } else if (component_length == 2U
            && input.data[component_start] == '.'
            && input.data[component_start + 1U] == '.') {
            pop_component(&result);
        } else if (component_length != 0U) {
            status = push_component(
                &result,
                input.data + component_start,
                component_length
            );
            if (!inferenceos_vfs_status_is_success(status)) {
                return status;
            }
        }
        while (index < input.length
            && input.data[index] == INFERENCEOS_VFS_PATH_SEPARATOR) {
            ++index;
        }
    }

    *output = result;
    return INFERENCEOS_VFS_STATUS_OK;
}

inferenceos_vfs_status inferenceos_vfs_path_component(
    const inferenceos_vfs_normalized_path *path,
    inferenceos_size component_index,
    inferenceos_vfs_path *component
)
{
    if (path == NULL || component == NULL) {
        return INFERENCEOS_VFS_STATUS_INVALID_ARGUMENT;
    }
    if (!normalized_path_is_valid(path)) {
        return INFERENCEOS_VFS_STATUS_INVALID_PATH;
    }
    if (component_index >= path->component_count) {
        return INFERENCEOS_VFS_STATUS_OUT_OF_RANGE;
    }
    component->data = path->text + path->component_offsets[component_index];
    component->length = path->component_lengths[component_index];
    return INFERENCEOS_VFS_STATUS_OK;
}

bool inferenceos_vfs_path_is_root(
    const inferenceos_vfs_normalized_path *path
)
{
    return normalized_path_is_valid(path) && path->component_count == 0U;
}
