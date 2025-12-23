#include <stdio.h>
#include <stdlib.h>
#include "common/thread_driver.h"

#include <SDL.h>

typedef struct desktop_thread {
    SDL_Thread *thread;
    SDL_sem *start;
    SDL_sem *end;
    int32_t (*threadFunc)(uint32_t, void *);
} desktop_thread_t;


static int childThread(void *arg)
{
    int32_t res;
    desktop_thread_t *desktop = (desktop_thread_t *)arg;
    SDL_SemWait(desktop->start);
    res = desktop->threadFunc(0, NULL);
    SDL_SemPost(desktop->end);
    return res;
}

static void *desktop_init(void) {
	desktop_thread_t *desktop = (desktop_thread_t*)calloc(1, sizeof(desktop_thread_t));
	return desktop;
}

static void desktop_free(void *data) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
	free(desktop);
}

static bool desktop_createThread(void *data, const char *name, int32_t (*threadFunc)(u_int32_t, void *), uint32_t priority, uint32_t stackSize) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
    desktop->threadFunc = threadFunc;
    desktop->start = SDL_CreateSemaphore(0);
    desktop->end = SDL_CreateSemaphore(0);
    desktop->thread = SDL_CreateThreadWithStackSize(childThread, name, stackSize, desktop);

	return desktop->thread != NULL;
}

static void desktop_startThread(void *data) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
    SDL_SemPost(desktop->start);
}

static void desktop_waitThreadEnd(void *data) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
    SDL_SemPost(desktop->end);
}

static void desktop_wakeupThread(void *data) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
}

static void desktop_deleteThread(void *data) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
}

static void desktop_resumeThread(void *data) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
}

static void desktop_suspendThread(void *data) {
	desktop_thread_t *desktop = (desktop_thread_t*)data;
}

static void desktop_sleepThread(void *data) {
}

static void desktop_exitThread(void *data, int32_t exitCode) {
}

thread_driver_t thread_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_createThread,
	desktop_startThread,
	desktop_waitThreadEnd,
	desktop_wakeupThread,
	desktop_deleteThread,
	desktop_resumeThread,
	desktop_suspendThread,
	desktop_sleepThread,
	desktop_exitThread
};
