/******************************************************************************

	ui_text_ids.h

	Stable, build-independent user-interface text IDs.

******************************************************************************/

#ifndef UI_TEXT_IDS_H
#define UI_TEXT_IDS_H

#define UI_TEXT_SCHEMA_HASH 0x1ed49de8u

typedef enum ui_text_id
{
#define UI_TEXT_ID(name, value) name = value,
#include "../../translations/messages.def"
#undef UI_TEXT_ID
	UI_TEXT_MAX
} ui_text_id_t;

#endif /* UI_TEXT_IDS_H */
