/******************************************************************************

	adhoc.c

	AdHoc 入力ポートデータ送信スレッド

******************************************************************************/

#include "emumain.h"
#include "thread_driver.h"


/******************************************************************************
	グローバル変数
******************************************************************************/

int adhoc_enable;
int adhoc_server;
char adhoc_matching[32];

ADHOC_DATA ALIGN16_DATA send_data;
ADHOC_DATA ALIGN16_DATA recv_data;
uint32_t adhoc_frame;
uint8_t adhoc_paused;
volatile int adhoc_update;


/******************************************************************************
	ローカル変数
******************************************************************************/

static void *adhoc_thread;
static volatile int adhoc_active;


/******************************************************************************
	ローカル関数
******************************************************************************/

/*------------------------------------------------------
	入力ポート送受信スレッド
------------------------------------------------------*/

static int32_t adhoc_update_inputport(uint32_t args, void *argp)
{
	int error = 0;

	adhoc_update = 0;

	while (adhoc_active)
	{
		while (!adhoc_update && adhoc_active)
		{
			usleep(1);
		}

		if (adhoc_server)
		{
			adhocSend(&send_data, sizeof(ADHOC_DATA), ADHOC_DATATYPE_INPUT);

			error = adhocRecv(&recv_data, ADHOC_TIMEOUT, ADHOC_DATATYPE_INPUT);

			if (error <= 0 || recv_data.error)
			{
				if (adhoc_send_state(&adhoc_frame))
				{
					memset(&send_data, 0, sizeof(send_data));
					memset(&recv_data, 0, sizeof(recv_data));

					memset(send_data.port_value, 0xff, sizeof(send_data.port_value));
					memset(recv_data.port_value, 0xff, sizeof(recv_data.port_value));

					send_data.loop_flag = LOOP_EXEC;
					recv_data.loop_flag = LOOP_EXEC;
				}
				else if (Loop == LOOP_EXEC)
				{
					fatalerror(TEXT(LOST_SYNC));
					Loop = LOOP_BROWSER;
				}
			}
		}
		else
		{
			error = adhocRecv(&recv_data, ADHOC_TIMEOUT, ADHOC_DATATYPE_INPUT);

			if (error <= 0 || recv_data.frame != send_data.frame)
			{
				send_data.error = 1;
				adhocSend(&send_data, sizeof(ADHOC_DATA), ADHOC_DATATYPE_INPUT);

				if (adhoc_recv_state(&adhoc_frame))
				{
					memset(&send_data, 0, sizeof(send_data));
					memset(&recv_data, 0, sizeof(recv_data));

					memset(send_data.port_value, 0xff, sizeof(send_data.port_value));
					memset(recv_data.port_value, 0xff, sizeof(recv_data.port_value));

					send_data.loop_flag = LOOP_EXEC;
					recv_data.loop_flag = LOOP_EXEC;
				}
				else if (Loop == LOOP_EXEC)
				{
					fatalerror(TEXT(LOST_SYNC));
					Loop = LOOP_BROWSER;
				}
			}
			else
			{
				adhocSend(&send_data, sizeof(ADHOC_DATA), ADHOC_DATATYPE_INPUT);
			}
		}

		adhoc_update = 0;

		usleep(100);
	}

	thread_driver->exitThread(adhoc_thread, 0);

	return 0;
}


/******************************************************************************
	入力ポートインタフェース関数
******************************************************************************/

/*------------------------------------------------------
	スレッド開始
------------------------------------------------------*/

int adhoc_start_thread(void)
{
	bool created = false;
	adhoc_update = 0;
	adhoc_active = 0;
	adhoc_paused = 0;
	adhoc_thread = thread_driver->init();
	created = thread_driver->createThread(adhoc_thread, "Input thread", adhoc_update_inputport, 0x10, 0x1000);

	if (!created)
	{
		thread_driver->free(adhoc_thread);
		adhoc_thread = NULL;
		return 0;
	}

	return 1;
}

