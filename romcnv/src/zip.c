/* zip.c -- IO on .zip files using zlib
   Version 0.15 beta, Mar 19th, 1998,

   Read zip.h for more info
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <errno.h>

#include "zip.h"

/* compile with -Dstatic if your debugger can't find static symbols */

#ifndef VERSIONMADEBY
# define VERSIONMADEBY   (0x0) /* platform depedent */
#endif

#ifndef Z_BUFSIZE
#define Z_BUFSIZE (16384)
#endif

#ifndef Z_MAXFILENAMEINZIP
#define Z_MAXFILENAMEINZIP (256)
#endif

#ifndef ALLOC
# define ALLOC(size) (malloc(size))
#endif
#ifndef TRYFREE
# define TRYFREE(p) {if (p) free(p);}
#endif

/*
#define SIZECENTRALDIRITEM (0x2e)
#define SIZEZIPLOCALHEADER (0x1e)
*/

/* I've found an old Unix (a SunOS 4.1.3_U1) without all SEEK_* defined.... */

#ifndef SEEK_CUR
#define SEEK_CUR	1
#endif

#ifndef SEEK_END
#define SEEK_END	2
#endif

#ifndef SEEK_SET
#define SEEK_SET	0
#endif

const char zip_copyright[] = " zip 0.15 Copyright 1998 Gilles Vollant ";


#define SIZEDATA_INDATABLOCK (4096-(4*4))

#define LOCALHEADERMAGIC	(0x04034b50)
#define CENTRALHEADERMAGIC  (0x02014b50)
#define ENDHEADERMAGIC	  (0x06054b50)

#define FLAG_LOCALHEADER_OFFSET (0x06)
#define CRC_LOCALHEADER_OFFSET  (0x0e)

#define SIZECENTRALHEADER (0x2e) /* 46 */

typedef struct linkedlist_datablock_internal_s
{
	struct linkedlist_datablock_internal_s* next_datablock;
	uLong  avail_in_this_block;
	uLong  filled_in_this_block;
	uLong  unused; /* for future use and alignement */
	unsigned char data[SIZEDATA_INDATABLOCK];
} linkedlist_datablock_internal;

typedef struct linkedlist_data_s
{
	linkedlist_datablock_internal* first_block;
	linkedlist_datablock_internal* last_block;
} linkedlist_data;


typedef struct
{
	z_stream stream;			/* zLib stream structure for inflate */
	int  stream_initialised;	/* 1 is stream is initialised */
	uInt pos_in_buffered_data;  /* last written byte in buffered_data */

	uLong pos_static_header;	 /* offset of the static header of the file
									 currenty writing */
	char* central_header;	   /* central header data for the current file */
	uLong size_centralheader;   /* size of the central header for cur file */
	uLong flag;				 /* flag of the file currently writing */

	int  method;				/* compression method of file currenty wr.*/
	Byte buffered_data[Z_BUFSIZE];/* buffer contain compressed data to be writ*/
	uLong dosDate;
	uLong crc32;
} curfile_info;

typedef struct
{
	FILE * filezip;
	linkedlist_data central_dir;/* datablock with central dir in construction*/
	int  in_opened_file_inzip;  /* 1 if a file in the zip is currently writ.*/
	curfile_info ci;			/* info on the file curretly writing */

	uLong begin_pos;			/* position of the beginning of the zipfile */
	uLong number_entry;
} zip_internal;


static linkedlist_datablock_internal *allocate_new_datablock(void)
{
	linkedlist_datablock_internal *ldi;

	ldi = (linkedlist_datablock_internal *)ALLOC(sizeof(linkedlist_datablock_internal));
	if (ldi)
	{
		ldi->next_datablock = NULL;
		ldi->filled_in_this_block = 0;
		ldi->avail_in_this_block = SIZEDATA_INDATABLOCK;
	}
	return ldi;
}

