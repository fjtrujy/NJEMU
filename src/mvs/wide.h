#ifndef MVS_WIDE_H
#define MVS_WIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mvs_view_geometry
{
	int source_left;
	int source_top;
	int source_right;
	int source_bottom;
	int render_left;
	int render_top;
	int render_right;
	int render_bottom;
	int x_bias;
} mvs_view_geometry_t;

const mvs_view_geometry_t *mvs_view_geometry_for_mode(bool wide);
void mvs_wide_fit_output(int output_width, int output_height,
	int *width, int *height);
void mvs_wide_set_active(bool active);
int mvs_true_wide_enabled(void);
const mvs_view_geometry_t *mvs_get_view_geometry(void);

/* Horizontal shift of each sprite's column in the wide view, set per frame
 * from the profile's stage layers; all zero otherwise. Indexed by sprite. */
#define MVS_WIDE_PAN_SPRITES 384
extern int8_t mvs_wide_sprite_pan[MVS_WIDE_PAN_SPRITES];

/* Profile lifecycle (mvs/wide_runtime.c). The per-game option exists where
 * the build enables profiles (MVS_WIDESCREEN). */
extern int option_mvs_widescreen;
void mvs_wide_init(void);
void mvs_wide_exit(void);
void mvs_wide_update(void);

#endif /* MVS_WIDE_H */
