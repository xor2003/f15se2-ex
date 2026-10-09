#!/usr/bin/env python3
"""Mechanical src_end -> src/f19 transform for the F-19 END module.

Per file: swap includes for f19en.h, strip data-extern decls (now defines
in f19envars.h), strip struct/type blocks that moved to f19en.h, and strip
non-extern dseg-global definitions (generated as members).  Function externs
are kept (harmless decls); offset-pointer sites are left for hand review.
"""
import re, sys, glob, os

SRC = '/home/xor/games/f19ru/F19/src_end'
DST = '/home/xor/games/f15se2-ex/src/f19'

# struct/typedef/union blocks hoisted into f19en.h — delete these spans
STRUCT_NAMES = [
    'MenuItem', 'TargetBlock', 'EvtItem', 'BlinkSprite', 'FlightLogRec',
    'AnimRec', 'ChanRec', 'MapRect', 'PlaneObjEnd', 'WorldObjEnd',
    'PlaneNameEnd', 'SamNameEnd', 'UnitInfo', 'OrdType',
    'CommDataEnd', 'PilotRecEnd', 'PilotRecMain', 'FarWords',
]


def strip_structs(txt):
    """Remove typedef/struct/union blocks whose tag is in STRUCT_NAMES."""
    out = []
    i = 0
    lines = txt.split('\n')
    while i < len(lines):
        l = lines[i]
        s = l.strip()
        # find the decl end for a block starting here
        def block_end(j):
            depth = 0
            started = False
            while j < len(lines):
                depth += lines[j].count('{') - lines[j].count('}')
                if '{' in lines[j]:
                    started = True
                j += 1
                if started and depth <= 0:
                    break
            return j          # index past the closing '}' line
        is_decl = (s.startswith('typedef ') or s.startswith('struct ')
                   or s.startswith('union '))
        if is_decl and '{' in s:
            j = block_end(i)
            span = '\n'.join(lines[i:j + 1])
            tag = None
            for t in STRUCT_NAMES:
                if re.search(r'\b%s\b' % t, span):
                    tag = t
                    break
            if tag is not None:
                i = j          # drop whole block incl. '};' line
                continue
        out.append(l)
        i += 1
    return '\n'.join(out)


def strip_data_externs(txt):
    """Remove extern decls without parens (data cells -> generated defines)."""
    out = []
    for l in txt.split('\n'):
        s = l.strip()
        if s.startswith('extern ') and '(' not in s and ';' in s:
            # keep a one-line comment remnant marker? just drop
            continue
        out.append(l)
    return '\n'.join(out)


def strip_global_defs(txt, names):
    """Remove non-extern global defs of generated members (multi-declarator)."""
    out = []
    for l in txt.split('\n'):
        s = l.strip()
        if s.startswith('extern ') or '(' in s or not s:
            out.append(l)
            continue
        m = re.match(r'(u?int(?:8|16|32)|char|struct \w+|void)\s*(far\s*)?(\*\s*)?(.*);', s)
        if m and m.group(4):
            rest = m.group(4)
            decls = [d.split('=')[0].strip() for d in rest.split(',')]
            if decls and all(re.sub(r'\[.*', '', d).strip() in names
                             for d in decls):
                continue
        out.append(l)
    return '\n'.join(out)


def transform(fname, extra_globals):
    txt = open(os.path.join(SRC, fname)).read()
    txt = re.sub(r'#include "(inttype|pointers)\.h"\n', '#include "f19en.h"\n',
                 txt, count=1)
    txt = re.sub(r'#include "(inttype|pointers)\.h"\n', '', txt)
    txt = re.sub(r'#define FP_OFF\(p\).*\n|#define FP_SEG\(p\).*\n', '', txt)
    txt = re.sub(r'#pragma pack\([^)]*\)\n?', '', txt)
    txt = strip_structs(txt)
    txt = strip_data_externs(txt)
    txt = strip_global_defs(txt, extra_globals)
    return txt


# non-extern defs per file that became generated members
GLOBALS = {
    'enbrief.c': ['mapViewX1', 'mapViewX2', 'mapViewY1', 'mapViewY2',
                  'clipMaxX', 'clipMaxY', 'lineX1', 'lineY1', 'lineX2',
                  'lineY2', 'nightMission', 'cursorX', 'cursorY',
                  'colorAnimEnabled', 'selectedMenuItem', 'inputChanged',
                  'enterPressed', 'joyRepeatFlag', 'colorAnimIdx',
                  'timerCounter2', 'timerCounter3', 'animDone',
                  'spriteToggle', 'joyAxisX', 'joyAxisY', 'quitFlag',
                  'colorTablePtr', 'targetBlock'],
    'enmain.c': ['initResultFlag', 'timerHandlerInstalled'],
    'enworld.c': ['worldBufPtr', 'worldBufHandle', 'worldDataReady',
                  'worldStrings', 'worldWaypointCount', 'worldObjectCount',
                  'worldRouteCount', 'worldRouteTable', 'worldObjects',
                  'worldSamCount', 'worldSamTable', 'unitTypeTable',
                  'worldUnitFlags', 'worldStringBuf', 'gridFlags',
                  'worldGridSize', 'worldMiscHeader', 'weaponDataBlock',
                  'targetBlockWd', 'flightDataBuf'],
    'enfile.c': ['picStagePos'],
}

MAP = {
    'enmain.c': 'enmain.c',
    'enworld.c': 'enworld.c',
    'enfile.c': 'enfile2.c',     # f19/enfile.c exists (EGAME-era)
    'enaward.c': 'enaward.c',
    'enstr.c': 'enstr.c',
    'drawstr.c': 'endrawstr.c',  # f19/drawstr.c exists (START-era)
    'stalloc.c': 'enalloc.c',
    'enbrief.c': 'enbrief.c',
    'enbrief2.c': 'enbrief2.c',
    'endevt.c': 'endevt.c',
}

for src, dst in MAP.items():
    out = transform(src, GLOBALS.get(src, []))
    out = ('/* ported from f19ru src_end/%s — see that file for seg000 offsets */\n'
           % src) + out
    open(os.path.join(DST, dst), 'w').write(out)
    print('%s -> %s (%d lines)' % (src, dst, out.count('\n')))
