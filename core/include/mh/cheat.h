/* Cheat module: embedded GameShark V5 database, cheat profiles, PSTCHT1 blobs. */
#ifndef MH_CHEAT_H
#define MH_CHEAT_H

#include <stdint.h>
#include <stddef.h>

typedef struct MhCheatProfile MhCheatProfile;

/* orig: cheat_profile_free @0x14000c9b0. Accepts NULL. */
void mh_cheat_profile_free(MhCheatProfile *p);

#endif
