#include "commands.h"
#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GitHub release-download metrics are public. GitHub repository traffic is
 * private, and a missing/unauthorized traffic response must not destroy history. */
typedef struct traffic_day {
    char date[11];
    long long count;
    long long uniques;
} traffic_day_t;

typedef struct traffic_series {
    traffic_day_t *items;
    size_t count;
} traffic_series_t;

static const host_json_value_t *member(const host_json_value_t *value, const char *name)
{
    return host_json_object_get(value, name);
}

static const char *string(const host_json_value_t *value)
{
    return value && value->type == HOST_JSON_STRING ? value->as.string.data : "";
}

static long long number(const host_json_value_t *value)
{
    return value && value->type == HOST_JSON_NUMBER && value->as.number > 0
        ? (long long)value->as.number : 0;
}

static int date_valid(const char *date)
{
    size_t i;
    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') return 0;
    for (i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && !isdigit((unsigned char)date[i])) return 0;
    return 1;
}

static void quote(FILE *output, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    fputc('"', output);
    while (*cursor) {
        unsigned char c = *cursor++;
        if (c == '"' || c == '\\') { fputc('\\', output); fputc(c, output); }
        else if (c == '\n') fputs("\\n", output);
        else if (c == '\r') fputs("\\r", output);
        else if (c == '\t') fputs("\\t", output);
        else if (c < 32) fprintf(output, "\\u%04x", c);
        else fputc(c, output);
    }
    fputc('"', output);
}

static void copy_json(FILE *output, const host_json_value_t *value)
{
    size_t i;
    if (!value) { fputs("null", output); return; }
    switch (value->type) {
    case HOST_JSON_NULL: fputs("null", output); break;
    case HOST_JSON_BOOL: fputs(value->as.boolean ? "true" : "false", output); break;
    case HOST_JSON_NUMBER: fprintf(output, "%lld", (long long)value->as.number); break;
    case HOST_JSON_STRING: quote(output, string(value)); break;
    case HOST_JSON_ARRAY:
        fputc('[', output);
        for (i = 0; i < value->as.array.count; ++i) {
            if (i) fputc(',', output);
            copy_json(output, value->as.array.items[i]);
        }
        fputc(']', output);
        break;
    case HOST_JSON_OBJECT:
        fputc('{', output);
        for (i = 0; i < value->as.object.count; ++i) {
            if (i) fputc(',', output);
            quote(output, value->as.object.members[i].key.data);
            fputc(':', output);
            copy_json(output, value->as.object.members[i].value);
        }
        fputc('}', output);
        break;
    }
}

static int archive_traffic(traffic_series_t *series, const host_json_value_t *records)
{
    size_t i, j;
    if (!records || records->type != HOST_JSON_ARRAY) return 1;
    for (i = 0; i < records->as.array.count; ++i) {
        const host_json_value_t *item = records->as.array.items[i];
        const char *date = string(member(item, "date"));
        char day[11];
        traffic_day_t *new_items;
        if (!date[0]) date = string(member(item, "timestamp"));
        if (strlen(date) < 10) continue;
        memcpy(day, date, 10);
        day[10] = '\0';
        if (!date_valid(day)) continue;
        for (j = 0; j < series->count; ++j)
            if (strcmp(series->items[j].date, day) == 0) break;
        if (j == series->count) {
            new_items = (traffic_day_t *)realloc(series->items,
                (series->count + 1) * sizeof(*series->items));
            if (!new_items) return 0;
            series->items = new_items;
            series->count++;
        }
        memcpy(series->items[j].date, day, 11);
        series->items[j].count = number(member(item, "count"));
        series->items[j].uniques = number(member(item, "uniques"));
    }
    return 1;
}

static int by_date(const void *left, const void *right)
{
    return strcmp(((const traffic_day_t *)left)->date,
                  ((const traffic_day_t *)right)->date);
}

static void write_traffic(FILE *out, const traffic_series_t *series)
{
    size_t i;
    fputc('[', out);
    for (i = 0; i < series->count; ++i) {
        if (i) fputc(',', out);
        fprintf(out, "{\"date\":\"%s\",\"count\":%lld,\"uniques\":%lld}",
            series->items[i].date, series->items[i].count, series->items[i].uniques);
    }
    fputc(']', out);
}

static int is_zip(const char *name)
{
    size_t length = strlen(name);
    return length >= 10 && strncmp(name, "njemu-", 6) == 0 &&
        strcmp(name + length - 4, ".zip") == 0;
}

static void write_release(FILE *out, const host_json_value_t *release)
{
    const host_json_value_t *assets = member(release, "assets");
    size_t i;
    int first = 1;
    fputs("{\"tag\":", out);
    quote(out, string(member(release, "tag_name")));
    fputs(",\"assets\":[", out);
    if (assets && assets->type == HOST_JSON_ARRAY) {
        for (i = 0; i < assets->as.array.count; ++i) {
            const host_json_value_t *asset = assets->as.array.items[i];
            const char *name = string(member(asset, "name"));
            if (!is_zip(name)) continue;
            if (!first) fputc(',', out);
            first = 0;
            fputs("{\"name\":", out);
            quote(out, name);
            fprintf(out, ",\"id\":%lld,\"downloads\":%lld}",
                number(member(asset, "id")), number(member(asset, "download_count")));
        }
    }
    fputs("]}", out);
}

