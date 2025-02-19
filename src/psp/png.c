/***************************************************************************

	png.c

    PSP PNG format image I/O functions. (based on M.A.M.E. PNG functions)

***************************************************************************/

#include <fcntl.h>
#include <math.h>
#include <zlib.h>
#include "emumain.h"


#define PNG_Signature       "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"

#define PNG_CN_IHDR 0x49484452L     /* Chunk names */
#define PNG_CN_PLTE 0x504C5445L
#define PNG_CN_IDAT 0x49444154L
#define PNG_CN_IEND 0x49454E44L
#define PNG_CN_gAMA 0x67414D41L
#define PNG_CN_sBIT 0x73424954L
#define PNG_CN_cHRM 0x6348524DL
#define PNG_CN_tRNS 0x74524E53L
#define PNG_CN_bKGD 0x624B4744L
#define PNG_CN_hIST 0x68495354L
#define PNG_CN_tEXt 0x74455874L
#define PNG_CN_zTXt 0x7A545874L
#define PNG_CN_pHYs 0x70485973L
#define PNG_CN_oFFs 0x6F464673L
#define PNG_CN_tIME 0x74494D45L
#define PNG_CN_sCAL 0x7343414CL

#define PNG_PF_None     0   /* Prediction filters */
#define PNG_PF_Sub      1
#define PNG_PF_Up       2
#define PNG_PF_Average  3
#define PNG_PF_Paeth    4


/* PNG support */
struct png_info
{
	uint32_t width;
	uint32_t height;
	uint8_t  *image;
	uint32_t rowbytes;
	uint8_t  *zimage;
	uint32_t zlength;

#if VIDEO_32BPP || (EMU_SYSTEM == NCDZ)
	uint32_t xres;
	uint32_t yres;
	uint32_t resolution_unit;
	uint32_t offset_unit;
	uint32_t scale_unit;
	uint8_t  bpp;
	uint8_t  bit_depth;
	uint8_t  color_type;
	uint8_t  compression_method;
	uint8_t  filter_method;
	uint8_t  interlace_method;
	uint32_t num_palette;
	uint8_t  *palette;
	uint32_t num_trans;
	uint8_t  *trans;
	uint8_t  *fimage;
	double xscale;
	double yscale;
	double source_gamma;
#endif
};

/********************************************************************************

  Helper functions

********************************************************************************/

static void errormsg(int number)
{
	switch (number)
	{
	case 0: ui_popup(TEXT(COULD_NOT_ALLOCATE_MEMORY_FOR_PNG)); break;
	case 1: ui_popup(TEXT(COULD_NOT_ENCODE_PNG_IMAGE)); break;
#if VIDEO_32BPP || (EMU_SYSTEM == NCDZ)
	case 2: ui_popup(TEXT(COULD_NOT_DECODE_PNG_IMAGE)); break;
#endif
	}
}


#if USE_CACHE

#define ALLOC_LOAD_SIZE	(((400*2) + 1024) * 1024)
#define ALLOC_SAVE_SIZE	((400 + 1024) * 1024)

static int left_mem;
static int alloc_size;
static uint8_t *next_ptr;

static void png_mem_init(int flag)
{
#ifdef SAVE_STATE
	if (GFX_MEMORY)
	{
		uint8_t *mem;

		if (flag)
			alloc_size = ALLOC_LOAD_SIZE;
		else
			alloc_size = ALLOC_SAVE_SIZE;

		mem = cache_alloc_state_buffer(alloc_size);

		next_ptr = (uint8_t *)(((uint32_t)mem + 15) & ~15);
		left_mem = alloc_size - 16;
	}
#endif
}

static void png_mem_exit(void)
{
#ifdef SAVE_STATE
	if (GFX_MEMORY)
	{
		cache_free_state_buffer(alloc_size);
	}
#endif
}

static void *png_alloc(int size)
{
	if (GFX_MEMORY)
	{
		size = (size + 15) & ~15;

		if (left_mem >= size)
		{
			uint8_t *mem = next_ptr;

			next_ptr = next_ptr + size;
			left_mem -= size;

			memset(mem, 0, size);

			return mem;
		}
		return NULL;
	}
	return malloc(size);
}

static void png_free(void *ptr)
{
	if (!GFX_MEMORY)
	{
		free(ptr);
	}
}

