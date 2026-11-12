#!/usr/bin/env python3
"""Apply the Memory Card compatibility patch to resources/embedded/picostation_menu.bin.

The PS1 menu detects a Memory Card with `FUN_800d480c` (menu text address 0x800d480c), which waits for the
card's ACK after every byte with a budget of 0x78 units (about 180 microseconds). Reading and writing frames
already use a budget of 2000 units (about 3 ms). A device that acknowledges more slowly than 180 us (some
emulators and adapters) is therefore "not found" although it would serve frames. The patch raises the two
detection budgets to 2000: `addiu $a0, $zero, 0x78` at 0x800d48b4 and 0x800d48fc becomes `addiu $a0, $zero, 0x7d0`.
Devices that answer quickly are unaffected (the wait ends at the first ACK); a missing card costs a few more
milliseconds.

Second part: the menu starts the first byte only 0x96 (detection), 0x64 (frame read) or 0x50 (frame write) delay units
(about 1 microsecond each) after /CS goes low. A device that is not listening yet at that moment (a microcontroller
waking up) loses the byte and is not found. These are minimum waits, so the three `addiu $a0, $zero, N` become 1000
(about 1 ms): every frame operation takes about 1 ms longer, nothing else changes.

Third part: a frame that fails to write or to read back is retried 7 times (data) or 9 times (directory), one attempt per
video frame, which covers a device that stays busy for only about 100 ms after a write. Both limits become 30 (about 2 s),
so the save only reports an error after a longer wait. Nothing else is touched.

The raw sectors that contain the instructions get fresh EDC and ECC (ECMA-130 Mode 2 Form 1), so the
sector check done by the Manager (`mh_menu_validate_blob`) and by CD hardware keeps passing.

usage: patch_menu.py [--check] [path]      default path: resources/embedded/picostation_menu.bin
  --check  do not write; exit 1 when the file is not fully patched
The tool is idempotent and refuses a file whose instructions are neither original nor patched.
"""
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_gameid_asm as gid   # noqa: E402  (the Game ID routine, see that file)

RAW = 2352
TEXT_BASE = 0x800D0000
EXE_LBA = 24            # PS-X EXE (SCES_313.37) starts at sector 24, 2048 byte user data per sector
EXE_HEADER = 0x800      # PS-X EXE header size; the text segment follows
# (text address of the instruction, original word, patched word): addiu $a0, $zero, imm
PATCHES = [
    # ACK wait of the card detection (0x78 -> 2000 units)
    (0x800D48B4, 0x24040078, 0x240407D0),
    (0x800D48FC, 0x24040078, 0x240407D0),
    # settle time between /CS going low and the first byte, in detection, frame read and frame write
    # (0x96, 0x64 and 0x50 units -> 1000 units, about 1 ms)
    (0x800D484C, 0x24040096, 0x240403E8),
    (0x800DEA94, 0x24040064, 0x240403E8),
    (0x800D4B94, 0x24040050, 0x240403E8),
    # retries of a Memory Card frame that failed to write or read back, in the save state machine (FUN_800d4c50):
    # data frames 7 -> 30 attempts (sltiu $v0, $v0, 7), directory frame 9 -> 30 (sltiu $v0, $v0, 9)
    (0x800D51B4, 0x2C420007, 0x2C42001E),
    (0x800D5244, 0x2C420009, 0x2C42001E),
    # first card detection of the save/load state machine now goes through the Game ID routine (menu_gameid_asm.py)
    (gid.HOOK_SITE, 3 << 26 | ((gid.DETECT & 0x0FFFFFFF) >> 2), gid.hook_word()),
]
# (text address, length, SHA-256 of the original bytes, new bytes): the unreferenced CD-ROM helper at 0x800d42e8 is
# replaced by the Game ID routine and the rest of it is zeroed
REGIONS = [
    (gid.BASE, gid.MAX_BYTES, "8ddfd20035ca2a6c79393efd44cd65566c5dc6786464cf9d19e5542acc911c71",
     gid.routine() + bytes(gid.MAX_BYTES - len(gid.routine()))),
]


def _build_tables():
    ecc_f = [0] * 256
    ecc_b = [0] * 256
    edc = [0] * 256
    for i in range(256):
        j = (i << 1) ^ (0x11D if i & 0x80 else 0)
        ecc_f[i] = j & 0xFF
        ecc_b[i ^ j & 0xFF] = i
        e = i
        for _ in range(8):
            e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
        edc[i] = e
    return ecc_f, ecc_b, edc


ECC_F, ECC_B, EDC = _build_tables()


def edc_compute(data: bytes) -> int:
    """EDC of ECMA-130 (reflected CRC, polynomial 0xD8018001)."""
    e = 0
    for b in data:
        e = (e >> 8) ^ EDC[(e ^ b) & 0xFF]
    return e


