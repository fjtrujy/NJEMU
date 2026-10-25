/******************************************************************************

	psvita_thread.c

	PS Vita threading primitives

******************************************************************************/

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>
#include "common/thread_driver.h"

/* Common code passes PSP-style priorities (lower is more urgent, e.g. 0x08 for
 * sound). Vita user threads only accept 64..191 and reject anything else, so
 * shift into that range keeping the order. */
#define PSVITA_THREAD_PRIORITY_HIGHEST	64
#define PSVITA_THREAD_PRIORITY_LOWEST	191

static int psvita_thread_priority(uint32_t priority)
{
	uint32_t mapped = PSVITA_THREAD_PRIORITY_HIGHEST + priority;

	return mapped > PSVITA_THREAD_PRIORITY_LOWEST ?
		PSVITA_THREAD_PRIORITY_LOWEST : (int)mapped;
}

/* The main thread (emulation and rendering) is pinned to user core 0 by the
 * platform; workers (sound, MP3) alternate between the other two cores so they
 * never preempt it. */
static int psvita_thread_cpu_mask(void)
{
	static unsigned int next_worker_core;

	return (next_worker_core++ & 1) ?
		SCE_KERNEL_CPU_MASK_USER_2 : SCE_KERNEL_CPU_MASK_USER_1;
}

typedef struct psvita_thread {
	SceUID thread_id;
	SceUID start_sema;
	SceUID end_sema;
	int32_t (*threadFunc)(uint32_t, void *);
} psvita_thread_t;

static int childThread(SceSize args, void *argp) {
	(void)args;
	int32_t res;
	psvita_thread_t *psvita = *(psvita_thread_t **)argp;
	sceKernelWaitSema(psvita->start_sema, 1, NULL);
	res = psvita->threadFunc(0, NULL);
	sceKernelSignalSema(psvita->end_sema, 1);
	return res;
}

static void *psvita_thread_init(void) {
	psvita_thread_t *psvita = (psvita_thread_t*)calloc(1, sizeof(psvita_thread_t));
	psvita->thread_id = -1;
	psvita->start_sema = -1;
	psvita->end_sema = -1;
	return psvita;
}

static void psvita_thread_free(void *data) {
	psvita_thread_t *psvita = (psvita_thread_t*)data;
	
	if (psvita->thread_id >= 0) {
		sceKernelDeleteThread(psvita->thread_id);
	}
	
	if (psvita->start_sema >= 0) {
		sceKernelDeleteSema(psvita->start_sema);
	}
	
	if (psvita->end_sema >= 0) {
		sceKernelDeleteSema(psvita->end_sema);
	}
	
	free(psvita);
}

static bool psvita_createThread(void *data, const char *name, int32_t (*threadFunc)(uint32_t, void *), uint32_t priority, uint32_t stackSize) {
	psvita_thread_t *psvita = (psvita_thread_t*)data;
	
	psvita->threadFunc = threadFunc;
	psvita->start_sema = sceKernelCreateSema("start_sema", 0, 0, 1, NULL);
	psvita->end_sema = sceKernelCreateSema("end_sema", 0, 0, 1, NULL);
	
	psvita->thread_id = sceKernelCreateThread(name, childThread,
		psvita_thread_priority(priority), stackSize, 0,
		psvita_thread_cpu_mask(), NULL);
	
	if (psvita->thread_id < 0) {
		return false;
	}
	
	sceKernelStartThread(psvita->thread_id, sizeof(void*), &psvita);
	
	return true;
}

static void psvita_startThread(void *data) {
	psvita_thread_t *psvita = (psvita_thread_t*)data;
	if (psvita->start_sema >= 0) {
		sceKernelSignalSema(psvita->start_sema, 1);
	}
}

static void psvita_waitThreadEnd(void *data) {
	psvita_thread_t *psvita = (psvita_thread_t*)data;
	if (psvita->end_sema >= 0) {
		sceKernelWaitSema(psvita->end_sema, 1, NULL);
	}
}

static void psvita_wakeupThread(void *data) {
	(void)data;
	// Not commonly used
}

static void psvita_deleteThread(void *data) {
	psvita_thread_t *psvita = (psvita_thread_t*)data;
	if (psvita->thread_id >= 0) {
		sceKernelDeleteThread(psvita->thread_id);
		psvita->thread_id = -1;
	}
}

static void psvita_resumeThread(void *data) {
	(void)data;
	// Not commonly used
}

static void psvita_suspendThread(void *data) {
	(void)data;
	// Not commonly used
}

static void psvita_sleepThread(void *data) {
	(void)data;
	// Not commonly used
}

static void psvita_yieldThread(void) {
}

static void psvita_exitThread(void *data, int32_t exitCode) {
	(void)data;
	(void)exitCode;
	// Not commonly used
}

thread_driver_t thread_psvita = {
	"psvita",
	psvita_thread_init,
	psvita_thread_free,
	psvita_createThread,
	psvita_startThread,
	psvita_waitThreadEnd,
	psvita_wakeupThread,
	psvita_deleteThread,
	psvita_resumeThread,
	psvita_suspendThread,
	psvita_sleepThread,
	psvita_yieldThread,
	psvita_exitThread,
};
