#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/ui_utf8.h"

static void test_ascii(void)
{
	uint32_t cp = 0;
	assert(ui_utf8_decode((const unsigned char *)"A", &cp) == 1);
	assert(cp == 'A');
	assert(ui_utf8_validate_n("hello", 5));
}

static void test_multibyte(void)
{
	uint32_t cp = 0;
	const char *middle_dot = "·";
	const char *japanese = "し";
	const char *emoji = "😀";

	assert(ui_utf8_decode((const unsigned char *)middle_dot, &cp) == 2);
	assert(cp == 0x00b7u);
	assert(ui_utf8_decode((const unsigned char *)japanese, &cp) == 3);
	assert(cp == 0x3057u);
	assert(ui_utf8_decode((const unsigned char *)emoji, &cp) == 4);
	assert(cp == 0x1f600u);
	assert(ui_utf8_validate_n(japanese, strlen(japanese)));
	assert(ui_utf8_validate_n(emoji, strlen(emoji)));
}

static void test_private_use(void)
{
	uint32_t cp = 0;
	static const char circle[] = { (char)0xee, (char)0x80, (char)0x84, 0 };
	assert(ui_utf8_decode((const unsigned char *)circle, &cp) == 3);
	assert(cp == 0xe004u);
}

static void test_invalid_sequences(void)
{
	static const char overlong[] = { (char)0xc0, (char)0xaf };
	static const char surrogate[] = { (char)0xed, (char)0xa0, (char)0x80 };
	static const char truncated[] = { (char)0xe3, (char)0x81 };
	static const char too_high[] = { (char)0xf4, (char)0x90, (char)0x80, (char)0x80 };

	assert(!ui_utf8_validate_n(overlong, sizeof(overlong)));
	assert(!ui_utf8_validate_n(surrogate, sizeof(surrogate)));
	assert(!ui_utf8_validate_n(truncated, sizeof(truncated)));
	assert(!ui_utf8_validate_n(too_high, sizeof(too_high)));
}

int main(void)
{
	test_ascii();
	test_multibyte();
	test_private_use();
	test_invalid_sequences();
	return 0;
}
