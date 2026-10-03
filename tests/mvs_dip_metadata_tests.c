#include <stdio.h>
#include <string.h>
#include "common/dip_metadata.h"

#define CHECK(condition) \
	do { if (!(condition)) { \
		fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} } while (0)

int main(int argc, char **argv)
{
	dip_metadata_t metadata = {0};
	dip_metadata_error_t error;
	dipswitch_t *rows;

	CHECK(argc == 2);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "default", DIP_METADATA_LANG_ENGLISH);
	CHECK(error == DIP_METADATA_OK);
	rows = dip_metadata_active(&metadata);
	CHECK(rows != NULL);
	CHECK(strcmp(rows[0].label, "Test Switch") == 0);
	CHECK(rows[3].mask == 0x38);
	CHECK(rows[3].value_max == 4);
	dip_metadata_unload(&metadata);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "mjneogeo", DIP_METADATA_LANG_CHINESE_SIMPLIFIED);
	CHECK(error == DIP_METADATA_OK);
	rows = dip_metadata_active(&metadata);
	CHECK(rows != NULL);
	CHECK(rows[2].mask == 0x04);
	CHECK(rows[2].enable == 0);
	dip_metadata_unload(&metadata);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "pcb", DIP_METADATA_LANG_ENGLISH);
	CHECK(error == DIP_METADATA_OK);
	rows = dip_metadata_active(&metadata);
	CHECK(rows != NULL);
	CHECK(rows[6].mask == 0x01);
	CHECK(strcmp(rows[6].label, "Hard Dip 3 (Region)") == 0);
	dip_metadata_unload(&metadata);

	error = dip_metadata_load_profile(
		&metadata, argv[1], "kog", DIP_METADATA_LANG_JAPANESE);
	CHECK(error == DIP_METADATA_OK);
	rows = dip_metadata_active(&metadata);
	CHECK(rows != NULL);
	CHECK(strcmp(rows[2].label, "Autofire (in some games)") == 0);
	CHECK(rows[2].enable == 1);
	dip_metadata_unload(&metadata);

	return 0;
}
