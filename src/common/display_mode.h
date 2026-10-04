#ifndef COMMON_DISPLAY_MODE_H
#define COMMON_DISPLAY_MODE_H

#include "common/video_driver.h"
#include "common/video_geometry.h"

typedef enum display_mode
{
	DISPLAY_MODE_ORIGINAL_SIZE = 0,
	DISPLAY_MODE_ORIGINAL_ASPECT,
	DISPLAY_MODE_4_3,
	DISPLAY_MODE_FULLSCREEN,
	DISPLAY_MODE_COUNT
} display_mode_t;

static inline void display_mode_size(display_mode_t mode,
	int output_width, int output_height, int native_width, int native_height,
	int *width, int *height)
{
	int aspect_width = native_width;
	int aspect_height = native_height;
	int pixel_aspect_num = 1;
	int pixel_aspect_den = 1;

	if (mode == DISPLAY_MODE_FULLSCREEN) {
		*width = output_width;
		*height = output_height;
		return;
	}
	if (mode == DISPLAY_MODE_ORIGINAL_SIZE) {
		*width = native_width;
		*height = native_height;
		return;
	}
	if (mode == DISPLAY_MODE_4_3) {
		aspect_width = 4;
		aspect_height = 3;
	}
	video_get_pixel_aspect_ratio(&pixel_aspect_num, &pixel_aspect_den);
	if ((long long)output_width * pixel_aspect_num * aspect_height <=
		(long long)output_height * pixel_aspect_den * aspect_width) {
		*width = output_width;
		*height = (int)(((long long)output_width * pixel_aspect_num *
			aspect_height) / ((long long)pixel_aspect_den * aspect_width));
	} else {
		*height = output_height;
		*width = (int)(((long long)output_height * pixel_aspect_den *
			aspect_width) / ((long long)pixel_aspect_num * aspect_height));
	}
}

static inline RECT display_mode_presentation_rect(int mode,
	int native_width, int native_height)
{
	int output_width = SCR_WIDTH;
	int output_height = SCR_HEIGHT;
	int viewport_x = 0;
	int viewport_y = 0;
	int viewport_width;
	int viewport_height;
	int width;
	int height;
	RECT rect;

	if (mode < DISPLAY_MODE_ORIGINAL_SIZE || mode >= DISPLAY_MODE_COUNT)
		mode = DISPLAY_MODE_ORIGINAL_ASPECT;
	if (video_driver && video_driver->getOutputSize)
		video_driver->getOutputSize(video_data, &output_width, &output_height);

	viewport_width = output_width;
	viewport_height = output_height;
	if (video_driver && video_driver->getPresentationViewport) {
		video_driver->getPresentationViewport(video_data,
			output_width, output_height,
			&viewport_x, &viewport_y, &viewport_width, &viewport_height);
	}

	display_mode_size((display_mode_t)mode, viewport_width, viewport_height,
		native_width, native_height, &width, &height);
	rect.left = (int16_t)(viewport_x + (viewport_width - width) / 2);
	rect.top = (int16_t)(viewport_y + (viewport_height - height) / 2);
	rect.right = (int16_t)(rect.left + width);
	rect.bottom = (int16_t)(rect.top + height);
	return rect;
}

#endif
