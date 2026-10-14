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

SRC_REF = None        # git ref to read sources from (pre-rewrite baseline)

def src_text(path):
    """source text of path: from SRC_REF (git show) or the worktree."""
    if SRC_REF:
        import subprocess
        try:
            return subprocess.check_output(
                ['git', 'show', '%s:%s' % (SRC_REF,
                                         os.path.relpath(path, ROOT))],
                cwd=ROOT, text=True)
        except subprocess.CalledProcessError:
            return ''
    return open(path).read()

def load_img(path, name):
    t = src_text(path)
    m = re.search(name + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', t, re.S)
    return [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1))]

# The flat image blobs were superseded by per-member initializers emitted
# by tools/f19_imginit.py (f19stvars.c / f19egvars.c); the segdat.c files
# are gone, so IMG degrades to empty and emit_side is no longer runnable.
def _img_or_empty(path, name):
    try:
        return load_img(path, name)
    except (IOError, AttributeError):
        return []


IMG = {'st': _img_or_empty(SRC + '/f19segdat.c', 'f19_dseg_img'),
       'eg': _img_or_empty(SRC + '/f19egsegdat.c', 'f19_egDsegImage')}
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
        for i, line in enumerate(src_text(fn).split('\n'), 1):
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
        t = src_text(fn)
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
    # secondary defines sharing an object's offset still need visible names
    for o in objs:
        for d in o['defs'][1:]:
            aliases.append((d, o))
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
    # retarget aliases hosted on swallowed objects to the surviving parent
    inset = set(id(o) for o in out)
    aliases = [(s, h if h is None or id(h) in inset else
                next((p for p in out
                      if p['off'] <= s['off'] < p['off'] + p['size']), None))
               for s, h in aliases]
    return out, aliases

BUILTIN_C = set(CSIZE)          # types safe to extern in a public header

def alias_expr(s, host):
    if host is None:
        if s['kind'] == 'indirect':
            return '((%s *)f19_dsegAt(*(uint16 *)f19_dsegAt(0x%X)))' \
                   % (s['ctype'], s['off'])
        if s['kind'] == 'addr':
            return '((uint8 *)f19_dsegAt(0x%X))' % s['off']
        if s['kind'] in ('ptrcast', 'arrcell', 'base'):
            return '((%s *)f19_dsegAt(0x%X))' % (s['ctype'], s['off'])
        return '(*(%s *)f19_dsegAt(0x%X))' % (s['ctype'], s['off'])
    delta = s['off'] - host['off']
    base, ht = host['mref'], host['emit_t']
    # address-of-storage: array members decay, scalar members need &
    bref = base if host['kind'] in ('ptrcast', 'arrnd', 'arrcell', 'gap') \
        else '(%s *)&%s' % (ht, base)
    t = s['ctype']
    if s['kind'] == 'scalar':
        # always '(*(T*)...)': at decl-shadow sites ('int16 name;' where the
        # local shadows the macro) the parenthesized deref still parses as a
        # C++ functional-cast expression - the exact shape the original had.
        return '(*(%s *)((uint8 *)%s + %d))' % (t, bref, delta)
    if s['kind'] in ('ptrcast', 'arrcell'):
        if delta == 0 and host['kind'] == 'ptrcast':
            return '((%s *)%s)' % (t, base)
        return '((%s *)((uint8 *)%s + %d))' % (t, bref, delta)
    if s['kind'] == 'arrnd':
        dims = ''.join('[%d]' % d for d in s['extra'])
        return '((%s (*)%s)((uint8 *)%s + %d))' % (t, dims, bref, delta)
    if s['kind'] == 'indirect':
        return '((%s *)f19_dsegAt(*(uint16 *)f19_dsegAt(0x%X)))' \
               % (t, s['off'])
    if s['kind'] == 'addr':
        return '((uint8 *)%s + %d)' % (bref, delta)
    if s['kind'] == 'base':
        return '((%s *)((uint8 *)%s + %d))' % (t, bref, delta)
    if s['kind'] == 'ptrscalar':
        return '(*(%s **)((uint8 *)%s + %d))' % (t, bref, delta)
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

