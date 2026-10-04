#include "cache_layouts.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define MAX_CACHE_LAYOUTS 64

static cps2_cache_layout_t cache_layouts[MAX_CACHE_LAYOUTS];
static int cache_layout_count;

cps2_cache_layout_error_t cps2_cache_layouts_load(const char *path)
{
	FILE *fp;
	char line[256];

	fp = fopen(path, "r");
	if (fp == NULL)
		return CPS2_CACHE_LAYOUT_NOT_FOUND;

	cache_layout_count = 0;
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		cps2_cache_layout_t *layout;

		if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n'
		|| line[0] == '#' || strncmp(line, "name\t", 5) == 0)
			continue;
		if (cache_layout_count >= MAX_CACHE_LAYOUTS)
			goto invalid;

		layout = &cache_layouts[cache_layout_count];
		if (sscanf(line,
			"%15s\t%" SCNx32 "\t%" SCNx32 "\t%" SCNx32 "\t%" SCNx32
			"\t%" SCNx32 "\t%" SCNx32 "\t%" SCNx32 "\t%" SCNx32
			"\t%" SCNx32 "\t%" SCNx32,
			layout->name,
			&layout->object_start, &layout->object_end,
			&layout->scroll1_start, &layout->scroll1_end,
			&layout->scroll2_start, &layout->scroll2_end,
			&layout->scroll3_start, &layout->scroll3_end,
			&layout->object2_start, &layout->object2_end) != 11)
		{
			goto invalid;
		}
		cache_layout_count++;
	}

	fclose(fp);
	return cache_layout_count != 0 ? CPS2_CACHE_LAYOUT_OK : CPS2_CACHE_LAYOUT_INVALID;

invalid:
	fclose(fp);
	cache_layout_count = 0;
	return CPS2_CACHE_LAYOUT_INVALID;
}

const cps2_cache_layout_t *cps2_cache_layout_find(const char *game_name, const char *cache_name)
{
	int i;

	for (i = 0; i < cache_layout_count; i++)
	{
		if (!strcmp(game_name, cache_layouts[i].name)
		|| (cache_name[0] && !strcmp(cache_name, cache_layouts[i].name)))
		{
			return &cache_layouts[i];
		}
	}
	return NULL;
}

int cps2_cache_layout_count(void)
{
	return cache_layout_count;
}

const cps2_cache_layout_t *cps2_cache_layout_at(int index)
{
	if (index < 0 || index >= cache_layout_count)
		return NULL;
	return &cache_layouts[index];
}
