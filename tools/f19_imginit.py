#!/usr/bin/env python3
"""f19_imginit — replace the flat f19_dseg_img[]/f19_egDsegImage[] byte
blobs with per-member initializers on f19_stSpace/f19_egSpace.

The DOS load image is sliced member-by-member (offsets come from the
generated packed-struct tables) and emitted as typed C initializers:
  uint16 m_word_X[N]  -> {0x1234, ...}   word values, not byte pairs
  char   m_str_X[N]   -> "text\000more"  string tables stay readable
  f19_stgap_X[N]      -> {0xNN, ...}     unnamed regions keep their bytes

Reset becomes `f19_xSpace = f19_xInit;` — a struct assignment, so BSS-tail
members re-zero automatically.  Byte-exactness vs the old image arrays is
verified by tests/f19_imginit_check.c (compile both, memcmp).
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = ROOT + '/src/f19'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from f19_bindlit import CSIZE                   # noqa: E402

IMGS = {'st': (SRC + '/f19segdat.c', 'f19_dseg_img',
               'f19stvars.h', 'f19stvars.c', 'F19STData',
               'f19_stSpace', 'f19_stInit', 'f19_stVarsReset'),
        'eg': (SRC + '/f19egsegdat.c', 'f19_egDsegImage',
               'f19egvars.h', 'f19egvars.c', 'F19EGData',
               'f19_egSpace', 'f19_egInit', 'f19_egVarsReset')}

SIGNED = {'int8': 1, 'int16': 2, 'int32': 4}


def load_img(fn, name):
    txt = open(fn).read()
    m = re.search(r'uint8\s+' + name + r'\s*\[[^\]]*\]\s*=\s*\{(.*?)\};',
                  txt, re.S)
    return [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1))]


def _strbytes(body):
    """Decode emitted literal text — may be several adjacent "..." parts
    (line-wrapped). Octal \\ooo escapes and printable chars; delimiter
    quotes and inter-literal whitespace are skipped."""
    out, i, instr = [], 0, False
    while i < len(body):
        ch = body[i]
        if not instr:
            if ch == '"':
                instr = True
            i += 1
            continue
        if ch == '"':
            instr = False
            i += 1
            continue
        if ch == '\\':
            out.append(int(body[i + 1:i + 4], 8))
            i += 4
            continue
        out.append(ord(ch))
        i += 1
    return out


def _charlit(tok):
    """Decode an emitted char element: 'x', escapes, \\ooo, or number."""
    if tok.startswith("'"):
        inner = tok[1:-1]
        if inner.startswith('\\'):
            if inner[1] in '01234567':
                return int(inner[1:], 8)
            return ord(inner[1])
        return ord(inner)
    if tok.startswith('0x') or tok.startswith('-0x'):
        return int(tok, 16)
    return int(tok)


def _elems(body):
    """Flatten an emitted init body into leaf tokens: top-level commas
    only — skips brace nesting, "..." string literals and 'x'/'\\ooo'
    char literals (emitted '"' data is \\042 so every '"' is a
    delimiter; commas inside char literals like ',' stay quoted)."""
    toks, depth, cur, instr, inch, esc = [], 0, '', False, False, False
    for ch in body:
        if instr:
            cur += ch
            if ch == '"':
                instr = False
            continue
        if inch:
            cur += ch
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == "'":
                inch = False
            continue
        if ch == '"':
            instr = True
            cur += ch
        elif ch == "'":
            inch = True
            cur += ch
        elif ch == '{':
            depth += 1
            if depth > 1:
                cur += ch
        elif ch == '}':
            depth -= 1
            if depth:
                cur += ch
            else:
                if cur.strip():
                    toks.append(cur)
                cur = ''
        elif ch == ',' and depth == 1:
            toks.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        toks.append(cur)
    return [t for t in toks if t.strip()]


def member_bytes(init_body, m):
    """Decode an emitted initializer back to the member's image bytes."""
    ct, esz = m['ctype'], CSIZE[m['ctype']]
    n = m['size']
    body = init_body.strip()
    if body.startswith('"'):
        return (_strbytes(body) + [0] * n)[:n]
    if not body.startswith('{'):
        v = int(body, 0)
        return [(v >> (8 * i)) & 0xFF for i in range(esz)]
    out = []

    def leaves(t):
        t = t.strip()
        if t.startswith('{'):
            for st in _elems(t):
                yield from leaves(st)
        else:
            yield t

    for t in _elems(body):
        for st in leaves(t):
            v = _charlit(st) if ct == 'char' else int(st, 0)
            out += [(v >> (8 * i)) & 0xFF for i in range(esz)]
    return (out + [0] * n)[:n]


