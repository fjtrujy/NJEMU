#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <timer.h>

#include "common/ticker_driver.h"

typedef struct ps2_ticker {
} ps2_ticker_t;

static void *ps2_init(void) {
	ps2_ticker_t *ps2 = (ps2_ticker_t*)calloc(1, sizeof(ps2_ticker_t));
	return ps2;
}

static void ps2_free(void *data) {
	ps2_ticker_t *ps2 = (ps2_ticker_t*)data;
	free(ps2);
}

static uint64_t ps2_currentUs(void *data) {
	u32 seconds;
	u32 microseconds;

	(void)data;
	TimerBusClock2USec(GetTimerSystemTime(), &seconds, &microseconds);
	return (uint64_t)seconds * 1000000ULL + microseconds;
}

ticker_driver_t ticker_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_currentUs,
};
