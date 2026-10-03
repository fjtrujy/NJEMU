/******************************************************************************

	translation.c

	Runtime language selection for the ROM converter.

******************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "translation.h"

#include "romcnv_translation_data.inc"

static const char *const *active_catalog = romcnv_catalog_en;

static int ascii_equal_ignore_case(const char *left, const char *right)
{
	unsigned char a;
	unsigned char b;

	if (left == NULL || right == NULL)
		return 0;
	while (*left != '\0' && *right != '\0')
	{
		a = (unsigned char)*left++;
		b = (unsigned char)*right++;
		if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
		if (a != b) return 0;
	}
	return *left == '\0' && *right == '\0';
}

static int ascii_prefix_ignore_case(const char *text, const char *prefix)
{
	unsigned char a;
	unsigned char b;

	if (text == NULL || prefix == NULL)
		return 0;
	while (*prefix != '\0')
	{
		if (*text == '\0') return 0;
		a = (unsigned char)*text++;
		b = (unsigned char)*prefix++;
		if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
		if (a != b) return 0;
	}
	return 1;
}

static void select_language(const char *tag)
{
	if (tag != NULL && (ascii_equal_ignore_case(tag, "zh-Hans")
		|| ascii_prefix_ignore_case(tag, "zh_CN")
		|| ascii_prefix_ignore_case(tag, "zh-CN")
		|| ascii_prefix_ignore_case(tag, "zh_SG")
		|| ascii_prefix_ignore_case(tag, "zh-SG")
		|| ascii_equal_ignore_case(tag, "zh")))
	{
		active_catalog = romcnv_catalog_zh_hans;
		return;
	}

	active_catalog = romcnv_catalog_en;
}

static const char *language_from_environment(void)
{
	const char *value = getenv("NJEMU_LANG");
	if (value != NULL && value[0] != '\0') return value;
	value = getenv("LC_ALL");
	if (value != NULL && value[0] != '\0') return value;
	value = getenv("LC_MESSAGES");
	if (value != NULL && value[0] != '\0') return value;
	value = getenv("LANG");
	return (value != NULL && value[0] != '\0') ? value : "en";
}

void romcnv_translation_init(int argc, char *argv[])
{
	const char *tag = NULL;
	int i;

	for (i = 1; i < argc; ++i)
	{
		if ((ascii_equal_ignore_case(argv[i], "-lang")
			|| ascii_equal_ignore_case(argv[i], "--lang")) && i + 1 < argc)
		{
			tag = argv[i + 1];
			break;
		}
		if (ascii_prefix_ignore_case(argv[i], "-lang="))
		{
			tag = argv[i] + 6;
			break;
		}
		if (ascii_prefix_ignore_case(argv[i], "--lang="))
		{
			tag = argv[i] + 7;
			break;
		}
	}

	select_language(tag != NULL ? tag : language_from_environment());
}

int romcnv_translation_option_span(int argc, char *argv[], int index)
{
	if (index < 0 || index >= argc)
		return 0;
	if (ascii_equal_ignore_case(argv[index], "-lang")
		|| ascii_equal_ignore_case(argv[index], "--lang"))
		return index + 1 < argc ? 2 : 1;
	if (ascii_prefix_ignore_case(argv[index], "-lang=")
		|| ascii_prefix_ignore_case(argv[index], "--lang="))
		return 1;
	return 0;
}

const char *romcnv_translation_get(romcnv_text_id_t id)
{
	if ((unsigned)id >= ROMCNV_TEXT_COUNT)
		return "";
	return active_catalog[id];
}
