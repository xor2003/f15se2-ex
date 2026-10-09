#!/usr/bin/env python3
"""f19_enseg — generate the END.EXE dseg space (f19envars.h/.c) for the
merged F-19 port, from the f19ru reconstruction sources.

  inputs   $F19RU/lst/end_en_ada.lst   (IDA listing: dseg labels + offsets)
           $F19RU/build/end_en.asm     (byte-exact skeleton: dseg image rows)
           $F19RU/src_end/*.c          (extern decls: names, types, offsets)

  output   src/f19/f19envars.h  — packed struct F19ENData (cells at exact DOS
             offsets), accessor defines, extern f19_enSpace/f19_enInit,
             f19_enVarsReset()
           src/f19/f19envars.c  — per-member initializers sliced from the
             skeleton image + the reset function

Every dseg cell a src_end module references becomes a named member at its
DOS offset; unreferenced regions become gap arrays so DOS offsets still
resolve through f19_dsegAt(world 2). Pointer-typed externs stay 16-bit
cells; the native pointer aliases are materialized in f19/enmain.c at
END entry (commData/pilotRec wire straight to f19_commBase).
"""
import re, sys, os, glob

F19RU = os.environ.get('F19RU', '/home/xor/games/f19ru/F19')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src/f19')
SIZE = 0x9EF0                      # dseg image extent (dseg:0000..0x9EEF)

CSIZE = {'int8': 1, 'uint8': 1, 'char': 1, 'int16': 2, 'uint16': 2,
         'int32': 4, 'uint32': 4}