static voidpf png_zcalloc(voidpf opaque, unsigned items, unsigned size)
{
	return png_alloc(items * size);
}

static void png_zcfree(voidpf opaque, voidpf ptr)
{
	png_free(ptr);
}

#else

#define png_mem_init(flag)
#define png_mem_exit()
#define png_alloc		malloc
#define png_free		free
#define png_zcalloc		0
#define png_zcfree		0

#endif


#if VIDEO_32BPP
static const uint8_t *png_data;
static uint32_t png_size;
static uint32_t png_offset;

static size_t png_read(void *buf, size_t size, FILE *fp)
{
	if (fp)
	{
		return fread(buf, 1, size, fp);
	}
	else
	{
		if ((png_size - png_offset) >= size)
		{
			memcpy(buf, &png_data[png_offset], size);
			png_offset += size;
			return size;
		}
		else
		{
			size = png_size - png_offset;

			memcpy(buf, &png_data[png_offset], size);
			png_offset += size;
			return size;
		}
	}
}
#elif (EMU_SYSTEM == NCDZ)
#define png_read(buf, size, fp)	fread(buf, 1, size, fp)
#endif


/********************************************************************************

  PNG read functions

********************************************************************************/

#if VIDEO_32BPP || (EMU_SYSTEM == NCDZ)

/* convert_uint is here so we don't have to deal with byte-ordering issues */
static uint32_t convert_from_network_order(uint8_t *v)
{
	return (v[0] << 24) | (v[1] << 16) | (v[2] << 8) | (v[3]);
}

static int png_verify_signature(FILE *fp)
{
	int8_t signature[8];

	if (png_read(signature, 8, fp) == 8)
	{
		if (memcmp(signature, PNG_Signature, 8) == 0)
			return 1;
	}

	errormsg(2);
	return 0;
}

static int png_unfilter(struct png_info *p)
{
	uint32_t res = 0;

	if ((p->image = (uint8_t *)png_alloc(p->height * (p->rowbytes + 1))) != NULL)
	{
		uint32_t i, j, bpp, filter;
		int32_t prediction, pA, pB, pC, dA, dB, dC;
		uint8_t *src, *dst;

		src = p->fimage;
		dst = p->image;
		bpp = p->bpp;
		memset(p->image, 0, p->height * (p->rowbytes + 1));

		for (i = 0; i < p->height; i++)
		{
			dst++;
			filter = *src++;
			if (!filter)
			{
				memcpy(dst, src, p->rowbytes);
				src += p->rowbytes;
				dst += p->rowbytes;
			}
			else
			{
				for (j = 0; j < p->rowbytes; j++)
				{
					pA = (j < bpp) ? 0 : *(dst - bpp);
					pB = (i < 1) ? 0 : *(dst - (p->rowbytes + 1));
					pC = (j < bpp || i < 1) ? 0 : *(dst - (p->rowbytes + 1) - bpp);

					switch (filter)
					{
					case PNG_PF_Sub:
						prediction = pA;
						break;

					case PNG_PF_Up:
						prediction = pB;
						break;

					case PNG_PF_Average:
						prediction = ((pA + pB) / 2);
						break;

					case PNG_PF_Paeth:
						prediction = pA + pB - pC;
						dA = abs(prediction - pA);
						dB = abs(prediction - pB);
						dC = abs(prediction - pC);
						if (dA <= dB && dA <= dC) prediction = pA;
						else if (dB <= dC) prediction = pB;
						else prediction = pC;
						break;

					default:
						errormsg(2);
						prediction = 0;
						goto error;
					}

					*dst++ = 0xff & (*src++ + prediction);
				}
			}
		}
		res = 1;
	}
	else errormsg(0);

error:
	png_free(p->fimage);

	return res;
}