static void free_datablock(linkedlist_datablock_internal* ldi)
{
	while (ldi)
	{
		linkedlist_datablock_internal *ldinext = ldi->next_datablock;
		TRYFREE(ldi);
		ldi = ldinext;
	}
}

static void init_linkedlist(linkedlist_data *ll)
{
	ll->first_block = ll->last_block = NULL;
}


#if 0
static void free_linkedlist(linkedlist_data *ll)
{
	free_datablock(ll->first_block);
	ll->first_block = ll->last_block = NULL;
}
#endif


static int add_data_in_datablock(linkedlist_data *ll, const void *buf, uLong len)
{
	linkedlist_datablock_internal *ldi;
	const unsigned char *from_copy;

	if (!ll) return ZIP_INTERNALERROR;

	if (!ll->last_block)
	{
		ll->first_block = ll->last_block = allocate_new_datablock();
		if (!ll->first_block) return ZIP_INTERNALERROR;
	}

	ldi = ll->last_block;
	from_copy = (unsigned char *)buf;

	while (len > 0)
	{
		uInt copy_this;
		uInt i;
		unsigned char *to_copy;

		if (!ldi->avail_in_this_block)
		{
			ldi->next_datablock = allocate_new_datablock();
			if (!ldi->next_datablock) return ZIP_INTERNALERROR;

			ldi = ldi->next_datablock;
			ll->last_block = ldi;
		}

		if (ldi->avail_in_this_block < len)
			copy_this = (uInt)ldi->avail_in_this_block;
		else
			copy_this = (uInt)len;

		to_copy = &(ldi->data[ldi->filled_in_this_block]);

		for (i = 0; i < copy_this; i++)
			*(to_copy + i) = *(from_copy + i);

		ldi->filled_in_this_block += copy_this;
		ldi->avail_in_this_block -= copy_this;
		from_copy += copy_this;
		len -= copy_this;
	}
	return ZIP_OK;
}


#if 0
static int write_datablock(FILE *fout, linkedlist_data *ll)
{
	linkedlist_datablock_internal *ldi;

	ldi = ll->first_block;
	while (!ldi)
	{
		if (ldi->filled_in_this_block > 0)
			if (fwrite(ldi->data,(uInt)ldi->filled_in_this_block, 1, fout) != 1)
				return ZIP_ERRNO;

		ldi = ldi->next_datablock;
	}
	return ZIP_OK;
}
#endif


/****************************************************************************/

/* ===========================================================================
   Outputs a long in LSB order to the given file
   nbByte == 1, 2 or 4 (byte, short or long)
*/

static int zipstatic_putValue(FILE *file, uLong x, int nbByte)
{
	unsigned char buf[4];
	int n;
	for (n = 0; n < nbByte; n++) {
		buf[n] = (unsigned char)(x & 0xff);
		x >>= 8;
	}
	if (fwrite(buf,nbByte,1,file)!=1)
		return ZIP_ERRNO;
	else
		return ZIP_OK;
}

static void zipstatic_putValue_inmemory(void* dest, uLong x, int nbByte)
{
	unsigned char* buf=(unsigned char*)dest;
	int n;
	for (n = 0; n < nbByte; n++) {
		buf[n] = (unsigned char)(x & 0xff);
		x >>= 8;
	}
}
/****************************************************************************/


static uLong zipstatic_TmzDateToDosDate(const tm_zip *ptm)
{
	uLong year = (uLong)ptm->tm_year;
	uLong dosdate;

	if (year > 1980)
		year -= 1980;
	else if (year > 80)
		year -= 80;

	dosdate = (uLong)(((ptm->tm_mday) + (32 * (ptm->tm_mon+1)) + (512 * year)) << 16);
	return dosdate | ((ptm->tm_sec/2) + (32* ptm->tm_min) + (2048 * (uLong)ptm->tm_hour));
}


/****************************************************************************/

