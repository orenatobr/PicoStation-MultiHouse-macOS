#!/usr/bin/env python3
"""Flatten a UF2 file into a raw binary image.

usage: python3 -I tools/uf2_to_bin.py in.uf2 out.bin
Prints the base address; gaps between blocks are filled with 0xFF (erased flash).
"""
import struct
import sys

MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)


def uf2_to_bin(data):
    blocks = {}
    for off in range(0, len(data) - 511, 512):
        m0, m1, flags, addr, size, _num, _tot, _fam = struct.unpack_from("<8I", data, off)
        if (m0, m1) != MAGIC[:2] or struct.unpack_from("<I", data, off + 508)[0] != MAGIC[2]:
            raise ValueError("bad UF2 block at 0x%x" % off)
        if flags & 1:  # not-main-flash
            continue
        blocks[addr] = data[off + 32:off + 32 + size]
    base = min(blocks)
    end = max(a + len(b) for a, b in blocks.items())
    out = bytearray(b"\xff" * (end - base))
    for a, b in blocks.items():
        out[a - base:a - base + len(b)] = b
    return base, bytes(out)


if __name__ == "__main__":
    base, img = uf2_to_bin(open(sys.argv[1], "rb").read())
    open(sys.argv[2], "wb").write(img)
    print("base 0x%08x size 0x%x" % (base, len(img)))
