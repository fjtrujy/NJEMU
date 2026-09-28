/******************************************************************************

	desktop_frame_dump.c

	See desktop_frame_dump.h.

******************************************************************************/

#include "desktop/desktop_frame_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUMP_MAX_FRAMES	64

static bool dump_parsed;
static uint32_t dump_frames[DUMP_MAX_FRAMES];
static int dump_count;

static void dump_parse(void)
{
	const char *list = getenv("NJEMU_DUMP_FRAMES");

	dump_parsed = true;
	while (list != NULL && *list != '\0' && dump_count < DUMP_MAX_FRAMES) {
		char *end = NULL;
		unsigned long frame = strtoul(list, &end, 10);
		if (end == list)
			break;
		if (frame != 0)
			dump_frames[dump_count++] = (uint32_t)frame;
		list = (*end == ',') ? end + 1 : end;
	}
}

bool desktop_dump_wanted(uint32_t frame)
{
	if (!dump_parsed)
		dump_parse();

	for (int i = 0; i < dump_count; i++) {
		if (dump_frames[i] == frame)
			return true;
	}
	return false;
}

void desktop_dump_write(const char *backend, uint32_t frame, const uint8_t *rgba,
						int width, int height, int stride, bool bottom_up)
{
	const char *dir = getenv("NJEMU_DUMP_DIR");
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s_%05u.ppm", dir ? dir : ".", backend, (unsigned)frame);
	FILE *f = fopen(path, "wb");
	if (f == NULL) {
		printf("frame dump: cannot write %s\n", path);
		return;
	}

	fprintf(f, "P6\n%d %d\n255\n", width, height);
	for (int y = 0; y < height; y++) {
		const uint8_t *row = rgba + (size_t)(bottom_up ? height - 1 - y : y) * stride;
		for (int x = 0; x < width; x++)
			fwrite(row + x * 4, 1, 3, f);
	}
	fclose(f);
	printf("frame dump: wrote %s\n", path);
}
