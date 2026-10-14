#include "commands.h"

#include <png.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#ifdef _WIN32
#include <direct.h>
#define make_directory(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define make_directory(path) mkdir(path, 0755)
#endif

typedef struct artwork_image {
    int width;
    int height;
    unsigned char *pixels; /* Top-down RGBA. */
} artwork_image_t;

typedef struct artwork_core {
    const char *name;
    double icon_focus_from_bottom;
} artwork_core_t;

static const artwork_core_t cores[] = {
    {"cps1", 0.44},
    {"cps2", 0.39},
    {"mvs", 0.41},
    {"ncdz", 0.35}
};

static void image_free(artwork_image_t *image)
{
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

static int image_create(artwork_image_t *image, int width, int height)
{
    size_t count;
    if (width <= 0 || height <= 0 || (size_t)width > SIZE_MAX / 4 / (size_t)height)
        return 0;
    count = (size_t)width * (size_t)height * 4;
    image->pixels = (unsigned char *)calloc(count, 1);
    if (!image->pixels)
        return 0;
    image->width = width;
    image->height = height;
    return 1;
}

static int image_read(const char *path, artwork_image_t *image)
{
    png_image png;
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&png, path)) {
        fprintf(stderr, "artwork: PNG open failed: %s\n", path);
        return 0;
    }
    if (png.width > INT32_MAX || png.height > INT32_MAX ||
            !image_create(image, (int)png.width, (int)png.height)) {
        png_image_free(&png);
        fprintf(stderr, "artwork: unsupported image dimensions: %s\n", path);
        return 0;
    }
    png.format = PNG_FORMAT_RGBA;
    if (!png_image_finish_read(&png, NULL, image->pixels, 0, NULL)) {
        fprintf(stderr, "artwork: PNG decode failed: %s (%s)\n", path, png.message);
        image_free(image);
        png_image_free(&png);
        return 0;
    }
    png_image_free(&png);
    return 1;
}

static int write_rgba(const char *path, const artwork_image_t *image)
{
    png_image png;
    memset(&png, 0, sizeof(png));
    png.version = PNG_IMAGE_VERSION;
    png.width = (png_uint_32)image->width;
    png.height = (png_uint_32)image->height;
    png.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&png, path, 0, image->pixels, 0, NULL)) {
        fprintf(stderr, "artwork: PNG encode failed: %s (%s)\n", path, png.message);
        return 0;
    }
    return 1;
}

static int write_palette(const char *path, const artwork_image_t *image)
{
    FILE *file = NULL;
    png_structp png = NULL;
    png_infop info = NULL;
    unsigned char *indices = NULL;
    png_bytep *rows = NULL;
    png_color palette[256];
    int ok = 0;
    int x, y, r, g, b;
    size_t i;

    file = fopen(path, "wb");
    if (!file)
        goto done;
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
        goto done;
    info = png_create_info_struct(png);
    if (!info)
        goto done;
    if (setjmp(png_jmpbuf(png)))
        goto done;
    indices = (unsigned char *)malloc((size_t)image->width * (size_t)image->height);
    rows = (png_bytep *)malloc((size_t)image->height * sizeof(*rows));
    if (!indices || !rows)
        goto done;
    for (y = 0; y < image->height; ++y) {
        rows[y] = indices + (size_t)y * (size_t)image->width;
        for (x = 0; x < image->width; ++x) {
            i = ((size_t)y * (size_t)image->width + (size_t)x) * 4;
            indices[(size_t)y * (size_t)image->width + (size_t)x] =
                (unsigned char)((image->pixels[i] & 0xe0u) |
                                ((image->pixels[i + 1] >> 5) << 2) |
                                (image->pixels[i + 2] >> 6));
        }
    }
    for (r = 0; r < 8; ++r)
        for (g = 0; g < 8; ++g)
            for (b = 0; b < 4; ++b) {
                i = (size_t)(r * 32 + g * 4 + b);
                palette[i].red = (png_byte)(r * 255 / 7);
                palette[i].green = (png_byte)(g * 255 / 7);
                palette[i].blue = (png_byte)(b * 255 / 3);
            }
    png_init_io(png, file);
    png_set_IHDR(png, info, (png_uint_32)image->width, (png_uint_32)image->height,
                 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_set_PLTE(png, info, palette, 256);
    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, info);
    ok = 1;

done:
    if (png)
        png_destroy_write_struct(&png, &info);
    free(rows);
    free(indices);
    if (file)
        fclose(file);
    if (!ok)
        fprintf(stderr, "artwork: failed to encode indexed PNG: %s\n", path);
    return ok;
}

