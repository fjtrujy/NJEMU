/******************************************************************************

	video_driver.c

******************************************************************************/

#include <stddef.h>
#include "video_driver.h"

void *video_data;

static int video_pixel_aspect_numerator = 1;
static int video_pixel_aspect_denominator = 1;

void video_set_pixel_aspect_ratio(int numerator, int denominator)
{
	if (numerator <= 0 || denominator <= 0) {
		numerator = 1;
		denominator = 1;
	}
	video_pixel_aspect_numerator = numerator;
	video_pixel_aspect_denominator = denominator;
}

void video_get_pixel_aspect_ratio(int *numerator, int *denominator)
{
	if (numerator)
		*numerator = video_pixel_aspect_numerator;
	if (denominator)
		*denominator = video_pixel_aspect_denominator;
}
