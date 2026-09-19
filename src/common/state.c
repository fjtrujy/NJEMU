/******************************************************************************

	state.c

	State Save/Load

******************************************************************************/

#ifdef SAVE_STATE

#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <zlib.h>
#include "emumain.h"
#include "common/ui.h"

typedef struct {
	uint16_t year;
	uint16_t month;
	uint16_t day;
	uint16_t hour;
	uint16_t minutes;
	uint16_t seconds;
	uint32_t microseconds;
} stateTime;

/******************************************************************************
	Global Variables
******************************************************************************/

char date_str[16];
char time_str[16];
char stver_str[16];
int state_version;
uint8_t *state_buffer;
int current_state_version;
#if (EMU_SYSTEM == MVS)
int  state_reload_bios;
#endif


/******************************************************************************
	Local Variables
******************************************************************************/

#ifdef ADHOC
static uint8_t state_buffer_base[STATE_BUFFER_SIZE];
#endif

#if (EMU_SYSTEM == CPS1)
static const char *current_version_str = "CPS1SV23";
#elif (EMU_SYSTEM == CPS2)
static const char *current_version_str = "CPS2SV23";
#elif (EMU_SYSTEM == MVS)
static const char *current_version_str = "MVS_SV23";
#elif (EMU_SYSTEM == NCDZ)
static const char *current_version_str = "NCDZSV23";
#endif


/******************************************************************************
	Local Functions
******************************************************************************/

/*------------------------------------------------------
	Save Thumbnail from Work Area to File
------------------------------------------------------*/

static uint16_t *state_thumbnail_addr(int x)
{
#if defined(PS2)
	return (uint16_t *)video_driver->frameAddr(video_data,
		COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, x, 0);
#else
	return ((uint16_t *)UI_TEXTURE) + x;
#endif
}

static void save_thumbnail(void)
{
	int x, y, w, h;
	uint16_t *src;
#if defined(PS2)
	uint16_t *readback = NULL;
#endif

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	if (machine_screen_type)
	{
		w = 112;
		h = 152;
	}
	else
#endif
	{
		w = 152;
		h = 112;
	}

#if defined(PS2)
	/* state_make_thumbnail() renders the preview into the GS-backed scratch,
	 * so its CPU staging copy is stale here.  Read back exactly the generated
	 * rectangle before serializing it into the state file. */
	readback = (uint16_t *)calloc((size_t)w * h, sizeof(uint16_t));
	if (readback) {
		/* A failed readback leaves the zero-filled thumbnail in place.  Keeping
		 * the fixed thumbnail payload is more important than the preview itself:
		 * the rest of the state file uses fixed offsets past this block. */
		ps2_video_read_frame(video_data,
			COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER,
			152, 0, w, h, readback, w);
	}
	src = readback;
#else
	src = state_thumbnail_addr(152);
	if (!src)
		return;
#endif

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
	#if defined(PS2)
			uint16_t empty = 0;
			state_save_word(src ? &src[x] : &empty, 1);
	#else
			state_save_word(&src[x], 1);
	#endif
		}
	#if defined(PS2)
		if (src)
			src += w;
	#else
		src += BUF_WIDTH;
	#endif
	}

#if defined(PS2)
	free(readback);
#endif
}


/*------------------------------------------------------
	Load Thumbnail from File to Work Area
------------------------------------------------------*/

static void load_thumbnail(int fd)
{
	int x, y, w, h;
	uint16_t *dst = state_thumbnail_addr(0);
	if (!dst)
		return;

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	if (machine_screen_type)
	{
		w = 112;
		h = 152;
	}
	else
#endif
	{
		w = 152;
		h = 112;
	}

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			read(fd, &dst[x], 2);
		}
		dst += BUF_WIDTH;
	}
}


/*------------------------------------------------------
	Clear Thumbnail in Work Area
------------------------------------------------------*/

static void clear_thumbnail(void)
{
	int x, y, w, h;
	uint16_t *dst = state_thumbnail_addr(0);
	if (!dst)
		return;

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	if (machine_screen_type)
	{
		w = 112;
		h = 152;
	}
	else
#endif
	{
		w = 152;
		h = 112;
	}

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			dst[x] = 0;
		}
		dst += BUF_WIDTH;
	}
}


/******************************************************************************
	State Save/Load Functions
******************************************************************************/

/*------------------------------------------------------
	State Save
------------------------------------------------------*/

