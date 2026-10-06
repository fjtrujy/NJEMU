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

static inline void display_mode_size_aspect(display_mode_t mode,
	int output_width, int output_height, int native_width, int native_height,
	int display_aspect_width, int display_aspect_height,
	int *width, int *height)
{
	int pixel_aspect_num = 1;
	int pixel_aspect_den = 1;

	/* Original Size means one source scanline per output scanline. On outputs
	 * with non-square pixels, preserve that vertical 1:1 mapping while sizing
	 * the horizontal extent to the content's intended display aspect. This is
	 * what a native 224-line arcade image needs inside PS2 240p: 224 active
	 * lines centered in the 240-line raster, rather than a 384-pixel-wide image
	 * interpreted as if PS2 framebuffer pixels were square. */
	if (mode == DISPLAY_MODE_ORIGINAL_SIZE &&
		display_aspect_width > 0 && display_aspect_height > 0) {
		long long scaled_width;

		video_get_pixel_aspect_ratio(&pixel_aspect_num, &pixel_aspect_den);
		if (pixel_aspect_num == pixel_aspect_den) {
			*width = native_width;
			*height = native_height;
			return;
		}
		*height = native_height < output_height ? native_height : output_height;
		scaled_width = (long long)(*height) * display_aspect_width *
			pixel_aspect_den;
		scaled_width /= (long long)display_aspect_height * pixel_aspect_num;
		*width = scaled_width > output_width ? output_width : (int)scaled_width;
		return;
	}

	if (mode == DISPLAY_MODE_ORIGINAL_ASPECT &&
		display_aspect_width > 0 && display_aspect_height > 0) {
		display_mode_size(DISPLAY_MODE_ORIGINAL_ASPECT,
			output_width, output_height,
			display_aspect_width, display_aspect_height,
			width, height);
		return;
	}

	display_mode_size(mode, output_width, output_height,
		native_width, native_height, width, height);
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

static inline RECT display_mode_presentation_rect_aspect(int mode,
	int native_width, int native_height,
	int display_aspect_width, int display_aspect_height)
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

	display_mode_size_aspect((display_mode_t)mode,
		viewport_width, viewport_height,
		native_width, native_height,
		display_aspect_width, display_aspect_height,
		&width, &height);
	rect.left = (int16_t)(viewport_x + (viewport_width - width) / 2);
	rect.top = (int16_t)(viewport_y + (viewport_height - height) / 2);
	rect.right = (int16_t)(rect.left + width);
	rect.bottom = (int16_t)(rect.top + height);
	return rect;
}

#endif
