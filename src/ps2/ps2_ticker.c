#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <timer.h>
#include <ps2sdkapi.h>
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

static TICKER ps2_ticker(void *data) {
	uint64_t current_ticks;


	current_ticks = ps2_clock();
	return current_ticks;
}

ticker_driver_t ticker_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_ticker,
};