zipFile zipOpen(const char *pathname, int append)
{
	zip_internal ziinit;
	zip_internal *zi;

	ziinit.filezip = fopen(pathname, (append == 0) ? "wb" : "ab");
	if (!ziinit.filezip) return NULL;

	ziinit.begin_pos = ftell(ziinit.filezip);
	ziinit.in_opened_file_inzip = 0;
	ziinit.ci.stream_initialised = 0;
	ziinit.number_entry = 0;
	init_linkedlist(&(ziinit.central_dir));

	zi = (zip_internal*)ALLOC(sizeof(zip_internal));
	if (!zi)
	{
		fclose(ziinit.filezip);
		return NULL;
	}

	*zi = ziinit;

	return (zipFile)zi;
}


int zipOpenNewFileInZip(
	zipFile file,
	const char *filename,
	const zip_fileinfo *zipfi,
	const void *extrafield_static,
	uInt size_extrafield_static,
	const void *extrafield_global,
	uInt size_extrafield_global,
	const char *comment,
	int method,
	int level)
{
	zip_internal *zi;
	uInt size_filename;
	uInt size_comment;
	uInt i;
	int err = ZIP_OK;

	if (!file) return ZIP_PARAMERROR;
	if (method && method != Z_DEFLATED) return ZIP_PARAMERROR;

	zi = (zip_internal *)file;

	if (zi->in_opened_file_inzip == 1)
	{
		err = zipCloseFileInZip(file);
		if (err != ZIP_OK)
			return err;
	}

	if (!filename) filename = "-";

	if (!comment)
		size_comment = 0;
	else
		size_comment = strlen(comment);

	size_filename = strlen(filename);

	if (!zipfi)
	{
		zi->ci.dosDate = 0;
	}
	else
	{
		if (zipfi->dosDate)
			zi->ci.dosDate = zipfi->dosDate;
		else
			zi->ci.dosDate = zipstatic_TmzDateToDosDate(&zipfi->tmz_date);
	}

	switch (level)
	{
	case 1: zi->ci.flag = 6; break;
	case 2: zi->ci.flag = 4; break;
	case 8: zi->ci.flag = 2; break;
	case 9: zi->ci.flag = 2; break;
	default: zi->ci.flag = 0; break;
	}

	zi->ci.crc32 = 0;
	zi->ci.method = method;
	zi->ci.stream_initialised = 0;
	zi->ci.pos_in_buffered_data = 0;
	zi->ci.pos_static_header = ftell(zi->filezip);
	zi->ci.size_centralheader = SIZECENTRALHEADER + size_filename + size_extrafield_global + size_comment;
	zi->ci.central_header = (char *)ALLOC((uInt)zi->ci.size_centralheader);

	zipstatic_putValue_inmemory(zi->ci.central_header, (uLong)CENTRALHEADERMAGIC, 4);
	/* version info */
	zipstatic_putValue_inmemory(zi->ci.central_header +  4, (uLong)VERSIONMADEBY, 2);
	zipstatic_putValue_inmemory(zi->ci.central_header +  6, (uLong)20, 2);
	zipstatic_putValue_inmemory(zi->ci.central_header +  8, (uLong)zi->ci.flag, 2);
	zipstatic_putValue_inmemory(zi->ci.central_header + 10, (uLong)zi->ci.method, 2);
	zipstatic_putValue_inmemory(zi->ci.central_header + 12, (uLong)zi->ci.dosDate, 4);
	zipstatic_putValue_inmemory(zi->ci.central_header + 16, (uLong)0, 4); /*crc*/
	zipstatic_putValue_inmemory(zi->ci.central_header + 20, (uLong)0, 4); /*compr size*/
	zipstatic_putValue_inmemory(zi->ci.central_header + 24, (uLong)0, 4); /*uncompr size*/
	zipstatic_putValue_inmemory(zi->ci.central_header + 28, (uLong)size_filename, 2);
	zipstatic_putValue_inmemory(zi->ci.central_header + 30, (uLong)size_extrafield_global, 2);
	zipstatic_putValue_inmemory(zi->ci.central_header + 32, (uLong)size_comment, 2);
	zipstatic_putValue_inmemory(zi->ci.central_header + 34, (uLong)0, 2); /*disk nm start*/

	if (!zipfi)
		zipstatic_putValue_inmemory(zi->ci.central_header + 36, (uLong)0, 2);
	else
		zipstatic_putValue_inmemory(zi->ci.central_header + 36, (uLong)zipfi->internal_fa, 2);

	if (!zipfi)
		zipstatic_putValue_inmemory(zi->ci.central_header + 38, (uLong)0, 4);
	else
		zipstatic_putValue_inmemory(zi->ci.central_header + 38, (uLong)zipfi->external_fa, 4);

	zipstatic_putValue_inmemory(zi->ci.central_header + 42, (uLong)zi->ci.pos_static_header, 4);

	for (i = 0; i < size_filename; i++)
		*(zi->ci.central_header + SIZECENTRALHEADER + i) = *(filename + i);

	for (i = 0; i < size_extrafield_global; i++)
		*(zi->ci.central_header + SIZECENTRALHEADER + size_filename + i) = *(((const char *)extrafield_global) + i);

	for (i = 0; i < size_comment; i++)
		*(zi->ci.central_header + SIZECENTRALHEADER + size_filename + size_extrafield_global + i) = *(filename + i);
	if (!zi->ci.central_header)
		return ZIP_INTERNALERROR;

	/* write the static header */
	err = zipstatic_putValue(zi->filezip, (uLong)LOCALHEADERMAGIC, 4);

	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)20, 2);/* version needed to extract */
	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)zi->ci.flag, 2);

	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)zi->ci.method, 2);

	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)zi->ci.dosDate, 4);

	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)0, 4); /* crc 32, unknown */
	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)0, 4); /* compressed size, unknown */
	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)0, 4); /* uncompressed size, unknown */

	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)size_filename, 2);

	if (err == ZIP_OK)
		err = zipstatic_putValue(zi->filezip, (uLong)size_extrafield_static, 2);

	if ((err == ZIP_OK) && (size_filename > 0))
		if (fwrite(filename,(uInt)size_filename, 1, zi->filezip) != 1)
			err = ZIP_ERRNO;

	if ((err == ZIP_OK) && (size_extrafield_static > 0))
		if (fwrite(extrafield_static, (uInt)size_extrafield_static, 1, zi->filezip) != 1)
			err = ZIP_ERRNO;

	zi->ci.stream.avail_in = (uInt)0;
	zi->ci.stream.avail_out = (uInt)Z_BUFSIZE;
	zi->ci.stream.next_out = zi->ci.buffered_data;
	zi->ci.stream.total_in = 0;
	zi->ci.stream.total_out = 0;

	if ((err == ZIP_OK) && (zi->ci.method == Z_DEFLATED))
	{
		zi->ci.stream.zalloc = (alloc_func)0;
		zi->ci.stream.zfree = (free_func)0;
		zi->ci.stream.opaque = (voidpf)0;

		err = deflateInit2(&zi->ci.stream, level, Z_DEFLATED, -MAX_WBITS, 8, 0);
		if (err == Z_OK)
			zi->ci.stream_initialised = 1;
	}

	if (err == Z_OK) zi->in_opened_file_inzip = 1;

	return err;
}


