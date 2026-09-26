#ifndef ZFILE_H
#define ZFILE_H

#include <stdint.h>

int64_t zip_open(const char *path, const char *mode);
void zip_close(void);

int64_t zopen(const char *filename);
int zwrite(int64_t fd, void *buf, unsigned size);
int zclose(int64_t fd);

#endif /* ZFILE_H */
