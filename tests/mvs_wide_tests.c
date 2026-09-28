#include "mvs/wide.h"
#include "mvs/wide_profile.h"

/* Keep the checks active when CTest is built with -DNDEBUG as well. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Synthetic instructions, not ROM data. Keep the verified profile explicit
 * here so a missing site or a wrong patch offset is detected independently. */
static const uint32_t sites[] = {
	0xdec8, 0xe170, 0xe416, 0xe6c2, 0xe98e, 0xecee, 0xf04a, 0xf3b6,
	0xf70a, 0xf9d4, 0xfca6, 0xff7e, 0x10288, 0x10626, 0x109ca, 0x10d7e,
	0x110d6, 0x1137c, 0x11620, 0x118ca, 0x11b94, 0x11ef2, 0x1224c, 0x125b6,
	0x12906, 0x12bcc, 0x12e9a, 0x1316e, 0x13474, 0x1380e, 0x13bae, 0x13f5e
};
static const uint16_t branches[] = {
	0x64dc, 0x64ca, 0x64dc, 0x64ca, 0x64dc, 0x64ca, 0x64de, 0x64cc
};
#define PROGRAM_BYTES 0x18300u

static mvs_wide_profile_t *test_profile;

/* Exercise the file-backed generic engine against the independent old oracle. */
static bool mvs_wide_patch_program(uint16_t *program, size_t size, bool enable)
{
	return mvs_wide_profile_apply(test_profile, program, size, enable, NULL, 0);
}

