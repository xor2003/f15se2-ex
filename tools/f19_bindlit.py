#!/usr/bin/env python3
"""f19_bindlit — bind literal f19_dsegAt(0xNNNN)/EG_W(0xNNNN) callsites to
named f19_stSpace/f19_egSpace members.

The packed structs reproduce the DOS dseg byte-for-byte, so a literal
resolver call f19_dsegAt(off) in a world-fixed file is just
&space.m_name + delta.  Shared runtime files (f19stubs/f19file/f19ovl/
f19crt/f19seg) resolve dynamically and are not touched.

Emission preserves byte semantics exactly:
  *(T *)f19_dsegAt(O)  -> f19_Sp.m            scalar member of type T at O
                      -> f19_Sp.m[i]          array elem T at O
                      -> *(T *)(m + i)        anything else
  (T *)f19_dsegAt(O)   -> &f19_Sp.m           scalar at O
                      -> f19_Sp.m             array base at O
                      -> f19_Sp.m + i         aligned array index
                      -> (uint8 *)&m + d      unaligned/interior
  EG_W(O)              -> uint16 deref equivalent (eg side)
  f19_farAt(O)/setFar  -> f19_farAt(f19_dsegOff(<addr>)) — celloff is an
                          offset, so name it via f19_dsegOff.
Gap members (f19_stgap_X) are still members — literals inside them bind
like anything else, honestly naming the unnamed region.
"""
import re, sys, os, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = ROOT + '/src/f19'

SIDE_ST = ['f19data.c']   # START-module init; runs under world 0
SIDE_EG = ['f19egdata.c', 'f19egsvc.c']   # EGAME module; world 1
SHARED = set('f19stubs.c f19file.c f19ovl.c f19crt.c f19seg.c '
             'f19stvars.c f19egvars.c f19segdat.c f19egsegdat.c'.split())

CSIZE = {'uint8': 1, 'int8': 1, 'char': 1, 'uint16': 2, 'int16': 2,
         'uint32': 4, 'int32': 4, 'int': 2, 'short': 2, 'long': 4}

MRE = re.compile(
    r'\s*(\w+)\s+(m_\w+|f19_(?:st|eg)gap_\w+)'
    r'(\s*\[[^\]]*\](?:\s*\[[^\]]*\])?)?\s*;'
    r'.*?/\*\s*(0x[0-9A-Fa-f]+)\s*\*/')


def members(fn):
    out = []
    for line in open(fn):
        m = MRE.match(line)
        if not m or m.group(1) not in CSIZE:
            continue
        ct, nm, arr, off = m.groups()
        off = int(off, 16)
        dims = [int(x) for x in re.findall(r'\[(\d+)\]', arr or '')]
        cnt = 1
        for d in dims:
            cnt *= d
        esz = CSIZE[ct]
        if len(dims) > 1:                      # T[R][C] — elem is T[C]
            esz *= dims[-1]
        out.append(dict(off=off, size=CSIZE[ct] * cnt, name=nm,
                        ctype=ct, esz=esz, array=bool(arr)))
    out.sort(key=lambda m: m['off'])
    return out


def side_of(fn):
    b = os.path.basename(fn)
    if b in SHARED or 'vars' in b or 'segdat' in b:
        return None
    if b in SIDE_EG or b.startswith('eg') or b.startswith('f19eg'):
        return 'eg'
    return 'st'


def find_mem(ms, off):
    for m in ms:
        if m['off'] <= off < m['off'] + m['size']:
            return m
        if m['off'] > off:
            break
    return None


def byte_ptr(m, sp):
    """uint8* to member base — keeps byte arith for any trailing '+ k'."""
    return '(uint8 *)&%s.%s' % (sp, m['name'])


