/* START.EXE — mission-setup screens (seg000:0xcbd8 / 0xce56).
 * sub_1CBD8 = notepad "Select an option" menu (state 2);
 * sub_1CE56 = the arming/loadout screen (state 6) — pick weapons into the
 * four bays, then return to state 2. */
#include "f19.h"

#define byte_2C160 (*(uint8 *)(f19_dseg + 0xC160))
#define byte_2C7D6 (*(uint8 *)(f19_dseg + 0xC7D6))
#define byte_2C7D7 (*(uint8 *)(f19_dseg + 0xC7D7))
#define byte_2CA6A (*(uint8 *)(f19_dseg + 0xCA6A))
#define byte_2CE26 (*(uint8 *)(f19_dseg + 0xCE26))
#define byte_298F6 (*(uint8 *)(f19_dseg + 0x98F6))
#define byte_298F7 (*(uint8 *)(f19_dseg + 0x98F7))
#define byte_20A1A (*(uint8 *)(f19_dseg + 0xA1A))   /* PIT-ISR tick cell */
#define word_2C7CE (*(int16 *)(f19_dseg + 0xC7CE))
#define word_2C7D4 (*(int16 *)(f19_dseg + 0xC7D4))
#define word_2C9E0 (*(int16 *)(f19_dseg + 0xC9E0))
#define word_2CA4C (*(int16 *)(f19_dseg + 0xCA4C))
#define word_2CA46 (*(int16 *)(f19_dseg + 0xCA46))
#define word_2CA48 (*(int16 *)(f19_dseg + 0xCA48))
#define word_2B386 (*(uint16 *)(f19_dseg + 0xB386))
#define word_2B94E (*(int16 *)(f19_dseg + 0xB94E))
#define word_2BB74 (*(int16 *)(f19_dseg + 0xBB74))
#define word_2BE4C (*(int16 *)(f19_dseg + 0xBE4C))
#define word_22324 (*(int16 *)(f19_dseg + 0x2324))
#define word_26E3C (*(int16 *)(f19_dseg + 0x6E3C))
#define word_26E54 (*(int16 *)(f19_dseg + 0x6E54))
#define word_26F1A (*(int16 *)(f19_dseg + 0x6F1A))
#define word_26F2A (*(int16 *)(f19_dseg + 0x6F2A))
#define word_27094 (*(int16 *)(f19_dseg + 0x7094))
#define word_270AC (*(int16 *)(f19_dseg + 0x70AC))
#define word_27B3A (*(int16 *)(f19_dseg + 0x7B3A))
#define word_277EE (*(int16 *)(f19_dseg + 0x77EE))
#define word_278C6 (*(int16 *)(f19_dseg + 0x78C6))
#define word_278D4 (*(int16 *)(f19_dseg + 0x78D4))
#define word_298D4 (*(int16 *)(f19_dseg + 0x98D4))
#define word_298E0 (*(int16 *)(f19_dseg + 0x98E0))
#define word_29948 (*(int16 *)(f19_dseg + 0x9948))
#define word_2A0C4 (*(int16 *)(f19_dseg + 0xA0C4))
#define word_2C972 (*(int16 *)(f19_dseg + 0xC972))
#define word_2C974 (*(int16 *)(f19_dseg + 0xC974))
#define word_2D064 (*(int16 *)(f19_dseg + 0xD064))
#define word_2D06C (*(int16 *)(f19_dseg + 0xD06C))
#define word_2D26E (*(int16 *)(f19_dseg + 0xD26E))
#define word_2D272 (*(int16 *)(f19_dseg + 0xD272))
#define word_2D2C4 (*(int16 *)(f19_dseg + 0xD2C4))
#define word_2D2C6 (*(int16 *)(f19_dseg + 0xD2C6))
#define word_2D2CA (*(int16 *)(f19_dseg + 0xD2CA))
#define word_2D2CC (*(int16 *)(f19_dseg + 0xD2CC))
#define word_2D2CE (*(int16 *)(f19_dseg + 0xD2CE))
#define word_2C9E0_CELL (f19_dseg + 0xC9E0)

