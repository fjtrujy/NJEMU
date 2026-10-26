#define NEWLIB_PORT_AWARE 1

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <debug.h>
#include <fileXio_rpc.h>
#include <iox_stat.h>
#include <elf-loader.h>
#include <ps2_fileXio_driver.h>
#include <ps2_dev9_driver.h>
#include <ps2_hdd_driver.h>
#include <ps2_poweroff_driver.h>

#ifndef NJEMU_HDD_BOOT_TARGET
#error "NJEMU_HDD_BOOT_TARGET must identify the companion executable"
#endif

#define NJEMU_HDD_BOOT_STRINGIFY_IMPL(name) #name
#define NJEMU_HDD_BOOT_STRINGIFY(name) NJEMU_HDD_BOOT_STRINGIFY_IMPL(name)

/* Provided by the installed PS2SDK ELF-loader library, including SDKs whose
 * public header does not yet declare its no-IOP-reset variant. */
int LoadELFFromFileWithPartitionNoReset(
    const char *filename, const char *partition, int argc, char *argv[]);

/* The SDK ELF loader calls newlib stat(), which may not be backed by fileXio
 * for a PFS mount established by a standalone loader. Limit this adapter to
 * PFS paths: all other filesystem behavior remains owned by PS2SDK. */
int __real_stat(const char *path, struct stat *buffer);
int __wrap_stat(const char *path, struct stat *buffer)
{
    iox_stat_t fs_stat;

    if (strncmp(path, "pfs:", 4) != 0 &&
        strncmp(path, "pfs0:", 5) != 0)
        return __real_stat(path, buffer);

    if (fileXioGetStat(path, &fs_stat) < 0) {
        errno = ENOENT;
        return -1;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->st_mode = S_IFREG | 0444;
    buffer->st_size = (off_t)fs_stat.size;
    return 0;
}

static bool find_hdd_partition(
    int argc, char *argv[], char *partition, size_t capacity)
{
    int i;

    for (i = 0; i < argc; ++i) {
        const char *path = argv[i];
        const char *delimiter;
        size_t length;

        if (path == NULL ||
            (strncmp(path, "hdd0:", 5) != 0 &&
             strncmp(path, "hdd:", 4) != 0))
            continue;

        delimiter = strstr(path, ":pfs:");
        if (delimiter == NULL)
            continue;
        length = (size_t)(delimiter - path);
        if (length == 0 || length >= capacity)
            continue;
        memcpy(partition, path, length);
        partition[length] = '\0';
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    char cwd[FILENAME_MAX];
    char partition[64];
    char target[FILENAME_MAX];
    char launch_arg[FILENAME_MAX + sizeof(partition) + 2];
    char *child_argv[1];
    size_t directory_length;
    int result;

    init_scr();
    if (getcwd(cwd, sizeof(cwd)) == NULL ||
        strncmp(cwd, "pfs:", 4) != 0) {
        scr_printf("BOOT_HDD.ELF must be launched from a mounted PFS partition.\n");
        return 1;
    }
    if (!find_hdd_partition(argc, argv, partition, sizeof(partition))) {
        scr_printf("Missing HDD partition information in launcher arguments.\n");
        return 2;
    }

    directory_length = strlen(cwd);
    if (snprintf(target, sizeof(target), "%s%s%s", cwd,
                 directory_length && cwd[directory_length - 1] == '/' ? "" : "/",
                 NJEMU_HDD_BOOT_STRINGIFY(NJEMU_HDD_BOOT_TARGET)) >=
                 (int)sizeof(target) ||
        snprintf(launch_arg, sizeof(launch_arg), "%s:%s",
                 partition, target) >= (int)sizeof(launch_arg)) {
        scr_printf("HDD target path is too long.\n");
        return 3;
    }

    SifInitRpc(0);
    while (!SifIopReset(NULL, 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();

    if (init_fileXio_driver() < 0 ||
        init_poweroff_driver() < 0 ||
        init_dev9_driver() < 0 ||
        init_hdd_driver(false, false) != HDD_INIT_STATUS_IRX_OK) {
        scr_printf("Failed to initialize HDD drivers.\n");
        return 4;
    }
    result = mount_hdd_partition("pfs:", partition);
    if (result != HDD_MOUNT_STATUS_OK ||
        chdir(cwd) != 0) {
        scr_printf("Failed to restore PFS partition (status %d).\n", result);
        return 5;
    }

    /* The executable's first argument carries the original HDD partition.
     * NJEMU preserves it before its own IOP reset to remount this PFS path. */
    child_argv[0] = launch_arg;
    printf("[hdd-bootstrap] loading %s from %s\n", target, partition);
    result = LoadELFFromFileWithPartitionNoReset(target, NULL, 1, child_argv);
    scr_printf("Failed to launch HDD executable (status %d).\n", result);
    return 6;
}