static void make_program(uint16_t *program)
{
	size_t i;
	memset(program, 0xa5, PROGRAM_BYTES);
	for (i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
	{
		uint16_t *p = program + sites[i] / 2;
		p[0] = 0x3405;
		p[1] = 0x0642;
		p[2] = 0x0200;
		p[3] = 0x0c42;
		p[4] = 0x5000;
		p[5] = branches[i / 4];
	}
	program[0x17e38 / 2] = 0x363c;
	program[0x17e3a / 2] = 0xa000;
	program[0x17dfe / 2] = 0x08a8;
	program[0x17e00 / 2] = 0x0007;
	program[0x17e02 / 2] = 0x007a;
	program[0x17e04 / 2] = 0x6700;
	program[0x17e06 / 2] = 0x03fc;
	for (i = 0; i < 32; i++)
	{
		static const uint16_t instructions[] = {
			0x0646, 0x0400, 0xb646, 0x6300, 0x0004,
			0xde41, 0x0446, 0x0400, 0x2886
		};
		memcpy(program + (0x17e46 + i * 30) / 2,
			instructions, sizeof(instructions));
	}
}

static void test_patch(void)
{
	uint16_t *program = malloc(PROGRAM_BYTES);
	uint16_t *original = malloc(PROGRAM_BYTES);
	uint16_t *snapshot = malloc(PROGRAM_BYTES);
	size_t i, changed;
	assert(program && original && snapshot);
	make_program(program);
	memcpy(original, program, PROGRAM_BYTES);
	/* Synthetic fixture identity; the production profile's real identity is
	 * checked separately against locally supplied program dumps. */
	test_profile->program_size = PROGRAM_BYTES;
	test_profile->program_crc32 = mvs_wide_program_crc32(program, PROGRAM_BYTES);

	assert(mvs_wide_patch_program(program, PROGRAM_BYTES, false));
	assert(memcmp(program, original, PROGRAM_BYTES) == 0);
	assert(mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	changed = 0;
	for (i = 0; i < PROGRAM_BYTES / 2; i++)
		changed += program[i] != original[i];
	assert(changed == 131);
	for (i = 0; i < 32; i++)
	{
		assert(program[sites[i] / 2 + 2] == 0x0e00);
		assert(program[sites[i] / 2 + 4] == 0x6800);
		assert(program[(0x17e46 + i * 30) / 2 + 1] == 0x1c00);
		assert(program[(0x17e46 + i * 30) / 2 + 7] == 0x1c00);
	}
	assert(program[0x17e3a / 2] == 0xd000);
	assert(program[0x17e04 / 2] == 0x4e71);
	assert(program[0x17e06 / 2] == 0x4e71);
	memcpy(snapshot, program, PROGRAM_BYTES);
	assert(mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);
	assert(mvs_wide_patch_program(program, PROGRAM_BYTES, false));
	assert(memcmp(original, program, PROGRAM_BYTES) == 0);

	/* A bad final instruction must not leave earlier sites half patched. */
	program[sites[31] / 2 + 5] ^= 2;
	memcpy(snapshot, program, PROGRAM_BYTES);
	assert(!mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);
	assert(!mvs_wide_patch_program(program, PROGRAM_BYTES, false));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);

	make_program(program);
	program[(0x17e46 + 31 * 30) / 2 + 8] ^= 2;
	memcpy(snapshot, program, PROGRAM_BYTES);
	assert(!mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);

	make_program(program);
	program[0x17e06 / 2] ^= 2;
	memcpy(snapshot, program, PROGRAM_BYTES);
	assert(!mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);

	make_program(program);
	memcpy(snapshot, program, PROGRAM_BYTES);
	assert(!mvs_wide_patch_program(program, sites[31] + 11, true));
	assert(!mvs_wide_patch_program(program, 0x17e46 + 31 * 30 + 17, true));
	assert(!mvs_wide_patch_program(program, 0, true));
	assert(!mvs_wide_patch_program(NULL, PROGRAM_BYTES, true));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);

	/* Reject a mixture of native and wide instructions, not just bad opcodes. */
	program[sites[31] / 2 + 2] = 0x0e00;
	program[sites[31] / 2 + 4] = 0x6800;
	memcpy(snapshot, program, PROGRAM_BYTES);
	assert(!mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	assert(!mvs_wide_patch_program(program, PROGRAM_BYTES, false));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);

	/* A matching instruction layout in another revision is not sufficient. */
	make_program(program);
	program[0x6000 / 2] ^= 1;
	memcpy(snapshot, program, PROGRAM_BYTES);
	assert(!mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	assert(memcmp(snapshot, program, PROGRAM_BYTES) == 0);
	make_program(program);
	program[0] ^= 1; /* BIOS vector changes are deliberately excluded. */
	assert(mvs_wide_patch_program(program, PROGRAM_BYTES, true));
	assert(mvs_wide_patch_program(program, PROGRAM_BYTES, false));

	free(snapshot);
	free(original);
	free(program);
}

static void test_clipping(void)
{
	unsigned int fixed_x;
	for (fixed_x = 0; fixed_x <= UINT16_MAX; fixed_x++)
	{
		const int signed_x = fixed_x < 0x8000 ? (int)fixed_x : (int)fixed_x - 0x10000;
		const bool native = (uint16_t)(fixed_x + 0x0200) < 0x5000;
		const bool wide = (uint16_t)(fixed_x + 0x0e00) < 0x6800;
		assert(native == (signed_x >= -8 * 64 && signed_x < 312 * 64));
		assert(wide == (signed_x >= -56 * 64 && signed_x < 360 * 64));
		assert(!native || wide);
		/* Background SCB4 positions wrap after 512 pixels. */
		assert(((uint16_t)(fixed_x + 0x1c00) < 0xd000) ==
			(fixed_x < 360 * 128 || fixed_x >= (512 - 56) * 128));
	}
}

static void test_geometry(void)
{
	const mvs_view_geometry_t *native = mvs_view_geometry_for_mode(false);
	const mvs_view_geometry_t *wide = mvs_view_geometry_for_mode(true);
	int hardware_x;
	assert(native->source_left == 24 && native->source_top == 16);
	assert(native->source_right == 328 && native->source_bottom == 240);
	assert(native->render_left == 24 && native->render_right == 336);
	assert(native->x_bias == 16);
	assert(wide->source_right - wide->source_left == 400);
	assert(wide->source_bottom - wide->source_top == 225);
	assert(wide->render_left == wide->source_left);
	assert(wide->render_right == wide->source_right);
	assert(wide->source_top + 1 == wide->render_top);
	assert(wide->source_bottom == wide->render_bottom);
	/* FIX and native sprites stay centered: 48 extra pixels on each side. */
	assert(8 + wide->x_bias - wide->source_left == (400 - 304) / 2);
	for (hardware_x = 0; hardware_x < 512; hardware_x++)
	{
		const int work_x = (hardware_x + wide->x_bias) & 511;
		const bool visible = work_x >= wide->render_left && work_x < wide->render_right;
		assert(visible == (hardware_x < 360 || hardware_x >= 472));
	}
}

static void test_presentation(void)
{
	int w, h;
	mvs_wide_fit_output(640, 480, &w, &h);
	assert(w == 640 && h == 360);
	mvs_wide_fit_output(1280, 720, &w, &h);
	assert(w == 1280 && h == 720);
	mvs_wide_fit_output(960, 544, &w, &h);
	assert(w == 960 && h == 540);
	mvs_wide_fit_output(480, 272, &w, &h);
	assert(w == 480 && h == 270);
	mvs_wide_fit_output(720, 1280, &w, &h);
	assert(w == 720 && h == 405);
	mvs_wide_fit_output(2560, 1080, &w, &h);
	assert(w == 1920 && h == 1080);
	mvs_wide_fit_output(0, 480, &w, &h);
	assert(w == 0 && h == 0);
	mvs_wide_fit_output(1, 1, &w, &h);
	assert(w == 1 && h == 1);
	mvs_wide_fit_output(INT_MAX, INT_MAX, &w, &h);
	assert(w == INT_MAX && h == (int)((int64_t)INT_MAX * 9 / 16));
}

int main(int argc, char **argv)
{
	char error[192];
	if (argc == 2)
	{
		mvs_wide_profile_t *profile = mvs_wide_profile_load(argv[1], error, sizeof(error));
		if (!profile) { fprintf(stderr, "%s\n", error); return 1; }
		free(profile);
		return 0;
	}
	test_profile = mvs_wide_profile_load(TEST_PROFILE_DIR "/mslug3.ini", error, sizeof(error));
	if (!test_profile) { fprintf(stderr, "%s\n", error); return 1; }
	assert(test_profile->program_size == 0x900000);
	assert(test_profile->program_crc32 == 0x714dc992);
	assert(!strcmp(test_profile->game, "mslug3") && test_profile->ngh == 0x0256);
	test_patch();
	test_clipping();
	test_geometry();
	test_presentation();
	free(test_profile);
	puts("MVS widescreen tests passed");
	return 0;
}
