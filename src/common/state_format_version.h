#ifndef NJEMU_STATE_FORMAT_VERSION_H
#define NJEMU_STATE_FORMAT_VERSION_H

/* Eight-byte save-state file signatures. They identify serialized state
 * formats, not the NJEMU release version; never bump them for SemVer alone. */
#define NJEMU_CPS1_STATE_SIGNATURE "CPS1SV23"
#define NJEMU_CPS2_STATE_SIGNATURE "CPS2SV23"
#define NJEMU_MVS_STATE_SIGNATURE  "MVS_SV23"
#define NJEMU_NCDZ_STATE_SIGNATURE "NCDZSV23"

#endif /* NJEMU_STATE_FORMAT_VERSION_H */
