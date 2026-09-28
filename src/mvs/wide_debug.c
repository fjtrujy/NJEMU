#include "mvs/wide_debug.h"

#if defined(DESKTOP) && !RELEASE
#include "mvs.h"
#include "common/runtime_paths.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *scene_path;
static uint32_t scene_frame;
static uint32_t rendered_frames;

static void dump_program(void)
{
	const char *path = getenv("NJEMU_MVS_DUMP_PROGRAM");
	const uint16_t *program = (const uint16_t *)memory_region_cpu1;
	FILE *file;
	size_t offset;
	int ok = 1;
	if (!path || !*path) return;
	file = fopen(path, "wbx");
	if (!file) { printf("[MVS_WIDE] Cannot create program dump: %s\n", path); return; }
	for (offset = 0; offset < memory_length_cpu1; offset += 2)
	{
		uint8_t bytes[2] = {(uint8_t)(program[offset / 2] >> 8), (uint8_t)program[offset / 2]};
		if (fwrite(bytes, 1, 2, file) != 2) { ok = 0; break; }
	}
	if (fclose(file) != 0) ok = 0;
	printf("[MVS_WIDE] Program dump %s: %s\n", ok ? "written" : "incomplete", path);
}

void mvs_wide_debug_init(void)
{
	const char *value = getenv("NJEMU_MVS_DUMP_SCENE_FRAME");
	char *end;
	unsigned long parsed;
	scene_path = getenv("NJEMU_MVS_DUMP_SCENE");
	scene_frame = rendered_frames = 0;
	if (scene_path && *scene_path && value && value[0] >= '0' && value[0] <= '9')
	{
		errno = 0;
		parsed = strtoul(value, &end, 10);
		if (!errno && end != value && !*end && parsed <= UINT32_MAX)
			scene_frame = (uint32_t)parsed;
	}
	if (scene_path && *scene_path && !scene_frame)
		printf("[MVS_WIDE] Scene export needs a positive decimal NJEMU_MVS_DUMP_SCENE_FRAME.\n");
	dump_program();
}

void mvs_wide_debug_frame(void)
{
	FILE *file;
	uint32_t i;
	int ok;
	if (!scene_frame || ++rendered_frames != scene_frame) return;
	/* Read only RAM/VRAM, not device registers with read side effects. This
	 * snapshot helps distinguish culling, stale map columns and camera edges. */
	file = fopen(scene_path, "wx");
	if (!file) { printf("[MVS_WIDE] Cannot create scene dump: %s\n", scene_path); return; }
	fprintf(file, "{\"version\":1,\"ngh\":%u,\"frame\":%u,\"registers\":{",
		(unsigned int)neogeo_ngh, (unsigned int)rendered_frames);
	for (i = 0; i < 8; i++)
		fprintf(file, "%s\"D%u\":%u", i ? "," : "", (unsigned int)i,
			(unsigned int)m68000_get_reg(M68K_D0 + i));
	for (i = 0; i < 8; i++)
		fprintf(file, ",\"A%u\":%u", (unsigned int)i,
			(unsigned int)m68000_get_reg(M68K_A0 + i));
	fprintf(file, ",\"PC\":%u},\"ram_base\":1048576,\"ram_words\":[",
		(unsigned int)m68000_get_reg(M68K_PC));
	for (i = 0; i < 0x8000; i++)
		fprintf(file, "%s%u", i ? "," : "", (unsigned int)m68000_read_memory_16(0x100000 + i * 2));
	fprintf(file, "],\"vram_words\":[");
	for (i = 0; i < sizeof(neogeo_videoram) / sizeof(neogeo_videoram[0]); i++)
		fprintf(file, "%s%u", i ? "," : "", (unsigned int)neogeo_videoram[i]);
	fprintf(file, "]}\n");
	ok = !ferror(file);
	if (fclose(file) != 0) ok = 0;
	printf("[MVS_WIDE] Scene dump %s at rendered frame %u: %s\n",
		ok ? "written" : "incomplete", (unsigned int)rendered_frames, scene_path);
}
#endif
