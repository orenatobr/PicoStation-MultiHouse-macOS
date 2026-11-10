#!/usr/bin/env python3
"""Carve the data blobs embedded in 'PicoStation Multi House V3 (x64).exe'.

The original program links these blobs into its .data section between linker symbols
(picostation_menu_start/end, pst_aliases_start/end, ...). The virtual addresses below were
read from the executable's symbol table. Output goes to resources/embedded/.

Usage: extract_embedded.py <path-to-exe> [out-dir]
"""
import hashlib
import os
import struct
import sys

IMAGE_BASE = 0x140000000

# name -> (start VA, end VA)
BLOBS = {
    "case_template.bgra": (0x140040000, 0x14019F900),
    "picostation_menu.bin": (0x14019F900, 0x1401D4F70),
    "aliases.bin": (0x1401D4F70, 0x1401E0B90),
    "cdx_cheats.bin": (0x1401E0B90, 0x1402E3855),
    "disc_index.bin": (0x1402E3855, 0x1404EE6E5),
    "redump_index.bin": (0x1404EE6E5, 0x140AF8625),
    "trackset_index.bin": (0x140AF8625, 0x140E05EDD),
    "umbrella_music.vag": (0x140E05EDD, 0x140E7AD7D),
}


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    off = pe + 24 + opt
    out = []
    for i in range(nsec):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", d, off + 40 * i)
        out.append((name.rstrip(b"\0").decode(), IMAGE_BASE + va, vsize, raw, rsize))
    return out


def va_to_off(secs, va):
    for _, sva, vsize, raw, rsize in secs:
        if sva <= va < sva + max(vsize, rsize):
            return raw + (va - sva)
    raise ValueError(hex(va))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "embedded")
    out = sys.argv[2] if len(sys.argv) > 2 else default_out
    os.makedirs(out, exist_ok=True)
    secs = sections(d)
    for name, (a, b) in BLOBS.items():
        start = va_to_off(secs, a)
        blob = d[start:start + (b - a)]
        with open(os.path.join(out, name), "wb") as f:
            f.write(blob)
        print("%-22s %8d  sha256=%s" % (name, len(blob), hashlib.sha256(blob).hexdigest()))


if __name__ == "__main__":
    main()
