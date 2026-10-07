#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/sound.h"
#include "sound/qsound.h"

#define TEST_ROM_SIZE 0x20000u
#define TEST_SAMPLES 64

uint8_t *memory_region_sound1;
uint32_t memory_length_sound1;
static struct sound_t test_sound;
struct sound_t *sound = &test_sound;
static const char *test_driver_name;

const char *capcom_driver_name(void)
{
	return test_driver_name;
}

static void init_context(qsound_context_t *context, const int8_t *sample_rom,
	int volume_shift)
{
	memset(context, 0, sizeof(*context));
	context->sample_rom = sample_rom;
	context->volume_shift = volume_shift;
}

static void write_command(qsound_context_t *context, uint8_t command, uint16_t value)
{
	qsound_context_data_h_w(context, (uint8_t)(value >> 8));
	qsound_context_data_l_w(context, (uint8_t)value);
	qsound_context_cmd_w(context, command);
}

static void configure_channel_zero(qsound_context_t *context, uint16_t start,
	uint16_t end, uint16_t loop, uint16_t volume)
{
	/* Bank register 0 targets the following channel, so channel 0 uses slot 15. */
	write_command(context, 0x78, 0x0000);
	write_command(context, 0x01, start);
	write_command(context, 0x02, 0x1000);
	write_command(context, 0x04, loop);
	write_command(context, 0x05, end);
	write_command(context, 0x80, 0x0020);
	write_command(context, 0x06, volume);
}

static void clear_stereo(int32_t *left, int32_t *right, uint32_t samples)
{
	memset(left, 0, samples * sizeof(*left));
	memset(right, 0, samples * sizeof(*right));
}

static int stereo_equal(const int32_t *a_left, const int32_t *a_right,
	const int32_t *b_left, const int32_t *b_right, uint32_t samples)
{
	return memcmp(a_left, b_left, samples * sizeof(*a_left)) == 0 &&
		memcmp(a_right, b_right, samples * sizeof(*a_right)) == 0;
}

static int stereo_nonzero(const int32_t *left, const int32_t *right,
	uint32_t samples)
{
	uint32_t i;

	for (i = 0; i < samples; i++)
	{
		if (left[i] != 0 || right[i] != 0)
			return 1;
	}
	return 0;
}

static int test_independent_contexts(const int8_t *sample_rom)
{
	qsound_context_t a;
	qsound_context_t b;

	init_context(&a, sample_rom, 6);
	init_context(&b, sample_rom, 6);

	write_command(&a, 0x78, 0x0001);
	write_command(&a, 0x01, 0x1234);
	write_command(&a, 0x02, 0x0456);
	write_command(&a, 0x04, 0x0020);
	write_command(&a, 0x05, 0x3456);
	write_command(&a, 0x80, 0x0018);
	write_command(&a, 0x06, 0x0200);

	if (a.channel[0].bank != 0x10000 ||
		a.channel[0].address != 0x1234 ||
		a.channel[0].pitch == 0 ||
		a.channel[0].loop != 0x0020 ||
		a.channel[0].end != 0x3456 ||
		a.channel[0].vol != 0x0200 ||
		a.channel[0].pan != 0x0018 ||
		!a.channel[0].key)
	{
		fprintf(stderr, "QSound register writes did not update channel state\n");
		return 0;
	}
	if (memcmp(&b.channel[0], &(qsound_channel_state_t){ 0 },
			sizeof(b.channel[0])) != 0)
	{
		fprintf(stderr, "QSound register state leaked between contexts\n");
		return 0;
	}
	return 1;
}