/*------------------------------------------------------
	スレッド終了
------------------------------------------------------*/

void adhoc_stop_thread(void)
{
	if (adhoc_thread)
	{
		adhoc_active = 0;
		thread_driver->waitThreadEnd(adhoc_thread);
		thread_driver->deleteThread(adhoc_thread);
		thread_driver->free(adhoc_thread);
		adhoc_thread = NULL;
	}
}


/*------------------------------------------------------
	スレッドをリセット
------------------------------------------------------*/

void adhoc_reset_thread(void)
{
	if (adhoc_thread)
	{
		if (adhoc_active)
		{
			adhoc_active = 0;
			thread_driver->waitThreadEnd(adhoc_thread);
		}

		memset(&send_data, 0, sizeof(send_data));
		memset(&recv_data, 0, sizeof(recv_data));

		memset(send_data.port_value, 0xff, sizeof(send_data.port_value));
		memset(recv_data.port_value, 0xff, sizeof(recv_data.port_value));

		send_data.loop_flag = LOOP_EXEC;
		recv_data.loop_flag = LOOP_EXEC;

		adhoc_frame = 0;

		adhoc_active = 1;
		thread_driver->startThread(adhoc_thread);
	}
}


/*------------------------------------------------------
	ポーズ
------------------------------------------------------*/

void adhoc_pause(void)
{
	int control, sel = 0;
	uint32_t buttons;
	char buf[64];
	RECT rect = { 140-8, 96-8, 340+8, 176+8 };
#if !ADHOC_UPDATE_EVERY_FRAME
	int frame = 0;
#endif

	if ((adhoc_server && adhoc_paused == 2) || (!adhoc_server && adhoc_paused == 1))
		control = 1;
	else
		control = 0;

	sound_thread_enable(0);

	video_driver->copyRect(video_data, show_frame, work_frame, &rect, &rect);

	do
	{
#if !ADHOC_UPDATE_EVERY_FRAME
		if (frame & 1)
		{
			frame++;
		}
		else
#endif
		{
			while (adhoc_update)
			{
				usleep(1);
			}

			if (control)
			{
				buttons = send_data.buttons;
				send_data.buttons = 0;
			}
			else
			{
				buttons = recv_data.buttons;
				recv_data.buttons = 0;
			}

			if (buttons & PLATFORM_PAD_UP)
			{
				sel = 0;
			}
			else if (buttons & PLATFORM_PAD_DOWN)
			{
				sel = 1;
			}
			else if (buttons & PLATFORM_PAD_B1)
			{
				adhoc_paused = 0;
				if (sel == 1) Loop = LOOP_BROWSER;
			}

			video_driver->transferWorkFrame(video_data, &rect, &rect);

			draw_dialog(140, 96, 340, 176);

			sprintf(buf, TEXT(PAUSED_BY_x), (adhoc_paused == 2) ? TEXT(SERVER) : TEXT(CLIENT));
			uifont_print_center(106, UI_COLOR(UI_PAL_INFO), buf);

			if (sel == 0)
			{
				uifont_print_center(132, COLOR_WHITE, TEXT(RETURN_TO_GAME));
				uifont_print_center(150, COLOR_GRAY, TEXT(DISCONNECT2));
			}
			else
			{
				uifont_print_center(132, COLOR_GRAY, TEXT(RETURN_TO_GAME));
				uifont_print_center(150, COLOR_WHITE, TEXT(DISCONNECT2));
			}

			video_driver->waitVsync(video_data);
			video_driver->copyRect(video_data, draw_frame, show_frame, &rect, &rect);

			buttons = poll_gamepad();

			send_data.buttons   = buttons;
			send_data.paused    = adhoc_paused;
			send_data.loop_flag = Loop;
			send_data.frame     = 0;

#if !ADHOC_UPDATE_EVERY_FRAME
			frame++;
#endif

			usleep(100);

			adhoc_update = 1;
		}

	} while (adhoc_paused);

	autoframeskip_reset();
	sound_thread_enable(1);
}
