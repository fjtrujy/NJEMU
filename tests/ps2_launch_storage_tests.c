#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

#include "ps2/ps2_launch_storage.h"

static void test_marker_takes_precedence(void)
{
	const char *argv[] = {"mass1:/NJEMU/MVS/MVS", "--ps2-drivers-ata-bdm"};
	const char *marker = "--ps2-drivers-ata-bdm";

	assert(ps2_launch_storage_detect(2, argv, "mass1:/NJEMU/MVS", marker) ==
	       PS2_LAUNCH_STORAGE_ATA_BDM);
	assert(ps2_launch_storage_detect(2, argv, "pfs0:/NJEMU/MVS", marker) ==
	       PS2_LAUNCH_STORAGE_ATA_BDM);
	assert(ps2_launch_storage_detect(2, argv, NULL, marker) ==
	       PS2_LAUNCH_STORAGE_ATA_BDM);
}

static void test_non_ata_launches(void)
{
	const char *argv[] = {"mass0:/NJEMU/MVS/MVS", "--other-option"};
	const char *marker = "--ps2-drivers-ata-bdm";

	assert(ps2_launch_storage_detect(2, argv, "mass0:/NJEMU/MVS", marker) ==
	       PS2_LAUNCH_STORAGE_DEFAULT);
	assert(ps2_launch_storage_detect(2, argv, "mass1:/NJEMU/CPS2", marker) ==
	       PS2_LAUNCH_STORAGE_DEFAULT);
	assert(ps2_launch_storage_detect(2, argv, "pfs:/NJEMU/MVS", marker) ==
	       PS2_LAUNCH_STORAGE_APA_PFS);
	assert(ps2_launch_storage_detect(2, argv, "pfs0:/NJEMU/CPS2", marker) ==
	       PS2_LAUNCH_STORAGE_APA_PFS);
	assert(ps2_launch_storage_detect(2, argv, "pfs1:/NJEMU/MVS", marker) ==
	       PS2_LAUNCH_STORAGE_DEFAULT);
	assert(ps2_launch_storage_detect(2, argv, NULL, marker) ==
	       PS2_LAUNCH_STORAGE_DEFAULT);
	assert(ps2_launch_storage_detect(0, NULL, "mass0:/", marker) ==
	       PS2_LAUNCH_STORAGE_DEFAULT);
}

int main(void)
{
	test_marker_takes_precedence();
	test_non_ata_launches();
	puts("PS2 launch storage tests passed");
	return 0;
}