static int test_looping_and_non_looping(const int8_t *sample_rom)
{
	qsound_context_t a;
	qsound_context_t b;
	int32_t a_left[TEST_SAMPLES], a_right[TEST_SAMPLES];
	int32_t b_left[TEST_SAMPLES], b_right[TEST_SAMPLES];
	int32_t *a_buffer[2] = { a_left, a_right };
	int32_t *b_buffer[2] = { b_left, b_right };
	uint32_t block;

	init_context(&a, sample_rom, 6);
	init_context(&b, sample_rom, 6);
	configure_channel_zero(&a, 0, 4, 0, 0x0100);
	configure_channel_zero(&b, 0, 4, 0, 0x0100);
	clear_stereo(a_left, a_right, 16);
	clear_stereo(b_left, b_right, 16);
	qsound_context_update(&a, a_buffer, 16);
	qsound_context_update(&b, b_buffer, 16);
	if (!stereo_equal(a_left, a_right, b_left, b_right, 16) ||
		!stereo_nonzero(a_left, a_right, 16) ||
		a.channel[0].key || b.channel[0].key)
	{
		fprintf(stderr, "QSound non-looping playback state/output mismatch\n");
		return 0;
	}

	init_context(&a, sample_rom, 6);
	init_context(&b, sample_rom, 6);
	configure_channel_zero(&a, 4, 8, 4, 0x0100);
	configure_channel_zero(&b, 4, 8, 4, 0x0100);
	for (block = 0; block < 3; block++)
	{
		clear_stereo(a_left, a_right, TEST_SAMPLES);
		clear_stereo(b_left, b_right, TEST_SAMPLES);
		qsound_context_update(&a, a_buffer, TEST_SAMPLES);
		qsound_context_update(&b, b_buffer, TEST_SAMPLES);
		if (!stereo_equal(a_left, a_right, b_left, b_right, TEST_SAMPLES))
		{
			fprintf(stderr, "QSound multi-block looping output diverged\n");
			return 0;
		}
	}
	if (!a.channel[0].key || !b.channel[0].key ||
		a.channel[0].address < 4 || a.channel[0].address >= 8 ||
		b.channel[0].address < 4 || b.channel[0].address >= 8)
	{
		fprintf(stderr, "QSound looping playback did not preserve active channel state\n");
		return 0;
	}
	return 1;
}

static int test_clone_restore_and_isolation(const int8_t *sample_rom,
	const int8_t *restored_rom)
{
	qsound_context_t source;
	qsound_context_t clone;
	qsound_context_t restored;
	int32_t source_left[TEST_SAMPLES], source_right[TEST_SAMPLES];
	int32_t peer_left[TEST_SAMPLES], peer_right[TEST_SAMPLES];
	int32_t *source_buffer[2] = { source_left, source_right };
	int32_t *peer_buffer[2] = { peer_left, peer_right };
	int original_volume;
	uint32_t block;

	init_context(&source, sample_rom, 6);
	configure_channel_zero(&source, 16, 48, 24, 0x0120);
	clear_stereo(source_left, source_right, 11);
	qsound_context_update(&source, source_buffer, 11);

	if (!qsound_context_clone_for_worker(&clone, &source) ||
		clone.sample_rom != source.sample_rom ||
		memcmp(clone.channel, source.channel, sizeof(source.channel)) != 0 ||
		clone.data != source.data || clone.volume_shift != source.volume_shift)
	{
		fprintf(stderr, "QSound context clone did not preserve live state\n");
		return 0;
	}

	for (block = 0; block < 3; block++)
	{
		clear_stereo(source_left, source_right, TEST_SAMPLES);
		clear_stereo(peer_left, peer_right, TEST_SAMPLES);
		qsound_context_update(&source, source_buffer, TEST_SAMPLES);
		qsound_context_update(&clone, peer_buffer, TEST_SAMPLES);
		if (!stereo_equal(source_left, source_right, peer_left, peer_right,
				TEST_SAMPLES) ||
			memcmp(clone.channel, source.channel, sizeof(source.channel)) != 0)
		{
			fprintf(stderr, "QSound clone continuation diverged\n");
			return 0;
		}
	}

	original_volume = source.channel[0].vol;
	write_command(&clone, 0x06, 0x0040);
	if (source.channel[0].vol != original_volume ||
		clone.channel[0].vol == source.channel[0].vol)
	{
		fprintf(stderr, "QSound cloned context was not isolated\n");
		return 0;
	}
	clear_stereo(source_left, source_right, TEST_SAMPLES);
	clear_stereo(peer_left, peer_right, TEST_SAMPLES);
	qsound_context_update(&source, source_buffer, TEST_SAMPLES);
	qsound_context_update(&clone, peer_buffer, TEST_SAMPLES);
	if (stereo_equal(source_left, source_right, peer_left, peer_right, TEST_SAMPLES))
	{
		fprintf(stderr, "QSound isolated context change did not affect PCM independently\n");
		return 0;
	}

	init_context(&restored, restored_rom, 6);
	write_command(&restored, 0x01, 0x7777);
	if (!qsound_context_restore_from_worker(&restored, &source) ||
		restored.sample_rom != restored_rom ||
		restored.volume_shift != 6 ||
		memcmp(restored.channel, source.channel, sizeof(source.channel)) != 0 ||
		restored.data != source.data)
	{
		fprintf(stderr, "QSound context restore did not preserve CPU bindings/state\n");
		return 0;
	}
	for (block = 0; block < 3; block++)
	{
		clear_stereo(source_left, source_right, TEST_SAMPLES);
		clear_stereo(peer_left, peer_right, TEST_SAMPLES);
		qsound_context_update(&source, source_buffer, TEST_SAMPLES);
		qsound_context_update(&restored, peer_buffer, TEST_SAMPLES);
		if (!stereo_equal(source_left, source_right, peer_left, peer_right,
				TEST_SAMPLES) ||
			memcmp(restored.channel, source.channel, sizeof(source.channel)) != 0)
		{
			fprintf(stderr, "QSound restored context continuation diverged\n");
			return 0;
		}
	}
	return 1;
}

