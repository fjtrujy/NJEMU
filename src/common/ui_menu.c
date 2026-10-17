/******************************************************************************

	ui_menu.c

	メニュー

******************************************************************************/

#include <limits.h>
#include <stdint.h>
#include "emucfg.h"
#include "common/cache.h"
#ifdef COMMAND_LIST
#include "common/cmdlist.h"
#endif
#include "common/config.h"
#include "common/dip_menu.h"
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/cps_orientation.h"
#endif
#include "common/emulator_options.h"
#ifdef PS2
#include "common/display_mode.h"
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "ps2/ps2_native_240p_policy.h"
#endif
#endif
#include "common/emulator_runtime.h"
#include "common/input_driver.h"
#include "common/filer.h"
#include "common/power_driver.h"
#include "common/runtime_paths.h"
#include "common/path_utils.h"
#include "common/sound.h"
#include "common/state.h"
#include "common/ui_defs.h"
#include "common/ui_text_driver.h"
#include "common/video_driver.h"
#include "common/ui_menu.h"
#include "sound/sndintrf.h"
#if (EMU_SYSTEM == NCDZ)
#include "common/mp3.h"
#include "ncdz/cdda.h"
#endif
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/ui.h"
#include "common/ui_draw.h"
#include "common/ui_layout.h"
#include "common/icon/menu_extra.h"
#include "common/menu/section_rules.h"

/* The menu definitions below are target-domain code compiled into this common
 * UI shell.  Depend on the selected target explicitly instead of inheriting
 * its declarations transitively through emumain.h. */
#if (EMU_SYSTEM == CPS1)
#include "cps1/cps1.h"
#elif (EMU_SYSTEM == CPS2)
#include "cps2/cps2.h"
#elif (EMU_SYSTEM == MVS)
#include "mvs/mvs.h"
#elif (EMU_SYSTEM == NCDZ)
#include "ncdz/ncdz.h"
#endif

#define MENU_BLANK	{ .label = LF }
#define MENU_BLANK_POWER(capability)	{ .label = LF, .required_power_capabilities = (capability) }
#define MENU_RETURN	{ .label = RETURN_TO_MAIN_MENU }
#define MENU_END	{ .label = EOM }

/*------------------------------------------------------
	DAVEX: VARIABLES GLOBALES DE CHEATS
------------------------------------------------------*/
int cheat_num = 0;
gamecheat_t* gamecheat[MAX_CHEATS];


/*------------------------------------------------------
	エミュレーションリセット
------------------------------------------------------*/

static int menu_reset(void)
{
	if (messagebox(MB_RESETEMULATION))
	{
		video_driver->beginFrame(video_data);
		video_driver->clearScreen(video_data);
		video_driver->endFrame(video_data);
		video_driver->flipScreen(video_data, 1);
		cheat_num = 0; 			//disable cheat
		Loop = LOOP_RESET;
		Sleep = 0;
		return 1;
	}
	return 0;
}


/*------------------------------------------------------
	エミュレーション再起動
------------------------------------------------------*/
#if (EMU_SYSTEM != CPS2 && EMU_SYSTEM != NCDZ)
static int menu_resetdip(void)
{
	if (messagebox(MB_RESTARTEMULATION))
	{
		video_driver->beginFrame(video_data);
		video_driver->clearScreen(video_data);
		video_driver->endFrame(video_data);
		video_driver->flipScreen(video_data, 1);
		cheat_num = 0; 			//disable cheat
		Loop = LOOP_RESET;		//dip no need to restart
//		Loop = LOOP_RESTART;
		Sleep = 0;
	}
	return 1;
}
#endif

static int menu_restart(void)
{
	if (messagebox(MB_RESTARTEMULATION))
	{
		video_driver->beginFrame(video_data);
		video_driver->clearScreen(video_data);
		video_driver->endFrame(video_data);
		video_driver->flipScreen(video_data, 1);
		cheat_num = 0; 			//disable cheat
		Loop = LOOP_RESTART;
		Sleep = 0;
	}
	return 1;
}


/*------------------------------------------------------
	ファイルブラウザに戻る
------------------------------------------------------*/

static int menu_browser(void)
{
	if (messagebox(MB_RETURNTOFILEBROWSER))
	{
	cheat_num = 0;  			//disable cheat
	Loop = LOOP_BROWSER;
	Sleep = 0;
	return 1;
	}
	return 0;
}


/*------------------------------------------------------
	エミュレータ終了
------------------------------------------------------*/

static int menu_exit(void)
{
	if (messagebox(MB_EXITEMULATION))
	{
		Loop = LOOP_EXIT;
		Sleep = 0;
		return 1;
	}
	return 0;
}


/*------------------------------------------------------
	コマンドリスト表示
------------------------------------------------------*/

#ifdef COMMAND_LIST
static int menu_cmdlist(void)
{
	commandlist(0);

	if (Loop == LOOP_EXIT) return 1;
	return 0;
}
#endif


/*------------------------------------------------------
	ゲーム設定メニュー
------------------------------------------------------*/

#define GAMECFG_MAX_ITEMS	32

typedef struct {
	const char *label;
	int *value;
	int enable;
	int flag;
	int old_value;
	int value_max;
	const char *values_label[12];
} gamecfg_t;

typedef struct {
	int label;
	int *value;
	int flag;
	int value_max;
	int values_label[12];
	uint32_t required_power_capabilities;
} gamecfg2_t;


#define CFG_CONTINUE	0
#define CFG_RESTART		1
#define CFG_RESET		2

#define INCLUDE_GAMECFG_STRUCT

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/menu/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/menu/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/menu/ncdz.c"
#endif

#undef INCLUDE_GAMECFG_STRUCT

#ifdef PS2
static void menu_release_ps2_ui_before_mode_change(void)
{
	/* The GS FINISH event is not a fence for all pending GIF DMA reads.
	 * UI upload buffers must remain allocated until both the command queue
	 * and the GIF channel have finished consuming them. */
	if (video_driver->flushAndWait)
		video_driver->flushAndWait(video_data);
	ui_exit();
}

static int menu_apply_live_video_output_mode(int previous_mode,
	int previous_display_mode)
{
	int requested_mode = option_video_output_mode;

	if (!video_driver->setOutputMode)
		return 0;

	/* The PS2 UI driver owns VRAM textures allocated after the emulator video
	 * resources. Drop those first, let the video backend rebuild the GS/VRAM
	 * layout, then recreate the UI against the new output geometry. */
	menu_release_ps2_ui_before_mode_change();
	if (video_driver->setOutputMode(video_data, requested_mode) && ui_init())
	{
		load_background(WP_GAMECFG);
		ui_popup_reset();
		return 1;
	}

	/* A mode can fail because the larger framebuffer leaves insufficient GS
	 * VRAM. Restore the previous known-good mode and UI instead of requiring a
	 * process restart or leaving a partially rebuilt renderer behind. */
	option_video_output_mode = previous_mode;
	option_display_mode = previous_display_mode;
	menu_release_ps2_ui_before_mode_change();
	if (!video_driver->setOutputMode(video_data, previous_mode) || !ui_init())
	{
		Loop = LOOP_EXIT;
		return 0;
	}

	load_background(WP_GAMECFG);
	ui_popup_reset();
	return 0;
}