static inline void tm_to_stateTime(stateTime *st, struct tm *t)
{
	st->year = t->tm_year;
	st->month = t->tm_mon;
	st->day = t->tm_mday;
	st->hour = t->tm_hour;
	st->minutes = t->tm_min;
	st->seconds = t->tm_sec;
	st->microseconds = 0;
}

int state_save(int slot)
{
	int32_t fd = -1;
   	stateTime nowtime;
	char path[PATH_MAX];
	char error_mes[128];
	char buf[128];
#if (EMU_SYSTEM == NCDZ)
	uint8_t *inbuf, *outbuf;
	unsigned long insize, outsize;
#else
#ifndef ADHOC
	uint8_t *state_buffer_base;
#endif
	uint32_t size;
#endif

	sprintf(path, "%sstate/%s.sv%d", launchDir, game_name, slot);
	remove(path);

	sprintf(buf, TEXT(STATE_SAVING), game_name, slot);
	init_progress(6, buf);

	time_t now = time(NULL);
   	struct tm *t = localtime(&now);
	tm_to_stateTime(&nowtime, t);

	if ((fd = open(path, O_WRONLY|O_CREAT, 0777)) >= 0)
#if (EMU_SYSTEM == NCDZ)
	{
		if ((inbuf = malloc(STATE_BUFFER_SIZE)) == NULL)
		{
			strcpy(error_mes, TEXT(COULD_NOT_ALLOCATE_STATE_BUFFER));
			goto error;
		}
		memset(inbuf, 0, STATE_BUFFER_SIZE);
		state_buffer = inbuf;

		state_save_byte(current_version_str, 8);
		state_save_byte(&nowtime, 16);
		update_progress();

		save_thumbnail();
		update_progress();

		write(fd, inbuf, (uint32_t)state_buffer - (uint32_t)inbuf);
		update_progress();

		memset(inbuf, 0, STATE_BUFFER_SIZE);
		state_buffer = inbuf;

		state_save_memory();
		state_save_m68000();
		state_save_z80();
		state_save_input();
		state_save_timer();
		state_save_driver();
		state_save_video();
		state_save_ym2610();
		state_save_cdda();
		state_save_cdrom();
		update_progress();

		insize = (uint32_t)state_buffer - (uint32_t)inbuf;
		outsize = insize * 1.1 + 12;
		if ((outbuf = malloc(outsize)) == NULL)
		{
			strcpy(error_mes, TEXT(COULD_NOT_ALLOCATE_STATE_BUFFER));
			free(inbuf);
			goto error;
		}
		memset(outbuf, 0, outsize);

		if (compress(outbuf, &outsize, inbuf, insize) != Z_OK)
		{
			strcpy(error_mes, TEXT(COULD_NOT_COMPRESS_STATE_DATA));
			free(inbuf);
			free(outbuf);
			goto error;
		}
		free(inbuf);
		update_progress();

		write(fd, &outsize, 4);
		write(fd, outbuf, outsize);
		close(fd);
		free(outbuf);
		update_progress();

		show_progress(buf);
		return 1;
	}
#else
	{
#ifdef ADHOC
		state_buffer = state_buffer_base;
#else
#if (EMU_SYSTEM == CPS1 || (EMU_SYSTEM == CPS2 && defined(LARGE_MEMORY)))
		state_buffer = state_buffer_base = malloc(STATE_BUFFER_SIZE);
#else
		state_buffer = state_buffer_base = cache_alloc_state_buffer(STATE_BUFFER_SIZE);
#endif
		if (!state_buffer)
		{
			strcpy(error_mes, TEXT(COULD_NOT_ALLOCATE_STATE_BUFFER));
			goto error;
		}
#endif
		memset(state_buffer, 0, STATE_BUFFER_SIZE);
		update_progress();

		state_save_byte(current_version_str, 8);
		state_save_byte(&nowtime, 16);
		update_progress();

		save_thumbnail();
		update_progress();

		state_save_memory();
		state_save_m68000();
		state_save_z80();
		state_save_input();
		state_save_timer();
		state_save_driver();
		state_save_video();
#if (EMU_SYSTEM == CPS1)
		state_save_coin();
		switch (machine_driver_type)
		{
		case MACHINE_qsound:
			state_save_qsound();
			state_save_eeprom();
			break;

		case MACHINE_pang3:
			state_save_eeprom();

		default:
			state_save_ym2151();
			break;
		}
#elif (EMU_SYSTEM == CPS2)
		state_save_coin();
		state_save_qsound();
		state_save_eeprom();
#elif (EMU_SYSTEM == MVS)
		state_save_ym2610();
		state_save_pd4990a();
#endif
		update_progress();

		size = (uint32_t)state_buffer - (uint32_t)state_buffer_base;
		write(fd, state_buffer_base, size);
		close(fd);
		update_progress();

#ifndef ADHOC
#if (EMU_SYSTEM == CPS1 || (EMU_SYSTEM == CPS2 && defined(LARGE_MEMORY)))
		free(state_buffer_base);
#else
		cache_free_state_buffer(STATE_BUFFER_SIZE);
#endif
#endif
		update_progress();

		show_progress(buf);
		return 1;
	}
#endif
	else
	{
		sprintf(error_mes, TEXT(COULD_NOT_CREATE_STATE_FILE), game_name, slot);
	}

#if !defined(ADHOC) || (EMU_SYSTEM == NCDZ)
error:
#endif
	if (fd >= 0)
	{
		close(fd);
		remove(path);
	}
	show_progress(error_mes);
	pad_wait_press(PAD_WAIT_INFINITY);

	return 0;
}


