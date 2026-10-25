#include "common/wide_profile.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 2048
#define MAX_FILE 65536u
#define MAX_LIST 128
#define MAX_SECTIONS 128

static void explain(char *error, size_t size, const char *format, ...)
{
	va_list args;
	if (!error || size == 0) return;
	va_start(args, format);
	vsnprintf(error, size, format, args);
	va_end(args);
}

static char *trim(char *text)
{
	char *end;
	while (isspace((unsigned char)*text)) text++;
	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
	return text;
}

static int number(const char *text, int base, uint32_t maximum, uint32_t *out)
{
	char *end;
	unsigned long value;
	if (!isxdigit((unsigned char)*text)) return 0;
	if (!base) base = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
	errno = 0;
	value = strtoul(text, &end, base);
	if (errno || end == text || *end || value > maximum) return 0;
	*out = (uint32_t)value;
	return 1;
}

static int numbers(char *text, int base, uint32_t maximum, uint32_t *out)
{
	int count = 0;
	char *p = text;
	while (*p)
	{
		char *start;
		while (*p == ',' || isspace((unsigned char)*p)) p++;
		if (!*p) break;
		start = p;
		while (*p && *p != ',' && !isspace((unsigned char)*p)) p++;
		if (*p) *p++ = '\0';
		if (count == MAX_LIST || !number(start, base, maximum, &out[count])) return -1;
		count++;
	}
	return count;
}

typedef struct patch_section {
	uint32_t offsets[MAX_LIST], original[MAX_LIST], wide[MAX_LIST];
	int offsets_count, original_count, wide_count;
	uint32_t repeat, stride;
	unsigned int seen;
} patch_section_t;

static int append_section(wide_profile_t *profile, const patch_section_t *patch)
{
	int i, j;
	uint32_t repetition;
	if ((patch->seen & 7) != 7 || patch->offsets_count <= 0 ||
		patch->original_count <= 0 || patch->original_count != patch->wide_count ||
		patch->repeat == 0 || (patch->repeat > 1 && patch->stride == 0)) return 0;
	for (i = 0; i < patch->offsets_count; i++)
		for (repetition = 0; repetition < patch->repeat; repetition++)
			for (j = 0; j < patch->original_count; j++)
			{
				uint64_t offset = (uint64_t)patch->offsets[i] +
					(uint64_t)repetition * patch->stride + (unsigned int)j * 2u;
				wide_patch_word_t *word;
				if (profile->word_count == WIDE_PROFILE_MAX_WORDS ||
					offset < 0x100 || (offset & 1) || offset + 2 > profile->program_size) return 0;
				word = &profile->words[profile->word_count++];
				word->offset = (uint32_t)offset;
				word->original = (uint16_t)patch->original[j];
				word->wide = (uint16_t)patch->wide[j];
			}
	return 1;
}

static const char *const layer_keys[] = {
	"address", "count", "stride", "active", "active_mask", "x", "width",
	"sprite", "columns", "main"
};
#define LAYER_KEYS (sizeof(layer_keys) / sizeof(layer_keys[0]))

/* Records must lie in the 64 KiB of 68000 work RAM and fields inside them. */
static int layers_valid(const wide_layers_t *layers, unsigned int seen)
{
	const uint16_t fields[] = {
		layers->active, layers->x, layers->width, layers->sprite, layers->columns
	};
	size_t i;
	if (seen != (1u << LAYER_KEYS) - 1 || layers->count == 0 || layers->count > 16 ||
		layers->stride < 2 || (layers->stride & 1) || (layers->address & 1) ||
		layers->address < 0x100000 ||
		layers->address + (uint32_t)layers->count * layers->stride > 0x110000 ||
		layers->main >= layers->count || layers->active_mask == 0) return 0;
	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
		if ((fields[i] & 1) || fields[i] > layers->stride - 2) return 0;
	return 1;
}

static int compare_words(const void *left, const void *right)
{
	const wide_patch_word_t *a = left, *b = right;
	return (a->offset > b->offset) - (a->offset < b->offset);
}

static int read_line(FILE *file, char line[MAX_LINE], size_t *total)
{
	size_t used = 0;
	int c;
	while ((c = fgetc(file)) != EOF)
	{
		if (++*total > MAX_FILE || c > 127 ||
			(c < 32 && c != '\t' && c != '\r' && c != '\n') || used == MAX_LINE - 1) return -1;
		line[used++] = (char)c;
		if (c == '\r')
		{
			int next = fgetc(file);
			if (next == '\n')
			{
				if (++*total > MAX_FILE || used == MAX_LINE - 1) return -1;
				line[used++] = '\n';
			}
			else if (next != EOF && ungetc(next, file) == EOF) return -1;
			break;
		}
		if (c == '\n') break;
	}
	line[used] = '\0';
	return used != 0;
}

