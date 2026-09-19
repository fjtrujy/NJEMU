#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <zfile.h>
#include <zlib.h>

#include "unzip.h"
#include "zip.h"


#define ZIP_NOTOPEN		0
#define ZIP_READOPEN	1
#define ZIP_WRITEOPEN	2

static zipFile zipfile = NULL;

static char basedir[PATH_MAX];
static char *basedirend;
static char zip_cache[4096];
static int  zip_cached_len;
static int  zip_filepos;
static int  zip_mode = ZIP_NOTOPEN;
static int64_t zip_fd = -1;


int64_t zip_open(const char *path, const char *mode)
{
	if (zipfile) zip_close();

	if (!strcmp(mode, "rb"))
	{
		zip_mode = ZIP_READOPEN;

		if ((zipfile = unzOpen(path)) != NULL)
			return (int64_t)zipfile;

		strcpy(basedir, path);

		basedirend = basedir + strlen(basedir);
		*basedirend++ = '/';
	}
	else if (!strcmp(mode, "wb"))
	{
		if ((zipfile = zipOpen(path, 0)) != NULL)
		{
			zip_mode = ZIP_WRITEOPEN;
			return (int64_t)zipfile;
		}
	}

	return -1;
}


void zip_close(void)
{
	if (zip_fd != -1) zclose(zip_fd);

	if (zipfile)
	{
		if (zip_mode == ZIP_READOPEN)
			unzClose(zipfile);
		else
			zipClose(zipfile, NULL);
		zipfile = NULL;
	}

	zip_mode = ZIP_NOTOPEN;
}


int zip_findfirst(struct zip_find_t *file)
{
	if (zipfile && zip_mode == ZIP_READOPEN)
	{
		if (unzGoToFirstFile(zipfile) == UNZ_OK)
		{
			unz_file_info info;

			unzGetCurrentFileInfo(zipfile, &info, file->name, PATH_MAX);
			file->length = info.uncompressed_size;
			file->crc32 = info.crc;
			return 1;
		}
	}
	return 0;
}


int zip_findnext(struct zip_find_t *file)
{
	if (zipfile && zip_mode == ZIP_READOPEN)
	{
		if (unzGoToNextFile(zipfile) == UNZ_OK)
		{
			unz_file_info info;

			unzGetCurrentFileInfo(zipfile, &info, file->name, PATH_MAX);
			file->length = info.uncompressed_size;
			file->crc32 = info.crc;
			return 1;
		}
	}
	return 0;
}


int64_t zopen(const char *filename)
{
	zip_cached_len = 0;

	if (zip_mode == ZIP_READOPEN)
	{
		if (!zipfile)
		{
			int fd_plain;

			strcpy(basedirend, filename);
			fd_plain = open(basedir, O_RDONLY);
			if (fd_plain >= 0)
			{
				zip_fd = (int64_t) fd_plain;
				return zip_fd;
			}
			return -1;
		}

		if (unzLocateFile(zipfile, filename) == UNZ_OK)
			if (unzOpenCurrentFile(zipfile) == UNZ_OK)
				return (int64_t)zipfile;
	}
	else
	{
		zip_fileinfo zip_info;
		struct tm *nowtime;
		time_t ltime;

		memset(&zip_info, 0, sizeof(zip_info));

		time(&ltime);
		nowtime = localtime(&ltime);

		zip_info.tmz_date.tm_sec  = nowtime->tm_sec;
		zip_info.tmz_date.tm_min  = nowtime->tm_min;
		zip_info.tmz_date.tm_hour = nowtime->tm_hour;
		zip_info.tmz_date.tm_mday = nowtime->tm_mday;
		zip_info.tmz_date.tm_mon  = nowtime->tm_mon ;
		zip_info.tmz_date.tm_year = nowtime->tm_year;

		if (zipOpenNewFileInZip(zipfile, filename, &zip_info, NULL, 0, NULL, 0, NULL, Z_DEFLATED, 9) == ZIP_OK)
			return (int64_t)zipfile;
	}
	return -1;
}


int zclose(int64_t fd)
{
	zip_cached_len = 0;

	if (zip_mode == ZIP_READOPEN)
	{
		if (!zipfile)
		{
			if (fd != -1) close((int)fd);
			zip_fd = -1;
			return 0;
		}
		return unzCloseCurrentFile(zipfile);
	}
	else
	{
		return zipCloseFileInZip(zipfile);
	}
}


size_t zread(int64_t fd, void *buf, unsigned size)
{
	if (zip_mode == ZIP_READOPEN)
	{
		if (!zipfile)
			return read((int)fd, buf, size);
		else
			return unzReadCurrentFile(zipfile, buf, size);
	}
	return 0;
}


int zwrite(int64_t fd, void *buf, unsigned size)
{
	if (zip_mode == ZIP_WRITEOPEN)
	{
		return zipWriteInFileInZip(zipfile, buf, size);
	}
	return 0;
}


int zgetc(int64_t fd)
{
	if (zip_mode == ZIP_READOPEN)
	{
		if (!zipfile)
		{
			unsigned char c;
			if (read((int)fd, &c, 1) == 1) return (int)c;
			return -1;
		}

		if (zip_cached_len == 0)
		{
			zip_cached_len = unzReadCurrentFile(zipfile, zip_cache, 4096);
			if (zip_cached_len == 0) return -1;
			zip_filepos = 0;
		}
		zip_cached_len--;
		return zip_cache[zip_filepos++] & 0xff;
	}
	return -1;
}


size_t zsize(int64_t fd)
{
	if (zip_mode == ZIP_READOPEN)
	{
		unz_file_info info;

		if (zipfile == NULL)
		{
			int fd_plain = (int)fd;
			off_t pos = lseek(fd_plain, 0, SEEK_CUR);
			off_t len = lseek(fd_plain, 0, SEEK_END);

			lseek(fd_plain, pos, SEEK_SET);

			return (size_t)len;
		}

		unzGetCurrentFileInfo(zipfile, &info, NULL, 0);

		return info.uncompressed_size;
	}
	return 0;
}


int zcrc(int64_t fd)
{
	if (zipfile && zip_mode == ZIP_READOPEN)
	{
		unz_file_info info;

		unzGetCurrentFileInfo(zipfile, &info, NULL, 0);

		return info.crc;
	}
	return 0;
}