static int png_inflate_image(struct png_info *p)
{
	uint32_t res = 0, i, has_filter = 0;
	unsigned long fbuff_size;
	z_stream stream;

	fbuff_size = p->height * (p->rowbytes + 1);

	if ((p->fimage = (uint8_t *)png_alloc(fbuff_size)) == NULL)
	{
		errormsg(0);
		png_free(p->zimage);
		return res;
	}

	stream.next_in   = p->zimage;
	stream.avail_in  = (uInt)p->zlength;
	stream.next_out  = (Bytef *)p->fimage;
	stream.avail_out = (uInt)&fbuff_size;
	stream.zalloc    = (alloc_func)png_zcalloc;
	stream.zfree     = (free_func)png_zcfree;
	stream.opaque    = (voidpf)0;

	if (inflateInit(&stream) == Z_OK)
	{
		if (inflate(&stream, Z_FINISH) == Z_STREAM_END)
		{
			res = 1;
		}
		inflateEnd(&stream);
	}

	png_free(p->zimage);

	if (res)
	{
		for (i = 0; i < p->height; i++)
		{
			if (p->fimage[i * (p->rowbytes + 1)] != 0)
			{
				has_filter = 1;
				break;
			}
		}

		if (has_filter)
		{
			return png_unfilter(p);
		}
		else
		{
			p->image = p->fimage;
		}
	}
	else errormsg(2);

	return res;
}

static int png_read_file(FILE *fp, struct png_info *p)
{
	/* translates color_type to bytes per pixel */
	const int samples[] = {1, 0, 3, 1, 2, 0, 4};

	uint32_t chunk_length, chunk_type=0, chunk_crc, crc;
	uint8_t *chunk_data, *temp;
	uint8_t str_chunk_type[5], v[4];

	struct idat
	{
		struct idat *next;
		int length;
		uint8_t *data;
	} *ihead, *pidat;

	if ((ihead = malloc(sizeof(struct idat))) == NULL)
		return 0;

	pidat = ihead;

	if (png_verify_signature(fp) == 0)
		return 0;

	while (chunk_type != PNG_CN_IEND)
	{
		if (png_read(v, 4, fp) != 4) errormsg(2);
		chunk_length = convert_from_network_order(v);

		if (png_read(str_chunk_type, 4, fp) != 4) errormsg(1);

		str_chunk_type[4] = 0; /* terminate string */

		crc = crc32(0, str_chunk_type, 4);
		chunk_type = convert_from_network_order(str_chunk_type);

		if (chunk_length)
		{
			if ((chunk_data = (uint8_t *)malloc(chunk_length + 1)) == NULL)
			{
				errormsg(0);
				return 0;
			}
			if (png_read(chunk_data, chunk_length, fp) != chunk_length)
			{
				errormsg(2);
				free(chunk_data);
				return 0;
			}

			crc = crc32(crc, chunk_data, chunk_length);
		}
		else
			chunk_data = NULL;

		if (png_read(v, 4, fp) != 4) errormsg(2);
		chunk_crc = convert_from_network_order(v);

		if (crc != chunk_crc)
		{
			errormsg(2);
			return 0;
		}

		switch (chunk_type)
		{
		case PNG_CN_IHDR:
			p->width = convert_from_network_order(chunk_data);
			p->height = convert_from_network_order(chunk_data + 4);
			p->bit_depth = *(chunk_data + 8);
			p->color_type = *(chunk_data + 9);
			p->compression_method = *(chunk_data + 10);
			p->filter_method = *(chunk_data + 11);
			p->interlace_method = *(chunk_data + 12);
			free(chunk_data);
			break;

		case PNG_CN_PLTE:
			p->num_palette = chunk_length/3;
			p->palette = chunk_data;
			break;

		case PNG_CN_tRNS:
			p->num_trans = chunk_length;
			p->trans = chunk_data;
			break;

		case PNG_CN_IDAT:
			pidat->data = chunk_data;
			pidat->length = chunk_length;
			if ((pidat->next = malloc(sizeof(struct idat))) == NULL)
				return 0;
			pidat = pidat->next;
			pidat->next = 0;
			p->zlength += chunk_length;
			break;

		case PNG_CN_tEXt:
			{
				char *text = (char *)chunk_data;

				while (*text++);
				chunk_data[chunk_length] = 0;
			}
			free(chunk_data);
			break;

		case PNG_CN_tIME:
			free(chunk_data);
			break;

		case PNG_CN_gAMA:
			p->source_gamma	 = convert_from_network_order(chunk_data) / 100000.0;
			free(chunk_data);
			break;

		case PNG_CN_pHYs:
			p->xres = convert_from_network_order(chunk_data);
			p->yres = convert_from_network_order(chunk_data + 4);
			p->resolution_unit = *(chunk_data + 8);
			free(chunk_data);
			break;

		case PNG_CN_IEND:
			break;

		default:
			if (chunk_data) free(chunk_data);
			break;
		}
	}

	if (p->width > SCR_WIDTH || p->height > SCR_HEIGHT)
	{
		errormsg(2);
		return 0;
	}

	if ((p->zimage = (uint8_t *)png_alloc(p->zlength)) == NULL)
	{
		errormsg(0);
		return 0;
	}

	/* combine idat chunks to compressed image data */
	temp = p->zimage;
	while (ihead->next)
	{
		pidat = ihead;
		memcpy(temp, pidat->data, pidat->length);
		free(pidat->data);
		temp += pidat->length;
		ihead = pidat->next;
		free(pidat);
	}
	p->bpp = (samples[p->color_type] * p->bit_depth) / 8;
	p->rowbytes = ceil((p->width * p->bit_depth * samples[p->color_type]) / 8.0);

	if (png_inflate_image(p) == 0)
		return 0;

	return 1;
}


