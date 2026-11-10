/* mh/base.h: umbrella header of the `base` module (wave 0). Everything else builds on this.
 *
 *   mh/common.h    MH_PATH_MAX, mh_alloc*, u64 helpers, MhProgress, MhErr, mh_err_set
 *   mh/str.h       mh_str_*, normalisation helpers, hex, MhWbuf string builder
 *   mh/path.h      path strings, files, whole-file read/write, MhCollect (drag and drop)
 *   mh/hash.h      CRC-32, SHA-1, SHA-256
 *   mh/cdsector.h  CD sector EDC/ECC check and repair, V2 track map sector validation
 *   mh/io.h        MhIo positional I/O with verify, plus fd helpers
 *
 * Map of original functions to these names: docs/map/base.md. */
#ifndef MH_BASE_H
#define MH_BASE_H

#include "mh/cdsector.h"
#include "mh/common.h"
#include "mh/hash.h"
#include "mh/io.h"
#include "mh/path.h"
#include "mh/str.h"

#endif
