#include <stdio.h>
#include <stdlib.h>
#include <kernel.h>
#include "common/thread_driver.h"

typedef struct ps2_thread {
	int32_t threadId;
	void *endfunc;
	void (*threadFunc)(void *);
} ps2_thread_t;

static void FinishThread(ps2_thread_t *ps2)
{
    ee_thread_status_t info;
    int res;

    res = ReferThreadStatus(ps2->threadId, &info);
    TerminateThread(ps2->threadId);
    DeleteThread(ps2->threadId);
    DeleteSema((int)ps2->endfunc);

    if (res > 0) {
        free(info.stack);
    }
}

static int childThread(void *arg)
{
    ps2_thread_t *ps2 = (ps2_thread_t *)arg;
    ps2->threadFunc(NULL);
    SignalSema((int)ps2->endfunc);
    return 0;
}

static void *ps2_init(void) {
	ps2_thread_t *ps2 = (ps2_thread_t*)calloc(1, sizeof(ps2_thread_t));
	return ps2;
}

static void ps2_free(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	free(ps2);
}

static bool ps2_createThread(void *data, const char *name, void (*threadFunc)(void *), uint32_t priority, uint32_t stackSize) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;

	ee_thread_t eethread;
	ee_sema_t sema;

	/* Create EE Thread */
    eethread.attr = 0;
    eethread.option = 0;
    eethread.func = &childThread;
    eethread.stack = malloc(stackSize);
    eethread.stack_size = stackSize;
    eethread.gp_reg = &_gp;
    eethread.initial_priority = priority;
    ps2->threadId = CreateThread(&eethread);

	// Prepare el semaphore for the ending function
    sema.init_count = 0;
    sema.max_count = 1;
    sema.option = 0;
    ps2->endfunc = (void *)CreateSema(&sema);

	ps2->threadFunc = threadFunc;

	return ps2->threadId >= 0 && ps2->endfunc >= 0;
}

static void ps2_startThread(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;

	StartThread(ps2->threadId, ps2);
}

static void ps2_waitThreadEnd(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	WaitSema((int)ps2->endfunc);
    ReleaseWaitThread(ps2->threadId);
    FinishThread(ps2);
}

static void ps2_wakeupThread(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	WakeupThread(ps2->threadId);
}

static void ps2_deleteThread(void *data) {
	ps2_thread_t *ps2 = (ps2_thread_t*)data;
	DeleteThread(ps2->threadId);
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
	ExitThread();
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