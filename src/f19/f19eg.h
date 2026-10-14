#ifndef F19EG_H
#define F19EG_H

#include "f19.h"
#include <stdlib.h>
#include <string.h>
#include "f19egsyms.h"
#include "f19egpfx.h"

/* little-endian 16-bit cell read through the dseg object resolver */
#define EG_W(off) (*(uint16 *)f19_dsegAt((off)))

#include "f19egglobals.h"

/* F-19 EGAME.EXE module. The game-data segment lives in f19egvars.c as
 * per-member initializers on f19_egSpace (RU layout — the src_en ports
 * index dseg objects by RU-land dseg offsets via f19_dsegAt). START and
 * EGAME have separate dseg object sets; which one f19_dsegAt resolves
 * depends on f19_segUseWorld(). commBase is shared between START and
 * EGAME exactly like the DOS shared-data block. */

void f19_egDsegLoad(void);              /* reset EGAME-side objects */

/* ---- flat-model pointer ops (pointers.h equivalents) ----
 * Segments are f19seg handles: 0 = dseg objects, 1 = f19_commBase,
 * >=0x10 = blocks. FP_OFF/FP_SEG are read-only here; the few lvalue uses in
 * egmain were rewritten to assignments through F19_FP. */
#define MK_FP(seg, off)   ((void *)f19_segResolve((uint16)(off), (uint16)(seg)))
#define FP_OFF(p)         ((uint16)f19_farOf(p))
#define FP_SEG(p)         ((uint16)(f19_farOf(p) >> 16))
#define PTR_OFF(p)        FP_OFF(p)
#define segread(s)        ((s)->ds = 0, (s)->es = 0, (s)->ss = 0, (s)->cs = 0)
#define _movedata         f19_movedata
#define movedata          f19_movedata
#ifndef pascal
#define pascal
#define PASCAL
#define _pascal
#endif
void f19_movedata(uint16 sseg, uint16 soff, uint16 dseg_, uint16 doff, uint16 n);

/* EGAME-side service shims (implemented in f19ovl.c) */
int f19eg_getTimeOfDay(void);       /* int 1Ah AH=0 — BIOS tick low word */

/* runtime segment handles + fake BIOS area */
extern uint8 f19_bda[0x500];
extern int16 f19eg_seg004;      /* seg004: the 3D/world-data block */

/* -> g_replayLog.vertexX in the app's shared driver (vertex-X table) */
extern int16 *const f19eg_vertexX;

#endif /* F19EG_H */
