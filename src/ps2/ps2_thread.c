#include <stdio.h>
#include <stdlib.h>
#include <kernel.h>
#include "common/thread_driver.h"

typedef struct ps2_thread {
	int32_t threadId;
	int32_t endSemaId;
	void *stack;
	int32_t (*threadFunc)(uint32_t, void *);
} ps2_thread_t;

static void cleanupThread(ps2_thread_t *ps2)
{
	if (ps2->threadId >= 0) {
		DeleteThread(ps2->threadId);
		ps2->threadId = -1;
	}

	if (ps2->endSemaId >= 0) {
		DeleteSema(ps2->endSemaId);
		ps2->endSemaId = -1;
	}

	free(ps2->stack);
	ps2->stack = NULL;
	ps2->threadFunc = NULL;
}

static int childThread(void *arg)
{
	ps2_thread_t *ps2 = (ps2_thread_t *)arg;
	int32_t result = ps2->threadFunc(0, NULL);

	SignalSema(ps2->endSemaId);
	ExitThread();
	return result;
}

static void *ps2_init(void)
{
	ps2_thread_t *ps2 = (ps2_thread_t*)calloc(1, sizeof(ps2_thread_t));

	if (ps2 != NULL) {
		ps2->threadId = -1;
		ps2->endSemaId = -1;
	}

	return ps2;
}

static void ps2_free(void *data)
{
	ps2_thread_t *ps2 = (ps2_thread_t*)data;

	if (ps2 == NULL)
		return;

	cleanupThread(ps2);
	free(ps2);
}

static bool ps2_createThread(void *data, const char *name, int32_t (*threadFunc)(uint32_t, void *), uint32_t priority, uint32_t stackSize)
{
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	ee_thread_t eethread = {0};
	ee_sema_t sema = {0};

	(void)name;

	if (ps2 == NULL || threadFunc == NULL || stackSize == 0)
		return false;

	ps2->stack = malloc(stackSize);
	if (ps2->stack == NULL)
		return false;

	ps2->threadFunc = threadFunc;

	/* Create EE Thread */
	eethread.attr = 0;
	eethread.option = 0;
	eethread.func = &childThread;
	eethread.stack = ps2->stack;
	eethread.stack_size = stackSize;
	eethread.gp_reg = &_gp;
	eethread.initial_priority = priority;
	ps2->threadId = CreateThread(&eethread);
	if (ps2->threadId < 0) {
		cleanupThread(ps2);
		return false;
	}

	/* The completion semaphore lets waitThreadEnd() wait without taking
	 * ownership of the EE thread object. deleteThread() performs cleanup. */
	sema.init_count = 0;
	sema.max_count = 1;
	sema.option = 0;
	ps2->endSemaId = CreateSema(&sema);
	if (ps2->endSemaId < 0) {
		cleanupThread(ps2);
		return false;
	}

	return true;
}

static void ps2_startThread(void *data)
{
	ps2_thread_t *ps2 = (ps2_thread_t*)data;

	if (ps2 != NULL && ps2->threadId >= 0)
		StartThread(ps2->threadId, ps2);
}

static void ps2_waitThreadEnd(void *data)
{
	ps2_thread_t *ps2 = (ps2_thread_t*)data;

	if (ps2 != NULL && ps2->endSemaId >= 0)
		WaitSema(ps2->endSemaId);
}

static void ps2_wakeupThread(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	WakeupThread(ps2->threadId);
}

static void ps2_deleteThread(void *data)
{
	ps2_thread_t *ps2 = (ps2_thread_t*)data;

	if (ps2 != NULL)
		cleanupThread(ps2);
}

static void ps2_resumeThread(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	ResumeThread(ps2->threadId);
}

static void ps2_suspendThread(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	SuspendThread(ps2->threadId);
}

static void ps2_sleepThread(void *data) {
	SleepThread();
}

static void ps2_exitThread(void *data, int32_t exitCode) {
	/* The wrapper must signal endSemaId before exiting, so the common
	 * exitThread() hook intentionally does not call ExitThread() directly. */
	(void)data;
	(void)exitCode;
}

thread_driver_t thread_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_createThread,
	ps2_startThread,
	ps2_waitThreadEnd,
	ps2_wakeupThread,
	ps2_deleteThread,
	ps2_resumeThread,
	ps2_suspendThread,
	ps2_sleepThread,
	ps2_exitThread
};
