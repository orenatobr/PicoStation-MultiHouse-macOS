#!/usr/bin/env python3
"""Run the real MIPS code of the PS1 menu's Memory Card routines against a modelled card.

This executes the unmodified machine code of `picostation_menu.bin` (original or patched) in the Unicorn CPU
emulator, with a model of the SIO0 port and of a PS1 Memory Card whose ACK latency can be set. It answers one
question without hardware: does the detection routine (text address 0x800d480c, used by `FUN_800d8c6c`) find a
card that acknowledges after a given delay, and do the frame read (`FUN_800dea3c`) and write (`FUN_800d4b00`)
routines still work? Time is counted in CPU cycles (33.8688 MHz, one cycle per instruction), so the figures are
approximate; the point is the comparison between the two blobs.

usage: sim_menu_memcard.py <picostation_menu.bin> [<other.bin> ...]       (needs: pip install unicorn)
"""
import struct
import sys

from unicorn import (UC_ARCH_MIPS, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_LITTLE_ENDIAN,
                     UC_MODE_MIPS32, Uc)
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_RA, \
    UC_MIPS_REG_SP, UC_MIPS_REG_V0

CPU_HZ = 33_868_800
RAW = 2352
TEXT_BASE = 0x800D0000
EXE_LBA = 24
STOP = 0x80001000        # return address that ends a call
BYTE_US = 32.0           # 8 bits at 250 kHz


def carve_text(path):
    blob = open(path, "rb").read()
    cooked = b"".join(blob[s * RAW + 24:s * RAW + 24 + 2048] for s in range(len(blob) // RAW))
    start = EXE_LBA * 2048
    assert cooked[start:start + 8] == b"PS-X EXE"
    size = struct.unpack_from("<I", cooked, start + 0x1C)[0]
    return cooked[start + 0x800:start + 0x800 + size]


class Card:
    """PS1 Memory Card protocol (psx-spx): replies are one byte behind the host bytes."""

    def __init__(self, first_ack_us, ack_us):
        self.mem = bytearray(0x20000)
        for i in range(len(self.mem)):
            self.mem[i] = (i * 7 + 3) & 0xFF
        self.first_ack_us, self.ack_us = first_ack_us, ack_us
        self.reset()

    def reset(self):
        self.idx, self.cmd, self.addr, self.chk = 0, 0, 0, 0
        self.prev_data = 0

    def exchange(self, b):
        """Host byte -> (reply byte, acknowledges this byte)."""
        i, r = self.idx, 0xFF
        if i == 0:
            r = 0xFF
        elif i == 1:
            self.cmd = b
            r = 0x08
        elif i == 2:
            r = 0x5A
        elif i == 3:
            r = 0x5D
        elif self.cmd == 0x52:
            if i == 4:
                self.addr = b << 8; r = 0x00
            elif i == 5:
                self.addr |= b; self.chk = (self.addr >> 8) ^ (self.addr & 0xFF); r = self.addr >> 8
            elif i == 6:
                r = 0x5C
            elif i == 7:
                r = 0x5D
            elif i == 8:
                r = self.addr >> 8
            elif i == 9:
                r = self.addr & 0xFF
            elif 10 <= i < 138:
                d = self.mem[self.addr * 128 + (i - 10)]
                self.chk ^= d
                r = d
            elif i == 138:
                r = self.chk
            elif i == 139:
                r = 0x47
        elif self.cmd == 0x57:
            if i == 4:
                self.addr = b << 8; r = 0x00
            elif i == 5:
                self.addr |= b; self.chk = (self.addr >> 8) ^ (self.addr & 0xFF); r = self.addr >> 8
            elif 6 <= i < 134:
                self.chk ^= b
                self.mem[self.addr * 128 + (i - 6)] = b
                r = self.addr & 0xFF if i == 6 else self.prev_data
                self.prev_data = b
            elif i == 134:
                self.recv = b; r = self.prev_data
            elif i == 135:
                r = 0x5C
            elif i == 136:
                r = 0x5D
            elif i == 137:
                r = 0x47 if self.recv == self.chk else 0x4E
        self.idx += 1
        last = (self.cmd == 0x52 and i == 139) or (self.cmd == 0x57 and i == 137)
        return r, not last


class Machine:
    def __init__(self, text, card):
        self.uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN)
        self.uc.mem_map(0, 0x200000)                    # RAM (physical)
        self.uc.mem_map(0x1F800000, 0x2000)              # I/O page
        self.uc.mem_write(TEXT_BASE & 0x1FFFFFFF, text)
        self.card = card
        self.cycles = 0
        self.reg = {}
        self.ctrl = 0
        self.istat = 0
        self.rx = []                 # (ready_cycle, byte)
        self.ack_cycle = None
        self.tx_done = 0
        self.selected = False
        self.uc.hook_add(UC_HOOK_CODE, self._tick)
        self.uc.hook_add(UC_HOOK_MEM_READ, self._io_read, begin=0x1F801000, end=0x1F801FFF)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._io_write, begin=0x1F801000, end=0x1F801FFF)

    def us(self):
        return self.cycles * 1e6 / CPU_HZ

    def _tick(self, uc, addr, size, _):
        self.cycles += 1
        if self.ack_cycle is not None and self.cycles >= self.ack_cycle:
            self.istat |= 0x80
            self.ack_cycle = None

    def _stat(self):
        s = 0x01 | (0x04 if self.cycles >= self.tx_done else 0)
        if self.rx and self.cycles >= self.rx[0][0]:
            s |= 0x02
        return s

    def _io_read(self, uc, access, addr, size, value, _):
        a = addr & 0xFFF
        if a == 0x044:
            v = self._stat()
        elif a == 0x040:
            v = 0xFF
            if self.rx and self.cycles >= self.rx[0][0]:
                v = self.rx.pop(0)[1]
        elif a == 0x070:
            v = self.istat
        elif a == 0x04A:
            v = self.ctrl
        else:
            v = 0
        uc.mem_write(0x1F801000 + a, struct.pack("<I", v))

    def _io_write(self, uc, access, addr, size, value, _):
        a = addr & 0xFFF
        if a == 0x040:
            if self.selected:
                first = self.card.idx == 0
                reply, acks = self.card.exchange(value & 0xFF)
                done = self.cycles + int(BYTE_US * CPU_HZ / 1e6)
                self.tx_done = done
                self.rx.append((done, reply))
                if acks:
                    lat = self.card.first_ack_us if first else self.card.ack_us
                    self.ack_cycle = done + int(lat * CPU_HZ / 1e6)
        elif a == 0x04A:
            was = self.selected
            self.ctrl = value & 0xFFFF
            self.selected = bool(self.ctrl & 0x2) and not (self.ctrl & 0x2000)   # DTR on, slot 1
            if self.selected and not was:
                self.card.reset(); self.rx.clear()
            if not self.selected and was:
                self.card.reset(); self.rx.clear(); self.ack_cycle = None
        elif a == 0x070:
            self.istat &= value & 0xFFFF
        elif a == 0x044:
            pass

    def call(self, addr, args=(), stack=(), limit=400_000_000):
        u = self.uc
        for r, v in zip((UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3), args):
            u.reg_write(r, v & 0xFFFFFFFF)
        sp = 0x1FFF00 & 0x1FFFFFFF
        u.mem_write(sp + 16, b"".join(struct.pack("<I", s) for s in stack))
        u.reg_write(UC_MIPS_REG_SP, sp)
        u.reg_write(UC_MIPS_REG_RA, STOP)
        self.cycles = 0
        u.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: uc.emu_stop() if a == STOP else None,
                   begin=STOP, end=STOP)
        u.emu_start(addr, STOP, count=limit)
        return struct.unpack("<i", struct.pack("<I", u.reg_read(UC_MIPS_REG_V0) & 0xFFFFFFFF))[0]