/*--------------------------------------------------------
	������������Ȥ���
--------------------------------------------------------*/

#if VIDEO_32BPP
static inline void adjust_blightness(uint8_t *r, uint8_t *g, uint8_t *b)
{
	switch (bgimage_blightness)
	{
	case 25:
		*r  >>= 2;
		*g  >>= 2;
		*b  >>= 2;
		break;

	case 50:
		*r  >>= 1;
		*g  >>= 1;
		*b  >>= 1;
		break;

	case 75:
		*r = (*r >> 1) + (*r >> 2);
		*g = (*g >> 1) + (*g >> 2);
		*b = (*b >> 1) + (*b >> 2);
		break;

	default:
		*r = (uint8_t)((int)*r * bgimage_blightness / 100);
		*g = (uint8_t)((int)*g * bgimage_blightness / 100);
		*b = (uint8_t)((int)*b * bgimage_blightness / 100);
		break;
	}
}
#endif


/*--------------------------------------------------------
	PNG�i���z��
--------------------------------------------------------*/

int load_png(const char *name, int number)
{
	struct png_info p;
	FILE *fp;
	uint32_t res = 0;

	memset(&p, 0, sizeof(struct png_info));

	video_driver->clearFrame(video_data, draw_frame);

#if VIDEO_32BPP
	if (name)
	{
		if ((fp = fopen(name, "rb")) == NULL)
			return 0;
	}
	else
	{
		fp = NULL;
		png_data = wallpaper[number];
		png_size = wallpaper_size[number];
		png_offset = 0;
	}
#else
	if ((fp = fopen(name, "rb")) == NULL)
		return 0;
#endif

	png_mem_init(1);

	if ((res = png_read_file(fp, &p)))
	{
		uint32_t x, y, sx, sy;
		uint8_t *src = p.image;

		sx = (SCR_WIDTH - p.width) >> 1;
		sy = (SCR_HEIGHT - p.height) >> 1;

#if VIDEO_32BPP
		if (video_mode == 32)
		{
			uint32_t *vptr, *dst;

			vptr = (uint32_t *)video_driver->frameAddr(video_data, draw_frame, sx, sy);

			switch (p.bpp * p.bit_depth)
			{
			case 8:
				for (y = 0; y < p.height; y++)
				{
					src++;
					dst = &vptr[y * BUF_WIDTH];

					for (x = 0; x < p.width; x++)
					{
						uint8_t color = *src++;
						uint8_t r = p.palette[color * 3 + 0];
						uint8_t g = p.palette[color * 3 + 1];
						uint8_t b = p.palette[color * 3 + 2];

						if (bgimage_blightness != 100)
							adjust_blightness(&r, &g, &b);

						dst[x] = MAKECOL32(r, g, b);
					}
				}
				break;

			case 24:
				for (y = 0; y < p.height; y++)
				{
					src++;
					dst = &vptr[y * BUF_WIDTH];

					for (x = 0; x < p.width; x++)
					{
						uint8_t r = *src++;
						uint8_t g = *src++;
						uint8_t b = *src++;

						if (bgimage_blightness != 100)
							adjust_blightness(&r, &g, &b);

						dst[x] = MAKECOL32(r, g, b);
					}
				}
				break;

			default:
				ui_popup(TEXT(xBIT_COLOR_PNG_IMAGE_NOT_SUPPORTED), p.bpp * p.bit_depth);
				break;
			}
		}
		else
#endif
		{
			uint16_t *vptr, *dst;

			vptr = (uint16_t *)video_driver->frameAddr(video_data, draw_frame, sx, sy);

			switch (p.bpp * p.bit_depth)
			{
			case 8:
				for (y = 0; y < p.height; y++)
				{
					src++;
					dst = &vptr[y * BUF_WIDTH];

					for (x = 0; x < p.width; x++)
					{
						uint8_t color = *src++;
						uint8_t r = p.palette[color * 3 + 0];
						uint8_t g = p.palette[color * 3 + 1];
						uint8_t b = p.palette[color * 3 + 2];

						dst[x] = MAKECOL15(r, g, b);
					}
				}
				break;

			case 24:
				for (y = 0; y < p.height; y++)
				{
					src++;
					dst = &vptr[y * BUF_WIDTH];

					for (x = 0; x < p.width; x++)
					{
						uint8_t r = *src++;
						uint8_t g = *src++;
						uint8_t b = *src++;

						dst[x] = MAKECOL15(r, g, b);
					}
				}
				break;

			default:
				ui_popup(TEXT(xBIT_COLOR_PNG_IMAGE_NOT_SUPPORTED), p.bpp * p.bit_depth);
				break;
			}
		}
	}

	if (p.palette) free(p.palette);
	if (p.image) png_free(p.image);

	fclose(fp);

	png_mem_exit();

	return res;
}