/*------------------------------------------------------
	State Load
------------------------------------------------------*/

int state_load(int slot)
{
	int32_t fd;
	char path[PATH_MAX];
	char error_mes[128];
	char buf[128];
#if (EMU_SYSTEM == NCDZ)
	uint8_t *inbuf, *outbuf;
	unsigned long insize, outsize;
#endif

	sprintf(path, "%sstate/%s.sv%d", launchDir, game_name, slot);

#if (EMU_SYSTEM == MVS)
	state_reload_bios = 0;
#endif

	sprintf(buf, TEXT(STATE_LOADING), game_name, slot);
#if (EMU_SYSTEM == NCDZ)
	init_progress(6, buf);
#else
	init_progress(4, buf);
#endif

#if (EMU_SYSTEM == NCDZ)
	if ((fd = open(path, O_RDONLY, 0777)) >= 0)
	{
		lseek(fd, (8+16) + (152*112*2), SEEK_SET);
		update_progress();

		read(fd, &insize, 4);
		if ((inbuf = malloc(insize)) == NULL)
		{
			strcpy(error_mes, TEXT(COULD_NOT_ALLOCATE_STATE_BUFFER));
			close(fd);
			goto error;
		}
		memset(inbuf, 0, insize);
		update_progress();

		read(fd, inbuf, insize);
		close(fd);
		update_progress();

		outsize = STATE_BUFFER_SIZE;
		if ((outbuf = malloc(outsize)) == NULL)
		{
			strcpy(error_mes, TEXT(COULD_NOT_ALLOCATE_STATE_BUFFER));
			free(inbuf);
			goto error;
		}
		memset(outbuf, 0, outsize);

		if (uncompress(outbuf, &outsize, inbuf, insize) != Z_OK)
		{
			strcpy(error_mes, TEXT(COULD_NOT_UNCOMPRESS_STATE_DATA));
			free(inbuf);
			free(outbuf);
			goto error;
		}
		free(inbuf);
		update_progress();

		state_buffer = outbuf;

		state_load_memory();
		state_load_m68000();
		state_load_z80();
		state_load_input();
		state_load_timer();
		state_load_driver();
		state_load_video();
		state_load_ym2610();
		state_load_cdda();
		state_load_cdrom();
		update_progress();

		free(outbuf);

		if (mp3_get_status() == MP3_SEEK)
		{
			mp3_seek_start();

			while (mp3_get_status() == MP3_SEEK)
				video_driver->waitVsync(video_data);
		}
		update_progress();

		show_progress(buf);
		return 1;
	}
#else
#ifdef ADHOC
	if ((fd = open(path, O_RDONLY, 0777)) >= 0)
	{
		int size;

		size = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		read(fd, state_buffer_base, size);
		close(fd);

		state_buffer = state_buffer_base;

		state_load_skip((8+16));
		update_progress();

		state_load_skip((152*112*2));
		update_progress();

		state_load_memory();
		state_load_m68000();
		state_load_z80();
		state_load_input();
		state_load_timer();
		state_load_driver();
		state_load_video();
#if (EMU_SYSTEM == CPS1)

		state_load_coin();
		switch (machine_driver_type)
		{
		case MACHINE_qsound:
			state_load_qsound();
			state_load_eeprom();
			break;

		case MACHINE_pang3:
			state_load_eeprom();

		default:
			state_load_ym2151();
			break;
		}
#elif (EMU_SYSTEM == CPS2)
		state_load_coin();
		state_load_qsound();
		state_load_eeprom();
#elif (EMU_SYSTEM == MVS)
		state_load_ym2610();
		state_load_pd4990a();

		if (state_reload_bios)
		{
			state_reload_bios = 0;

			if (!reload_bios())
			{
				show_progress(TEXT(COULD_NOT_RELOAD_BIOS));
				pad_wait_press(PAD_WAIT_INFINITY);
				Loop = LOOP_BROWSER;
				return 0;
			}
		}
#endif
#else
	if ((fd = open(path, O_RDONLY)) >= 0)
	{
		state_load_skip((8+16));
		update_progress();

		state_load_skip((152*112*2));
		update_progress();

		state_load_memory(fd);
		state_load_m68000(fd);
		state_load_z80(fd);
		state_load_input(fd);
		state_load_timer(fd);
		state_load_driver(fd);
		state_load_video(fd);
#if (EMU_SYSTEM == CPS1)

		state_load_coin(fd);
		switch (machine_driver_type)
		{
		case MACHINE_qsound:
			state_load_qsound(fd);
			state_load_eeprom(fd);
			break;

		case MACHINE_pang3:
			state_load_eeprom(fd);

		default:
			state_load_ym2151(fd);
			break;
		}
		close(fd);
#elif (EMU_SYSTEM == CPS2)
		state_load_coin(fd);
		state_load_qsound(fd);
		state_load_eeprom(fd);
		close(fd);
#elif (EMU_SYSTEM == MVS)
		state_load_ym2610(fd);
		state_load_pd4990a(fd);
		close(fd);

		if (state_reload_bios)
		{
			state_reload_bios = 0;

			if (!reload_bios())
			{
				show_progress(TEXT(COULD_NOT_RELOAD_BIOS));
				pad_wait_press(PAD_WAIT_INFINITY);
				Loop = LOOP_BROWSER;
				return 0;
			}
		}
#endif
#endif

		update_progress();

		show_progress(buf);
		return 1;
	}
#endif
	else
	{
		sprintf(error_mes, TEXT(COULD_NOT_OPEN_STATE_FILE), game_name, slot);
	}

#if (EMU_SYSTEM == NCDZ)
error:
#endif
	show_progress(error_mes);
	pad_wait_press(PAD_WAIT_INFINITY);

	return 0;
}


/*------------------------------------------------------
	Create Thumbnail
------------------------------------------------------*/

void state_make_thumbnail(void)
{
	{
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
		RECT clip1 = { 64, 16, 64 + 384, 16 + 224 };

		if (machine_screen_type)
		{
			RECT clip2 = { 152, 0, 152 + 112, 152 };
			video_driver->copyRectRotate(video_data, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, &clip1, &clip2);
		}
		else
		{
			RECT clip2 = { 152, 0, 152 + 152, 112 };
			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, &clip1, &clip2);
		}
#elif (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
		RECT clip1 = { 24, 16, 336, 240 };
		RECT clip2 = { 152, 0, 152 + 152, 112 };

		video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, &clip1, &clip2);
#endif
	}
}


