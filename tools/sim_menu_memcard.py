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
import os
import struct
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

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

    def __init__(self, first_ack_us, ack_us, ready_us=0.0, busy_after_write_us=0.0):
        self.busy_after_write_us = busy_after_write_us   # ignores /CS this long after the end of a write
        self.wrote = False
        self.ready_us = ready_us          # bytes sent earlier than this after /CS goes low are lost
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


class McpCard(Card):
    """SD2PSX / MemCard PRO family model: answers 27 FF to `81 20`, switches virtual card on `81 21` (Game ID).

    The switch makes the card ignore /CS for `reconnect_us` (it simulates a card swap). Every id has its own
    virtual card; ids that were never seen get an empty one.
    """

    def __init__(self, active, reconnect_us=300_000.0, **kw):
        Card.__init__(self, 8, 8, **kw)
        self.channels = {}
        self.active = active
        self.reconnect_us = reconnect_us
        self.sent_ids = []
        self.id_buf = bytearray()
        self.channels[active] = self.mem

    def switch_to(self, ident):
        if ident not in self.channels:
            self.channels[ident] = bytearray(0x20000)
        self.active = ident
        self.mem = self.channels[ident]

    def exchange(self, b):
        i = self.idx
        if i == 1 and b in (0x20, 0x21, 0x53):
            self.cmd = b
        if self.cmd == 0x20 and i >= 1:
            self.idx += 1
            return (0x27 if i == 4 else 0xFF if i == 5 else 0x08), i < 5
        if self.cmd == 0x21 and i >= 1:
            self.idx += 1
            if i >= 4:
                self.id_buf.append(b)
            return 0x08, False                     # these devices do not ACK a Game ID
        if self.cmd == 0x53 and i >= 1:
            self.idx += 1
            return (0x5A, 0x5D, 0x5C, 0x5D, 0x04, 0x00, 0x00, 0x80)[min(i - 1, 7)], i < 8
        return Card.exchange(self, b)

    def on_deselect(self):
        if self.cmd == 0x21 and self.id_buf:
            ident = bytes(self.id_buf).split(b"\0")[0].decode("ascii", "replace")
            self.sent_ids.append(ident)
            self.id_buf = bytearray()
            if ident != self.active:
                self.switch_to(ident)
            return self.reconnect_us
        return 0.0


class Machine:
    def __init__(self, text, card):
        self.uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN)
        self.uc.mem_map(0, 0x200000)                    # RAM (physical)
        self.uc.mem_map(0x1F800000, 0x2000)              # I/O page
        self.uc.mem_write(TEXT_BASE & 0x1FFFFFFF, text)
        self.card = card
        self.cycles = 0
        self.start = 0
        self.reg = {}
        self.ctrl = 0
        self.istat = 0
        self.rx = []                 # (ready_cycle, byte)
        self.ack_cycle = None
        self.tx_done = 0
        self.selected = False
        self.sel_cycle = 0
        self.busy_until = 0
        self.uc.hook_add(UC_HOOK_CODE, self._tick)
        self.uc.hook_add(UC_HOOK_MEM_READ, self._io_read, begin=0x1F801000, end=0x1F801FFF)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._io_write, begin=0x1F801000, end=0x1F801FFF)

    def us(self):
        return (self.cycles - self.start) * 1e6 / CPU_HZ

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
                if (self.cycles < self.sel_cycle + int(self.card.ready_us * CPU_HZ / 1e6)
                        or self.cycles < self.busy_until):
                    # the device is not listening yet: nothing answers, no ACK
                    self.tx_done = self.cycles + int(BYTE_US * CPU_HZ / 1e6)
                    self.rx.append((self.tx_done, 0xFF))
                    return
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
                self.card.reset(); self.rx.clear(); self.sel_cycle = self.cycles
            if not self.selected and was:
                hook = getattr(self.card, "on_deselect", None)
                if hook:
                    self.busy_until = max(self.busy_until, self.cycles + int(hook() * CPU_HZ / 1e6))
                if self.card.cmd == 0x57 and self.card.idx >= 138:
                    self.busy_until = self.cycles + int(self.card.busy_after_write_us * CPU_HZ / 1e6)
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
        self.start = self.cycles
        u.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: uc.emu_stop() if a == STOP else None,
                   begin=STOP, end=STOP)
        u.emu_start(addr, STOP, count=limit)
        return struct.unpack("<i", struct.pack("<I", u.reg_read(UC_MIPS_REG_V0) & 0xFFFFFFFF))[0]


def detect(text, first_ack_us, ack_us, ready_us=0.0):
    m = Machine(text, Card(first_ack_us, ack_us, ready_us))
    m.uc.mem_write(0x100000, bytes([0x52, 0x00]))            # command bytes after the select byte
    n = m.call(0x800D480C, (0x81, 0x80100000, 0x80100100, 2), (4,))
    return n, m.us()


def frame_read(text, frame, first_ack_us=8, ack_us=8, ready_us=0.0):
    m = Machine(text, Card(first_ack_us, ack_us, ready_us))
    ok = m.call(0x800DEA3C, (0, frame))
    return ok, bytes(m.uc.mem_read(0x800E7860 & 0x1FFFFFFF, 128)), m.card


def frame_write(text, frame, data, first_ack_us=8, ack_us=8, ready_us=0.0):
    m = Machine(text, Card(first_ack_us, ack_us, ready_us))
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


