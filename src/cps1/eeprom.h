/******************************************************************************

	eeprom.c

	EEPROMインタフェース関数

******************************************************************************/

#ifndef EEPROM_H
#define EEPROM_H

struct EEPROM_interface
{
	int address_bits;	/* EEPROM has 2^address_bits cells */
	int data_bits;		/* every cell has this many bits (8 or 16) */
	const char *cmd_read;	/*   read command string, e.g. "0110" */
	const char *cmd_write;	/*  write command string, e.g. "0111" */
	const char *cmd_erase;	/*  erase command string, or 0 if n/a */
	const char *cmd_lock;	/*   lock command string, or 0 if n/a */
	const char *cmd_unlock;	/* unlock command string, or 0 if n/a */
	int enable_multi_read;/* set to 1 to enable multiple values to be read from one read command */
	int reset_delay;	/* number of times EEPROM_read_bit() should return 0 after a reset, */
						/* before starting to return 1. */
};


void EEPROM_init(struct EEPROM_interface *interface);

void EEPROM_write_bit(int bit);
int EEPROM_read_bit(void);
void EEPROM_set_cs_line(int state);
void EEPROM_set_clock_line(int state);

void EEPROM_load(FILE *file);
void EEPROM_save(FILE *file);

uint8_t EEPROM_read_data(uint32_t address);
void EEPROM_write_data(uint32_t address, uint8_t data);

#ifdef SAVE_STATE
STATE_SAVE( eeprom );
STATE_LOAD( eeprom );
#endif

#endif /* EEPROM_H */