static int menu_requires_native_ps2_scanout_switch(int previous_display_mode)
{
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	return option_video_output_mode == VIDEO_OUTPUT_240P &&
		(previous_display_mode == DISPLAY_MODE_NATIVE_PIXELS ||
		 option_display_mode == DISPLAY_MODE_NATIVE_PIXELS);
#else
	(void)previous_display_mode;
	return 0;
#endif
}

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
int ui_sync_ps2_native_scanout(int previous_display_mode)
{
	int output_width = 0, output_height = 0;
	int native_before;
	int native_requested;
	int restore_mode;

	if (option_video_output_mode != VIDEO_OUTPUT_240P ||
	    !video_data || !video_driver->getOutputSize ||
	    !video_driver->setOutputMode)
		return 1;

	video_driver->getOutputSize(video_data, &output_width, &output_height);
	native_before = output_width == PS2_CPS_NATIVE_WIDTH &&
		output_height == PS2_CPS_NATIVE_HEIGHT;
	native_requested = option_display_mode == DISPLAY_MODE_NATIVE_PIXELS;
	if (native_before == native_requested)
		return 1;

	/* Game-specific settings load after initial video/UI creation. Rebuild
	 * only when the requested GS geometry actually differs. */
	menu_release_ps2_ui_before_mode_change();
	if (video_driver->setOutputMode(video_data, option_video_output_mode) &&
	    ui_init())
		return 1;

	/* Restore the old framebuffer and UI; prefer a non-native presentation
	 * if the saved previous setting didn't match the actual framebuffer. */
	restore_mode = native_before ? DISPLAY_MODE_NATIVE_PIXELS :
		(previous_display_mode == DISPLAY_MODE_NATIVE_PIXELS ?
		 DISPLAY_MODE_ORIGINAL_ASPECT : previous_display_mode);
	option_display_mode = restore_mode;
	menu_release_ps2_ui_before_mode_change();
	if (!video_driver->setOutputMode(video_data, option_video_output_mode) ||
	    !ui_init()) {
		Loop = LOOP_EXIT;
		return 0;
	}
	return 0;
}
#endif

#endif

/*------------------------------------------------------
	CHEAT OPTIONS DAVEX
------------------------------------------------------*/


int add_new_cheat(int type, char *cheat_name, gamecheat_t** new_cheat){
	(void)type;
    gamecheat_t *cheat;
    int i;

		if( cheat_num >= MAX_CHEATS){ *(new_cheat) = NULL; return -1; }

    cheat = (gamecheat_t *) malloc( sizeof(gamecheat_t) );
		if( cheat == NULL) return -1;

	//Se reserva la memoria dinamica del nombre del cheat
	cheat->cheat_name = (char *) malloc( strlen(cheat_name) + 1 ); //incluir null al final
	if( cheat->cheat_name == NULL)
		return -2;

	//se copia el nombre del cheat
	strcpy(cheat->cheat_name, cheat_name);

    //se inicializan el resto de los valores
	//cheat->type = type; //mem save
	cheat->num_cheat_options = 0;
	cheat->curr_option = 0;

	for( i = 0; i< MAX_CHEAT_OPTION; i++)
		cheat->cheat_option[i] = NULL;

    //Se agrega el cheat al arreglo de cheats
    gamecheat[cheat_num++] = cheat;

	*(new_cheat) = cheat; //se regresa la referencia para agregar los addres_value
	return 0;

}

const char *DISABLED_LABEL = "Disabled";
const char *ENABLED_LABEL = "Enabled";

int add_new_cheat_option(char *label, gamecheat_t* cheat,cheat_option_t** new_cheat_option){
	int i;
	cheat_option_t *cheat_option;

	//Se verifican indices
	//if( cheat->num_cheat_options >= MAX_CHEAT_OPTION  ) return -1;
	if( cheat == NULL){ *(new_cheat_option) = NULL; return -1;}
	if( cheat->num_cheat_options >= MAX_CHEAT_OPTION  ){ *(new_cheat_option) = NULL;  return -1; }


	cheat_option = (cheat_option_t*) malloc( sizeof(cheat_option_t) );
	if( cheat_option == NULL)
		return -10;

	 //Se verifica si se puede ahorrar memoria usando etiquetas fijas para 'Enabled' y 'Disabled'
	if( (strncmp(label, "Disabled", 8 ) == 0 ) || (strncmp(label, "Enabled", 7) == 0 )   ){
		if(  strncmp(label, "Disabled", 8 ) == 0 )
			cheat_option->label = (char *)DISABLED_LABEL;
		else
			cheat_option->label = (char *)ENABLED_LABEL;

	}else{ //No coincide, reservar memoria

		//Se reserva memoria para la etiqueta y se copia
		cheat_option->label =  (char *) malloc( strlen(label) + 1);
		if( cheat_option->label == NULL)
			return -20;

		strcpy(cheat_option->label, label);

	}

	//se inicializan los cheat_values
	cheat_option->num_cheat_values = 0;
	for(i = 0; i< MAX_CHEAT_VALUE; i++){
		cheat_option->cheat_value[i] = NULL;
	}

	cheat->cheat_option[ cheat->num_cheat_options++] =  cheat_option;
	*(new_cheat_option) = cheat_option;

	return 0;
}

int add_new_cheat_value(int cpu, int address, int value, cheat_option_t *cheat_option){
	(void)cpu;

	cheat_value_t *cheat_value;

	if( cheat_option == NULL) return -200;
	if( cheat_option->num_cheat_values >= MAX_CHEAT_VALUE) return -201;


	cheat_value = (cheat_value_t *) malloc( sizeof(cheat_value_t) );
	if( cheat_value == NULL)
		return -100;

	//cheat_value->cpu = cpu; //mem_save
	cheat_value->address = address;
	cheat_value->value = value;
	cheat_option->cheat_value[ cheat_option->num_cheat_values++ ] = cheat_value;

	return 0;
}

int cheat_clear(void)
{
  gamecheat_t *a_cheat = NULL;
	cheat_option_t *a_cheat_option = NULL;
	cheat_value_t *a_cheat_value = NULL;
	int c,i,j;
    for( c = 0; c < cheat_num; c++)
	{ //arreglo de cheats
        a_cheat = gamecheat[c];
        free( a_cheat->cheat_name); //etiqueta del cheat

        //Se limpian los cheat option
		for(i = 0; i < a_cheat->num_cheat_options; i++)
		{
			a_cheat_option = a_cheat->cheat_option[i];

            free( a_cheat_option->label ); //se libera la etiqueta de la opcion

			//Se imprimen los cheat_value
			for(  j = 0; j< a_cheat_option->num_cheat_values; j++)
			{
                a_cheat_value = a_cheat_option->cheat_value[j];
                free( a_cheat_value);
			}
            //aqui ya se puede eliminar el cheat_option
            free( a_cheat_option);

	    }

        //se libera el cheat
        free( a_cheat);
    }

    cheat_num = 0; //se pone el numero de cheats a 0

    return 0;
}

int axtoi(char *str_num){
	int n;
	sscanf( str_num,"%X", &n);
  return n;
}

int parse_cheat_option_line(char *line, gamecheat_t* cheat ){

    //se analiza una linea de tipo
    //1 "Blanka - Rolling Attack", 0, 0xFF8456, 0x06, 0, 0xFF8458, 0x06
    cheat_option_t* new_cheat_option = NULL;
    char buff[512] = {0};
    char *ptrTok = NULL;
    char *tmpPtr = NULL;
    char *tmpPtrDest = NULL;
    int primTok = 0;
    int tokCounter = 0;
    strcpy(buff, line);
    int cpu = 0;
    int address = 0;
    int value = 0;
    int i = 0;


    const int MIN_CPU = 0;
    const int MAX_CPU = 3;
    const int MIN_ADDRESS = 0x000000;
    const int MAX_ADDRESS = 0xFFFFFF;
    const int MIN_VALUE = 0x00;
    const int MAX_VALUE = 0xFF;



    ptrTok = strtok(buff, ","); //se parten los gdes bloques por el caracter ','
    while(ptrTok != NULL){

        tmpPtr = ptrTok; //depuracion

        if( primTok == 0){ //Se verifica que contega el label


            tmpPtr = ptrTok;
            tmpPtrDest = buff;

            //agotar los primeros caracteres hasta la primera '"'
            while( 1){
                if( *tmpPtr == '"' || *tmpPtr == '\0'  )
                    break;
                tmpPtr++;
            }
            //Se encontro o el fin de cadena o la primera comilla doble
            if( *tmpPtr == '\0' ){ //no hubo comilla doble, fue un mal token, por lo que retorna la funcion completa
                return -1;
            }

            tmpPtr++; //la primera comilla doble no se copia
            i = 0;
            while( 1){
                if( *tmpPtr == '"' || *tmpPtr == '\0' || *tmpPtr == '\r' || *tmpPtr == '\n'  )break;

                if( i > 23 ) break; //las opciones tienen limite fijo de 23 caracteres
                *(tmpPtrDest++) = *(tmpPtr++);
                i++;

            }
            *tmpPtrDest = '\0'; //se termina la cadena

            add_new_cheat_option(buff, cheat, &new_cheat_option); //se agrega el cheat option


            primTok = 1;
            ptrTok = strtok(NULL, ","); //siguiente token

            continue;
        }

        if( tokCounter == 0){ //cada token 0 contiene el CPU
            cpu = atoi(ptrTok);
            if( cpu < MIN_CPU || cpu > MAX_CPU) //CPU incorrecto
                cpu = -1;
        }

        if( tokCounter == 1 ){//cada token 1 contiene address
            address = axtoi(ptrTok);
            if( address < MIN_ADDRESS || address > MAX_ADDRESS )
                address = -1;
        }

        if( tokCounter == 2 ){//cada token 2 contiene value
            value = axtoi(ptrTok);
            if( value < MIN_VALUE || value > MAX_VALUE )
                value = -1;
        }

        if( tokCounter == 2 ){ //despues del token 2 se agrega el cheat value
            //Se valida si el cheat_value es valido
            if( cpu >=0 && address >=0 && value >=0 )
                add_new_cheat_value(cpu, address, value, new_cheat_option);
        }


        tokCounter++;
        if( tokCounter > 2) tokCounter = 0;

	    ptrTok = strtok(NULL, ","); //siguiente token
    }

    return 0;
}


