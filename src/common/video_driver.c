/******************************************************************************

	video_driver.c

******************************************************************************/

#include <stddef.h>
#include "video_driver.h"

void *video_data;

video_driver_t video_null = {
	"null", // ident
	NULL, // init
	NULL, // free
	NULL, // waitVsync
	NULL, // flipScreen
	NULL, // beginFrame
	NULL, // endFrame
	NULL, // frameAddr
	NULL, // workFrame
	NULL, // textureLayer
	NULL, // scissor
	NULL, // clearScreen
	NULL, // clearFrame
	NULL, // fillFrame
	NULL, // startWorkFrame
	NULL, // transferWorkFrame
	NULL, // copyRect
	NULL, // copyRectFlip
	NULL, // copyRectRotate
	NULL, // drawTexture
	NULL, // getNativeObjects
	NULL, // uploadMem
	NULL, // uploadClut
	NULL, // blitTexture
	NULL, // blitPoints
	NULL, // flushCache
	NULL, // enableDepthTest
	NULL, // disableDepthTest
	NULL, // clearDepthBuffer
	NULL, // clearColorBuffer
	NULL, // drawUISprite
	NULL, // drawUILine
	NULL, // drawUILineGradient
	NULL, // drawUIRect
	NULL, // fillUIRect
	NULL, // fillUIRectGradient
};

video_driver_t *video_drivers[] = {
#ifdef PSP
	&video_psp,
#endif
#ifdef PS2
	&video_ps2,
#endif
#ifdef DESKTOP
	&video_desktop,
#endif
	&video_null,
	NULL,
};
