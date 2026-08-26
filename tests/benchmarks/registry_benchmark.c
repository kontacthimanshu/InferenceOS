#include <inferenceos/test.h>

#include <inferenceos/fs/registry.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum {
    REGISTRY_BENCHMARK_CORPUS_COUNT = 12,
    REGISTRY_BENCHMARK_QUERY_COUNT = 8,
    REGISTRY_BENCHMARK_TYPE_COUNT = 4,
    REGISTRY_BENCHMARK_MARKER_COUNT = 6
};

enum registry_benchmark_phase {
    REGISTRY_BENCHMARK_COLD_QUERY,
    REGISTRY_BENCHMARK_WARM_QUERY,
    REGISTRY_BENCHMARK_DURABLE_SAVE
};

enum registry_benchmark_edge {
    REGISTRY_BENCHMARK_BEGIN,
    REGISTRY_BENCHMARK_END
};

struct registry_benchmark_spec {
    ios_u8 base[8];
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
};

struct registry_benchmark_marker {
    bool enabled;
    enum registry_benchmark_phase phase;
    enum registry_benchmark_edge edge;
    ios_size ordinal;
    const char *text;
};

struct registry_benchmark_result {
    ios_u32 seed;
    ios_u32 corpus_checksum;
    ios_u32 query_checksum;
    ios_u32 correctness_digest;
    ios_size cold_result_count;
    ios_size warm_result_count;
    ios_size durable_save_count;
    struct registry_benchmark_marker markers[REGISTRY_BENCHMARK_MARKER_COUNT];
    ios_size marker_count;
};

static const ios_u32 registry_benchmark_seed = UINT32_C(0x1a2b3c4d);

