#include <inferenceos/test.h>

#include <inferenceos/fs_diagnostic.h>

#include <string.h>

struct text_sink {
    char bytes[4096];
    ios_size length;
    ios_size limit;
};

static bool capture_character(void *context, char character)
{
    struct text_sink *sink = context;
    if (sink->length >= sink->limit || sink->length + 1 >= sizeof(sink->bytes)) return false;
    sink->bytes[sink->length++] = character;
    sink->bytes[sink->length] = '\0';
    return true;
}

static struct ios_fs_diagnostic_reply reply_for(enum ios_fs_diagnostic_query query)
{
    struct ios_fs_diagnostic_reply reply;
    memset(&reply, 0, sizeof(reply));
    reply.size = sizeof(reply);
    reply.version = IOS_FS_DIAGNOSTIC_ABI_VERSION;
    reply.query = query;
    return reply;
}

static void test_formats_file_bytes_without_control_character_disclosure(void)
{
    struct ios_fs_diagnostic_reply reply = reply_for(IOS_FS_DIAGNOSTIC_QUERY_FILE);
    struct text_sink sink = { .limit = sizeof(sink.bytes) - 1 };
    reply.value.file.canonical_name[0] = 'A';
    reply.value.file.canonical_name[1] = '\n';
    reply.value.file.canonical_name[2] = 0xff;
    reply.value.file.object_type = IOS_FS_DIRECTORY_ENTRY_REGULAR;
    reply.value.file.size = 42;
    reply.value.file.first_cluster = 7;
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_format(&reply, capture_character, &sink), IOS_OK);
    IOS_TEST_ASSERT(strstr(sink.bytes, "query=file") != NULL);
    IOS_TEST_ASSERT(strstr(sink.bytes, "canonical_name_hex=410aff") != NULL);
    IOS_TEST_ASSERT(strstr(sink.bytes, " size=42 first_cluster=7") != NULL);
    IOS_TEST_ASSERT(sink.bytes[sink.length - 1] == '\n');
    IOS_TEST_ASSERT(strchr(sink.bytes, '\n') == sink.bytes + sink.length - 1);
}

static void test_formats_bounded_fat_chain(void)
{
    struct ios_fs_diagnostic_reply reply = reply_for(IOS_FS_DIAGNOSTIC_QUERY_FAT);
    struct text_sink sink = { .limit = sizeof(sink.bytes) - 1 };
    reply.value.fat.cluster_count = 3;
    reply.value.fat.clusters[0] = 2;
    reply.value.fat.clusters[1] = 19;
    reply.value.fat.clusters[2] = 400;
    reply.value.fat.end_of_chain = true;
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_format(&reply, capture_character, &sink), IOS_OK);
    IOS_TEST_ASSERT(strstr(sink.bytes, "query=fat cluster_count=3 chain=2,19,400 end_of_chain=1") != NULL);
    reply.value.fat.cluster_count = IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY + 1;
    sink.length = 0;
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_format(&reply, capture_character, &sink),
                           IOS_ERROR(IOS_E_PROTOCOL));
}

static void test_rejects_invalid_envelopes_and_propagates_sink_failure(void)
{
    struct ios_fs_diagnostic_reply reply = reply_for(IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM);
    struct text_sink sink = { .limit = 8 };
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_format(&reply, capture_character, &sink),
                           IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(sink.length == sink.limit);
    reply.version = IOS_FS_DIAGNOSTIC_ABI_VERSION + 1;
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_format(&reply, capture_character, &sink),
                           IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_format(NULL, capture_character, &sink),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_format(&reply, NULL, &sink),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_formats_file_bytes_without_control_character_disclosure),
    IOS_TEST_CASE(test_formats_bounded_fat_chain),
    IOS_TEST_CASE(test_rejects_invalid_envelopes_and_propagates_sink_failure)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