# externs that must NOT become dseg members (native objects / far-block ptrs)
NATIVE = {
    'commData', 'pilotRec',           # comm/game block (f19_commBase)
    'picBufPos',                      # ss:-cell — plain native int16
    'worldStrings',                   # near-ptr table -> native char* array
}
# manual offsets for names whose comments lack them
MISSING = {
    'word_1BA9E': 0x1D0E, 'word_1BACE': 0x1D3E, 'word_1BAD0': 0x1D40,
    'word_1BAE0': 0x1D50, 'word_1BAE2': 0x1D52,
    'byte_2089D': 0x6B0D, 'byte_208AD': 0x6B1D, 'byte_208BD': 0x6B2D,
    'byte_208CD': 0x6B3D, 'byte_1C6E0': 0x2950, 'byte_1C6E1': 0x2951,
    'byte_1C6E2': 0x2952, 'byte_1C6E3': 0x2953,
    'word_1C6E8': 0x2958, 'word_1C6EA': 0x295A,
    'word_1E25C': 0x44CC, 'word_1E280': 0x44F0, 'word_1E2A4': 0x4514,
    'word_1E51A': 0x478A, 'word_1E7BA': 0x4A2A, 'word_1E8D4': 0x4B44,
    'word_1EA42': 0x4CB2, 'word_1C6EC': 0x295C, 'recCount': 0x3CB4,
    'colorStyleTable': 0x41DE,
    'awardPrim': 0x4206, 'awardSec': 0x420C, 'awardVisId': 0x4210,
    'awardArmedGnd': 0x4216, 'awardUnitChk': 0x421C, 'awardCivilian': 0x4222,
    'awardUnarmedGnd': 0x4228, 'awardFriendlyGnd': 0x422E,
    'awardRadarId': 0x4234, 'award423a': 0x423A, 'award4240': 0x4240,
    'award4246': 0x4246, 'award424c': 0x424C, 'award4252': 0x4252,
    'multTheater': 0x42B8, 'multMission': 0x42C2, 'multDiff': 0x42CA,
    'multUnk': 0x42D2, 'multResult': 0x42D8,
    'mapWinX1': 0x41FE, 'mapWinY1': 0x4200, 'mapWinX2': 0x4202,
    'mapWinY2': 0x4204,
    # enworld.c non-extern defs (offsets from readWorldData disasm)
    'worldBufPtr': 0x6C26, 'worldBufHandle': 0x86AA,
    'worldDataReady': 0x99F2, 'worldStrings': 0x9A22,
    'worldWaypointCount': 0x99FA, 'worldObjectCount': 0x9920,
    'worldRouteTable': 0x86B6, 'worldRouteCount': 0x9ED4,
    'worldSamCount': 0x9A1C, 'worldSamTable': 0x8EA8,
    'unitTypeTable': 0x998A, 'worldUnitFlags': 0x9922,
    'worldStringBuf': 0x9AEC, 'worldGridSize': 0x99F0,
    'worldMiscHeader': 0x86A6, 'weaponDataBlock': 0x0042,
    'targetBlockWd': 0x8C7A, 'flightDataBuf': 0x9182,
    # non-extern globals in enbrief.c/enmain.c (comments + disasm)
    'mapViewX1': 0x99F6, 'mapViewY1': 0x99F8,
    'clipMaxX': 0x1909, 'clipMaxY': 0x190B,
    'lineX1': 0x157F, 'lineX2': 0x1581, 'lineY1': 0x1583,
    'lineY2': 0x1585, 'nightMission': 0x071C, 'tickByte': 0x41DB,
    'cursorX': 0x9A10, 'cursorY': 0x9A16,
    'colorAnimEnabled': 0x8696, 'selectedMenuItem': 0x9782,
    'inputChanged': 0x8692, 'enterPressed': 0x991F,
    'joyRepeatFlag': 0x8698, 'colorAnimIdx': 0x8694,
    'timerCounter2': 0x41DC, 'timerCounter3': 0x41DD,
    'animDone': 0x869A, 'spriteToggle': 0x8699,
    'joyAxisX': 0x193E, 'joyAxisY': 0x193F, 'quitFlag': 0x1942,
    'initResultFlag': 0x8B76, 'timerHandlerInstalled': 0x41BF,
    'colorTablePtr': 0x8690,
    # map-view cells (runMapView/loadMapView/drawMapView disasm offsets)
    'word_23786': 0x99F6, 'word_23788': 0x99F8,   # map-view origin == mapViewX1/Y1
    'word_1BAD2': 0x1D42, 'word_1BAD4': 0x1D44,   # ptr cells inside the 0x1D40 block
    'word_1DA44': 0x3CB4,                          # record count == recCount
    'word_2244C': 0x86BC,                          # page-A seg handle cell
    'word_23C60': 0x9ED0,                          # staged-stream cursor
    'word_23C62': 0x9ED2,                          # pic-stream fd cell
    'word_23C74': 0x9EE4,                          # staged-resource seg cell
    'word_1B544': 0x1BB4,                          # picReadBlock fd cell
    # RLE-blitter cell pairs (sub_12FE8 file variant / gety staged variant)
    'word_1DF3E': 0x41AE, 'word_1DF40': 0x41B0,
    'word_1DF42': 0x41B2, 'byte_1DF44': 0x41B4,
    'word_1DF46': 0x41B6, 'word_1DF48': 0x41B8,
    'word_1DF4A': 0x41BA, 'byte_1DF4C': 0x41BC,
    'byte_1DF6B': 0x41DB,                          # serviceTick counter == tickByte
}
# enworld.c defines dseg cells non-extern — inject them as synthetic decls
EXTRA = [
    ('uint8 far', 'worldBufPtr', 1, []),       # far-ptr cell (off+seg)
    ('int16', 'worldBufHandle', 0, []),
    ('int16', 'worldDataReady', 0, []),
    ('char', 'worldStrings', 1, [100]),        # near-ptr table -> char*[100]
    ('int16', 'worldWaypointCount', 0, []),
    ('int16', 'worldObjectCount', 0, []),
    ('int16', 'worldRouteTable', 0, [1]),
    ('int16', 'worldRouteCount', 0, []),
    ('int16', 'worldSamCount', 0, []),
    ('int16', 'worldSamTable', 0, [1]),
    ('int16', 'unitTypeTable', 0, [1]),
    ('int16', 'worldUnitFlags', 0, [1]),
    ('char', 'worldStringBuf', 0, [1]),
    ('int16', 'worldGridSize', 0, []),
    ('int16', 'worldMiscHeader', 0, []),
    ('int16', 'weaponDataBlock', 0, [8]),
    ('int16', 'targetBlockWd', 0, [18]),
    ('uint8', 'flightDataBuf', 0, [1]),
    # enbrief.c non-extern globals
    ('int16', 'mapViewX1', 0, []), ('int16', 'mapViewY1', 0, []),
    ('int16', 'clipMaxX', 0, []), ('int16', 'clipMaxY', 0, []),
    ('int16', 'lineX1', 0, []), ('int16', 'lineY1', 0, []),
    ('int16', 'lineX2', 0, []), ('int16', 'lineY2', 0, []),
    ('int16', 'nightMission', 0, []), ('uint8', 'tickByte', 0, []),
    ('uint16', 'cursorX', 0, []), ('uint16', 'cursorY', 0, []),
    ('int16', 'colorAnimEnabled', 0, []),
    ('int16', 'selectedMenuItem', 0, []),
    ('uint8', 'inputChanged', 0, []), ('uint8', 'enterPressed', 0, []),
    ('uint8', 'joyRepeatFlag', 0, []), ('int16', 'colorAnimIdx', 0, []),
    ('uint8', 'timerCounter2', 0, []), ('uint8', 'timerCounter3', 0, []),
    ('uint8', 'animDone', 0, []), ('uint8', 'spriteToggle', 0, []),
    ('uint8', 'joyAxisX', 0, []), ('uint8', 'joyAxisY', 0, []),
    ('uint8', 'quitFlag', 0, []), ('int16', 'initResultFlag', 0, []),
    ('uint8', 'timerHandlerInstalled', 0, []),
    ('uint16', 'colorTablePtr', 1, []),
    # decoder/blitter cells only referenced by skeleton natives (f19enskel.c)
    ('int16', 'word_1DF3E', 0, []), ('int16', 'word_1DF40', 0, []),
    ('int16', 'word_1DF42', 0, []), ('uint8', 'byte_1DF44', 0, []),
    ('int16', 'word_1DF46', 0, []), ('int16', 'word_1DF48', 0, []),
    ('int16', 'word_1DF4A', 0, []), ('uint8', 'byte_1DF4C', 0, []),
    ('int16', 'word_1B544', 0, []),                 # picReadBlock fd cell
    ('int16', 'word_23C60', 0, []),                 # staged-stream cursor
    ('int16', 'word_23C62', 0, []),                 # pic-stream fd
    ('int16', 'word_23C74', 0, []),                 # staged-resource seg
    ('int16', 'word_2244C', 0, []),                 # page-A seg handle
]


