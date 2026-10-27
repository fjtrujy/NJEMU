#ifndef COMMON_COMMAND_DATA_H
#define COMMON_COMMAND_DATA_H

#include <stddef.h>

enum {
    COMMAND_DATA_LATIN1 = 1,
    COMMAND_DATA_GBK = 2,
    COMMAND_DATA_SHIFTJIS = 3,
    COMMAND_DATA_UTF8 = 4
};

typedef struct command_data_document {
    char *storage;
    char ***items;
    size_t *line_counts;
    size_t item_count;
    int encoding;
} command_data_document_t;

/* The input is an untrusted, length-bounded MAME Plus command.dat file. */
int command_data_parse(const unsigned char *data, size_t length,
                       const char *game, const char *parent,
                       command_data_document_t *document);
void command_data_free(command_data_document_t *document);
int command_data_detect_encoding(const unsigned char *data, size_t length);

/* Reads the matching game only; the caller owns and closes fd.
 * section_bytes may be NULL and reports the sole retained text allocation. */
int command_data_load_fd(int fd, const char *game, const char *parent,
                         command_data_document_t *document,
                         size_t *section_bytes);

/* Keep the original line endings, BOM and command tokens byte-for-byte. */
int command_data_reduce(const unsigned char *data, size_t length,
                        const char *const *games, size_t game_count,
                        unsigned char **output, size_t *output_length);

#endif
