#!/usr/bin/env python3
"""RSOUND.EXE (Roland MT-32 driver) extractor.

Parses the MicroProse RSOUND overlay from F-15 SE II and F-19, harvests the
embedded SysEx timbre dump + all sound/music bytecode streams, and emits
C include tables for the port (rsound_data_<game>.inc).

Driver model (from disasm of both binaries; identical interpreter, shifted
data addresses):

  channel state, stride 0x22:
    [0]  wait ticks          [1]  bend step (signed)   [2]  volsweep step (signed)
    [3]  pansweep step (signed)                       [4]  last note
    [6]  velocity            [7]  keyoff gap           [8]  noteoff countdown
    [9]  volsweep tick       [0xa] bendfx flag         [0xb] pansweep tick
    [0xc] CC7  (volume)      [0xd] CC10 (pan)          [0xe] volsweep rate
    [0xf] pansweep rate      [0x10] bend count         [0x11] bend delay
    [0x12] stream base       [0x14] stream pos         [0x16/0x18] loop marks
    [0x1a/0x1c] loop counts  [0x1e] bend14             [0x20] end callback

  ops: 0x00-0x7f = note, next byte = duration ticks (note==0 or dur==0 -> all-notes-off)
       f2 p        CC1 modulation            f8 r,s      volume sweep (rate, step)
       f3 r,s      pan sweep (s==0 -> 0x40)  f9 v        velocity
       f4 v        CC10 pan                  fa d,s,l    pitch sweep (delay, signed step, len)
       f5 n        poly note-on (n notes)    fb g        keyoff gap
       f6 v        CC7 volume                fc p        program change
       f7 b        pitch bend (b<<8)         fd          end (callback else loop to base)
       fe n / ff n nested loops (count; 0 = end marker -> skip)

  per-tick: step 6 music channels, then the SFX/drone channel (program 10,
  note 0x28, bend+pan track the drone-pitch word written by slot 0x6a;
  0x68/0x69 toggle it via the drone flag).

  variant pick: handlers load cx from a 4-entry code table indexed by the
  PRNG low bits; tick does rng = ror16(rng + scramble, 3).

  slot table (first_slot=0x64, identical ABI to ASOUND):
    0x64 setup (MPU reset + upload timbre dump)
    0x65 shutdown      0x66 dispatch_sound   0x67 play_intro
    0x68 drone on      0x69 drone off        0x6a set drone pitch
    0x6b timer tick (steps sequencer, returns status)
    0x6c noise tick (stub on MT-32)         [0x6d present on f15 only]
"""
import re
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_16, Cs

CH_STRIDE = 0x22
MUSIC_CHANNELS = 6


class RsoundImage:
    def __init__(self, path):
        raw = open(path, 'rb').read()
        # skip MZ header: header_paragraphs @0x08 *16
        hdr_paras = struct.unpack_from('<H', raw, 0x08)[0]
        self.img = raw[hdr_paras * 16:]
        self.path = path
        (self.code_seg, self.base_seg, self.first_slot, self.size1,
         self.size2, self.count) = struct.unpack_from('<6H', self.img, 0x18)
        self.slots = struct.unpack_from(f'<{self.count}H', self.img, 0x24)
        self.desc = self.img[:0x18].split(b'\x00')[0].decode('ascii', 'replace')
        self.code_off = self.code_seg * 16
        self.md = Cs(CS_ARCH_X86, CS_MODE_16)

    @property
    def code(self):
        return self.img[self.code_off:]

    def dis(self, off, n):
        return list(self.md.disasm(self.code[off:off + n], off))

    def u16(self, a):
        return struct.unpack('<H', self.img[a:a + 2])[0]

    def wcode16(self, codeoff):
        return struct.unpack('<H', self.code[codeoff:codeoff + 2])[0]


def find_sysex_records(img):
    """'f0 41 10 16 12 ff' header, then addr3+payload+ff records."""
    i = img.find(bytes([0xf0, 0x41, 0x10, 0x16, 0x12, 0xff]))
    if i < 0:
        return None, []
    p, records = i + 6, []
    while img[p] != 0xff:
        addr = img[p] << 16 | img[p + 1] << 8 | img[p + 2]
        p += 3
        start = p
        while img[p] != 0xff:
            p += 1
        records.append((addr, bytes(img[start:p])))
        p += 1
    return i, records