int zipWriteInFileInZip(zipFile file, const void *buf, unsigned len)
{
	zip_internal *zi;
	int err = ZIP_OK;

	if (!file) return ZIP_PARAMERROR;
	zi = (zip_internal *)file;

	if (!zi->in_opened_file_inzip) return ZIP_PARAMERROR;

	zi->ci.stream.next_in = (Bytef *)buf;
	zi->ci.stream.avail_in = len;
	zi->ci.crc32 = crc32(zi->ci.crc32, buf, len);

	while ((err == ZIP_OK) && (zi->ci.stream.avail_in > 0))
	{
		if (zi->ci.stream.avail_out == 0)
		{
			if (fwrite(zi->ci.buffered_data, (uInt)zi->ci.pos_in_buffered_data, 1, zi->filezip) != 1)
				err = ZIP_ERRNO;

			zi->ci.pos_in_buffered_data = 0;
			zi->ci.stream.avail_out = (uInt)Z_BUFSIZE;
			zi->ci.stream.next_out = zi->ci.buffered_data;
		}

		if (zi->ci.method == Z_DEFLATED)
		{
			uLong uTotalOutBefore = zi->ci.stream.total_out;

			err = deflate(&zi->ci.stream, Z_NO_FLUSH);
			zi->ci.pos_in_buffered_data += (uInt)(zi->ci.stream.total_out - uTotalOutBefore);

		}
		else
		{
			uInt copy_this, i;

			if (zi->ci.stream.avail_in < zi->ci.stream.avail_out)
				copy_this = zi->ci.stream.avail_in;
			else
				copy_this = zi->ci.stream.avail_out;

			for (i = 0; i < copy_this; i++)
				*(((char *)zi->ci.stream.next_out) + i) = *(((const char *)zi->ci.stream.next_in) + i);

			zi->ci.stream.avail_in -= copy_this;
			zi->ci.stream.avail_out -= copy_this;
			zi->ci.stream.next_in += copy_this;
			zi->ci.stream.next_out += copy_this;
			zi->ci.stream.total_in += copy_this;
			zi->ci.stream.total_out += copy_this;
			zi->ci.pos_in_buffered_data += copy_this;
		}
	}

	return 0;
}