#define strB96A ((char *)(f19_dseg + 0xB96A))
#define DSTR(off) ((char *)(f19_dseg + (off)))
#define PDESC(cell) ((int16 *)(f19_dseg + (uint16)(cell)))
#define COMMW(i) (((int16 *)f19_commBase)[i])
#define GDW(i) (((int16 *)(f19_commBase + 0x120E))[i])
#define NAMEROW(i) (((struct MenuRow *)(f19_dseg + word_2B386))[i])

struct MenuRow { int16 name, yoff; };
struct SelRow;
extern int16  f19_sub_10AE8(struct SelRow *tab, int16 namesOfs, int16 cnt,
                            int16 a4, int16 *pd, int16 a6);
extern void   sub_10924(char *tab, int16 sel, int16 cnt, int16 x, int16 y, ...);
extern void   sub_13B50(int16 *p, struct MenuRow r, int16 c, int16 d);
extern void   sub_13B76(void *o, char *s, int16 x, int16 y);
extern void   sub_13D15(int16 *pg, char *s, int16 a, int16 b, int16 c, int16 d);
extern void   sub_13FB3(int16 n, char *b);
extern void   sub_14622(void *o, int16 x0, int16 y0, int16 x1, int16 y1);
extern void   sub_14746(char *s, int16 a, int16 b);
extern int16  sub_161CC(int16 a, int16 b, int16 c);
extern void   sub_1685C(int16 v);
extern void   sub_14BEE(int16 a, int16 b);
extern void   f19_loadSpriteRes(const char *name, int16 sel);
extern int16  f19_setViewOrigin(int16 a, int16 b, int16 flag);
extern void   f19_sub_108B7(void);
extern void   sub_14E9C(void);
extern void   sub_14EDA(void);
extern void   sub_15120(char *d, char *s);
extern void   sub_15189(char *d, char *s);
extern void   f19_drawScorePanel(void);
extern int16  randMul(uint16 n);
extern void   mystrcat(char *d, const char *s);
extern int16  far ovlCall_c7b(void);          /* gfx_getVal2 -> 1 */

/* seg000:0xcbd8 — notepad "Select an option" page. Draws Note.pic, lists the
 * count entries of the far-ptr table at word_2CA46 via the MenuRow table at
 * word_2B386, runs the row-select widget over rows at dseg 0x6E56, then maps
 * the result through byte_26F2C into byte_2C160. */