/*------------------------------------------------------
	Load Thumbnail
------------------------------------------------------*/

int state_load_thumbnail(int slot)
{
	int fd;
	char path[PATH_MAX];

	clear_thumbnail();

	sprintf(path, "%sstate/%s.sv%d", launchDir, game_name, slot);

	fd = open(path, O_RDONLY);
	if (fd >= 0)
	{
		stateTime t;

		memset(stver_str, 0, 16);

		read(fd, stver_str, 8);
		read(fd, &t, 16);
		load_thumbnail(fd);
		close(fd);

		current_state_version = current_version_str[7] - '0';
		state_version = stver_str[7] - '0';

		sprintf(date_str, "%04d/%02d/%02d", t.year, t.month, t.day);
		sprintf(time_str, "%02d:%02d:%02d", t.hour, t.minutes, t.seconds);

		return 1;
	}

	ui_popup(TEXT(COULD_NOT_OPEN_STATE_FILE), game_name, slot);

	return 0;
}


/*------------------------------------------------------
	Clear Thumbnail
------------------------------------------------------*/

void state_clear_thumbnail(void)
{
	strcpy(date_str, "----/--/--");
	strcpy(time_str, "--:--:--");
	strcpy(stver_str, "--------");

	state_version = 0;

	clear_thumbnail();
}

/******************************************************************************
	AdHoc State Send/Receive Functions
******************************************************************************/

#ifdef ADHOC

/*
	Data size is calculated as ((actual data size / 0x3ff) + 1) * 0x3ff

	0x3ff = 0x400 bytes (send buffer size) - 1 byte (data identifier code)
*/

