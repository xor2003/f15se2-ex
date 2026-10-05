/*
 * eginstr.c — F-19 EGAME instrument-cluster renderer: sub_22092
 * (drawInstrumentGauges), entered per frame via the sub_22086 trampoline when
 * g_viewMode == 0.  Faithful port of lst/egame.asm seg002:0012-098E — speed
 * and altitude drum tapes, compass strip + heading marker, gauge frame, gun
 * sight, G readout, and the roll-rotated pitch ladder.
 *
 * Cell names are the listing's (f19egsyms.h); the layout scalars it reads
 * (tapeOriginX, tickPitch, pitchClipMax*, ...) are initialised by
 * writeInstrumentLayoutCells / f19eg_setupInstrumentLayoutFar in f19egsvc.c.
 *
 * Slot mapping (EGAME trampoline at dseg 0x1BA + slot*5):
 *   sub_2F02F/34/39/3E/43/48 = the clipped-glyph engine, slots 0x01..0x06
 *       -> gfx_drawGlyphStr(desc, str, slot)
 *   sub_2F061 = slot 0x0b complexRender (tape tick column)
 *   sub_2F084 = slot 0x12 blitCore -> gfx_blitSprite (F-19 offset form)
 *   sub_2F0A7 = slot 0x19 setBlitOffset3 (blitOffset = 0)
 *   sub_2F0B1 = slot 0x1b setBlitOffsetReg (blitOffset = ax)
 *   sub_2F0C5 = slot 0x1f drawLine(ax=x1, bx=y1, cx=x2, dx=y2)
 *   sub_2F0CA = slot 0x20 setDrawColor(ah)
 *   sub_2F0D4 = slot 0x22 nop
 *   sub_2F10B = slot 0x2d getDisplayPage -> draw page index (0 here)
 *   sub_21B3A = clip line vs the 32A03/32A05 bounds, drawing via the shared
 *       g_lineX1..g_lineY2/g_clipMaxX/Y block -> drawClipLineGlobal
 *   sub_11C58 = interpolated sine (256-entry/circle table at dseg 0x3782,
 *       high byte = index, low byte = fraction)
 */
#include "f19eg.h"
#include "f19egvars.h"
#include "inttype.h"
#include "struct.h"
#include "gfx.h"
#include <string.h>

extern void FAR CDECL gfx_drawGlyphStr(int16 *desc, const char *str, int slot);
extern void FAR CDECL gfx_complexRender(int bxArg, int dxArg, int cxArg, int siArg);
extern void FAR CDECL gfx_drawLine(uint16 x1, uint16 y1, uint16 x2, uint16 y2);
extern void FAR CDECL gfx_setDrawColor(uint16 color);
extern void FAR CDECL gfx_setBlitOffset(int offset);
extern void FAR CDECL gfx_nop22(void);              /* slot 0x22 bare RETF */
extern void far gfx_blitSprite(int16 spr);              /* f19ovl.c — dseg-offset form */
extern void far drawClipLineGlobal(void);               /* eghudm.c — shared clip line */

/* --- listing-name aliases for the cells this routine uses --- */

#define digitStrip ((uint8 *)f19_egSpace.m_digitStrip)

/* ladder work areas inside compassTape (buf 0x4340) */
#define LADDER_X  (compassTape + 0xEC)   /* word_3xx vertex X array @0x442C */
#define LADDER_Y  (compassTape + 0x15C)  /* vertex Y array            @0x449C */
#define LADDER_CNT (compassTape + 0x1CC) /* per-column-group counts   @0x450C */
#define LADDER_IDX (compassTape + 0x1E8) /* byte index pairs          @0x4528 */
#define LADDER_LBL (compassTape + 0x20A) /* pitchLabelTable           @0x454A */

/* byte view of a word cell / unaligned-safe 16-bit ops on dseg objects */
#define LOB(v)  (*(uint8 *)&(v))
static int16 ldw(const uint8 *p) { return (int16)((uint16)p[0] | ((uint16)p[1] << 8)); }
static void  stw(uint8 *p, int16 v) { p[0] = (uint8)v; p[1] = (uint8)(v >> 8); }

/* sub_11C58 -> sub_11C64: sine lookup, high byte = index, low = fraction;
 * interpolated table at dseg 0x3984 (NOT sub_11C39's separate 0x3782 table) */