def load_labels():
    """dseg offset -> IDA label, from the lst dseg section."""
    lab = {}
    for l in open(F19RU + '/lst/end_en_ada.lst'):
        m = re.match(r'dseg:([0-9A-Fa-f]{4})\s+(.*)', l)
        if not m:
            continue
        off = int(m.group(1), 16)
        rest = m.group(2)
        mm = re.match(r'([A-Za-z_]\w*)\s*:', rest)
        if mm:
            lab.setdefault(off, mm.group(1))
            continue
        mm = re.match(r'([A-Za-z_]\w*)\s+(db|dw|dd|dp|dq|dt|proc|endp)\b', rest)
        if mm and mm.group(1) not in ('db', 'dw', 'dd', 'dp', 'dq', 'dt'):
            lab.setdefault(off, mm.group(1))
    return lab


def load_image():
    """dseg byte rows from the byte-exact skeleton asm."""
    lines = open(F19RU + '/build/end_en.asm').read().splitlines()
    start = end = None
    for i, l in enumerate(lines):
        if re.match(r'\s*dseg\s+segment', l):
            start = i
        if re.match(r'\s*dseg\s+ends', l):
            end = i
            break
    data = []
    for l in lines[start:end]:
        m = re.match(r'\s*db\s+(.+)', l)
        if m:
            for tok in m.group(1).split(','):
                tok = tok.strip()
                if tok.endswith('h'):
                    data.append(int(tok[:-1], 16))
                elif tok == '?':
                    data.append(0)
                else:
                    data.append(int(tok))
    return data