wide_profile_t *wide_profile_load(const char *path, char *error, size_t error_size)
{
	FILE *file;
	wide_profile_t *profile;
	patch_section_t patch;
	char line[MAX_LINE], section_names[MAX_SECTIONS][64];
	unsigned int line_no = 0, meta_seen = 0, layers_seen = 0, version = 0;
	size_t total = 0, i;
	int section = 0, sections = 0, changed = 0, line_status;
	const char *failure = "invalid profile";

	if (error && error_size) error[0] = '\0';
	file = path ? fopen(path, "rb") : NULL;
	if (!file) { explain(error, error_size, "cannot open profile: %s", path ? path : "(null)"); return NULL; }
	profile = calloc(1, sizeof(*profile));
	if (!profile) { fclose(file); explain(error, error_size, "out of memory"); return NULL; }
	memset(&patch, 0, sizeof(patch));
	patch.repeat = 1;

	while ((line_status = read_line(file, line, &total)) > 0)
	{
		char *text, *equals, *key, *value, *comment;
		unsigned int bit = 0;
		uint32_t parsed;
		line_no++;
		comment = strpbrk(line, "#;");
		if (comment) *comment = '\0';
		text = trim(line);
		if (!*text) continue;
		if (*text == '[')
		{
			size_t length = strlen(text);
			if (length < 3 || text[length - 1] != ']') goto fail;
			text[length - 1] = '\0';
			text++;
			if (section == 2 && !append_section(profile, &patch))
			{ failure = "invalid or out-of-range patch section"; goto fail; }
			if (section == 3 && !layers_valid(&profile->layers, layers_seen))
			{ failure = "invalid layers section"; goto fail; }
			if (strcmp(text, "profile") == 0 && section == 0) section = 1;
			else if (strcmp(text, "layers") == 0 && section != 0 && version >= 2 && !profile->has_layers)
			{
				if ((meta_seen & 63) != 63) { failure = "missing metadata"; goto fail; }
				profile->has_layers = true;
				section = 3;
			}
			else if (strncmp(text, "patch ", 6) == 0 && text[6] && section != 0)
			{
				if ((meta_seen & 63) != 63 || sections == MAX_SECTIONS || strlen(text) >= 64)
				{ failure = "missing metadata or too many/long patch sections"; goto fail; }
				for (i = 0; i < (size_t)sections; i++)
					if (strcmp(text, section_names[i]) == 0) { failure = "duplicate section"; goto fail; }
				strcpy(section_names[sections++], text);
				section = 2;
				memset(&patch, 0, sizeof(patch));
				patch.repeat = 1;
			}
			else { failure = "unknown, repeated or misplaced section"; goto fail; }
			continue;
		}
		equals = strchr(text, '=');
		if (!equals || !section) goto fail;
		*equals = '\0';
		key = trim(text);
		value = trim(equals + 1);
		if (section == 1)
		{
			if (strcmp(key, "version") == 0)
			{
				bit = 1;
				if (strcmp(value, "1") != 0 && strcmp(value, "2") != 0) { failure = "unsupported profile version"; goto fail; }
				version = (unsigned int)(value[0] - '0');
			}
			else if (strcmp(key, "game") == 0)
			{
				bit = 2;
				if (!*value || strlen(value) >= sizeof(profile->game)) goto fail;
				for (i = 0; value[i]; i++)
					if (!((value[i] >= 'a' && value[i] <= 'z') || isdigit((unsigned char)value[i]) ||
						value[i] == '_' || value[i] == '-')) goto fail;
				strcpy(profile->game, value);
			}
			else if (strcmp(key, "ngh") == 0)
			{ bit = 4; if (!number(value, 0, 0xffff, &parsed)) goto fail; profile->ngh = (uint16_t)parsed; }
			else if (strcmp(key, "program_size") == 0)
			{
				bit = 8;
				if (!number(value, 0, WIDE_PROFILE_MAX_PROGRAM, &parsed) || parsed < 0x100 || (parsed & 1)) goto fail;
				profile->program_size = parsed;
			}
			else if (strcmp(key, "program_crc32") == 0)
			{ bit = 16; if (!number(value, 0, UINT32_MAX, &profile->program_crc32)) goto fail; }
			else if (strcmp(key, "status") == 0)
			{
				bit = 32;
				if (strcmp(value, "verified") == 0) profile->status = WIDE_PROFILE_VERIFIED;
				else if (strcmp(value, "experimental") == 0) profile->status = WIDE_PROFILE_EXPERIMENTAL;
				else if (strcmp(value, "draft") == 0) profile->status = WIDE_PROFILE_DRAFT;
				else goto fail;
			}
			else if (strcmp(key, "description") == 0)
			{ bit = 64; if (strlen(value) >= sizeof(profile->description)) goto fail; strcpy(profile->description, value); }
			else if (strcmp(key, "viewport_only") == 0)
			{ bit = 128; if (!number(value, 0, 1, &parsed)) goto fail; profile->viewport_only = parsed != 0; }
			else { failure = "unknown metadata key"; goto fail; }
			if (meta_seen & bit) { failure = "duplicate metadata key"; goto fail; }
			meta_seen |= bit;
		}
		else if (section == 3)
		{
			uint16_t *const targets[LAYER_KEYS] = {
				NULL, &profile->layers.count, &profile->layers.stride,
				&profile->layers.active, &profile->layers.active_mask,
				&profile->layers.x, &profile->layers.width,
				&profile->layers.sprite, &profile->layers.columns, &profile->layers.main
			};
			for (i = 0; i < LAYER_KEYS && strcmp(key, layer_keys[i]) != 0; i++);
			if (i == LAYER_KEYS) { failure = "unknown layers key"; goto fail; }
			bit = 1u << i;
			if (layers_seen & bit) { failure = "duplicate layers key"; goto fail; }
			if (!number(value, 0, i ? 0xffff : 0xffffff, &parsed)) goto fail;
			if (i) *targets[i] = (uint16_t)parsed;
			else profile->layers.address = parsed;
			layers_seen |= bit;
		}
		else
		{
			if (strcmp(key, "offsets") == 0)
			{ bit = 1; patch.offsets_count = numbers(value, 0, WIDE_PROFILE_MAX_PROGRAM, patch.offsets); }
			else if (strcmp(key, "original") == 0)
			{ bit = 2; patch.original_count = numbers(value, 16, 0xffff, patch.original); }
			else if (strcmp(key, "wide") == 0)
			{ bit = 4; patch.wide_count = numbers(value, 16, 0xffff, patch.wide); }
			else if (strcmp(key, "repeat") == 0)
			{ bit = 8; if (!number(value, 0, MAX_LIST, &patch.repeat)) goto fail; }
			else if (strcmp(key, "stride") == 0)
			{ bit = 16; if (!number(value, 0, WIDE_PROFILE_MAX_PROGRAM, &patch.stride) || (patch.stride & 1)) goto fail; }
			else { failure = "unknown patch key"; goto fail; }
			if (patch.seen & bit) { failure = "duplicate patch key"; goto fail; }
			patch.seen |= bit;
		}
	}
	if (line_status < 0) { failure = "invalid ASCII/control character or size limit exceeded"; goto fail; }
	if (ferror(file)) { failure = "read error"; goto fail; }
	if ((meta_seen & 63) != 63) { failure = "missing required metadata"; goto fail; }
	if (section == 2 && !append_section(profile, &patch)) { failure = "invalid final patch section"; goto fail; }
	if (section == 3 && !layers_valid(&profile->layers, layers_seen)) { failure = "invalid layers section"; goto fail; }
	qsort(profile->words, profile->word_count, sizeof(profile->words[0]), compare_words);
	for (i = 0; i < profile->word_count; i++)
	{
		if (i && profile->words[i - 1].offset == profile->words[i].offset)
		{ failure = "overlapping patch/guard words"; goto fail; }
		changed |= profile->words[i].original != profile->words[i].wide;
	}
	if (!changed && !profile->viewport_only) { failure = "no changes; use viewport_only=1 for an explicit probe"; goto fail; }
	if (changed && profile->viewport_only) { failure = "viewport-only profile contains program changes"; goto fail; }
	fclose(file);
	return profile;
fail:
	explain(error, error_size, "line %u: %s", line_no, failure);
	fclose(file);
	free(profile);
	return NULL;
}

