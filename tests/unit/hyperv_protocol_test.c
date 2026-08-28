#include <inferenceos/drivers/hyperv/protocol.h>
#include <inferenceos/test.h>

static void protocol_layouts_are_stable(void)
{
    IOS_TEST_ASSERT(sizeof(struct ios_hv_post_message_input) == 256);
    IOS_TEST_ASSERT(sizeof(struct ios_hv_packet_descriptor) == 16);
    IOS_TEST_ASSERT(sizeof(struct ios_vstor_packet) == 64);
    IOS_TEST_ASSERT(IOS_HV_CALL_POST_MESSAGE == 0x005c);
    IOS_TEST_ASSERT(IOS_HV_CALL_SIGNAL_EVENT == 0x005d);
}

static void device_class_guids_are_distinct(void)
{
    IOS_TEST_ASSERT(hyperv_guid_equal(
        &IOS_HV_GUID_SYNTHETIC_SCSI, &IOS_HV_GUID_SYNTHETIC_SCSI));
    IOS_TEST_ASSERT(!hyperv_guid_equal(
        &IOS_HV_GUID_SYNTHETIC_SCSI, &IOS_HV_GUID_SYNTHETIC_KEYBOARD));
    IOS_TEST_ASSERT(!hyperv_guid_equal(
        &IOS_HV_GUID_SYNTHETIC_KEYBOARD, &IOS_HV_GUID_SYNTHETIC_MOUSE));
    IOS_TEST_ASSERT(!hyperv_guid_equal(NULL, &IOS_HV_GUID_SYNTHETIC_MOUSE));
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(protocol_layouts_are_stable),
    IOS_TEST_CASE(device_class_guids_are_distinct)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