#define MAX_BUFF 512

void cheats_load_file(char *fn);

void cheats_load(void){

    char fn[PATH_MAX];
    char buff[ MAX_BUFF];
		int fd;
		char include_fn[50];
		int i = 0;
		char* buff_ptr = NULL;

    //Si ya hay elementos, no agregar mas a la funcion
		if( cheat_num > 0) return;

		if (!path_format(fn, sizeof(fn), "%scheats/%s.ini", launchDir, game_name)) return;

    fd = open(fn, O_RDONLY);
    if( fd < 0)
	{
		ui_popup(TEXT(CHEAT_FOR_THIS_GAME_NOT_FOUND));
		return;
	}

    //se revisa si la primera linea es un include
    {
    	ssize_t n = 0;
    	char c;
    	while (n < MAX_BUFF - 1) {
    		if (read(fd, &c, 1) <= 0) break;
    		buff[n++] = c;
    		if (c == '\n') break;
    	}
    	buff[n] = '\0';
    	if (n > 0) {
    	if( strncmp(buff, "include",7) == 0){

    			buff_ptr = buff;
    			//se eliminan todos los caracteres hasta la primer comilla doble
    			while(1){
    					if( *buff_ptr == '\0' || *buff_ptr == '\r' || *buff_ptr == '\n') break;
    					if( *buff_ptr == '"'){ buff_ptr++; break;} //Se encontro la primera comilla doble, se consume el caracter y  termina el ciclo
    					buff_ptr++;
    			}
    			//Se revisa el ultimo caracter
    			if( *buff_ptr == '\0' || *buff_ptr == '\r' || *buff_ptr == '\n') {
    				close(fd);
    				return; //mal cadena de include, no se encontro la primera comilla doble
    			}
    			//Se copian el resto de los caracteres
    			i = 0;
    			while(1){
    				if( *buff_ptr == '\0' || *buff_ptr == '\r' || *buff_ptr == '\n') {
    					close(fd);
    					return; //mal cadena de include, no se encontro la ultima comilla doble
    				}
    				if( *buff_ptr == '"'){
    					include_fn[i] = '\0'; //ultima comilla doble, se termina la cadena y el ciclo
    					break;
    				}

    				include_fn[i++] = *(buff_ptr++);
    			}

			if (!path_format(fn, sizeof(fn), "%scheats/%s.ini", launchDir, include_fn)) { close(fd); return; }
    	}
    	}
    }
    close(fd);

    //ahora si, se abre el archivo correcto
    cheats_load_file(fn);

}

void cheats_load_file(char *fn){

    int cheat_detected = 0;
    char buff[ MAX_BUFF];
    gamecheat_t* new_cheat = NULL;
		int fd;
		char * ptr_tmp = NULL;
    char clean_label[50];
    int i;

    //Si ya hay elementos, no agregar mas a la funcion
		if( cheat_num > 0) return;

    fd = open(fn, O_RDONLY);
    if( fd < 0) return;

    while (1) {
    	ssize_t n = 0;
    	char c;
    	while (n < MAX_BUFF - 1) {
    		if (read(fd, &c, 1) <= 0) goto done;
    		buff[n++] = c;
    		if (c == '\n') break;
    	}
    	buff[n] = '\0';

        if( buff[0] == '\r' || buff[0] == '\n' ){
            cheat_detected = 0;
            continue;
        }

        if( strncmp(buff, "default",7) == 0) continue;

        if( strncmp(buff, "cheat",5) == 0){ //comienza un cheat
            //Se limpia el nombre del cheat
            //a)quitar la palabra cheat
            //b)remover " comilla doble

            ptr_tmp = &(buff[5]);
            i = 0;
            while(1){
                if( *ptr_tmp == '\r' || *ptr_tmp == '\n' || *ptr_tmp == '\0')break;
                if( *ptr_tmp == '"'){ ptr_tmp++; continue;}
                if( i> 23) break; //maximo 23 caracteres

                clean_label[i++] = *(ptr_tmp++);
            }
            clean_label[i] = '\0'; //se agrega el final de la cadena


            cheat_detected = 1;
            add_new_cheat(0, clean_label, &new_cheat);

        }else if( cheat_detected == 1){ //linea de cheat option
            parse_cheat_option_line( buff, new_cheat);
        }
    }
done:
    close(fd);

}


int get_free_memory()
{
  enum
  {
    chunk_size = 65536, // 64 kB
    chunks = 1024 // 65536 * 1024 = 64 MB
  };
  void *mem_reserv[chunks];
  int total_mem = 0, i;

  /* Initialize */
  for (i = 0; i < chunks; i++)
    mem_reserv[i] = NULL;

  /* Allocate */
  for (i = 0; i < chunks; i++)
  {
    if (!(mem_reserv[i] = malloc(chunk_size)))
      break;

    total_mem += chunk_size;
  }

  /* Free */
  for (i = 0; i < chunks; i++)
  {
    if (!mem_reserv[i])
      break;
    free(mem_reserv[i]);
  }

	return total_mem;
}
/*------------------------------------------------------
	CHEAT OPTIONSDAVEX
------------------------------------------------------*/
//menu que se llena a partir de los cheatgame
//static int menu_gamecfg_davex(void)
/*------------------------------------------------------
	ボタン設定メニュー
------------------------------------------------------*/

#define KEYCFG_MAX_ITEMS	48

static const int button_value[13] =
{
	0,
	PLATFORM_PAD_UP,
	PLATFORM_PAD_DOWN,
	PLATFORM_PAD_LEFT,
	PLATFORM_PAD_RIGHT,
	PLATFORM_PAD_B1,
	PLATFORM_PAD_B2,
	PLATFORM_PAD_B3,
	PLATFORM_PAD_B4,
	PLATFORM_PAD_L,
	PLATFORM_PAD_R,
	PLATFORM_PAD_START,
	PLATFORM_PAD_SELECT
};

typedef struct {
	const char *label;
	int type;
	int value;
	int code;
} keycfg_t;

typedef struct {
	int label;
	int type;
	int code;
} keycfg2_t;

#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
enum
{
	NEOGEO_PAD = 0,
	NEOGEO_MVS,
	NEOGEO_PS,
	USER_DEFINE
};
#endif

#define KEYCFG_COMMENT 0
#define KEYCFG_BUTTON  1
#define KEYCFG_NUMBER  2
#define KEYCFG_ANALOG  3
#define KEYCFG_LAYOUT  4
#define KEYCFG_CONTROLLER 5
#define KEYCFG_ACTION  6

#define INCLUDE_KEYCFG_STRUCT

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/menu/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/menu/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/menu/ncdz.c"
#endif

#undef INCLUDE_KEYCFG_STRUCT

