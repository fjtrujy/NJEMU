#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pspkernel.h>
#include <pspthreadman.h>
#include <me-safe-task/me-stask-mist.h>
#include <me-core-mapper/me-core-mapper.h>
#include <me-core-mapper/hw-registers.h>
#include "psp/psp_me_spsc_ring_mist_test.h"

PSP_MODULE_INFO("NJEMU ME Ring Test", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define PSP_ME_RING_HW_SYSCALL_INDEX 13
#define PSP_ME_RING_HW_LOG_PATH "host0:/njemu_me_ring_hw.log"

typedef struct psp_me_ring_hw_job
{
	void (*task)(void *);
	void *data;
	uint32_t size;
	uint32_t reserved[13];
} psp_me_ring_hw_job_t;

_Static_assert(sizeof(psp_me_ring_hw_job_t) == 64,
	"hardware test job descriptor must occupy exactly one cache line");

static psp_me_ring_hw_job_t hw_job __attribute__((aligned(64)));

static void hw_job_entry(void *param)
{
	psp_me_ring_hw_job_t *job = (psp_me_ring_hw_job_t *)param;

	meCoreDcacheInvalidateRange(job, sizeof(*job));
	meCoreDcacheInvalidateRange(job->data, job->size);
	job->task(job->data);
	meCoreDcacheWritebackRange(job->data, job->size);
}

static int hw_mist_entry(int index, void *param)
{
	(void)index;
	hw_job_entry(param);
	return meSafeTaskMistFinish();
}

static bool hw_dispatch_start(void (*task)(void *), void *data, uint32_t size,
	void *opaque)
{
	MistTrigger trigger;
	int result;

	(void)opaque;
	hw_job.task = task;
	hw_job.data = data;
	hw_job.size = size;
	sceKernelDcacheWritebackInvalidateRange(&hw_job, sizeof(hw_job));
	trigger.index = PSP_ME_RING_HW_SYSCALL_INDEX;
	trigger.param = &hw_job;
	result = meSafeTaskMistTrigger(&trigger);
	return result >= 0;
}

static void hw_dispatch_wait(void *opaque)
{
	(void)opaque;
	meSafeTaskMistWait();
}

static int hw_init_mist(void)
{
	MistInjector injector;
	int result = meSafeTaskMistInit();

	if (result < 0)
		return result;
	injector.index = PSP_ME_RING_HW_SYSCALL_INDEX;
	injector.addr = CACHED_KERNEL_MASK | (uint32_t)hw_mist_entry;
	meSafeTaskMistInjectSyscall(&injector);
	return 0;
}

static void write_result(const psp_me_spsc_ring_mist_result_t *result,
	int init_result, bool passed)
{
	char line[1024];
	uint64_t latency_ns = result->latency_us ?
		(result->latency_us * 1000ULL) / PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES : 0;
	uint64_t bulk_roundtrips_per_sec = result->bulk_us ?
		((uint64_t)PSP_ME_SPSC_RING_MIST_BULK_MESSAGES * 1000000ULL) /
			result->bulk_us : 0;
	int fd;
	int length;

	length = snprintf(line, sizeof(line),
		"[psp-me-ring-hw] passed=%d init=%d latency_messages=%u latency_us=%llu "
		"latency_avg_ns=%llu bulk_messages=%u bulk_us=%llu "
		"bulk_roundtrips_per_sec=%llu completed=%lu "
		"to_me_high_water=%lu to_me_overflow=%lu to_me_underflow=%lu "
		"to_me_seqerr=%lu to_me_corrupt=%lu "
		"to_main_high_water=%lu to_main_overflow=%lu to_main_underflow=%lu "
		"to_main_seqerr=%lu to_main_corrupt=%lu error=%lu\n",
		passed ? 1 : 0,
		init_result,
		PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES,
		(unsigned long long)result->latency_us,
		(unsigned long long)latency_ns,
		PSP_ME_SPSC_RING_MIST_BULK_MESSAGES,
		(unsigned long long)result->bulk_us,
		(unsigned long long)bulk_roundtrips_per_sec,
		(unsigned long)result->completed,
		(unsigned long)result->to_me_high_water,
		(unsigned long)result->to_me_overflow,
		(unsigned long)result->to_me_underflow,
		(unsigned long)result->to_me_sequence_errors,
		(unsigned long)result->to_me_corrupt,
		(unsigned long)result->to_main_high_water,
		(unsigned long)result->to_main_overflow,
		(unsigned long)result->to_main_underflow,
		(unsigned long)result->to_main_sequence_errors,
		(unsigned long)result->to_main_corrupt,
		(unsigned long)result->error);
	if (length <= 0 || (size_t)length >= sizeof(line))
		return;

	printf("%s", line);
	fd = open(PSP_ME_RING_HW_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
	{
		write(fd, line, (size_t)length);
		close(fd);
	}
}

int main(int argc, char *argv[])
{
	const psp_me_spsc_ring_mist_dispatch_t dispatch = {
		hw_dispatch_start,
		hw_dispatch_wait,
		NULL,
	};
	psp_me_spsc_ring_mist_result_t result = { 0 };
	bool passed = false;
	int init_result;

	(void)argc;
	(void)argv;
	remove(PSP_ME_RING_HW_LOG_PATH);
	init_result = hw_init_mist();
	if (init_result >= 0)
		passed = psp_me_spsc_ring_mist_test_run(&dispatch, &result);
	else
		result.error = PSP_ME_SPSC_RING_MIST_ERROR_DISPATCH;
	write_result(&result, init_result, passed);
	return passed ? 0 : 1;
}
