/******************************************************************************
 *
 * gbk_s14_runtime.c
 *
 * External fixed-size GBK UI font with a small resident glyph cache.
 *
 ******************************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common/font/gbk_s14_runtime.h"

#define GBK_S14_GLYPH_COUNT  0x5e80u
#define GBK_S14_GLYPH_WIDTH  14
#define GBK_S14_GLYPH_HEIGHT 14
#define GBK_S14_GLYPH_BYTES  ((GBK_S14_GLYPH_WIDTH * GBK_S14_GLYPH_HEIGHT) / 2)
#define GBK_S14_CACHE_SLOTS  64

typedef struct gbk_s14_cache_entry
{
	uint16_t code;
	uint8_t valid;
	uint8_t data[GBK_S14_GLYPH_BYTES];
	uint32_t last_used;
} gbk_s14_cache_entry_t;

static int gbk_s14_fd = -1;
static gbk_s14_cache_entry_t gbk_s14_cache[GBK_S14_CACHE_SLOTS];
static uint32_t gbk_s14_cache_clock;

static uint32_t gbk_s14_next_tick(void)
{
	int i;

	gbk_s14_cache_clock++;
	if (gbk_s14_cache_clock != 0)
		return gbk_s14_cache_clock;

	/* Preserve LRU ordering after the practically unreachable 32-bit wrap. */
	for (i = 0; i < GBK_S14_CACHE_SLOTS; i++)
		gbk_s14_cache[i].last_used = 0;
	gbk_s14_cache_clock = 1;
	return gbk_s14_cache_clock;
}

static const uint8_t *gbk_s14_load_glyph(uint16_t code)
{
	gbk_s14_cache_entry_t *empty = NULL;
	gbk_s14_cache_entry_t *oldest_entry = NULL;
	gbk_s14_cache_entry_t *victim;
	uint32_t oldest = UINT32_MAX;
	uint32_t tick;
	off_t offset;
	size_t bytes_read;
	int i;

	if (gbk_s14_fd < 0 || code >= GBK_S14_GLYPH_COUNT)
		return NULL;

	tick = gbk_s14_next_tick();
	for (i = 0; i < GBK_S14_CACHE_SLOTS; i++)
	{
		gbk_s14_cache_entry_t *entry = &gbk_s14_cache[i];

		if (entry->valid && entry->code == code)
		{
			entry->last_used = tick;
			return entry->data;
		}
		if (!entry->valid)
		{
			if (empty == NULL)
				empty = entry;
			continue;
		}
		if (entry->last_used < oldest)
		{
			oldest = entry->last_used;
			oldest_entry = entry;
		}
	}

	victim = empty != NULL ? empty : oldest_entry;
	if (victim == NULL)
		return NULL;

	offset = (off_t)code * GBK_S14_GLYPH_BYTES;
	if (lseek(gbk_s14_fd, offset, SEEK_SET) != offset)
		return NULL;
	bytes_read = 0;
	while (bytes_read < GBK_S14_GLYPH_BYTES)
	{
		ssize_t count = read(gbk_s14_fd, victim->data + bytes_read,
			GBK_S14_GLYPH_BYTES - bytes_read);
		if (count <= 0)
			return NULL;
		bytes_read += (size_t)count;
	}

	victim->code = code;
	victim->valid = 1;
	victim->last_used = tick;
	return victim->data;
}

int gbk_s14_font_init(const char *base_dir)
{
	char path[1024];
	const char *separator;
	size_t base_len;
	off_t size;
	int written;

	gbk_s14_font_shutdown();
	if (base_dir == NULL)
		return 0;

	base_len = strlen(base_dir);
	separator = (base_len > 0 && base_dir[base_len - 1] == '/') ? "" : "/";
	written = snprintf(path, sizeof(path), "%s%sfont/gbk_s14.bin",
		base_dir, separator);
	if (written < 0 || written >= (int)sizeof(path))
		return 0;

	gbk_s14_fd = open(path, O_RDONLY);
	if (gbk_s14_fd < 0)
	{
		printf("Failed to open UI font asset: %s\n", path);
		return 0;
	}

	size = lseek(gbk_s14_fd, 0, SEEK_END);
	if (size < 0)
		goto invalid_asset;
	if (size != (off_t)(GBK_S14_GLYPH_COUNT * GBK_S14_GLYPH_BYTES))
		goto invalid_asset;
	if (lseek(gbk_s14_fd, 0, SEEK_SET) != 0)
		goto invalid_asset;

	memset(gbk_s14_cache, 0, sizeof(gbk_s14_cache));
	gbk_s14_cache_clock = 0;
	return 1;

invalid_asset:
	printf("Invalid UI font asset size: %s\n", path);
	close(gbk_s14_fd);
	gbk_s14_fd = -1;
	return 0;
}

void gbk_s14_font_shutdown(void)
{
	if (gbk_s14_fd >= 0)
	{
		close(gbk_s14_fd);
		gbk_s14_fd = -1;
	}
	memset(gbk_s14_cache, 0, sizeof(gbk_s14_cache));
	gbk_s14_cache_clock = 0;
}

static int gbk_s14_get_glyph(struct font_t *font, uint16_t code)
{
	const uint8_t *data = gbk_s14_load_glyph(code);

	if (data == NULL)
		return 0;

	font->data = data;
	font->width = GBK_S14_GLYPH_WIDTH;
	font->height = GBK_S14_GLYPH_HEIGHT;
	font->pitch = GBK_S14_GLYPH_WIDTH;
	font->skipx = 0;
	font->skipy = 0;
	return 1;
}

#ifdef COMMAND_LIST
int gbk_s14_get_gryph(struct font_t *font, uint16_t code)
{
	return gbk_s14_get_glyph(font, code);
}
#endif

int gbk_s14p_get_gryph(struct font_t *font, uint16_t code)
{
	return gbk_s14_get_glyph(font, code);
}

int gbk_s14p_get_pitch(uint16_t code)
{
	(void)code;
	return GBK_S14_GLYPH_WIDTH;
}
