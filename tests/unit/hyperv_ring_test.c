#include <inferenceos/drivers/hyperv/ring.h>
#include <inferenceos/test.h>

#include <string.h>

_Alignas(IOS_HV_PAGE_SIZE) static ios_u8 ring_memory[IOS_HV_PAGE_SIZE + 128];

static void reset_ring(struct ios_hv_ring *ring)
{
    memset(ring_memory, 0, sizeof(ring_memory));
    IOS_TEST_ASSERT_STATUS(
        hyperv_ring_initialize(ring, ring_memory, sizeof(ring_memory)), IOS_OK);
}

static void writes_and_reads_inband_packet(void)
{
    struct ios_hv_ring ring;
    const ios_u8 sent[] = {1, 2, 3, 4, 5};
    ios_u8 received[8] = {0};
    ios_u16 type = 0;
    ios_u64 transaction_id = 0;
    ios_size size = 0;
    bool signal = false;

    reset_ring(&ring);
    IOS_TEST_ASSERT_STATUS(hyperv_ring_write(
        &ring, IOS_HV_PACKET_TYPE_DATA_INBAND,
        IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED, UINT64_C(0x1122334455667788),
        sent, sizeof(sent), &signal), IOS_OK);
    IOS_TEST_ASSERT(signal);
    IOS_TEST_ASSERT_STATUS(hyperv_ring_read(
        &ring, &type, &transaction_id, received, sizeof(received), &size), IOS_OK);
    IOS_TEST_ASSERT(type == IOS_HV_PACKET_TYPE_DATA_INBAND);
    IOS_TEST_ASSERT(transaction_id == UINT64_C(0x1122334455667788));
    IOS_TEST_ASSERT(size == sizeof(received));
    IOS_TEST_ASSERT(memcmp(received, sent, sizeof(sent)) == 0);
    IOS_TEST_ASSERT_STATUS(hyperv_ring_read(
        &ring, &type, &transaction_id, received, sizeof(received), &size),
        IOS_ERROR(IOS_E_WOULD_BLOCK));
}

static void packet_wraps_at_end_of_ring(void)
{
    struct ios_hv_ring ring;
    ios_u8 sent[24];
    ios_u8 received[24];
    ios_u16 type;
    ios_u64 transaction_id;
    ios_size size;
    bool signal;

    reset_ring(&ring);
    memset(sent, 0xa5, sizeof(sent));
    ring.header->write_index = 112;
    ring.header->read_index = 112;
    IOS_TEST_ASSERT_STATUS(hyperv_ring_write(
        &ring, IOS_HV_PACKET_TYPE_COMPLETION, 0, 7, sent, sizeof(sent), &signal), IOS_OK);
    IOS_TEST_ASSERT(ring.header->write_index < 112);
    IOS_TEST_ASSERT_STATUS(hyperv_ring_read(
        &ring, &type, &transaction_id, received, sizeof(received), &size), IOS_OK);
    IOS_TEST_ASSERT(type == IOS_HV_PACKET_TYPE_COMPLETION);
    IOS_TEST_ASSERT(transaction_id == 7);
    IOS_TEST_ASSERT(size == sizeof(sent));
    IOS_TEST_ASSERT(memcmp(received, sent, sizeof(sent)) == 0);
}

static void malformed_descriptor_is_rejected_without_consuming(void)
{
    struct ios_hv_ring ring;
    struct ios_hv_packet_descriptor *descriptor;
    ios_u8 payload[8];
    ios_u16 type;
    ios_u64 transaction_id;
    ios_size size;

    reset_ring(&ring);
    descriptor = (struct ios_hv_packet_descriptor *)ring.data;
    descriptor->type = IOS_HV_PACKET_TYPE_DATA_INBAND;
    descriptor->data_offset_8 = 1;
    descriptor->length_8 = 2;
    ring.header->write_index = 24;
    IOS_TEST_ASSERT_STATUS(hyperv_ring_read(
        &ring, &type, &transaction_id, payload, sizeof(payload), &size),
        IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT(ring.header->read_index == 0);
}

static void full_ring_reports_backpressure(void)
{
    struct ios_hv_ring ring;
    ios_u8 payload[88] = {0};
    bool signal;

    reset_ring(&ring);
    IOS_TEST_ASSERT_STATUS(hyperv_ring_write(
        &ring, IOS_HV_PACKET_TYPE_DATA_INBAND, 0, 1,
        payload, sizeof(payload), &signal), IOS_OK);
    IOS_TEST_ASSERT_STATUS(hyperv_ring_write(
        &ring, IOS_HV_PACKET_TYPE_DATA_INBAND, 0, 2,
        payload, 1, &signal), IOS_ERROR(IOS_E_WOULD_BLOCK));
    IOS_TEST_ASSERT(ring.header->pending_send_size != 0);
}

static void gpa_direct_packet_describes_each_page(void)
{
    struct ios_hv_ring ring;
    const ios_u8 command[8] = {0x5a};
    const struct ios_hv_packet_descriptor *descriptor;
    const ios_u32 *range_header;
    const ios_u64 *pfns;
    bool signal;

    reset_ring(&ring);
    IOS_TEST_ASSERT_STATUS(hyperv_ring_write_gpa_direct(
        &ring, IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED, 9,
        command, sizeof(command), UINT64_C(0x123ff0), 32, &signal), IOS_OK);
    descriptor = (const struct ios_hv_packet_descriptor *)ring.data;
    range_header = (const ios_u32 *)(ring.data + sizeof(*descriptor));
    pfns = (const ios_u64 *)(range_header + 4);
    IOS_TEST_ASSERT(descriptor->type == IOS_HV_PACKET_TYPE_DATA_USING_GPA_DIRECT);
    IOS_TEST_ASSERT(descriptor->data_offset_8 == 6);
    IOS_TEST_ASSERT(range_header[1] == 1);
    IOS_TEST_ASSERT(range_header[2] == 32);
    IOS_TEST_ASSERT(range_header[3] == 0xff0);
    IOS_TEST_ASSERT(pfns[0] == 0x123);
    IOS_TEST_ASSERT(pfns[1] == 0x124);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(writes_and_reads_inband_packet),
    IOS_TEST_CASE(packet_wraps_at_end_of_ring),
    IOS_TEST_CASE(malformed_descriptor_is_rejected_without_consuming),
    IOS_TEST_CASE(full_ring_reports_backpressure),
    IOS_TEST_CASE(gpa_direct_packet_describes_each_page)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
