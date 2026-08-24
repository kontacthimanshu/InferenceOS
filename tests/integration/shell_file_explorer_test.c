#include <inferenceos/test.h>

#include <inferenceos/display_safe_entry.h>
#include <inferenceos/gui/file_explorer.h>

#include <string.h>

enum trace_operation {
    TRACE_DIRECTORY_VIEW = 1,
    TRACE_TYPE_VIEW,
    TRACE_SEARCH,
    TRACE_GUI_VIEW
};

enum trace_boundary {
    TRACE_FILE_EXPLORER_REQUEST = 1,
    TRACE_SHELL_BROKER,
    TRACE_KERNEL_SERVICE,
    TRACE_VFS,
    TRACE_GUI_SERVICE,
    TRACE_SHELL_REPLY,
    TRACE_FILE_EXPLORER_MODEL
};

struct trace_event {
    enum trace_operation operation;
    enum trace_boundary boundary;
};

/* Filesystem-private metadata: only the test VFS/kernel oracle may inspect this type. */
struct raw_vfs_record {
    const char *base_name;
    const char *extension;
    ios_u64 extension_hash;
    ios_u64 object_handle;
    ios_u64 type_icon_capability;
    enum ios_display_safe_object_kind object_kind;
};

struct trace_stack {
    struct trace_event events[32];
    ios_size event_count;
    ios_u8 shell_authority;
};

struct file_view_request {
    enum trace_operation operation;
    ios_u64 directory_handle;
    ios_u64 type_icon_capability;
    const char *search_extension;
};

struct file_view_reply {
    struct ios_display_safe_entry *entries;
    ios_size capacity;
    ios_size count;
};

static void trace_append(
    struct trace_stack *stack, enum trace_operation operation, enum trace_boundary boundary
)
{
    IOS_TEST_ASSERT(stack->event_count < IOS_ARRAY_COUNT(stack->events));
    stack->events[stack->event_count++] = (struct trace_event){ operation, boundary };
}

static void trace_expect(
    const struct trace_stack *stack,
    enum trace_operation operation,
    enum trace_boundary backend
)
{
    static const enum trace_boundary common[] = {
        TRACE_FILE_EXPLORER_REQUEST,
        TRACE_SHELL_BROKER,
        TRACE_KERNEL_SERVICE
    };
    IOS_TEST_ASSERT(stack->event_count == 6);
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(common); ++index) {
        IOS_TEST_ASSERT(stack->events[index].operation == operation);
        IOS_TEST_ASSERT(stack->events[index].boundary == common[index]);
    }
    IOS_TEST_ASSERT(stack->events[3].operation == operation);
    IOS_TEST_ASSERT(stack->events[3].boundary == backend);
    IOS_TEST_ASSERT(stack->events[4].operation == operation);
    IOS_TEST_ASSERT(stack->events[4].boundary == TRACE_SHELL_REPLY);
    IOS_TEST_ASSERT(stack->events[5].operation == operation);
    IOS_TEST_ASSERT(stack->events[5].boundary == TRACE_FILE_EXPLORER_MODEL);
}

static ios_status vfs_file_view(
    struct trace_stack *stack,
    const struct file_view_request *request,
    struct file_view_reply *reply
)
{
    static const struct raw_vfs_record records[] = {
        { "REPORT", "TXT", UINT64_C(0x545854), 7, 101, IOS_DISPLAY_SAFE_REGULAR_FILE },
        { "REPORT", "PNG", UINT64_C(0x504e47), 9, 202, IOS_DISPLAY_SAFE_REGULAR_FILE },
        { "DOCS", "", 0, 11, 0, IOS_DISPLAY_SAFE_DIRECTORY }
    };
    trace_append(stack, request->operation, TRACE_VFS);
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(records); ++index) {
        const struct raw_vfs_record *raw = &records[index];
        bool include = request->operation == TRACE_DIRECTORY_VIEW;
        if (request->operation == TRACE_TYPE_VIEW) {
            include = raw->type_icon_capability == request->type_icon_capability;
        } else if (request->operation == TRACE_SEARCH) {
            include = strcmp(raw->extension, request->search_extension) == 0;
        }
        if (!include) continue;
        if (reply->count >= reply->capacity) return IOS_ERROR(IOS_E_NO_SPACE);
        const struct ios_display_safe_source_entry source = {
            .base_name = raw->base_name,
            .object_handle = raw->object_handle,
            .type_icon_capability = raw->type_icon_capability,
            .byte_size = raw->object_kind == IOS_DISPLAY_SAFE_REGULAR_FILE ? 64 : 0,
            .allowed_operations = raw->object_kind == IOS_DISPLAY_SAFE_DIRECTORY
                ? IOS_DISPLAY_SAFE_OPERATION_ENUMERATE : IOS_DISPLAY_SAFE_OPERATION_OPEN,
            .object_kind = raw->object_kind
        };
        ios_status status = ios_display_safe_entry_convert(
            &source, &reply->entries[reply->count]
        );
        if (IOS_FAILED(status)) return status;
        ++reply->count;
    }
    return IOS_OK;
}

/* This oracle keeps stage ordering explicit; the production path has separate integration tests. */
static ios_status kernel_dispatch(
    struct trace_stack *stack,
    const void *shell_authority,
    const struct file_view_request *request,
    struct file_view_reply *reply
)
{
    if (shell_authority != &stack->shell_authority) return IOS_ERROR(IOS_E_ACCESS_DENIED);
    trace_append(stack, request->operation, TRACE_KERNEL_SERVICE);
    if (request->operation == TRACE_GUI_VIEW) {
        trace_append(stack, request->operation, TRACE_GUI_SERVICE);
        return IOS_OK;
    }
    return vfs_file_view(stack, request, reply);
}

