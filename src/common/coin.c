/******************************************************************************

	coin.c

	Coin Counter

******************************************************************************/

#include "emumain.h"


/******************************************************************************
	Local Variables
******************************************************************************/

#define COIN_COUNTERS	4

static uint32_t coins[COIN_COUNTERS];
static uint32_t lastcoin[COIN_COUNTERS];
static uint32_t coinlockedout[COIN_COUNTERS];


/******************************************************************************
	Global Functions
******************************************************************************/

/*--------------------------------------------------------
	Coin Counter Reset
--------------------------------------------------------*/

void coin_counter_reset(void)
{
	memset(coins, 0, sizeof(coins));
	memset(lastcoin, 0, sizeof(lastcoin));
	memset(coinlockedout, 0, sizeof(coinlockedout));
}


/*--------------------------------------------------------
	Coin Counter Update
--------------------------------------------------------*/

void coin_counter_w(int num, int on)
{
	if (num >= COIN_COUNTERS) return;

	if (on && (lastcoin[num] == 0))
	{
		coins[num]++;
	}
	lastcoin[num] = on;
}


/*--------------------------------------------------------
	Coin Counter Lock
--------------------------------------------------------*/

void coin_lockout_w(int num, int on)
{
	if (num >= COIN_COUNTERS) return;

	coinlockedout[num] = on;
}


/*--------------------------------------------------------
	Save/Load State
--------------------------------------------------------*/

#ifdef SAVE_STATE

STATE_SAVE( coin )
{
	state_save_long(coins, COIN_COUNTERS);
	state_save_long(lastcoin, COIN_COUNTERS);
	state_save_long(coinlockedout, COIN_COUNTERS);
}

STATE_LOAD( coin )
{
	state_load_long(coins, COIN_COUNTERS);
	state_load_long(lastcoin, COIN_COUNTERS);
	state_load_long(coinlockedout, COIN_COUNTERS);
}

#endif /* SAVE_STATE */
