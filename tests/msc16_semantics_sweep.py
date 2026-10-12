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

ARITH = {'*', '<<', '>>'}

def case_vals(switch_node):
    """All integer case values under a SwitchStmt."""
    vals = []
    def rec(n):
        if isinstance(n, dict):
            if n.get('kind') == 'CaseStmt':
                for c in n.get('inner') or []:
                    c = sem_node(c)
                    if c and c.get('kind') == 'IntegerLiteral':
                        try:
                            vals.append(int(str(c.get('value', '0')), 0))
                        except ValueError:
                            pass
            for v in n.values():
                rec(v)
        elif isinstance(n, list):
            for v in n:
                rec(v)
    rec(switch_node)
    return vals

# Expr nodes that pass semantics through unchanged — the effective consumer of
# an arith op is its nearest ancestor NOT in this set.
TRANSPARENT = {'ImplicitCastExpr', 'ParenExpr', 'ConstantExpr', 'UnaryOperator',
               'MaterializeTemporaryExpr', 'ConditionalOperator'}

def consumer_of(node):
    return (node.get('kind', ''), node.get('opcode', ''),
            norm((node.get('type') or {}).get('qualType', '')))

def arith_use(ck, cop, ct):
    """Classify the effective consumer of an int16 arith result.
    'narrow'  — 16-bit wrap is reproduced (cast/store to int16, mask, index).
    'wide'    — full int32 value changes semantics vs MSC (compare, wide store,
                truthiness). Flag these.
    'safe'    — consumers where the wider result is the correct one
                (pointer/index offsets, call args — original relied on no wrap).
    'chain'   — nested arith; the outer op's consumer decides."""
    if ck in ('ArraySubscriptExpr', 'CallExpr'):
        return 'safe'
    if ck == 'CStyleCastExpr' or ck == 'VarDecl':
        return 'narrow' if ct in ('int16', 'uint16', 'short', 'unsigned short',
                                  'int8', 'uint8', 'char', 'signed char',
                                  'unsigned char') else 'wide'
    if '*' in ct or 'far' in ct:
        return 'safe'  # pointer arithmetic: offset math, wrap was never intended
    if ck == 'BinaryOperator':
        if cop in CMPS:
            return 'wide'
        if cop in ('=',) or ck == 'CompoundAssignOperator':
            # the op's own qualType is the LHS type — 16-bit store narrows to
            # the same wrapped value MSC produced
            return 'narrow' if ct in ('int16', 'uint16', 'short',
                                      'unsigned short') else 'wide'
        if cop in ARITH or cop in ('+', '-', '&', '|', '^', '%', '/'):
            return 'chain'
        return 'wide'  # '&&', '||', ','
    if ck == 'CompoundAssignOperator':
        return 'narrow' if ct in ('int16', 'uint16', 'short',
                                  'unsigned short') else 'wide'
    if ck == 'ReturnStmt':
        return 'safe'  # implicit narrow on int16-returning fns; wide returns rare
    if ck in ('IfStmt', 'WhileStmt', 'ForStmt', 'ConditionalOperator',
              'DoStmt', 'SwitchStmt', 'UnaryOperator'):
        return 'wide'  # truthiness: wrapped-zero vs nonzero differs
    if ck in ('VarDecl', 'InitListExpr'):
        return 'wide'
    if ck in ('ParmVarDecl', 'CXXStdInitializerListExpr', 'FunctionDecl',
              'CompoundStmt', 'DeclStmt', 'FileScopeAsmDecl',
              'TranslationUnitDecl', ''):
        return 'safe'  # top-level/initializers: constants, no runtime wrap
    return 'wide'    # unknown consumer: assume the result is used wide

def walk(node, src, fname, out, consumer=None):
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
        # switch(int16) with high-bit case labels never match under GCC
        if node.get('kind') == 'SwitchStmt':
            inner = node.get('inner') or []
            if inner:
                cond = sem_node(inner[0])
                ct = norm((cond.get('type') or {}).get('qualType', '')) if cond else ''
                if ct in ('int16', 'short'):
                    for v in case_vals(node):
                        if 0x8000 <= v <= 0xFFFF:
                            o = off(node)
                            line = src.count('\n', 0, o) + 1
                            text = src.splitlines()[line-1].strip()[:120]
                            out.append(f'{os.path.basename(fname)}:{line}: '
                                       f'int16-switch-case({hex(v)}) :: {text}')
        # int16 arithmetic: MSC wraps at 16 bits, GCC keeps int32 —
        # flag unless the result is demonstrably narrowed again.
        if node.get('kind') == 'BinaryOperator' and node.get('opcode') in ARITH | {'+', '-'}:
            inner = node.get('inner') or []
            if len(inner) >= 2:
                na, nb = sem_node(inner[0]), sem_node(inner[1])
                ta = norm((na.get('type') or {}).get('qualType', '')) if na else ''
                tb = norm((nb.get('type') or {}).get('qualType', '')) if nb else ''
                s16 = lambda t: t in ('int16', 'short')
                lit = lambda n: n and n.get('kind') == 'IntegerLiteral'
                if (s16(ta) and (s16(tb) or lit(nb))) or \
                   (s16(tb) and lit(na)):
                    ck, cop, ct = consumer_of(consumer) if consumer else ('', '', '')
                    use = arith_use(ck, cop, ct)
                    if use == 'chain':
                        pass  # outer op's consumer decides
                    elif use == 'wide':
                        o = off(node)
                        line = src.count('\n', 0, o) + 1
                        text = src.splitlines()[line-1].strip()[:120]
                        tag = 'CMP' if (ck == 'BinaryOperator' and cop in CMPS) \
                              or ck in ('IfStmt', 'WhileStmt', 'ForStmt',
                                        'DoStmt', 'SwitchStmt') else 'use'
                        out.append(f'{os.path.basename(fname)}:{line}: '
                                   f'int16-arith({node["opcode"]},{tag}) :: {text}')
        child = node if node.get('kind') not in TRANSPARENT else consumer
        for v in node.values():
            walk(v, src, fname, out, child)
    elif isinstance(node, list):
        for v in node:
            walk(v, src, fname, out, consumer)

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
        if a.startswith('-std=') or a == '-std':
            skip = (a == '-std')  # consume the separate arg too if split
            continue
        if a == '-x' or a.startswith('-x='):
            skip = (a == '-x')    # language override: we force -x c below
            continue
        if a.startswith('-o') and len(a) > 2:
            continue
        args.append(a)
    cmd = ['clang', '-x', 'c++', '-std=gnu++14', '-fsyntax-only',
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
