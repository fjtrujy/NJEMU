#include <assert.h>

#include "common/ui_layout.h"

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

int main(void)
{
	test_responsive_layout(480, 272, 480, 272, 11);
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
	return 0;
}
