#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/dip_metadata.h"

#define CHECK(condition) \
	do { if (!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} } while (0)

static int find_mask_row(const dipswitch_t *rows, uint8_t mask, uint8_t value_max)
{
	int i;
	for (i = 0; rows[i].label != NULL && rows[i].label[0] != '\0'; i++)
	{
		if (rows[i].mask == mask && rows[i].value_max == value_max)
			return i;
	}
	return -1;
}

static int write_copy_with_mutation(const char *source, const char *destination,
	size_t truncate_bytes, int mutate)
{
	FILE *in = fopen(source, "rb");
	FILE *out;
	long size;
	unsigned char *data;
	if (in == NULL)
		return 0;
	if (fseek(in, 0, SEEK_END) != 0 || (size = ftell(in)) <= 0 || fseek(in, 0, SEEK_SET) != 0)
	{
		fclose(in);
		return 0;
	}
	if ((size_t)size <= truncate_bytes)
	{
		fclose(in);
		return 0;
	}
	data = (unsigned char *)malloc((size_t)size);
	if (data == NULL)
	{
		fclose(in);
		return 0;
	}
	if (fread(data, 1, (size_t)size, in) != (size_t)size || fclose(in) != 0)
	{
		free(data);
		return 0;
	}
	if (mutate)
		data[48] ^= 0x01;
	out = fopen(destination, "wb");
	if (out == NULL)
	{
		free(data);
		return 0;
	}
	if (fwrite(data, 1, (size_t)size - truncate_bytes, out) != (size_t)size - truncate_bytes
		|| fclose(out) != 0)
	{
		free(data);
		return 0;
	}
	free(data);
	return 1;
}

int main(int argc, char **argv)
{
	dip_metadata_t metadata = {0};
	dip_metadata_error_t error;
	dipswitch_t *rows;
	char corrupt_path[1024];
	char truncated_path[1024];
	int row;

	CHECK(argc == 2);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "forgottn", DIP_METADATA_LANG_ENGLISH);
	CHECK(error == DIP_METADATA_OK);
	rows = dip_metadata_active(&metadata);
	CHECK(rows != NULL);
	CHECK(strcmp(rows[0].label, "Coin A") == 0);
	CHECK(rows[0].value_max == 7);
	CHECK(strcmp(rows[0].values_label[3], "1 Coins/1 Credit") == 0);
	dip_metadata_unload(&metadata);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "forgottn", DIP_METADATA_LANG_CHINESE_SIMPLIFIED);
	CHECK(error == DIP_METADATA_OK);
	rows = dip_metadata_active(&metadata);
	CHECK(rows != NULL);
	CHECK(strcmp(rows[0].label, "投币A") == 0);
	dip_metadata_unload(&metadata);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "msword", DIP_METADATA_LANG_ENGLISH);
	CHECK(error == DIP_METADATA_OK);
	rows = dip_metadata_active(&metadata);
	CHECK(rows != NULL);
	row = find_mask_row(rows, 0x03, 3);
	CHECK(row >= 0);
	CHECK(strcmp(rows[row].values_label[3], "4 (3 when continue") == 0);
	dip_metadata_unload(&metadata);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "does-not-exist", DIP_METADATA_LANG_ENGLISH);
	CHECK(error == DIP_METADATA_ERROR_PROFILE);

	snprintf(corrupt_path, sizeof(corrupt_path), "%s.corrupt", argv[1]);
	snprintf(truncated_path, sizeof(truncated_path), "%s.truncated", argv[1]);
	CHECK(write_copy_with_mutation(argv[1], corrupt_path, 0, 1));
	CHECK(write_copy_with_mutation(argv[1], truncated_path, 7, 0));

	error = dip_metadata_load_profile(
		&metadata, corrupt_path, "forgottn", DIP_METADATA_LANG_ENGLISH);
	CHECK(error == DIP_METADATA_ERROR_CHECKSUM);
	error = dip_metadata_load_profile(
		&metadata, truncated_path, "forgottn", DIP_METADATA_LANG_ENGLISH);
	CHECK(error != DIP_METADATA_OK);

	remove(corrupt_path);
	remove(truncated_path);
	return 0;
}
