/******************************************************************************
 *
 *    zip_archive_internal.h
 *
 *    Internal helpers used only by the transitional zfile compatibility layer
 *
 ******************************************************************************/

#ifndef ZIP_ARCHIVE_INTERNAL_H
#define ZIP_ARCHIVE_INTERNAL_H

#include "zip/zip_archive.h"

bool zip_archive_stat_index(zip_archive_t *archive,
                            size_t index,
                            zip_entry_info_t *info);

size_t zip_archive_entry_count(zip_archive_t *archive);

#endif /* ZIP_ARCHIVE_INTERNAL_H */
