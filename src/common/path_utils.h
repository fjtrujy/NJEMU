#ifndef COMMON_PATH_UTILS_H
#define COMMON_PATH_UTILS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static inline int path_format(char *dst, size_t dst_size, const char *fmt, ...)
{
	va_list args;
	int written;

	va_start(args, fmt);
	written = vsnprintf(dst, dst_size, fmt, args);
	va_end(args);

	if (written < 0 || (size_t)written >= dst_size)
	{
		if (dst_size != 0)
			dst[0] = '\0';
		return 0;
	}
	return 1;
}

#endif