static void write_releases(FILE *out, const host_json_value_t *data)
{
    size_t i, j;
    int first = 1;
    fputc('[', out);
    if (!data || data->type != HOST_JSON_ARRAY) { fputc(']', out); return; }
    for (i = 0; i < data->as.array.count; ++i) {
        const host_json_value_t *page = data->as.array.items[i];
        const host_json_value_t *list = page->type == HOST_JSON_ARRAY ? page : data;
        if (page->type != HOST_JSON_ARRAY && i) break;
        for (j = 0; j < list->as.array.count; ++j) {
            const host_json_value_t *release = list->as.array.items[j];
            const char *tag = string(member(release, "tag_name"));
            if (!tag[0]) continue;
            if (!first) fputc(',', out);
            first = 0;
            write_release(out, release);
        }
        if (page->type != HOST_JSON_ARRAY) break;
    }
    fputc(']', out);
}

int command_stats_snapshot(int argc, char **argv)
{
    const host_json_value_t *old_snapshots, *old_traffic;
    host_json_value_t *releases = NULL, *views = NULL, *clones = NULL, *previous = NULL;
    traffic_series_t view_days = {0}, clone_days = {0};
    FILE *output = NULL;
    char error[256];
    int ok = 0, count = 0;
    size_t i;

    if (argc != 7 || !date_valid(argv[6])) {
        fprintf(stderr, "usage: njemu-tool stats-snapshot RELEASES VIEWS|- CLONES|- HISTORY|- OUTPUT YYYY-MM-DD\n");
        return 2;
    }
    releases = host_json_parse_file(argv[1], error, sizeof(error));
    if (!releases || releases->type != HOST_JSON_ARRAY) {
        fprintf(stderr, "stats-snapshot: invalid release response: %s\n", error);
        goto done;
    }
    if (strcmp(argv[2], "-") != 0) views = host_json_parse_file(argv[2], error, sizeof(error));
    if (strcmp(argv[3], "-") != 0) clones = host_json_parse_file(argv[3], error, sizeof(error));
    if (strcmp(argv[4], "-") != 0) {
        previous = host_json_parse_file(argv[4], error, sizeof(error));
        if (!previous || number(member(previous, "schema")) != 1 ||
            !member(previous, "snapshots") ||
            member(previous, "snapshots")->type != HOST_JSON_ARRAY) {
            fprintf(stderr, "stats-snapshot: invalid previous archive; refusing to discard it\n");
            goto done;
        }
    }
    old_traffic = member(previous, "traffic");
    if (!archive_traffic(&view_days, member(old_traffic, "views")) ||
        !archive_traffic(&clone_days, member(old_traffic, "clones")) ||
        !archive_traffic(&view_days, views) || !archive_traffic(&clone_days, clones)) {
        fprintf(stderr, "stats-snapshot: out of memory merging traffic\n");
        goto done;
    }
    if (view_days.count > 1)
        qsort(view_days.items, view_days.count, sizeof(*view_days.items), by_date);
    if (clone_days.count > 1)
        qsort(clone_days.items, clone_days.count, sizeof(*clone_days.items), by_date);
    output = fopen(argv[5], "wb");
    if (!output) { perror(argv[5]); goto done; }
    fputs("{\"schema\":1,\"updated\":", output);
    quote(output, argv[6]);
    fputs(",\"snapshots\":[", output);
    old_snapshots = member(previous, "snapshots");
    if (old_snapshots && old_snapshots->type == HOST_JSON_ARRAY) {
        for (i = 0; i < old_snapshots->as.array.count; ++i) {
            const host_json_value_t *snapshot = old_snapshots->as.array.items[i];
            if (strcmp(string(member(snapshot, "date")), argv[6]) == 0) continue;
            if (count++) fputc(',', output);
            copy_json(output, snapshot);
        }
    }
    if (count) fputc(',', output);
    fputs("{\"date\":", output);
    quote(output, argv[6]);
    fputs(",\"releases\":", output);
    write_releases(output, releases);
    fputs("}],\"traffic\":{\"views\":", output);
    write_traffic(output, &view_days);
    fputs(",\"clones\":", output);
    write_traffic(output, &clone_days);
    fputs("}}\n", output);
    ok = !ferror(output) && fclose(output) == 0;
    output = NULL;
    if (!ok) fprintf(stderr, "stats-snapshot: output write failed\n");
done:
    if (output) fclose(output);
    free(view_days.items);
    free(clone_days.items);
    host_json_free(releases);
    host_json_free(views);
    host_json_free(clones);
    host_json_free(previous);
    return ok ? 0 : 1;
}
