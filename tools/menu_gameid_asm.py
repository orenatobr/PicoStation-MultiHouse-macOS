#!/usr/bin/env python3
"""MIPS routine that makes the PS1 menu select its own virtual card on SD2PSX family devices.

Cards of the SD2PSX / MemCard PRO family switch to another virtual card when they receive a Game ID, which the menu
sends only when it launches a game. The menu's settings and favorites file therefore lands on whichever card is
active, which after a game is that game's card. This routine, called before the menu looks for a Memory Card, sends
the menu's own id (SCES-31337, from its boot file SCES_313.37) to every slot that answers like such a device, and waits
for the device to switch. Slots that do not answer the probe are not touched (original cards see the same probes the
menu already sends at every game launch).

It replaces the body of the unreferenced function at 0x800d42e8 (a CD-ROM helper that nothing calls) and is entered by
redirecting `jal 0x800d8c6c` at 0x800d8db0 (the first card detection of the save and load state machine) to it; it ends
by jumping to 0x800d8c6c with a0 = 0, so the caller sees the original result.

The sender mirrors the Game ID code of the launch flow (0x800de0c8..0x800de300): probe `81 53` and `81 20` (a MemCard
PRO or SD2PSX answers 27 FF at positions 3 and 4), then `81 21 00 len+1 id... 00` without waiting for ACKs.
"""
import struct

BASE = 0x800D42E8
MAX_BYTES = 996            # size of the dead function that is replaced
HOOK_SITE = 0x800D8DB0      # jal 0x800d8c6c  ->  jal BASE
DETECT = 0x800D8C6C
GAME_ID = b"SCES-31337"
WAIT_UNITS = 1_500_000      # about 1.5 s: the device waits 250 ms, then opens the card and loads it from SD
SETTLE_UNITS = 1000