static int16 f19_sin(int16 angle) {
    uint16 a = (uint16)angle;
    int idx = (a >> 8) & 0xff;
    int frac = a & 0xff;
    int32 v0 = sinTable[idx], v1 = sinTable[idx + 1];
    return (int16)(v0 + (int16)(((v1 - v0) * frac + 0x80) >> 8));
}

static void tapeStr(int16 *desc, const uint8 *str, int slot) {
    gfx_drawGlyphStr(desc, (const char *)str, slot);
}
static void drawL(int16 x1, int16 y1, int16 x2, int16 y2) {
    gfx_drawLine((uint16)x1, (uint16)y1, (uint16)x2, (uint16)y2);
}

void far f19eg_drawInstrumentGaugesFar(void) {
    int16 di, si;
    int8 dl, ch, cl;
    int16 bxY;
    int16 disp;

    /* the tapes/compass/frame draw at absolute coords; the pitch ladder under
     * g_pitchBlitOfs — reset so the frame lands where the original put it. */
    gfx_setBlitOffset(0);

    disp = 0;                       /* slot 0x2d getDisplayPage: single page */
    tapeText0[0] = tapeText1[0] = tapeText2[0] = tapeText3[0] = disp;
    sprite0[3] = sprite1[3] = sprite2[3] = sprite3[3] = disp;

    /* ---- speed tape ---------------------------------------------------- */
    {
        uint16 knots = (uint16)g_knots;         /* word_373E8 */
        uint8 al;
        dl = (int8)((knots / 50) - 1);          /* byte_330FB */
        knots = (uint16)((knots % 50) << byte_330C2);   /* scaleShift */
        al = (uint8)((knots / 5) + (uint8)word_330BD);  /* + tapeOriginX */
        LOB(word_32FF6) = al;                   /* tapeText0[5] lo = drawY */
        byte_330FB = (uint8)dl;
        al = (uint8)(al - byte_330BF);          /* - cursorBackShift */
        LOB(word_330F1) = al;                   /* renderX */
        di = (uint8)((uint8)dl << 1);            /* shl dl,1 — byte shift */
        byte_330F4 = 4;                          /* pageCounter */
        for (;;) {
            LOB(word_32FF6) = (uint8)(LOB(word_32FF6) - (uint8)LOB(word_330C0));
            word_32FF4 = word_330C3;            /* desc[4] = speedTickStep */
            if (di >= 0x28) {
                di = 0;
                byte_330F4--;
                word_330F1 -= word_330C0;
                continue;
            }
            stw(speedLabelBuf, ldw(digitStrip + di + 8));
            stw(speedLabelBuf + 2, ldw(digitStrip + di + 0x58));
            tapeStr(tapeText0, speedLabelBuf, 0x01);
            di += 2;
            if (--byte_330F4 == 0) break;
        }
        gfx_complexRender(word_330F1, (int8)byte_330FB, byte_330EA, 0);
    }

    /* tape tick marks, colour 0x0f (frame differs by byte_330EA layout) */
    gfx_setDrawColor(0x0F);
    if (byte_330EA != 0) {
        drawL(0x7A, 0x52, 0x7C, 0x52);
        drawL(0xC4, 0x52, 0xC6, 0x52);
        drawL(0x9F, 0x43, 0x9F, 0x44);
    } else {
        drawL(0x49, 0x38, 0x4C, 0x38);
        drawL(0xF3, 0x38, 0xF6, 0x38);
        drawL(0x9F, 0x14, 0x9F, 0x16);
    }

    gfx_nop22();                                 /* slot 0x22 — call kept */

    /* ---- altitude tape ------------------------------------------------- */
    {
        uint16 alt = (uint16)g_altitude;        /* word_33578 */
        int16 thousands = (int16)(alt / 1000) - 1;
        uint16 rem = (uint16)(alt % 1000);
        uint16 pix;
        word_330EB = (int16)rem;                /* altRemainder */
        pix = (uint16)(((uint16)(rem << byte_330C2)) / 100);
        di = thousands * 2;
        byte_330F4 = 4;
        if (di < 0) {
            /* altitude < 1000 ft: hundreds tape */
            uint8 al;
            dl = (int8)((word_330EB / 100) - 1);
            rem = (uint16)(((uint16)word_330EB % 100) << byte_330C2);
            al = (uint8)((rem / 10) + (uint8)word_330BD);
            LOB(word_32FF6) = al;
            byte_330FB = (uint8)dl;
            LOB(word_330F1) = (uint8)(al - byte_330BF);
            di = (uint8)((uint8)dl << 1);        /* shl dl,1 — byte shift */
            for (;;) {
                LOB(word_32FF6) = (uint8)(LOB(word_32FF6) - (uint8)LOB(word_330C0));
                word_32FF4 = word_330C5;        /* altTickStep */
                if (di >= 0x28) {
                    di = 0;
                    byte_330F4--;
                    word_330F1 -= word_330C0;
                    continue;
                }
                digitStrip[0] = digitStrip[di + 0xA9];
                tapeStr(tapeText0, digitStrip, 0x01);
                di += 2;
                if (--byte_330F4 == 0) goto alt_done;
                if (di >= 0x14) {
                    di = 2;
                    break;                      /* wrap into the thousands loop */
                }
            }
        } else {
            word_32FF6 = (int16)(pix + (uint16)word_330BD);
            byte_330FB = 2;
            LOB(word_330F1) = (uint8)((uint8)(pix + (uint8)word_330BD) - byte_330BF);
        }
        for (;;) {
            LOB(word_32FF6) = (uint8)(LOB(word_32FF6) - (uint8)LOB(word_330C0));
            word_32FF4 = word_330C5;
            if (di == 0) {
                tapeStr(tapeText0, digitStrip + 4, 0x01);
            } else {
                stw(altLabelBuf, ldw(digitStrip + di + 0xA8));
                tapeStr(tapeText0, altLabelBuf, 0x01);
            }
            di += 2;
            if (--byte_330F4 == 0) break;
        }
    alt_done:
        gfx_complexRender(word_330F1, (int8)byte_330FB, byte_330EA, 2);
    }

    /* ---- compass strip -------------------------------------------------- */
    {
        uint16 head = (uint16)(g_ourHead - 0x2000);   /* word_33570 */
        uint8 head_hi = (uint8)(head >> 8);
        uint16 s = (uint16)((head & 0x1F80) << 1);
        uint16 prod = (uint16)((s >> 8) * (uint16)(uint8)LOB(word_330C7));
        uint16 p6 = prod >> 6;
        int16 idx;
        byte_330F3 = (uint8)p6;                  /* marker phase */
        word_330EF = (int16)(uint8)((uint8)byte_330BC - (uint8)p6);
        word_3300A = word_330EF;                 /* tapeText1[4] = drawX */
        word_330ED = (int16)((head_hi >> 2) & 0x38);   /* scrollIdx */
        tapeStr(tapeText1, compassTape + 0x84 + word_330ED, 0x02);

        word_330ED = (word_330ED + 8) & 0x3F;
        word_330EF = word_3300A = word_330EF + word_330C7;
        tapeStr(tapeText1, compassTape + 0x84 + word_330ED, 0x04);

        word_330ED = (word_330ED + 8) & 0x3F;
        word_330EF = word_3300A = word_330EF + word_330C7;
        tapeStr(tapeText1, compassTape + 0x84 + word_330ED, 0x04);

        if ((uint16)word_330EF < (uint16)word_330C9) {
            uint8 *bx;
            word_330ED = (word_330ED + 8) & 0x3F;
            bx = compassTape + 0x84 + word_330ED;
            stw(bx + 4, ldw(bx));
            stw(bx + 6, ldw(bx + 2));
            bx += 4;
            word_330EF = word_3300A = word_330EF + word_330C7;
            tapeStr(tapeText1, bx, 0x03);

            /* heading marker sprite selected by heading modulo */
            idx = (int16)((uint8)byte_330F3 % (uint8)LOB(word_330CB));
            if (g_ourHead & 0x2000) idx += word_330CD;
            if (idx >= word_330CB) idx -= word_330CB;
            di = idx * 2;
            word_33048 = (byte_330EA == 1)
                ? ldw(compassTape + 0xD8 + di)
                : ldw(compassTape + 0xC4 + di);
            gfx_blitSprite(0x41D4);             /* sprite0 */
        }
    }

    /* ---- gauge frame ---------------------------------------------------- */
    if (byte_330EA == 0) {
        drawL(0x2E, 0x6B, 0x2E, 0x0F);
        drawL(0x112, 0x6B, 0x112, 0x0F);
        gfx_setDrawColor(8);
        drawL(0x2D, 0x6B, 0x2D, 0x0F);
        drawL(0x113, 0x6B, 0x113, 0x0F);
        drawL(0x113, 0x0F, 0xE8, 3);
        drawL(0xE8, 3, 0x5A, 3);
        drawL(0x2D, 0x0F, 0x59, 3);
    } else {
        drawL(0x67, 0x6C, 0x67, 0x3F);
        drawL(0xD9, 0x6C, 0xD9, 0x3F);
        gfx_setDrawColor(8);
        drawL(0x66, 0x6C, 0x66, 0x3F);
        drawL(0xDA, 0x6C, 0xDA, 0x3F);
        drawL(0xD9, 0x3F, 0xC4, 0x39);
        drawL(0xC4, 0x39, 0x7C, 0x39);
        drawL(0x7C, 0x39, 0x67, 0x3F);
    }

    gfx_nop22();                                 /* slot 0x22 — call kept */

    /* ---- HUD gun-sight --------------------------------------------------- */
    if (byte_330F5 != 0)
        gfx_blitSprite(0x41F2);                 /* sprite1 */

    /* ---- G-meter readout -------------------------------------------------- */
    word_33036 = word_330E8;                    /* tapeText3[4] = geeReadoutX */
    tapeStr(tapeText3, (uint8 *)geeStrBuf, 0x04);

    /* ---- pitch ladder -----------------------------------------------------
     * Build up to 5 rung vertex groups (X at 0x442C, Y at 0x449C, per-group
     * seg counts at 0x450C, index pairs at 0x4528), rotate by roll, then
     * clip-draw each segment with colour 7. */
    {
        int16 pitch = g_ourPitch;               /* word_33572 */
        uint16 ap = (uint16)(pitch < 0 ? -pitch : pitch);
        uint16 tScale;
        int16 subY;
        ap >>= 6;
        tScale = (uint16)(((uint32)ap * 360u) >> 8);   /* mul 360, >>8 of dx:ax */
        ch = (int8)(tScale / 40);
        subY = (int16)(((uint16)(tScale % 40) * (uint16)(uint8)word_330D7) / 40);
        if (pitch < 0) subY = (uint8)word_330D7 - subY;
        subY = (uint8)((uint8)subY + byte_330D9);      /* + pitchCenterY */
        bxY = (int16)(uint16)(uint8)subY;
        if (pitch < 0) { ch = (int8)-ch; ch = (int8)(ch - 1); }
        word_333B6 = (int16)(ch - 2);           /* tapeCursorX */
        dl = (int8)word_333B6;
        cl = 5;
        di = 0;
        si = 0;
        ch = 0;
        for (;;) {
            int16 d = dl;
            if (d == 9) {                        /* half-rung (right) */
                stw(LADDER_X + di, word_330CF);
                stw(LADDER_X + di + 2, word_330D5);
                stw(LADDER_Y + di, bxY);
                stw(LADDER_Y + di + 2, bxY);
                LADDER_CNT[si] = 1;
                LADDER_CNT[si + 1] = 0;
                stw(LADDER_IDX + si, di);
                di += 2;
                stw(LADDER_IDX + si + 1, di);
                di += 2;
                si += 2;
                bxY -= word_330D7;
                ch += 1;
            } else if (d > 9) {                  /* 3-seg rung, shift -5 */
                stw(LADDER_X + di, word_330CF);
                stw(LADDER_X + di + 2, word_330CF);
                stw(LADDER_X + di + 4, word_330D5);
                stw(LADDER_X + di + 6, word_330D5);
                stw(LADDER_Y + di + 2, bxY);
                stw(LADDER_Y + di + 4, bxY);
                stw(LADDER_Y + di, bxY - 5);
                stw(LADDER_Y + di + 6, bxY - 5);
                LADDER_CNT[si] = 3;
                LADDER_CNT[si + 1] = 0;
                di += 2;
                stw(LADDER_IDX + si, di);
                di += 2;
                stw(LADDER_IDX + si + 1, di);
                di += 4;
                si += 2;
                bxY -= word_330D7;
                ch += 3;
            } else if (d == -9 || d == 0) {      /* full 4-vertex rung */
                stw(LADDER_X + di, word_330CF);
                stw(LADDER_X + di + 2, word_330D1);
                stw(LADDER_X + di + 4, word_330D3);
                stw(LADDER_X + di + 6, word_330D5);
                stw(LADDER_Y + di, bxY);
                stw(LADDER_Y + di + 2, bxY);
                stw(LADDER_Y + di + 4, bxY);
                stw(LADDER_Y + di + 6, bxY);
                LADDER_CNT[si] = 1;
                LADDER_CNT[si + 1] = 1;
                stw(LADDER_IDX + si, di);
                di += 6;
                stw(LADDER_IDX + si + 1, di);
                di += 2;
                si += 2;
                bxY -= word_330D7;
                ch += 2;
            } else if (d < -9) {                 /* 4-seg rung, +5 */
                stw(LADDER_X + di, word_330CF);
                stw(LADDER_X + di + 2, word_330CF);
                stw(LADDER_X + di + 4, word_330D1);
                stw(LADDER_X + di + 6, word_330D3);
                stw(LADDER_X + di + 8, word_330D5);
                stw(LADDER_X + di + 10, word_330D5);
                stw(LADDER_Y + di + 2, bxY);
                stw(LADDER_Y + di + 4, bxY);
                stw(LADDER_Y + di + 6, bxY);
                stw(LADDER_Y + di + 8, bxY);
                stw(LADDER_Y + di, bxY + 5);
                stw(LADDER_Y + di + 10, bxY + 5);
                LADDER_CNT[si] = 2;
                LADDER_CNT[si + 1] = 2;
                di += 2;
                stw(LADDER_IDX + si, di);
                di += 6;
                stw(LADDER_IDX + si + 1, di);
                di += 4;
                si += 2;
                bxY -= word_330D7;
                ch += 4;
            } else if (d > 0) {                  /* 1..8: 3-seg rung, +5 */
                stw(LADDER_X + di, word_330CF);
                stw(LADDER_X + di + 2, word_330CF);
                stw(LADDER_X + di + 4, word_330D5);
                stw(LADDER_X + di + 6, word_330D5);
                stw(LADDER_Y + di + 2, bxY);
                stw(LADDER_Y + di + 4, bxY);
                stw(LADDER_Y + di, bxY + 5);
                stw(LADDER_Y + di + 6, bxY + 5);
                LADDER_CNT[si] = 3;
                LADDER_CNT[si + 1] = 0;
                di += 2;
                stw(LADDER_IDX + si, di);
                di += 2;
                stw(LADDER_IDX + si + 1, di);
                di += 4;
                si += 2;
                bxY -= word_330D7;
                ch += 3;
            } else {                             /* -8..-1: 4-seg rung, -5 */
                stw(LADDER_X + di, word_330CF);
                stw(LADDER_X + di + 2, word_330CF);
                stw(LADDER_X + di + 4, word_330D1);
                stw(LADDER_X + di + 6, word_330D3);
                stw(LADDER_X + di + 8, word_330D5);
                stw(LADDER_X + di + 10, word_330D5);
                stw(LADDER_Y + di + 2, bxY);
                stw(LADDER_Y + di + 4, bxY);
                stw(LADDER_Y + di + 6, bxY);
                stw(LADDER_Y + di + 8, bxY);
                stw(LADDER_Y + di, bxY - 5);
                stw(LADDER_Y + di + 10, bxY - 5);
                LADDER_CNT[si] = 2;
                LADDER_CNT[si + 1] = 2;
                di += 2;
                stw(LADDER_IDX + si, di);
                di += 6;
                stw(LADDER_IDX + si + 1, di);
                di += 4;
                si += 2;
                bxY -= word_330D7;
                ch += 4;
            }
            dl = (int8)(dl + 1);
            if (--cl == 0) break;
        }
        byte_333B5 = (uint8)ch;                  /* tapeChar = column count */
        di -= 2;

        /* rotate the built vertices by roll (sub_11C58 sine pair):
         *   X' = (bp*X)>>15 - (bx*Y)>>15
         *   Y' = (3*(bp*Y + bx*X))>>17
         * bp = sin(0x4000-roll) = cos(roll); bx = sin(-roll) = -sin(roll). */
        {
            int32 bp = f19_sin((int16)(0x4000 - g_ourRoll));
            int32 bx2 = f19_sin((int16)(-g_ourRoll));
            for (; di >= 0; di -= 2) {
                int32 x = ldw(LADDER_X + di);
                int32 y = ldw(LADDER_Y + di);
                int16 nx = (int16)((bp * x) >> 15) - (int16)((bx2 * y) >> 15);
                int16 ny = (int16)((int16)((3 * (bp * y)) >> 16) >> 1)
                         + (int16)((int16)((3 * (bx2 * x)) >> 16) >> 1);
                stw(LADDER_X + di, nx);
                stw(LADDER_Y + di, ny);
            }
        }

        /* draw the rungs (skipped while the high-G blackout flag is clear) */
        if (byte_38B0A != 0) {
            int16 savedMaxX = g_clipMaxX;
            int16 savedMaxY = g_clipMaxY;
            gfx_setDrawColor(7);
            gfx_setBlitOffset(word_330E2);      /* pitchBlitOfs */
            g_clipMaxX = word_330E4;
            g_clipMaxY = word_330E6;
            di = 0;
            si = 0;
            byte_333B4 = 0;                      /* tapeColumn */
            for (;;) {
                g_lineX1 = ldw(LADDER_X + di) + word_330DA;
                g_lineY1 = ldw(LADDER_Y + di) + word_330DC;
                g_lineX2 = ldw(LADDER_X + di + 2) + word_330DA;
                g_lineY2 = ldw(LADDER_Y + di + 2) + word_330DC;
                drawClipLineGlobal();
                di += 2;
                byte_333B4++;
                if (--LADDER_CNT[si] != 0) continue;
                di += 2;
                si++;
                if (LADDER_CNT[si] == 0) si++;
                if ((int8)byte_333B4 < (int8)byte_333B5) continue;
                break;
            }

            /* rung labels (roll-offset curves at rollOfsBase+1A/9A/11A/19A) */
            {
                int16 rollIdx;
                gfx_nop22();                     /* slot 0x22 — call kept */
                word_333B6 += 0x0B;              /* tapeCursorX += 11 */
                si = 0;
                word_333B8 = 5;                  /* label counter cell */
                rollIdx = (((uint16)g_ourRoll >> 8) >> 2) & 0xFF;
                di = rollIdx * 2;
                word_33422 = ldw(rollOfsBase + 0x1A + di);   /* A0 */
                word_33424 = ldw(rollOfsBase + 0x9A + di);   /* A1 */
                word_33426 = ldw(rollOfsBase + 0x11A + di);  /* A2 */
                word_33428 = ldw(rollOfsBase + 0x19A + di);  /* A3 */
                /* +0x80 wraps in the byte before the >>2, as the original's
                 * 8-bit register does — keeps the index inside one curve. */
                rollIdx = ((((uint16)g_ourRoll >> 8) + 0x80) & 0xFF) >> 2;
                di = rollIdx * 2;
                word_3341A = ldw(rollOfsBase + 0x1A + di);   /* B0 */
                word_3341C = ldw(rollOfsBase + 0x9A + di);   /* B1 */
                word_3341E = ldw(rollOfsBase + 0x11A + di);  /* B2 */
                word_33420 = ldw(rollOfsBase + 0x19A + di);  /* B3 */
                for (;;) {
                    int16 idx4, vi, ax;
                    idx4 = word_333B6 * 4;
                    stw(tapeDrawStr, ldw(LADDER_LBL + idx4));
                    stw(tapeDrawStr + 2, ldw(LADDER_LBL + idx4 + 2));
                    vi = ldw(LADDER_IDX + si) & 0xFF;
                    ax = ldw(LADDER_X + vi);
                    ax += (idx4 >= 0x2C) ? word_3341A : word_3341E; /* B0 : B2 */
                    word_33020 = ax + word_330E0;              /* text2[4] */
                    ax = ldw(LADDER_Y + vi);
                    ax += (vi >= 0x2C) ? word_3341C : word_33420;   /* B1 : B3 */
                    word_33022 = ax + word_330DE;              /* text2[5] */
                    tapeStr(tapeText2, tapeDrawStr, 0x06);
                    si++;
                    idx4 = word_333B6 * 4;
                    stw(tapeDrawStr, ldw(LADDER_LBL + idx4));
                    stw(tapeDrawStr + 2, ldw(LADDER_LBL + idx4 + 2));
                    vi = ldw(LADDER_IDX + si) & 0xFF;
                    ax = ldw(LADDER_X + vi);
                    ax += (idx4 >= 0x2C) ? word_33422 : word_33426; /* A0 : A2 */
                    word_33020 = ax + word_330E0;
                    ax = ldw(LADDER_Y + vi);
                    ax += (vi >= 0x2C) ? word_33424 : word_33428;   /* A1 : A3 */
                    word_33022 = ax + word_330DE;
                    tapeStr(tapeText2, tapeDrawStr, 0x06);
                    si++;
                    word_333B6++;
                    if (--word_333B8 == 0) break;
                }
            }
            g_clipMaxY = savedMaxY;
            g_clipMaxX = savedMaxX;
            gfx_setBlitOffset(0);
        }
    }
}
