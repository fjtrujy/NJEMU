#include "commands.h"

#include "file.h"
#include "text.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ppm_image {
    host_buffer_t file;
    size_t width;
    size_t height;
    const uint8_t *pixels;
} ppm_image_t;

static void ppm_init(ppm_image_t *image)
{
    host_buffer_init(&image->file);
    image->width = 0;
    image->height = 0;
    image->pixels = NULL;
}

static void ppm_free(ppm_image_t *image)
{
    host_buffer_free(&image->file);
    image->pixels = NULL;
}

static int is_space(uint8_t value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n'
        || value == '\f' || value == '\v';
}

static int next_token(const uint8_t **cursor, const uint8_t *end, char *token, size_t token_size)
{
    const uint8_t *p = *cursor;
    size_t length = 0;

    for (;;) {
        while (p < end && is_space(*p))
            ++p;
        if (p >= end)
            return 0;
        if (*p != '#')
            break;
        while (p < end && *p != '\n')
            ++p;
    }
    while (p < end && !is_space(*p) && *p != '#') {
        if (length + 1 >= token_size)
            return 0;
        token[length++] = (char)*p++;
    }
    token[length] = '\0';
    *cursor = p;
    return length != 0;
}

static int read_ppm(const char *path, ppm_image_t *image)
{
    const uint8_t *cursor;
    const uint8_t *end;
    char token[64];
    size_t max_value;
    size_t pixel_size;

    if (!host_read_file(path, &image->file)) {
        fprintf(stderr, "compare-frames: could not read %s\n", path);
        return 0;
    }
    cursor = image->file.data;
    end = image->file.data + image->file.size;
    if (!next_token(&cursor, end, token, sizeof(token)) || strcmp(token, "P6") != 0
        || !next_token(&cursor, end, token, sizeof(token)) || !host_parse_size(token, &image->width)
        || !next_token(&cursor, end, token, sizeof(token)) || !host_parse_size(token, &image->height)
        || !next_token(&cursor, end, token, sizeof(token)) || !host_parse_size(token, &max_value)
        || max_value != 255) {
        fprintf(stderr, "compare-frames: %s: not an 8-bit binary PPM\n", path);
        return 0;
    }
    if (cursor >= end || !is_space(*cursor)) {
        fprintf(stderr, "compare-frames: %s: malformed PPM header\n", path);
        return 0;
    }
    ++cursor;
    if (image->width != 0 && image->height > (size_t)-1 / image->width) {
        fprintf(stderr, "compare-frames: %s: image size overflow\n", path);
        return 0;
    }
    pixel_size = image->width * image->height;
    if (pixel_size > (size_t)-1 / 3)
        return 0;
    pixel_size *= 3;
    if ((size_t)(end - cursor) < pixel_size) {
        fprintf(stderr, "compare-frames: %s: truncated PPM pixels\n", path);
        return 0;
    }
    image->pixels = cursor;
    return 1;
}

static void quantized_pixel(const ppm_image_t *image, size_t x, size_t y, uint8_t out[3])
{
    size_t offset = (y * image->width + x) * 3;
    out[0] = image->pixels[offset] >> 3;
    out[1] = image->pixels[offset + 1] >> 3;
    out[2] = image->pixels[offset + 2] >> 3;
}

static int same_color(const uint8_t a[3], const uint8_t b[3])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static int neighborhood_contains(const ppm_image_t *image, size_t x, size_t y,
    const uint8_t color[3])
{
    size_t min_x = x > 0 ? x - 1 : 0;
    size_t min_y = y > 0 ? y - 1 : 0;
    size_t max_x = x + 1 < image->width ? x + 1 : image->width - 1;
    size_t max_y = y + 1 < image->height ? y + 1 : image->height - 1;
    size_t ny;

    for (ny = min_y; ny <= max_y; ++ny) {
        size_t nx;
        for (nx = min_x; nx <= max_x; ++nx) {
            uint8_t candidate[3];
            quantized_pixel(image, nx, ny, candidate);
            if (same_color(candidate, color))
                return 1;
        }
    }
    return 0;
}

