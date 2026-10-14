#ifndef NJEMU_CACHE_FORMAT_VERSION_H
#define NJEMU_CACHE_FORMAT_VERSION_H

/* On-disk processed/cache asset format, deliberately independent of
 * NJEMU's application SemVer. Existing V24 assets remain compatible. */
#define NJEMU_CACHE_FORMAT_VERSION "V24"
#define NJEMU_CPS2_CACHE_SIGNATURE "CPS2" NJEMU_CACHE_FORMAT_VERSION
#define NJEMU_MVS_CACHE_SIGNATURE "MVS_" NJEMU_CACHE_FORMAT_VERSION

#endif /* NJEMU_CACHE_FORMAT_VERSION_H */
