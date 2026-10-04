#!/usr/bin/env python3
"""f19_deblob — convert f19_dseg offset macros into real named globals.

  harvest   read '#define NAME <f19_dseg expr>' under src/f19/
  partition per side (st/eg): containers (ptrcast/arrnd) -> arrays, scalars
            -> vars, interior/dup names -> '#define <access expr>' aliases
  emit      f19stvars.{h,c} / f19egvars.{h,c}: externs, defs with image
            initializers, reset fns, offset->object tables
  rewrite   drop converted defines + dead extern decls, inject the vars
            header, rewrite body 'f19_dseg + E'/'f19_dseg[E]' -> f19_dsegAt()

DOS offsets stay resolvable at runtime via the F19SegObj tables - 16-bit
offset tables and {off,seg} far cells are part of the original data model.
"""
import re, sys, glob, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src/f19')

def load_img(path, name):
    t = open(path).read()
    m = re.search(name + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', t, re.S)
    return [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1))]

IMG = {'st': load_img(SRC + '/f19segdat.c', 'f19_dseg_img'),
       'eg': load_img(SRC + '/f19egsegdat.c', 'f19_egDsegImage')}
IMGNAME = {'st': 'f19_dseg_img', 'eg': 'f19_egDsegImage'}

CSIZE = {'int8': 1, 'uint8': 1, 'char': 1, 'int16': 2, 'uint16': 2,
         'int32': 4, 'uint32': 4}
TSIZE = dict(CSIZE)

def side_of(fn):
    return 'eg' if os.path.basename(fn).startswith(('eg', 'f19eg')) else 'st'

P_SCALAR = re.compile(r'^\(\*\(([\w ]+?)\s*\*\)\s*\(f19_dseg \+ (0x[0-9A-Fa-f]+)\)')
P_PTRCAST = re.compile(r'^\(\((const )?([\w ]+?)\s*\*\)\s*\(f19_dseg \+ (0x[0-9A-Fa-f]+)\)')
P_ARRND = re.compile(r'^\(\((const )?([\w ]+?)\s*\(\*\)((?:\[[0-9a-fA-Fx]+\])+)\)\s*\(f19_dseg \+ (0x[0-9A-Fa-f]+)\)')
P_INDIRECT = re.compile(r'^\(\(([\w ]+?)\s*\*\)\s*\(f19_dseg \+ \*\(u?int16 \*\)\s*\(f19_dseg \+ (0x[0-9A-Fa-f]+)\)\)')
P_BASE = re.compile(r'^\(\(([\w ]+?)\s*\*\)\s*f19_dseg\)')
P_ADDR = re.compile(r'^\(?\s*f19_dseg \+ (0x[0-9A-Fa-f]+)\s*\)?$')
P_ARRCELL = re.compile(r'^\(\(([\w ]+?)\s*\*\*\s*\)\s*\(f19_dseg \+ (0x[0-9A-Fa-f]+)\)\)')

def parse_define(expr):
    m = P_ARRND.match(expr)
    if m:
        dims = [int(d, 0) for d in re.findall(r'\[([^\]]+)\]', m.group(3))]
        return ('arrnd', (m.group(1) or '') + m.group(2), int(m.group(4), 0), dims)
    m = P_INDIRECT.match(expr)
    if m:
        return ('indirect', m.group(1), int(m.group(2), 0), None)
    m = P_ARRCELL.match(expr)
    if m:
        return ('arrcell', m.group(1), int(m.group(2), 0), None)
    m = P_SCALAR.match(expr)
    if m:
        t = m.group(1).strip()
        if t.endswith('*'):
            return ('ptrscalar', t[:-1].strip(), int(m.group(2), 0), None)
        return ('scalar', t, int(m.group(2), 0), None)
    m = P_PTRCAST.match(expr)
    if m:
        return ('ptrcast', (m.group(1) or '') + m.group(2), int(m.group(3), 0), None)
    m = P_BASE.match(expr)
    if m:
        return ('base', m.group(1), 0, None)
    m = P_ADDR.match(expr)
    if m:
        return ('addr', 'uint8', int(m.group(1), 0), None)
    return None

