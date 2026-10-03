#ifndef COMMON_DIP_MENU_H
#define COMMON_DIP_MENU_H

#include <stdint.h>

#define MAX_DIPSWITCHS 32

typedef struct dipswitch
{
	const char *label;
	uint8_t enable;
	uint8_t mask;
	uint8_t value;
	uint8_t value_max;
	const char *values_label[MAX_DIPSWITCHS + 1];
} dipswitch_t;

#endif /* COMMON_DIP_MENU_H */
