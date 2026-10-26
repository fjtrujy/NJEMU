#include "common/command_data.h"
#include "common/ui_utf8.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define COMMAND_DATA_MAX_BYTES (16u * 1024u * 1024u)
#define COMMAND_DATA_MAX_ITEMS 512u
#define COMMAND_DATA_MAX_LINES 8192u

typedef struct command_line {
    const unsigned char *text;
    size_t length;
    size_t start;
    size_t next;
} command_line_t;

static int next_line(const unsigned char *data, size_t length,
                     size_t *position, command_line_t *line)
{
    size_t pos = *position;
    size_t end;
    if (pos >= length)
        return 0;
    end = pos;
    while (end < length && data[end] != '\r' && data[end] != '\n')
        ++end;
    line->text = data + pos;
    line->length = end - pos;
    line->start = pos;
    if (end < length && data[end] == '\r')
        ++end;
    if (end < length && data[end] == '\n')
        ++end;
    line->next = end;
    *position = end;
    return 1;
}

static int tag(const command_line_t *line, const char *name)
{
    size_t n = strlen(name);
    return line->length >= n + 1 && line->text[0] == '$'
        && strncasecmp((const char *)line->text + 1, name, n) == 0
        && (line->length == n + 1 || line->text[n + 1] == '='
            || isspace(line->text[n + 1]));
}

static command_line_t value(const command_line_t *line)
{
    command_line_t result = *line;
    size_t i = 0;
    while (i < line->length && line->text[i] != '=')
        ++i;
    if (i == line->length) {
        result.length = 0;
        return result;
    }
    ++i;
    while (i < line->length && (line->text[i] == ' ' || line->text[i] == '\t'))
        ++i;
    result.text += i;
    result.length -= i;
    return result;
}

static int name_matches(const command_line_t *line, const char *game)
{
    command_line_t names = value(line);
    size_t i = 0;
    if (game == NULL || names.length == 0)
        return 0;
    while (i < names.length) {
        size_t start = i;
        size_t end;
        while (i < names.length && names.text[i] != ',')
            ++i;
        end = i;
        while (start < end && isspace(names.text[start]))
            ++start;
        while (end > start && isspace(names.text[end - 1]))
            --end;
        if ((strlen(game) == end - start &&
             strncasecmp((const char *)names.text + start, game, end - start) == 0) ||
            (strcmp(game, "rallych") == 0 && end - start == 6 &&
             strncasecmp((const char *)names.text + start, "trally", 6) == 0))
            return 1;
        if (i < names.length)
            ++i;
    }
    return 0;
}

int command_data_reduce(const unsigned char *data, size_t length,
                        const char *const *games, size_t game_count,
                        unsigned char **output, size_t *output_length)
{
    unsigned char *filtered;
    size_t cursor = 0, used = 0, matched = 0, i;
    int include = 1;
    command_line_t line;
    if (output == NULL || output_length == NULL)
        return 0;
    *output = NULL;
    *output_length = 0;
    if (data == NULL || length == 0 || length > COMMAND_DATA_MAX_BYTES ||
        game_count == 0 || games == NULL || memchr(data, 0, length) != NULL)
        return 0;
    filtered = malloc(length + 1u);
    if (filtered == NULL)
        return 0;
    while (next_line(data, length, &cursor, &line)) {
        if (tag(&line, "info")) {
            include = 0;
            for (i = 0; i < game_count; ++i) {
                if (name_matches(&line, games[i])) {
                    include = 1;
                    ++matched;
                    break;
                }
            }
        }
        if (include) {
            memcpy(filtered + used, data + line.start, line.next - line.start);
            used += line.next - line.start;
        }
    }
    if (matched == 0) {
        free(filtered);
        return 0;
    }
    filtered[used] = 0;
    *output = filtered;
    *output_length = used;
    return 1;
}

int command_data_detect_encoding(const unsigned char *data, size_t length)
{
    size_t i;
    size_t dbcs = 0, printable = 0;
    int high = 0;
    if (data == NULL || length == 0)
        return COMMAND_DATA_LATIN1;
    if (length >= 3 && memcmp(data, "\xef\xbb\xbf", 3) == 0)
        return COMMAND_DATA_UTF8;
    for (i = 0; i < length; ++i)
        if (data[i] >= 0x80) {
            high = 1;
            break;
        }
    if (high && ui_utf8_validate_n((const char *)data, length))
        return COMMAND_DATA_UTF8;
    for (i = 0; i < length; ++i) {
        if (data[i] > 0x20)
            ++printable;
        if (i + 1 < length && data[i] >= 0x81 && data[i] <= 0xfe &&
            data[i + 1] >= 0x40 && data[i + 1] <= 0xfe &&
            data[i + 1] != 0x7f) {
            ++dbcs;
            ++i;
        }
    }
    return printable != 0 && dbcs * 100u / printable > 20u
        ? COMMAND_DATA_GBK : COMMAND_DATA_LATIN1;
}

