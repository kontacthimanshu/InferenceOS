#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/handle_table.h>
#include <inferenceos/process.h>
#include <inferenceos/proprietary_adapter.h>

#include <string.h>

enum {
    CUSTOM_APPLICATION = 0x43555354,
    PROPRIETARY_APPLICATION = 0x50524f50,
    APPROVED_OPERATION_WORD_COUNT = 1,
    APPROVED_ADAPTER_IDENTITY = 0x41445054
};

struct test_content {
    const char *bytes;
    ios_size byte_count;
    ios_u32 references;
};

struct approved_adapter {
    ios_size invocations;
};

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 flags) { (void)flags; }

static void retain_content(void *opaque)
{
    struct test_content *content = opaque;
    ++content->references;
}

static void release_content(void *opaque)
{
    struct test_content *content = opaque;
    IOS_TEST_ASSERT(content->references != 0);
    --content->references;
}

static void initialize_process(
    struct ios_process *process, ios_u64 process_id, ios_u64 application_identity)
{
    memset(process, 0, sizeof(*process));
    process->process_id = process_id;
    process->application_identity = application_identity;
    process->state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, process_id), IOS_OK);
}

static ios_status official_word_count(
    void *opaque,
    struct ios_process *proprietary_process,
    ios_handle content_handle,
    ios_u32 operation,
    const ios_u8 *input,
    ios_size input_size,
    ios_u8 *output,
    ios_size output_capacity,
    ios_size *output_size)
{
    struct approved_adapter *adapter = opaque;
    struct test_content *content = NULL;
    ios_u32 words = 0;
    bool in_word = false;
    ios_status status;

    if (adapter == NULL || proprietary_process == NULL || output == NULL
        || output_size == NULL || proprietary_process->application_identity
            != PROPRIETARY_APPLICATION || operation != APPROVED_OPERATION_WORD_COUNT
        || input == NULL || input_size != 0 || output_capacity < sizeof(words)) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    status = handle_table_resolve(
        &proprietary_process->handles, content_handle,
        IOS_OBJECT_CONTENT, IOS_RIGHT_READ, (void **)&content);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < content->byte_count; ++index) {
        const bool separator = content->bytes[index] == ' ' || content->bytes[index] == '\n';
        if (!separator && !in_word) ++words;
        in_word = !separator;
    }
    ++adapter->invocations;
    memcpy(output, &words, sizeof(words));
    *output_size = sizeof(words);
    return IOS_OK;
}

static bool contains_bytes(
    const void *haystack, ios_size haystack_size,
    const void *needle, ios_size needle_size)
{
    const ios_u8 *bytes = haystack;
    const ios_u8 *pattern = needle;
    if (needle_size == 0 || needle_size > haystack_size) return false;
    for (ios_size offset = 0; offset <= haystack_size - needle_size; ++offset) {
        if (memcmp(bytes + offset, pattern, needle_size) == 0) return true;
    }
    return false;
}

static void test_approved_adapter_operates_end_to_end_on_reduced_content_handle(void)
{
    static const char hidden_extension[] = "PFT";
    static const char hidden_hash[] = "A1B2C3D4";
    struct ios_process custom;
    struct ios_process proprietary;
    struct test_content content = { .bytes = "alpha beta gamma", .byte_count = 16, .references = 1 };
    struct approved_adapter adapter = { 0 };
    struct ios_proprietary_adapter_service service;
    struct ios_proprietary_adapter_reply reply;
    struct ios_proprietary_adapter_request request;
    ios_handle adapter_handle;
    ios_handle content_handle;
    void *resolved = NULL;
    ios_u32 word_count = 0;

    initialize_process(&custom, 10, CUSTOM_APPLICATION);
    initialize_process(&proprietary, 20, PROPRIETARY_APPLICATION);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &custom.handles, &content, IOS_OBJECT_CONTENT,
        IOS_RIGHT_READ | IOS_RIGHT_TRANSFER,
        retain_content, release_content, &content_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_service_initialize(&service), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_register(
        &service, &proprietary, &(struct ios_proprietary_adapter_descriptor){
            .size = sizeof(struct ios_proprietary_adapter_descriptor),
            .version = IOS_PROPRIETARY_ADAPTER_VERSION,
            .adapter_identity = APPROVED_ADAPTER_IDENTITY,
            .proprietary_application_identity = PROPRIETARY_APPLICATION,
            .authorized_caller_identity = CUSTOM_APPLICATION,
            .allowed_operation_mask = UINT64_C(1) << APPROVED_OPERATION_WORD_COUNT,
            .required_content_rights = IOS_RIGHT_READ,
            .invoke = official_word_count,
            .context = &adapter
        }), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_authorize(
        &service, &custom, APPROVED_ADAPTER_IDENTITY, &adapter_handle), IOS_OK);
    request = (struct ios_proprietary_adapter_request){
        .size = sizeof(request),
        .version = IOS_PROPRIETARY_ADAPTER_VERSION,
        .adapter_handle = adapter_handle,
        .content_handle = content_handle,
        .operation = APPROVED_OPERATION_WORD_COUNT
    };
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_invoke(
        &service, &custom, &request, &reply), IOS_OK);
    IOS_TEST_ASSERT(adapter.invocations == 1);
    IOS_TEST_ASSERT(reply.size == sizeof(reply)
        && reply.version == IOS_PROPRIETARY_ADAPTER_VERSION
        && reply.output_size == sizeof(word_count));
    memcpy(&word_count, reply.output, sizeof(word_count));
    IOS_TEST_ASSERT(word_count == 3);
    IOS_TEST_ASSERT(content.references == 1);
    IOS_TEST_ASSERT(handle_table_open_count(&proprietary.handles) == 0);
    IOS_TEST_ASSERT_STATUS(handle_table_resolve(
        &custom.handles, content_handle, IOS_OBJECT_CONTENT,
        IOS_RIGHT_READ, &resolved), IOS_OK);
    IOS_TEST_ASSERT(!contains_bytes(&reply, sizeof(reply), hidden_extension, 3));
    IOS_TEST_ASSERT(!contains_bytes(&reply, sizeof(reply), hidden_hash, 8));
}

