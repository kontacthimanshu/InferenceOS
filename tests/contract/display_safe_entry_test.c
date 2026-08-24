#include <inferenceos/test.h>

#include <inferenceos/display_safe_entry.h>

#include <stddef.h>
#include <string.h>

static bool contains_bytes(
    const void *haystack, ios_size haystack_length,
    const void *needle, ios_size needle_length
)
{
    const ios_u8 *bytes = haystack;
    const ios_u8 *pattern = needle;
    if (needle_length == 0 || needle_length > haystack_length) return false;
    for (ios_size offset = 0; offset <= haystack_length - needle_length; ++offset) {
        if (memcmp(bytes + offset, pattern, needle_length) == 0) return true;
    }
    return false;
}

static void test_dto_contains_only_permitted_ordinary_view_fields(void)
{
    struct ios_display_safe_entry entry = {
        .size = sizeof(entry),
        .version = IOS_DISPLAY_SAFE_ENTRY_VERSION,
        .generic_attributes = IOS_DISPLAY_SAFE_ATTRIBUTE_READ_ONLY,
        .object_handle = UINT64_C(0x0000000200000007),
        .type_icon_capability = UINT64_C(0x0000000600000009),
        .byte_size = 15,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN
                            | IOS_DISPLAY_SAFE_OPERATION_READ,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE,
        .display_name_length = 6,
        .display_name = "REPORT"
    };
    IOS_TEST_ASSERT(entry.size == 112 && entry.version == 1);
    IOS_TEST_ASSERT(entry.object_handle != 0 && entry.type_icon_capability != 0);
    IOS_TEST_ASSERT(entry.byte_size == 15);
    IOS_TEST_ASSERT(entry.object_kind == IOS_DISPLAY_SAFE_REGULAR_FILE);
    IOS_TEST_ASSERT(entry.display_name_length == strlen(entry.display_name));
    IOS_TEST_ASSERT(strcmp(entry.display_name, "REPORT") == 0);
    for (ios_size index = 0; index < sizeof(entry.reserved); ++index) {
        IOS_TEST_ASSERT(entry.reserved[index] == 0);
    }
}

static void test_serialized_dto_leaks_no_forbidden_source_metadata(void)
{
    static const char canonical_name[11] = {
        'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T'
    };
    static const char extension[] = "TXT";
    static const char extension_hash[] = "E771F04F";
    static const char registry_marker[] = "INFOSFS1";
    struct ios_display_safe_entry entry = {
        .size = sizeof(entry),
        .version = IOS_DISPLAY_SAFE_ENTRY_VERSION,
        .object_handle = UINT64_C(0x0000000200000007),
        .type_icon_capability = UINT64_C(0x0000000600000009),
        .byte_size = 15,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE,
        .display_name_length = 6,
        .display_name = "REPORT"
    };
    IOS_TEST_ASSERT(!contains_bytes(&entry, sizeof(entry), canonical_name, sizeof(canonical_name)));
    IOS_TEST_ASSERT(!contains_bytes(&entry, sizeof(entry), extension, sizeof(extension) - 1));
    IOS_TEST_ASSERT(!contains_bytes(
        &entry, sizeof(entry), extension_hash, sizeof(extension_hash) - 1
    ));
    IOS_TEST_ASSERT(!contains_bytes(
        &entry, sizeof(entry), registry_marker, sizeof(registry_marker) - 1
    ));
}

static void test_schema_cannot_identify_internal_companion_objects(void)
{
    IOS_TEST_ASSERT(IOS_DISPLAY_SAFE_REGULAR_FILE != IOS_DISPLAY_SAFE_DIRECTORY);
    IOS_TEST_ASSERT(IOS_DISPLAY_SAFE_REGULAR_FILE == 1);
    IOS_TEST_ASSERT(IOS_DISPLAY_SAFE_DIRECTORY == 2);
}

static void test_conversion_copies_only_authorized_base_name_and_opaque_metadata(void)
{
    const struct ios_display_safe_source_entry source = {
        .base_name = "REPORT",
        .object_handle = UINT64_C(0x0000000200000007),
        .type_icon_capability = UINT64_C(0x0000000600000009),
        .byte_size = 15,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN
                            | IOS_DISPLAY_SAFE_OPERATION_READ,
        .generic_attributes = IOS_DISPLAY_SAFE_ATTRIBUTE_READ_ONLY,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE
    };
    struct ios_display_safe_entry entry;
    IOS_TEST_ASSERT_STATUS(ios_display_safe_entry_convert(&source, &entry), IOS_OK);
    IOS_TEST_ASSERT(strcmp(entry.display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(entry.display_name_length == 6 && entry.byte_size == 15);
    IOS_TEST_ASSERT(entry.object_handle == source.object_handle);
    IOS_TEST_ASSERT(entry.type_icon_capability == source.type_icon_capability);
}

static void test_conversion_rejects_extension_bearing_or_unbounded_names(void)
{
    const struct ios_display_safe_source_entry source = {
        .base_name = "REPORT.TXT",
        .object_handle = 7,
        .type_icon_capability = 9,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE
    };
    struct ios_display_safe_entry entry;
    memset(&entry, 0xa5, sizeof(entry));
    IOS_TEST_ASSERT_STATUS(
        ios_display_safe_entry_convert(&source, &entry), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT(entry.object_handle == 0 && entry.display_name[0] == '\0');
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_dto_contains_only_permitted_ordinary_view_fields),
    IOS_TEST_CASE(test_serialized_dto_leaks_no_forbidden_source_metadata),
    IOS_TEST_CASE(test_schema_cannot_identify_internal_companion_objects),
    IOS_TEST_CASE(test_conversion_copies_only_authorized_base_name_and_opaque_metadata),
    IOS_TEST_CASE(test_conversion_rejects_extension_bearing_or_unbounded_names)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
