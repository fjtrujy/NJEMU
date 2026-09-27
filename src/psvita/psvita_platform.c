/******************************************************************************

	psvita_platform.c

	PS Vita startup, data directory and platform services

******************************************************************************/

#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <psp2/apputil.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/system_param.h>
#include "common/platform_driver.h"
#include "common/runtime_paths.h"

/* The app itself is read-only (app0:). Everything the emulator reads or writes
 * relative to its launch directory lives in ux0:data/<target>/. */
#define PSVITA_APP_DIR		"app0:"
#define PSVITA_DATA_ROOT	"ux0:data"

/* Size of newlib's heap (VitaSDK sbrk), the pool malloc()/free() use. */
extern unsigned int _get_vita_heap_size(void);

typedef struct psvita_platform {
	int apputil_ready;
} psvita_platform_t;

static void *psvita_init(void)
{
	psvita_platform_t *psvita = calloc(1, sizeof(psvita_platform_t));
	SceAppUtilInitParam init_param = { 0 };
	SceAppUtilBootParam boot_param = { 0 };

	if (psvita != NULL)
		psvita->apputil_ready = sceAppUtilInit(&init_param, &boot_param) == 0;
	return psvita;
}

static void psvita_free(void *data)
{
	psvita_platform_t *psvita = data;

	if (psvita != NULL && psvita->apputil_ready)
		sceAppUtilShutdown();
	free(psvita);
}

static void copy_file(const char *src, const char *dst)
{
	char buffer[16384];
	size_t bytes;
	FILE *in = fopen(src, "rb");
	FILE *out;

	if (in == NULL)
		return;
	out = fopen(dst, "wb");
	if (out != NULL) {
		while ((bytes = fread(buffer, 1, sizeof(buffer), in)) != 0)
			fwrite(buffer, 1, bytes, out);
		fclose(out);
	}
	fclose(in);
}

/* Copies the packaged resources (files and folders) that are missing from dst. */
static void copy_missing(const char *src_dir, const char *dst_dir)
{
	SceIoDirent entry;
	SceUID dir = sceIoDopen(src_dir);

	if (dir < 0)
		return;
	sceIoMkdir(dst_dir, 0777);

	memset(&entry, 0, sizeof(entry));
	while (sceIoDread(dir, &entry) > 0) {
		char src[PATH_MAX];
		char dst[PATH_MAX];
		SceIoStat st;

		if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, "..")
			|| !strcmp(entry.d_name, "sce_sys") || !strcmp(entry.d_name, "eboot.bin")
			|| !strcmp(entry.d_name, "_placeholder"))
			continue;

		snprintf(src, sizeof(src), "%s/%s", src_dir, entry.d_name);
		snprintf(dst, sizeof(dst), "%s/%s", dst_dir, entry.d_name);
		if (SCE_S_ISDIR(entry.d_stat.st_mode))
			copy_missing(src, dst);
		else if (sceIoGetstat(dst, &st) < 0)
			copy_file(src, dst);
		memset(&entry, 0, sizeof(entry));
	}
	sceIoDclose(dir);
}

static void psvita_main(void *data, int argc, char *argv[])
{
	char base[PATH_MAX];
	size_t len;
	(void)data;
	(void)argc;
	(void)argv;

	/* ux0:data/mvs, ux0:data/cps1, ... */
	len = (size_t)snprintf(base, sizeof(base), "%s/%s", PSVITA_DATA_ROOT, TARGET_STR);
	for (size_t i = strlen(PSVITA_DATA_ROOT) + 1; i < len; i++)
		base[i] = (char)tolower((unsigned char)base[i]);

	copy_missing(PSVITA_APP_DIR, base);
	if (chdir(base) != 0)
		printf("psvita: cannot enter %s\n", base);

	snprintf(launchDir, sizeof(launchDir), "%s/", base);
	snprintf(screenshotDir, sizeof(screenshotDir), "%s/pict", base);
}

static bool psvita_queryMemoryInfo(void *data, platform_memory_info_t *out)
{
	struct mallinfo heap_info;
	uint64_t heap_size;
	uint64_t heap_used;
	(void)data;

	if (out == NULL)
		return false;
	memset(out, 0, sizeof(*out));

	/*
	 * sceKernelGetFreeMemorySize() reports USER_RW memory outside newlib's
	 * preallocated heap, which is not a malloc() budget. Report the heap itself.
	 */
	heap_size = (uint64_t)_get_vita_heap_size();
	heap_info = mallinfo();
	heap_used = (uint64_t)heap_info.uordblks;
	if (heap_size == 0 || heap_used >= heap_size)
		return false;

	out->budget_cap_bytes = heap_size;
	out->free_bytes = heap_size - heap_used;
	out->capabilities = PLATFORM_MEMORY_CAP_QUERY_FREE;
	out->reliability_flags = PLATFORM_MEMORY_FREE_IS_ESTIMATE;
	platform_memory_info_normalize(out);
	return true;
}

static ui_language_t psvita_getSystemLanguage(void *data)
{
	psvita_platform_t *psvita = data;
	int language = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;

	if (psvita == NULL || !psvita->apputil_ready
		|| sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &language) < 0)
		return UI_LANG_ENGLISH;

	switch (language) {
	case SCE_SYSTEM_PARAM_LANG_JAPANESE: return UI_LANG_JAPANESE;
	case SCE_SYSTEM_PARAM_LANG_SPANISH: return UI_LANG_SPANISH;
	case SCE_SYSTEM_PARAM_LANG_CHINESE_S: return UI_LANG_CHINESE_SIMPLIFIED;
	case SCE_SYSTEM_PARAM_LANG_CHINESE_T: return UI_LANG_CHINESE_TRADITIONAL;
	default: return UI_LANG_ENGLISH;
	}
}

platform_driver_t platform_psvita = {
	.ident = "psvita",
	.init = psvita_init,
	.free = psvita_free,
	.main = psvita_main,
	.queryMemoryInfo = psvita_queryMemoryInfo,
	.getSystemLanguage = psvita_getSystemLanguage,
};
