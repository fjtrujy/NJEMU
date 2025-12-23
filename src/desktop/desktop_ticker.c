#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <timer.h>
#include "common/ticker_driver.h"

typedef struct desktop_ticker {
} desktop_ticker_t;

static void *desktop_init(void) {
	desktop_ticker_t *desktop = (desktop_ticker_t*)calloc(1, sizeof(desktop_ticker_t));
	return desktop;
}

static void desktop_free(void *data) {
	desktop_ticker_t *desktop = (desktop_ticker_t*)data;
	free(desktop);
}

static u_int64_t desktop_currentUs(void *data) {
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    return (start.tv_sec * 1000000) + (start.tv_nsec / 1000);
}

ticker_driver_t ticker_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_currentUs,
};