def member_decl(o):
    """packed-struct member decl for the object (byte storage for opaque
    types; real typed members for builtin scalars/arrays)."""
    k, n, sz = o['kind'], o['emit'], o['size']
    t = o['emit_t']
    if o['gap']:
        return 'uint8 %s[%d];   /* gap */' % (n, sz)
    if k == 'arrnd' and t != 'uint8':
        return '%s %s[%d]%s;' % (t, n, o['count'],
                                ''.join('[%d]' % d for d in o['extra']))
    if k in ('ptrcast', 'arrnd'):
        if t == 'uint8':
            return 'uint8 %s[%d];' % (n, sz)
        return '%s %s[%d];' % (t, n, sz // max(1, esz(t)))
    if k == 'arrcell':
        return 'uint32 %s[%d];   /* far {off,seg} cells */' % \
               (n, max(1, sz // 4))
    if k == 'indirect':
        return 'uint16 %s;   /* offset cell */' % n
    if k == 'ptrscalar':
        return 'uint32 %s;   /* far cell */' % n
    if t == 'uint8' and o['ctype'] != 'uint8':
        return 'uint8 %s[%d];' % (n, sz)
    return '%s %s;' % (o['ctype'], n)

def membref(o, side):
    return 'f19_%sSpace.%s' % (side, o['emit'])

def view_expr(o, side):
    """typed view over byte storage, for the name's public macro."""
    t, k, b = o['ctype'], o['kind'], membref(o, side)
    if k == 'ptrcast':
        return '((%s *)%s)' % (t, b)
    if k == 'arrnd':
        return '((%s (*)%s)%s)' % (t, ''.join('[%d]' % d
                                             for d in o['extra']), b)
    if k == 'arrcell':
        return '((%s **)%s)' % (t, b)
    return '(*(%s *)&%s)' % (t, b)

def emit_side(side, objs, aliases, gaps):
    tag = 'st' if side == 'st' else 'eg'
    hname = 'f19%svars' % tag
    space = 'f19_%sSpace' % tag
    im = IMG[side]
    S = 'F19%sData' % tag.upper()
    H = [('/* generated by tools/f19_deblob.py - named globals replacing the'
          '   f19_dseg blob (%s.EXE data segment).  All dseg cells are members'
          '   of one packed struct at their exact DOS offsets, so indexed and'
          '   overlapping accesses see contiguous DOS-ordered storage. */')
         % ('START' if side == 'st' else 'EGAME'),
         '#ifndef F19%sVARS_H' % tag.upper(),
         '#define F19%sVARS_H' % tag.upper(),
         '#include "inttype.h"', '#include "f19seg.h"', '',
         '#pragma pack(push, 1)',
         'struct %s {' % S]
    # members in DOS-offset order: objs + gaps sorted together
    allsp = sorted(objs + gaps, key=lambda o: o['off'])
    for o in allsp:
        H.append('    %-40s /* 0x%05X */' % (member_decl(o), o['off']))
    H += ['};', '#pragma pack(pop)', '',
          'extern struct %s %s;' % (S, space), '']
    C = ['/* generated by tools/f19_deblob.py - see %s.h */' % hname,
         '#include "%s.h"' % hname, '#include <string.h>',
         'extern const uint8 %s[];' % IMGNAME[side], '',
         'struct %s %s;' % (S, space), '']
    R = ['/* image bytes cover [0,0x%X); the rest is DOS BSS (zero). */' % len(im),
         'void f19_%sVarsReset(void) {' % tag,
         '    memcpy(&%s, %s, %d);' % (space, IMGNAME[side], len(im)),
         '    memset((char *)&%s + %d, 0, sizeof %s - %d);'
         % (space, len(im), space, len(im)),
         '}',
         '',
         'typedef char f19_%sLayoutChk[(sizeof %s) == 0x10000 ? 1 : -1];'
         % (side, space)]
    # public names -> member lvalues / typed views / resolver exprs
    for o in objs:
        if o['defs'][0].get('file_local'):
            continue
        k = o['kind']
        if k == 'ptrscalar':
            H.append('#define %s (*(%s **)f19_dsegAt(0x%X))'
                     % (o['name'], o['ctype'], o['off']))
        elif k == 'indirect':
            H.append('#define %s ((%s *)f19_dsegAt(*(uint16 *)'
                     'f19_dsegAt(0x%X)))' % (o['name'], o['ctype'], o['off']))
        elif o.get('need_view'):
            H.append('#define %s %s' % (o['name'], view_expr(o, side)))
        elif k == 'scalar':
            # parenthesized deref: a 'T name;' decl-shadow site still parses
            # as a functional cast, exactly like the original blob form did.
            H.append('#define %s (*(%s *)&%s)'
                     % (o['name'], o['ctype'], membref(o, side)))
        else:
            H.append('#define %s %s' % (o['name'], membref(o, side)))
    seen = {o['name'] for o in objs if not o['defs'][0].get('file_local')}
    for s, host in aliases:
        if s.get('file_local') or s['name'] in seen:
            continue
        seen.add(s['name'])
        e = alias_expr(s, host)
        if e and not re.search(r'\b%s\b' % s['name'], e):
            H.append('#define %s %s' % (s['name'], e))
        # else: name == its own host object; bare name resolves directly
    H += ['', 'void f19_%sVarsReset(void);' % tag, '#endif', '']
    C += R + ['']
    open(SRC + '/%s.h' % hname, 'w').write('\n'.join(H))
    open(SRC + '/%s.c' % hname, 'w').write('\n'.join(C))
    print('%s: %d objects, %d gaps, %d aliases' %
          (side, len(objs), len(gaps), len(aliases)))

def make_gaps(objs, side):
    gaps, cur = [], 0
    for o in sorted(objs, key=lambda o: o['off']):
        if o['off'] > cur:
            nm = 'f19_%sgap_%X' % (side, cur)
            gaps.append(dict(off=cur, kind='gap', ctype='uint8',
                             name=nm, emit=nm, emit_t='uint8',
                             size=o['off'] - cur, count=0, extra=None,
                             gap=True))
        cur = max(cur, o['off'] + o['size'])
    if cur < 0x10000:
        nm = 'f19_%sgap_%X' % (side, cur)
        gaps.append(dict(off=cur, kind='gap', ctype='uint8',
                         name=nm, emit=nm, emit_t='uint8',
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
            # match the opening '[': nested brackets tracked, any ops pass
            d, j2 = 1, k
            while j2 < len(line) and d:
                if line[j2] == '[':
                    d += 1
                elif line[j2] == ']':
                    d -= 1
                j2 += 1
            out.append('(*(uint8 *)f19_dsegAt(%s))' % line[k:j2 - 1].strip())
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

SKIP_BODY = ('f19data.c', 'f19egdata.c', 'f19seg.c', 'f19seg.h',
             'f19segdat.c', 'f19egsegdat.c')          # hand-edited

# pointer<->offset sites the xlator can't infer ('X - f19_dseg' idioms and
# prose).  Keyed by filename, applied to the post-rewrite text.
POST_PATCH = {
    'stmap.c': [(
        'esTabBase = (uint16)((char *)esTable - (char *)((uint8 *)f19_dsegAt(0)) /*BARE*/);',
        'esTabBase = f19_dsegOff(esTable);')],
    'stgen.c': [(
        'wldOffsets[j++] = (int16)((uint8 *)(wldReadBuf11 + l + 1) - ((uint8 *)f19_dsegAt(0)) /*BARE*/);',
        'wldOffsets[j++] = (int16)f19_dsegOff(wldReadBuf11 + l + 1);')],
    'eg3dload.c': [(
        'matrix3dt_2[cat][tile] = (uint16)((char *)OBJ(byteOff) - (char *)((uint8 *)f19_dsegAt(0)) /*BARE*/);',
        'matrix3dt_2[cat][tile] = f19_dsegOff(OBJ(byteOff));')],
    'f19stubs.c': [(
        '/* resolve a descriptor arg that callers pass either as a bare dseg offset\n'
        ' * (cast through a pointer type) or as a real ((uint8 *)f19_dsegAt(0)) /*BARE*/-relative pointer. */\n'
        'static int16 *f19_descPtr(void *o) {\n'
        '    uintptr_t v = (uintptr_t)o;\n'
        '    if (v >= (uintptr_t)((uint8 *)f19_dsegAt(0)) /*BARE*/ && v < (uintptr_t)((uint8 *)f19_dsegAt(0x100000)))\n'
        '        v -= (uintptr_t)((uint8 *)f19_dsegAt(0)) /*BARE*/;\n'
        '    return (int16 *)(((uint8 *)f19_dsegAt((uint16)v)));\n'
        '}',
        '/* resolve a descriptor arg that callers pass either as a bare dseg offset\n'
        ' * (cast through a pointer type) or as a real dseg-object pointer. */\n'
        'static int16 *f19_descPtr(void *o) {\n'
        '    if (f19_dsegOff(o) != 0xFFFF)\n'
        '        return (int16 *)o;\n'
        '    return (int16 *)f19_dsegAt((uint16)(uintptr_t)o);\n'
        '}'), (
        'f19_wrapUnitText((int16)(uintptr_t)((char *)f19_descPtr(pg) - (char *)((uint8 *)f19_dsegAt(0)) /*BARE*/), s, a, b, c, d);',
        'f19_wrapUnitText((int16)f19_dsegOff(f19_descPtr(pg)), s, a, b, c, d);')],
    'eginstr.c': [(
        '/* byte view of a word cell / unaligned-safe 16-bit ops on ((uint8 *)f19_dsegAt(0)) /*BARE*/ */',
        '/* byte view of a word cell / unaligned-safe 16-bit ops on dseg objects */')],
    'f19file.c': [(
        '/* resFileRead — near read: (h, count, dstoff) into ((uint8 *)f19_dsegAt(0)) /*BARE*/; count <0 => EOF.',
        '/* resFileRead — near read: (h, count, dstoff) into dseg objects; count <0 => EOF.')],
}

def rewrite_files(syms, aliases, objs, local_names):
    amap = {id(s): h for s, h in aliases}
    prim = {id(d): o for o in objs for d in o['defs']}
    byname = {s['name'] for s in syms}
    touched = set(s['file'] for s in syms)
    # any other file still touching the blob in its body
    for fn in glob.glob(SRC + '/*.[ch]'):
        if 'vars' in os.path.basename(fn):
            continue
        if re.search(r'\bf19_dseg\b', src_text(fn)):
            touched.add(fn)
    touched -= {os.path.join(SRC, b) for b in SKIP_BODY}
    report = []
    for fn in sorted(touched):
        lines = src_text(fn).split('\n')
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
            # drop 'extern <T> <name>([..]);' for converted names — but a
            # file-local name's extern refers to the app global: keep it
            em = re.match(r'\s*extern\s+[\w \*]+?\b(\w+)\s*(\[[^\]]*\])?\s*;',
                          line)
            if em and em.group(1) in byname and \
                    em.group(1) not in local_names:
                continue
            nl.append(line)
        # body xlation on the joined text: index exprs may span lines
        text, hits = xlate_dseg('\n'.join(nl))
        if need_inc:
            inc = '#include "f19%svars.h"' % side_of(fn)
            nl2 = text.split('\n')
            for k, l in enumerate(nl2):
                if l.startswith('#include'):
                    nl2.insert(k + 1, inc)
                    break
            else:
                nl2.insert(0, inc)
            text = '\n'.join(nl2)
        for old, new in POST_PATCH.get(os.path.basename(fn), []):
            if old not in text:
                print('  PATCH-MISS %s: %.60s' %
                      (os.path.basename(fn), old.replace('\n', ' ')))
            text = text.replace(old, new)
        open(fn, 'w').write(text)
        report.append(fn)
    return report

def extern_globals():
    """names defined (real objects) outside src/f19 - the app/F-15 globals."""
    names = set()
    for fn in glob.glob(ROOT + '/src/*.c') + glob.glob(ROOT + '/src/*.h'):
        for l in src_text(fn).split('\n'):
            s = l.lstrip()
            if s.startswith(('#', 'typedef')):
                continue
            # 'extern int x = {...}' is a definition; a bare extern is not
            if s.startswith('extern') and '=' not in s:
                continue
            m = re.match(r'[\w \*]+\b(\w+)\s*(\[|=|;)', s)
            if m:
                names.add(m.group(1))
    return names

def emit():
    global SRC_REF
    for a in sys.argv[1:]:
        if a.startswith('--src-ref='):
            SRC_REF = a.split('=', 1)[1]
    if SRC_REF is None:
        # default: harvest the committed baseline so reruns stay stable
        # even after the worktree has been rewritten
        import subprocess
        dirty = subprocess.check_output(
            ['git', 'status', '--porcelain', 'src/f19/'],
            cwd=ROOT, text=True).strip()
        if dirty:
            print('ERROR: src/f19/ worktree is dirty and no --src-ref given;\n'
                  '      harvest would read already-converted sources.\n'
                  '      Use --src-ref=<commit> with a pre-rewrite baseline.')
            sys.exit(1)
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
        # finalize emitted storage: builtin typed objects keep their name;
        # local-macro and opaque-type objects get an f19d_ storage symbol
        # (offset suffix when the name maps to several cells).
        seen = {}
        for o in objs:
            bt = o['ctype'].replace('const ', '').strip()
            if o['kind'] == 'arrcell':
                bt = 'uint32'
            builtin = bt in BUILTIN_C
            o['need_view'] = not builtin and o['kind'] != 'indirect'
            nm = 'm_' + o['name']
            if nm in seen:                       # multi-sig dup member
                nm = '%s_%X' % (nm, o['off'])
            o['emit'] = nm
            o['emit_t'] = bt if builtin else 'uint8'
            o['mref'] = 'f19_%sSpace.%s' % (side, nm)
            seen[o['emit']] = o
        # alias hosts that fell back to None resolve via f19_dsegAt
        for s, h in aliases:
            if h is not None and 'mref' not in h:
                h['mref'] = 'f19_%sSpace.m_%s' % (side, h['name'])
                bt = h['ctype'].replace('const ', '').strip()
                h['emit_t'] = bt if bt in BUILTIN_C else 'uint8'
        emit_side(side, objs, aliases, make_gaps(objs, side))
        allalias += aliases
        allobjs += objs
    if '--rewrite' in sys.argv:
        for fn in rewrite_files(syms, allalias, allobjs, local):
            print('rewrote', os.path.relpath(fn, ROOT))

if __name__ == '__main__':
    emit()