def find_channel_init(rs):
    """Routine writing the channel-init constants. Its entry has no standard
    prologue, so find the constant-write pattern, then take the call target
    inside the preceding window that callers actually use."""
    pat = (b'\xc6\x47\x0e\xff\xc6\x47\x0f\xff\xc6\x47\x0c\x64'
           b'\xc7\x47\x1e\x00\x20\xc6\x47\x0d\x40')
    i = rs.code.find(pat)
    if i < 0:
        return None
    from collections import Counter
    votes = Counter()
    for ins in rs.dis(0, len(rs.code)):
        if ins.mnemonic != 'call' or not ins.op_str.startswith('0x'):
            continue
        tgt = int(ins.op_str, 16)
        if i - 0x40 <= tgt <= i:
            votes[tgt] += 1
    return votes.most_common(1)[0][0] if votes else i


def harvest_init_sites(rs, init_off):
    """call sites of channel-init -> (code_off, ch_base, stream, cb).
    Works on disassembled instructions so lea-immediates can't desync the
    scan (e.g. `lea cx,[0x12e8]` contains an 0xe8 byte)."""
    sites = []
    insns = rs.dis(0, len(rs.code))
    for i, ins in enumerate(insns):
        if ins.mnemonic != 'call' or ins.op_str != hex(init_off):
            continue
        bx = cx = ax = None
        for prev in insns[max(0, i - 8):i]:
            ops = prev.op_str
            if prev.mnemonic == 'lea' and ops.startswith('bx'):
                bx = int(ops.split('[')[1].rstrip(']'), 0)
            elif prev.mnemonic == 'lea' and ops.startswith('cx'):
                cx = int(ops.split('[')[1].rstrip(']'), 0)
            elif prev.mnemonic == 'lea' and ops.startswith('ax'):
                ax = int(ops.split('[')[1].rstrip(']'), 0)
            elif prev.mnemonic == 'xor' and ops == 'ax, ax':
                ax = 0
        if bx is None or cx is None:
            continue
        sites.append((ins.address, bx, cx, ax if ax is not None else 0))
    return sites


def find_dispatch_table(rs, ch_base, init_off):
    """Pick the `jmp word ptr cs:[bx + T]` table whose handlers decode to
    (channel, stream) pairs — the interpreter's own opcode table also matches
    the opcode pattern, so score candidates by decodable handlers."""
    best = (0, None, [])
    for m in re.finditer(rb'\x2e\xff[\x87\x97\xa7](..)', rs.code):
        tbl = struct.unpack('<H', m.group(1))[0]
        entries = []
        for i in range(40):
            v = rs.wcode16(tbl + i * 2)
            if not (0x200 <= v < len(rs.code)):
                break
            entries.append(v)
        if len(entries) < 4:
            continue
        score = sum(1 for e in entries if decode_handler(rs, e, ch_base, init_off)[0] >= 0)
        if score > best[0]:
            best = (score, tbl, entries)
    return best[1], best[2]


def decode_handler(rs, addr, ch_base, init_off):
    """Dispatch handler -> (channel_idx, [stream_offsets]); (-1,[]) for a
    bare-ret no-op.

    Two forms, both ending in `jmp init_off`:
      lea bx,[ch]; lea cx,[stream]; jmp init        (single stream)
      mov bx,[rng]; ...; mov cx,cs:[bx+T]; lea bx,[ch]; jmp init  (4 variants)
    """
    insns = rs.dis(addr, 0x50)
    chan, streams, vtbl, if_idle = None, [], None, False
    for ins in insns:
        ops = ins.op_str
        if ins.mnemonic == 'lea' and ops.startswith('bx'):
            chan = int(ops.split('[')[1].rstrip(']'), 0)
        elif ins.mnemonic == 'lea' and ops.startswith('cx'):
            streams.append(int(ops.split('[')[1].rstrip(']'), 0))
        elif ins.mnemonic == 'mov' and 'cs:[bx +' in ops:
            vtbl = int(ops.split('+')[1].rstrip(']'), 0)
        elif ins.mnemonic == 'cmp' and 'byte ptr [0x' in ops:
            if_idle = True   # `cmp [ch].wait,0; jne done` = only-if-idle load
        elif ins.mnemonic in ('ret', 'retf'):
            break
        elif ins.mnemonic == 'jmp' and ops == hex(init_off):
            break
    if vtbl is not None:
        streams = [rs.wcode16(vtbl + i * 2) for i in range(4)]
    if chan is None:
        return -1, [], False
    ch = (chan - ch_base) // CH_STRIDE
    return (ch, streams, if_idle)