def img_from_vars(hdrfn, cfn):
    """Rebuild the DOS image bytes from the emitted vars.c initializers
    — segdat.c no longer exists, so vars.c is the image source of truth."""
    decls = []
    for line in open(hdrfn):
        mm = re.match(
            r'\s*(\w+)\s+(m_\w+|f19_(?:st|eg)gap_\w+)'
            r'((?:\s*\[[^\]]*\])+)?\s*;.*?/\*\s*(0x[0-9A-Fa-f]+)\s*\*/',
            line)
        if mm and mm.group(1) in CSIZE:
            dims = [int(x) for x in re.findall(r'\[(\d+)\]',
                                               mm.group(3) or '')]
            cnt = 1
            for d in dims:
                cnt *= d
            decls.append(dict(name=mm.group(2), off=int(mm.group(4), 16),
                              ctype=mm.group(1), arr=mm.group(3) or '',
                              array=bool(mm.group(3)),
                              size=CSIZE[mm.group(1)] * cnt))
    decls.sort(key=lambda m: m['off'])
    txt = open(cfn).read()
    body = re.search(r'=\s*\{(.*?)\n\};', txt, re.S).group(1)
    inits = [t for t in _elems('{' + body + '}')
             if re.sub(r'/\*.*?\*/', '', t).strip()]
    if len(inits) != len(decls):
        raise ValueError('%s: %d init entries vs %d members'
                         % (cfn, len(inits), len(decls)))
    img = []
    for m, ib in zip(decls, inits):
        ib = re.sub(r'/\*.*?\*/', '', ib)           # strip comments
        ib = re.sub(r'^\s*\.\w+\s*=\s*', '', ib)    # strip designator
        img += member_bytes(ib.strip(), m)
    return img, decls


def sval(ct, v):
    """Scalar value string — signed decimal for signed types (avoids
    C++ narrowing complaints on things like int16 0xFFFF)."""
    sz = CSIZE[ct]
    if ct in SIGNED and v >= 1 << (sz * 8 - 1):
        v -= 1 << (sz * 8)
    if ct == 'char':
        return char_lit(v)
    if ct in SIGNED:
        return str(v)
    return '0x%0*X' % (sz * 2, v)


def char_lit(b):
    if b == 0:
        return '0'
    if 32 <= b < 127 and b not in (39, 92):      # ' and \ escape
        return "'%c'" % b
    if b == 39:
        return "'\\''"
    if b == 92:
        return "'\\\\'"
    return "'\\%03o'" % b                       # octal — fixed width, safe


def str_lit(sl):
    """Slice → "..." string literal iff every byte is representable and
    the emitted bytes leave room for the implicit terminator + zero fill.
    Octal escapes make NULs and binary bytes unambiguous."""
    out = ''.join(chr(b) if 32 <= b < 127 and b not in (34, 92)
                  else '\\%03o' % b for b in sl)
    return '"%s"' % out


def wrap(body):
    """Wrap a long initializer at token boundaries — never inside a
    string literal (commas live inside them) or an octal escape."""
    if len(body) <= 100:
        return body
    if body.startswith('"'):
        # adjacent literal concatenation; split only at escape boundaries
        inner = body[1:-1]
        toks = re.findall(r'\\[0-7]{3}|.', inner)
        lines, cur = [], '"'
        for t in toks:
            if len(cur) + len(t) > 95:
                lines.append(cur + '"')
                cur = '"' + t
            else:
                cur += t
        lines.append(cur + '"')
        return '\n    '.join(lines)
    parts = re.findall(r'[^,]+,?', body)
    lines, cur = [], ''
    for p in parts:
        if cur and len(cur) + len(p) > 95:
            lines.append(cur)
            cur = p
        else:
            cur += p
    if cur:
        lines.append(cur)
    return '\n    '.join(lines)


