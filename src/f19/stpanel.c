/* START.EXE — per-row object/detail panel renderer (seg000:0x2754).
 * base = item-table pointer, row = record index (stride 0x32),
 * pg = pointer into the sprite-parameter table.  The record flag byte at
 * offset 0x30 (& 7) selects one of several status-panel families; 0x10 marks
 * the record live.  This module compiles /Ot — the original emits branch-
 * target alignment nops that /Os suppresses. */
#include "f19.h"
#include "f19stvars.h"

#define objectActive ((uint8 *)f19_stSpace.m_objectActive)

extern void sub_14622(void *o, int16 a, int16 b, int16 c, int16 d); /* clearRect */
extern void sub_13B76(void *o, char *s, int16 x, int16 y); /* drawStringAt */
extern void sub_15120(char *d, char *s);
extern void sub_15189(char *d, char *s);
extern void sub_13D15(int16 *pg, char *s, int16 a, int16 b, int16 c,
                      int16 d);
extern void sub_13FB3(int16 n, char *b);    /* seg000:0x3fb3 numToStr */

struct ObjD {                               /* dseg:0xb38e, stride 0x10 */
    int16 f0;                               /* linked id (0 ⇒ fE byte) */
    int16 pad2[2];                          /* f19_worldObjects x/y_coord */
    int16 pad4;                             /* sub-record index */
    int16 targetFlags;                      /* &0x400 / &0x100 tested */
    int16 padA;                             /* *0x20 name-table index */
    int16 padC;                             /* count field (itoa'd) */
    uint8  fE;                              /* fallback id */
    uint8  padF;
};
struct Attr14 { int16 f0, f2;               /* dseg:0x3e26, stride 0x0e */
                uint8 f4, pad5[9]; };
struct Rec18 { int16 f0, pad[8]; };

/* seg000:0x2754 — object detail panel.  base = item-table offset into the
 * sprite-param pool, row = record index (stride 0x32), pg = page handle.
 * The flag byte at record+0x30 selects a family of status panels. */
void f19_sub_12754(void *t, int16 idx, int16 *pd) {
    int8  *base = (int8 *)t;
    int16  row  = (uint16)idx;
    int16 *pg   = pd;
    char   cr[2], v[2], cc[2], x[2];
    int16  w;
    int16  i, j, k, l;
    char   flags;
    char   numbuf[8];
    int16  m;
    register int16 id;

    cr[0] = 0x0D; cr[1] = 0;
    v[0]  = 0x89; v[1] = 0;
    cc[0] = 0x8D; cc[1] = 0;
    x[0]  = 0x80; x[1]  = 0;
    id    = row * 0x32;
    flags = base[id + 0x30];
    if (!(flags & 0x10))
        return;
    if ((flags & 7) == 7) {
        sub_14622(pg, 0xEB, 0xA, 0x13F, 0x6D);
        pg[2] = 0;
        sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), v);
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x32))));
        sub_13D15(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0x50, 0xEB, 0x3C, 8);
    }
    if ((base[(id = row * 0x32) + 0x30] & 7) == 5) {
        sub_14622(pg, 0xEB, 0xA, 0x13F, 0x6D);
        sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x57))));
        sub_13D15(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0x50, 0xEB, 0x1E, 8);
        sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), v);
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x81))));
        sub_13D15(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0x50, 0xEB, 0x50, 8);
    }
    if ((base[(id = row * 0x32) + 0x30] & 7) == 6) {
        if (byte_2C976 == 1) {
            sub_14622(pg, 0xEB, 0xA, 0x13F, 0x6D);
            switch (tileMarksOn) {
            case 0:
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0xBE))));
                sub_13D15(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0x50, 0xEB, 0x1E, 8);
                pg[2] = 9;
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0xDD))));
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEB, 0x5A);
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0xED))));
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEB, 0x62);
                break;
            case 1:
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x102))));
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x13D))));
                sub_13D15(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0x50, 0xEB, 0x1E, 8);
                pg[2] = 9;
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x150))));
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEB, 0x5A);
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x162))));
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEB, 0x62);
                break;
            }
        } else {
            sub_14622(pg, 0xEB, 0xA, 0x13F, 0x6D);
            sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x177))));
            sub_13D15(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0x50, 0xEB, 0x1E, 8);
        }
    }
    if ((base[(id = row * 0x32) + 0x30] & 7) == 1) {
        sub_14622(pg, 0xEB, 0xA, 0x13E, 0x6D);
        sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x1B8))));
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), cr);
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), v);
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x1E4))));
        sub_13D15(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0x50, 0xEB, 0x28, 8);
    }
    flags = base[(id = row * 0x32) + 0x30] & 7;
    if (flags == 2 || flags == 3) {
        sub_14622(pg, 0xEB, 0xA, 0x13F, 0x6D);
        if ((base[(id = row * 0x32) + 0x30] & 7) == 2) {
            if (ringMode == 0)
                goto type3chk;
            goto panel;
        }
type3chk:
        if ((base[(id = row * 0x32) + 0x30] & 7) == 3)
            goto chkUnit;
        else
            goto other;
chkUnit:
        if (byte_2C977 == 1)
            goto panel;
        goto other;
panel:
        pg[2] = 0x0D;
        sub_13B76(pg, (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x210))), 0xFA, 0x14);
        sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x21B))));
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)f19_dsegAt(word_2CA70[(id = word_2B38E[word_2C968].f0)
                                                                                                           ? id
                                                                                                           : word_2B38E[word_2C968].fE]))));
        sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, 0x1C);
        w = 0x24;
        if (objectActive[word_2C968] == 1)
            goto afterObjs;
        else {
            sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x221))));
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), strTab14[word_2B38E[word_2C968].pad4]);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x228))));
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)),
                      (word_23E26[word_2B38E[word_2C968].pad4].f4 & 1)
                          ? (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x22A)))
                          : (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x232))));
            sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, w);
            w += 8;
            sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x238))));
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
            if (ringMode == 2)
                sub_13FB3(word_23E26[word_2B38E[word_2C968].pad4].f0,
                          numbuf);
            else {
                id = word_2B38E[word_2C968].pad4 * 14;
                sub_13FB3((*(int16 *)((char *)word_23E26 + id) *
                           *(int16 *)((char *)&word_23E28 + id)) / 16,
                          numbuf);
            }
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), numbuf);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x241))));
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)),
                      ringMode == 1 ? (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x245))) : (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x24B))));
            sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, w);
            w += 8;
            sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x251))));
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
            sub_13FB3(word_241C8[word_2B38E[word_2C968].pad4].f0, numbuf);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), numbuf);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x25A))));
            sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, w);
            w += 8;
            if (word_2B38E[word_2C968].targetFlags & 0x100) {
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x263))));
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x26C))));
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, w);
                w += 8;
            }
        }