static double clamp_double(double value, double low, double high)
{
    return value < low ? low : (value > high ? high : value);
}

static void sample_image(const artwork_image_t *source, double x, double y,
                         unsigned char *result)
{
    int x0, x1, y0, y1, c;
    double fx, fy, top, bottom, value;
    x = clamp_double(x, 0, source->width - 1);
    y = clamp_double(y, 0, source->height - 1);
    x0 = (int)x; y0 = (int)y;
    x1 = x0 + 1 < source->width ? x0 + 1 : x0;
    y1 = y0 + 1 < source->height ? y0 + 1 : y0;
    fx = x - x0; fy = y - y0;
    for (c = 0; c < 4; ++c) {
        top = source->pixels[((size_t)y0 * source->width + x0) * 4 + c] * (1 - fx)
            + source->pixels[((size_t)y0 * source->width + x1) * 4 + c] * fx;
        bottom = source->pixels[((size_t)y1 * source->width + x0) * 4 + c] * (1 - fx)
               + source->pixels[((size_t)y1 * source->width + x1) * 4 + c] * fx;
        value = top * (1 - fy) + bottom * fy;
        result[c] = (unsigned char)(value + 0.5);
    }
}

static void crop_resize(const artwork_image_t *source, artwork_image_t *destination,
                        double left, double top, double crop_width, double crop_height)
{
    int x, y;
    for (y = 0; y < destination->height; ++y) {
        double sy = top + ((y + 0.5) / destination->height) * crop_height - 0.5;
        for (x = 0; x < destination->width; ++x) {
            double sx = left + ((x + 0.5) / destination->width) * crop_width - 0.5;
            sample_image(source, sx, sy, destination->pixels +
                         ((size_t)y * destination->width + x) * 4);
        }
    }
}

static void crop_cover(const artwork_image_t *source, artwork_image_t *destination)
{
    double width = source->width, height = source->height;
    double source_aspect = width / height;
    double target_aspect = (double)destination->width / destination->height;
    if (source_aspect > target_aspect)
        width = height * target_aspect;
    else
        height = width / target_aspect;
    crop_resize(source, destination, (source->width - width) / 2,
                (source->height - height) / 2, width, height);
}

static void crop_icon(const artwork_image_t *source, artwork_image_t *destination,
                      double focus_from_bottom)
{
    double width, height, top, center;
    if (destination->width == destination->height) {
        crop_cover(source, destination);
        return;
    }
    width = source->width * 0.97;
    height = width * destination->height / destination->width;
    center = source->height * (1.0 - focus_from_bottom);
    top = clamp_double(center - height / 2, 0, source->height - height);
    crop_resize(source, destination, (source->width - width) / 2, top, width, height);
}

/* The small PSP supplementary overlay remains transparent except for its caption. */
static const struct { char c; unsigned char lines[7]; } glyphs[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'N', {17, 25, 21, 21, 19, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}
};

static void overlay_caption(artwork_image_t *image)
{
    const char *caption = "OPEN SOURCE ARCADE";
    int x, y;
    size_t i, g;
    for (y = 241; y < 264; ++y) {
        for (x = 338; x < 473; ++x) {
            size_t offset = ((size_t)y * image->width + x) * 4;
            image->pixels[offset + 3] = 158;
        }
    }
    for (i = 0; caption[i]; ++i) {
        if (caption[i] == ' ')
            continue;
        for (g = 0; g < sizeof(glyphs) / sizeof(glyphs[0]); ++g)
            if (glyphs[g].c == caption[i])
                break;
        if (g == sizeof(glyphs) / sizeof(glyphs[0]))
            continue;
        for (y = 0; y < 7; ++y)
            for (x = 0; x < 5; ++x)
                if (glyphs[g].lines[y] & (1 << (4 - x))) {
                    size_t offset = ((size_t)(249 + y) * image->width + 347 + i * 6 + x) * 4;
                    image->pixels[offset] = image->pixels[offset + 1] =
                        image->pixels[offset + 2] = 255;
                    image->pixels[offset + 3] = 225;
                }
    }
}

