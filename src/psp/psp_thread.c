#include <stdio.h>
#include <stdlib.h>
#include <pspthreadman.h>
#include "common/thread_driver.h"

typedef struct psp_thread {
	SceUID threadId;
} psp_thread_t;

static void *psp_init(void) {
	psp_thread_t *psp = (psp_thread_t*)calloc(1, sizeof(psp_thread_t));
	return psp;
}

static void psp_free(void *data) {
	psp_thread_t *psp = (psp_thread_t*)data;
	free(psp);
}

static bool psp_createThread(void *data, const char *name, int32_t (*threadFunc)(uint32_t, void *), uint32_t priority, uint32_t stackSize) {
	psp_thread_t *psp = (psp_thread_t*)data;
	psp->threadId = sceKernelCreateThread(name, (SceKernelThreadEntry)threadFunc, priority, stackSize, 0, NULL);
	return psp->threadId >= 0;
}

static void psp_startThread(void *data) {
	psp_thread_t *psp = (psp_thread_t*)data;
	sceKernelStartThread(psp->threadId, 0, NULL);
}

static void psp_waitThreadEnd(void *data) {
	psp_thread_t *psp = (psp_thread_t*)data;
	sceKernelWaitThreadEnd(psp->threadId, NULL);
}

static void psp_wakeupThread(void *data) {
	psp_thread_t *psp = (psp_thread_t*)data;
	sceKernelWakeupThread(psp->threadId);
}

static void psp_deleteThread(void *data) {
	psp_thread_t *psp = (psp_thread_t*)data;
	sceKernelDeleteThread(psp->threadId);
}

static void psp_resumeThread(void *data) {
	psp_thread_t *psp = (psp_thread_t*)data;
	sceKernelResumeThread(psp->threadId);
}

static void psp_suspendThread(void *data) {
	psp_thread_t *psp = (psp_thread_t*)data;
	sceKernelSuspendThread(psp->threadId);
}

static void psp_sleepThread(void *data) {
	(void)data;
	sceKernelSleepThread();
}

static void psp_yieldThread(void) {
	int priority = sceKernelGetThreadCurrentPriority();

	if (priority >= 0)
		sceKernelRotateThreadReadyQueue(priority);
}

static void psp_exitThread(void *data, int32_t exitCode) {
	(void)data;
	sceKernelExitThread(exitCode);
}

thread_driver_t thread_psp = {
	"psp",
	psp_init,
	psp_free,
	psp_createThread,
	psp_startThread,
	psp_waitThreadEnd,
	psp_wakeupThread,
	psp_deleteThread,
	psp_resumeThread,
	psp_suspendThread,
	psp_sleepThread,
	psp_yieldThread,
	psp_exitThread
};
