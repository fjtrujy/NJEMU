#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pspkernel.h>
#include <me-safe-task/me-stask-mist.h>
#include <me-core-mapper/hw-registers.h>

#include "common/okim6295_job.h"
#include "common/qsound_mix_job.h"
#include "common/ym2610_adpcma_job.h"

PSP_MODULE_INFO("NJEMU ME Audio Jobs", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define PSP_ME_AUDIO_JOBS_HW_SYSCALL_INDEX 13
#define PSP_ME_AUDIO_JOBS_HW_LOG_PATH "host0:/njemu_me_audio_jobs_hw.log"

typedef struct psp_me_audio_jobs_hw_dispatch
{
	void (*task)(void *);
	void *data;
	uint32_t size;
	uint32_t reserved[13];
} psp_me_audio_jobs_hw_dispatch_t;

_Static_assert(sizeof(psp_me_audio_jobs_hw_dispatch_t) == 64,
	"hardware audio job descriptor must occupy exactly one cache line");

static psp_me_audio_jobs_hw_dispatch_t hw_dispatch __attribute__((aligned(64)));
static qsound_mix_job_t qsound_job __attribute__((aligned(64)));
static okim6295_job_t okim_job __attribute__((aligned(64)));
static ym2610_adpcma_job_t ym2610_job __attribute__((aligned(64)));

static void hw_job_entry(void *param)
{
	psp_me_audio_jobs_hw_dispatch_t *dispatch =
		(psp_me_audio_jobs_hw_dispatch_t *)param;

	meCoreDcacheInvalidateRange(dispatch, sizeof(*dispatch));
	meCoreDcacheInvalidateRange(dispatch->data, dispatch->size);
	dispatch->task(dispatch->data);
	meCoreDcacheWritebackRange(dispatch->data, dispatch->size);
}

static int hw_mist_entry(int index, void *param)
{
	(void)index;
	hw_job_entry(param);
	return meSafeTaskMistFinish();
}

static int hw_init_mist(void)
{
	MistInjector injector;
	int result = meSafeTaskMistInit();

	if (result < 0)
		return result;
	injector.index = PSP_ME_AUDIO_JOBS_HW_SYSCALL_INDEX;
	injector.addr = CACHED_KERNEL_MASK | (uint32_t)hw_mist_entry;
	meSafeTaskMistInjectSyscall(&injector);
	return 0;
}

static bool hw_run(void (*task)(void *), void *data, uint32_t size)
{
	MistTrigger trigger;

	hw_dispatch.task = task;
	hw_dispatch.data = data;
	hw_dispatch.size = size;
	sceKernelDcacheWritebackInvalidateRange(&hw_dispatch, sizeof(hw_dispatch));
	sceKernelDcacheWritebackInvalidateRange(data, size);
	trigger.index = PSP_ME_AUDIO_JOBS_HW_SYSCALL_INDEX;
	trigger.param = &hw_dispatch;
	if (meSafeTaskMistTrigger(&trigger) < 0)
		return false;
	meSafeTaskMistWait();
	sceKernelDcacheInvalidateRange(data, size);
	return true;
}

static bool test_qsound(void)
{
	memset(&qsound_job, 0, sizeof(qsound_job));
	qsound_job.samples = 3;
	qsound_job.channel[0].left_gain = 128;
	qsound_job.channel[0].right_gain = 64;
	qsound_job.channel[0].sample[0] = 1;
	qsound_job.channel[0].sample[1] = -2;
	qsound_job.channel[0].sample[2] = 3;

	if (!hw_run(qsound_mix_job_run, &qsound_job, sizeof(qsound_job)))
		return false;
	return !qsound_job.error &&
		qsound_job.left[0] == 2 && qsound_job.left[1] == -4 &&
		qsound_job.left[2] == 6 && qsound_job.right[0] == 1 &&
		qsound_job.right[1] == -2 && qsound_job.right[2] == 3;
}

static bool test_okim6295(void)
{
	memset(&okim_job, 0, sizeof(okim_job));
	okim_job.samples = 2;
	okim_job.source_step = 1 << 12;
	okim_job.stream_pos = 1 << 12;
	okim_job.status = 1;
	okim_job.diff_lookup[1] = 10;
	okim_job.voice[0].count = 2;
	okim_job.voice[0].signal = -2;
	okim_job.voice[0].volume = 256;
	okim_job.voice[0].source_size = 1;
	okim_job.voice[0].source[0] = 0x11;

	if (!hw_run(okim6295_job_run, &okim_job, sizeof(okim_job)))
		return false;
	return !okim_job.error && okim_job.output[0] == 0 &&
		okim_job.output[1] == 128 && okim_job.voice[0].offset == 2 &&
		okim_job.voice[0].count == 0 && okim_job.voice[0].signal == 18 &&
		okim_job.voice[0].step == 0 && okim_job.stream_pos == (1 << 12) &&
		okim_job.prev_sample == 128 && okim_job.curr_sample == 288 &&
		okim_job.status == 1;
}

static bool test_ym2610_adpcma(void)
{
	ym2610_adpcma_channel_job_t *channel;

	memset(&ym2610_job, 0, sizeof(ym2610_job));
	ym2610_job.samples = 2;
	ym2610_job.steps[0] = 16;
	ym2610_job.step_inc[0] = -16;
	ym2610_job.step_inc[1] = -16;
	channel = &ym2610_job.channel[0];
	channel->flag = 1;
	channel->flag_mask = 1;
	channel->pan = YM2610_ADPCMA_PAN_CENTER;
	channel->step = 1u << 16;
	channel->end = 8;
	channel->vol_mul = 8;
	channel->vol_shift = 1;
	channel->source_size = 1;
	channel->source[0] = 0x11;

	if (!hw_run(ym2610_adpcma_job_run, &ym2610_job, sizeof(ym2610_job)))
		return false;
	return !ym2610_job.error && ym2610_job.ended_mask == 0 &&
		ym2610_job.left[0] == 24 && ym2610_job.right[0] == 24 &&
		ym2610_job.left[1] == 48 && ym2610_job.right[1] == 48 &&
		channel->now_addr == 2 && channel->now_step == 0 &&
		channel->adpcma_acc == 12 && channel->adpcma_step == 0 &&
		channel->adpcma_out == 48;
}

static void write_result(int init_result, bool qsound_passed, bool okim_passed,
	bool ym2610_passed)
{
	char line[256];
	int fd;
	int length = snprintf(line, sizeof(line),
		"[psp-me-audio-jobs-hw] passed=%d init=%d qsound=%d okim6295=%d ym2610_adpcma=%d\n",
		(init_result >= 0 && qsound_passed && okim_passed && ym2610_passed) ? 1 : 0,
		init_result, qsound_passed ? 1 : 0, okim_passed ? 1 : 0,
		ym2610_passed ? 1 : 0);

	if (length <= 0 || (size_t)length >= sizeof(line))
		return;
	printf("%s", line);
	fd = open(PSP_ME_AUDIO_JOBS_HW_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
	{
		(void)write(fd, line, (size_t)length);
		close(fd);
	}
}

int main(int argc, char *argv[])
{
	bool qsound_passed = false;
	bool okim_passed = false;
	bool ym2610_passed = false;
	int init_result;

	(void)argc;
	(void)argv;
	remove(PSP_ME_AUDIO_JOBS_HW_LOG_PATH);
	init_result = hw_init_mist();
	if (init_result >= 0)
	{
		qsound_passed = test_qsound();
		okim_passed = test_okim6295();
		ym2610_passed = test_ym2610_adpcma();
	}
	write_result(init_result, qsound_passed, okim_passed, ym2610_passed);
	return init_result >= 0 && qsound_passed && okim_passed && ym2610_passed ? 0 : 1;
}
