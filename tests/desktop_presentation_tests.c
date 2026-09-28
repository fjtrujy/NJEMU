/* Exercise the actual SDL window target, not the cropped source-frame dumper.
 * No ROMs, display server or hardware renderer are required. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/video_driver.h"
#include "desktop/desktop_video.h"

extern video_driver_t video_desktop;

#define CHECK(call) do { \
	if (!(call)) { \
		fprintf(stderr, "%s:%d: %s failed: %s\n", __FILE__, __LINE__, #call, SDL_GetError()); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void check_window(SDL_Renderer *renderer, const RECT *game,
	uint8_t red, uint8_t green, uint8_t blue)
{
	int width, height, x, y;
	uint8_t *pixels;
	CHECK(SDL_GetRenderTarget(renderer) == NULL);
	CHECK(SDL_GetRendererOutputSize(renderer, &width, &height) == 0);
	pixels = malloc((size_t)width * height * 4);
	CHECK(pixels != NULL);
	CHECK(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGBA32,
		pixels, width * 4) == 0);
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			const uint8_t *p = pixels + ((size_t)y * width + x) * 4;
			const int inside = x >= game->left && x < game->right &&
				y >= game->top && y < game->bottom;
			const uint8_t r = inside ? red : 0;
			const uint8_t g = inside ? green : 0;
			const uint8_t b = inside ? blue : 0;
			if (p[0] != r || p[1] != g || p[2] != b)
			{
				fprintf(stderr, "Window pixel (%d,%d): got (%u,%u,%u), "
					"expected (%u,%u,%u)\n", x, y,
					p[0], p[1], p[2], r, g, b);
				free(pixels);
				exit(EXIT_FAILURE);
			}
		}
	free(pixels);
}

int main(void)
{
	uint16_t palette[16] = {0};
	layer_texture_info_t layer = {16, 16, 1};
	clut_info_t clut = {palette, 16, 1};
	RECT wide_source = {40, 15, 440, 240};
	RECT wide_destination = {0, 60, 640, 420};
	RECT native_source = {24, 16, 328, 240};
	RECT native_destination = {168, 128, 472, 352};
	SDL_Rect stale_clip = {7, 9, 20, 20};
	SDL_Rect stale_viewport = {13, 17, 600, 400};
	SDL_Renderer *renderer;
	void *video;
	int width, height;

	SDL_SetMainReady();
	CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
	video = video_desktop.init(&layer, 1, &clut);
	CHECK(video != NULL);
	renderer = desktop_video_get_renderer(video);
	CHECK(renderer != NULL);
	CHECK(SDL_GetRendererOutputSize(renderer, &width, &height) == 0);
	CHECK(width == 640 && height == 480);

	/* Simulate stale window contents and clipping left by a previous screen. */
	CHECK(SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255) == 0);
	CHECK(SDL_RenderClear(renderer) == 0);
	CHECK(SDL_RenderSetViewport(renderer, &stale_viewport) == 0);
	CHECK(SDL_RenderSetClipRect(renderer, &stale_clip) == 0);
	video_desktop.startWorkFrame(video, MAKECOL32(0, 255, 0));
	video_desktop.scissor(video, 40, 16, 440, 240);
	video_desktop.transferWorkFrame(video, &wide_source, &wide_destination);
	check_window(renderer, &wide_destination, 0, 255, 0);
	video_desktop.flipScreen(video, false);

	/* Changing back to native must erase all of the previous wide image. */
	video_desktop.startWorkFrame(video, MAKECOL32(255, 0, 0));
	video_desktop.scissor(video, 24, 16, 336, 240);
	video_desktop.transferWorkFrame(video, &native_source, &native_destination);
	check_window(renderer, &native_destination, 255, 0, 0);

	video_desktop.free(video);
	SDL_Quit();
	puts("Desktop presentation: black margins, full destination and mode switch passed");
	return 0;
}
