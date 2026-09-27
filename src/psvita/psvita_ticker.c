/******************************************************************************

	psvita_ticker.c

	PS Vita timing and vsync control

******************************************************************************/

#include <psp2/kernel/processmgr.h>
#include <stdlib.h>
#include "common/ticker_driver.h"

typedef struct psvita_ticker {
} psvita_ticker_t;

static void *psvita_ticker_init(void) {
	psvita_ticker_t *psvita = (psvita_ticker_t*)calloc(1, sizeof(psvita_ticker_t));
	return psvita;
}

static void psvita_ticker_free(void *data) {
	psvita_ticker_t *psvita = (psvita_ticker_t*)data;
	free(psvita);
}

static uint64_t psvita_currentUs(void *data) {
	return sceKernelGetProcessTimeWide();
}

ticker_driver_t ticker_psvita = {
	"psvita",
	psvita_ticker_init,
	psvita_ticker_free,
	psvita_currentUs,
};
