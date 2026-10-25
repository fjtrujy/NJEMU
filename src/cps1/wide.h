#ifndef CPS1_WIDE_H
#define CPS1_WIDE_H

/* The wide view shows hardware X [16,496) instead of [64,448): 480x224 at
 * the arcade pixel aspect (5:3). The work frame is 480 pixels wide, so the
 * renderer draws it shifted left by cps1_wide_bias. Both are 0 natively. */
#define CPS1_WIDE_MARGIN 48
#define CPS1_WIDE_WIDTH (384 + 2 * CPS1_WIDE_MARGIN)

extern int cps1_wide_margin;
extern int cps1_wide_bias;

/* Per-game widescreen profile lifecycle (CPS_WIDESCREEN builds). The
 * profile's guarded program patches keep the game's view inside its stage;
 * this module only switches them and the geometry between frames. */
extern int option_cps_widescreen;
void cps1_wide_init(void);
void cps1_wide_exit(void);
void cps1_wide_update(void);

#endif /* CPS1_WIDE_H */