static const struct registry_benchmark_spec registry_benchmark_corpus[] = {
    { { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' }, { 'T', 'X', 'T' } },
    { { 'C', 'H', 'A', 'R', 'T', ' ', ' ', ' ' }, { 'P', 'N', 'G' } },
    { { 'E', 'V', 'E', 'N', 'T', ' ', ' ', ' ' }, { 'L', 'O', 'G' } },
    { { 'K', 'E', 'R', 'N', 'E', 'L', ' ', ' ' }, { 'B', 'I', 'N' } },
    { { 'N', 'O', 'T', 'E', 'S', ' ', ' ', ' ' }, { 'T', 'X', 'T' } },
    { { 'I', 'C', 'O', 'N', ' ', ' ', ' ', ' ' }, { 'P', 'N', 'G' } },
    { { 'S', 'Y', 'S', 'T', 'E', 'M', ' ', ' ' }, { 'L', 'O', 'G' } },
    { { 'D', 'R', 'I', 'V', 'E', 'R', ' ', ' ' }, { 'B', 'I', 'N' } },
    { { 'R', 'E', 'A', 'D', 'M', 'E', ' ', ' ' }, { 'T', 'X', 'T' } },
    { { 'P', 'H', 'O', 'T', 'O', ' ', ' ', ' ' }, { 'P', 'N', 'G' } },
    { { 'A', 'U', 'D', 'I', 'T', ' ', ' ', ' ' }, { 'L', 'O', 'G' } },
    { { 'M', 'O', 'D', 'U', 'L', 'E', ' ', ' ' }, { 'B', 'I', 'N' } }
};

static const ios_u8 registry_benchmark_queries[][IOS_FS_EXTENSION_SIZE] = {
    { 'T', 'X', 'T' },
    { 'P', 'N', 'G' },
    { 'L', 'O', 'G' },
    { 'B', 'I', 'N' },
    { 'T', 'X', 'T' },
    { 'L', 'O', 'G' },
    { 'P', 'N', 'G' },
    { 'B', 'I', 'N' }
};

static const char *const registry_benchmark_markers[2][3][2] = {
    {
        {
            "INFERENCEOS:REGISTRY_BENCH_BEGIN mode=disabled phase=cold-query",
            "INFERENCEOS:REGISTRY_BENCH_END mode=disabled phase=cold-query"
        },
        {
            "INFERENCEOS:REGISTRY_BENCH_BEGIN mode=disabled phase=warm-query",
            "INFERENCEOS:REGISTRY_BENCH_END mode=disabled phase=warm-query"
        },
        {
            "INFERENCEOS:REGISTRY_BENCH_BEGIN mode=disabled phase=durable-save",
            "INFERENCEOS:REGISTRY_BENCH_END mode=disabled phase=durable-save"
        }
    },
    {
        {
            "INFERENCEOS:REGISTRY_BENCH_BEGIN mode=enabled phase=cold-query",
            "INFERENCEOS:REGISTRY_BENCH_END mode=enabled phase=cold-query"
        },
        {
            "INFERENCEOS:REGISTRY_BENCH_BEGIN mode=enabled phase=warm-query",
            "INFERENCEOS:REGISTRY_BENCH_END mode=enabled phase=warm-query"
        },
        {
            "INFERENCEOS:REGISTRY_BENCH_BEGIN mode=enabled phase=durable-save",
            "INFERENCEOS:REGISTRY_BENCH_END mode=enabled phase=durable-save"
        }
    }
};

static ios_u32 digest_mix(ios_u32 digest, ios_u32 value)
{
    digest ^= value;
    digest *= UINT32_C(0x01000193);
    return digest;
}

static void make_source(
    const struct registry_benchmark_spec *spec,
    ios_u32 directory_cluster,
    ios_u16 primary_slot,
    struct ios_fs_registry_source_entry *entry
)
{
    struct ios_fs_primary value = {
        { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 4096
    };
    memset(entry, 0, sizeof(*entry));
    memcpy(value.name, spec->base, sizeof(spec->base));
    memcpy(
        value.name + sizeof(spec->base), spec->extension,
        IOS_FS_EXTENSION_SIZE
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &entry->primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_encode(value.name, true, &entry->companion), IOS_OK
    );
    entry->directory_cluster = directory_cluster;
    entry->primary_slot = primary_slot;
}

static void record_marker(
    struct registry_benchmark_result *result,
    bool enabled,
    enum registry_benchmark_phase phase,
    enum registry_benchmark_edge edge
)
{
    IOS_TEST_ASSERT(result->marker_count < IOS_ARRAY_COUNT(result->markers));
    struct registry_benchmark_marker *marker =
        &result->markers[result->marker_count];
    marker->enabled = enabled;
    marker->phase = phase;
    marker->edge = edge;
    marker->ordinal = result->marker_count;
    marker->text = registry_benchmark_markers[enabled ? 1 : 0][phase][edge];
    ++result->marker_count;
    puts(marker->text);
}

static ios_size run_query_trace(
    struct ios_fs_registry *registry,
    const struct ios_fs_registry_source_entry *sources,
    struct registry_benchmark_result *result
)
{
    ios_size total_matches = 0;
    for (ios_size query = 0; query < REGISTRY_BENCHMARK_QUERY_COUNT; ++query) {
        ios_size matches[REGISTRY_BENCHMARK_CORPUS_COUNT];
        ios_size match_count = 0;
        memset(matches, 0xff, sizeof(matches));
        IOS_TEST_ASSERT_STATUS(
            ios_fs_registry_lookup(
                registry, sources, REGISTRY_BENCHMARK_CORPUS_COUNT,
                registry_benchmark_queries[query], IOS_FS_EXTENSION_SIZE,
                matches, IOS_ARRAY_COUNT(matches), &match_count
            ),
            IOS_OK
        );
        IOS_TEST_ASSERT(match_count == 3);
        result->correctness_digest = digest_mix(
            result->correctness_digest,
            ios_fs_fnv1a32(
                registry_benchmark_queries[query], IOS_FS_EXTENSION_SIZE
            )
        );
        result->correctness_digest = digest_mix(
            result->correctness_digest, (ios_u32)match_count
        );
        for (ios_size match = 0; match < match_count; ++match) {
            IOS_TEST_ASSERT(matches[match] < REGISTRY_BENCHMARK_CORPUS_COUNT);
            result->correctness_digest = digest_mix(
                result->correctness_digest, (ios_u32)matches[match]
            );
        }
        total_matches += match_count;
    }
    return total_matches;
}

static void run_benchmark(bool enabled, struct registry_benchmark_result *result)
{
    struct ios_fs_registry_source_entry sources[REGISTRY_BENCHMARK_CORPUS_COUNT];
    struct ios_fs_registry_source_entry saved;
    struct ios_fs_registry_record_disk storage[REGISTRY_BENCHMARK_TYPE_COUNT];
    struct ios_fs_registry registry;
    const struct registry_benchmark_spec saved_spec = {
        { 'S', 'A', 'V', 'E', 'D', ' ', ' ', ' ' }, { 'T', 'X', 'T' }
    };
    ios_size record_index = SIZE_MAX;

    memset(result, 0, sizeof(*result));
    memset(storage, 0, sizeof(storage));
    for (ios_size index = 0; index < REGISTRY_BENCHMARK_CORPUS_COUNT; ++index) {
        make_source(
            &registry_benchmark_corpus[index], 100 + (ios_u32)(index / 4),
            (ios_u16)(index * 2 + 1), &sources[index]
        );
    }
    make_source(&saved_spec, 104, 25, &saved);
    result->seed = registry_benchmark_seed;
    result->corpus_checksum = ios_fs_fnv1a32(
        registry_benchmark_corpus, sizeof(registry_benchmark_corpus)
    );
    result->query_checksum = ios_fs_fnv1a32(
        registry_benchmark_queries, sizeof(registry_benchmark_queries)
    );
    result->correctness_digest = UINT32_C(0x811c9dc5);
    printf(
        "INFERENCEOS:REGISTRY_BENCH_CORPUS mode=%s seed=%08" PRIx32
        " corpus=%08" PRIx32 " queries=%08" PRIx32 "\n",
        enabled ? "enabled" : "disabled", result->seed,
        result->corpus_checksum, result->query_checksum
    );

    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(
            &registry, storage, IOS_ARRAY_COUNT(storage), enabled
        ),
        IOS_OK
    );
    if (enabled) {
        IOS_TEST_ASSERT_STATUS(
            ios_fs_registry_rebuild(
                &registry, sources, IOS_ARRAY_COUNT(sources)
            ),
            IOS_OK
        );
    }

    record_marker(
        result, enabled, REGISTRY_BENCHMARK_COLD_QUERY,
        REGISTRY_BENCHMARK_BEGIN
    );
    result->cold_result_count = run_query_trace(&registry, sources, result);
    record_marker(
        result, enabled, REGISTRY_BENCHMARK_COLD_QUERY,
        REGISTRY_BENCHMARK_END
    );

    record_marker(
        result, enabled, REGISTRY_BENCHMARK_WARM_QUERY,
        REGISTRY_BENCHMARK_BEGIN
    );
    result->warm_result_count = run_query_trace(&registry, sources, result);
    record_marker(
        result, enabled, REGISTRY_BENCHMARK_WARM_QUERY,
        REGISTRY_BENCHMARK_END
    );

    record_marker(
        result, enabled, REGISTRY_BENCHMARK_DURABLE_SAVE,
        REGISTRY_BENCHMARK_BEGIN
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(
            &registry, &saved.companion, &saved.primary,
            saved.directory_cluster, saved.primary_slot, &record_index
        ),
        IOS_OK
    );
    result->correctness_digest = digest_mix(
        result->correctness_digest,
        ios_fs_fnv1a32(&saved.primary, sizeof(saved.primary))
    );
    result->durable_save_count = 1;
    record_marker(
        result, enabled, REGISTRY_BENCHMARK_DURABLE_SAVE,
        REGISTRY_BENCHMARK_END
    );
    printf(
        "INFERENCEOS:REGISTRY_BENCH_RESULT mode=%s correctness=%08" PRIx32
        " cold-results=%zu warm-results=%zu durable-saves=%zu\n",
        enabled ? "enabled" : "disabled", result->correctness_digest,
        result->cold_result_count, result->warm_result_count,
        result->durable_save_count
    );
}

static void test_enabled_and_disabled_runs_use_matched_inputs_and_results(void)
{
    struct registry_benchmark_result disabled;
    struct registry_benchmark_result enabled;

    run_benchmark(false, &disabled);
    run_benchmark(true, &enabled);
    IOS_TEST_ASSERT(disabled.seed == registry_benchmark_seed);
    IOS_TEST_ASSERT(disabled.seed == enabled.seed);
    IOS_TEST_ASSERT(disabled.corpus_checksum != 0);
    IOS_TEST_ASSERT(disabled.corpus_checksum == enabled.corpus_checksum);
    IOS_TEST_ASSERT(disabled.query_checksum != 0);
    IOS_TEST_ASSERT(disabled.query_checksum == enabled.query_checksum);
    IOS_TEST_ASSERT(disabled.correctness_digest != 0);
    IOS_TEST_ASSERT(disabled.correctness_digest == enabled.correctness_digest);
    IOS_TEST_ASSERT(disabled.cold_result_count == enabled.cold_result_count);
    IOS_TEST_ASSERT(disabled.warm_result_count == enabled.warm_result_count);
    IOS_TEST_ASSERT(disabled.cold_result_count == 24);
    IOS_TEST_ASSERT(disabled.warm_result_count == 24);
    IOS_TEST_ASSERT(disabled.durable_save_count == enabled.durable_save_count);
}

static void test_measurement_markers_have_stable_mode_phase_and_order(void)
{
    struct registry_benchmark_result enabled;

    run_benchmark(true, &enabled);
    IOS_TEST_ASSERT(enabled.marker_count == REGISTRY_BENCHMARK_MARKER_COUNT);
    for (ios_size index = 0; index < enabled.marker_count; ++index) {
        IOS_TEST_ASSERT(enabled.markers[index].enabled);
        IOS_TEST_ASSERT(enabled.markers[index].ordinal == index);
        IOS_TEST_ASSERT(enabled.markers[index].phase ==
            (enum registry_benchmark_phase)(index / 2));
        IOS_TEST_ASSERT(enabled.markers[index].edge ==
            (enum registry_benchmark_edge)(index % 2));
        IOS_TEST_ASSERT(strcmp(
            enabled.markers[index].text,
            registry_benchmark_markers[1][index / 2][index % 2]
        ) == 0);
    }
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_enabled_and_disabled_runs_use_matched_inputs_and_results),
    IOS_TEST_CASE(test_measurement_markers_have_stable_mode_phase_and_order)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
