/******************************************************************************

	font_t.h

	Common font glyph structure used by font/icon data files

******************************************************************************/

#ifndef COMMON_FONT_T_H
#define COMMON_FONT_T_H

#include <stdint.h>

struct font_t
{
	const uint8_t *data;
	int width;
	int height;
	int pitch;
	int skipx;
	int skipy;
};

#endif /* COMMON_FONT_T_H */