def harvest():
    syms = []
    for fn in sorted(glob.glob(SRC + '/*.h') + glob.glob(SRC + '/*.c')):
        if 'vars' in os.path.basename(fn):
            continue
        for i, line in enumerate(open(fn), 1):
            m = re.match(r'#define\s+(\w+)\s+(.*)', line.rstrip())
            if not m or not re.search(r'\bf19_dseg\b', m.group(2)):
                continue
            expr = re.sub(r'/\*.*?\*/', '', m.group(2)).strip()
            p = parse_define(expr)
            syms.append(dict(file=fn, line=i, name=m.group(1),
                             kind=p[0] if p else 'UNHANDLED',
                             ctype=p[1] if p else '', off=p[2] if p else 0,
                             extra=p[3] if p else None, expr=expr,
                             side=side_of(fn)))
    return syms

# ------------------------------------------------- pack(1) struct sizing
def body_size(body, structs, depth=0):
    if depth > 8:
        return 0
    size = 0
    for stmt in body.split(';'):
        stmt = re.sub(r'/\*.*?\*/', '', stmt).strip()
        if not stmt:
            continue
        um = re.match(r'union\s*\{(.*)\}\s*\w+(?:\[(\w+)\])?$', stmt, re.S)
        if um:
            memb = max((body_size(p, structs, depth + 1)
                        for p in um.group(1).split(';')), default=0)
            size += memb * (int(um.group(2), 0) if um.group(2) else 1)
            continue
        sm = re.match(r'struct\s+(\w+)\s+(.+)$', stmt, re.S)
        if sm:
            t, names = 'struct ' + sm.group(1), sm.group(2)
        else:
            tm = re.match(r'((?:const )?\w+)\s+(.+)$', stmt, re.S)
            if not tm:
                continue
            t, names = tm.group(1), tm.group(2)
        if t.startswith('struct '):
            bs = structs.get(t[7:])
            esz = max((body_size(b, structs, depth + 1)
                       for b in bs), default=4) if bs else 4
        else:
            esz = TSIZE.get(t, 0)
        for nm in names.split(','):
            nm = nm.strip()
            if not nm:
                continue
            if nm.startswith('*') or '(*' in nm:
                size += 4
                continue
            am = re.search(r'\[(0x[0-9a-fA-F]+|\d+)\]', nm)
            size += esz * (int(am.group(1), 0) if am else 1)
    return size

def size_structs(syms):
    cand = {}
    for fn in glob.glob(SRC + '/*.[ch]') + glob.glob(ROOT + '/src/*.h'):
        t = open(fn).read()
        for m in re.finditer(r'struct\s+(\w+)\s*\{(.*?)\};', t, re.S):
            cand.setdefault(m.group(1), []).append(m.group(2))
    want = set()
    for s in syms:
        want.update(re.findall(r'struct (\w+)', s['ctype']))
    missing = []
    for tag in want:
        best = max((body_size(b, cand) for b in cand.get(tag, [])),
                   default=-1)
        if best < 0:
            missing.append(tag)
        else:
            TSIZE['struct ' + tag] = best
    return missing

def esz(ctype):
    return TSIZE.get(ctype.replace('const ', '').strip())

def imgw(side, off, n=2):
    im = IMG[side]
    return sum((im[off + i] if off + i < len(im) else 0) << (8 * i)
               for i in range(n))

# ------------------------------------------------------------- partitioning
def prod(xs):
    r = 1
    for x in xs:
        r *= x
    return r