int zipCloseFileInZip(zipFile file)
{
	zip_internal *zi;
	int err = ZIP_OK;

	if (!file) return ZIP_PARAMERROR;
	zi = (zip_internal *)file;

	if (!zi->in_opened_file_inzip) return ZIP_PARAMERROR;
	zi->ci.stream.avail_in = 0;

	if (zi->ci.method == Z_DEFLATED)
	{
		while (err == ZIP_OK)
		{
			uLong uTotalOutBefore;

			if (!zi->ci.stream.avail_out)
			{
				if (fwrite(zi->ci.buffered_data,(uInt)zi->ci.pos_in_buffered_data,1,zi->filezip) != 1)
					err = ZIP_ERRNO;

				zi->ci.pos_in_buffered_data = 0;
				zi->ci.stream.avail_out = (uInt)Z_BUFSIZE;
				zi->ci.stream.next_out = zi->ci.buffered_data;
			}
			uTotalOutBefore = zi->ci.stream.total_out;
			err = deflate(&zi->ci.stream, Z_FINISH);
			zi->ci.pos_in_buffered_data += (uInt)(zi->ci.stream.total_out - uTotalOutBefore);
		}
	}

	if (err == Z_STREAM_END)
		err = ZIP_OK; /* this is normal */

	if ((zi->ci.pos_in_buffered_data>0) && (err == ZIP_OK))
		if (fwrite(zi->ci.buffered_data,(uInt)zi->ci.pos_in_buffered_data,1,zi->filezip) != 1)
			err = ZIP_ERRNO;

	if ((zi->ci.method == Z_DEFLATED) && (err == ZIP_OK))
	{
		err = deflateEnd(&zi->ci.stream);
		zi->ci.stream_initialised = 0;
	}

	zipstatic_putValue_inmemory(zi->ci.central_header + 16, (uLong)zi->ci.crc32, 4); /*crc*/
	zipstatic_putValue_inmemory(zi->ci.central_header + 20, (uLong)zi->ci.stream.total_out, 4); /*compr size*/
	zipstatic_putValue_inmemory(zi->ci.central_header + 24, (uLong)zi->ci.stream.total_in, 4); /*uncompr size*/

	if (err == ZIP_OK)
		err = add_data_in_datablock(&zi->central_dir,zi->ci.central_header, (uLong)zi->ci.size_centralheader);
	free(zi->ci.central_header);

	if (err == ZIP_OK)
	{
		long cur_pos_inzip = ftell(zi->filezip);

		if (fseek(zi->filezip, zi->ci.pos_static_header + 14, SEEK_SET) != 0)
			err = ZIP_ERRNO;

		if (err == ZIP_OK)
			err = zipstatic_putValue(zi->filezip, (uLong)zi->ci.crc32, 4); /* crc 32, unknown */

		if (err == ZIP_OK) /* compressed size, unknown */
			err = zipstatic_putValue(zi->filezip, (uLong)zi->ci.stream.total_out, 4);

		if (err == ZIP_OK) /* uncompressed size, unknown */
			err = zipstatic_putValue(zi->filezip, (uLong)zi->ci.stream.total_in, 4);

		if (fseek(zi->filezip, cur_pos_inzip,SEEK_SET) != 0)
			err = ZIP_ERRNO;
	}

	zi->number_entry++;
	zi->in_opened_file_inzip = 0;

	return err;
}