def find_tick_rng(rs, tick_off):
    """Inside the tick routine (slot 0x6b chain): rng = ror16(rng+add,3).
    Returns (rng_addr, add_addr) — the two data refs before the ror x3."""
    # find `ror ax,1` x3 inside code reachable from the tick; the tick calls
    # a helper at its top that does the ror. Search whole code for the pattern.
    for m in re.finditer(rb'\xd1\xc8\xd1\xc8\xd1\xc8', rs.code):
        # operands just before: mov ax,[add]; add ax,[rng]; ror x3
        back = rs.code[max(0, m.start() - 0x20):m.start()]
        refs = re.findall(rb'\xa1(..)|\x03\x06(..)|\x8b\x06(..)|\xa3(..)', back)
        if len(refs) >= 2:
            rng_w = [x for grp in refs for x in grp if x]
            add_addr = struct.unpack('<H', rng_w[-2])[0]
            rng_addr = struct.unpack('<H', rng_w[-1])[0]
            return rng_addr, add_addr
    return None, None


def streams_blob(rs, stream_offsets):
    """Cut a contiguous blob covering all streams; return (base_off, bytes)."""
    lo, hi = min(stream_offsets), max(stream_offsets) + 0x400
    hi = min(hi, len(rs.img))
    return lo, bytes(rs.img[lo:hi])


def emit_c(rs, game, records, sounds, music_groups, stray, rng, out):
    lo, blob = streams_blob(rs, [s for _, ss, _fl in sounds for s in ss] +
                          [s for grp in music_groups for _, s, _, _ in grp] +
                          [s for _, ss in stray for s in ss])
    w = open(out, 'w')
    g = game.upper()
    w.write(f'/* Auto-generated by tools/rsound_extract.py from {rs.path}\n')
    w.write(f' * {rs.desc} — do not edit. Stream blob base = image 0x{lo:x}. */\n\n')
    # streams blob
    w.write(f'static const unsigned char rsound_{game}_blob[0x{len(blob):x}] = {{\n')
    for i in range(0, len(blob), 16):
        w.write('    ' + ','.join(f'0x{b:02x}' for b in blob[i:i + 16]) + ',\n')
    w.write('};\n\n')
    w.write(f'#define RSOUND_{g}_BLOB_BASE 0x{lo:x}u\n\n')
    # sysex init records
    w.write(f'static const RsoundSysex rsound_{game}_init[] = {{\n')
    for addr, payload in records:
        vals = ','.join(f'0x{b:02x}' for b in payload)
        w.write(f'    {{0x{addr:06x}u, {{{vals}}}, {len(payload)}u}},\n')
    w.write('};\n\n')
    nchan = max(ch for g in music_groups for ch, _, _, _ in g) + 1 if music_groups else 0
    # music programs: clustered channel-init runs. Each program's cb is the
    # code offset of the routine that starts the NEXT program; translate to
    # a program index, -1 = done, -2 = no cb (stream end loops to base).
    site_offs = [min(site for _, _, _, site in grp) for grp in music_groups]
    w.write(f'static const RsoundStreamRef rsound_{game}_music[] = {{\n')
    for gi, grp in enumerate(music_groups):
        for ch, soff, cb, _site in sorted(grp):
            if cb == 0:
                nxt = -2  # no callback: stream end loops to base
            else:
                nxt = -1  # done routine (intro-finished flag)
                for j, so in enumerate(site_offs):
                    if so > cb:
                        nxt = j
                        break
            w.write(f'    /* prog{gi} ch{ch} */ {{0x{soff - lo:x}u, {nxt}}},\n')
    w.write('};\n')
    w.write(f'#define RSOUND_{g}_MUSIC_PROGRAMS {len(music_groups)}u\n')
    w.write(f'#define RSOUND_{g}_MUSIC_CHANNELS {nchan}u\n\n')
    # sound dispatch table
    w.write(f'static const RsoundSound rsound_{game}_sounds[] = {{\n')
    for entry in sounds:
        ch, streams, idle = entry if len(entry) == 3 else (*entry, False)
        refs = ','.join(f'0x{s - lo:x}u' for s in streams)
        w.write(f'    {{{ch}, {1 if idle else 0}u, {len(streams)}u, {{{refs}}}}},\n')
    w.write('};\n\n')
    if stray:
        w.write('/* stray channel-init call sites (covered by the sounds table):\n')
        for ch, streams in stray:
            refs = ','.join(f'0x{s - lo:x}' for s in streams)
            w.write(f' *   ch{ch}: {refs}\n')
        w.write(' */\n\n')
    n_init = len(records)
    n_snd = len(sounds)
    w.write(f'const RsoundData rsound_data_{game} = {{\n')
    w.write(f'    rsound_{game}_blob, RSOUND_{g}_BLOB_BASE,\n')
    w.write(f'    (uint16_t)sizeof(rsound_{game}_blob),\n')
    w.write(f'    rsound_{game}_init, {n_init}u,\n')
    w.write(f'    rsound_{game}_music, RSOUND_{g}_MUSIC_PROGRAMS,\n')
    w.write(f'    RSOUND_{g}_MUSIC_CHANNELS,\n')
    w.write(f'    rsound_{game}_sounds, {n_snd}u,\n')
    w.write('};\n')
    w.close()
    print(f'  wrote {out}')


