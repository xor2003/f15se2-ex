/* F-19 overlay-call shim decls.
 *
 * In DOS, ovlCall_XXXX were far calls into the dseg trampoline table at
 * dseg:0xAFA+slot*5, patched by sub_14107 from the loaded driver's slot
 * table. Decoded against MGRAPHIC.EXE's header (baseslot 0, count 0x54,
 * table at image+0x24 of SEG001-relative handler offsets):
 *
 *   slot = (XXXX - 0xAFA)/5
 *
 *   b4f=11 gfx_blitSprite      c2b=3d gfx_setFadeSteps
 *   b6d=17 gfx_getBufSize      c49=43 gfx_getConst1
 *   b9f=21 gfx_setColor        c4e=44 gfx_setDac
 *   ba9=23 nop                 c53=45 gfx_waitRetrace
 *   bc7=29 gfx_switchColor     c58=46 gfx_flipPage
 *   bea=30 gfx_blitToCurrent   c71=4b gfx_storeBufPtr
 *   bef=31 gfx_getAuxBufSize   c8a=50 gfx_commitPage
 *   bf4=32 gfx_getFreeMem      cbc=5a misc_checkKeyBuf
 *   c49=43 getConst1           cc1=5b misc_getKey
 *                              ccb=5d misc_readJoystick
 *                              cd0=5e misc_clearKeyFlags
 *   cee=64 audio slot0 (setup) cf3=65 audio slot1  cfd=67 audio slot3
 *
 * ovlF43_* / ovlFee_* are far calls into the other resident overlays
 * (misc/config + DS driver selector) rather than the dseg table.
 */
#ifndef F19_OVL_H
#define F19_OVL_H

#include "inttype.h"

/* graphics-driver slots */
int16 far ovlCall_b4f(int16 spr);                   /* gfx_blitSprite */
int16 far ovlCall_b6d(void);                        /* gfx_getBufSize   */
void  far ovlCall_b9f(int16 color);                 /* gfx_setColor     */
void  far ovlCall_ba9(void);                        /* nop23            */
void  far ovlCall_bc7(int16 *pg, int16 x1, int16 y1, int16 x2, int16 y2,
                      int16 oldC, int16 newC);      /* gfx_switchColor  */
void  far ovlCall_bea(int16 seg);                   /* gfx_blitToCurrent*/
int16 far ovlCall_bef(void);                        /* gfx_getAuxBufSize*/
int16 far ovlCall_bf4(void);                        /* gfx_getFreeMem   */
void  far ovlCall_c2b(int16 steps);                 /* gfx_setFadeSteps */
int16 far ovlCall_c49(void);                        /* gfx_getConst1    */
void  far ovlCall_c4e(int16 pal);                   /* gfx_setDac       */
int16 far ovlCall_c53(void);                        /* gfx_waitRetrace  */
void  far ovlCall_c58(void);                        /* gfx_flipPage     */
void  far ovlCall_c71(int16 ptr, int16 n);          /* gfx_storeBufPtr  */
int16 far ovlCall_c7b(void);                        /* gfx_getVal2 -> 1 */
void  far ovlCall_c8a(void);                        /* gfx_commitPage   */

/* misc-driver slots */
int16 far ovlCall_cbc(void);                        /* misc_checkKeyBuf */
int16 far ovlCall_cc1(void);                        /* misc_getKey      */
int16 far ovlCall_ccb(int16 axis);                  /* misc_readJoystick*/
void  far ovlCall_cd0(void);                        /* misc_clearKeyFlags*/

/* audio-driver slots */
void  far ovlCall_cee(void);                        /* audio slot0      */
void  far ovlCall_cf3(void);                        /* audio slot1      */
void  far ovlCall_cfd(void);                        /* audio slot3      */

/* page/misc slots defined natively in f19ovl.c */
void  far gfx_setMode13(int16 mono);
void  far gfx_setPageN(uint16 n);
int16 far gfx_allocPage(int16 pageNum);
void  far gfx_blitToCurrent(int16 seg);
void  far gfx_storeBufPtr(int16 p, int16 n);
void  far gfx_drawString(int16 o, char *s);
int16 far misc_jump_5a_keybuf(void);
int16 far misc_jump_5b_getkey(void);
int16 far misc_jump_5d_readJoy(int16 n);
void  far misc_jump_5e_clearKeyFlags(void);

/* cross-overlay far calls (0f43 = config/misc overlay, 0fee = DS selector) */
int16 far ovlF43_a(int16 v);
void  far ovlF43_10d(int16 v);
void  far ovlFee_23(void);
void  far ovlFee_fd(int8 far *p);

#endif /* F19_OVL_H */