static ios_status shell_broker_request(
    struct trace_stack *stack,
    const struct file_view_request *request,
    struct file_view_reply *reply
)
{
    trace_append(stack, request->operation, TRACE_SHELL_BROKER);
    ios_status status = kernel_dispatch(stack, &stack->shell_authority, request, reply);
    if (IOS_FAILED(status)) return status;
    trace_append(stack, request->operation, TRACE_SHELL_REPLY);
    return IOS_OK;
}

static ios_status file_explorer_request(
    struct trace_stack *stack,
    const struct file_view_request *request,
    struct file_view_reply *reply
)
{
    trace_append(stack, request->operation, TRACE_FILE_EXPLORER_REQUEST);
    ios_status status = shell_broker_request(stack, request, reply);
    if (IOS_FAILED(status)) return status;
    trace_append(stack, request->operation, TRACE_FILE_EXPLORER_MODEL);
    return IOS_OK;
}

static ios_status traced_enumerate(
    void *context,
    ios_u64 directory_handle,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
)
{
    struct trace_stack *stack = context;
    const struct file_view_request request = {
        .operation = TRACE_DIRECTORY_VIEW,
        .directory_handle = directory_handle
    };
    struct file_view_reply reply = { entries, capacity, 0 };
    trace_append(stack, request.operation, TRACE_FILE_EXPLORER_REQUEST);
    ios_status status = shell_broker_request(stack, &request, &reply);
    if (IOS_FAILED(status)) return status;
    *entry_count = reply.count;
    return IOS_OK;
}

static ios_status resolve_icon(
    void *context,
    ios_u64 type_icon_capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    (void)context;
    (void)type_icon_capability;
    *icon = object_kind == IOS_DISPLAY_SAFE_DIRECTORY
        ? IOS_ICON_FOLDER : IOS_ICON_GENERIC_FILE;
    return IOS_OK;
}

static void assert_safe_entry(const struct ios_display_safe_entry *entry)
{
    IOS_TEST_ASSERT(entry->version == IOS_DISPLAY_SAFE_ENTRY_VERSION);
    IOS_TEST_ASSERT(strchr(entry->display_name, '.') == NULL);
    IOS_TEST_ASSERT(strstr(entry->display_name, "TXT") == NULL);
    IOS_TEST_ASSERT(strstr(entry->display_name, "PNG") == NULL);
    IOS_TEST_ASSERT(entry->type_icon_capability != UINT64_C(0x545854));
    IOS_TEST_ASSERT(entry->type_icon_capability != UINT64_C(0x504e47));
}

static void test_directory_view_traces_shell_vfs_and_populates_safe_model(void)
{
    struct trace_stack stack = { 0 };
    struct ios_file_explorer_model model;
    const struct ios_file_explorer_view_provider provider = {
        &stack, traced_enumerate, resolve_icon
    };
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_initialize(&model, provider, 1), IOS_OK);
    trace_append(&stack, TRACE_DIRECTORY_VIEW, TRACE_FILE_EXPLORER_MODEL);
    trace_expect(&stack, TRACE_DIRECTORY_VIEW, TRACE_VFS);
    IOS_TEST_ASSERT(model.entry_count == 3);
    IOS_TEST_ASSERT(strcmp(model.entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(strcmp(model.entries[1].display_name, "REPORT (2)") == 0);
    for (ios_size index = 0; index < model.entry_count; ++index) {
        assert_safe_entry(&model.entries[index]);
    }
}

static void test_type_search_and_gui_view_each_trace_through_shell(void)
{
    const struct file_view_request requests[] = {
        { TRACE_TYPE_VIEW, 1, 101, NULL },
        { TRACE_SEARCH, 1, 0, "TXT" },
        { TRACE_GUI_VIEW, 1, 0, NULL }
    };
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(requests); ++index) {
        struct trace_stack stack = { 0 };
        struct ios_display_safe_entry entries[3];
        struct file_view_reply reply = { entries, IOS_ARRAY_COUNT(entries), 0 };
        IOS_TEST_ASSERT_STATUS(file_explorer_request(&stack, &requests[index], &reply), IOS_OK);
        trace_expect(
            &stack, requests[index].operation,
            requests[index].operation == TRACE_GUI_VIEW ? TRACE_GUI_SERVICE : TRACE_VFS
        );
        if (requests[index].operation != TRACE_GUI_VIEW) {
            IOS_TEST_ASSERT(reply.count == 1);
            assert_safe_entry(&reply.entries[0]);
        } else {
            IOS_TEST_ASSERT(reply.count == 0);
        }
    }
}

static void test_file_explorer_cannot_bypass_shell_authority(void)
{
    struct trace_stack stack = { 0 };
    struct ios_display_safe_entry entries[1];
    const struct file_view_request request = { TRACE_DIRECTORY_VIEW, 1, 0, NULL };
    struct file_view_reply reply = { entries, IOS_ARRAY_COUNT(entries), 0 };
    trace_append(&stack, request.operation, TRACE_FILE_EXPLORER_REQUEST);
    IOS_TEST_ASSERT_STATUS(
        kernel_dispatch(&stack, NULL, &request, &reply), IOS_ERROR(IOS_E_ACCESS_DENIED)
    );
    IOS_TEST_ASSERT(stack.event_count == 1);
    IOS_TEST_ASSERT(reply.count == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_directory_view_traces_shell_vfs_and_populates_safe_model),
    IOS_TEST_CASE(test_type_search_and_gui_view_each_trace_through_shell),
    IOS_TEST_CASE(test_file_explorer_cannot_bypass_shell_authority)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
