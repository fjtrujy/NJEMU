/*
 * Manual PCSX2 stress harness for NJEMU issue #17 (NCDZ CDDA).
 *
 * Compile separately and link into a PS2 NCDZ GUI=OFF test executable
 * with -Wl,--wrap=update_screen. Build ncdz.c without LTO so its
 * update_screen() call remains an external relocation. Never link
 * this hook into a production executable.
 *
 * The game must contain MP3 tracks with suffixes 02.mp3 through 06.mp3.
 */
#include <stdio.h>
#include <stdint.h>
#include "common/emulator_runtime.h"
#include "ncdz/cdda.h"
#include "common/mp3.h"

void __real_update_screen(void);

void __wrap_update_screen(void)
{
    static unsigned int frames;
    unsigned int track;

    __real_update_screen();
    ++frames;
    if (frames < 120)
        return;

    if (frames % 150 == 0) {
        cdda_stop();
        printf("[issue17-pcsx2] frame=%u stop mp3_status=%d\n",
               frames, mp3_get_status());
        fflush(stdout);
    } else if (frames % 30 == 0) {
        track = 2 + ((frames / 30) % 5);
        cdda_play((int)track);
        printf("[issue17-pcsx2] frame=%u play track=%u mp3_status=%d\n",
               frames, track, mp3_get_status());
        fflush(stdout);
    } else if (frames % 300 == 65) {
        cdda_pause();
    } else if (frames % 300 == 75) {
        cdda_resume();
    }

    if (frames == 7200) {
        printf("[issue17-pcsx2] completed 7200 frames, ending\n");
        fflush(stdout);
        Loop = LOOP_EXIT;
    }
}