REG = {n: i for i, n in enumerate(
    "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split())}


def _r(x):
    return REG[x]


class Asm:
    """Just enough MIPS I (little endian) for the routine below. Delay slots are written by hand."""

    def __init__(self, base):
        self.base, self.items, self.labels = base, [], {}

    def label(self, name):
        self.labels[name] = self.base + 4 * sum(1 for i in self.items if i[0] != "data") + \
            sum(len(i[1]) for i in self.items if i[0] == "data")
        return self

    def op(self, *a):
        self.items.append(("op",) + a)
        return self

    def data(self, raw):
        self.items.append(("data", raw))
        return self

    def _enc(self, it, pc):
        m, a = it[1], it[2:]
        if m == "nop":
            return 0
        if m in ("addiu", "andi", "ori", "sltiu"):
            code = {"addiu": 9, "andi": 0xC, "ori": 0xD, "sltiu": 0xB}[m]
            return code << 26 | _r(a[1]) << 21 | _r(a[0]) << 16 | (a[2] & 0xFFFF)
        if m == "lui":
            return 0xF << 26 | _r(a[0]) << 16 | (a[1] & 0xFFFF)
        if m in ("lw", "lbu", "lhu", "sb", "sh", "sw"):
            code = {"lw": 0x23, "lbu": 0x24, "lhu": 0x25, "sb": 0x28, "sh": 0x29, "sw": 0x2B}[m]
            return code << 26 | _r(a[2]) << 21 | _r(a[0]) << 16 | (a[1] & 0xFFFF)
        if m in ("or", "and", "addu", "sllv"):
            f = {"or": 0x25, "and": 0x24, "addu": 0x21, "sllv": 4}[m]
            if m == "sllv":                      # sllv rd, rt, rs
                return _r(a[2]) << 21 | _r(a[1]) << 16 | _r(a[0]) << 11 | f
            return _r(a[1]) << 21 | _r(a[2]) << 16 | _r(a[0]) << 11 | f
        if m == "move":
            return _r(a[1]) << 21 | _r(a[0]) << 11 | 0x25
        if m in ("beq", "bne", "beqz", "bnez"):
            lab = a[-1]
            off = (self.labels[lab] - (pc + 4)) >> 2
            if m == "beq":
                return 4 << 26 | _r(a[0]) << 21 | _r(a[1]) << 16 | (off & 0xFFFF)
            if m == "bne":
                return 5 << 26 | _r(a[0]) << 21 | _r(a[1]) << 16 | (off & 0xFFFF)
            return (4 if m == "beqz" else 5) << 26 | _r(a[0]) << 21 | (off & 0xFFFF)
        if m == "b":
            off = (self.labels[a[0]] - (pc + 4)) >> 2
            return 4 << 26 | (off & 0xFFFF)
        if m in ("j", "jal"):
            t = self.labels[a[0]] if isinstance(a[0], str) else a[0]
            return (2 if m == "j" else 3) << 26 | ((t & 0x0FFFFFFF) >> 2)
        raise ValueError(m)

    def build(self):
        out, pc = b"", self.base
        for it in self.items:
            if it[0] == "data":
                out += it[1]
                pc += len(it[1])
            else:
                out += struct.pack("<I", self._enc(it, pc))
                pc += 4
        return out


def routine():
    a = Asm(BASE)
    O = a.op
    frame = 0x68
    O("addiu", "sp", "sp", -frame)
    for off, r in ((0x64, "ra"), (0x60, "s5"), (0x5C, "s4"), (0x58, "s3"), (0x54, "s2"), (0x50, "s1"), (0x4C, "s0")):
        O("sw", r, off, "sp")
    O("addiu", "v0", "zero", 0x53); O("sb", "v0", 0x14, "sp")                      # probe: 53
    O("addiu", "v0", "zero", 0x20); O("sb", "v0", 0x18, "sp")                      # probe: 20 00 00 00 00
    for off in (0x19, 0x1A, 0x1B, 0x1C):
        O("sb", "zero", off, "sp")
    O("move", "s3", "zero")                                                         # mask of slots to switch
    O("move", "s1", "zero")                                                         # slot
    a.label("probe")
    O("addiu", "a0", "sp", 0x2C); O("move", "a1", "zero"); O("addiu", "a2", "zero", 5)
    O("jal", 0x800DFBC0); O("nop")                                                  # memset(out2, 0, 5)
    O("addiu", "a0", "sp", 0x20); O("move", "a1", "zero"); O("addiu", "a2", "zero", 9)
    O("jal", 0x800DFBC0); O("nop")                                                  # memset(out1, 0, 9)
    O("move", "a0", "s1"); O("jal", 0x800D41E4); O("nop")                           # select slot
    O("addiu", "v0", "zero", 9); O("sw", "v0", 0x10, "sp")
    O("addiu", "a0", "zero", 0x81); O("addiu", "a1", "sp", 0x14); O("addiu", "a2", "sp", 0x20)
    O("jal", 0x800D480C); O("addiu", "a3", "zero", 1)                               # 81 53
    O("addiu", "v0", "zero", 5); O("sw", "v0", 0x10, "sp")
    O("addiu", "a0", "zero", 0x81); O("addiu", "a1", "sp", 0x18); O("addiu", "a2", "sp", 0x2C)
    O("jal", 0x800D480C); O("addiu", "a3", "zero", 5)                               # 81 20 00 00 00 00
    O("addiu", "v1", "zero", 5); O("bne", "v0", "v1", "next_probe"); O("nop")
    O("lbu", "v0", 0x2F, "sp"); O("addiu", "v1", "zero", 0x27); O("bne", "v0", "v1", "next_probe"); O("nop")
    O("lbu", "v0", 0x30, "sp"); O("addiu", "v1", "zero", 0xFF); O("bne", "v0", "v1", "next_probe"); O("nop")
    O("addiu", "v0", "zero", 1); O("sllv", "v0", "v0", "s1"); O("or", "s3", "s3", "v0")
    a.label("next_probe")
    O("addiu", "s1", "s1", 1); O("sltiu", "v0", "s1", 2); O("bnez", "v0", "probe"); O("nop")
    O("beqz", "s3", "finish"); O("nop")
    O("move", "s2", "zero")
    a.label("send")
    O("addiu", "v1", "zero", 1); O("sllv", "v1", "v1", "s2"); O("and", "v0", "s3", "v1")
    O("beqz", "v0", "send_next"); O("nop")
    O("move", "a0", "s2"); O("jal", 0x800D41E4); O("nop")                           # select slot
    O("lui", "s4", 0xBF80)
    O("addiu", "v0", "zero", -0x81); O("sh", "v0", 0x1070, "s4")
    O("lhu", "v0", 0x104A, "s4"); O("nop"); O("ori", "v0", "v0", 0x12); O("sh", "v0", 0x104A, "s4")
    O("addiu", "a0", "zero", SETTLE_UNITS); O("jal", 0x800D4738); O("nop")
    O("addiu", "a0", "zero", 1); O("jal", 0x800DEBA0); O("nop")
    O("bnez", "v0", "go"); O("nop")
    O("b", "deselect"); O("nop")
    a.label("go")
    O("addiu", "v0", "zero", 0x81); O("sb", "v0", 0x1040, "s4")
    O("addiu", "a0", "zero", 0x1E); O("jal", 0x800D4738); O("nop")
    O("jal", 0x800D4160); O("nop")
    O("beqz", "v0", "deselect"); O("nop")
    O("lui", "s5", "PKT_HI"); O("ori", "s5", "s5", "PKT_LO")                        # patched below
    O("addiu", "s4", "zero", 3 + len(GAME_ID) + 1)
    a.label("bytes")
    O("lbu", "a0", 0, "s5"); O("addiu", "a1", "sp", 0x34)
    O("jal", 0x800D3204); O("addiu", "s5", "s5", 1)
    O("beqz", "v0", "deselect"); O("nop")
    O("addiu", "a0", "zero", 0x1E); O("jal", 0x800D4738); O("addiu", "s4", "s4", -1)
    O("bnez", "s4", "bytes"); O("nop")
    O("addiu", "a0", "zero", 0x96); O("jal", 0x800D4738); O("nop")
    a.label("deselect")
    O("jal", 0x800D41B4); O("nop")
    a.label("send_next")
    O("addiu", "s2", "s2", 1); O("sltiu", "v0", "s2", 2); O("bnez", "v0", "send"); O("nop")
    O("lui", "a0", WAIT_UNITS >> 16); O("ori", "a0", "a0", WAIT_UNITS & 0xFFFF)
    O("jal", 0x800D4738); O("nop")                                                  # let the device switch card
    a.label("finish")
    for off, r in ((0x64, "ra"), (0x60, "s5"), (0x5C, "s4"), (0x58, "s3"), (0x54, "s2"), (0x50, "s1"), (0x4C, "s0")):
        O("lw", r, off, "sp")
    O("addiu", "sp", "sp", frame)
    O("j", DETECT); O("move", "a0", "zero")
    a.label("PKT")
    pkt = bytes([0x21, 0x00, len(GAME_ID) + 1]) + GAME_ID + b"\x00"
    a.data(pkt + b"\x00" * (-len(pkt) % 4))
    # resolve the packet address in the two placeholders (lui/ori of s5)
    pkt_addr = a.labels["PKT"]
    items = []
    for it in a.items:
        if it[0] == "op" and it[1] == "lui" and it[3] == "PKT_HI":
            it = ("op", "lui", it[2], pkt_addr >> 16)
        if it[0] == "op" and it[1] == "ori" and it[4] == "PKT_LO":
            it = ("op", "ori", it[2], it[3], pkt_addr & 0xFFFF)
        items.append(it)
    a.items = items
    code = a.build()
    assert len(code) <= MAX_BYTES, len(code)
    return code


def hook_word():
    return 3 << 26 | ((BASE & 0x0FFFFFFF) >> 2)


if __name__ == "__main__":
    c = routine()
    print(f"routine: {len(c)} bytes at {BASE:#x}, hook word at {HOOK_SITE:#x} = {hook_word():#010x}")
