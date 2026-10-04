#ifndef ROMCNV_CPS2_CACHE_LAYOUTS_H
#define ROMCNV_CPS2_CACHE_LAYOUTS_H

#include <stdint.h>

#define CPS2_CACHE_LAYOUT_FILENAME "cps2_cache_layouts.tsv"

typedef struct
{
	char name[16];
	uint32_t object_start;
	uint32_t object_end;
	uint32_t scroll1_start;
	uint32_t scroll1_end;
	uint32_t scroll2_start;
	uint32_t scroll2_end;
	uint32_t scroll3_start;
	uint32_t scroll3_end;
	uint32_t object2_start;
	uint32_t object2_end;
} cps2_cache_layout_t;

typedef enum
{
	CPS2_CACHE_LAYOUT_OK = 0,
	CPS2_CACHE_LAYOUT_NOT_FOUND,
	CPS2_CACHE_LAYOUT_INVALID
} cps2_cache_layout_error_t;

cps2_cache_layout_error_t cps2_cache_layouts_load(const char *path);
const cps2_cache_layout_t *cps2_cache_layout_find(const char *game_name, const char *cache_name);
int cps2_cache_layout_count(void);
const cps2_cache_layout_t *cps2_cache_layout_at(int index);

#endif