static int menu_keycfg(void)
{
	int sel = 0, prev_sel, rows = ui_layout_visible_rows(40, 17), top = 0;
	int i, j, arrowl, arrowr, update = 1;
	keycfg_t keycfg[KEYCFG_MAX_ITEMS];
	keycfg2_t *keycfg2;
	int keycfg_num;
	const char *button_name[13];
	const char *sensitivity[3];
#if (EMU_SYSTEM == CPS2)
	const char *progear_p2[2];
#elif (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
	const char *layout[4];
#endif

#define INCLUDE_KEYCFG_MENU

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/menu/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/menu/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/menu/ncdz.c"
#endif

#undef INCLUDE_KEYCFG_MENU

	for (i = 0; i < 13; i++)
		button_name[i] = TEXT(NOT_USE + i);

	sensitivity[0] = TEXT(SENSITIVITY_LOW);
	sensitivity[1] = TEXT(SENSITIVITY_NORMAL);
	sensitivity[2] = TEXT(SENSITIVITY_HIGH);

#if (EMU_SYSTEM == CPS2)
	progear_p2[0] = TEXT(PLAYER1_START);
	progear_p2[1] = TEXT(PLAYER2_START);
#elif (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
	layout[0] = TEXT(LAYOUT_TYPE1);
	layout[1] = TEXT(LAYOUT_TYPE2);
	layout[2] = TEXT(LAYOUT_TYPE3);
	layout[3] = TEXT(LAYOUT_USER);
#endif

	i = 0;
	while (keycfg2[i].label)
	{
		keycfg[i].label = TEXT(keycfg2[i].label);
		keycfg[i].type  = keycfg2[i].type;
		keycfg[i].value = 0;
		keycfg[i].code  = keycfg2[i].code;

		switch (keycfg[i].type)
		{
		case KEYCFG_BUTTON:
			{
				int code = input_map[keycfg[i].code];

				if (code)
				{
					for (j = 0; j < 13; j++)
					{
						if (code == button_value[j])
						{
							keycfg[i].value = j;
							break;
						}
					}
				}
			}
			break;

		case KEYCFG_NUMBER:
			keycfg[i].value = af_interval;
			break;

		case KEYCFG_ANALOG:
			keycfg[i].value = analog_sensitivity;
			break;
		}
		i++;
	}
	keycfg_num = i;
	/* The player selector and command-list viewer are rows of Controls, not
	 * another submenu. Preserve the original game-specific button indices. */
	if (keycfg_num + 2 < KEYCFG_MAX_ITEMS)
	{
		keycfg_t back = keycfg[keycfg_num - 1];
		keycfg[keycfg_num - 1].label = TEXT(CONTROLLER);
		keycfg[keycfg_num - 1].type = KEYCFG_CONTROLLER;
		keycfg[keycfg_num - 1].value = option_controller;
		keycfg[keycfg_num - 1].code = 0;
#ifdef COMMAND_LIST
		keycfg[keycfg_num].label = TEXT(MENU_COMMAND_LIST);
		keycfg[keycfg_num].type = KEYCFG_ACTION;
		keycfg[keycfg_num].value = 0;
		keycfg[keycfg_num].code = 0;
		keycfg_num++;
#endif
		keycfg[keycfg_num++] = back;
	}

#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
	if (keycfg[6].value == 6 && keycfg[7].value == 5 && keycfg[8].value == 7 && keycfg[9].value == 8)
	{
		keycfg[0].value = NEOGEO_PAD;
	}
	else if (keycfg[6].value == 7 && keycfg[7].value == 8 && keycfg[8].value == 6 && keycfg[9].value == 5)
	{
		keycfg[0].value = NEOGEO_MVS;
	}
	else if (keycfg[6].value == 7 && keycfg[7].value == 6 && keycfg[8].value == 8 && keycfg[9].value == 5)
	{
		keycfg[0].value = NEOGEO_PS;
	}
	else
	{
		keycfg[0].value = USER_DEFINE;
	}
#endif

	pad_wait_clear();
	load_background(WP_KEYCFG);
	ui_popup_reset();

	do
	{
		if (update)
		{
			video_driver->beginFrame(video_data);
			show_background();

			small_icon_shadow(8, 3, UI_COLOR(UI_PAL_TITLE), ICON_KEYCONFIG);
			uifont_print_shadow(36, 5, UI_COLOR(UI_PAL_TITLE), TEXT(MENU_CONTROLS_SETTINGS));
			draw_scrollbar(ui_layout_right(10), 26, ui_layout_right(0),
				ui_layout_bottom(1), rows, keycfg_num, sel);

			arrowl = 0;
			arrowr = 0;
			if (keycfg[sel].type)
			{
				if (keycfg[sel].value > 0) arrowl = 1;
				if (keycfg[sel].type == KEYCFG_BUTTON && keycfg[sel].value < 12) arrowr = 1;
				if (keycfg[sel].type == KEYCFG_NUMBER && keycfg[sel].value < 10) arrowr = 1;
				if (keycfg[sel].type == KEYCFG_ANALOG && keycfg[sel].value < 2) arrowr = 1;
				if (keycfg[sel].type == KEYCFG_CONTROLLER &&
					keycfg[sel].value <
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
					(input_max_players - 1)
#else
					1
#endif
					) arrowr = 1;
#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
				if (keycfg[sel].type == KEYCFG_LAYOUT && keycfg[sel].value < 1) arrowr = 1;
#endif
			}

			for (i = 0; i < rows; i++)
			{
				int value;
				char temp[16];
				const char *name;
				const char *label;

				if ((top + i) >= keycfg_num) break;
				value = keycfg[top + i].value;
				label = keycfg[top + i].label;

				if (label[0] == '\n')
					continue;

				switch (keycfg[top + i].type)
				{
				case KEYCFG_BUTTON:
#if (EMU_SYSTEM == CPS2)
					if (!strcmp(driver->name, "progear"))
						if (top + i == 10) label = progear_p2[option_controller ^ 1];
#endif
					name = button_name[value];
					break;

				case KEYCFG_NUMBER:
					if (value == 0)
						sprintf(temp, TEXT(xFRAME), value + 1);
					else
						sprintf(temp, TEXT(xFRAMES), value + 1);
					name = (const char *)temp;
					break;

				case KEYCFG_ANALOG:
					name = sensitivity[value];
					break;

				case KEYCFG_CONTROLLER:
					name = TEXT(CONTROLLER1 + value);
					break;

#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
				case KEYCFG_LAYOUT:
					name = layout[value];
					break;
#endif

				default:
					temp[0] = '\0';
					name = temp;
					break;
				}

				if ((top + i) == sel)
				{
					uifont_print_shadow(16, 40 + i * 17, UI_COLOR(UI_PAL_SELECT), label);
					uifont_print_shadow(210, 40 + i * 17, UI_COLOR(UI_PAL_SELECT), name);

					if (arrowl)
					{
						uifont_print_shadow(190, 40 + i * 17, UI_COLOR(UI_PAL_SELECT), FONT_LEFTTRIANGLE);
					}
					if (arrowr)
					{
						int width = uifont_get_string_width(name);
						uifont_print_shadow(214 + width, 40 + i * 17, UI_COLOR(UI_PAL_SELECT), FONT_RIGHTTRIANGLE);
					}
				}
				else
				{
					uifont_print(16, 40 + i * 17, UI_COLOR(UI_PAL_NORMAL), label);
					uifont_print(210, 40 + i * 17, UI_COLOR(UI_PAL_NORMAL), name);
				}
			}

			update  = draw_battery_status(1);
			update |= ui_show_popup(1);
			video_driver->endFrame(video_data);
			video_driver->flipScreen(video_data, 1);
		}
		else
		{
			update  = draw_battery_status(0);
			update |= ui_show_popup(0);
			video_driver->waitVsync(video_data);
		}

		pad_update();
		prev_sel = sel;

		if (pad_pressed(PLATFORM_PAD_UP))
		{
			sel--;
			if (sel < 0) sel = keycfg_num - 1;
			if (keycfg[sel].label[0] == '\n') sel--;
		}
		else if (pad_pressed(PLATFORM_PAD_DOWN))
		{
			sel++;
			if (sel > keycfg_num - 1) sel = 0;
			if (keycfg[sel].label[0] == '\n') sel++;
		}
		else if (pad_pressed(PLATFORM_PAD_L))
		{
			sel -= rows;
			if (sel < 0) sel = 0;
			if (keycfg[sel].label[0] == '\n') sel--;
		}
		else if (pad_pressed(PLATFORM_PAD_R))
		{
			sel += rows;
			if (sel >= keycfg_num) sel = keycfg_num - 1;
			if (keycfg[sel].label[0] == '\n') sel++;
		}
		else if (pad_pressed(PLATFORM_PAD_LEFT))
		{
			if (keycfg[sel].type)
			{
				if (keycfg[sel].value > 0)
				{
					keycfg[sel].value--;
					update = 1;
				}
			}
#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
			if (keycfg[sel].type == KEYCFG_LAYOUT && update)
			{
				switch (keycfg[sel].value)
				{
				case NEOGEO_PAD:
					keycfg[6].value = 6;
					keycfg[7].value = 5;
					keycfg[8].value = 7;
					keycfg[9].value = 8;
					break;

				case NEOGEO_MVS:
					keycfg[6].value = 7;
					keycfg[7].value = 8;
					keycfg[8].value = 6;
					keycfg[9].value = 5;
					break;
				case NEOGEO_PS:
					keycfg[6].value = 7;
					keycfg[7].value = 6;
					keycfg[8].value = 8;
					keycfg[9].value = 5;
					break;
				}
			}
#endif
		}
			else if (pad_pressed(PLATFORM_PAD_RIGHT))
		{
			if (keycfg[sel].type == KEYCFG_BUTTON)
			{
				if (keycfg[sel].value < 12)
				{
					keycfg[sel].value++;
					update = 1;
				}
			}
			else if (keycfg[sel].type == KEYCFG_NUMBER)
			{
				if (keycfg[sel].value < 10)
				{
					keycfg[sel].value++;
					update = 1;
				}
			}
			else if (keycfg[sel].type == KEYCFG_ANALOG)
			{
				if (keycfg[sel].value < 2)
				{
					keycfg[sel].value++;
					update = 1;
				}
			}
			else if (keycfg[sel].type == KEYCFG_CONTROLLER)
			{
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
				int max_controller = input_max_players - 1;
#else
				int max_controller = 1;
#endif
				if (keycfg[sel].value < max_controller)
				{
					keycfg[sel].value++;
					update = 1;
				}
			}
#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
			else if (keycfg[sel].type == KEYCFG_LAYOUT)
			{
				if (keycfg[sel].value == 0)
				{
					keycfg[sel].value++;
					update = 1;

					keycfg[6].value = 7;
					keycfg[7].value = 8;
					keycfg[8].value = 6;
					keycfg[9].value = 5;
				}
				else if (keycfg[sel].value == 1)
				{
					keycfg[sel].value++;
					update = 1;
					keycfg[6].value = 7;
					keycfg[7].value = 6;
					keycfg[8].value = 8;
					keycfg[9].value = 5;
				}
			}
#endif
		}
		else if (pad_pressed(PLATFORM_PAD_B1))
		{
			if (sel == keycfg_num - 1)
				break;
#ifdef COMMAND_LIST
			if (keycfg[sel].type == KEYCFG_ACTION)
			{
				if (menu_cmdlist()) break;
				pad_wait_clear();
				load_background(WP_KEYCFG);
				update = 1;
			}
#endif
		}
		else if (pad_pressed(PLATFORM_PAD_SELECT))
		{
			help(HELP_KEYCONFIG);
			update = 1;
		}

		if (top > keycfg_num - rows) top = keycfg_num - rows;
		if (top < 0) top = 0;
		if (sel >= keycfg_num) sel = 0;
		if (sel < 0) sel = keycfg_num - 1;
		if (sel >= top + rows) top = sel - rows + 1;
		if (sel < top) top = sel;

		if (prev_sel != sel) update = 1;

#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
		if (update)
		{
			if (keycfg[6].value == 6 && keycfg[7].value == 5 && keycfg[8].value == 7 && keycfg[9].value == 8)
			{
				keycfg[0].value = NEOGEO_PAD;
			}
			else if (keycfg[6].value == 7 && keycfg[7].value == 8 && keycfg[8].value == 6 && keycfg[9].value == 5)
			{
				keycfg[0].value = NEOGEO_MVS;
			}
			else if (keycfg[6].value == 7 && keycfg[7].value == 6 && keycfg[8].value == 8 && keycfg[9].value == 5)
			{
				keycfg[0].value = NEOGEO_PS;
			}
			else
			{
				keycfg[0].value = USER_DEFINE;
			}
		}
#endif

		if (Loop == LOOP_EXIT) break;

	} while (!pad_pressed(PLATFORM_PAD_B2));

	for (i = 0; i < keycfg_num; i++)
	{
		if (keycfg[i].type == KEYCFG_BUTTON)
		{
			input_map[keycfg[i].code] = button_value[keycfg[i].value];
		}
		else if (keycfg[i].type == KEYCFG_NUMBER)
		{
			af_interval = keycfg[i].value;
		}
		else if (keycfg[i].type == KEYCFG_ANALOG)
		{
			analog_sensitivity = keycfg[i].value;
		}
		else if (keycfg[i].type == KEYCFG_CONTROLLER)
		{
			option_controller = keycfg[i].value;
		}
	}

	return 0;
}


