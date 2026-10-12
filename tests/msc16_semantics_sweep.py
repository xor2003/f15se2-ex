#!/usr/bin/env python3
"""Semantic sweep: 16-bit MSC vs 64-bit GCC compare hazards via clang AST.

A: int16 cmp uint16 operand   (MSC promotes both to u16 -> unsigned cmp)
B: int16 cmp literal 0x8000-0xFFFF (literal is positive int under GCC)
Explicit C-style casts are honored (cast target = operand type).
"""
import json, re, subprocess, sys, os, shlex

CC = sys.argv[1] if len(sys.argv) > 1 else 'build/compile_commands.json'
CMPS = {'<', '>', '<=', '>=', '==', '!='}
SKIP_PREFIX = ('-o', '-MF', '-MT', '-MQ', '-MJ', '-include', '-isystem',
               '-iquote', '-idirafter', '-iprefix', '-iwithprefix')
DROP = ('-c', '-MD', '-MMD', '-MP', '-w', '-fsyntax-only')

def sem_node(node):
    """Unwrap implicit casts / parens only; explicit casts define semantics."""
    while node is not None and node.get('kind') in (
            'ImplicitCastExpr', 'ParenExpr'):
        inner = node.get('inner') or []
        node = inner[0] if inner else None
    return node

def norm(qt):
    return re.sub(r'\bconst\b|\bvolatile\b', '', qt).strip()

def off(node):
    b = (node.get('range') or {}).get('begin') or {}
    return b.get('expansionOffset', b.get('spellingOffset',
             b.get('offset', 0)))

def walk(node, src, fname, out):
    if isinstance(node, dict):
        if node.get('kind') == 'BinaryOperator' and node.get('opcode') in CMPS:
            inner = node.get('inner') or []
            if len(inner) >= 2:
                na, nb = sem_node(inner[0]), sem_node(inner[1])
                ta = norm((na.get('type') or {}).get('qualType', '')) if na else ''
                tb = norm((nb.get('type') or {}).get('qualType', '')) if nb else ''
                s16 = lambda t: t in ('int16', 'short')
                u16 = lambda t: t in ('uint16', 'unsigned short')
                litv = None
                for n in (na, nb):
                    if n and n.get('kind') == 'IntegerLiteral':
                        try:
                            litv = int(str(n.get('value', '0')), 0)
                        except ValueError:
                            pass
                hazard = None
                if (s16(ta) and u16(tb)) or (u16(ta) and s16(tb)):
                    hazard = f'int16-vs-uint16 {node["opcode"]}'
                elif litv is not None and 0x8000 <= litv <= 0xFFFF \
                        and (s16(ta) or s16(tb)):
                    hazard = f'int16-vs-lit({hex(litv)}) {node["opcode"]}'
                if hazard:
                    o = off(node)
                    line = src.count('\n', 0, o) + 1
                    text = src.splitlines()[line-1].strip()[:120]
                    out.append(f'{os.path.basename(fname)}:{line}: {hazard}: '
                               f'{ta} {node["opcode"]} {tb} :: {text}')
        for v in node.values():
            walk(v, src, fname, out)
    elif isinstance(node, list):
        for v in node:
            walk(v, src, fname, out)

cmds = json.load(open(CC))
out = []
dec = json.JSONDecoder()
for e in cmds:
    f = e['file']
    if '/src/f19/' not in f or not f.endswith('.c'):
        continue
    argv = e.get('arguments') or shlex.split(e['command'])
    args, skip = [], False
    for a in argv[1:]:
        if skip:
            skip = False
            continue
        if a in SKIP_PREFIX:
            skip = True
            continue
        if a in DROP or a.endswith('.o') or a[:3] in ('-MF', '-MT', '-o='):
            continue
        if a.startswith('-o') and len(a) > 2:
            continue
        args.append(a)
    cmd = ['clang', '-x', 'c', '-std=gnu89', '-fsyntax-only',
           '-Xclang', '-ast-dump=json', '-w'] + args
    try:
        p = subprocess.run(cmd, cwd=e['directory'], capture_output=True,
                           timeout=120)
        if p.returncode != 0:
            print(f'# {os.path.basename(f)}: clang rc={p.returncode} '
                  f'{p.stderr[:200].decode(errors="replace")}', file=sys.stderr)
            continue
        ast, _ = dec.raw_decode(p.stdout.decode(errors='replace'))
        src = open(f, errors='replace').read()
        walk(ast, src, f, out)
    except Exception as ex:
        print(f'# {os.path.basename(f)}: {ex}', file=sys.stderr)
seen = set()
for h in out:
    if h not in seen:
        seen.add(h)
        print(h)
print(f'-- {len(seen)} hazards --')
