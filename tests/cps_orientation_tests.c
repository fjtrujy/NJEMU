#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "common/cps_orientation.h"
#include "common/hw_render.h"

static void test_presentation_resolution(void)
{
	/* Persisted legacy RotateScreen values are part of the config contract. */
	assert(CPS_SCREEN_ORIENTATION_ROTATED_DISPLAY == 0);
	assert(CPS_SCREEN_ORIENTATION_UPRIGHT == 1);
	assert(CPS_SCREEN_ORIENTATION_AUTO == 2);

	assert(!cps_orientation_rotates_to_upright(
		false, CPS_SCREEN_ORIENTATION_ROTATED_DISPLAY));
	assert(!cps_orientation_rotates_to_upright(
		false, CPS_SCREEN_ORIENTATION_UPRIGHT));
	assert(!cps_orientation_rotates_to_upright(
		false, CPS_SCREEN_ORIENTATION_AUTO));

	assert(!cps_orientation_rotates_to_upright(
		true, CPS_SCREEN_ORIENTATION_ROTATED_DISPLAY));
	assert(cps_orientation_rotates_to_upright(
		true, CPS_SCREEN_ORIENTATION_UPRIGHT));
	assert(cps_orientation_rotates_to_upright(
		true, CPS_SCREEN_ORIENTATION_AUTO));
}

static void test_upright_vertical_controls(void)
{
	const uint32_t buttons =
		PLATFORM_PAD_UP | PLATFORM_PAD_RIGHT |
		PLATFORM_PAD_B1 | PLATFORM_PAD_R;

	assert(cps_orientation_adjust_buttons(
		buttons, true, true, false) == buttons);
}

static void test_rotated_display_controls(void)
{
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_UP, true, false, false) == PLATFORM_PAD_LEFT);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_DOWN, true, false, false) == PLATFORM_PAD_RIGHT);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_LEFT, true, false, false) == PLATFORM_PAD_DOWN);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_RIGHT, true, false, false) == PLATFORM_PAD_UP);

	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_B1, true, false, false) == PLATFORM_PAD_B4);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_B2, true, false, false) == PLATFORM_PAD_B1);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_B3, true, false, false) == PLATFORM_PAD_B2);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_B4, true, false, false) == PLATFORM_PAD_B3);

	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_START | PLATFORM_PAD_SELECT |
		PLATFORM_PAD_L | PLATFORM_PAD_R,
		true, false, false) ==
		(PLATFORM_PAD_START | PLATFORM_PAD_SELECT |
		 PLATFORM_PAD_L | PLATFORM_PAD_R));
}

static void test_flip_composition(void)
{
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_UP, false, false, true) == PLATFORM_PAD_DOWN);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_B1, false, false, true) == PLATFORM_PAD_B3);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_R, false, false, true) == PLATFORM_PAD_L);

	/* Rotated-display UP becomes LEFT, then the 180-degree flip makes it RIGHT. */
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_UP, true, false, true) == PLATFORM_PAD_RIGHT);
	assert(cps_orientation_adjust_buttons(
		PLATFORM_PAD_R, true, false, true) == PLATFORM_PAD_L);
}

static void assert_point(const hw_xform_t *m, float x, float y,
	float expected_x, float expected_y)
{
	float mapped_x;
	float mapped_y;

	hw_map_point(m, x, y, &mapped_x, &mapped_y);
	assert(mapped_x == expected_x);
	assert(mapped_y == expected_y);
}

static void test_hardware_orientation_geometry(void)
{
	const RECT src = { 64, 16, 448, 240 };
	const RECT dst = { 100, 20, 324, 404 };
	RECT out;
	hw_xform_t transform;

	assert(hw_present_geometry(&src, &dst, HW_ORIENT_ROTATE,
		640, 448, &out, &transform));
	assert(out.left == dst.left && out.top == dst.top);
	assert(out.right == dst.right && out.bottom == dst.bottom);

	/* CPS upright rotation: TR -> TL, BR -> TR, TL -> BL, BL -> BR. */
	assert_point(&transform, src.right, src.top, dst.left, dst.top);
	assert_point(&transform, src.right, src.bottom, dst.right, dst.top);
	assert_point(&transform, src.left, src.top, dst.left, dst.bottom);
	assert_point(&transform, src.left, src.bottom, dst.right, dst.bottom);

	assert(hw_present_geometry(&src, &dst, HW_ORIENT_ROTATE_FLIP,
		640, 448, &out, &transform));
	assert_point(&transform, src.left, src.bottom, dst.left, dst.top);
	assert_point(&transform, src.left, src.top, dst.right, dst.top);
	assert_point(&transform, src.right, src.bottom, dst.left, dst.bottom);
	assert_point(&transform, src.right, src.top, dst.right, dst.bottom);
}

int main(void)
{
	test_presentation_resolution();
	test_upright_vertical_controls();
	test_rotated_display_controls();
	test_flip_composition();
	test_hardware_orientation_geometry();
	return 0;
}