/*------------------------------------------------------
	ディップスイッチ設定メニュー
------------------------------------------------------*/

/*------------------------------------------------------
	セーブ/ロードステート
------------------------------------------------------*/

#ifdef SAVE_STATE

#define STATE_FUNC_SAVE		0
#define STATE_FUNC_LOAD		1
#define STATE_FUNC_DEL		2


static uint8_t slot[10];
static int state_func;
static int state_sel;

static int state_thumbnail_x(void)
{
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	if (machine_screen_type)
		return ui_layout_get()->logical_width - 163;
#endif
	return ui_layout_get()->logical_width - 182;
}

static int state_info_x(void)
{
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	if (machine_screen_type)
		return state_thumbnail_x() - 17;
#endif
	return state_thumbnail_x() - 8;
}

static void state_draw_thumbnail(void)
{
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	if (machine_screen_type)
	{
		int dx, dy, dw, dh;
		RECT clip1 = { 0, 0, 112, 152 };
		const int x = state_thumbnail_x();
		RECT clip2;
		ui_layout_transform_rect(x, 34, 112, 152,
			&dx, &dy, &dw, &dh);
		clip2.left = dx;
		clip2.top = dy;
		clip2.right = dx + dw;
		clip2.bottom = dy + dh;
		video_driver->drawTexture(video_data, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &clip1, &clip2);
	}
	else
#endif
	{
		int dx, dy, dw, dh;
		RECT clip1 = { 0, 0, 152, 112 };
		const int x = state_thumbnail_x();
		RECT clip2;
		ui_layout_transform_rect(x, 52, 152, 112,
			&dx, &dy, &dw, &dh);
		clip2.left = dx;
		clip2.top = dy;
		clip2.right = dx + dw;
		clip2.bottom = dy + dh;
		video_driver->drawTexture(video_data, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &clip1, &clip2);
	}
}