static uint32_t program_crc32(const uint16_t *program, size_t byte_length,
	const wide_profile_t *profile)
{
	uint32_t table[256], crc = UINT32_MAX;
	size_t i, next = 0;
	unsigned int bit;
	for (i = 0; i < 256; i++)
	{
		uint32_t entry = (uint32_t)i;
		for (bit = 0; bit < 8; bit++) entry = (entry >> 1) ^ ((entry & 1) ? 0xedb88320u : 0);
		table[i] = entry;
	}
	for (i = WIDE_PROFILE_CRC_START; i < byte_length; i += 2)
	{
		uint16_t word = program[i / 2];
		if (profile && next < profile->word_count && profile->words[next].offset == i)
			word = profile->words[next++].original;
		crc = table[(crc ^ (word >> 8)) & 255] ^ (crc >> 8);
		crc = table[(crc ^ (word & 255)) & 255] ^ (crc >> 8);
	}
	return crc ^ UINT32_MAX;
}

uint32_t wide_program_crc32(const uint16_t *program, size_t byte_length)
{
	if (!program || byte_length < 0x100 || byte_length > WIDE_PROFILE_MAX_PROGRAM ||
		(byte_length & 1)) return 0;
	return program_crc32(program, byte_length, NULL);
}

int wide_profile_check(const wide_profile_t *profile,
	const uint16_t *program, size_t byte_length, char *error, size_t error_size)
{
	size_t i;
	int mode = -1;
	uint32_t crc;
	if (error && error_size) error[0] = '\0';
	if (!profile || !program || byte_length != profile->program_size ||
		byte_length < 0x100 || byte_length > WIDE_PROFILE_MAX_PROGRAM ||
		(byte_length & 1) || profile->word_count > WIDE_PROFILE_MAX_WORDS)
	{ explain(error, error_size, "program size/profile mismatch"); return -1; }
	for (i = 0; i < profile->word_count; i++)
	{
		const wide_patch_word_t *p = &profile->words[i];
		uint16_t actual;
		int word_mode;
		if (p->offset < 0x100 || (p->offset & 1) || p->offset > byte_length - 2 ||
			(i && p->offset <= profile->words[i - 1].offset))
		{ explain(error, error_size, "invalid guard offset"); return -1; }
		actual = program[p->offset / 2];
		if (actual != p->original && actual != p->wide)
		{ explain(error, error_size, "instruction mismatch at 0x%06x", (unsigned int)p->offset); return -1; }
		if (p->original == p->wide) continue;
		word_mode = actual == p->wide;
		if (mode >= 0 && word_mode != mode)
		{ explain(error, error_size, "mixed native/wide instructions at 0x%06x", (unsigned int)p->offset); return -1; }
		mode = word_mode;
	}
	crc = program_crc32(program, byte_length, profile);
	if (crc != profile->program_crc32)
	{ explain(error, error_size, "program CRC32 mismatch: expected %08x, found %08x", (unsigned int)profile->program_crc32, (unsigned int)crc); return -1; }
	return mode < 0 ? 0 : mode;
}