def build_side(syms, side):
    syms = [s for s in syms if s['side'] == side and s['kind'] != 'UNHANDLED']
    cont = sorted([s for s in syms if s['kind'] in ('ptrcast', 'arrnd',
                                                  'arrcell')],
                  key=lambda s: s['off'])
    coffs = sorted(set(s['off'] for s in cont))
    objs = []
    for s in cont:
        if objs and objs[-1]['off'] == s['off']:
            objs[-1]['defs'].append(s)
            continue
        nxt = next((c for c in coffs if c > s['off']),
                   len(IMG[side]) if s['off'] < len(IMG[side])
                   else s['off'] + 0x200)
        gap = max(1, nxt - s['off'])
        e = esz(s['ctype']) or 1
        if s['kind'] == 'arrnd':
            cnt = max(1, gap // (e * prod(s['extra'])))
            size = cnt * e * prod(s['extra'])
        elif s['kind'] == 'arrcell':
            cnt, size = max(1, gap // 4), max(1, gap // 4) * 4
        else:
            cnt, size = max(1, gap // e), max(1, gap // e) * e
        objs.append(dict(off=s['off'], kind=s['kind'], ctype=s['ctype'],
                         name=s['name'], defs=[s], size=size, count=cnt,
                         extra=s['extra'], gap=False))
    aliases = []
    scal = sorted([s for s in syms if s['kind'] not in
                   ('ptrcast', 'arrnd', 'arrcell')], key=lambda s: s['off'])
    for s in scal:
        host = next((o for o in objs
                     if o['off'] <= s['off'] < o['off'] + o['size']), None)
        if host or s['kind'] in ('base', 'addr', 'indirect'):
            aliases.append((s, host))
            continue
        sz = esz(s['ctype']) if s['kind'] == 'scalar' else 4
        objs.append(dict(off=s['off'], kind=s['kind'], ctype=s['ctype'],
                         name=s['name'], defs=[s], size=sz or 4,
                         count=0, extra=None, gap=False))
    objs.sort(key=lambda o: o['off'])
    out = []
    for o in objs:
        host = next((h for h in out if h['off'] <= o['off'] <
                     h['off'] + h['size']), None)
        if host:
            for d in o['defs']:
                aliases.append((d, host))
        else:
            out.append(o)
    return out, aliases

def alias_expr(s, host):
    if host is None:
        if s['kind'] == 'indirect':
            return '((%s *)f19_dsegAt(*(uint16 *)f19_dsegAt(0x%X)))' \
                   % (s['ctype'], s['off'])
        if s['kind'] == 'addr':
            return '((uint8 *)f19_dsegAt(0x%X))' % s['off']
        return '((%s *)f19_dsegAt(0))' % s['ctype']
    delta = s['off'] - host['off']
    base, t = host['name'], s['ctype']
    if s['kind'] == 'scalar':
        if delta == 0 and host['kind'] == 'scalar' and \
                esz(t) == esz(host['ctype']):
            return '(*(%s *)&%s)' % (t, base)
        he = esz(host['ctype'])
        if host['kind'] in ('ptrcast', 'gap') and he and \
                delta % he == 0 and esz(t) == he:
            return '%s[%d]' % (base, delta // he)
        return '(*(%s *)((uint8 *)%s + %d))' % (t, base, delta)
    if s['kind'] in ('ptrcast', 'arrcell'):
        if delta == 0 and host['kind'] == 'ptrcast':
            return '((%s *)%s)' % (t, base)
        return '((%s *)((uint8 *)%s + %d))' % (t, base, delta)
    if s['kind'] == 'arrnd':
        dims = ''.join('[%d]' % d for d in s['extra'])
        return '((%s (*)%s)((uint8 *)%s + %d))' % (t, dims, base, delta)
    if s['kind'] == 'indirect':
        return '((%s *)f19_dsegAt(*(uint16 *)f19_dsegAt(0x%X)))' \
               % (t, s['off'])
    if s['kind'] == 'addr':
        return '((uint8 *)%s + %d)' % (base, delta)
    if s['kind'] == 'ptrscalar':
        return '(*(%s **)((uint8 *)%s + %d))' % (t, base, delta)
    return None

# ------------------------------------------------------------------- emit
def litval(side, off, sz, ctype):
    v = imgw(side, off, sz)
    if ctype in ('int8', 'int16', 'int32'):
        sign = {1: 0x80, 2: 0x8000, 4: 0x80000000}[sz]
        if v & sign:
            v -= 1 << (8 * sz)
        return str(v)
    return '0x%0*X' % (2 * sz, v)

def arr_init(side, off, ctype, total):
    e = esz(ctype)
    t = ctype.replace('const ', '')
    return '{' + ','.join(litval(side, off + i * e, e, t)
                          for i in range(total)) + '}'

def cdecl(o):
    t, n, k = o['ctype'], o['name'], o['kind']
    if k == 'gap':
        return 'uint8 %s[%d]' % (n, o['size'])
    if k == 'ptrcast':
        return '%s%s[%d]' % (t + ' ', n, o['count'])
    if k == 'arrnd':
        return '%s%s[%d]%s' % (t + ' ', n, o['count'],
                               ''.join('[%d]' % d for d in o['extra']))
    if k == 'arrcell':
        return 'uint32 %s[%d]' % (n, o['count'])
    if k in ('ptrscalar', 'indirect'):
        return '%s *%s' % (t, n)
    return '%s %s' % (t, n)

def emit_side(side, objs, aliases, gaps):
    tag = 'st' if side == 'st' else 'eg'
    hname = 'f19%svars' % tag
    im = IMG[side]
    H = ['/* generated by tools/f19_deblob.py - real globals replacing the',
         '   f19_dseg offset macros (%s.EXE data segment). */'
         % ('START' if side == 'st' else 'EGAME'),
         '#ifndef F19%sVARS_H' % tag.upper(),
         '#define F19%sVARS_H' % tag.upper(),
         '#include "inttype.h"', '#include "f19seg.h"', '']
    C = ['/* generated by tools/f19_deblob.py - see %s.h */' % hname,
         '#include "%s.h"' % hname, '#include <string.h>',
         'extern const uint8 %s[];' % IMGNAME[side], '']
    R = ['void f19_%sVarsReset(void) {' % tag]
    T = []
    for o in objs + gaps:
        nm, t, off, k = o['name'], o['ctype'], o['off'], o['kind']
        init = off < len(im)
        d = cdecl(o)
        if o['gap']:
            C.append('static %s;   /* dseg 0x%X..0x%X */'
                     % (d, off, off + o['size'] - 1))
            if init:
                R.append('    memcpy(%s, %s + 0x%X, %d);'
                         % (nm, IMGNAME[side], off, o['size']))
            else:
                R.append('    memset(%s, 0, %d);' % (nm, o['size']))
        elif k in ('ptrcast', 'arrnd'):
            if t.startswith('struct '):
                H.append('struct %s;' % t.split()[-1])
            H.append('extern ' + d + ';')
            if esz(t):
                C.append('%s = %s;' % (d, arr_init(side, off, t,
                                                 o['count'] *
                                                 prod(o['extra'] or [1]))))
            else:
                C.append('%s;' % d)
            if init:
                R.append('    memcpy(%s, %s + 0x%X, sizeof %s);'
                         % (nm, IMGNAME[side], off, nm))
            else:
                R.append('    memset(%s, 0, sizeof %s);' % (nm, nm))
        elif k == 'arrcell':
            H.append('extern ' + d + ';')
            C.append('%s;' % d)
            R.append('    memset(%s, 0, sizeof %s);   /* far cells */'
                     % (nm, nm))
        elif k == 'ptrscalar':
            H.append('extern ' + d + ';')
            C.append('%s;   /* far cell {off=0x%X,seg=0x%X} */'
                     % (d, imgw(side, off), imgw(side, off + 2)))
            R.append('    %s = (%s *)f19_segResolve(0x%X, 0x%X);'
                     % (nm, t, imgw(side, off), imgw(side, off + 2)))
        elif k == 'indirect':
            H.append('extern ' + d + ';')
            C.append('%s;' % d)
            R.append('    %s = (%s *)f19_dsegAt(0x%X);'
                     % (nm, t, imgw(side, off)))
        elif k == 'scalar' and t.startswith('struct '):
            H.append('struct %s;' % t.split()[-1])
            H.append('extern ' + d + ';')
            C.append('%s;' % d)
            R.append(('    memcpy(&%s, %s + 0x%X, sizeof %s);'
                      if init else '    memset(&%s, 0, sizeof %s);')
                     % ((nm, IMGNAME[side], off, nm) if init
                        else (nm, nm)))
        elif k == 'scalar':
            C.append('%s = %s;' % (d, litval(side, off, o['size'], t)))
            R.append('    %s = %s;' % (nm, litval(side, off, o['size'], t)))
            H.append('extern ' + d + ';')
        T.append('    { (void *)%s%s, 0x%X, 0x%X },'
                 % ('' if k in ('ptrcast', 'arrnd', 'arrcell', 'gap')
                    else '&', nm, o['size'], off))
    for s, host in aliases:
        if s.get('file_local'):
            continue
        e = alias_expr(s, host)
        H.append('#define %s %s' % (s['name'], e) if e else
                 '/* FIXME %s = %s */' % (s['name'], s['expr']))
    H += ['', 'extern const struct F19SegObj f19_%sObjs[];' % tag,
          'extern const int f19_%sObjCount;' % tag,
          'void f19_%sVarsReset(void);' % tag, '#endif', '']
    T = ['const struct F19SegObj f19_%sObjs[] = {' % tag] + T + ['};']
    C += [''] + T + ['',
        'const int f19_%sObjCount = sizeof f19_%sObjs / sizeof f19_%sObjs[0];'
        % (tag, tag, tag), ''] + R + ['}', '']
    open(SRC + '/%s.h' % hname, 'w').write('\n'.join(H))
    open(SRC + '/%s.c' % hname, 'w').write('\n'.join(C))
    print('%s: %d objects, %d gaps, %d aliases' %
          (side, len(objs), len(gaps), len(aliases)))

def make_gaps(objs, side):
    gaps, cur = [], 0
    for o in sorted(objs, key=lambda o: o['off']):
        if o['off'] > cur:
            gaps.append(dict(off=cur, kind='gap', ctype='uint8',
                             name='f19_%sgap_%X' % (side, cur),
                             size=o['off'] - cur, count=0, extra=None,
                             gap=True))
        cur = max(cur, o['off'] + o['size'])
    if cur < 0x10000:
        gaps.append(dict(off=cur, kind='gap', ctype='uint8',
                         name='f19_%sgap_%X' % (side, cur),
                         size=0x10000 - cur, count=0, extra=None,
                         gap=True))
    return gaps

# ---------------------------------------------------------------- rewrite
STOP = set(';,)]}?:')           # expr terminators at depth 0
BINOP_LO = set('+-<>=!&|^')     # ops of precedence <= additive

def capture_expr(t, i):
    """capture a multiplicative expr starting at i -> (text, next_i)"""
    d = 0
    j = i
    while j < len(t) and t[j] in ' \t\n':
        j += 1
    if j < len(t) and t[j] in '+-':
        j += 1                        # leading unary
    while j < len(t):
        c = t[j]
        if c in '([':
            d += 1
        elif c in ')]':
            if d == 0:
                break
            d -= 1
        elif d == 0:
            if c in STOP:
                break
            if c in BINOP_LO:
                if c == '-' and t[j:j+2] == '->':
                    j += 2; continue
                break
        j += 1
    return t[i:j], j

def xlate_dseg(line):
    """rewrite f19_dseg occurrences in a non-define line."""
    out, i, hits = [], 0, 0
    while True:
        m = re.search(r'\bf19_dseg\b', line[i:])
        if not m:
            break
        p = i + m.start()
        out.append(line[i:p])
        j = p + len('f19_dseg')
        rest = line[j:]
        rm = re.match(r'\s*\+', rest)
        if rest.lstrip().startswith('['):
            k = j + len(rest) - len(rest.lstrip()) + 1
            e, j2 = capture_expr(line, k)
            # find matching ]
            d = 0
            while j2 < len(line):
                if line[j2] == '[':
                    d += 1
                elif line[j2] == ']':
                    d -= 1
                    if d == 0:
                        j2 += 1
                        break
                j2 += 1
            out.append('(*(uint8 *)f19_dsegAt(%s))' % line[k:j2 - 1])
            i = j2
        elif rm:
            k = j + rm.end()
            e, j2 = capture_expr(line, k)
            out.append('((uint8 *)f19_dsegAt(%s))' % e.strip())
            i = j2
        else:
            out.append('((uint8 *)f19_dsegAt(0)) /*BARE*/')
            hits += 1
            i = j
    out.append(line[i:])
    return ''.join(out), hits

SKIP_BODY = ('f19data.c', 'f19egdata.c', 'f19seg.c')   # hand-edited

def rewrite_files(syms, aliases, objs):
    amap = {id(s): h for s, h in aliases}
    prim = {id(d): o for o in objs for d in o['defs']}
    byname = {s['name'] for s in syms}
    touched = set(s['file'] for s in syms)
    # any other file still touching the blob in its body
    for fn in glob.glob(SRC + '/*.[ch]'):
        if 'vars' in os.path.basename(fn):
            continue
        if re.search(r'\bf19_dseg\b', open(fn).read()):
            touched.add(fn)
    touched -= {os.path.join(SRC, b) for b in SKIP_BODY}
    report = []
    for fn in sorted(touched):
        lines = open(fn).read().split('\n')
        nl, need_inc = [], re.search(r'\bf19_dseg\b', '\n'.join(lines)) != None
        for i, line in enumerate(lines):
            m = re.match(r'#define\s+(\w+)\s+(.*)', line)
            if m and re.search(r'\bf19_dseg\b', m.group(2)):
                s = next((x for x in syms if x['name'] == m.group(1)
                          and x['line'] == i + 1 and x['file'] == fn), None)
                if s and s.get('file_local'):
                    nl.append('#define %s %s' % (
                        s['name'],
                        alias_expr(s, amap.get(id(s), prim.get(id(s))))))
                # else drop the define entirely
                continue
            # drop 'extern <T> <name>([..]);' for converted names
            em = re.match(r'\s*extern\s+[\w \*]+?\b(\w+)\s*(\[[^\]]*\])?\s*;',
                          line)
            if em and em.group(1) in byname:
                continue
            nl.append(xlate_dseg(line)[0])
        if need_inc:
            inc = '#include "f19%svars.h"' % side_of(fn)
            for k, l in enumerate(nl):
                if l.startswith('#include'):
                    nl.insert(k + 1, inc)
                    break
            else:
                nl.insert(0, inc)
        open(fn, 'w').write('\n'.join(nl))
        report.append(fn)
    return report

def extern_globals():
    """names defined (real objects) outside src/f19 - the app/F-15 globals."""
    names = set()
    for fn in glob.glob(ROOT + '/src/*.c') + glob.glob(ROOT + '/src/*.h'):
        for l in open(fn):
            if l.lstrip().startswith(('extern', '#', 'typedef')):
                continue
            m = re.match(r'[\w \*]+\b(\w+)\s*(\[|=|;)', l)
            if m:
                names.add(m.group(1))
    return names

def emit():
    syms = harvest()
    miss = size_structs(syms)
    if miss:
        print('missing struct defs:', miss)
    unh = [s for s in syms if s['kind'] == 'UNHANDLED']
    print('UNHANDLED:', len(unh))
    for s in unh[:40]:
        print('  %s:%d %s = %s' % (os.path.basename(s['file']), s['line'],
                                  s['name'], s['expr'][:90]))
    # names that must stay macros at their define site (not global decls):
    #  - multi-signature (different exprs in different files)
    #  - colliding with an app/F-15 global symbol
    appg = extern_globals()
    sigs = {}
    for s in syms:
        sigs.setdefault(s['name'], set()).add(
            (s['kind'], s['off'], s['ctype'], str(s['extra'])))
    local = {n for n, v in sigs.items() if len(v) > 1} | \
            ({s['name'] for s in syms} & appg)
    for s in syms:
        s['file_local'] = s['name'] in local
    print('file-local names: %d' % len(local))
    allalias, allobjs = [], []
    for side in ('st', 'eg'):
        objs, aliases = build_side(syms, side)
        # rename primary objects whose name must stay a macro: the emitted
        # symbol becomes f19d_<name> and each define site keeps its expr.
        for o in objs:
            if o['name'] in local:
                o['name'] = 'f19d_' + o['name']
        emit_side(side, objs, aliases, make_gaps(objs, side))
        allalias += aliases
        allobjs += objs
    if '--rewrite' in sys.argv:
        for fn in rewrite_files(syms, allalias, allobjs):
            print('rewrote', os.path.relpath(fn, ROOT))

if __name__ == '__main__':
    emit()