static void state_refresh_screen(int reload_thumbnail)
{
	int i, x;
	const int preview_x = state_thumbnail_x();
	const int info_x = state_info_x();
	char name[16], state[32], buf[64];

	if (reload_thumbnail)
	{
		if (!slot[state_sel])
		{
			state_clear_thumbnail();
			state_func = STATE_FUNC_SAVE;
		}
		else
		{
			state_load_thumbnail(state_sel);
			if (state_version == current_state_version)
				state_func = STATE_FUNC_LOAD;
			else
			{
				state_func = STATE_FUNC_SAVE;
				strcpy(date_str, "----/--/--");
				strcpy(time_str, "--:--:--");
			}
		}
	}

	show_background();
	if (machine_screen_type)
		draw_box_shadow(preview_x + 1, 33, preview_x + 112, 184);
	else
		draw_box_shadow(preview_x, 52, preview_x + 151, 163);
	state_draw_thumbnail();

	sprintf(buf, "%s %s", FONT_LTRIGGER, TEXT(MAIN_MENU));
	i = uifont_get_string_width(buf);
	uifont_print_shadow(8, 5, UI_COLOR(UI_PAL_NORMAL), buf);

	sprintf(buf, "%s %s", TEXT(SAVE_LOAD_STATE), FONT_RTRIGGER);
	uifont_print_shadow(i + 16, 5, UI_COLOR(UI_PAL_NORMAL), "|");
	uifont_print_shadow(i + 24, 5, UI_COLOR(UI_PAL_TITLE), buf);

	if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_JAPANESE)
		x = 20;
	else
		x = 0;

	for (i = 0; i < 10; i++)
	{
		sprintf(name, TEXT(SLOTx), i);
		if (slot[i])
			sprintf(state, "%s.sv%d", game_name, i);
		else
			strcpy(state, TEXT(EMPTY));

		if (i == state_sel)
		{
			small_icon_light(12, 38 + i * 22, UI_COLOR(UI_PAL_SELECT), ICON_MEMSTICK);
			uifont_print_shadow(40, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), name);
			uifont_print_shadow(92 + x, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), state);

			if (slot[state_sel])
			{
				if (state_func == STATE_FUNC_LOAD)
				{
					uifont_print_shadow(200, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), FONT_LEFTTRIANGLE);
					uifont_print_shadow(222, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), TEXT(LOAD));
					uifont_print_shadow(262, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), FONT_RIGHTTRIANGLE);
				}
				else if (state_func == STATE_FUNC_DEL)
				{
					uifont_print_shadow(200, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), FONT_LEFTTRIANGLE);
					uifont_print_shadow(222, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), TEXT(DELETE));
				}
				else
				{
					uifont_print_shadow(222, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), TEXT(SAVE));
					uifont_print_shadow(262, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), FONT_RIGHTTRIANGLE);
				}
			}
			else
			{
				uifont_print_shadow(222, 40 + i * 22, UI_COLOR(UI_PAL_SELECT), TEXT(SAVE));
			}

			hline_gradation(40, 278, 56 + i * 22, UI_COLOR(UI_PAL_NORMAL), UI_COLOR(UI_PAL_SELECT), 14);
			hline_gradation(41, 279, 57 + i * 22, COLOR_BLACK, COLOR_BLACK, 8);
		}
		else
		{
			small_icon(12, 38 + i * 22, UI_COLOR(UI_PAL_NORMAL), ICON_MEMSTICK);
			uifont_print(40, 40 + i * 22, UI_COLOR(UI_PAL_NORMAL), name);
			uifont_print(92 + x, 40 + i * 22, UI_COLOR(UI_PAL_NORMAL), state);
		}
	}

	if (state_version == current_state_version || !slot[state_sel])
	{
	#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
			if (machine_screen_type)
			{
				uifont_print(info_x, 198, UI_COLOR(UI_PAL_NORMAL), TEXT(PLAY_DATE));
				uifont_print(info_x + 78, 198, UI_COLOR(UI_PAL_NORMAL), date_str);
				uifont_print(info_x, 218, UI_COLOR(UI_PAL_NORMAL), TEXT(SAVE_TIME));
				uifont_print(info_x + 78, 218, UI_COLOR(UI_PAL_NORMAL), time_str);
				uifont_print(info_x, 238, UI_COLOR(UI_PAL_NORMAL), TEXT(STATE_VERSION));
				uifont_print(info_x + 78, 238, UI_COLOR(UI_PAL_NORMAL), stver_str);
			}
			else
	#endif
			{
				uifont_print(info_x, 190, UI_COLOR(UI_PAL_NORMAL), TEXT(PLAY_DATE));
				uifont_print(info_x + 78, 190, UI_COLOR(UI_PAL_NORMAL), date_str);
				uifont_print(info_x, 210, UI_COLOR(UI_PAL_NORMAL), TEXT(SAVE_TIME));
				uifont_print(info_x + 78, 210, UI_COLOR(UI_PAL_NORMAL), time_str);
				uifont_print(info_x, 230, UI_COLOR(UI_PAL_NORMAL), TEXT(STATE_VERSION));
				uifont_print(info_x + 78, 230, UI_COLOR(UI_PAL_NORMAL), stver_str);
			}
	}
	else
	{
	#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
			if (machine_screen_type)
			{
				uifont_print(info_x, 198, COLOR_GRAY, TEXT(PLAY_DATE));
				uifont_print(info_x + 78, 198, COLOR_GRAY, date_str);
				uifont_print(info_x, 218, COLOR_GRAY, TEXT(SAVE_TIME));
				uifont_print(info_x + 78, 218, COLOR_GRAY, time_str);
				uifont_print(info_x, 238, COLOR_GRAY, TEXT(STATE_VERSION));
				uifont_print(info_x + 78, 238, COLOR_GRAY, stver_str);
			}
			else
	#endif
			{
				uifont_print(info_x, 190, COLOR_GRAY, TEXT(PLAY_DATE));
				uifont_print(info_x + 78, 190, COLOR_GRAY, date_str);
				uifont_print(info_x, 210, COLOR_GRAY, TEXT(SAVE_TIME));
				uifont_print(info_x + 78, 210, COLOR_GRAY, time_str);
				uifont_print(info_x, 230, COLOR_GRAY, TEXT(STATE_VERSION));
				uifont_print(info_x + 78, 230, COLOR_GRAY, stver_str);
			}
	}
}

static int state_save_slot(void)
{
	if (messagebox(MB_STARTSAVESTATE))
	{
		int res;

		video_driver->beginFrame(video_data);
		state_refresh_screen(0);
		video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, &full_rect, &full_rect);
		video_driver->endFrame(video_data);

		power_set_performance_level(platform_performance_level);
	#if (EMU_SYSTEM == CPS2)
			if (!cps2_sound_state_prepare())
				res = 0;
			else
			{
				res = state_save(state_sel);
				(void)cps2_sound_state_resume();
			}
	#elif (EMU_SYSTEM == MVS) || (EMU_SYSTEM == NCDZ)
			if (!neogeo_sound_state_prepare())
			res = 0;
		else
		{
			res = state_save(state_sel);
			(void)neogeo_sound_state_resume();
		}
#else
		res = state_save(state_sel);
#endif
		power_set_lowest_performance_level();

		load_background(WP_STATE);

		if (res)
		{
			slot[state_sel] = 1;
			video_driver->beginFrame(video_data);
			state_refresh_screen(1);
			draw_battery_status(1);
			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER, &full_rect, &full_rect);
			video_driver->endFrame(video_data);
			res = messagebox(MB_FINISHSAVESTATE);
		}
		return res + 1;
	}
	return 0;
}

static int state_load_slot(void)
{
	if (messagebox(MB_STARTLOADSTATE))
	{
		int res;

		video_driver->beginFrame(video_data);
		state_refresh_screen(0);
		video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, &full_rect, &full_rect);
		video_driver->endFrame(video_data);

		power_set_performance_level(platform_performance_level);
	#if (EMU_SYSTEM == CPS2)
			if (!cps2_sound_state_prepare())
				res = 0;
			else
			{
				res = state_load(state_sel);
				(void)cps2_sound_state_resume();
			}
	#elif (EMU_SYSTEM == MVS) || (EMU_SYSTEM == NCDZ)
			if (!neogeo_sound_state_prepare())
			res = 0;
		else
		{
			res = state_load(state_sel);
			(void)neogeo_sound_state_resume();
		}
#else
		res = state_load(state_sel);
#endif
		power_set_lowest_performance_level();

		if (res)
		{
			video_driver->beginFrame(video_data);
			show_background();
			draw_battery_status(1);
			video_driver->endFrame(video_data);
			video_driver->flipScreen(video_data, 1);

			messagebox(MB_FINISHLOADSTATE);
		}

		load_background(WP_STATE);
		return res + 1;
	}
	return 0;
}

static void state_delete_slot(void)
{
	if (messagebox(MB_DELETESTATE))
	{
		char path[PATH_MAX];

		if (!path_format(path, sizeof(path), "%sstate/%s.sv%d", launchDir, game_name, state_sel)) return;
		if (remove(path) < 0)
		{
			ui_popup(TEXT(FAILD_TO_DELETE_FILEx), strrchr(path, '/') + 1);
		}
		else
		{
			slot[state_sel] = 0;
			state_func = STATE_FUNC_SAVE;
			state_clear_thumbnail();
		}
	}
}