bool wide_profile_apply(const wide_profile_t *profile,
	uint16_t *program, size_t byte_length, bool enable, char *error, size_t error_size)
{
	size_t i;
	if (wide_profile_check(profile, program, byte_length, error, error_size) < 0) return false;
	for (i = 0; i < profile->word_count; i++)
		program[profile->words[i].offset / 2] = enable ? profile->words[i].wide : profile->words[i].original;
	return true;
}

/* Shift that keeps [X+left+s, X+right+s) inside a W-pixel map, preferring
 * no shift; bounded by the extra width beyond the 320-pixel screen. */
static int layer_shift(int x, int width, int left, int right)
{
	const int limit = (right - left - 320) / 2;
	int shift = 0;
	if (shift < -left - x) shift = -left - x;
	if (shift > width - right - x) shift = width - right - x;
	if (shift > limit) shift = limit;
	if (shift < -limit) shift = -limit;
	return shift;
}

void wide_layers_pan(const wide_layers_t *layers,
	uint16_t (*read16)(uint32_t address), int left, int right,
	int8_t *pan, size_t sprites)
{
	uint32_t base = layers->address + (uint32_t)layers->main * layers->stride;
	int main_shift, width;
	unsigned int i;

	memset(pan, 0, sprites);
	width = (int)read16(base + layers->width) * 16;
	if (!(read16(base + layers->active) & layers->active_mask) || width < right - left) return;
	main_shift = layer_shift((int16_t)read16(base + layers->x), width, left, right);
	for (i = 0, base = layers->address; i < layers->count; i++, base += layers->stride)
	{
		size_t first = read16(base + layers->sprite) / 64, last;
		const uint16_t columns = read16(base + layers->columns);
		int shift = main_shift;
		if (!(read16(base + layers->active) & layers->active_mask)) continue;
		/* A map wider than its resident columns is streamed and repeats
		 * modulo its width. Fully resident or narrow maps are scenery
		 * pieces placed in the main world. */
		width = (int)read16(base + layers->width) * 16;
		if (columns * 16 < width && width >= right - left)
			shift = layer_shift((int16_t)read16(base + layers->x), width, left, right);
		last = first + columns;
		for (; first < last && first < sprites; first++)
			pan[first] = (int8_t)-shift;
	}
}