#endif


/********************************************************************************

  PNG write functions

********************************************************************************/

struct png_text
{
	char *data;
	int length;
	struct png_text *next;
};

static struct png_text *png_text_list = 0;

static void convert_to_network_order(uint32_t i, uint8_t *v)
{
	v[0] = (i >> 24) & 0xff;
	v[1] = (i >> 16) & 0xff;
	v[2] = (i >>  8) & 0xff;
	v[3] = (i >>  0) & 0xff;
}

static int png_add_text(const char *keyword, const char *text)
{
	struct png_text *pt;

	if ((pt = malloc(sizeof(struct png_text))) == NULL)
		return 0;

	pt->length = strlen(keyword) + strlen(text) + 1;
	if ((pt->data = malloc(pt->length + 1)) == NULL)
		return 0;

	strcpy(pt->data, keyword);
	strcpy(pt->data + strlen(keyword) + 1, text);
	pt->next = png_text_list;
	png_text_list = pt;

	return 1;
}

static int write_chunk(SceUID fd, uint32_t chunk_type, uint8_t *chunk_data, uint32_t chunk_length)
{
	uint32_t crc;
	uint8_t v[4];
	uint32_t written;

	/* write length */
	convert_to_network_order(chunk_length, v);
	written = write(fd, v, 4);

	/* write type */
	convert_to_network_order(chunk_type, v);
	written += write(fd, v, 4);

	/* calculate crc */
	crc = crc32(0, v, 4);
	if (chunk_length > 0)
	{
		/* write data */
		written += write(fd, chunk_data, chunk_length);
		crc = crc32(crc, chunk_data, chunk_length);
	}
	convert_to_network_order(crc, v);

	/* write crc */
	written += write(fd, v, 4);

	if (written != 3 * 4 + chunk_length)
	{
		errormsg(1);
		return 0;
	}
	return 1;
}

static int png_write_sig(SceUID fd)
{
	/* PNG Signature */
	if (write(fd, PNG_Signature, 8) != 8)
	{
		errormsg(1);
		return 0;
	}
	return 1;
}

