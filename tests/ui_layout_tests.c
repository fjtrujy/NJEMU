#include <assert.h>

#include "common/display_mode.h"
#include "common/ui_layout.h"
#include "common/video_driver.h"

static int test_output_width = 704;
static int test_output_height = 480;

static void test_get_output_size(void *data, int *width, int *height)
{
	(void)data;
	if (width) *width = test_output_width;
	if (height) *height = test_output_height;
}

static void test_get_presentation_viewport(void *data,
	int output_width, int output_height,
	int *x, int *y, int *width, int *height)
{
	(void)data;
	assert(output_width == 704);
	assert(output_height == 480);
	if (x) *x = 32;
	if (y) *y = 16;
	if (width) *width = 640;
	if (height) *height = 448;
}

static video_driver_t test_video_driver = {
	.getOutputSize = test_get_output_size,
	.getPresentationViewport = test_get_presentation_viewport,
};
video_driver_t *video_driver = &test_video_driver;

static void test_responsive_layout(int width, int height,
	int expected_logical_width, int expected_logical_height,
	int expected_rows)
{
	const ui_layout_metrics_t *layout;
	int logical_width;
	int logical_height;
	int center_x;
	int center_y;

	ui_layout_compute_responsive_size(width, height, &logical_width, &logical_height);
	ui_layout_init(logical_width, logical_height, width, height);
	layout = ui_layout_get();

	assert(layout->logical_width == expected_logical_width);
	assert(layout->logical_height == expected_logical_height);
	assert(layout->output_width == width);
	assert(layout->output_height == height);
	assert(layout->viewport_x >= 0 && layout->viewport_x <= 1);
	assert(layout->viewport_y >= 0 && layout->viewport_y <= 1);
	assert(layout->viewport_width <= width);
	assert(layout->viewport_height <= height);
	assert(width - layout->viewport_width <= 2);
	assert(height - layout->viewport_height <= 2);
	assert(ui_layout_center_x() == expected_logical_width / 2);
	assert(ui_layout_center_y() == expected_logical_height / 2);
	assert(ui_layout_right(0) == expected_logical_width - 1);
	assert(ui_layout_bottom(0) == expected_logical_height - 1);
	assert(ui_layout_visible_rows(37, 20) == expected_rows);

	ui_layout_transform_point(ui_layout_center_x(), ui_layout_center_y(),
		&center_x, &center_y);
	assert(center_x >= width / 2 - 1 && center_x <= width / 2 + 1);
	assert(center_y >= height / 2 - 1 && center_y <= height / 2 + 1);
}

static void test_responsive_content_scaling(void)
{
	int logical_width;
	int logical_height;
	int x;
	int y;

	ui_layout_compute_responsive_size(640, 448, &logical_width, &logical_height);
	ui_layout_init(logical_width, logical_height, 640, 448);
	ui_layout_transform_point(210, 40, &x, &y);
	assert(x == 280);
	assert(y == 53);
}

static void test_aspect_preserving_viewport(void)
{
	const ui_layout_metrics_t *layout;

	ui_layout_init(480, 272, 640, 448);
	layout = ui_layout_get();

	assert(layout->logical_width == 480);
	assert(layout->logical_height == 272);
	assert(layout->output_width == 640);
	assert(layout->output_height == 448);
	assert(layout->viewport_x == 0);
	assert(layout->viewport_y == 42);
	assert(layout->viewport_width == 640);
	assert(layout->viewport_height == 363);
	assert(ui_layout_uses_output_transform());
}

static void test_non_square_pixel_layout(int width, int height,
	int pixel_aspect_num, int pixel_aspect_den)
{
	const ui_layout_metrics_t *layout;
	int logical_width;
	int logical_height;
	int x;
	int y;
	int w;
	int h;

	video_set_pixel_aspect_ratio(pixel_aspect_num, pixel_aspect_den);
	ui_layout_compute_responsive_size(width, height,
		&logical_width, &logical_height);
	assert(logical_width == 480);
	assert(logical_height == 360);

	ui_layout_init(logical_width, logical_height, width, height);
	layout = ui_layout_get();
	assert(layout->viewport_x == 0);
	assert(layout->viewport_y == 0);
	assert(layout->viewport_width == width);
	assert(layout->viewport_height == height);

	ui_layout_transform_rect(0, 0, logical_width, logical_height,
		&x, &y, &w, &h);
	assert(x == 0);
	assert(y == 0);
	assert(w == width);
	assert(h == height);
}

static void test_non_square_pixel_display_mode(void)
{
	int width;
	int height;

	video_set_pixel_aspect_ratio(10, 11);
	display_mode_size(DISPLAY_MODE_4_3, 704, 480, 304, 224,
		&width, &height);
	assert(width == 704);
	assert(height == 480);

	video_set_pixel_aspect_ratio(10, 22);
	display_mode_size(DISPLAY_MODE_4_3, 704, 240, 304, 224,
		&width, &height);
	assert(width == 704);
	assert(height == 240);
}

static void test_cps1_240p_display_modes(void)
{
	int width;
	int height;

	/* PS2 240p presents gameplay through a 640x224 visible framebuffer. Original
	 * Size must remain a true 1:1 384x224 copy; the other modes intentionally
	 * scale and therefore require filtered presentation in the PS2 backend. */
	video_set_pixel_aspect_ratio(10, 22);

	display_mode_size(DISPLAY_MODE_ORIGINAL_SIZE, 640, 224, 384, 224,
		&width, &height);
	assert(width == 384);
	assert(height == 224);

	display_mode_size(DISPLAY_MODE_ORIGINAL_ASPECT, 640, 224, 384, 224,
		&width, &height);
	assert(width == 640);
	assert(height == 169);

	display_mode_size(DISPLAY_MODE_4_3, 640, 224, 384, 224,
		&width, &height);
	assert(width == 640);
	assert(height == 218);

	display_mode_size(DISPLAY_MODE_FULLSCREEN, 640, 224, 384, 224,
		&width, &height);
	assert(width == 640);
	assert(height == 224);
}