void sub_1CBD8(void) {
    int16 y, i, res;

    word_2B386 = 0x8DA;
    word_2CA46 = 0x8D6;
    word_2CA48 = 0x802;
    word_29948 = 0;
    f19_dseg[0x6F2C] = (COMMW(0x11) == 1) ? 8 : 7;
    switch (word_2C7D4) {
    case 0:
        if (word_2BB74 == 1) {
            sub_14746(DSTR(0x6DE0), word_2D2CA, word_2D2CC);
            word_22324 = sub_161CC(1, 1, word_2D2C6);
        }
        ovlCall_c2b(0xF);
        sub_14746(DSTR(0x6DEA), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        ovlCall_c53();
        ovlCall_bea(word_2D06C);
        break;
    case 1:
        if (word_2BB74 == 1)
            word_22324 = sub_161CC(1, 1, word_2D2CE);
        if (byte_2C7D7 == 0) {
            ovlCall_c2b(0xF);
            sub_14746(DSTR(0x6DF3), word_2C972, word_2C974);
            sub_14BEE(word_2D064, word_2D2C4);
            byte_2C7D7 = 1;
        }
        ovlCall_c53();
        ovlCall_bea(word_2D2C4);
        break;
    case 2:
        ovlCall_c2b(0xF);
        sub_14746(DSTR(0x6DFC), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        if (word_2BB74 == 1)
            word_22324 = sub_161CC(1, 1, word_2D2CE);
        ovlCall_c53();
        ovlCall_bea(word_2D06C);
        break;
    }
    ovlCall_c58();
    f19_sub_108B7();
    ovlCall_c2b(9);
    y = 0x5B;
    for (i = 0; i < *(uint8 *)f19_farAt(word_2CA46); i++) {
        sub_13B50(PDESC(word_26E3C), NAMEROW(i), 0x76, y);
        y += 0x11;
    }
    PDESC(word_26E54)[2] = 9;
    sub_13B76(PDESC(word_26E54), DSTR(0x6E05), 0x76, 0xBE);
    PDESC(word_26E54)[2] = 0;
    word_26F1A = 2;
    sub_10924(DSTR(0x6E56), word_2CA48, *(uint8 *)f19_farAt(word_2CA46),
              0x96, 3 * PDESC(word_26F2A)[1] + 0x5E, PDESC(word_26E3C));
    sub_14E9C();
    res = f19_sub_10AE8((struct SelRow *)DSTR(0x6E56), word_2CA48,
                        *(uint8 *)f19_farAt(word_2CA46), word_26F2A,
                        PDESC(word_26E3C), 0);
    if (res == 0)
        byte_2CA6A = 1;
    GDW(0x21) = res;
    sub_14EDA();
    if (word_2BB74 == 1)
        sub_1685C(word_2A0C4);
    word_22324 = 0;
    byte_2C160 = f19_dseg[0x6F2C + res];
}

/* seg000:0xce56 — the arming screen. Draws arming.pic + arming.spr sheets,
 * labels the 18 weapon rows + special-aircraft row, seeds a random loadout
 * when byte_2C7D6 is set, blits the four bay sprites from commData+0x38, then
 * runs the pick-weapon -> pick-bay loop until row 0x12 ("Arming Complete")
 * is selected. */
void sub_1CE56(void) {
    char   tmp[8];
    int16  w, i, y, n5, t, w2, st, sel, res2, run, ok;

    word_2B386 = 0x8F2;
    word_2CA46 = 0x8EE;
    word_2CA48 = 0x93E;
    word_2C9E0 = ovlCall_c7b();
    switch (word_2C7D4) {
    case 0:
        ovlCall_c2b(1);
        sub_14746(DSTR(0x6FC8), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        ovlCall_c2b(0xF);
        f19_loadSpriteRes(DSTR(0x6FD3), word_2D26E);
        w = word_2D26E;
        ovlCall_c53();
        ovlCall_bea(word_2D06C);
        break;
    case 1:
        w = word_2D272;
        if (byte_2CE26 == 0) {
            ovlCall_c2b(1);
            sub_14746(DSTR(0x6FDE), word_2C972, word_2C974);
            sub_14BEE(word_2D064, word_298E0);
            byte_2CE26 = 1;
        }
        if (byte_298F6 == 0) {
            ovlCall_c2b(0xF);
            f19_loadSpriteRes(DSTR(0x6FE9), word_2D272);
            byte_298F6 = 1;
        }
        ovlCall_c53();
        ovlCall_bea(word_298E0);
        break;
    case 2:
        ovlCall_c2b(1);
        sub_14746(DSTR(0x6FF4), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        w = word_2D272;
        ovlCall_c53();
        ovlCall_bea(word_2D06C);
        break;
    default:
        w = 0;
        break;
    }
    ovlCall_c2b(3);
    /* SpriteParams.bufPtr (+0) cells for the 21 weapon records at
     * 0x71A4+i*0x1E (i != 19 is the empty-bay sprite) and the four bay
     * records at 0x70AE+i*0x1E. */
    for (i = 0; i < 19; i++)
        *(int16 *)(f19_dseg + 0x71A4 + i * 0x1E) = w;
    *(int16 *)(f19_dseg + 0x73FC) = w;
    *(int16 *)(f19_dseg + 0x741C) = w;
    for (i = 0; i < 4; i++)
        *(int16 *)(f19_dseg + 0x70AE + i * 0x1E) = w;
    if (*(int16 *)(f19_dseg + 0x4B10 + word_2B94E * 0xC) == 3 ||
        *(int16 *)(f19_dseg + 0x4B10 + word_2B94E * 0xC) == 4) {
        word_27B3A = 0x73DE;
        byte_298F7 = 1;
    } else {
        word_27B3A = 0x7384;
        byte_298F7 = 0;
    }
    PDESC(word_270AC)[2] = 6;
    y = 0x8D;
    for (i = 0; i < 6; i++) {
        sub_13B50(PDESC(word_270AC), NAMEROW(i), 0x12, y);
        y += 6;
    }
    y = 0x8D;
    for (i = 6; i < 0xC; i++) {
        sub_13B50(PDESC(word_270AC), NAMEROW(i), 0x7A, y);
        y += 6;
    }
    y = 0x8D;
    for (i = 0xC; i < 0x10; i++) {
        sub_13B50(PDESC(word_270AC), NAMEROW(i), 0xDC, y);
        y += 6;
    }
    if (byte_298F7 == 1)
        sub_13B50(PDESC(word_270AC), NAMEROW(0x12), 0xDC, y);
    else
        sub_13B50(PDESC(word_270AC), NAMEROW(0x10), 0xDC, y);
    y += 6;
    sub_13B50(PDESC(word_270AC), NAMEROW(0x11), 0xDC, y);
    PDESC(word_27094)[2] = 6;
    PDESC(word_27094)[6] = 1;
    sub_13B76(PDESC(word_27094), DSTR(0x6FFF), 0xDC, 0x7D);
    PDESC(word_270AC)[2] = 0xF;
    sub_15120(strB96A, DSTR(0x700F));
    sub_13FB3(COMMW(0x17), tmp);
    sub_15189(strB96A, tmp);
    sub_15189(strB96A, DSTR(0x701B));
    sub_13B76(PDESC(word_270AC), strB96A, 3, 0x6C);
    f19_drawScorePanel();
    PDESC(word_27094)[6] = 4;
    PDESC(word_27094)[2] = 0;
    PDESC(word_270AC)[2] = 9;
    sub_13B76(PDESC(word_270AC), DSTR(0x7020), 0x64, 0xB3);
    PDESC(word_27094)[2] = 0;
    if (byte_2C7D6 == 1) {
        int16 *flags = (int16 *)(f19_dseg + 0x9924);
        int16 *bays  = (int16 *)(f19_dseg + 0x98F8);
        for (i = 0; i < 0x12; i++)
            flags[i] = 0;
        byte_2C7D6 = 0;
        for (i = 0; i < 0x12; i++)
            bays[i] = 0;
        n5 = randMul(5);
        for (i = 0; i < n5; ) {
            w2 = randMul(0x12);
            if (((int16 *)(f19_dseg + 0x7932))[w2] == 0xFF)
                continue;
            if (flags[((int16 *)(f19_dseg + 0x7932))[w2]] == 1)
                continue;
            if (w2 == COMMW(0x1C) || w2 == COMMW(0x1D) ||
                w2 == COMMW(0x1E) || w2 == COMMW(0x1F))
                continue;
            flags[w2] = 1;
            for (;;) {
                t = randMul(8);
                if (bays[t] == 0) {
                    bays[t] = 1;
                    ((int16 *)(f19_dseg + 0xD2D0))[w2] = t;
                    break;
                }
            }
            i++;
        }
    }
    PDESC(word_270AC)[2] = 7;
    for (i = 0; i < 0x12; i++) {
        if (((int16 *)(f19_dseg + 0x9924))[i] == 1) {
            int16 *rec;
            word_2BE4C = 0x743C + i * 0x32;
            rec = (int16 *)(f19_dseg + word_2BE4C);
            sub_13B50(PDESC(word_270AC), NAMEROW(i), rec[0], rec[1]);
            *(int16 *)(f19_dseg + 0x744C + i * 0x32) = 1;
        }
    }
    st = 0;
    if (word_2C9E0 == 1)
        ovlCall_bc7(PDESC(word_27094),
                    ((int16 *)(f19_dseg + 0x7136))[0],
                    ((int16 *)(f19_dseg + 0x713E))[0],
                    ((int16 *)(f19_dseg + 0x7146))[0],
                    ((int16 *)(f19_dseg + 0x714E))[0], 5, 9);
    PDESC(word_270AC)[2] = 0xF;
    for (i = 0; i < 4; i++) {
        word_298D4 = 0x70AE + i * 0x1E;
        if (COMMW(0x1C + i) == 0x13) {
            *(int16 *)(f19_dseg + word_298D4 + 2) = 1;
            *(int16 *)(f19_dseg + word_298D4 + 4) = 0xA4;
        } else {
            *(int16 *)(f19_dseg + word_298D4 + 2) =
                (COMMW(0x1C + i) % 6) * 0x33 + 1;
            *(int16 *)(f19_dseg + word_298D4 + 4) =
                (COMMW(0x1C + i) / 6) * 0x23 + 1;
        }
        ovlCall_b4f(word_298D4);
        COMMW(0x20 + i) =
            ((int16 *)(f19_dseg + 0x7156))[COMMW(0x1C + i)];
    }
    f19_sub_108B7();
    ovlCall_c58();
    run = 1;
    sub_14E9C();
    while (run) {
        ovlCall_bc7(PDESC(word_27094), 0xD5, 0x43, 0x12E, 0x5F, 9, 6);
        word_277EE = 2;
        sel = 0x12;
        word_2C7CE = 1;
        ok = 1;
        do {
            if (COMMW(0x39) == 1) {              /* joystick flush */
                while (ovlCall_ccb(0) != 0)
                    ;
                byte_20A1A = 0;
                while (byte_20A1A <= 5)
                    ;
                while (ovlCall_ccb(0) != 0)
                    ;
            }
            sub_10924(DSTR(0x743C), word_2CA48,
                      *(uint8 *)f19_farAt(word_2CA46),
                      ((int16 *)(f19_dseg + 0x78D6))[sel],
                      ((int16 *)(f19_dseg + 0x78FC))[sel],
                      PDESC(word_27094));
            sel = f19_sub_10AE8((struct SelRow *)DSTR(0x743C), word_2CA48,
                                *(uint8 *)f19_farAt(word_2CA46), word_278D4,
                                PDESC(word_27094), 0);
            if (sel < 0 || ((int16 *)(f19_dseg + 0x9924))[sel] == 0 ||
                sel == 0x12)
                ok = 0;
            else
                *(int16 *)(f19_dseg + 0x746A + sel * 0x32) = 2;
        } while (ok);
        word_2C7CE = 0;
        if (sel == 0x12) {
            run = 0;
            continue;
        }
        PDESC(word_270AC)[2] = 9;
        word_2BE4C = 0x743C + sel * 0x32;
        {
            int16 *rec = (int16 *)(f19_dseg + word_2BE4C);
            if (byte_298F7 == 1 && sel == 0x10)
                sub_13B50(PDESC(word_270AC), NAMEROW(0x12),
                          rec[0], rec[1]);
            else
                sub_13B50(PDESC(word_270AC), NAMEROW(sel),
                          rec[0], rec[1]);
        }
        ovlCall_b4f(*(int16 *)(f19_dseg + 0x741A));
        sub_15120(strB96A, DSTR(0x7044));
        if (sel == 0x10 && byte_298F7 == 1)
            sub_15189(strB96A,
                      DSTR(*(uint16 *)(f19_dseg + 0x71A2)));
        else
            sub_15189(strB96A,
                      DSTR(((uint16 *)(f19_dseg + 0x717E))[sel]));
        sub_15189(strB96A, DSTR(0x704B));
        sub_13D15(PDESC(word_27094), strB96A, 0x64, 0xD0, 2, 8);
        sub_13B76(PDESC(word_270AC), DSTR(0x705C), 0xC1, 0x18);
        if (COMMW(0x39) == 1) {
            while (ovlCall_ccb(0) != 0)
                ;
            byte_20A1A = 0;
            while (byte_20A1A <= 8)
                ;
            while (ovlCall_ccb(0) != 0)
                ;
        }
        *(int16 *)(f19_dseg + 0x7820 + st * 0x32) = 2;
        word_2CA4C = 1;
        sub_10924(DSTR(0x77F2), word_2CA48, 4,
                  ((int16 *)(f19_dseg + 0x7922))[st],
                  ((int16 *)(f19_dseg + 0x792A))[st],
                  PDESC(word_27094));
        res2 = f19_sub_10AE8((struct SelRow *)DSTR(0x77F2), word_2CA48, 4,
                             word_278C6, PDESC(word_27094), 0);
        word_2CA4C = 0;
        if (res2 >= 0 && res2 <= 3) {
            st = res2;
            PDESC(word_27094)[3] = (word_2C9E0 == 1) ? 9 : 0;
            sub_14622(PDESC(word_27094),
                      ((int16 *)(f19_dseg + 0x7136))[st],
                      ((int16 *)(f19_dseg + 0x713E))[st],
                      ((int16 *)(f19_dseg + 0x7146))[st],
                      ((int16 *)(f19_dseg + 0x714E))[st]);
            PDESC(word_27094)[3] = 0xF;
            word_298D4 = 0x70AE + st * 0x1E;
            if (sel == 0x10 && byte_298F7 == 1) {
                COMMW(0x1C + st) = 0x13;
                COMMW(0x20 + st) = *(int16 *)(f19_dseg + 0x717C);
                *(int16 *)(f19_dseg + word_298D4 + 2) = 1;
                *(int16 *)(f19_dseg + word_298D4 + 4) = 0xA4;
            } else {
                COMMW(0x1C + st) = sel;
                COMMW(0x20 + st) =
                    ((int16 *)(f19_dseg + 0x7156))[sel];
                *(int16 *)(f19_dseg + word_298D4 + 2) =
                    (COMMW(0x1C + st) % 6) * 0x33 + 1;
                *(int16 *)(f19_dseg + word_298D4 + 4) =
                    (COMMW(0x1C + st) / 6) * 0x23 + 1;
            }
            ovlCall_b4f(word_298D4);
            PDESC(word_270AC)[2] = 0xF;
            f19_drawScorePanel();
        }
        ovlCall_b4f(*(int16 *)(f19_dseg + 0x743A));
        if (word_2C9E0 == 1)
            ovlCall_bc7(PDESC(word_27094),
                        ((int16 *)(f19_dseg + 0x7136))[st],
                        ((int16 *)(f19_dseg + 0x713E))[st],
                        ((int16 *)(f19_dseg + 0x7146))[st],
                        ((int16 *)(f19_dseg + 0x714E))[st], 9, 5);
        if (res2 >= 0 && res2 <= 3)
            st = (st + 1) & 3;
        if (word_2C9E0 == 1)
            ovlCall_bc7(PDESC(word_27094),
                        ((int16 *)(f19_dseg + 0x7136))[st],
                        ((int16 *)(f19_dseg + 0x713E))[st],
                        ((int16 *)(f19_dseg + 0x7146))[st],
                        ((int16 *)(f19_dseg + 0x714E))[st], 5, 9);
        ovlCall_bc7(PDESC(word_27094), 0x12, 0x8D, 0x131, 0xB0, 9, 6);
    }
    sub_14EDA();
    if (COMMW(0x15) == 0)
        for (i = 0; i < 4; i++)
            COMMW(0x20 + i) = 0;
    byte_2C160 = 2;
}
