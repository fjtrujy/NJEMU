#include "ps2_benchmark_result.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX "NJEMU_BENCHMARK_V1 "

static int fail(char *error, size_t length, const char *reason)
{
    if (length != 0)
        snprintf(error, length, "%s", reason);
    return 0;
}

static int copy_field(char *dest, size_t size, const char *value)
{
    size_t len = strlen(value);
    if (len == 0 || len >= size)
        return 0;
    memcpy(dest, value, len + 1);
    return 1;
}

static int parse_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long number;
    if (*text == '\0' || *text == '-')
        return 0;
    errno = 0;
    number = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0')
        return 0;
    *value = (uint64_t)number;
    return 1;
}

static int assign_field(ps2_benchmark_result_t *r, const char *key,
    const char *value)
{
    uint64_t n = 0;
    if (strcmp(key, "core") == 0)
        return copy_field(r->core, sizeof(r->core), value);
    if (strcmp(key, "rom") == 0)
        return copy_field(r->rom, sizeof(r->rom), value);
    if (strcmp(key, "compiler") == 0)
        return copy_field(r->compiler, sizeof(r->compiler), value);
    if (strcmp(key, "scenario") == 0)
        return copy_field(r->scenario, sizeof(r->scenario), value);
    if (strcmp(key, "state") == 0)
        return copy_field(r->state, sizeof(r->state), value);
    if (strcmp(key, "state_hash") == 0)
        return copy_field(r->state_hash, sizeof(r->state_hash), value);
    if (strcmp(key, "cache") == 0)
        return copy_field(r->cache, sizeof(r->cache), value);
    if (strcmp(key, "sound") == 0)
        return copy_field(r->sound, sizeof(r->sound), value);
    if (strcmp(key, "resident") == 0)
        return copy_field(r->resident, sizeof(r->resident), value);
    if (strcmp(key, "status") == 0)
        return copy_field(r->status, sizeof(r->status), value);
    if (strcmp(key, "revision") == 0)
        return copy_field(r->revision, sizeof(r->revision), value);
    if (strcmp(key, "cc_version") == 0)
        return copy_field(r->compiler_version, sizeof(r->compiler_version), value);

    if (!parse_u64(value, &n))
        return 0;
    if (strcmp(key, "total_us") == 0) r->total_us = n;
    else if (strcmp(key, "sample_sum_us") == 0) r->sample_sum_us = n;
    else if (strcmp(key, "mean_us") == 0) r->mean_us = n;
    else if (strcmp(key, "io_bytes") == 0) r->io_bytes = n;
    else if (strcmp(key, "core_update_us") == 0) r->core_update_us = n;
    else if (strcmp(key, "screen_update_us") == 0) r->screen_update_us = n;
    else {
        uint32_t *field = NULL;
        if (n > UINT32_MAX) return 0;
        if (strcmp(key, "warmup") == 0) field = &r->warmup;
        else if (strcmp(key, "frames") == 0) field = &r->frames;
        else if (strcmp(key, "expected") == 0) field = &r->expected;
        else if (strcmp(key, "min_us") == 0) field = &r->min_us;
        else if (strcmp(key, "max_us") == 0) field = &r->max_us;
        else if (strcmp(key, "p50_us") == 0) field = &r->p50_us;
        else if (strcmp(key, "p95_us") == 0) field = &r->p95_us;
        else if (strcmp(key, "p99_us") == 0) field = &r->p99_us;
        else if (strcmp(key, "over_60hz") == 0) field = &r->over_60hz;
        else if (strcmp(key, "io_ops") == 0) field = &r->io_ops;
        else if (strcmp(key, "ticker_pair_ns") == 0) field = &r->ticker_pair_ns;
        else if (strcmp(key, "cpu_bytes") == 0) field = &r->cpu_bytes;
        else if (strcmp(key, "gfx_bytes") == 0) field = &r->gfx_bytes;
        else if (strcmp(key, "pcm_bytes") == 0) field = &r->pcm_bytes;
        else return 0;
        *field = (uint32_t)n;
    }
    return 1;
}

