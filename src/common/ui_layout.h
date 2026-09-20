/******************************************************************************

	ui_layout.h

	Resolution-independent UI layout metrics.

******************************************************************************/

#ifndef COMMON_UI_LAYOUT_H
#define COMMON_UI_LAYOUT_H

typedef struct ui_layout_metrics
{
	int logical_width;
	int logical_height;
	int output_width;
	int output_height;
	int viewport_x;
	int viewport_y;
	int viewport_width;
	int viewport_height;
	float scale;
} ui_layout_metrics_t;

void ui_layout_init(int logical_width, int logical_height,
	int output_width, int output_height);
const ui_layout_metrics_t *ui_layout_get(void);
int ui_layout_uses_output_transform(void);
void ui_layout_transform_point(int x, int y, int *out_x, int *out_y);
void ui_layout_transform_rect(int x, int y, int w, int h,
	int *out_x, int *out_y, int *out_w, int *out_h);

static inline int ui_layout_center_x(void)
{
	return ui_layout_get()->logical_width / 2;
}

static inline int ui_layout_center_y(void)
{
	return ui_layout_get()->logical_height / 2;
}

static inline int ui_layout_right(int inset)
{
	return ui_layout_get()->logical_width - 1 - inset;
}

static inline int ui_layout_bottom(int inset)
{
	return ui_layout_get()->logical_height - 1 - inset;
}

static inline int ui_layout_visible_rows(int top, int row_height)
{
	int available = ui_layout_get()->logical_height - top;
	return row_height > 0 && available > 0 ? available / row_height : 0;
}

#endif /* COMMON_UI_LAYOUT_H */
