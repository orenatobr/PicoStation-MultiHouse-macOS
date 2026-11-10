/* Internal helpers shared by the disc module sources. */
#ifndef MH_DISC_INTERNAL_H
#define MH_DISC_INTERNAL_H

#include "mh/disc.h"

uint32_t mh_rd32(const uint8_t *p);
uint16_t mh_rd16(const uint8_t *p);
uint64_t mh_rd64(const uint8_t *p);

/* Disc index record i (0x80 bytes: serial[16], title[64], ...), NULL if out of range. */
const uint8_t *mh_db_disc_rec(const MhDb *db, uint32_t idx);
/* 16 byte track entry, NULL if outside the blob. */
const uint8_t *mh_db_entry_ptr(const MhDb *db, uint32_t entry);
/* Alias record (32 bytes: key[16], canonical[16]) by binary search, NULL if absent. */
const uint8_t *mh_db_alias_find(const MhDb *db, const char *key);

#endif
