/******************************************************************************

	config.h

	Application Settings File Management

******************************************************************************/

#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

void load_gamecfg(const char *name);
void save_gamecfg(const char *name);

void load_settings(void);
void save_settings(void);

#endif /* COMMON_CONFIG_H */