def harvest():
    """extern decls across src_end/*.c -> (name, ctype, ptr, dims, off)."""
    recs = []
    for f in sorted(glob.glob(F19RU + '/src_end/*.c')):
        txt = open(f).read()
        for m in re.finditer(r'extern\s+([^;()]*);', txt):
            decl = m.group(1).strip()
            # comment on the extern's own line(s); else nearest comment
            # *before* it (struct-def comments carry the dseg offset).
            eol = txt.find('\n', m.end())
            seg = txt[m.start():eol]
            cm = re.search(r'/\*(.*?)\*/', seg, re.S)
            comment = cm.group(1) if cm else ''
            has_off = re.search(r'dseg:0x|\b(?:word|byte|dword|unk|off|seg)_[0-9A-Fa-f]{5}\b|0x[0-9A-Fa-f]{3,4}', comment)
            if not has_off:
                # nearest preceding comment carrying an offset, at most
                # ~300 chars back (struct-def comments sit adjacent)
                back = txt[max(0, m.start() - 300):m.start()]
                for cb in re.finditer(r'/\*((?:(?!\*/).)*?)\*/', back, re.S):
                    if re.search(r'dseg:0x|\b(?:word|byte|dword|unk|off|seg)_[0-9A-Fa-f]{5}\b|0x[0-9A-Fa-f]{3,4}', cb.group(1)):
                        comment = cb.group(1)
            # base type = leading words before first declarator
            TYPES = {'MenuItem', 'EvtItem', 'BlinkSprite', 'MapRect',
                     'FlightLogRec', 'PlaneObjEnd', 'WorldObjEnd',
                     'PlaneNameEnd', 'SamNameEnd', 'CommDataEnd',
                     'PilotRecEnd', 'PilotRecMain'}
            names = []
            prev = ''
            for nm in re.finditer(
                    r'([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)', decl):
                n = nm.group(1)
                skip = n in ('int16', 'int8', 'uint8', 'uint16', 'int32',
                             'uint32', 'char', 'void', 'far', 'near',
                             'struct', 'const', 'unsigned', 'signed') \
                    or n in TYPES
                if skip or prev == 'struct':
                    prev = n
                    continue
                prev = n
                names.append((n, nm.group(2)))
            # base type string = decl prefix up to the first declarator
            first = names[0][0] if names else ''
            tstr = decl[:decl.find(first)].strip() if first else 'int16'
            tstr = re.sub(r'\s*(far|near)\s*', ' ', tstr).strip()
            tstr = tstr.rstrip('*').strip()
            bty = tstr.split()[-1] if tstr else 'int16'
            is_ptr = '*' in decl
            def hexruns(s):
                """0xAAA/BB/CC -> ['AAA','AABB','AACC'] (suffix parts keep
                the first group's high nibbles)."""
                out = []
                for run in re.findall(r'0x([0-9A-Fa-f]+(?:/[0-9A-Fa-f]+)*)', s):
                    parts = run.split('/')
                    for i, p in enumerate(parts):
                        if i:
                            p = parts[0][:-len(p)] + p
                        out.append(p)
                return out
            offs = []
            for grp in re.findall(r'dseg:\s*((?:0x[0-9A-Fa-f]+/?)+)', comment):
                offs += hexruns(grp)
            # IDA label tokens in the comment resolve through their name;
            # a /NN suffix inherits the head's high nibbles (word_23E6E/70).
            for m in re.finditer(r'\b(?:word|byte|dword|unk|off|seg)_([0-9A-Fa-f]{5})((?:/[0-9A-Fa-f]+)*)', comment):
                w = m.group(1)
                for p in ([w] + [x.lstrip('/') for x in
                                 re.findall(r'/[0-9A-Fa-f]+', m.group(2))]):
                    if p != w:
                        p = w[:-len(p)] + p
                    v = int(p, 16) - 0x19D90
                    if 0 <= v < 0x10000:
                        offs.append('%x' % v)
            if not offs and len(names) > 0:
                # bare-hex comments like "0x667a/88/96" or "record table at 0x295e"
                offs = hexruns(comment)
            for i, (n, dims) in enumerate(names):
                off = None
                if n in MISSING:
                    off = MISSING[n]
                elif i < len(offs):
                    off = int(offs[i], 16)
                recs.append({'name': n, 'ty': bty, 'tstr': tstr,
                             'ptr': is_ptr,
                             'dims': dims, 'off': off, 'src': f})
    # synthetic decls for non-extern dseg defs (enworld.c globals)
    for tstr, n, is_ptr, dims in EXTRA:
        dims_s = ''.join('[%d]' % d for d in dims)
        recs.append({'name': n, 'ty': tstr.split()[-1],
                     'tstr': tstr.replace(' far', '').strip(),
                     'ptr': is_ptr, 'dims': dims_s,
                     'off': MISSING.get(n), 'src': '(enworld.c)'})
    return recs