def extract(game, path, out_inc):
    rs = RsoundImage(path)
    print(f'== {game}: {rs.desc!r} code@{rs.code_off:#x} '
          f'first_slot={rs.first_slot:#x} slots={rs.count}')

    sysex_at, records = find_sysex_records(rs.img)
    print(f'  sysex @{sysex_at:#x}: {len(records)} records, '
          f'{sum(len(p) for _, p in records)} payload bytes')

    init_off = find_channel_init(rs)
    sites = harvest_init_sites(rs, init_off)
    ch_base = min(bx for _, bx, _, _ in sites)
    print(f'  ch-init @{init_off:#x}: {len(sites)} sites, ch_base={ch_base:#x}')

    # group consecutive call-site runs of MUSIC_CHANNELS into music programs;
    # leftover sites are drone/SFX stream loads
    sites_sorted = sorted(sites)
    # a program = one contiguous run over the channel indices, restarting
    # when the index drops back to the base channel
    groups, run, prev_ch = [], [], -1
    for off, bx, cx, ax in sites_sorted:
        ch = (bx - ch_base) // CH_STRIDE
        if run and ch <= prev_ch:
            groups.append(run)
            run = []
        run.append((ch, cx, ax, off))
        prev_ch = ch
    if run:
        groups.append(run)
    music_groups = [g for g in groups if len(g) >= 2]
    stray = [(ch, [cx]) for g in groups if len(g) < 2 for ch, cx, ax, off in g]
    nchan = max(ch for g in music_groups for ch, _, _, _ in g) + 1 if music_groups else 0
    print(f'  music programs: {len(music_groups)} ({nchan} channels); '
          f'stray stream loads: {len(stray)}')

    tbl, entries = find_dispatch_table(rs, ch_base, init_off)
    sounds = [decode_handler(rs, e, ch_base, init_off) for e in entries]
    print(f'  dispatch tbl @{tbl:#x}: {len(sounds)} sounds '
          f'(chs {sorted({e[0] for e in sounds if e[0] >= 0})})')

    rng_addr, rng_add = find_tick_rng(rs, rs.slots[0x6b - rs.first_slot])
    print(f'  rng: addr={rng_addr and hex(rng_addr)} add={rng_add and hex(rng_add)}'
          f' -> const {hex(rs.u16(rng_add)) if rng_add else "?"}')

    emit_c(rs, game, records, sounds, music_groups, stray,
           (rng_addr, rs.u16(rng_add) if rng_add else 0), out_inc)
    return rs


if __name__ == '__main__':
    for game, path in (
        ('f19', '/home/xor/games/f19/F19/RSOUND.EXE'),
        ('f15', '/home/xor/games/f15/RSOUND.EXE'),
    ):
        extract(game, path, f'src/asound/rsound_data_{game}.inc')