static int menu_state(void)
{
	int prev_sel = -1, prev_func = -1, update = 1;

	state_func = STATE_FUNC_SAVE;
	state_sel = 0;

	pad_wait_clear();
	load_background(WP_STATE);
	ui_popup_reset();

	memset(slot, 0, sizeof(slot));
	state_clear_thumbnail();

	find_state_file(slot);

	do
	{
		if (update & UI_FULL_REFRESH)
		{
			video_driver->beginFrame(video_data);
			state_refresh_screen((prev_sel == state_sel) ? 0 : 1);

			update  = draw_battery_status(1);
			update |= ui_show_popup(1);
			video_driver->endFrame(video_data);
			video_driver->flipScreen(video_data, 1);
		}
		else if (update & UI_PARTIAL_REFRESH)
		{
			int x, y, w, h;
			RECT clip1, clip2;

			video_driver->beginFrame(video_data);
			show_background();

			small_icon_light(12, 38 + state_sel * 22, UI_COLOR(UI_PAL_SELECT), ICON_MEMSTICK);

			x = 12;
			y = 38 + state_sel * 22;
			w = 24 + 8;
			h = 18 + 8;

			clip1.left   = x - 4;
			clip1.top    = y - 4;
			clip1.right  = clip1.left + w;
			clip1.bottom = clip1.top  + h;

			clip2.left   = 0;
			clip2.top    = 112;
			clip2.right  = clip2.left + w;
			clip2.bottom = clip2.top  + h;

			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, &clip1, &clip2);
			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &full_rect, &full_rect);
			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &clip2, &clip1);

			update  = draw_battery_status(0);
			update |= ui_show_popup(0);
			video_driver->endFrame(video_data);
			video_driver->flipScreen(video_data, 1);
		}
		else
		{
			update  = draw_battery_status(0);
			update |= ui_show_popup(0);
			video_driver->waitVsync(video_data);
		}

		update |= ui_light_update();
		pad_update();
		prev_sel = state_sel;
		prev_func = state_func;

		if (pad_pressed(PLATFORM_PAD_UP))
		{
			state_sel--;
			if (state_sel < 0) state_sel = 10 - 1;
		}
		else if (pad_pressed(PLATFORM_PAD_DOWN))
		{
			state_sel++;
			if (state_sel >= 10) state_sel = 0;
		}
		else if (pad_pressed(PLATFORM_PAD_LEFT))
		{
			if (state_func > STATE_FUNC_SAVE) state_func--;
			if (state_version != current_state_version && state_func == STATE_FUNC_LOAD)
				state_func--;
		}
		else if (pad_pressed(PLATFORM_PAD_RIGHT))
		{
			if (slot[state_sel])
			{
				if (state_func < STATE_FUNC_DEL) state_func++;
				if (state_version != current_state_version && state_func == STATE_FUNC_LOAD)
					state_func++;
			}
		}
		else if (pad_pressed(PLATFORM_PAD_B1))
		{
			int res = 0;

			if (state_func == STATE_FUNC_LOAD)
			{
				if (slot[state_sel])
				{
					if ((res = state_load_slot()) != 0)
					{
						update = 1;
						res--;
					}
				}
			}
			else if (state_func == STATE_FUNC_DEL)
			{
				if (slot[state_sel])
				{
					state_delete_slot();
					update = 1;
				}
			}
			else
			{
				if ((res = state_save_slot()) != 0)
				{
					update = 1;
					res--;
				}
			}
			if (res) return 1;
		}
		else if (pad_pressed(PLATFORM_PAD_SELECT))
		{
			help(HELP_STATE);
			update = 1;
		}

		if (prev_sel != state_sel || prev_func != state_func)
			update = 1;

		if (Loop == LOOP_EXIT) break;

	} while (!pad_pressed(PLATFORM_PAD_L) && !pad_pressed(PLATFORM_PAD_B2));

	return 0;
}

#endif


/* Category rows share the existing per-core configuration declarations. */
#include "common/menu/settings_page.inc"

/*------------------------------------------------------
	メインメニュー
------------------------------------------------------*/

#define MENU_MAX_ITEMS	16

typedef struct
{
	const char *label;
	int (*menu_func)(void);
	int icon;
	const char *help;
} menu_t;

typedef struct
{
	int label;
	int (*menu_func)(void);
	int icon;
	int help;
} menu2_t;

static int menu_video_section(void)
{
    return menu_settings_page(MENU_CFG_VIDEO, MENU_VIDEO_DISPLAY, ICON_VIDEO);
}
static int menu_audio_section(void)
{
    return menu_settings_page(MENU_CFG_AUDIO, MENU_AUDIO_SETTINGS, ICON_AUDIO);
}
static int menu_controls_section(void)
{
    return menu_keycfg();
}
#if (EMU_SYSTEM != CPS2 || ENABLE_RASTER_OPTION)
static int menu_emulation_section(void)
{
    return menu_settings_page(MENU_CFG_EMULATION, MENU_EMULATION_SETTINGS, ICON_CONFIG);
}
#endif
static int menu_performance_section(void)
{
    return menu_settings_page(MENU_CFG_PERFORMANCE, MENU_PERFORMANCE, ICON_PERFORMANCE);
}

#if (EMU_SYSTEM == CPS1)
#define MENUHELP_RESET_EMULATION_ID MENUHELP_RESET_EMULATION_CPS1
#elif (EMU_SYSTEM == CPS2)
#define MENUHELP_RESET_EMULATION_ID MENUHELP_RESET_EMULATION_CPS2
#elif (EMU_SYSTEM == MVS)
#define MENUHELP_RESET_EMULATION_ID MENUHELP_RESET_EMULATION_MVS
#else
#define MENUHELP_RESET_EMULATION_ID MENUHELP_RESET_EMULATION_NCDZ
#endif

static const menu2_t mainmenu2[] = {
    { MENU_RETURN_TO_GAME, NULL, ICON_RETURN, MENUHELP_RETURN_TO_GAME },
    { MENU_VIDEO_DISPLAY, menu_video_section, ICON_VIDEO, MENUHELP_VIDEO_DISPLAY },
    { MENU_AUDIO_SETTINGS, menu_audio_section, ICON_AUDIO, MENUHELP_AUDIO_SETTINGS },
    { MENU_CONTROLS_SETTINGS, menu_controls_section, ICON_KEYCONFIG, MENUHELP_CONTROLS_SETTINGS },
#if (EMU_SYSTEM != CPS2 || ENABLE_RASTER_OPTION)
    { MENU_EMULATION_SETTINGS, menu_emulation_section, ICON_CONFIG, MENUHELP_EMULATION_SETTINGS },
#endif
    { MENU_PERFORMANCE, menu_performance_section, ICON_PERFORMANCE, MENUHELP_PERFORMANCE },
    { MENU_RESET_EMULATION, menu_reset, ICON_SYSTEM, MENUHELP_RESET_EMULATION_ID },
    { MENU_RETURN_TO_BROWSER, menu_browser, ICON_FOLDER, MENUHELP_RETURN_TO_BROWSER },
    { MENU_EXIT_EMULATOR, menu_exit, ICON_EXIT, MENUHELP_EXIT_EMULATOR },
    MENU_END
};


