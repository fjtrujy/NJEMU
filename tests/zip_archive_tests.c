#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <miniz.h>

#include "common/zip_archive.h"

#ifndef TEST_ZIP_ARCHIVE_PATH
#define TEST_ZIP_ARCHIVE_PATH "zip_archive_tests.zip"
#endif

#ifndef TEST_ZIP_DIRECTORY
#define TEST_ZIP_DIRECTORY "."
#endif

static void create_test_archive(const char *path)
{
    static const char alpha[] = "Hello from Alpha";
    static const unsigned char beta[] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
    mz_zip_archive writer;

    remove(path);
    memset(&writer, 0, sizeof(writer));
    assert(mz_zip_writer_init_file(&writer, path, 0));
    assert(mz_zip_writer_add_mem(&writer,
                                 "Alpha.TXT",
                                 alpha,
                                 sizeof(alpha) - 1,
                                 MZ_BEST_SPEED));
    assert(mz_zip_writer_add_mem(&writer,
                                 "beta.bin",
                                 beta,
                                 sizeof(beta),
                                 MZ_BEST_SPEED));
    assert(mz_zip_writer_finalize_archive(&writer));
    assert(mz_zip_writer_end(&writer));
}

int main(void)
{
    static const char alpha[] = "Hello from Alpha";
    static const unsigned char beta[] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
    const uint32_t alpha_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT,
                                                  (const unsigned char *)alpha,
                                                  sizeof(alpha) - 1);
    const uint32_t beta_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT,
                                                 beta,
                                                 sizeof(beta));
    zip_archive_t archive = { 0 };
    zip_archive_t directory_archive = { 0 };
    zip_entry_info_t info;
    zip_entry_t alpha_entry = { 0 };
    zip_entry_t beta_entry = { 0 };
    unsigned char buffer[64];
    size_t length;
    size_t i;

    create_test_archive(TEST_ZIP_ARCHIVE_PATH);

    assert(!zip_archive_open(&directory_archive, TEST_ZIP_DIRECTORY));
    assert(zip_archive_open(&archive, TEST_ZIP_ARCHIVE_PATH));
    assert(!zip_archive_open(&archive, TEST_ZIP_ARCHIVE_PATH));

    /* Filename lookup remains case-insensitive and metadata-only. */
    assert(zip_archive_stat(&archive, "alpha.txt", &info));
    assert(strcmp(info.name, "Alpha.TXT") == 0);
    assert(info.size == sizeof(alpha) - 1);
    assert(info.crc32 == alpha_crc);

    assert(zip_archive_find_crc(&archive, beta_crc, &info));
    assert(strcmp(info.name, "beta.bin") == 0);
    assert(info.size == sizeof(beta));
    assert(!zip_archive_find_crc(&archive, 0x12345678U, &info));

    /* Two entries may be open at once; opening one must not close the other. */
    assert(!zip_entry_is_open(&alpha_entry));
    assert(zip_entry_open(&archive, "ALPHA.TXT", &alpha_entry));
    assert(zip_entry_is_open(&alpha_entry));
    assert(!zip_entry_open(&archive, "beta.bin", &alpha_entry));
    assert(zip_entry_open(&archive, "beta.bin", &beta_entry));
    assert(zip_entry_read(&alpha_entry, buffer, 5) == 5);
    assert(memcmp(buffer, alpha, 5) == 0);
    assert(zip_entry_read(&beta_entry, buffer, 3) == 3);
    assert(memcmp(buffer, beta, 3) == 0);
    assert(zip_entry_close(&alpha_entry));
    assert(!zip_entry_is_open(&alpha_entry));
    assert(zip_entry_close(&beta_entry));

    /* Sequential reuse has no hidden iterator or byte-cache state. */
    assert(zip_entry_open(&archive, "beta.bin", &beta_entry));
    for (i = 0; i < sizeof(beta); ++i)
        assert(zip_entry_getc(&beta_entry) == beta[i]);
    assert(zip_entry_getc(&beta_entry) == EOF);
    assert(zip_entry_close(&beta_entry));

    assert(zip_entry_open(&archive, "Alpha.TXT", &alpha_entry));
    memset(buffer, 0, sizeof(buffer));
    length = zip_entry_read(&alpha_entry, buffer, sizeof(buffer));
    assert(length == sizeof(alpha) - 1);
    assert(memcmp(buffer, alpha, length) == 0);
    assert(zip_entry_read(&alpha_entry, buffer, sizeof(buffer)) == 0);
    assert(zip_entry_close(&alpha_entry));

    zip_archive_close(&archive);
    remove(TEST_ZIP_ARCHIVE_PATH);
    return 0;
}
