#include <assert.h>

#include "common/ui_layout.h"

static void test_native_layout(int width, int height, int expected_rows)
{
	const ui_layout_metrics_t *layout;

	ui_layout_init(width, height, width, height);
	layout = ui_layout_get();

	assert(layout->logical_width == width);
	assert(layout->logical_height == height);
	assert(layout->output_width == width);
	assert(layout->output_height == height);
	assert(layout->viewport_x == 0);
	assert(layout->viewport_y == 0);
	assert(layout->viewport_width == width);
	assert(layout->viewport_height == height);
	assert(ui_layout_center_x() == width / 2);
	assert(ui_layout_center_y() == height / 2);
	assert(ui_layout_right(0) == width - 1);
	assert(ui_layout_bottom(0) == height - 1);
	assert(ui_layout_visible_rows(37, 20) == expected_rows);
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
	test_native_layout(480, 272, 11);
	test_native_layout(640, 448, 20);
	test_native_layout(800, 600, 28);
	test_native_layout(960, 540, 25);
	test_aspect_preserving_viewport();
	return 0;
}
