/******************************************************************************

	translation.h

	Runtime language selection for the ROM converter.

******************************************************************************/

#ifndef ROMCNV_TRANSLATION_H
#define ROMCNV_TRANSLATION_H

typedef enum romcnv_text_id
{
#define ROMCNV_TEXT_ID(name, value) ROMCNV_TEXT_##name = value,
#include "../../translations/romcnv/messages.def"
#undef ROMCNV_TEXT_ID
	ROMCNV_TEXT_COUNT
} romcnv_text_id_t;

void romcnv_translation_init(int argc, char *argv[]);
int romcnv_translation_option_span(int argc, char *argv[], int index);
const char *romcnv_translation_get(romcnv_text_id_t id);

#define ROMCNV_TEXT(name) romcnv_translation_get(ROMCNV_TEXT_##name)

#endif /* ROMCNV_TRANSLATION_H */