void showmenu(void)
{
	static int sel = 0, prev_sel = 0, top = 0;
	int rows = ui_layout_visible_rows(40, 32);
	int i, source, update = 1;
	int mainmenu_num = 0;
#ifdef SAVE_STATE
	char buf[128];
#endif
	menu_t mainmenu[MENU_MAX_ITEMS];
	/* A sampled menu tap must not be replayed as an attack after closing the
	 * menu; short-press edge capture is intended for emulated gameplay. */
	set_gamepad_edge_capture(false);

#ifdef NJEMU_EXTERNAL_MENU_ICONS
	/* The pack is resident only while the in-game menu is active. */
	{
		char icon_path[PATH_MAX];
		if (path_format(icon_path, sizeof(icon_path), "%smenu_icons.bin", launchDir))
			menu_icon_pack_load(icon_path);
	}
#endif

#ifdef SAVE_STATE
	state_make_thumbnail();
#endif

#if (EMU_SYSTEM == NCDZ)
	if (cdda_playing == CDDA_PLAY) mp3_pause(1);
#endif
	sound_mute(1);
#if USE_CACHE
	cache_sleep(1);
#endif
	/* Some platforms require explicit device discovery (for example PS2
	 * multitaps). Keep that potentially blocking work in the paused UI path,
	 * never in the per-frame gameplay controller-count query. */
	refresh_gamepads();

	i = 0;
	source = 0;
	while (mainmenu2[source].label)
	{
		if (mainmenu2[source].menu_func == menu_performance_section &&
			!menu_has_performance_settings(USE_CACHE,
				power_has_capability(POWER_CAP_PERFORMANCE))) {
			source++;
			continue;
		}
		mainmenu[i].label     = TEXT(mainmenu2[source].label);
		mainmenu[i].menu_func = mainmenu2[source].menu_func;
		mainmenu[i].icon      = mainmenu2[source].icon;
		mainmenu[i].help      = TEXT(mainmenu2[source].help);
		source++;
		i++;
	}
	mainmenu_num = i;

	power_set_lowest_performance_level();
	video_driver->beginFrame(video_data);
	video_driver->clearScreen(video_data);
	video_driver->endFrame(video_data);
	load_background(WP_LOGO);
	ui_popup_reset();
	pad_wait_clear();

	do
	{
		if (update & UI_FULL_REFRESH)
		{
			video_driver->beginFrame(video_data);
			show_background();

#ifdef SAVE_STATE
			sprintf(buf, "%s %s", FONT_LTRIGGER, TEXT(MAIN_MENU));
			i = uifont_get_string_width(buf);
			uifont_print_shadow(8, 5, UI_COLOR(UI_PAL_TITLE), buf);

			sprintf(buf, "%s %s", TEXT(SAVE_LOAD_STATE), FONT_RTRIGGER);
			uifont_print_shadow(i + 16, 5, UI_COLOR(UI_PAL_NORMAL), "|");
			uifont_print_shadow(i + 24, 5, UI_COLOR(UI_PAL_NORMAL), buf);
#else
			small_icon_shadow(8, 3, UI_COLOR(UI_PAL_TITLE), ICON_SYSTEM);
			uifont_print_shadow(36, 5, UI_COLOR(UI_PAL_TITLE), TEXT(MAIN_MENU));
#endif
				draw_scrollbar(ui_layout_right(10), 26, ui_layout_right(0),
					ui_layout_bottom(1), rows, mainmenu_num, sel);

			for (i = 0; i < rows; i++)
			{
				if ((top + i) >= mainmenu_num) break;

				if (mainmenu[top + i].label[0] == '\n')
					continue;

				if (top + i == sel)
				{
					large_icon_light(12, 40 + i * 32, UI_COLOR(UI_PAL_SELECT), mainmenu[sel].icon);
					uifont_print_shadow(54, 37 + i * 32, UI_COLOR(UI_PAL_SELECT), mainmenu[sel].label);
					uifont_print_shadow(54, 56 + i * 32, UI_COLOR(UI_PAL_SELECT), mainmenu[sel].help);

					hline_gradation(54, ui_layout_right(19), 53 + i * 32,
						UI_COLOR(UI_PAL_NORMAL), UI_COLOR(UI_PAL_SELECT), 14);
					hline_gradation(55, ui_layout_right(18), 54 + i * 32,
						COLOR_BLACK, COLOR_BLACK, 8);
				}
				else
				{
					small_icon(16, 42 + i * 32, UI_COLOR(UI_PAL_NORMAL), mainmenu[top + i].icon);
					uifont_print(56, 44 + i * 32, UI_COLOR(UI_PAL_NORMAL), mainmenu[top + i].label);
				}
			}

			update  = draw_battery_status(1);
			update |= ui_show_popup(1);
			video_driver->endFrame(video_data);
			video_driver->flipScreen(video_data, 1);
		}
		else if (update & UI_PARTIAL_REFRESH)
		{
			int x, y, w, h;
			RECT clip1, clip2;

			video_driver->beginFrame(video_data);
			show_background();

			for (i = 0; i < rows; i++)
				if (top + i == sel) break;

			large_icon_light(12, 40 + i * 32, UI_COLOR(UI_PAL_SELECT), mainmenu[sel].icon);

			x = 12;
			y = 40 + i * 32;
			w = 32 + 8;
			h = 24 + 8;

			clip1.left   = x - 4;
			clip1.top    = y - 4;
			clip1.right  = clip1.left + w;
			clip1.bottom = clip1.top  + h;

			clip2.left   = 0;
			clip2.top    = 112;
			clip2.right  = clip2.left + w;
			clip2.bottom = clip2.top  + h;

			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, &clip1, &clip2);
			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &full_rect, &full_rect);
			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &clip2, &clip1);

			update  = draw_battery_status(0);
			update |= ui_show_popup(0);
			video_driver->endFrame(video_data);
			video_driver->flipScreen(video_data, 1);
		}
		else
		{
			update  = draw_battery_status(0);
			update |= ui_show_popup(0);
			video_driver->waitVsync(video_data);
		}

		update |= ui_light_update();
		pad_update();
		prev_sel = sel;

		if (pad_pressed(PLATFORM_PAD_UP))
		{
			sel--;
			if (sel < 0) sel = mainmenu_num - 1;
		}
		else if (pad_pressed(PLATFORM_PAD_DOWN))
		{
			sel++;
			if (sel >= mainmenu_num) sel = 0;
		}
		else if (pad_pressed(PLATFORM_PAD_B1))
		{
			if (mainmenu[sel].menu_func)
			{
				int res = mainmenu[sel].menu_func();

					if (res == 0)
					{
						pad_wait_clear();
						load_background(WP_LOGO);
						/* A submenu can rebuild the video/UI geometry (PS2 output-mode
						 * switching does exactly that). Refresh the parent menu's row
						 * count immediately so its selection window stays focused on the
						 * current item instead of using the previous resolution's density. */
						rows = ui_layout_visible_rows(40, 32);
						update = 1;
					}
				else break;
			}
			else break;
		}
		else if (pad_pressed(PLATFORM_PAD_SELECT))
		{
			help(HELP_MAINMENU);
			update = 1;
		}
#ifdef SAVE_STATE
		else if (pad_pressed(PLATFORM_PAD_R))
		{
			if (menu_state()) break;
			pad_wait_clear();
			load_background(WP_LOGO);
		}
#endif
		update = 1;

		if (top > mainmenu_num - rows) top = mainmenu_num - rows;
		if (top < 0) top = 0;
		if (sel >= mainmenu_num) sel = 0;
		if (sel < 0) sel = mainmenu_num - 1;
		if (sel >= top + rows) top = sel - rows + 1;
		if (sel < top) top = sel;

		if (prev_sel != sel) update |= 1;

		if (Loop == LOOP_EXIT) break;

	} while (!pad_pressed(PLATFORM_PAD_B2));

	autoframeskip_reset();
	blit_clear_all_sprite();

#ifdef NJEMU_EXTERNAL_MENU_ICONS
	menu_icon_pack_unload();
#endif

#ifdef SAVE_STATE
	state_release_thumbnail();
#endif

	pad_wait_clear();
	ui_popup_reset();
	video_driver->beginFrame(video_data);
	video_driver->clearScreen(video_data);
	video_driver->clearFrame(video_data, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP);
	video_driver->endFrame(video_data);
#if (EMU_SYSTEM != CPS2)
	sound_set_samplerate();
#endif
	power_set_performance_level(platform_performance_level);

#if USE_CACHE
	cache_sleep(0);
#endif
	set_gamepad_edge_capture(true);

#if (EMU_SYSTEM == MVS || EMU_SYSTEM == NCDZ)
	neogeo_reset_driver_type();
#endif

	if (Loop == LOOP_EXEC)
	{
		sound_thread_set_volume();
#if (EMU_SYSTEM == NCDZ)
		mp3_set_volume();

		if (option_mp3_enable && cdda_playing == CDDA_PLAY)
			mp3_pause(0);
		else
			cdda_stop();
#endif
		sound_mute(0);
	}
	else
	{
#if (EMU_SYSTEM == NCDZ)
		cdda_stop();
#endif
		sound_mute(1);
	}
}
