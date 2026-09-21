#include "common/ui_utf8.h"

static size_t decode_n(const unsigned char *text, size_t available, uint32_t *codepoint)
{
	uint32_t value;
	unsigned char c0, c1, c2, c3;

	if (available == 0)
		return 0;

	c0 = text[0];
	if (c0 < 0x80) {
		if (codepoint != NULL)
			*codepoint = c0;
		return 1;
	}
	if (c0 < 0xc2)
		return 0;

	if (c0 <= 0xdf) {
		if (available < 2)
			return 0;
		c1 = text[1];
		if ((c1 & 0xc0) != 0x80)
			return 0;
		value = ((uint32_t)(c0 & 0x1f) << 6) | (uint32_t)(c1 & 0x3f);
		if (codepoint != NULL)
			*codepoint = value;
		return 2;
	}

	if (c0 <= 0xef) {
		if (available < 3)
			return 0;
		c1 = text[1];
		c2 = text[2];
		if ((c1 & 0xc0) != 0x80 || (c2 & 0xc0) != 0x80)
			return 0;
		if (c0 == 0xe0 && c1 < 0xa0)
			return 0;
		if (c0 == 0xed && c1 >= 0xa0)
			return 0;
		value = ((uint32_t)(c0 & 0x0f) << 12)
			| ((uint32_t)(c1 & 0x3f) << 6)
			| (uint32_t)(c2 & 0x3f);
		if (codepoint != NULL)
			*codepoint = value;
		return 3;
	}

	if (c0 <= 0xf4) {
		if (available < 4)
			return 0;
		c1 = text[1];
		c2 = text[2];
		c3 = text[3];
		if ((c1 & 0xc0) != 0x80 || (c2 & 0xc0) != 0x80 || (c3 & 0xc0) != 0x80)
			return 0;
		if (c0 == 0xf0 && c1 < 0x90)
			return 0;
		if (c0 == 0xf4 && c1 >= 0x90)
			return 0;
		value = ((uint32_t)(c0 & 0x07) << 18)
			| ((uint32_t)(c1 & 0x3f) << 12)
			| ((uint32_t)(c2 & 0x3f) << 6)
			| (uint32_t)(c3 & 0x3f);
		if (codepoint != NULL)
			*codepoint = value;
		return 4;
	}

	return 0;
}

size_t ui_utf8_decode(const unsigned char *text, uint32_t *codepoint)
{
	size_t available = 0;

	if (text == NULL || text[0] == 0)
		return 0;

	while (available < 4 && text[available] != 0)
		++available;
	return decode_n(text, available, codepoint);
}

int ui_utf8_validate_n(const char *text, size_t length)
{
	size_t offset = 0;

	if (text == NULL)
		return length == 0;

	while (offset < length) {
		size_t consumed;
		if ((unsigned char)text[offset] == 0)
			return 0;
		consumed = decode_n((const unsigned char *)text + offset,
			length - offset, NULL);
		if (consumed == 0)
			return 0;
		offset += consumed;
	}
	return 1;
}
