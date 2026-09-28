/******************************************************************************

	desktop_frame_dump.h

	Test hook shared by the Desktop video backends: dumps the visible work
	frame (the transferWorkFrame source rectangle, 1x) of selected frames as
	PPM files so backends can be compared pixel by pixel.

	NJEMU_DUMP_FRAMES=100,300,600   presented frame numbers to dump (1-based)
	NJEMU_DUMP_DIR=path             output directory (default: current)

******************************************************************************/

#ifndef DESKTOP_FRAME_DUMP_H
#define DESKTOP_FRAME_DUMP_H

#include <stdbool.h>
#include <stdint.h>

/* True when frame `frame` was requested through NJEMU_DUMP_FRAMES. */
bool desktop_dump_wanted(uint32_t frame);

/* Writes <dir>/<backend>_<frame>.ppm from RGBA8 pixels. */
void desktop_dump_write(const char *backend, uint32_t frame, const uint8_t *rgba,
						int width, int height, int stride, bool bottom_up);

#endif /* DESKTOP_FRAME_DUMP_H */
