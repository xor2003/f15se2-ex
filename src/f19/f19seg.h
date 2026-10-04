/* F-19 segment-handle allocator.
 *
 * The DOS code stores "segment" values in int16 globals and passes
 * (offset, segment) pairs to the resource loader. Natively each segment
 * is a small handle into a block table; f19_segPtr resolves it.
 *
 * The flat f19_dseg blob is gone: every DOS dseg cell is a named object
 * (see f19stvars/f19egvars). DOS offsets remain resolvable through
 * per-world object tables because 16-bit offset tables and {off,seg}
 * far-pointer cells are part of the original data model.
 */
#ifndef F19_SEG_H
#define F19_SEG_H

#include "inttype.h"

int16  f19_allocSeg(uint16 paras);   /* paragraphs -> handle (>=0x10) */
int16  f19_freeSeg(int16 seg);
void  *f19_segPtr(int16 seg);        /* handle -> native block, NULL-safe */
/* resolve a (off,seg) pair to a native pointer */
#define F19_FP(seg, off) ((char *)f19_segResolve((uint16)(off), (uint16)(seg)))

/* dseg objects: base+size+their original DOS offset. One table per world
 * (START / EGAME); f19_segUseWorld selects which one dsegAt/farOf use. */
struct F19SegObj {
    void   *base;
    uint16  size;
    uint16  off;
};

void  f19_segUseWorld(int world);    /* 0 = START space, 1 = EGAME space */
void *f19_dsegAt(uint32 off);        /* DOS dseg offset -> object address */
uint16 f19_dsegOff(const void *p);   /* object address -> DOS dseg offset */
void *f19_segResolve(uint16 off, uint16 seg); /* (off,seg) -> native ptr */

/* --- dseg-resident far pointer cells ------------------------------------
 * A DOS far pointer lives in dseg as a 4-byte {off16, seg16} cell.  Natively
 * the seg half is a 16-bit handle: 0 = dseg world objects, 1 = f19_commBase,
 * >=0x10 = an allocated block.  Cells stay 4 bytes so the dense pointer
 * tables at dseg 0x7EA-0x9F2 keep their original layout. */
void *f19_farAt(uint16 celloff);             /* resolve cell -> native ptr */
void  f19_setFar(uint16 celloff, void *p);   /* store native ptr into cell */
uint32 f19_farOf(const void *p);             /* native ptr -> {off,handle} */

#endif /* F19_SEG_H */
