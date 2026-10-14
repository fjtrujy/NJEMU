#include <string.h>

#include "common/cache_format_version.h"
#include "common/state_format_version.h"

int main(void)
{
    /* These 8-byte headers are independent of the app version, and older
     * CPS2/MVS processed assets must remain readable after a SemVer bump. */
    static const char cps2[8] = NJEMU_CPS2_CACHE_SIGNATURE;
    static const char mvs[8] = NJEMU_MVS_CACHE_SIGNATURE;
    /* Keep the C terminator in test storage. The on-disk format still
     * writes only the first eight signature bytes. */
    static const char state_cps1[] = NJEMU_CPS1_STATE_SIGNATURE;
    static const char state_cps2[] = NJEMU_CPS2_STATE_SIGNATURE;
    static const char state_mvs[] = NJEMU_MVS_STATE_SIGNATURE;
    static const char state_ncdz[] = NJEMU_NCDZ_STATE_SIGNATURE;

    if (sizeof(cps2) != 8 || sizeof(mvs) != 8)
        return 1;
    if (sizeof(state_cps1) != 9 || sizeof(state_cps2) != 9 ||
        sizeof(state_mvs) != 9 || sizeof(state_ncdz) != 9)
        return 1;
    if (memcmp(cps2, "CPS2V24\0", 8) != 0)
        return 1;
    if (memcmp(mvs, "MVS_V24\0", 8) != 0)
        return 1;
    if (memcmp(state_cps1, "CPS1SV23", 8) != 0 ||
        memcmp(state_cps2, "CPS2SV23", 8) != 0 ||
        memcmp(state_mvs, "MVS_SV23", 8) != 0 ||
        memcmp(state_ncdz, "NCDZSV23", 8) != 0)
        return 1;
    return 0;
}
