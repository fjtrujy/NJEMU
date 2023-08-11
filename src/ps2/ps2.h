/******************************************************************************

	ps2.h


******************************************************************************/

#ifndef PS2_MAIN_H
#define PS2_MAIN_H

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

#define SCR_WIDTH  640
#define SCR_HEIGHT 480
#define BUF_WIDTH  640

#define	FRAMESIZE			(BUF_WIDTH * SCR_HEIGHT * sizeof(uint16_t))
#define	FRAMESIZE32			(BUF_WIDTH * SCR_HEIGHT * sizeof(uint32_t))

#define REFRESH_RATE		(59.940059)		// (9000000Hz * 1) / (525 * 286)

#define FONTSIZE			14

#endif /* PS2_MAIN_H */
