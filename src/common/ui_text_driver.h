/******************************************************************************

	ui_text_driver.h

******************************************************************************/

#ifndef UI_TEXT_DRIVER_H
#define UI_TEXT_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "ui_language.h"
#include "ui_text_ids.h"

#define TEXT(s)		ui_text_driver->getText(ui_text_data, s)

typedef struct ui_text_driver
{
	/* Human-readable identifier. */
	const char *ident;
	/* Creates and initializes handle to ui_text driver.
	*
	* Returns: ui_text driver handle on success, otherwise NULL.
	**/
	void *(*init)(void);
	/* Stops and frees driver data. */
   	void (*free)(void *data);
	ui_language_t (*getLanguage)(void *data);
	const char *(*getText)(void *data, ui_text_id_t id);

} ui_text_driver_t;


extern ui_text_driver_t ui_text_psp;
extern ui_text_driver_t ui_text_ps2;
extern ui_text_driver_t ui_text_desktop;
extern ui_text_driver_t ui_text_null;

extern ui_text_driver_t *ui_text_drivers[];

#define ui_text_driver ui_text_drivers[0]

extern void *ui_text_data;

#endif /* UI_TEXT_DRIVER_H */