def ptr_expr(m, off, sp):
    """Replacement for f19_dsegAt(off) — a pointer to the cell.

    Scalars emit &m; arrays emit m (decays to elem ptr).  Interior
    offsets emit elem-indexed m + k when element-aligned, else the
    byte-pointer form which is arithmetically identical either way."""
    d = off - m['off']
    if d == 0:
        return '%s.%s' % (sp, m['name']) if m['array'] else '&' + \
            '%s.%s' % (sp, m['name'])
    if m['array'] and d % m['esz'] == 0:
        return '(%s.%s + 0x%X)' % (sp, m['name'], d // m['esz'])
    return '(%s + 0x%X)' % (byte_ptr(m, sp), d)


def deref_expr(m, off, T, sp):
    """Replacement for *(T *)f19_dsegAt(off)."""
    d = off - m['off']
    if d == 0 and not m['array'] and m['ctype'] == T:
        return '%s.%s' % (sp, m['name'])
    if m['array'] and m['ctype'] == T and m['esz'] == CSIZE[T] \
            and d % m['esz'] == 0:
        return '%s.%s[0x%X]' % (sp, m['name'], d // m['esz'])
    if d:
        return '*(%s *)(%s + 0x%X)' % (T, byte_ptr(m, sp), d)
    return '*(%s *)%s' % (T, byte_ptr(m, sp))


DEREF = re.compile(
    r'\*\s*\(\s*(u?int(?:8|16|32)|char|short|long|int)\s*\*\s*\)\s*'
    r'f19_dsegAt\s*\(\s*(0x[0-9A-Fa-f]+)\s*(\+\s*[^)]+)?\)')
EGW = re.compile(r'EG_W\(\s*(0x[0-9A-Fa-f]+)\s*\)')
LIT = re.compile(r'f19_dsegAt\s*\(\s*(0x[0-9A-Fa-f]+)\s*(\+\s*[^)]+)?\)')
FARC = re.compile(r'\b(f19_farAt|f19_setFar)\s*\(\s*(0x[0-9A-Fa-f]+)\s*')


def convert(txt, ms, sp):
    stats = {'deref': 0, 'egw': 0, 'lit': 0, 'far': 0, 'miss': []}

    def dsub(m):
        T, off, tail = m.group(1), int(m.group(2), 16), m.group(3)
        mem = find_mem(ms, off)
        if not mem:
            stats['miss'].append(hex(off)); return m.group(0)
        stats['deref'] += 1
        if tail:
            d = off - mem['off']
            add = (hex(d) + ' + ' if d else '') + tail[1:].strip()
            return '*(%s *)(%s + %s)' % (T, byte_ptr(mem, sp), add)
        return deref_expr(mem, off, T, sp)

    def esub(m):
        off = int(m.group(1), 16)
        mem = find_mem(ms, off)
        if not mem:
            stats['miss'].append(hex(off)); return m.group(0)
        stats['egw'] += 1
        return deref_expr(mem, off, 'uint16', sp)

    def lsub(m):
        off, tail = int(m.group(1), 16), m.group(2)
        mem = find_mem(ms, off)
        if not mem:
            stats['miss'].append(hex(off)); return m.group(0)
        stats['lit'] += 1
        if tail:                        # f19_dsegAt(base + expr)
            d = off - mem['off']
            add = (hex(d) + ' + ' if d else '') + tail[1:].strip()
            return '(%s + %s)' % (byte_ptr(mem, sp), add)
        return ptr_expr(mem, off, sp)

    def fsub(m):
        fn, off = m.group(1), int(m.group(2), 16)
        mem = find_mem(ms, off)
        if not mem:
            stats['miss'].append(hex(off)); return m.group(0)
        stats['far'] += 1
        return '%s(f19_dsegOff(%s)' % (fn, ptr_expr(mem, off, sp))

    txt = DEREF.sub(dsub, txt)
    txt = EGW.sub(esub, txt)
    txt = LIT.sub(lsub, txt)
    txt = FARC.sub(fsub, txt)
    return txt, stats


def main():
    apply = '-w' in sys.argv
    tabs = {'st': members(SRC + '/f19stvars.h'),
            'eg': members(SRC + '/f19egvars.h')}
    sp = {'st': 'f19_stSpace', 'eg': 'f19_egSpace'}
    for fn in sorted(glob.glob(SRC + '/*.c')):
        side = side_of(fn)
        if not side:
            continue
        txt = open(fn).read()
        out, st = convert(txt, tabs[side], sp[side])
        n = st['deref'] + st['egw'] + st['lit'] + st['far']
        if n == 0 and not st['miss']:
            continue
        tag = 'ok' if not st['miss'] else 'MISS ' + ','.join(st['miss'])
        print('%-16s %-2s deref=%d egw=%d lit=%d far=%d %s'
              % (os.path.basename(fn), side, st['deref'], st['egw'],
                 st['lit'], st['far'], tag))
        if apply and out != txt:
            open(fn, 'w').write(out)


if __name__ == '__main__':
    main()