int zipClose(zipFile file, const char *global_comment)
{
	zip_internal* zi;
	int err = 0;
	uLong size_centraldir = 0;
	uLong centraldir_pos_inzip;
	uInt size_global_comment;

	if (!file) return ZIP_PARAMERROR;
	zi = (zip_internal *)file;

	if (zi->in_opened_file_inzip == 1)
	{
		err = zipCloseFileInZip(file);
	}

	if (!global_comment)
		size_global_comment = 0;
	else
		size_global_comment = strlen(global_comment);

	centraldir_pos_inzip = ftell(zi->filezip);
	if (err == ZIP_OK)
	{
		linkedlist_datablock_internal *ldi = zi->central_dir.first_block;

		while (ldi)
		{
			if ((err == ZIP_OK) && (ldi->filled_in_this_block > 0))
				if (fwrite(ldi->data, (uInt)ldi->filled_in_this_block, 1, zi->filezip) != 1)
					err = ZIP_ERRNO;

			size_centraldir += ldi->filled_in_this_block;
			ldi = ldi->next_datablock;
		}
	}
	free_datablock(zi->central_dir.first_block);

	if (err == ZIP_OK) /* Magic End */
		err = zipstatic_putValue(zi->filezip, (uLong)ENDHEADERMAGIC, 4);

	if (err == ZIP_OK) /* number of this disk */
		err = zipstatic_putValue(zi->filezip, (uLong)0, 2);

	if (err == ZIP_OK) /* number of the disk with the start of the central directory */
		err = zipstatic_putValue(zi->filezip, (uLong)0, 2);

	if (err == ZIP_OK) /* total number of entries in the central dir on this disk */
		err = zipstatic_putValue(zi->filezip, (uLong)zi->number_entry, 2);

	if (err == ZIP_OK) /* total number of entries in the central dir */
		err = zipstatic_putValue(zi->filezip, (uLong)zi->number_entry, 2);

	if (err == ZIP_OK) /* size of the central directory */
		err = zipstatic_putValue(zi->filezip, (uLong)size_centraldir, 4);

	if (err == ZIP_OK) /* offset of start of central directory with respect to the starting disk number */
		err = zipstatic_putValue(zi->filezip, (uLong)centraldir_pos_inzip , 4);

	if (err == ZIP_OK) /* zipfile comment length */
		err = zipstatic_putValue(zi->filezip, (uLong)size_global_comment, 2);

	if ((err == ZIP_OK) && (size_global_comment > 0))
		if (fwrite(global_comment, (uInt)size_global_comment, 1, zi->filezip) != 1)
				err = ZIP_ERRNO;
	fclose(zi->filezip);
	TRYFREE(zi);

	return err;
}
