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
    img = load_img(segfn, imgname)
    decls = []
    for line in open(SRC + '/' + hdr):
        mm = re.match(
            r'\s*(\w+)\s+(m_\w+|f19_(?:st|eg)gap_\w+)'
            r'((?:\s*\[[^\]]*\])+)?\s*;.*?/\*\s*(0x[0-9A-Fa-f]+)'
            r'\s*\*/', line)
        if mm and mm.group(1) in CSIZE:
            decls.append(dict(name=mm.group(2), off=int(mm.group(4), 16),
                              ctype=mm.group(1), arr=mm.group(3) or '',
                              array=bool(mm.group(3))))
    decls.sort(key=lambda m: m['off'])

    lines = ['/* generated by tools/f19_imginit.py - see %s */' % hdr,
             '/* per-member initializers replace the flat dseg image */',
             '#include "%s"' % hdr, '',
             'static const struct %s %s = {' % (stname, initname)]
    for m in decls:
        lines.append('    %s,  /* %s @0x%05X */'
                     % (wrap(init(m, img)), m['name'], m['off']))
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
