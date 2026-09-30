#ifndef COMMON_VIDEO_GEOMETRY_H
#define COMMON_VIDEO_GEOMETRY_H

/* NJEMU's logical UI canvas is shared by every backend. Physical output
 * dimensions remain platform/backend-owned. */
#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define BUF_WIDTH 512

/* Historical PSP refresh cadence used by the common frame limiter. */
#define REFRESH_RATE (59.940059)

#endif /* COMMON_VIDEO_GEOMETRY_H */