def init(m, img):
    """Return the C initializer text for member m from image bytes."""
    off, ct = m['off'], m['ctype']
    dims = [int(x) for x in re.findall(r'\[(\d+)\]', m.get('arr') or '')]
    esz = CSIZE[ct]
    cnt = 1
    for d in dims:
        cnt *= d
    sl = img[off:off + cnt * esz]
    sl += [0] * (cnt * esz - len(sl))           # BSS tail beyond image

    if not m['array']:
        v = 0
        for i, b in enumerate(sl[:esz]):
            v |= b << (8 * i)
        return sval(ct, v)

    if not any(sl):
        return '{0}'

    if ct == 'char' and len(dims) == 1:
        # single literal works iff trailing bytes (beyond what we emit)
        # are zero AND at least one implicit-NUL slot remains
        k = max(i for i, b in enumerate(sl) if b) + 1
        if k <= cnt - 1 and not any(sl[k:]):
            return str_lit(sl[:k])

    def elem(i):
        b = sl[i * CSIZE[ct]:(i + 1) * CSIZE[ct]]
        v = 0
        for j, x in enumerate(b):
            v |= x << (8 * j)
        return char_lit(v) if ct == 'char' else sval(ct, v)

    def nest(dim, base):
        """Emit dims[dim:] — elems are CSIZE[ct] bytes each."""
        if dim == len(dims) - 1:
            return '{%s}' % ','.join(
                elem(base + i) for i in range(dims[dim]))
        stride = 1
        for d in dims[dim + 1:]:
            stride *= d
        return '{%s}' % ','.join(
            nest(dim + 1, base + i * stride) for i in range(dims[dim]))

    return nest(0, 0)


def emit_side(side):
    segfn, imgname, hdr, cfile, stname, spname, initname, resetfn = \
        IMGS[side]
    if os.path.exists(segfn):
        img = load_img(segfn, imgname)
        decls = []
        for line in open(SRC + '/' + hdr):
            mm = re.match(
                r'\s*(\w+)\s+(m_\w+|f19_(?:st|eg)gap_\w+)'
                r'((?:\s*\[[^\]]*\])+)?\s*;.*?/\*\s*(0x[0-9A-Fa-f]+)'
                r'\s*\*/', line)
            if mm and mm.group(1) in CSIZE:
                decls.append(dict(name=mm.group(2),
                                  off=int(mm.group(4), 16),
                                  ctype=mm.group(1),
                                  arr=mm.group(3) or '',
                                  array=bool(mm.group(3))))
        decls.sort(key=lambda m: m['off'])
    else:
        # segdat.c is gone — vars.c holds the image as initializers
        img, decls = img_from_vars(SRC + '/' + hdr, SRC + '/' + cfile)

    lines = ['/* generated by tools/f19_imginit.py - see %s */' % hdr,
             '/* per-member initializers replace the flat dseg image */',
             '#include "%s"' % hdr, '',
             'static const struct %s %s = {' % (stname, initname)]
    for m in decls:
        lines.append('    .%s = %s,  /* @0x%05X */'
                     % (m['name'], wrap(init(m, img)), m['off']))
    lines += ['};', '',
              'struct %s %s;' % (stname, spname), '',
              'void %s(void) { %s = %s; }' % (resetfn, spname, initname),
              '',
              'typedef char %sLayoutChk[(sizeof %s) == 0x10000 ? 1 : -1];'
              % (side == 'st' and 'f19_st' or 'f19_eg', spname),
              '']
    open(SRC + '/' + cfile, 'w').write('\n'.join(lines))
    print('%s: %d members, %d image bytes -> %s'
          % (cfile, len(decls), len(img), initname))


def main():
    for side in ('st', 'eg'):
        emit_side(side)


if __name__ == '__main__':
    main()