static void test_cps1_240p_presentation_rect(void)
{
	RECT rect;
	void (*saved_viewport)(void *, int, int, int *, int *, int *, int *) =
		test_video_driver.getPresentationViewport;

	test_output_width = 640;
	test_output_height = 224;
	test_video_driver.getPresentationViewport = NULL;
	video_set_pixel_aspect_ratio(10, 22);

	rect = display_mode_presentation_rect(DISPLAY_MODE_ORIGINAL_SIZE, 384, 224);
	assert(rect.left == 128);
	assert(rect.top == 0);
	assert(rect.right == 512);
	assert(rect.bottom == 224);

	rect = display_mode_presentation_rect(DISPLAY_MODE_ORIGINAL_ASPECT, 384, 224);
	assert(rect.left == 0);
	assert(rect.top == 27);
	assert(rect.right == 640);
	assert(rect.bottom == 196);

	rect = display_mode_presentation_rect(DISPLAY_MODE_FULLSCREEN, 384, 224);
	assert(rect.left == 0);
	assert(rect.top == 0);
	assert(rect.right == 640);
	assert(rect.bottom == 224);

	test_output_width = 704;
	test_output_height = 480;
	test_video_driver.getPresentationViewport = saved_viewport;
}

static void test_display_mode_presentation_viewport(void)
{
	RECT rect;

	video_set_pixel_aspect_ratio(10, 11);
	rect = display_mode_presentation_rect(DISPLAY_MODE_FULLSCREEN, 304, 224);
	assert(rect.left == 32);
	assert(rect.top == 16);
	assert(rect.right == 672);
	assert(rect.bottom == 464);

	rect = display_mode_presentation_rect(DISPLAY_MODE_4_3, 304, 224);
	assert(rect.left == 32);
	assert(rect.top == 22);
	assert(rect.right == 672);
	assert(rect.bottom == 458);

	/* Backends without a safe-area override retain the full physical output. */
	test_video_driver.getPresentationViewport = NULL;
	rect = display_mode_presentation_rect(DISPLAY_MODE_FULLSCREEN, 304, 224);
	assert(rect.left == 0);
	assert(rect.top == 0);
	assert(rect.right == 704);
	assert(rect.bottom == 480);
	test_video_driver.getPresentationViewport = test_get_presentation_viewport;
}

static void test_non_square_pixel_safe_viewport(int output_width, int output_height,
	int safe_x, int safe_y, int safe_width, int safe_height,
	int pixel_aspect_num, int pixel_aspect_den)
{
	const ui_layout_metrics_t *layout;
	int logical_width;
	int logical_height;
	int center_x;
	int center_y;

	video_set_pixel_aspect_ratio(pixel_aspect_num, pixel_aspect_den);
	ui_layout_compute_responsive_size(safe_width, safe_height,
		&logical_width, &logical_height);
	assert(logical_width == 480);
	assert(logical_height == 370);

	ui_layout_init_viewport(logical_width, logical_height,
		output_width, output_height,
		safe_x, safe_y, safe_width, safe_height);
	layout = ui_layout_get();
	assert(layout->output_width == output_width);
	assert(layout->output_height == output_height);
	assert(layout->viewport_x >= safe_x);
	assert(layout->viewport_y >= safe_y);
	assert(layout->viewport_x + layout->viewport_width <= safe_x + safe_width);
	assert(layout->viewport_y + layout->viewport_height <= safe_y + safe_height);

	ui_layout_transform_point(ui_layout_center_x(), ui_layout_center_y(),
		&center_x, &center_y);
	assert(center_x >= output_width / 2 - 1 &&
		center_x <= output_width / 2 + 1);
	assert(center_y >= output_height / 2 - 1 &&
		center_y <= output_height / 2 + 1);
}

int main(void)
{
	video_set_pixel_aspect_ratio(1, 1);
	test_responsive_layout(480, 272, 480, 272, 11);
	test_responsive_layout(640, 224, 778, 272, 11);
	test_responsive_layout(640, 448, 480, 336, 14);
	test_responsive_layout(640, 480, 480, 360, 16);
	test_responsive_layout(720, 480, 480, 320, 14);
	test_responsive_layout(720, 576, 480, 384, 17);
	test_responsive_layout(512, 448, 480, 420, 19);
	test_responsive_layout(800, 600, 480, 360, 16);
	test_responsive_layout(960, 540, 484, 272, 11);
	test_responsive_layout(1280, 720, 484, 272, 11);
	test_responsive_layout(320, 240, 480, 360, 16);
	test_responsive_layout(1024, 768, 480, 360, 16);
	test_responsive_layout(1920, 1080, 484, 272, 11);
	test_responsive_layout(600, 900, 480, 720, 34);
	test_responsive_content_scaling();
	test_aspect_preserving_viewport();
	test_non_square_pixel_layout(704, 480, 10, 11);
	test_non_square_pixel_layout(704, 240, 10, 22);
	test_non_square_pixel_display_mode();
	test_cps1_240p_display_modes();
	test_cps1_240p_presentation_rect();
	test_display_mode_presentation_viewport();
	test_non_square_pixel_safe_viewport(704, 480, 32, 16, 640, 448, 10, 11);
	test_non_square_pixel_safe_viewport(704, 240, 32, 8, 640, 224, 10, 22);
	video_set_pixel_aspect_ratio(1, 1);
	return 0;
}
