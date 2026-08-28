#include <inferenceos/drivers/hyperv/vmbus.h>

#include <inferenceos/runtime.h>

ios_u8 hyperv_input_test_response[32];
ios_size hyperv_input_test_response_size;
ios_size hyperv_input_test_request_size;
bool hyperv_input_test_response_ready;

ios_status vmbus_channel_write(
    struct ios_vmbus *bus, struct ios_vmbus_channel *channel, ios_u16 packet_type,
    ios_u16 flags, ios_u64 transaction_id, const void *payload, ios_size payload_size
)
{
    (void)bus; (void)channel; (void)packet_type; (void)flags;
    (void)transaction_id; (void)payload;
    hyperv_input_test_request_size = payload_size;
    return IOS_OK;
}

ios_status vmbus_channel_read(
    struct ios_vmbus_channel *channel, ios_u16 *packet_type, ios_u64 *transaction_id,
    void *payload, ios_size payload_capacity, ios_size *payload_size
)
{
    (void)channel;
    if (!hyperv_input_test_response_ready) return IOS_ERROR(IOS_E_WOULD_BLOCK);
    if (payload_capacity < hyperv_input_test_response_size) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    *packet_type = IOS_HV_PACKET_TYPE_DATA_INBAND;
    *transaction_id = 1;
    *payload_size = hyperv_input_test_response_size;
    memcpy(payload, hyperv_input_test_response, hyperv_input_test_response_size);
    hyperv_input_test_response_ready = false;
    return IOS_OK;
}

void x86_64_cpu_relax(void)
{
}

ios_u64 x86_64_interrupt_save_disable(void)
{
    return 0;
}

void x86_64_interrupt_restore(ios_u64 flags)
{
    (void)flags;
}