static void test_broker_rejects_untransferable_or_unregistered_requests(void)
{
    struct ios_process custom;
    struct ios_process intruder;
    struct ios_process proprietary;
    struct test_content content = { .bytes = "data", .byte_count = 4, .references = 1 };
    struct approved_adapter adapter = { 0 };
    struct ios_proprietary_adapter_service service;
    struct ios_proprietary_adapter_reply reply;
    struct ios_proprietary_adapter_request request;
    ios_handle adapter_handle;
    ios_handle read_only_handle;

    initialize_process(&custom, 30, CUSTOM_APPLICATION);
    initialize_process(&intruder, 31, 0x494e5452);
    initialize_process(&proprietary, 40, PROPRIETARY_APPLICATION);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &custom.handles, &content, IOS_OBJECT_CONTENT, IOS_RIGHT_READ,
        retain_content, release_content, &read_only_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_service_initialize(&service), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_register(
        &service, &proprietary, &(struct ios_proprietary_adapter_descriptor){
            .size = sizeof(struct ios_proprietary_adapter_descriptor),
            .version = IOS_PROPRIETARY_ADAPTER_VERSION,
            .adapter_identity = APPROVED_ADAPTER_IDENTITY,
            .proprietary_application_identity = PROPRIETARY_APPLICATION,
            .authorized_caller_identity = CUSTOM_APPLICATION,
            .allowed_operation_mask = UINT64_C(1) << APPROVED_OPERATION_WORD_COUNT,
            .required_content_rights = IOS_RIGHT_READ,
            .invoke = official_word_count,
            .context = &adapter
        }), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_register(
        &service, &proprietary, &(struct ios_proprietary_adapter_descriptor){
            .size = sizeof(struct ios_proprietary_adapter_descriptor),
            .version = IOS_PROPRIETARY_ADAPTER_VERSION,
            .adapter_identity = APPROVED_ADAPTER_IDENTITY,
            .proprietary_application_identity = PROPRIETARY_APPLICATION,
            .authorized_caller_identity = CUSTOM_APPLICATION,
            .allowed_operation_mask = UINT64_C(1) << APPROVED_OPERATION_WORD_COUNT,
            .required_content_rights = IOS_RIGHT_READ,
            .invoke = official_word_count,
            .context = &adapter
        }), IOS_ERROR(IOS_E_ALREADY_EXISTS));
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_authorize(
        &service, &intruder, APPROVED_ADAPTER_IDENTITY, &adapter_handle),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_authorize(
        &service, &custom, APPROVED_ADAPTER_IDENTITY, &adapter_handle), IOS_OK);
    request = (struct ios_proprietary_adapter_request){
        .size = sizeof(request),
        .version = IOS_PROPRIETARY_ADAPTER_VERSION,
        .adapter_handle = adapter_handle,
        .content_handle = read_only_handle,
        .operation = APPROVED_OPERATION_WORD_COUNT
    };
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_invoke(
        &service, &custom, &request, &reply), IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_invoke(
        &service, &intruder, &request, &reply), IOS_ERROR(IOS_E_BAD_HANDLE));
    request.operation = 63;
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_invoke(
        &service, &custom, &request, &reply), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_authorize(
        &service, &custom, APPROVED_ADAPTER_IDENTITY + 1, &adapter_handle),
        IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT(adapter.invocations == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_approved_adapter_operates_end_to_end_on_reduced_content_handle),
    IOS_TEST_CASE(test_broker_rejects_untransferable_or_unregistered_requests)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
