#ifndef COMMON_DISPLAY_MODE_H
#define COMMON_DISPLAY_MODE_H

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
	if ((long long)output_width * aspect_height <=
		(long long)output_height * aspect_width) {
		*width = output_width;
		*height = (output_width * aspect_height) / aspect_width;
	} else {
		*height = output_height;
		*width = (output_height * aspect_width) / aspect_height;
	}
}

#endif