static int find_section(const unsigned char *data, size_t length,
                        const char *game, size_t *begin, size_t *end)
{
    size_t cursor = 0;
    command_line_t line;
    int found = 0;
    while (next_line(data, length, &cursor, &line)) {
        if (!tag(&line, "info"))
            continue;
        if (found) {
            *end = line.start;
            return 1;
        }
        if (name_matches(&line, game)) {
            found = 1;
            *begin = line.next;
        }
    }
    if (found) {
        *end = length;
        return 1;
    }
    return 0;
}

void command_data_free(command_data_document_t *document)
{
    size_t i;
    if (document == NULL)
        return;
    if (document->items != NULL)
        for (i = 0; i < document->item_count; ++i)
            free(document->items[i]);
    free(document->items);
    free(document->line_counts);
    free(document->storage);
    memset(document, 0, sizeof(*document));
}

int command_data_parse(const unsigned char *data, size_t length,
                       const char *game, const char *parent,
                       command_data_document_t *document)
{
    size_t begin = 0, end = 0, cursor, total = 0, item = 0;
    size_t counts[COMMAND_DATA_MAX_ITEMS] = {0};
    size_t index = 0;
    command_line_t line;
    int in_command = 0;
    int encoding;
    if (document == NULL)
        return 0;
    memset(document, 0, sizeof(*document));
    if (data == NULL || length == 0 || length > COMMAND_DATA_MAX_BYTES ||
        memchr(data, 0, length) != NULL)
        return 0;
    encoding = command_data_detect_encoding(data, length);
    cursor = 0;
    while (next_line(data, length, &cursor, &line)) {
        if (tag(&line, "charset")) {
            command_line_t v = value(&line);
            if (v.length == 3 && strncasecmp((const char *)v.text, "GBK", 3) == 0)
                encoding = COMMAND_DATA_GBK;
            else if (v.length == 9 && strncasecmp((const char *)v.text, "Shift_JIS", 9) == 0)
                encoding = COMMAND_DATA_SHIFTJIS;
            else if ((v.length == 6 && strncasecmp((const char *)v.text, "Latin1", 6) == 0)
                     || (v.length == 10 && strncasecmp((const char *)v.text, "ISO-8859-1", 10) == 0))
                encoding = COMMAND_DATA_LATIN1;
            else if (v.length == 5 && strncasecmp((const char *)v.text, "UTF-8", 5) == 0)
                encoding = COMMAND_DATA_UTF8;
        }
    }
    if (!find_section(data, length, game, &begin, &end) &&
        !find_section(data, length, parent, &begin, &end))
        return 0;
    cursor = begin;
    while (cursor < end && next_line(data, end, &cursor, &line)) {
        if (tag(&line, "cmd")) {
            if (in_command || total == COMMAND_DATA_MAX_ITEMS)
                return 0;
            in_command = 1;
            counts[total] = 0;
        } else if (tag(&line, "end")) {
            if (in_command) {
                if (counts[total] == 0)
                    return 0;
                ++total;
                in_command = 0;
            }
        } else if (in_command && line.length > 0 && line.text[0] == '$') {
            return 0;
        } else if (in_command) {
            if (++counts[total] > COMMAND_DATA_MAX_LINES)
                return 0;
        }
    }
    if (in_command || total == 0)
        return 0;
    document->storage = malloc(end - begin + 1u);
    document->items = calloc(total, sizeof(*document->items));
    document->line_counts = calloc(total, sizeof(*document->line_counts));
    if (document->storage == NULL || document->items == NULL ||
        document->line_counts == NULL)
        goto failure;
    memcpy(document->storage, data + begin, end - begin);
    document->storage[end - begin] = 0;
    document->item_count = total;
    document->encoding = encoding;
    for (item = 0; item < total; ++item) {
        document->line_counts[item] = counts[item];
        document->items[item] = calloc(counts[item], sizeof(char *));
        if (document->items[item] == NULL)
            goto failure;
    }
    cursor = 0;
    item = 0;
    in_command = 0;
    while (next_line((const unsigned char *)document->storage,
                     end - begin, &cursor, &line)) {
        if (tag(&line, "cmd")) {
            in_command = 1;
            index = 0;
        } else if (tag(&line, "end")) {
            if (in_command) {
                ++item;
                in_command = 0;
            }
        } else if (in_command) {
            if (item >= total || index >= document->line_counts[item])
                goto failure;
            document->items[item][index++] = (char *)line.text;
        }
        memset(document->storage + line.start + line.length, 0,
               line.next - line.start - line.length);
    }
    return 1;
failure:
    command_data_free(document);
    return 0;
}