static int output_image(const char *root, const char *core, const char *filename,
                        const artwork_image_t *source, int width, int height,
                        int icon, double focus, int indexed, int overlay)
{
    char path[1024];
    artwork_image_t result = {0};
    int success = 0;
    if (snprintf(path, sizeof(path), "%s/%s/%s", root, core, filename) >= (int)sizeof(path) ||
            !image_create(&result, width, height))
        return 0;
    if (overlay)
        overlay_caption(&result);
    else if (icon)
        crop_icon(source, &result, focus);
    else
        crop_cover(source, &result);
    success = indexed ? write_palette(path, &result) : write_rgba(path, &result);
    image_free(&result);
    return success;
}

static int build_core(const char *source_root, const char *destination_root,
                      const artwork_core_t *core)
{
    char path[1024];
    artwork_image_t background = {0}, icon = {0};
    int success = 0;
    if (snprintf(path, sizeof(path), "%s/%s", destination_root, core->name) >=
            (int)sizeof(path))
        return 0;
    if (make_directory(path) != 0 && errno != EEXIST) {
        fprintf(stderr, "artwork: cannot create directory: %s\n", path);
        return 0;
    }
    if (snprintf(path, sizeof(path), "%s/%s/source_1666_944.png",
                 source_root, core->name) >= (int)sizeof(path) || !image_read(path, &background))
        goto done;
    if (snprintf(path, sizeof(path), "%s/%s/icon_source_1254_1254.png",
                 source_root, core->name) >= (int)sizeof(path) || !image_read(path, &icon))
        goto done;
    if (background.width != 1666 || background.height != 944 ||
            icon.width != 1254 || icon.height != 1254) {
        fprintf(stderr, "artwork: unexpected source dimensions for %s\n", core->name);
        goto done;
    }
#define OUTPUT(filename, src, w, h, is_icon, is_indexed, is_overlay) \
    if (!output_image(destination_root, core->name, filename, src, w, h, is_icon, \
                      core->icon_focus_from_bottom, is_indexed, is_overlay)) goto done
    OUTPUT("icon_144_80.png", &icon, 144, 80, 1, 0, 0);
    OUTPUT("pic0_480_272.png", &background, 480, 272, 0, 0, 1);
    OUTPUT("pic1_480_272.png", &background, 480, 272, 0, 0, 0);
    OUTPUT("icon_128_128.png", &icon, 128, 128, 1, 1, 0);
    OUTPUT("pic0_960_544.png", &background, 960, 544, 0, 1, 0);
    OUTPUT("bg_840_500.png", &background, 840, 500, 0, 1, 0);
    OUTPUT("startup_280_158.png", &icon, 280, 158, 1, 1, 0);
#undef OUTPUT
    success = 1;
    printf("artwork: generated %s\n", core->name);
done:
    image_free(&background);
    image_free(&icon);
    return success;
}

int command_artwork(int argc, char **argv)
{
    const char *source_root, *destination_root;
    size_t core;
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: njemu-tool artwork <source-root> [output-root]\n");
        return 2;
    }
    source_root = argv[1];
    destination_root = argc == 3 ? argv[2] : source_root;
    if (make_directory(destination_root) != 0 && errno != EEXIST) {
        fprintf(stderr, "artwork: cannot create output directory: %s\n", destination_root);
        return 1;
    }
    for (core = 0; core < sizeof(cores) / sizeof(cores[0]); ++core)
        if (!build_core(source_root, destination_root, &cores[core]))
            return 1;
    return 0;
}
