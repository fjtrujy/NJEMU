#ifndef COMMON_WIDE_PROFILE_H
#define COMMON_WIDE_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIDE_PROFILE_MAX_WORDS 4096
#define WIDE_PROFILE_CRC_START 0x80u
#define WIDE_PROFILE_MAX_PROGRAM 0x4000000u

enum wide_profile_status {
	WIDE_PROFILE_DRAFT,
	WIDE_PROFILE_EXPERIMENTAL,
	WIDE_PROFILE_VERIFIED
};

typedef struct wide_patch_word {
	uint32_t offset;
	uint16_t original;
	uint16_t wide;
} wide_patch_word_t;

/* Version 2 [layers]: the game's stage-layer records in 68000 RAM. Each
 * record has a word whose active_mask bits mark a running layer, a signed
 * scroll X, a map width in 16-pixel tiles, its first SCB1 word and its
 * sprite column count, at these word offsets. */
typedef struct wide_layers {
	uint32_t address;
	uint16_t count;
	uint16_t stride;
	uint16_t active;
	uint16_t active_mask;
	uint16_t x;
	uint16_t width;
	uint16_t sprite;
	uint16_t columns;
	uint16_t main;
} wide_layers_t;

typedef struct wide_profile {
	char game[16];
	char description[160];
	uint16_t ngh;
	uint32_t program_size;
	uint32_t program_crc32;
	enum wide_profile_status status;
	bool viewport_only;
	bool has_layers;
	wide_layers_t layers;
	size_t word_count;
	wide_patch_word_t words[WIDE_PROFILE_MAX_WORDS];
} wide_profile_t;

/* The caller owns the returned profile and releases it with free(). */
wide_profile_t *wide_profile_load(const char *path,
	char *error, size_t error_size);

/* Fingerprints are CRC32/ISO-HDLC of canonical big-endian program bytes,
 * excluding the first 128 bytes which the BIOS replaces with its vectors.
 * This detects revision mismatches; it is not an authenticity signature. */
uint32_t wide_program_crc32(const uint16_t *program, size_t byte_length);

/* Returns native=0, wide=1, or -1 for invalid/mixed instructions or fingerprint.
 * Geometry-only profiles have no changed words and return 0 in either mode. */
int wide_profile_check(const wide_profile_t *profile,
	const uint16_t *program, size_t byte_length, char *error, size_t error_size);

/* Validates the entire program and every guard before the first write.
 * Neither this function nor the loader writes any ROM/profile file. */
bool wide_profile_apply(const wide_profile_t *profile,
	uint16_t *program, size_t byte_length, bool enable,
	char *error, size_t error_size);

/* Fills pan[0..sprites) with the horizontal shift of each sprite column of
 * the active stage layers, so that hardware X [left,right) never shows a
 * layer beyond its finite map; other sprites get 0. Each streamed layer
 * (map wider than its columns) at least as wide as the view keeps its own
 * shift; other layers follow the main one. Without an active main layer
 * that wide, every shift is 0. read16 reads 68000 RAM. */
void wide_layers_pan(const wide_layers_t *layers,
	uint16_t (*read16)(uint32_t address), int left, int right,
	int8_t *pan, size_t sprites);

#endif
