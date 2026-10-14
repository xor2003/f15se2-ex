/* F-19 Stealth Fighter — merged module common header.
 *
 * The sources in this directory are native adaptations of the verified
 * f19ru reconstruction C (src_start). The DOS seg:off model is flattened:
 *   - `far`/`near` are no-ops (compat64/dos.h)
 *   - "segment" values returned by allocBuffer are int16 handles resolved
 *     through f19seg.c; resFileReadBlock(path, off, seg) reads into the
 *     resolved block.
 *   - the comm/game-data blocks are native buffers; the dseg cells that
 *     held their far pointers appear as the commData/gameData globals
 *     (word_2D066 / word_2991C were the DOS cells' flat names).
 *   - BIOS low-mem pointers (word_2B942 -> 0:0x4F2, word_298EC -> 0:0x4F4)
 *     become pointers into a small shared-flag area.
 */
#ifndef F19_MODULE_H
#define F19_MODULE_H

#include <dos.h>            /* compat64: far/near + REGS */
#include "inttype.h"
#include "shared/common.h"  /* mystrcpy, my_ltoa, file io decls */

extern uint8 timerCounter; /* shared/timer.c — 60 Hz tick byte (stdata.c) */

SDL_IOStream *f19_fileIo(int16 h);       /* f19file.c */
void *f19_pagePixels(int16 n);           /* f19ovl.c: page idx -> pixels
                                            (0 = app back buffer, >0 = seg) */
int16 f19_pageSegHandle(int16 n);        /* f19ovl.c: page idx -> seg handle */
void far f19_blitSpriteParams(void *params, int opaque); /* f19ovl.c: SpriteParams* form */
#define gfx_copyRect f19_gfx_copyRect                    /* F-19 pages are seg-backed, not app pages */
#include "stcode.h"          /* mystrcat */
#include "strand.h"          /* (f19 uses its own LCG: f19_rand/f19_randMul) */
#include "stgen.h"           /* mystrlen */
#include "f19seg.h"
#include "f19ovl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* dseg-cell aliases for the comm/game pointer slots */
#define word_2D066 f19_commData
#define word_2991C f19_gameData
#define commData f19_commData
#define gameData f19_gameData

/* The real objects behind the two cells above. Declared void* here;
 * every user file redeclares them with its own struct view. */

/* the F-19 comm/game shared block (commData at +0, gameData at +0x120E) */
extern uint8 f19_commBase[];

/* DOS dseg globals are real objects now (f19stvars.h / f19egvars.h);
 * DOS offsets resolve via f19_dsegAt()/f19_dsegOff() in f19seg.c. */

/* shared low-mem flag cells (0:4F2 reload-request, 0:4F4 gfx vector) */
extern int16 f19_lowFlags[];

/* CRT stdio compat for the reconstructed sources: FILE* calls route
 * through the app's SDL_IOStream layer so game files resolve via the
 * bundledRoot/convertedDir search path.  Must come after the sources'
 * own #include <stdio.h>, and only affects tokens that follow it. */
#include "shared/common.h"
#define FILE        SDL_IOStream
#define fopen(name, mode)   (((mode)[0] == 'w' || (mode)[0] == 'a') ? \
                             createFile((name), 0) : openFile((name), 0))
#define fread(p,s,c,f)      fileRead((p),(s),(c),(f))
#define fwrite(p,s,c,f)     fileWrite((p),(s),(c),(f))
#define fclose(f)           (fileClose(f), 0)
#define fseek(f,o,w)        ((int)SDL_SeekIO((f),(o),SDL_IO_SEEK_SET) < 0)

/* entry points wired through GameDesc */
void f19_dsegInit(void);
int f19_start_main(void);
int f19_egame_main(void);
int f19_end_main(void);

#endif /* F19_MODULE_H */