# ---------------------------------------------------------------------------
# Replayer: execute the interpreter over extracted tables -> MIDI event list.
# Used to validate the model before the C port (and to dump .mid files).

class Chan:
    __slots__ = ('wait','bend_step','volsw_step','pansw_step','note','vel','gap',
                 'noteoff','volsw_t','bflag','pansw_t','vol','pan','volsw_r',
                 'pansw_r','bend_cnt','bend_del','base','pos','mark','loop',
                 'bend14','cb','held','alive')
    def __init__(s):
        s.wait=1; s.bend_step=0; s.volsw_step=0; s.pansw_step=0; s.note=0
        s.vel=0; s.gap=0; s.noteoff=0; s.volsw_t=0; s.bflag=0; s.pansw_t=0
        s.vol=0x64; s.pan=0x40; s.volsw_r=0xff; s.pansw_r=0xff
        s.bend_cnt=0; s.bend_del=0; s.bend14=0x2000
        s.base=0; s.pos=0; s.mark=[0,0]; s.loop=[0,0]; s.cb=0
        s.held=[0xff]*4; s.alive=True


def replay(blob, music, prog, ticks=20000, verbose=False):
    """music = [(ch,stream_off,cb)] for one program. Yields MIDI events
    (tick, type, ch, a, b)."""
    chans = []
    for ch, off, cb in music:
        c = Chan(); c.base = c.pos = c.mark[0] = c.mark[1] = off; c.cb = cb
        chans.append(c)
    events = []
    last_status = [None]*MUSIC_CHANNELS
    def out(ci, status, a=None, b=None):
        if status != last_status[ci]:
            events.append((t, 'status', ci, status))
            last_status[ci] = status
        if a is not None: events.append((t, 'data', ci, a))
        if b is not None: events.append((t, 'data', ci, b))
    def note_off(c, ci):
        for i,n in enumerate(c.held):
            if n == 0xff: break
            out(ci, 0x90|ci, n, 0)
            c.held[i] = 0xff
    def note_on(c, ci, note):
        out(ci, 0x90|ci, note, c.vel)
        for i in range(4):
            if c.held[i] == 0xff:
                c.held[i] = note; return
    def fx_tick(c, ci):
        if c.volsw_step:
            c.volsw_t -= 1
            if c.volsw_t == 0:
                c.volsw_t = c.volsw_r
                c.vol = (c.vol + c.volsw_step) & 0xff
                out(ci, 0xb0|ci, 7, c.vol)
        if c.bflag:  # bend sweep: delay then len ticks of step adds
            if c.bend_del: c.bend_del -= 1
            else:
                c.bend14 = (c.bend14 + c.bend_step) & 0x3fff
                out(ci, 0xe0|ci, c.bend14 & 0x7f, c.bend14 >> 7)
                c.bend_cnt -= 1
                if c.bend_cnt == 0: c.bflag = 0
        if c.pansw_step:
            c.pansw_t -= 1
            if c.pansw_t == 0:
                c.pansw_t = c.pansw_r
                c.pan = (c.pan + c.pansw_step) & 0xff
                out(ci, 0xb0|ci, 0x0a, c.pan)

    done = False
    for t in range(ticks):
        if done: break
        alive_any = False
        for ci, c in enumerate(chans):
            if not c.alive: continue
            alive_any = True
            if c.noteoff:
                c.noteoff -= 1
                if c.noteoff == 0: note_off(c, ci)
            if c.wait > 0:
                c.wait -= 1
                if c.wait > 0:
                    fx_tick(c, ci); continue
            # fetch loop
            while True:
                op = blob[c.pos]; c.pos += 1
                if op < 0xf2:
                    note = op; dur = blob[c.pos]; c.pos += 1
                    c.note = note; c.wait = dur
                    if note == 0 or dur == 0:
                        note_off(c, ci)
                    else:
                        c.noteoff = max(1, dur - c.gap)
                        note_on(c, ci, note)
                    break
                idx = op - 0xf2
                if idx == 0:  # f2 CC1 modulation
                    v = blob[c.pos]; c.pos += 1
                    out(ci, 0xb0|ci, 1, v & 0x7f)
                elif idx == 1:  # f3 r,s pan sweep (s==0 -> pan=0x40)
                    r = blob[c.pos]; s = blob[c.pos+1]; c.pos += 2
                    c.pansw_r = r; c.pansw_step = s if s < 0x80 else s - 0x100
                    if s == 0: c.pan = 0x40; out(ci, 0xb0|ci, 0x0a, 0x40)
                    c.pansw_t = 1
                elif idx == 2:  # f4 v -> CC10 pan
                    v = blob[c.pos]; c.pos += 1
                    c.pan = v; out(ci, 0xb0|ci, 0x0a, v & 0x7f)
                elif idx == 3:  # f5 n: n held notes -> note-on each
                    n = blob[c.pos]
                    for j in range(n):
                        nt = blob[c.pos + 1 + j]
                        c.held[j] = nt
                        note_on(c, ci, nt)
                    c.pos += 1 + n
                elif idx == 4:  # f6 v -> CC7 volume
                    v = blob[c.pos]; c.pos += 1
                    c.vol = v; out(ci, 0xb0|ci, 7, v)
                elif idx == 5:  # f7 b -> bend b<<8
                    b = blob[c.pos]; c.pos += 1
                    c.bend14 = (b << 8) & 0x3fff
                    out(ci, 0xe0|ci, c.bend14 & 0x7f, c.bend14 >> 7)
                elif idx == 6:  # f8 r,s volume sweep
                    r = blob[c.pos]; s = blob[c.pos+1]; c.pos += 2
                    c.volsw_r = r; c.volsw_step = s if s < 0x80 else s - 0x100
                    c.volsw_t = 1
                elif idx == 7:  # f9 v velocity
                    c.vel = blob[c.pos]; c.pos += 1
                elif idx == 8:  # fa d,s,l pitch sweep
                    d = blob[c.pos]; s = blob[c.pos+1]; l = blob[c.pos+2]
                    c.pos += 3
                    c.bend14 = 0x2000
                    c.bend_del = d
                    c.bend_step = (s if s < 0x80 else s - 0x100) << 8
                    c.bend_cnt = l; c.bflag = 1
                elif idx == 9:  # fb g keyoff gap
                    c.gap = blob[c.pos]; c.pos += 1
                elif idx == 10:  # fc p program change
                    v = blob[c.pos]; c.pos += 1
                    if (0xc0|ci) != last_status[ci]:
                        out(ci, 0xc0|ci)
                    out(ci, 0xc0|ci, v)
                elif idx == 11:  # fd end: cb else loop to base
                    if c.cb:
                        done = True   # cb = next-program start (modelled)
                        c.alive = False
                    else:
                        c.pos = c.base
                    break
                elif idx == 12 or idx == 13:  # fe/ff nested loop k
                    k = idx - 12
                    if c.loop[k] == 0:
                        cnt = blob[c.pos]
                        if cnt == 0:      # end marker -> skip past
                            c.pos += 1
                            break
                        c.loop[k] = cnt
                        c.mark[k] = c.pos + 1
                        c.pos += 1
                    else:
                        c.loop[k] -= 1
                        if c.loop[k] == 0:
                            c.pos += 1
                        else:
                            c.pos = c.mark[k]
                    continue
                if not alive_any: break
            fx_tick(c, ci)
        if not alive_any: break
    return events


def dump_events(game, prog=0, ticks=12000):
    """Convenience: replay extracted f19/f15 program and print event stats."""
    import importlib.util
    rs = RsoundImage({'f19': '/home/xor/games/f19/F19/RSOUND.EXE',
                      'f15': '/home/xor/games/f15/RSOUND.EXE'}[game])
    init_off = find_channel_init(rs)
    sites = sorted(harvest_init_sites(rs, init_off))
    ch_base = min(bx for _, bx, _, _ in sites)
    lo = min(cx for _, _, cx, _ in sites)
    # group into programs of 6
    music = [tuple(((bx - ch_base)//CH_STRIDE, cx, ax)
                   for _, bx, cx, ax in sites[i*6:(i+1)*6])
             for i in range(len(sites)//6)]
    blob = bytes(rs.img[lo:])
    ev = replay(blob, [(c, o - lo, cb) for c, o, cb in music[prog]], ticks)
    stat = [e for e in ev if e[1] == 'status']
    print(f'{game} prog{prog}: {len(ev)} events, last tick {ev[-1][0] if ev else 0}')
    for e in ev[:80]:
        print('   ', e)
