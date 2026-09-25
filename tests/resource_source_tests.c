#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <miniz.h>

#include "ncdz/resource_source.h"

#ifndef TEST_RESOURCE_ROOT
#define TEST_RESOURCE_ROOT "."
#endif

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(data, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void create_zip(const char *path, const char *name,
                       const void *data, size_t size)
{
    mz_zip_archive writer;

    remove(path);
    memset(&writer, 0, sizeof(writer));
    assert(mz_zip_writer_init_file(&writer, path, 0));
    assert(mz_zip_writer_add_mem(&writer, name, data, size, MZ_BEST_SPEED));
    assert(mz_zip_writer_finalize_archive(&writer));
    assert(mz_zip_writer_end(&writer));
}

int main(void)
{
    static const char payload[] = "NCDZ resource source";
    char directory[PATH_MAX];
    char file_path[PATH_MAX];
    char zip_path[PATH_MAX];
    resource_source_t directory_source = {0};
    resource_source_t zip_source = {0};
    resource_source_t invalid_source = {0};
    resource_file_info_t info;
    resource_file_t file = {0};
    char buffer[sizeof(payload)] = {0};

    assert(snprintf(directory, sizeof(directory), "%s/resource_source_dir",
                    TEST_RESOURCE_ROOT) > 0);
    assert(snprintf(file_path, sizeof(file_path), "%s/IPL.TXT", directory) > 0);
    assert(snprintf(zip_path, sizeof(zip_path), "%s/resource_source.zip",
                    TEST_RESOURCE_ROOT) > 0);

    if (mkdir(directory, 0777) != 0)
        assert(errno == EEXIST);
    write_file(file_path, payload, sizeof(payload) - 1);
    create_zip(zip_path, "IPL.TXT", payload, sizeof(payload) - 1);

    assert(resource_source_open_directory(&directory_source, directory));
    assert(directory_source.type == RESOURCE_SOURCE_DIRECTORY);
    assert(resource_source_stat(&directory_source, "IPL.TXT", &info));
    assert(info.size == sizeof(payload) - 1);
    assert(resource_file_open(&directory_source, "IPL.TXT", &file));
    assert(resource_file_read(&file, buffer, sizeof(buffer)) == sizeof(payload) - 1);
    assert(memcmp(buffer, payload, sizeof(payload) - 1) == 0);
    assert(resource_file_close(&file));
    resource_source_close(&directory_source);

    memset(buffer, 0, sizeof(buffer));
    assert(resource_source_open_zip(&zip_source, zip_path));
    assert(zip_source.type == RESOURCE_SOURCE_ZIP);
    assert(resource_source_stat(&zip_source, "ipl.txt", &info));
    assert(info.size == sizeof(payload) - 1);
    assert(resource_file_open(&zip_source, "ipl.txt", &file));
    assert(resource_file_read(&file, buffer, sizeof(buffer)) == sizeof(payload) - 1);
    assert(memcmp(buffer, payload, sizeof(payload) - 1) == 0);
    assert(resource_file_close(&file));
    resource_source_close(&zip_source);

    /* Backend selection is explicit: neither operation falls back to the other. */
    assert(!resource_source_open_zip(&invalid_source, directory));
    assert(invalid_source.type == RESOURCE_SOURCE_NONE);
    assert(!resource_source_open_directory(&invalid_source, zip_path));
    assert(invalid_source.type == RESOURCE_SOURCE_NONE);

    remove(file_path);
    rmdir(directory);
    remove(zip_path);
    return 0;
}