static int write_diff(const char *path, size_t width, size_t height, const host_buffer_t *pixels)
{
    host_buffer_t output;
    char header[96];
    int header_length;
    int ok;

    header_length = snprintf(header, sizeof(header), "P6\n%lu %lu\n255\n",
        (unsigned long)width, (unsigned long)height);
    if (header_length < 0 || (size_t)header_length >= sizeof(header))
        return 0;
    host_buffer_init(&output);
    ok = host_buffer_append(&output, header, (size_t)header_length)
        && host_buffer_append(&output, pixels->data, pixels->size)
        && host_write_file(path, output.data, output.size);
    host_buffer_free(&output);
    return ok;
}

static void usage(void)
{
    fprintf(stderr, "usage: njemu-tool compare-frames REFERENCE.ppm CANDIDATE.ppm [DIFF.ppm]\n");
}

int command_compare_frames(int argc, char **argv)
{
    ppm_image_t reference;
    ppm_image_t candidate;
    host_buffer_t diff;
    size_t x, y;
    size_t real = 0;
    size_t ties = 0;
    size_t bbox_min_x = 0, bbox_min_y = 0, bbox_max_x = 0, bbox_max_y = 0;
    int have_bbox = 0;
    int result = 1;

    if (argc < 3 || argc > 4) {
        usage();
        return 2;
    }
    ppm_init(&reference);
    ppm_init(&candidate);
    host_buffer_init(&diff);
    if (!read_ppm(argv[1], &reference) || !read_ppm(argv[2], &candidate))
        goto out;
    if (reference.width != candidate.width || reference.height != candidate.height) {
        printf("size mismatch: %lux%lu vs %lux%lu\n",
            (unsigned long)reference.width, (unsigned long)reference.height,
            (unsigned long)candidate.width, (unsigned long)candidate.height);
        goto out;
    }
    if (!host_buffer_reserve(&diff, reference.width * reference.height * 3)) {
        fprintf(stderr, "compare-frames: out of memory\n");
        goto out;
    }
    diff.size = reference.width * reference.height * 3;

    for (y = 0; y < reference.height; ++y) {
        for (x = 0; x < reference.width; ++x) {
            size_t offset = (y * reference.width + x) * 3;
            uint8_t a[3], b[3];
            quantized_pixel(&reference, x, y, a);
            quantized_pixel(&candidate, x, y, b);
            if (same_color(a, b)) {
                diff.data[offset] = reference.pixels[offset] / 4;
                diff.data[offset + 1] = reference.pixels[offset + 1] / 4;
                diff.data[offset + 2] = reference.pixels[offset + 2] / 4;
            } else if (neighborhood_contains(&reference, x, y, b)
                || neighborhood_contains(&candidate, x, y, a)) {
                ++ties;
                diff.data[offset] = 0xff;
                diff.data[offset + 1] = 0xff;
                diff.data[offset + 2] = 0x00;
            } else {
                ++real;
                diff.data[offset] = 0xff;
                diff.data[offset + 1] = 0x00;
                diff.data[offset + 2] = 0xff;
                if (!have_bbox) {
                    bbox_min_x = bbox_max_x = x;
                    bbox_min_y = bbox_max_y = y;
                    have_bbox = 1;
                } else {
                    if (x < bbox_min_x) bbox_min_x = x;
                    if (x > bbox_max_x) bbox_max_x = x;
                    if (y < bbox_min_y) bbox_min_y = y;
                    if (y > bbox_max_y) bbox_max_y = y;
                }
            }
        }
    }

    printf("%s: %lu/%lu pixels differ (%lu sampling ties)", argv[2],
        (unsigned long)real, (unsigned long)(reference.width * reference.height),
        (unsigned long)ties);
    if (have_bbox)
        printf(", bbox (%lu, %lu, %lu, %lu)", (unsigned long)bbox_min_x,
            (unsigned long)bbox_min_y, (unsigned long)bbox_max_x, (unsigned long)bbox_max_y);
    putchar('\n');

    if (argc == 4 && !write_diff(argv[3], reference.width, reference.height, &diff)) {
        fprintf(stderr, "compare-frames: could not write %s\n", argv[3]);
        goto out;
    }
    result = real != 0 ? 1 : 0;
out:
    host_buffer_free(&diff);
    ppm_free(&candidate);
    ppm_free(&reference);
    return result;
}