def _ecc(src: bytes, major: int, minor: int, mult: int, inc: int) -> bytes:
    size = major * minor
    out = bytearray(major * 2)
    for m in range(major):
        idx = (m >> 1) * mult + (m & 1)
        a = b = 0
        for _ in range(minor):
            t = src[idx]
            idx += inc
            if idx >= size:
                idx -= size
            a ^= t
            b ^= t
            a = ECC_F[a]
        a = ECC_B[ECC_F[a] ^ b]
        out[m] = a
        out[m + major] = a ^ b
    return bytes(out)


def seal_form1(sector: bytearray) -> None:
    """Recompute EDC, P and Q parity of a raw Mode 2 Form 1 sector in place.

    Q covers the P parity, so P is written into the covered bytes before Q is computed.
    """
    struct.pack_into("<I", sector, 0x818, edc_compute(bytes(sector[0x10:0x818])))
    body = bytearray(sector[0x0C:0x0C + 2340])
    body[0:4] = b"\x00\x00\x00\x00"      # Mode 2: the address field is not covered by the ECC
    p = _ecc(bytes(body), 86, 24, 2, 86)
    body[0x81C - 0x0C:0x81C - 0x0C + 172] = p
    q = _ecc(bytes(body), 52, 43, 86, 88)
    sector[0x81C:0x81C + 172] = p
    sector[0x8C8:0x8C8 + 104] = q


def is_form1(sector: bytes) -> bool:
    return not (sector[0x12] & 0x20)


def self_test(blob: bytes) -> int:
    """Recompute EDC/ECC of every Form 1 sector of the untouched image and compare. Returns the sector count."""
    n = 0
    for s in range(len(blob) // RAW):
        sec = bytearray(blob[s * RAW:(s + 1) * RAW])
        if not is_form1(sec):
            continue
        ref = bytes(sec)
        seal_form1(sec)
        if bytes(sec) != ref:
            raise SystemExit(f"EDC/ECC self test failed on sector {s}: the tool cannot be trusted on this image")
        n += 1
    return n


def locate(addr: int):
    """Return (sector index, offset of the 32 bit word in the raw sector) for a text address."""
    cooked = EXE_LBA * 2048 + EXE_HEADER + (addr - TEXT_BASE)
    sector, off = divmod(cooked, 2048)
    if off > 2048 - 4:
        raise SystemExit("instruction crosses a sector boundary")
    return sector, sector * RAW + 24 + off


def main(argv) -> int:
    check = "--check" in argv
    args = [a for a in argv[1:] if not a.startswith("--")]
    path = Path(args[0] if args else "resources/embedded/picostation_menu.bin")
    blob = bytearray(path.read_bytes())
    n = self_test(bytes(blob))
    touched = set()
    state = []
    for addr, orig, new in PATCHES:
        sector, pos = locate(addr)
        word = struct.unpack_from("<I", blob, pos)[0]
        if word == new:
            state.append("patched")
        elif word == orig:
            state.append("original")
            if not check:
                struct.pack_into("<I", blob, pos, new)
                touched.add(sector)
        else:
            raise SystemExit(f"unexpected word {word:#010x} at {addr:#x}: not the menu this patch was written for")
    for addr, length, sha, new in REGIONS:
        # the region may span sectors: read and write it byte by byte through the cooked offsets
        cooked0 = EXE_LBA * 2048 + EXE_HEADER + (addr - TEXT_BASE)

        def raw_pos(i):
            sec, off = divmod(cooked0 + i, 2048)
            return sec, sec * RAW + 24 + off
        cur = bytes(blob[raw_pos(i)[1]] for i in range(length))
        if cur == new:
            state.append("patched")
        elif hashlib.sha256(cur).hexdigest() == sha:
            state.append("original")
            if not check:
                for i in range(length):
                    sec, pos = raw_pos(i)
                    blob[pos] = new[i]
                    touched.add(sec)
        else:
            raise SystemExit(f"unexpected bytes in the region at {addr:#x}: not the menu this patch was written for")
    for sector in sorted(touched):
        sec = bytearray(blob[sector * RAW:(sector + 1) * RAW])
        seal_form1(sec)
        blob[sector * RAW:(sector + 1) * RAW] = sec
    if check:
        ok = all(s == "patched" for s in state)
        print(f"self test ok on {n} Form 1 sectors; {'patched' if ok else 'NOT patched'}: {state}")
        return 0 if ok else 1
    if touched:
        path.write_bytes(bytes(blob))
    print(f"self test ok on {n} Form 1 sectors; {state} -> sectors rewritten: {sorted(touched)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