static int expect_default_volume_shift(const char *driver_name, int expected)
{
	qsound_context_t snapshot;

	test_driver_name = driver_name;
	qsound_sh_start();
	qsound_sh_reset();
	if (!qsound_default_clone_for_worker(&snapshot) ||
		snapshot.volume_shift != expected)
	{
		fprintf(stderr, "QSound volume shift for %s was %d, expected %d\n",
			driver_name, snapshot.volume_shift, expected);
		return 0;
	}
	return 1;
}

static int test_target_volume_profiles(void)
{
#if (EMU_SYSTEM == CPS1)
	return expect_default_volume_shift("sf2", 6) &&
		expect_default_volume_shift("punisher", 4);
#elif (EMU_SYSTEM == CPS2)
	return expect_default_volume_shift("ssf2", 6) &&
		expect_default_volume_shift("csclub", 4) &&
		expect_default_volume_shift("ddsom", 5) &&
		expect_default_volume_shift("batcir", 7);
#else
	return 1;
#endif
}

int main(void)
{
	static int8_t sample_rom[TEST_ROM_SIZE];
	static int8_t restored_rom[TEST_ROM_SIZE];
	uint32_t i;

	if (qsound_context_size() != sizeof(qsound_context_t))
	{
		fprintf(stderr, "QSound context size API mismatch\n");
		return 1;
	}
	for (i = 0; i < TEST_ROM_SIZE; i++)
	{
		sample_rom[i] = (int8_t)((i * 37u + 11u) & 0xffu);
		restored_rom[i] = sample_rom[i];
	}
	memory_region_sound1 = (uint8_t *)sample_rom;
	memory_length_sound1 = TEST_ROM_SIZE;

	if (!test_independent_contexts(sample_rom) ||
		!test_looping_and_non_looping(sample_rom) ||
		!test_clone_restore_and_isolation(sample_rom, restored_rom) ||
		!test_target_volume_profiles())
		return 1;

	printf("QSound independent context clone/restore/render oracle passed\n");
	return 0;
}