def main():
    lab = load_labels()
    img = load_image()
    print('image bytes: %#x (%d), labels: %d' % (len(img), len(img), len(lab)))
    recs = harvest()
    # resolve offsets: MISSING > comment dseg: > lst label > linear formula
    unresolved = []
    for r in recs:
        n = r['name']
        if n in NATIVE:
            continue
        if r['off'] is None:
            if n in lab.values():
                for o, v in lab.items():
                    if v == n:
                        r['off'] = o
                        break
            elif re.match(r'^(word|byte|dword|unk|off|seg)_[0-9A-Fa-f]{5}$', n):
                v = int(n.split('_')[1], 16) - 0x19D90
                if 0 <= v < 0x10000:
                    r['off'] = v
        if r['off'] is None:
            unresolved.append(n)
    if unresolved:
        print('UNRESOLVED:', sorted(set(unresolved)))
        sys.exit(1)
    # dedupe + sort
    seen = {}
    for r in recs:
        if r['name'] in NATIVE or r['off'] is None:
            continue
        if r['name'] not in seen:
            seen[r['name']] = r
    items = sorted(seen.values(), key=lambda r: r['off'])

    # ---- emit struct -----------------------------------------------------
    # Every name at a distinct offset gets a member whose extent runs to the
    # next member start — indexed access past a truncated member still lands
    # on the right DOS-ordered bytes through the define's pointer cast.
    # Same-offset duplicates become aliases on the earlier member.
    raw = [r for r in items if r['off'] < SIZE]
    members = []
    for r in raw:
        if members and r['off'] == members[-1]['off']:
            members[-1].setdefault('alias', []).append(r)
            continue
        r['alias'] = []
        members.append(r)
    for i, r in enumerate(members):
        hi = members[i + 1]['off'] if i + 1 < len(members) else SIZE
        r['extent'] = max(hi - r['off'], CSIZE.get(r['ty'], 2))

    def decl_elem(r):
        """member element C type for record r."""
        t = r['ty']
        if t not in CSIZE or r['ptr']:
            t = 'uint8'
        return t

    hmembers, cmembers = [], []
    pos = 0
    gapi = 0
    for r in members:
        off, ext = r['off'], r['extent']
        if off > pos:
            gap = off - pos
            hmembers.append('    uint8 f19_engap_%X[%d];   /* gap */'
                            % (pos, gap) + '      /* 0x%05X */' % pos)
            cmembers.append((pos, gap, None))
            pos = off
        el = decl_elem(r)
        esz = CSIZE[el]
        cnt = ext // esz if ext % esz == 0 and ext >= esz else None
        if cnt is None:
            el = 'uint8'
            cnt = ext
        r['el'] = el
        r['cnt'] = cnt
        if cnt == 1:
            hmembers.append('    %s m_%s;' % (el, r['name'])
                            + '                      /* 0x%05X */' % off)
        else:
            hmembers.append('    %s m_%s[%d];' % (el, r['name'], cnt)
                            + '                /* 0x%05X */' % off)
        cmembers.append((off, ext, r))
        pos = off + ext
    if pos < SIZE:
        hmembers.append('    uint8 f19_engap_%X[%d];   /* tail */'
                        % (pos, SIZE - pos) + '    /* 0x%05X */' % pos)
        cmembers.append((pos, SIZE - pos, None))

    # ---- defines ---------------------------------------------------------
    defines = []
    for r in members:
        n = r['name']
        t = r['ty'] if r['ty'] in CSIZE else 'uint8'
        ts = r.get('tstr', t) or t        # full type incl. struct tags
        dims = [int(x) if x else 0 for x in
                re.findall(r'\[(\d*)\]', r['dims'])]
        if r['ptr']:
            continue                     # native pointer alias handled by hand
        if dims:
            if len(dims) >= 2 and dims[-1]:
                row = dims[-1]
                defines.append('#define %s ((%s(*)[%d])f19_enSpace.m_%s)'
                               % (n, ts, row, n))
            else:
                defines.append('#define %s ((%s*)f19_enSpace.m_%s)'
                               % (n, ts, n))
        elif r['cnt'] == 1:
            defines.append('#define %s (*(%s *)&f19_enSpace.m_%s)'
                           % (n, t, n))
        else:
            # scalar-named but stored as array (table cells)
            defines.append('#define %s f19_enSpace.m_%s' % (n, n))
        for a in r.get('alias', []):
            at = a.get('tstr') or (a['ty'] if a['ty'] in CSIZE else 'uint8')
            d0 = a['off'] - r['off']
            defines.append('#define %s (*(%s *)((uint8 *)&f19_enSpace.m_%s + %d))'
                           % (a['name'], at, n, d0))

    # ---- initializer: per-member byte slices of the image ----------------
    def slice_init(off, ext):
        bs = img[off:off + ext] if off < len(img) else []
        bs += [0] * (ext - len(bs))
        return bs

    cbody = []
    for off, ext, r in cmembers:
        bs = slice_init(off, ext)
        if all(b == 0 for b in bs):
            continue                       # zero-init skips the field
        if r is None or r['el'] == 'uint8' or True:
            # byte-level init for exactness
            pass
        el = r['el'] if r else 'uint8'
        esz = CSIZE[el]
        vals = []
        for i in range(0, ext, esz):
            if esz == 1:
                vals.append('0x%02X' % bs[i])
            elif esz == 2:
                vals.append('0x%04X' % (bs[i] | bs[i + 1] << 8))
            else:
                vals.append('0x%08X' % (bs[i] | bs[i + 1] << 8
                                        | bs[i + 2] << 16 | bs[i + 3] << 24))
        nm = ('f19_engap_%X' % off) if r is None else ('m_' + r['name'])
        # wrap at ~16 values/line
        lines = []
        for i in range(0, len(vals), 16):
            lines.append(','.join(vals[i:i + 16]))
        cbody.append('    .%s = {%s},' % (nm, ',\n'.join(lines)))

    hdr = '''/* generated by tools/f19_enseg.py — END.EXE data segment as named
 * globals (the debrief/debrief-map world). All dseg cells are members of
 * one packed struct at their exact DOS offsets, so indexed and overlapping
 * accesses see contiguous DOS-ordered storage.  World 2 in f19seg.c. */
#ifndef F19ENVARS_H
#define F19ENVARS_H
#include "inttype.h"
#include "f19seg.h"

#pragma pack(push, 1)
struct F19ENData {
%s
};
#pragma pack(pop)

extern struct F19ENData f19_enSpace;
extern const struct F19ENData f19_enInit;
void f19_enVarsReset(void);

%s
#endif /* F19ENVARS_H */
''' % ('\n'.join(hmembers), '\n'.join(defines))

    src = '''/* generated by tools/f19_enseg.py — per-member initializers for the
 * END.EXE dseg (sliced from the byte-exact skeleton image). */
#include "f19envars.h"

const struct F19ENData f19_enInit = {
%s
};

struct F19ENData f19_enSpace;

void f19_enVarsReset(void) {
    f19_enSpace = f19_enInit;
}
''' % ('\n'.join(cbody))

    open(SRC + '/f19envars.h', 'w').write(hdr)
    open(SRC + '/f19envars.c', 'w').write(src)
    print('wrote f19envars.h/.c: %d members, %d defines, %d init bodies'
          % (len(members), len(defines), len(cbody)))
    if any(off == 0x9EDC for off, _, r in cmembers):
        pass


if __name__ == '__main__':
    main()
