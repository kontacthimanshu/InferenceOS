#include <inferenceos/drivers/hyperv/storvsc.h>
#include <inferenceos/test.h>

#include <string.h>

static void rw10_cdb_uses_big_endian_lba_and_count(void)
{
    ios_u8 cdb[16];

    IOS_TEST_ASSERT_STATUS(storvsc_build_rw10_cdb(
        cdb, false, UINT64_C(0x12345678), 0x2345), IOS_OK);
    IOS_TEST_ASSERT(cdb[0] == 0x28);
    IOS_TEST_ASSERT(cdb[2] == 0x12 && cdb[3] == 0x34);
    IOS_TEST_ASSERT(cdb[4] == 0x56 && cdb[5] == 0x78);
    IOS_TEST_ASSERT(cdb[7] == 0x23 && cdb[8] == 0x45);
    IOS_TEST_ASSERT_STATUS(storvsc_build_rw10_cdb(
        cdb, true, 7, 1), IOS_OK);
    IOS_TEST_ASSERT(cdb[0] == 0x2a);
}

static void rw10_rejects_unrepresentable_requests(void)
{
    ios_u8 cdb[16];

    IOS_TEST_ASSERT_STATUS(storvsc_build_rw10_cdb(
        cdb, false, UINT64_C(0x100000000), 1), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(storvsc_build_rw10_cdb(
        cdb, false, 0, 0), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
}

static void capacity10_is_validated_and_converted(void)
{
    const ios_u8 response[8] = {0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x02, 0x00};
    const ios_u8 large[8] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x02, 0x00};
    ios_u64 sectors;
    ios_u32 block_size;

    IOS_TEST_ASSERT_STATUS(storvsc_parse_capacity10(
        response, &sectors, &block_size), IOS_OK);
    IOS_TEST_ASSERT(sectors == 65536);
    IOS_TEST_ASSERT(block_size == 512);
    IOS_TEST_ASSERT_STATUS(storvsc_parse_capacity10(
        large, &sectors, &block_size), IOS_ERROR(IOS_E_OUT_OF_RANGE));
}

static void wire_direction_values_match_vmstor(void)
{
    IOS_TEST_ASSERT(IOS_STORVSC_DATA_WRITE == 0);
    IOS_TEST_ASSERT(IOS_STORVSC_DATA_READ == 1);
    IOS_TEST_ASSERT(IOS_STORVSC_DATA_UNKNOWN == 2);
    IOS_TEST_ASSERT(offsetof(struct ios_vstor_srb, request) == 16);
    IOS_TEST_ASSERT(offsetof(struct ios_vstor_srb, srb_flags) == 40);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(rw10_cdb_uses_big_endian_lba_and_count),
    IOS_TEST_CASE(rw10_rejects_unrepresentable_requests),
    IOS_TEST_CASE(capacity10_is_validated_and_converted),
    IOS_TEST_CASE(wire_direction_values_match_vmstor)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
