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
extern void sub_13B76(void *o, const char *s, int16 x, int16 y); /* drawStringAt */
extern void sub_15120(char *d, const char *s);
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
struct MsnRec {                             /* dseg:0x9d54, stride 0x50 */
    int16 f0;
    char  name[0x1E];
    int16 f20, f22, f24, f26, f28, f2a, f2c, f2e, f30;
    union { struct { int16 f32, f34; } w; long coord; } u32;
    int16 f36, f38, f3a, f3c, f3e, f40;
    int16 f42, f44, f46, f48, f4a, f4c, f4e;
};

extern int16 sub_13E38(int16 *p, char *s);  /* seg000:0x3e38 stringWidth */
extern void  sub_13E7C(long v, char *buf);  /* seg000:0x3e7c my_ltoa */
extern int16 far ovlCall_b4f(int16 spr);    /* overlay 1000:0b4f sprite blit */

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
    int16 id;

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

/* seg000:0x3218 — pilot detail-panel drawer. When the item's flag word at
 * +0x30 carries 0x400, clears the item's detail rect (item fields +0x14..1A)
 * and draws the pilot block: header ("Next mission assigned to:" or "Career
 * Record of:" chosen by pilot-record f4e), rank+name, missions/career/last/
 * best stats, then one icon+label per medal the pilot holds. Medal icons
 * blit through the sprite descriptor the item's f28 points at (+2=sprite id,
 * +8=x). The IDA/map extent ends at 0x3253 because the reachable stream
 * stopped at the early-out jump; the real body runs to 0x3885. */
void sub_13218(void *t, int16 i, int16 *pd) {
    int8 *base = (int8 *)t;
    int16 id;
    struct MsnRec *rec;
    int16 *mdesc;
    int16 w, x;
    char buf[0x1E];

    id = i * 0x32;
    if (!(*(uint16 *)(base + id + 0x30) & 0x400))
        return;
    sub_14622(pd, *(int16 *)(base + id + 0x14), *(int16 *)(base + id + 0x16),
              *(int16 *)(base + id + 0x18), *(int16 *)(base + id + 0x1A));
    pd[2] = 8;
    rec = &word_29D54[i];
    if (rec->f4e == 0)
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x626));
    else
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x640));
    w = sub_13E38(pd, (char *)f19_stSpace.m_strB96A);
    sub_13B76(pd, (char *)f19_stSpace.m_strB96A, (0x140 - w) >> 1, 0x1A);
    pd[2] = 9;
    sub_15120((char *)f19_stSpace.m_strB96A,
              (char *)f19_dsegAt(*(uint16 *)f19_dsegAt(0x286 + rec->f20 * 2)));
    sub_15189((char *)f19_stSpace.m_strB96A, rec->name);
    sub_13B76(pd, (char *)f19_stSpace.m_strB96A, 0x2D, 0x25);
    sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x652));
    sub_13FB3(rec->f36, buf);
    sub_15189((char *)f19_stSpace.m_strB96A, buf);
    sub_13B76(pd, (char *)f19_stSpace.m_strB96A, 0x2D, 0x2D);
    sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x663));
    sub_13E7C(rec->u32.coord, buf);
    sub_15189((char *)f19_stSpace.m_strB96A, buf);
    sub_13B76(pd, (char *)f19_stSpace.m_strB96A, 0xA5, 0x2D);
    sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x672));
    sub_13E7C((long)(uint16)rec->f30, buf);
    sub_15189((char *)f19_stSpace.m_strB96A, buf);
    sub_13B76(pd, (char *)f19_stSpace.m_strB96A, 0x2D, 0x35);
    sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x681));
    sub_13E7C((long)(uint16)rec->f2e, buf);
    sub_15189((char *)f19_stSpace.m_strB96A, buf);
    sub_13B76(pd, (char *)f19_stSpace.m_strB96A, 0xA5, 0x35);
    x = 0x2D;
    mdesc = (int16 *)f19_dsegAt(*(uint16 *)(base + id + 0x28));
    if (rec->f24 != 0) {
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x690));
        if (rec->f24 > 1) {
            sub_15189((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x693));
            sub_13FB3(rec->f24, buf);
            sub_15189((char *)f19_stSpace.m_strB96A, buf);
        }
        w = sub_13E38(pd, (char *)f19_stSpace.m_strB96A);
        sub_13B76(pd, (char *)f19_stSpace.m_strB96A, ((0x22 - w) >> 1) + x, 0x3D);
        mdesc[1] = 0x55;
        mdesc[4] = x;
        ovlCall_b4f(*(uint16 *)(base + id + 0x28));
        x += 0x27;
    }
    if (rec->f26 != 0) {
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x695));
        if (rec->f26 > 1) {
            sub_15189((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x699));
            sub_13FB3(rec->f26, buf);
            sub_15189((char *)f19_stSpace.m_strB96A, buf);
        }
        w = sub_13E38(pd, (char *)f19_stSpace.m_strB96A);
        sub_13B76(pd, (char *)f19_stSpace.m_strB96A, ((0x22 - w) >> 1) + x, 0x3D);
        mdesc[1] = 0x77;
        mdesc[4] = x;
        ovlCall_b4f(*(uint16 *)(base + id + 0x28));
        x += 0x27;
    }
    if (rec->f28 != 0) {
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x69B));
        if (rec->f28 > 1) {
            sub_15189((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x69E));
            sub_13FB3(rec->f28, buf);
            sub_15189((char *)f19_stSpace.m_strB96A, buf);
        }
        w = sub_13E38(pd, (char *)f19_stSpace.m_strB96A);
        sub_13B76(pd, (char *)f19_stSpace.m_strB96A, ((0x22 - w) >> 1) + x, 0x3D);
        mdesc[1] = 0x99;
        mdesc[4] = x;
        ovlCall_b4f(*(uint16 *)(base + id + 0x28));
        x += 0x27;
    }
    if (rec->f2a != 0) {
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x6A0));
        if (rec->f2a > 1) {
            sub_15189((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x6A4));
            sub_13FB3(rec->f2a, buf);
            sub_15189((char *)f19_stSpace.m_strB96A, buf);
        }
        w = sub_13E38(pd, (char *)f19_stSpace.m_strB96A);
        sub_13B76(pd, (char *)f19_stSpace.m_strB96A, ((0x22 - w) >> 1) + x, 0x3D);
        mdesc[1] = 0xBB;
        mdesc[4] = x;
        ovlCall_b4f(*(uint16 *)(base + id + 0x28));
        x += 0x27;
    }
    if (rec->f22 == 1) {
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x6A6));
        w = sub_13E38(pd, (char *)f19_stSpace.m_strB96A);
        sub_13B76(pd, (char *)f19_stSpace.m_strB96A, ((0x22 - w) >> 1) + x, 0x3D);
        mdesc[1] = 0x33;
        mdesc[4] = x;
        ovlCall_b4f(*(uint16 *)(base + id + 0x28));
        x += 0x27;
    }
    if (rec->f2c == 1) {
        sub_15120((char *)f19_stSpace.m_strB96A, (char *)f19_dsegAt(0x6A9));
        w = sub_13E38(pd, (char *)f19_stSpace.m_strB96A);
        sub_13B76(pd, (char *)f19_stSpace.m_strB96A, ((0x22 - w) >> 1) + x, 0x3D);
        mdesc[1] = 0xDD;
        mdesc[4] = x;
        ovlCall_b4f(*(uint16 *)(base + id + 0x28));
    }
}