def detect(text, first_ack_us, ack_us):
    m = Machine(text, Card(first_ack_us, ack_us))
    m.uc.mem_write(0x100000, bytes([0x52, 0x00]))            # command bytes after the select byte
    n = m.call(0x800D480C, (0x81, 0x80100000, 0x80100100, 2), (4,))
    return n, m.us()


def frame_read(text, frame):
    m = Machine(text, Card(8, 8))
    ok = m.call(0x800DEA3C, (0, frame))
    return ok, bytes(m.uc.mem_read(0x800E7860 & 0x1FFFFFFF, 128)), m.card


def frame_write(text, frame, data):
    m = Machine(text, Card(8, 8))
    m.uc.mem_write(0x100000, data)
    ok = m.call(0x800D4B00, (0, frame, 0x80100000))
    return ok, bytes(m.card.mem[frame * 128:frame * 128 + 128])


def main(paths):
    for path in paths:
        text = carve_text(path)
        print(f"== {path}")
        for label, f, a in (("fast card (ACK 8 us)", 8, 8), ("first ACK 150 us", 150, 8),
                            ("first ACK 400 us", 400, 8), ("first ACK 1 ms", 1000, 8),
                            ("first ACK 2.5 ms", 2500, 8), ("first ACK 6 ms", 6000, 8),
                            ("every ACK 400 us", 400, 400)):
            n, t = detect(text, f, a)
            print(f"  detect, {label:22} -> bytes acknowledged {n}  ({'FOUND' if n > 0 else 'NOT FOUND'}, {t:7.0f} us)")
        card0 = Card(8, 8)
        ok, got, _ = frame_read(text, 5)
        want = bytes(card0.mem[5 * 128:6 * 128])
        print(f"  frame read  : ok={ok} data matches card = {got == want}")
        pat = bytes((i * 3 + 1) & 0xFF for i in range(128))
        ok, stored = frame_write(text, 9, pat)
        print(f"  frame write : ok={ok} card holds the data = {stored == pat}")


if __name__ == "__main__":
    main(sys.argv[1:])
