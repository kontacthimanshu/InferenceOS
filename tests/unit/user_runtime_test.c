#include <inferenceos/test.h>

#include <inferenceos/user/runtime.h>

static ios_u64 observed_number;
static ios_u64 observed_arguments[6];
static ios_status mock_status;
static struct ios_user_ipc_connect_request observed_connect;
static struct ios_user_ipc_send_request observed_send;
static struct ios_user_ipc_receive_request observed_receive;

ios_status ios_user_syscall6(
    ios_u64 number,
    ios_u64 argument0,
    ios_u64 argument1,
    ios_u64 argument2,
    ios_u64 argument3,
    ios_u64 argument4,
    ios_u64 argument5
)
{
    observed_number = number;
    observed_arguments[0] = argument0;
    observed_arguments[1] = argument1;
    observed_arguments[2] = argument2;
    observed_arguments[3] = argument3;
    observed_arguments[4] = argument4;
    observed_arguments[5] = argument5;
    if (number == IOS_SYSCALL_ABI_INFO && argument0 != 0) {
        struct ios_syscall_abi_info *information = (void *)(ios_uptr)argument0;
        *information = (struct ios_syscall_abi_info){
            .size = sizeof(*information),
            .version = IOS_SYSCALL_ABI_MAJOR,
            .major = IOS_SYSCALL_ABI_MAJOR,
            .minor = IOS_SYSCALL_ABI_MINOR,
            .feature_bits = IOS_SYSCALL_FEATURE_IPC
        };
    }
    if (number == IOS_SYSCALL_IPC_SERVICE_CONNECT && argument0 != 0
        && IOS_SUCCEEDED(mock_status)) {
        struct ios_user_ipc_connect_request *request = (void *)(ios_uptr)argument0;
        observed_connect = *request;
        request->endpoint_handle = UINT64_C(0x123400020001);
        request->service_generation = 7;
    }
    if (number == IOS_SYSCALL_IPC_SEND && argument0 != 0) {
        observed_send = *(const struct ios_user_ipc_send_request *)(ios_uptr)argument0;
    }
    if (number == IOS_SYSCALL_IPC_RECEIVE && argument0 != 0) {
        observed_receive = *(const struct ios_user_ipc_receive_request *)(ios_uptr)argument0;
    }
    return mock_status;
}

static void reset_mock(ios_status status)
{
    observed_number = UINT64_MAX;
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(observed_arguments); ++index) {
        observed_arguments[index] = UINT64_MAX;
    }
    mock_status = status;
}

static void assert_unused_arguments_are_zero(void)
{
    for (ios_size index = 1; index < IOS_ARRAY_COUNT(observed_arguments); ++index) {
        IOS_TEST_ASSERT(observed_arguments[index] == 0);
    }
}

static void test_abi_query_uses_the_versioned_output_pointer(void)
{
    struct ios_syscall_abi_info information = { 0 };
    reset_mock(IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_user_abi_query(&information), IOS_OK);
    IOS_TEST_ASSERT(observed_number == IOS_SYSCALL_ABI_INFO);
    IOS_TEST_ASSERT(observed_arguments[0] == (ios_uptr)&information);
    assert_unused_arguments_are_zero();
    IOS_TEST_ASSERT(information.size == sizeof(information));
    IOS_TEST_ASSERT(information.major == IOS_SYSCALL_ABI_MAJOR);
    IOS_TEST_ASSERT_STATUS(
        ios_user_abi_query(NULL), IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
}

static void test_connect_uses_a_zero_reserved_versioned_request(void)
{
    struct ios_user_ipc_connection connection;
    reset_mock(IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_user_ipc_service_connect(IOS_SERVICE_SHELL, &connection), IOS_OK
    );
    IOS_TEST_ASSERT(observed_number == IOS_SYSCALL_IPC_SERVICE_CONNECT);
    assert_unused_arguments_are_zero();
    const struct ios_user_ipc_connect_request *request = &observed_connect;
    IOS_TEST_ASSERT(request->size == sizeof(*request));
    IOS_TEST_ASSERT(request->version == IOS_IPC_ABI_VERSION);
    IOS_TEST_ASSERT(request->service == IOS_SERVICE_SHELL);
    IOS_TEST_ASSERT(request->flags == 0 && request->reserved32 == 0);
    IOS_TEST_ASSERT(connection.endpoint_handle == UINT64_C(0x123400020001));
    IOS_TEST_ASSERT(connection.service_generation == 7);

    connection = (struct ios_user_ipc_connection){ 9, 9 };
    reset_mock(IOS_ERROR(IOS_E_NOT_FOUND));
    IOS_TEST_ASSERT_STATUS(
        ios_user_ipc_service_connect(IOS_SERVICE_SHELL, &connection),
        IOS_ERROR(IOS_E_NOT_FOUND)
    );
    IOS_TEST_ASSERT(connection.endpoint_handle == 0);
    IOS_TEST_ASSERT(connection.service_generation == 0);
}

static void test_send_marshals_only_bounded_opaque_payload_data(void)
{
    const ios_u8 payload[] = { 1, 3, 3, 7 };
    reset_mock(IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_user_ipc_send(UINT64_C(0x10001), 42, 11, 2, payload, sizeof(payload)),
        IOS_OK
    );
    IOS_TEST_ASSERT(observed_number == IOS_SYSCALL_IPC_SEND);
    assert_unused_arguments_are_zero();
    const struct ios_user_ipc_send_request *request = &observed_send;
    IOS_TEST_ASSERT(request->size == sizeof(*request));
    IOS_TEST_ASSERT(request->version == IOS_IPC_ABI_VERSION);
    IOS_TEST_ASSERT(request->endpoint_handle == UINT64_C(0x10001));
    IOS_TEST_ASSERT(request->request_id == 42 && request->operation == 11);
    IOS_TEST_ASSERT(request->item_count == 2);
    IOS_TEST_ASSERT(request->payload_size == sizeof(payload));
    IOS_TEST_ASSERT(request->payload_address == (ios_uptr)payload);
    IOS_TEST_ASSERT(request->flags == 0 && request->message_flags == 0
        && request->reserved32 == 0);
}

static void test_receive_marshals_a_fixed_bounded_message_buffer(void)
{
    struct ios_ipc_message message;
    reset_mock(IOS_ERROR(IOS_E_WOULD_BLOCK));
    IOS_TEST_ASSERT_STATUS(
        ios_user_ipc_receive(UINT64_C(0x20001), &message),
        IOS_ERROR(IOS_E_WOULD_BLOCK)
    );
    IOS_TEST_ASSERT(observed_number == IOS_SYSCALL_IPC_RECEIVE);
    assert_unused_arguments_are_zero();
    const struct ios_user_ipc_receive_request *request = &observed_receive;
    IOS_TEST_ASSERT(request->size == sizeof(*request));
    IOS_TEST_ASSERT(request->version == IOS_IPC_ABI_VERSION);
    IOS_TEST_ASSERT(request->endpoint_handle == UINT64_C(0x20001));
    IOS_TEST_ASSERT(request->message_address == (ios_uptr)&message);
    IOS_TEST_ASSERT(request->message_capacity == sizeof(message));
    IOS_TEST_ASSERT(request->flags == 0 && request->reserved32 == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_user_ipc_receive(UINT64_C(0x20001), NULL),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_abi_query_uses_the_versioned_output_pointer),
    IOS_TEST_CASE(test_connect_uses_a_zero_reserved_versioned_request),
    IOS_TEST_CASE(test_send_marshals_only_bounded_opaque_payload_data),
    IOS_TEST_CASE(test_receive_marshals_a_fixed_bounded_message_buffer)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