def sweep(paths):
    """Which device profiles does each blob handle? (detect / frame read / frame write)"""
    pat = bytes((i * 3 + 1) & 0xFF for i in range(128))
    profiles = [("normal", 8, 8, 0)] + [(f"settle {r} us", 8, 8, r) for r in (100, 200, 300, 500, 1000, 2000)] + \
        [(f"ACK {a} us on every byte", a, a, 0) for a in (100, 200, 400, 800, 1500)]
    for path in paths:
        text = carve_text(path)
        print(f"== {path}")
        for name, f, a, r in profiles:
            n, _ = detect(text, f, a, r)
            ok_r, got, card = frame_read(text, 5, f, a, r)
            ok_w, stored = frame_write(text, 9, pat, f, a, r)
            want = bytes(card.mem[5 * 128:6 * 128])
            print(f"  {name:26} detect={'yes' if n > 0 else 'NO ':3}  read={'yes' if ok_r and got == want else 'NO '}  "
                  f"write={'yes' if ok_w and stored == pat else 'NO '}")


def busy_sweep(paths):
    """Write a frame, then read it back at once (as the menu's save does) with a device that ignores /CS for a while."""
    pat = bytes((i * 5 + 2) & 0xFF for i in range(128))
    for path in paths:
        text = carve_text(path)
        print(f"== {path}")
        for busy_ms in (0, 5, 20, 50, 70, 100, 200):
            m = Machine(text, Card(8, 8, 0, busy_after_write_us=busy_ms * 1000.0))
            m.uc.mem_write(0x100000, pat)
            w = m.call(0x800D4B00, (0, 9, 0x80100000))
            r = m.call(0x800DEA3C, (0, 9))
            back = bytes(m.uc.mem_read(0x800E7860 & 0x1FFFFFFF, 128))
            print(f"  busy {busy_ms:3} ms after a write: write ok={w}  read back ok={r}  data equal={back == pat}")


def retries(paths):
    """Run the menu's save state machine (FUN_800d4c50) against a card that never answers; count the attempts."""
    for path in paths:
        text = carve_text(path)
        print(f"== {path}")
        for state, name in ((5, "data frame write"), (6, "directory frame write")):
            m = Machine(text, Card(8, 8, ready_us=1e9))        # never listens
            ram = lambda a: a & 0x1FFFFFFF
            m.uc.mem_write(ram(0x800E77C8), struct.pack("<I", state))     # state
            m.uc.mem_write(ram(0x800E77CC), struct.pack("<I", 0))         # slot 0
            m.uc.mem_write(ram(0x800E77D2), struct.pack("<H", 0))         # block
            calls = 0
            while calls < 500:
                r = m.call(0x800D4C50, (0x80100000,))
                calls += 1
                if r != 1:
                    break
            print(f"  {name:22}: gave up after {calls} attempts (result {r})")


def patched_text(path):
    """The blob's text with the Game ID routine applied in memory (what tools/patch_menu.py writes)."""
    import menu_gameid_asm as g
    t = bytearray(carve_text(path))
    code = g.routine()
    off = g.BASE - TEXT_BASE
    t[off:off + g.MAX_BYTES] = code + bytes(g.MAX_BYTES - len(code))
    struct.pack_into("<I", t, g.HOOK_SITE - TEXT_BASE, g.hook_word())
    return bytes(t)


def gameid(path):
    """Does the menu land on its own virtual card? (original blob text vs the same text with the routine applied)"""
    import menu_gameid_asm as g
    text = carve_text(path)
    fav = bytes((0xA0 + i) & 0xFF for i in range(128))
    entry = {"menu blob": (text, 0x800D8C6C), "with the Game ID routine": (patched_text(path), g.BASE)}
    for label, (t, fn) in entry.items():
        print(f"== {label}")
        for name, mk in (
            ("SD2PSX family, last game still active", lambda: McpCard("SLUS-01251")),
            ("same, 2 s to switch card", lambda: McpCard("SLUS-01251", reconnect_us=2_000_000.0)),
            ("original card (does not answer 27 FF)", lambda: Card(8, 8)),
        ):
            card = mk()
            if isinstance(card, McpCard):
                card.channels["SCES-31337"] = bytearray(0x20000)
                card.channels["SCES-31337"][5 * 128:6 * 128] = fav
            m = Machine(t, card)
            found = m.call(fn, (0,))
            took = m.us()
            ok_r = m.call(0x800DEA3C, (0, 5)) if found > 0 else 0
            back = bytes(m.uc.mem_read(0x800E7860 & 0x1FFFFFFF, 128))
            where = getattr(card, "active", "-")
            sent = getattr(card, "sent_ids", [])
            print(f"  {name:40} found={'yes' if found else 'NO '} active card={where:11} ids sent={sent} "
                  f"menu favorites read={'yes' if ok_r and back == fav else 'no '} ({took / 1000:7.1f} ms)")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--gameid"]:
        gameid(sys.argv[2])
    elif sys.argv[1:2] == ["--retries"]:
        retries(sys.argv[2:])
    elif sys.argv[1:2] == ["--busy"]:
        busy_sweep(sys.argv[2:])
    elif sys.argv[1:2] == ["--sweep"]:
        sweep(sys.argv[2:])
    else:
        main(sys.argv[1:])
