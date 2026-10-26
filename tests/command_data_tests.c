#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/command_data.h"

static void check_selection(const unsigned char *input, size_t bytes,
                            const char *game, const char *parent,
                            int encoding, const char *first, size_t expected)
{
    command_data_document_t doc = {0};
    assert(command_data_parse(input, bytes, game, parent, &doc));
    assert(doc.encoding == encoding);
    assert(doc.item_count == expected);
    assert(strcmp(doc.items[0][0], first) == 0);
    assert(doc.line_counts[0] >= 1);
    command_data_free(&doc);
    command_data_free(&doc);
}

static void check_invalid(const unsigned char *input, size_t bytes)
{
    command_data_document_t doc = {0};
    assert(!command_data_parse(input, bytes, "kof98", NULL, &doc));
    command_data_free(&doc);
}

int main(int argc, char **argv)
{
    const char sample[] =
        "\xef\xbb\xbf# Japanese\r\n"
        "$info=kof98,kof98h\r\n"
        "$cmd\r\n"
        "[ Basic moves ]\r\n"
        "_6_+_A\r\n"
        "\xe3\x81\x82\r\n"
        "$end\r\n"
        "$cmd\r\n"
        "Second\r\n"
        "$end\r\n"
        "$info=mslug3\r\n"
        "$cmd\r\n"
        "Metal Slug 3\r\n"
        "$end\r\n";
    const char lf[] = "$info=kof98\n$cmd\nLF\n$end";
    const char cr[] = "$info=kof98\r$cmd\rCR\r$end";
    const char legacy[] = "$charset=Shift_JIS\n$info=kof98\n$cmd\nLegacy\n$end\n";
    const char bad1[] = "$info=kof98\n$cmd\nMissing end";
    const char bad2[] = "$info=kof98\n$cmd\n$end\n";
    const char bad3[] = "$info=kof98\n$cmd\nTitle\n$cmd\nTitle\n$end\n";
    const unsigned char nul[] = {'$', 'i', 'n', 'f', 'o', 0, '$'};
    char *long_line = malloc(11000);
    const char *games[] = {"kof98"};
    unsigned char *reduced = NULL;
    size_t reduced_size = 0;
    FILE *file;
    long length;
    unsigned char *data;

    assert(long_line != NULL);
    memset(long_line, 'a', 10999);
    long_line[10999] = 0;
    check_selection((const unsigned char *)sample, strlen(sample), "kof98", NULL,
                    COMMAND_DATA_UTF8, "[ Basic moves ]", 2);
    check_selection((const unsigned char *)sample, strlen(sample), "kof98h", NULL,
                    COMMAND_DATA_UTF8, "[ Basic moves ]", 2);
    check_selection((const unsigned char *)sample, strlen(sample), "kof98j", "kof98",
                    COMMAND_DATA_UTF8, "[ Basic moves ]", 2);
    check_selection((const unsigned char *)sample, strlen(sample), "mslug3", NULL,
                    COMMAND_DATA_UTF8, "Metal Slug 3", 1);
    check_selection((const unsigned char *)lf, strlen(lf), "kof98", NULL,
                    COMMAND_DATA_LATIN1, "LF", 1);
    check_selection((const unsigned char *)cr, strlen(cr), "kof98", NULL,
                    COMMAND_DATA_LATIN1, "CR", 1);
    check_selection((const unsigned char *)legacy, strlen(legacy), "kof98", NULL,
                    COMMAND_DATA_SHIFTJIS, "Legacy", 1);
    check_invalid((const unsigned char *)bad1, strlen(bad1));
    check_invalid((const unsigned char *)bad2, strlen(bad2));
    check_invalid((const unsigned char *)bad3, strlen(bad3));
    check_invalid(nul, sizeof(nul));
    check_invalid((const unsigned char *)"", 0);
    assert(command_data_reduce((const unsigned char *)sample, strlen(sample),
                               games, 1, &reduced, &reduced_size));
    assert(reduced_size < strlen(sample));
    assert(strstr((const char *)reduced, "$info=kof98") != NULL);
    assert(strstr((const char *)reduced, "$info=mslug3") == NULL);
    assert(memcmp(reduced, "\xef\xbb\xbf", 3) == 0);
    assert(strstr((const char *)reduced, "_6_+_A\r\n") != NULL);
    free(reduced);
    reduced = NULL;
    assert(!command_data_reduce(nul, sizeof(nul), games, 1,
                                &reduced, &reduced_size));
    assert(reduced == NULL);
    assert(command_data_detect_encoding((const unsigned char *)" \t\n", 3)
           == COMMAND_DATA_LATIN1);
    assert(command_data_detect_encoding((const unsigned char *)"\xc3", 1)
           == COMMAND_DATA_LATIN1);
    {
        char *large = malloc(12000);
        size_t n;
        assert(large != NULL);
        n = (size_t)snprintf(large, 12000, "$info=kof98\n$cmd\n%s\n$end\n", long_line);
        check_selection((const unsigned char *)large, n, "kof98", NULL,
                        COMMAND_DATA_LATIN1, long_line, 1);
        free(large);
    }
    free(long_line);

    /* An optional untracked fixture tests the reporter's exact 0.168 archive. */
    if (argc >= 2) {
        command_data_document_t doc = {0};
        file = fopen(argv[1], "rb");
        assert(file != NULL);
        assert(fseek(file, 0, SEEK_END) == 0);
        length = ftell(file);
        assert(length > 0 && length < 16 * 1024 * 1024);
        rewind(file);
        data = malloc((size_t)length);
        assert(data != NULL);
        assert(fread(data, 1, (size_t)length, file) == (size_t)length);
        fclose(file);
        assert(command_data_parse(data, (size_t)length, "kof98", NULL, &doc));
        assert(doc.encoding == COMMAND_DATA_UTF8);
        assert(doc.item_count >= 1);
        assert(strstr(doc.items[0][0], "[ ") != NULL);
        command_data_free(&doc);
        assert(command_data_parse(data, (size_t)length, "mslug3", NULL, &doc));
        assert(doc.encoding == COMMAND_DATA_UTF8);
        assert(doc.item_count >= 1);
        command_data_free(&doc);
        assert(command_data_reduce(data, (size_t)length, games, 1,
                                   &reduced, &reduced_size));
        assert(reduced_size < (size_t)length);
        assert(command_data_parse(reduced, reduced_size, "kof98", NULL, &doc));
        assert(doc.encoding == COMMAND_DATA_UTF8);
        command_data_free(&doc);
        free(reduced);
        free(data);
        printf("MAME Plus 0.168 fixture: PASS\n");
    }
    puts("command_data_tests: PASS");
    return 0;
}
