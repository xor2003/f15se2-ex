/* F-19 segment-handle allocator.
 *
 * The DOS code stores "segment" values in int16 globals and passes
 * (offset, segment) pairs to the resource loader. Natively each segment
 * is a small handle into a block table; f19_segPtr resolves it.
 */
#ifndef F19_SEG_H
#define F19_SEG_H

#include "inttype.h"

int16  f19_allocSeg(uint16 paras);   /* paragraphs -> handle (>=0x10) */
int16  f19_freeSeg(int16 seg);
void  *f19_segPtr(int16 seg);        /* handle -> native block, NULL-safe */
/* resolve a (off,seg) pair to a native pointer */
#define F19_FP(seg, off) ((char *)f19_segPtr(seg) + (uint16)(off))

extern uint8 f19_dseg[];

/* --- dseg-resident far pointer cells ------------------------------------
 * A DOS far pointer lives in dseg as a 4-byte {off16, seg16} cell.  Natively
 * the seg half is a 16-bit handle: 0 = f19_dseg itself (near-ptr equivalent),
 * 1 = f19_commBase, >=0x10 = an allocated block.  Cells stay 4 bytes so the
 * dense pointer tables at dseg 0x7EA-0x9F2 keep their original layout. */
void *f19_farAt(uint16 celloff);             /* resolve cell -> native ptr */
void  f19_setFar(uint16 celloff, void *p);   /* store native ptr into cell */
uint32 f19_farOf(const void *p);             /* native ptr -> {off,handle} */

#endif /* F19_SEG_H */