afterObjs:
        if (objectActive[word_2C968] != 0)
            goto actArm;
        goto plainRow;
actArm:
    sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
    sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x274))));
    sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
    switch (objectActive[word_2C968]) {
    case 1:
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x27C))));
        sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, w);
        sub_13B76(pg, (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x28A))), 0xEC, w + 8);
        break;
    case 2:
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x29D))));
        sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, w);
        sub_13B76(pg, (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x2AC))), 0xEC, w + 8);
        break;
    case 3:
        sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x2BC))));
        sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, w);
        sub_13B76(pg, (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x2C9))), 0xEC, w + 8);
        break;
    }
plainRow:
        pg[2] = 9;
        sub_13B76(pg, (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x2DB))), 0xEB, 0x52);
other:
        pg[2] = 9;
        sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x2EA))));
        if ((base[(id = row * 0x32) + 0x30] & 7) == 2) {
            switch (ringMode) {
            case 0:
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x2F6))));
                break;
            case 1:
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x2FF))));
                break;
            case 2:
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x308))));
                break;
            }
        } else {
            switch (byte_2C977) {
            case 0:
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x313))));
                break;
            case 1:
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x31C))));
                break;
            }
        }
        sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEB, 0x5A);
    }
    if ((base[row * 0x32 + 0x30] & 7) == 4) {
        sub_14622(pg, 0xEB, 0xA, 0x13F, 0x6D);
        if (unitMarksOn == 1) {
            pg[2] = 0x0D;
            sub_13B76(pg, (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x327))), 0xF5, 0x14);
            sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x334))));
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
            id = word_2B38E[word_2C144].f0;
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)f19_dsegAt(word_2CA70[id ? id : word_2B38E[word_2C144].fE]))));
            sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, 0x1C);
            sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x33A))));
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
            if (word_2B38E[word_2C144].targetFlags & 0x400) {
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x343))));
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, 0x24);
                goto tail4;
            }
            if (word_2B38E[word_2C144].targetFlags & 0x100) {
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x34C))));
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, 0x24);
                goto tail4;
            }
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x354))));
            sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, 0x24);
            if (word_2B38E[word_2C144].padC != 0) {
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x35C))));
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)),
                          strTab32[word_2B38E[word_2C144].padA]);
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, 0x2C);
                sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), cc);
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x368))));
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), x);
                sub_13FB3(word_2B38E[word_2C144].padC, numbuf);
                sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), numbuf);
                sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEC, 0x34);
            }
tail4:
            pg[2] = 9;
            sub_13B76(pg, (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x371))), 0xEB, 0x52);
        }
        pg[2] = 9;
        sub_15120((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x384))));
        switch (unitMarksOn) {
        case 0:
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x390))));
            break;
        case 1:
            sub_15189((char *)(((uint8 *)f19_stSpace.m_strB96A)), (char *)(((uint8 *)(f19_stSpace.m_str282 + 0x399))));
            break;
        }
        sub_13B76(pg, (char *)(((uint8 *)f19_stSpace.m_strB96A)), 0xEB, 0x5A);
    }
}
