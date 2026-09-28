#ifndef MVS_WIDE_PROFILE_H
#define MVS_WIDE_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MVS_WIDE_PROFILE_MAX_WORDS 4096
#define MVS_WIDE_PROFILE_CRC_START 0x80u
#define MVS_WIDE_PROFILE_MAX_PROGRAM 0x4000000u

enum mvs_wide_profile_status {
	MVS_WIDE_PROFILE_DRAFT,
	MVS_WIDE_PROFILE_EXPERIMENTAL,
	MVS_WIDE_PROFILE_VERIFIED
};

typedef struct mvs_wide_patch_word {
	uint32_t offset;
	uint16_t original;
	uint16_t wide;
} mvs_wide_patch_word_t;

typedef struct mvs_wide_profile {
	char game[16];
	char description[160];
	uint16_t ngh;
	uint32_t program_size;
	uint32_t program_crc32;
	enum mvs_wide_profile_status status;
	bool viewport_only;
	size_t word_count;
	mvs_wide_patch_word_t words[MVS_WIDE_PROFILE_MAX_WORDS];
} mvs_wide_profile_t;

/* The caller owns the returned profile and releases it with free(). */
mvs_wide_profile_t *mvs_wide_profile_load(const char *path,
	char *error, size_t error_size);

/* Fingerprints are CRC32/ISO-HDLC of canonical big-endian program bytes,
 * excluding the first 128 bytes which the BIOS replaces with its vectors.
 * This detects revision mismatches; it is not an authenticity signature. */
uint32_t mvs_wide_program_crc32(const uint16_t *program, size_t byte_length);

/* Returns native=0, wide=1, or -1 for invalid/mixed instructions or fingerprint.
 * Geometry-only profiles have no changed words and return 0 in either mode. */
int mvs_wide_profile_check(const mvs_wide_profile_t *profile,
	const uint16_t *program, size_t byte_length, char *error, size_t error_size);

/* Validates the entire program and every guard before the first write.
 * Neither this function nor the loader writes any ROM/profile file. */
bool mvs_wide_profile_apply(const mvs_wide_profile_t *profile,
	uint16_t *program, size_t byte_length, bool enable,
	char *error, size_t error_size);

#endif