static int png_write_datastream(SceUID fd, struct png_info *p)
{
	uint8_t ihdr[13];
	struct png_text *pt;

	/* IHDR */
	convert_to_network_order(p->width, ihdr);
	convert_to_network_order(p->height, ihdr + 4);
	*(ihdr +  8) = 8;	// bit depth;
	*(ihdr +  9) = 2;	// color type
	*(ihdr + 10) = 0;	// compression method;
	*(ihdr + 11) = 0;	// fliter
	*(ihdr + 12) = 0;	// interlace

	if (write_chunk(fd, PNG_CN_IHDR, ihdr, 13) == 0)
		return 0;

	/* IDAT */
	if (write_chunk(fd, PNG_CN_IDAT, p->zimage, p->zlength) == 0)
		return 0;

	/* tEXt */
	while (png_text_list)
	{
		pt = png_text_list;
		if (write_chunk(fd, PNG_CN_tEXt, (uint8_t *)pt->data, pt->length) == 0)
			return 0;
		free(pt->data);

		png_text_list = pt->next;
		free(pt);
	}

	/* IEND */
	if (write_chunk(fd, PNG_CN_IEND, NULL, 0) == 0)
		return 0;

	return 1;
}

static int png_deflate_image(struct png_info *p)
{
	unsigned long zbuff_size;
	z_stream stream;

	zbuff_size = (p->height * (p->rowbytes + 1)) * 1.1 + 12;

	if ((p->zimage = (uint8_t *)png_alloc(zbuff_size)) == NULL)
	{
		errormsg(0);
		return 0;
	}

	stream.next_in   = (Bytef*)p->image;
	stream.avail_in  = p->height * (p->rowbytes + 1);//
	stream.next_out  = p->zimage;
	stream.avail_out = (uInt)&zbuff_size;
	stream.zalloc    = (alloc_func)png_zcalloc;
	stream.zfree     = (free_func)png_zcfree;
	stream.opaque    = (voidpf)0;

	if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) == Z_OK)
	{
		if (deflate(&stream, Z_FINISH) == Z_STREAM_END)
		{
			deflateEnd(&stream);
			p->zlength = stream.total_out;
			return 1;
		}
		deflateEnd(&stream);
	}

	errormsg(1);
	return 0;
}

static int png_create_datastream(SceUID fd)
{
	uint32_t x, y;
	uint8_t *dst;
	struct png_info p;

	memset(&p, 0, sizeof (struct png_info));
	p.width    = SCR_WIDTH;
	p.height   = SCR_HEIGHT;
	p.rowbytes = p.width * 3;

	if ((p.image = (uint8_t *)png_alloc(p.height * (p.rowbytes + 1))) == NULL)
	{
		errormsg(0);
		return 0;
	}

	dst = p.image;

#if VIDEO_32BPP
#if 0
	if (video_mode == 32)
	{
		uint32_t *vptr, *src;

		vptr = (uint32_t *)video_driver->frameAddr(video_data, show_frame, 0, 0);

		for (y = 0; y < p.height; y++)
		{
			src = &vptr[y * BUF_WIDTH];

			*dst++ = 0;
			for (x = 0; x < p.width; x++)
			{
				uint32_t color = src[x];
				*dst++ = (uint8_t)GETR32(color);
				*dst++ = (uint8_t)GETG32(color);
				*dst++ = (uint8_t)GETB32(color);
			}
		}
	}
#endif
#endif
	{
		uint16_t *vptr, *src;

		vptr = (uint16_t *)video_driver->frameAddr(video_data, show_frame, 0, 0);

		for (y = 0; y < p.height; y++)
		{
			src = &vptr[y * BUF_WIDTH];

			*dst++ = 0;
			for (x = 0; x < p.width; x++)
			{
				uint16_t color = src[x];
				*dst++ = (uint8_t)GETR15(color);
				*dst++ = (uint8_t)GETG15(color);
				*dst++ = (uint8_t)GETB15(color);
			}
		}
	}

	if (png_deflate_image(&p) == 0)
		return 0;

	if (png_write_datastream(fd, &p) == 0)
		return 0;

	if (p.image)  png_free(p.image);
	if (p.zimage) png_free(p.zimage);

	return 1;
}


/*--------------------------------------------------------
	PNG����
--------------------------------------------------------*/

int save_png(const char *path)
{
	SceUID fd;
	int res = 0;

	png_mem_init(0);

	if ((fd = open(path, O_WRONLY|O_CREAT, 0777)) >= 0)
	{
		if ((res = png_add_text("Software", APPNAME_STR " " VERSION_STR)))
		{
			if ((res = png_add_text("System", "PSP")))
			{
				if ((res = png_write_sig(fd)))
				{
					res = png_create_datastream(fd);
				}
			}
		}

		close(fd);
	}

	png_mem_exit();

	return res;
}