int ps2_benchmark_parse(const char *text, ps2_benchmark_result_t *result,
    char *error, size_t error_size)
{
    const char *found = NULL;
    const char *p = text;
    char line[1024];
    size_t length;
    uint64_t fields = 0;
    static const char *required[] = {
        "core", "rom", "compiler", "warmup", "frames", "expected",
        "total_us", "sample_sum_us", "mean_us", "min_us", "max_us",
        "p50_us", "p95_us", "p99_us", "over_60hz", "io_ops",
        "io_bytes", "cache", "sound", "state", "status", "revision",
        "cc_version", "state_hash", "scenario", "resident",
        "cpu_bytes", "gfx_bytes", "pcm_bytes",
        "core_update_us", "screen_update_us", "ticker_pair_ns"
    };
    unsigned i;
    memset(result, 0, sizeof(*result));
    if (error_size) error[0] = '\0';

    while ((p = strstr(p, PREFIX)) != NULL) {
        if (found != NULL)
            return fail(error, error_size, "multiple benchmark records");
        found = p;
        p += strlen(PREFIX);
    }
    if (found == NULL)
        return fail(error, error_size, "missing benchmark record");
    found += strlen(PREFIX);
    p = strchr(found, '\n');
    length = p ? (size_t)(p - found) : strlen(found);
    if (length >= sizeof(line))
        return fail(error, error_size, "benchmark record too long");
    memcpy(line, found, length);
    line[length] = '\0';
    if (length && line[length - 1] == '\r')
        line[length - 1] = '\0';

    {
        char *token = strtok(line, " ");
        while (token != NULL) {
            char *equals = strchr(token, '=');
            if (!equals || equals == token || !equals[1])
                return fail(error, error_size, "malformed key=value");
            *equals++ = '\0';
            for (i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
                if (strcmp(token, required[i]) == 0) {
                    if (fields & (UINT64_C(1) << i))
                        return fail(error, error_size, "duplicate field");
                    fields |= (UINT64_C(1) << i);
                    break;
                }
            }
            if (!assign_field(result, token, equals))
                return fail(error, error_size, "invalid or unknown field");
            token = strtok(NULL, " ");
        }
    }
    if (fields != ((UINT64_C(1) << (sizeof(required) / sizeof(required[0]))) - 1))
        return fail(error, error_size, "missing required field");
    if ((strcmp(result->core, "CPS1") != 0 &&
         strcmp(result->core, "CPS2") != 0 &&
         strcmp(result->core, "MVS") != 0) ||
        (strcmp(result->compiler, "GCC") != 0 &&
         strcmp(result->compiler, "LLVM") != 0) ||
        result->frames > result->expected ||
        (result->frames && result->min_us > result->max_us))
        return fail(error, error_size, "inconsistent benchmark metadata");
    return 1;
}

int ps2_benchmark_result_accepted(const ps2_benchmark_result_t *r)
{
    return strcmp(r->status, "ok") == 0 &&
        strcmp(r->state, "verified") == 0 &&
        r->state_hash[0] != '\0' &&
        r->revision[0] != '\0' &&
        r->scenario[0] != '\0' &&
        strcmp(r->cache, "off") == 0 &&
        strcmp(r->resident, "on") == 0 &&
        r->gfx_bytes != 0 && r->cpu_bytes != 0 &&
        strcmp(r->sound, "on") == 0 &&
        r->io_ops == 0 && r->io_bytes == 0 &&
        r->frames > 0 && r->frames == r->expected &&
        r->total_us > 0;
}

static int compare_u64(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a;
    uint64_t right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

uint64_t ps2_benchmark_median(const uint64_t *values, size_t count)
{
    uint64_t *copy;
    uint64_t result;
    if (count == 0 || count > PS2_BENCHMARK_MAX_TRIALS)
        return 0;
    copy = (uint64_t *)malloc(count * sizeof(*copy));
    if (!copy) return 0;
    memcpy(copy, values, count * sizeof(*copy));
    qsort(copy, count, sizeof(*copy), compare_u64);
    if (count & 1u)
        result = copy[count / 2];
    else
        result = copy[count / 2 - 1] / 2 + copy[count / 2] / 2 +
            ((copy[count / 2 - 1] & 1u) && (copy[count / 2] & 1u));
    free(copy);
    return result;
}