#if (EMU_SYSTEM == CPS1)
#define ADHOC_STATE_SIZE	0x452eb		// CPS1 adhoc: 0x450d3
#elif (EMU_SYSTEM == CPS2)
#define ADHOC_STATE_SIZE	0x4b2d3		// CPS2 adhoc: 0x4b1f7
#elif (EMU_SYSTEM == MVS)
#define ADHOC_STATE_SIZE	0x46ee4		// MVS adhoc: 0x46da2
#endif

/*------------------------------------------------------
	State Transmission
------------------------------------------------------*/

int adhoc_send_state(uint32_t *frame)
{
	int error = 0;
	int retry_count = 10;

	state_buffer = state_buffer_base;

	memset(state_buffer, 0, STATE_BUFFER_SIZE);

	if (frame != NULL)
		*(uint32_t *)state_buffer = *frame;

	state_buffer += 4;

	state_save_memory();
	state_save_m68000();
	state_save_z80();
	state_save_input();
	state_save_timer();
	state_save_driver();
	state_save_video();

#if (EMU_SYSTEM == CPS1)
	state_save_coin();
	switch (machine_driver_type)
	{
	case MACHINE_qsound:
		state_save_qsound();
		state_save_eeprom();
		break;

	case MACHINE_pang3:
		state_save_eeprom();

	default:
		state_save_ym2151();
		break;
	}

#elif (EMU_SYSTEM == CPS2)
	state_save_coin();
	state_save_qsound();
	state_save_eeprom();

#elif (EMU_SYSTEM == MVS)
	state_save_ym2610();
	state_save_pd4990a();
#endif

#if 0
	{
		int size = (uint32_t)state_buffer - (uint32_t)state_buffer_base;
		ui_popup("size = %08x (%08x)", size, ((size / 0x3ff) + 1) * 0x3ff);
	}
#endif

retry:
	adhocWait(ADHOC_DATASIZE_SYNC);
	if (adhocSync() < 0)
	{
		return 0;
	}
	if ((error = adhocSendRecvAck(state_buffer_base, ADHOC_STATE_SIZE, ADHOC_TIMEOUT, ADHOC_DATATYPE_STATE)) != ADHOC_STATE_SIZE)
	{
		if (error == (int)0x80410715)	// Timeout
		{
			if (Loop != LOOP_EXEC) return 1;
			if (--retry_count) goto retry;
		}
		return 0;
	}

	return 1;
}


/*------------------------------------------------------
	State Reception
------------------------------------------------------*/

int adhoc_recv_state(uint32_t *frame)
{
	int error = 0;
	int retry_count = 10;

retry:
	adhocWait(ADHOC_DATASIZE_SYNC);
	if (adhocSync() < 0)
	{
		return 0;
	}

	adhocWait(ADHOC_DATASIZE_STATE);
	if ((error = adhocRecvSendAck(state_buffer_base, ADHOC_STATE_SIZE, ADHOC_TIMEOUT, ADHOC_DATATYPE_STATE)) != ADHOC_STATE_SIZE)
	{
		if (error == -1)
		{
			if (Loop != LOOP_EXEC) return 1;
			goto retry;	// Data type mismatch
		}
		else if (error == (int)0x80410715)	// Timeout
		{
			if (Loop != LOOP_EXEC) return 1;
			if (--retry_count) goto retry;
		}
		return 0;
	}

	state_buffer = state_buffer_base;

	if (frame != NULL)
		*frame = *(uint32_t *)state_buffer;

	state_buffer += 4;

	state_load_memory();
	state_load_m68000();
	state_load_z80();
	state_load_input();
	state_load_timer();
	state_load_driver();
	state_load_video();

#if (EMU_SYSTEM == CPS1)
	state_load_coin();
	switch (machine_driver_type)
	{
	case MACHINE_qsound:
		state_load_qsound();
		state_load_eeprom();
		break;

	case MACHINE_pang3:
		state_load_eeprom();

	default:
		state_load_ym2151();
		break;
	}

#elif (EMU_SYSTEM == CPS2)
	state_load_coin();
	state_load_qsound();
	state_load_eeprom();

#elif (EMU_SYSTEM == MVS)
	state_load_ym2610();
	state_load_pd4990a();
#endif

	if (adhoc_server)
		option_controller = INPUT_PLAYER1;
	else
		option_controller = INPUT_PLAYER2;

	return 1;
}

#endif

#endif /* SAVE_STATE */
