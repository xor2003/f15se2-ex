/* START.EXE — theater/mission menu screens (seg000:0xa68c+). f15 stmissn.c
 * lineage: draws a vertical item list via sub_13B50 rows, highlights the
 * current selection with sub_10924, then f19_sub_10AE8 runs the select widget
 * and the result lands in gameData->theater; byte_2C160 picks the next page. */
#include "f19.h"

#define word_2B386 (*(uint16 *)(f19_dseg + 0xB386))
#define word_2CA48 (*(int16 *)(f19_dseg + 0xCA48))
#define word_25F7C (*(uint16 *)(f19_dseg + 0x5F7C))
#define word_25D30 (*(uint16 *)(f19_dseg + 0x5D30))
#define word_25D48 (*(uint16 *)(f19_dseg + 0x5D48))
#define word_2D26E (*(int16 *)(f19_dseg + 0xD26E))
#define selInitTab ((int16 *)(f19_dseg + 0x5D4A))
#define menuSelTab ((struct MenuSelBlk *)(f19_dseg + 0x5EA4))
#define menuSelTab2 ((struct MenuSelBlk *)(f19_dseg + 0x5FDC))
#define flag_29948 (*(int16 *)(f19_dseg + 0x9948))
#define byte_2C160 (*(uint8 *)(f19_dseg + 0xC160))
#define word_2D276 (*(uint16 *)(f19_dseg + 0xD276))
#define word_2C7D2 (*(int16 *)(f19_dseg + 0xC7D2))
#define word_26050 (*(uint16 *)(f19_dseg + 0x6050))
#define word_25F94 (*(uint16 *)(f19_dseg + 0x5F94))
#define word_25FAC (*(uint16 *)(f19_dseg + 0x5FAC))
#define menuSelTab3 ((struct MenuSelBlk *)(f19_dseg + 0x6202))
#define word_2D272 (*(int16 *)(f19_dseg + 0xD272))
#define initTab2 ((int16 *)(f19_dseg + 0x6094))
#define byte_2D060 (*(uint8 *)(f19_dseg + 0xD060))
#define word_2D06C (*(int16 *)(f19_dseg + 0xD06C))
#define word_2D2CA (*(int16 *)(f19_dseg + 0xD2CA))
#define word_2D2CC (*(int16 *)(f19_dseg + 0xD2CC))
#define word_262A8 (*(uint16 *)(f19_dseg + 0x62A8))
#define word_2607A (*(uint16 *)(f19_dseg + 0x607A))
#define word_26092 (*(uint16 *)(f19_dseg + 0x6092))
#define menuSelTab4 ((struct MenuSelBlk *)(f19_dseg + 0x6308))
#define word_263AE (*(uint16 *)(f19_dseg + 0x63AE))
#define word_262C0 (*(uint16 *)(f19_dseg + 0x62C0))
#define word_262D8 (*(uint16 *)(f19_dseg + 0x62D8))
#define menuSelTab5 ((struct MenuSelBlk *)(f19_dseg + 0x640C))
#define word_26480 (*(uint16 *)(f19_dseg + 0x6480))
#define word_263C6 (*(uint16 *)(f19_dseg + 0x63C6))
#define rtcEnabled (*(int16 *)(f19_dseg + 0xBB74))
#define word_2A0C4 (*(int16 *)(f19_dseg + 0xA0C4))
#define word_2BE4A (*(int16 *)(f19_dseg + 0xBE4A))
#define byte_2B388 (*(int8 *)(f19_dseg + 0xB388))
#define byte_298F0 (*(uint8 *)(f19_dseg + 0x98F0))
#define byte_2CA62 (*(uint8 *)(f19_dseg + 0xCA62))
#define byte_2CA6A (*(uint8 *)(f19_dseg + 0xCA6A))
#define word_2C7D4 (*(int16 *)(f19_dseg + 0xC7D4))
#define word_2D2C8 (*(int16 *)(f19_dseg + 0xD2C8))
#define word_26572 (*(uint16 *)(f19_dseg + 0x6572))
#define word_2655A (*(uint16 *)(f19_dseg + 0x655A))
#define word_26574 ((int16 *)(f19_dseg + 0x6574))
#define word_26592 (*(int16 *)(f19_dseg + 0x6592))
#define word_265B2 (*(int16 *)(f19_dseg + 0x65B2))
#define word_265B6 (*(int16 *)(f19_dseg + 0x65B6))
#define word_265D6 (*(int16 *)(f19_dseg + 0x65D6))
#define word_26676 (*(int16 *)(f19_dseg + 0x6676))
#define word_26696 (*(int16 *)(f19_dseg + 0x6696))
#define word_2681E (*(uint16 *)(f19_dseg + 0x681E))
#define word_26820 ((uint16 *)(f19_dseg + 0x6820))
#define menuSelTab6 ((struct MenuSelBlk *)(f19_dseg + 0x66E2))
#define word_2CA6C (*(int16 *)(f19_dseg + 0xCA6C))
#define word_2B948 (*(int16 *)(f19_dseg + 0xB948))
#define word_2B95A (*(int16 *)(f19_dseg + 0xB95A))
#define byte_2C976 (*(uint8 *)(f19_dseg + 0xC976))
#define byte_2C977 (*(uint8 *)(f19_dseg + 0xC977))
#define ringMode (*(uint8 *)(f19_dseg + 0x9922))
#define byte_20A1A timerCounter   /* shared/timer.c 60 Hz tick — was PIT-ISR cell */
#define unitMarksOn (*(uint8 *)(f19_dseg + 0x98E8))
#define tileMarksOn (*(uint8 *)(f19_dseg + 0x98E8))
#define tileMarkMap ((int8 *)(f19_dseg + 0xB842))
#define objectCount (*(int16 *)(f19_dseg + 0xC978))
#define objectActive ((uint8 *)(f19_dseg + 0xD278))
#define f19_worldObjects ((WorldObject *)(f19_dseg + 0xB390))
#define word_2D2C6 (*(int16 *)(f19_dseg + 0xD2C6))
#define word_22324 (*(int16 *)(f19_dseg + 0x2324))
#define word_2C972 (*(int16 *)(f19_dseg + 0xC972))
#define word_2C974 (*(int16 *)(f19_dseg + 0xC974))
#define byte_298E9 (*(uint8 *)(f19_dseg + 0x98E9))
#define byte_2B384 (*(uint8 *)(f19_dseg + 0xB384))
#define word_2D064 (*(int16 *)(f19_dseg + 0xD064))
#define word_2D270 (*(int16 *)(f19_dseg + 0xD270))
#define word_25B2C (*(uint16 *)(f19_dseg + 0x5B2C))
#define word_25B44 (*(uint16 *)(f19_dseg + 0x5B44))
#define word_25CF6 (*(uint16 *)(f19_dseg + 0x5CF6))
#define word_25CE6 (*(int16 *)(f19_dseg + 0x5CE6))
#define word_25B46 ((int16 *)(f19_dseg + 0x5B46))
#define word_25B64 (*(int16 *)(f19_dseg + 0x5B64))
#define word_25B84 (*(int16 *)(f19_dseg + 0x5B84))
#define word_25BA4 (*(int16 *)(f19_dseg + 0x5BA4))
#define word_25BC4 (*(int16 *)(f19_dseg + 0x5BC4))
#define word_25BE4 (*(int16 *)(f19_dseg + 0x5BE4))
#define word_25C04 (*(int16 *)(f19_dseg + 0x5C04))
#define word_25C24 (*(int16 *)(f19_dseg + 0x5C24))
#define word_25C44 (*(int16 *)(f19_dseg + 0x5C44))
#define word_25C64 (*(int16 *)(f19_dseg + 0x5C64))
#define word_25C84 (*(int16 *)(f19_dseg + 0x5C84))
#define word_2542C ((int16 *)(f19_dseg + 0x542C))
#define word_25454 ((uint16 *)(f19_dseg + 0x5454))
#define word_2547C ((int16 *)(f19_dseg + 0x547C))
#define word_25480 ((int16 *)(f19_dseg + 0x5480))
#define word_25014 (*(uint16 *)(f19_dseg + 0x5014))
#define word_24FFC (*(uint16 *)(f19_dseg + 0x4FFC))
#define word_2542A (*(uint16 *)(f19_dseg + 0x542A))
#define word_25016 (*(int16 *)(f19_dseg + 0x5016))
#define word_25034 (*(int16 *)(f19_dseg + 0x5034))
#define word_25064 (*(int16 *)(f19_dseg + 0x5064))
#define byte_29B50 (*(uint8 *)(f19_dseg + 0x9B50))
#define byte_2D06A (*(uint8 *)(f19_dseg + 0xD06A))
#define blinkTimer (*(uint8 *)(f19_dseg + 0xA1C))
#define word_2C968 (*(int16 *)(f19_dseg + 0xC968))
#define word_2C144 (*(int16 *)(f19_dseg + 0xC144))
#define word_2CA70 ((int16 *)(f19_dseg + 0xCA70))
#define word_2B38E ((struct ObjD *)(f19_dseg + 0xB38E))
#define word_23E26 ((struct Attr14 *)(f19_dseg + 0x3E26))
#define word_23E28 (*(int16 *)(f19_dseg + 0x3E28))
#define word_241C8 ((struct Rec18 *)(f19_dseg + 0x41C8))
#define strTab14 ((char (*)[0x0E])(f19_dseg + 0x3E1E))
#define strTab32 ((char (*)[0x20])(f19_dseg + 0x3F60))
#define word_29D54 ((struct MsnRec *)(f19_dseg + 0x9D54))
#define word_25722 ((int16 (*)[0x19])(f19_dseg + 0x5722))
#define word_256E2 (*(int16 *)(f19_dseg + 0x56E2))
#define word_256FA (*(int16 *)(f19_dseg + 0x56FA))
#define word_256FC (*(int16 *)(f19_dseg + 0x56FC))
#define word_2591A (*(int16 *)(f19_dseg + 0x591A))
#define word_298D2 (*(int16 *)(f19_dseg + 0x98D2))
#define word_2B38A (*(int16 *)(f19_dseg + 0xB38A))
#define word_29B52 (*(uint16 *)(f19_dseg + 0x9B52))
#define word_2591C ((uint16 *)(f19_dseg + 0x591C))
#define word_2592A ((uint16 *)(f19_dseg + 0x592A))
#define byte_212BA (*(uint8 *)(f19_dseg + 0x12BA))
#define byte_2D06B (*(uint8 *)(f19_dseg + 0xD06B))
#define byte_298F6 (*(uint8 *)(f19_dseg + 0x98F6))
#define sprParmsTab ((int8 *)f19_dseg)
extern struct GD *gameData;                 /* far ptr dseg:0x991c */


struct GD { uint16 f0;                      /* 0x00 next-page (mul'd index) */
            int8 pad0[0x1E];
            int16 f20,f22,f24,f26,f28,f2a,f2c,f2e,f30;
            union { struct { int16 f32, f34; } w; long coord; } u32;
            int16 f36;
            int16 theater;                  /* 0x38 */
            int16 isCampaignMission;        /* 0x3a */
            int16 flags3c;                  /* 0x3c */
            int16 flags3e;                  /* 0x3e */
            int16 flags40;                  /* 0x40 */
            int16 f42,f44,f46,f48,f4a,f4c,f4e; };

extern void f19_sub_108B7(void);                /* seg000:0x08b7 screen reset */
extern void sub_14622(void *o, int16 a, int16 b, int16 c, int16 d); /* clearRect */
extern void sub_13B76(void *o, char *s, int16 x, int16 y); /* drawStringAt */
struct MenuRow { int16 name, yoff; };
extern void sub_13B50(int16 *page, struct MenuRow r, int16 c, int16 d);
extern void sub_10924(char *tab, int16 sel, int16 cnt, int16 x, int16 y, ...);
struct SelRow;
struct SelRow;
extern int16 f19_sub_10AE8(struct SelRow *tab, int16 namesOfs, int16 cnt,
                              int16 a4, int16 *pd, int16 a6 = 0);
extern void sub_14E9C(void);
extern void sub_125EA(void);
extern void sub_14EDA(void);
extern void sub_14584(void *o, int16 a, int16 b, int16 c, int16 d);
extern void sub_14A5F(char *s, int16 v);
extern void sub_14746(char *s, int16 a, int16 b);
extern int16 far gfx_unknown2b(int16 v);        /* overlay slot 0x2b */

#define word_2CA46 (*(uint16*)(f19_dseg + 0xCA46))
#define word_207EA ((uint8 *)f19_farAt(0x07EA))
struct MenuSelBlk { int16 sel; int16 pad[24]; };
#define word_20822 ((uint8 *)f19_farAt(0x0822))
#define word_20862 ((uint8 *)f19_farAt(0x0862))
#define word_20896 ((uint8 *)f19_farAt(0x0896))
#define word_208BA ((uint8 *)f19_farAt(0x08BA))
extern void sub_1685C(int16 v);

/* f19_sub_1B452 (mission-setup screen) externs */
#define word_209B6(i) ((char *)f19_farAt(0x09B6 + (i) * 4))
typedef struct {                            /* f19_worldObjects: stride 0x10 */
    int16 x_coord, y_coord;                 /* 0x00, 0x02 */
    int16 pad4;                             /* 0x04 */
    int16 targetFlags;                      /* 0x06 */
    int16 pad8[4];                          /* 0x08 */
} WorldObject;
struct PgParms { int8 pad[0x22]; int16 f22; int8 pad1[0x0C]; int16 f30;
                 int8 pad2[0x40]; int16 f72; };
extern struct PgParms far *word_2D066;  /* UNMAPPED */
/* f19_sub_1A68C (main select screen) externs */
#define word_209AA ((uint8 *)f19_farAt(0x09AA))
extern void sub_14BEE(int16 a, int16 b);
extern void sub_161CC(int16 a, int16 b, int16 c);
extern void sub_15120(char *d, char *s);
extern int16 sub_13E38(int16 *p, char *s);
extern void far ovlCall_c4e(int16 v);       /* overlay 1000:0c4e */
extern void far ovlCall_bea(int16 v);       /* overlay 1000:0bea */
/* f19_sub_18F12 (briefing screen) externs */
extern void sub_14ACB(char *s, int16 v, long x);
extern void sub_1513B(char far *d, char *s);
extern void far ovlCall_bc7(int16 *pg, int16 a, int16 b, int16 c,
                            int16 d, int16 e, int16 f);
extern void sub_15152(char *d, const char far *s);  /* far-src strcpy */
/* f19_sub_12754 (object detail panel) externs */
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
/* f19_sub_193EE (roster screen) externs */
struct MsnRec {                             /* dseg:0x9d54, stride 0x50 */
    int16 f0;                               /* record id */
    char  name[0x1E];                       /* mission name */
    int16 f20, f22, f24, f26, f28, f2a, f2c, f2e, f30;
    union { struct { int16 f32, f34; } w; long coord; } u32;
    int16 f36, f38, f3a, f3c, f3e, f40;
    int16 f42, f44, f46, f48, f4a, f4c, f4e;
};
extern int16 far *word_2B942;  /* UNMAPPED */
extern void sub_1A4FB(void), sub_1A376(void), sub_10882(void);
extern void sub_15189(char *d, char *s);
extern void sub_13D15(int16 *pg, char *s, int16 a, int16 b, int16 c,
                      int16 d);
extern void sub_13E7C(long v, char *buf);
extern void sub_13FB3(int16 n, char *b);    /* seg000:0x3fb3 numToStr */
extern void sub_146E3(void), sub_1DCAC(int16);
extern char *f19_pilotNameInput(int16 *page, int16 x, int16 y, int16 maxLen,
                            int16 a5, int16 a6);
extern void f19_loadSpriteRes(const char *n, int16 sel);
extern int16 f19_randMul(uint16 n);
extern void f19_selectNextObject(void);
extern void f19_selectNextUnit(void);
extern void f19_missionGenerate(void);
extern void f19_drawRoutePath(void);
extern void f19_drawThreatRings(void);
extern void f19_drawSiteMarkers(void);
extern void f19_drawUnitMarkers(void);
extern void f19_drawTileMarkers(void);
extern int16 far ovlCall_b4f(int16 h);       /* overlay 1000:0b4f */
extern int16 far ovlCall_c53(void);         /* overlay 1000:0c53 */
extern void far ovlCall_c58(void);          /* overlay 1000:0c58 */
extern void far ovlCall_c8a(void);          /* overlay 1000:0c8a */
extern int16 far ovlCall_ccb(int16 v);      /* overlay 1000:0ccb poll */

/* seg000:0x8f12 — briefing/mission-summary screen: builds the objective
 * list (two word_25454 row runs), picks a random mission index, runs the
 * select widget, then branches to the accept or next-page arm. The shipped
 * EN binary is patched: the second f19_sub_10AE8 call site was nop'd (3 bytes)
 * and `if (res == choice)` became `res = choice` + unconditional jumps,
 * forcing the accept arm — the map marks both patch sites U so mzdiff
 * skips them; the C below carries the unpatched semantics. */

void f19_sub_18F12(void) {
    int16 low;                  /* [bp-2]  */
    int16 fl;                   /* [bp-4]  */
    int16 hn;                   /* [bp-6]  */
    int16 v;                    /* [bp-8]  */
    int16 t2;                   /* [bp-0a] */
    int16 pad1;                 /* [bp-0c] */
    int16 i;                    /* [bp-0e] */
    int16 y;                    /* [bp-10] */
    int16 w2;                   /* [bp-12] */
    int16 res;                  /* [bp-14] */
    int16 w3;                   /* [bp-16] */
    int16 choice;               /* [bp-18] */
    int16 len;                  /* [bp-1a] */

    if (word_2D066->f22 == 1) {
        byte_2C160 = 8;
        return;
    }
    choice = f19_randMul(0x14);
    word_2B386 = 0x992;
    word_2CA46 = 0x98E;
    word_2CA48 = 0x996;
    byte_29B50 = 1;
    byte_298F0 = 1;
    gfx_unknown2b(0);
    switch (word_2C7D4) {
    case 0:
        sub_14746((char *)(f19_dseg + 0x4F0A), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        sub_14ACB((char *)(f19_dseg + 0x4F15), word_2D26E, (long)word_2542C[choice]);
        v = word_2D26E;
        break;
    case 1:
        sub_14ACB((char *)(f19_dseg + 0x4F1F), word_2D2C8, (long)word_2542C[choice]);
        v = word_2D2C8;
        if (byte_2D06A == 0) {
            gfx_unknown2b(0);
            sub_14746((char *)(f19_dseg + 0x4F29), word_2C972, word_2C974);
            sub_14BEE(word_2D064, word_2D06C);
            byte_2D06A = 1;
        }
        break;
    case 2:
        sub_14746((char *)(f19_dseg + 0x4F34), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        sub_14ACB((char *)(f19_dseg + 0x4F3F), word_2D2C8, (long)word_2542C[choice]);
        v = word_2D2C8;
        break;
    }
    ovlCall_c53();
    ovlCall_bea(word_2D06C);
    word_25016 = v;
    ovlCall_b4f(word_25034);
    y = 0x6F;
    for (i = 0; i < 0xA; i++) {
        sub_13B76((int16 *)(f19_dseg + word_24FFC), (char *)(f19_dseg + word_25454[i]), 0x2E, y);
        y += 8;
    }
    y = 0x6F;
    for (i = 0xA; i < 0x14; i++) {
        sub_13B76((int16 *)(f19_dseg + word_24FFC), (char *)(f19_dseg + word_25454[i]), 0x9F, y);
        y += 8;
    }
    sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x4F49));
    len = sub_13E38((int16 *)(f19_dseg + word_24FFC), (char *)(f19_dseg + 0xB96A));
    sub_13B76((int16 *)(f19_dseg + word_24FFC), (char *)(f19_dseg + 0xB96A), (uint16)(uint16)(0x140 - len) >> 1, 0x15);
    sub_13B76((int16 *)(f19_dseg + word_25014), (char *)(f19_dseg + 0x4F66), 0x46, 0xC1);
    ovlCall_c8a();
    ovlCall_c4e(1);
    f19_sub_108B7();
    sub_14E9C();
    word_25064 = 2;
    res = 0;
    sub_10924((char *)(f19_dseg + 0x5036), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), 0x64, 0x70,
              (int16 *)(f19_dseg + word_24FFC));
    res = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x5036), word_2CA48, *(uint8 *)f19_farAt(word_2CA46),
                    word_2542A, (int16 *)(f19_dseg + word_24FFC), 0);
    sub_14622((int16 *)(f19_dseg + word_25014), 0x1E, 0xC1, 0x12C, 0xC6);
    fl = 0;
    if (res == choice)
        goto ACCEPT;
    goto NEXT;

ACCEPT:
    ((int16 *)(f19_dseg + word_25014))[2] = 2;
    sub_13B76((int16 *)(f19_dseg + word_25014), (char *)(f19_dseg + 0x4F94), 0x6E, 0xC1);
    ovlCall_c8a();
    byte_2C160 = 8;
    word_2D066->f22 = 1;
    t2 = 0;
    while (t2 < 8) {
        if (blinkTimer <= 8)
            continue;
        blinkTimer = 0;
        {
            register int16 sv = word_2547C[fl];
            hn = sv >> 4;
            low = sv & 0xF;
        }
        ovlCall_bc7((int16 *)(f19_dseg + word_25014), 0x64, 0xC1, 0x12C, 0xC6, hn, low);
        fl = (fl + 1) & 1;
        t2++;
    }
    sub_14EDA();
    return;

NEXT:
    ((int16 *)(f19_dseg + word_25014))[2] = 4;
    sub_13B76((int16 *)(f19_dseg + word_25014), (char *)(f19_dseg + 0x4FAB), 0x46, 0xC1);
    ovlCall_c8a();
    byte_2C160 = 0xF;
    gameData->f0 = 0xA;
    sub_1513B((char *)gameData + 2, (char *)(f19_dseg + 0x4FDC));
    gameData->f20 = 0;
    gameData->f22 = 0;
    gameData->f24 = 0;
    gameData->f26 = 0;
    gameData->f28 = 0;
    gameData->f2a = 0;
    gameData->f2c = 0;
    gameData->f2e = 0;
    gameData->f30 = 0;
    gameData->u32.w.f32 = gameData->u32.w.f34 = 0;
    gameData->f36 = 0;
    gameData->theater = 0;
    gameData->isCampaignMission = 0;
    gameData->flags3e = 0;
    gameData->flags40 = 0;
    gameData->flags3c = 3;
    gameData->f42 = 4;
    gameData->f44 = 0;
    gameData->f46 = 0;
    gameData->f48 = 0;
    gameData->f4a = 0;
    gameData->f4c = 0;
    gameData->f4e = 0;
    word_2D066->f30 = 1;
    word_2D066->f22 = 0;
    t2 = 0;
    do {
        if (blinkTimer > 0xC) {
            register int16 sv = word_25480[fl];
            hn = sv >> 4;
            low = sv & 0xF;
            ovlCall_bc7((int16 *)(f19_dseg + word_25014), 0x46, 0xC1, 0x12C, 0xC6, hn, low);
            fl = (fl + 1) & 1;
            t2++;
        }
    } while (t2 < 8);
    sub_14EDA();
}

/* seg000:0x93ee — mission roster screen: if a reload is flagged, copy
 * missionList[word_29B52] into gameData (else write gameData back into the
 * record), load the word_2C7D4 resources, draw the 10-row roster (name /
 * coord / status / kind columns), then loop the select widget: sel<=9
 * switches on missionList[sel].f4e for the info panels, sel>9 creates a
 * new record via f19_pilotNameInput, and case 0 / guard bytes leave the loop
 * and copy the final record into gameData. */
void f19_sub_193EE(void) {
    char  *nameResult;              /* [bp-2]  */
    int16  a, b, c;                 /* [bp-4/6/8] unused */
    int16  v;                       /* [bp-0a] */
    int16  h;                       /* [bp-0e] unused */
    int16  looping;                 /* [bp-0c] */
    int16  y;                       /* [bp-12] */
    int16  i;                       /* [bp-10] */
    int16  j;                       /* [bp-14] unused */
    union { int16 sel; char sb[2]; } selw;  /* [bp-16] */
    char   obuf[34];                /* [bp-18..-38] unused */
    int16  len;                     /* [bp-3a] */

    sub_1A4FB();
    if (gameData->f0 == 0xA)
        *word_2B942 = 1;
    if (*word_2B942 == 1) {
        gameData->f0 = word_29D54[word_29B52].f0;
        sub_1513B((char *)gameData + 2, word_29D54[word_29B52].name);
        gameData->f20 = word_29D54[word_29B52].f20;
        gameData->f22 = word_29D54[word_29B52].f22;
        gameData->f24 = word_29D54[word_29B52].f24;
        gameData->f26 = word_29D54[word_29B52].f26;
        gameData->f28 = word_29D54[word_29B52].f28;
        gameData->f2a = word_29D54[word_29B52].f2a;
        gameData->f2c = word_29D54[word_29B52].f2c;
        gameData->f2e = word_29D54[word_29B52].f2e;
        gameData->f30 = word_29D54[word_29B52].f30;
        gameData->u32.coord = word_29D54[word_29B52].u32.coord;
        gameData->f36 = word_29D54[word_29B52].f36;
        gameData->theater = word_29D54[word_29B52].f38;
        gameData->isCampaignMission = word_29D54[word_29B52].f3a;
        gameData->flags3c = word_29D54[word_29B52].f3c;
        gameData->flags3e = word_29D54[word_29B52].f3e;
        gameData->flags40 = word_29D54[word_29B52].f40;
        gameData->f42 = word_29D54[word_29B52].f42;
        gameData->f44 = word_29D54[word_29B52].f44;
        gameData->f46 = word_29D54[word_29B52].f46;
        gameData->f48 = word_29D54[word_29B52].f48;
        gameData->f4a = word_29D54[word_29B52].f4a;
        gameData->f4c = word_29D54[word_29B52].f4c;
        gameData->f4e = word_29D54[word_29B52].f4e;
        *word_2B942 = 0;
    } else {
        word_29D54[gameData->f0].f0 = gameData->f0;
        sub_15152(word_29D54[gameData->f0].name, (char *)gameData + 2);
        word_29D54[gameData->f0].f20 = gameData->f20;
        word_29D54[gameData->f0].f22 = gameData->f22;
        word_29D54[gameData->f0].f24 = gameData->f24;
        word_29D54[gameData->f0].f26 = gameData->f26;
        word_29D54[gameData->f0].f28 = gameData->f28;
        word_29D54[gameData->f0].f2a = gameData->f2a;
        word_29D54[gameData->f0].f2c = gameData->f2c;
        word_29D54[gameData->f0].f2e = gameData->f2e;
        word_29D54[gameData->f0].f30 = gameData->f30;
        word_29D54[gameData->f0].u32.coord = gameData->u32.coord;
        word_29D54[gameData->f0].f36 = gameData->f36;
        word_29D54[gameData->f0].f38 = gameData->theater;
        word_29D54[gameData->f0].f3a = gameData->isCampaignMission;
        word_29D54[gameData->f0].f3c = gameData->flags3c;
        word_29D54[gameData->f0].f3e = gameData->flags3e;
        word_29D54[gameData->f0].f40 = gameData->flags40;
        word_29D54[gameData->f0].f42 = gameData->f42;
        word_29D54[gameData->f0].f44 = gameData->f44;
        word_29D54[gameData->f0].f46 = gameData->f46;
        word_29D54[gameData->f0].f48 = gameData->f48;
        word_29D54[gameData->f0].f4a = gameData->f4a;
        word_29D54[gameData->f0].f4c = gameData->f4c;
        word_29D54[gameData->f0].f4e = gameData->f4e;
    }
    if (byte_29B50 == 1) {
        sub_14622((void *)(intptr_t)word_256E2, 0x2D, 0x15, 0x113, 0xC7);
        ovlCall_c8a();
        ovlCall_c4e(0);
        switch (word_2C7D4) {
        case 0:
            gfx_unknown2b(1);
            f19_loadSpriteRes((char *)(f19_dseg + 0x54C8), word_2D26E);
            v = word_2D26E;
            break;
        case 1:
            v = word_2D272;
            if (byte_298F6 == 0) {
                gfx_unknown2b(0xF);
                f19_loadSpriteRes((char *)(f19_dseg + 0x54D3), word_2D272);
                byte_298F6 = 1;
            }
            break;
        case 2:
            v = word_2D272;
            break;
        }
    } else {
        switch (word_2C7D4) {
        case 0:
            gfx_unknown2b(0);
            sub_14746((char *)(f19_dseg + 0x54DE), word_2C972, word_2C974);
            sub_14BEE(word_2D064, word_2D06C);
            gfx_unknown2b(1);
            f19_loadSpriteRes((char *)(f19_dseg + 0x54E9), word_2D26E);
            v = word_2D26E;
            ovlCall_c53();
            ovlCall_bea(word_2D06C);
            ovlCall_c4e(0);
            break;
        case 1:
            v = word_2D272;
            if (byte_2D06A == 0) {
                gfx_unknown2b(0);
                sub_14746((char *)(f19_dseg + 0x54F4), word_2C972, word_2C974);
                sub_14BEE(word_2D064, word_2D06C);
                byte_2D06A = 1;
            }
            if (byte_298F6 == 0) {
                gfx_unknown2b(0xF);
                f19_loadSpriteRes((char *)(f19_dseg + 0x54FF), word_2D272);
                byte_298F6 = 1;
            }
            ovlCall_c53();
            ovlCall_bea(word_2D06C);
            ovlCall_c4e(0);
            break;
        case 2:
            gfx_unknown2b(0);
            sub_14746((char *)(f19_dseg + 0x550A), word_2C972, word_2C974);
            sub_14BEE(word_2D064, word_2D06C);
            v = word_2D272;
            ovlCall_c53();
            ovlCall_bea(word_2D06C);
            ovlCall_c4e(0);
            break;
        }
    }
    sub_1A376();
    word_256FC = v;
    *(int16 *)(sprParmsTab + word_256E2 + 4) = 0;
    *(int16 *)(sprParmsTab + word_256E2 + 0xC) = 4;
    sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0x5515), 0xB1, 0x50);
    sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0x551C), 0xB6, 0x58);
    *(int16 *)(sprParmsTab + word_256E2 + 4) = 6;
    y = 0x60;
    for (i = 0; i < 0xA; i++) {
        sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + word_2591C[word_29D54[i].f20]));
        sub_15189((char *)(f19_dseg + 0xB96A), word_29D54[i].name);
        sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0x23, y);
        sub_13E7C(word_29D54[i].u32.coord, (char *)(f19_dseg + 0xB96A));
        sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0xB4, y);
        sub_13FB3(word_29D54[i].f36, (char *)(f19_dseg + 0xB96A));
        sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0xE0, y);
        sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + word_2592A[word_29D54[i].f4e]));
        len = sub_13E38((int16 *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A));
        sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), ((uint16)(0x23 - len) >> 1) + 0xF5, y);
        y += 8;
    }
    *(int16 *)(sprParmsTab + word_256E2 + 0xC) = 1;
    *(int16 *)(sprParmsTab + word_256E2 + 4) = 0;
    sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5533));
    len = sub_13E38((int16 *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A));
    sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0x23, 0x54);
    f19_sub_108B7();
    sub_14E9C();
    word_25722[gameData->f0][0x13] = 2;
    selw.sel = gameData->f0;
    looping = 1;
    word_2B38A = 1;
    while (looping) {
        *(int16 *)(sprParmsTab + word_256FA + 0xC) = 4;
        *(int16 *)(sprParmsTab + word_256FA + 4) = 9;
        sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5543));
        len = sub_13E38((int16 *)(intptr_t)word_256FA, (char *)(f19_dseg + 0xB96A));
        sub_13B76((void *)(intptr_t)word_256FA, (char *)(f19_dseg + 0xB96A), (uint16)(0x140 - len) >> 1, 0xB5);
        sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5576));
        *(int16 *)(sprParmsTab + word_256FA + 0xC) = 3;
        *(int16 *)(sprParmsTab + word_256FA + 4) = 1;
        len = sub_13E38((int16 *)(intptr_t)word_256FA, (char *)(f19_dseg + 0xB96A));
        sub_13B76((void *)(intptr_t)word_256FA, (char *)(f19_dseg + 0xB96A), (uint16)(0x140 - len) >> 1, 0xBE);
        sub_10924((char *)(f19_dseg + 0x571A), word_298D2, 0xA, 0x64,
                  (selw.sel << 3) + 0x60, (int16 *)(f19_dseg + word_256E2));
        selw.sel = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x571A), word_298D2, 0xA, word_2591A,
                        (int16 *)(f19_dseg + word_256E2));
        if (byte_212BA == 0 && byte_2D06B == 0) {
            if (selw.sel <= 9) {
                switch (word_29D54[selw.sel].f4e) {
                case 0:
                    looping = 0;
                    break;
                case 1:
                    sub_14622((void *)(intptr_t)word_256FA, 0x2C, 0x1A, 0x113, 0x4F);
                    sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x55A6));
                    sub_15189((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x55D4));
                    sub_13D15((int16 *)(intptr_t)word_256FA, (char *)(f19_dseg + 0xB96A), 0xCE, 0x41,
                              0x28, 8);
                    word_25722[selw.sel][0x13] = 2;
                    blinkTimer = 0;
                    while (blinkTimer < 0xC8) ;
                    sub_14622((void *)(intptr_t)word_256FA, 0x2C, 0x1A, 0x113, 0x4F);
                    break;
                case 2:
                    sub_14622((void *)(intptr_t)word_256FA, 0x2C, 0x1A, 0x113, 0x4F);
                    sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x55EA));
                    sub_15189((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5618));
                    sub_15189((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x564E));
                    sub_13D15((int16 *)(intptr_t)word_256FA, (char *)(f19_dseg + 0xB96A), 0xD4, 0x3E,
                              0x28, 8);
                    word_25722[selw.sel][0x13] = 2;
                    blinkTimer = 0;
                    while (blinkTimer < 0xF0) ;
                    sub_14622((void *)(intptr_t)word_256FA, 0x2C, 0x1A, 0x113, 0x4F);
                    break;
                }
            } else {
                selw.sb[1] = 0;
                sub_14622((void *)(intptr_t)word_256FA, 0x2D, 0x1A, 0x113, 0x45);
                sub_14622((void *)(intptr_t)word_256FA, 0x14, 0xB5, 0x122, 0xC7);
                sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5677));
                sub_15189((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x56A8));
                sub_13D15((int16 *)(intptr_t)word_256FA, (char *)(f19_dseg + 0xB96A), 0xCE, 0x41,
                          0x28, 8);
                sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x56C2));
                sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0x41, 0x3C);
                *(int16 *)(sprParmsTab + word_256FA + 0xC) = 4;
                nameResult = f19_pilotNameInput((int16 *)word_256E2,
                    *(int16 *)(sprParmsTab + word_256E2 + 8),
                    0x3C, 0x1D, 8, 8);
                *(int16 *)(sprParmsTab + word_256FA + 0xC) = 3;
                if (*nameResult != 0) {
                    word_29D54[selw.sel].f0 = selw.sel;
                    sub_15120(word_29D54[selw.sel].name, nameResult);
                    word_29D54[selw.sel].f20 = 0;
                    word_29D54[selw.sel].f22 = 0;
                    word_29D54[selw.sel].f24 = 0;
                    word_29D54[selw.sel].f26 = 0;
                    word_29D54[selw.sel].f28 = 0;
                    word_29D54[selw.sel].f2a = 0;
                    word_29D54[selw.sel].f2c = 0;
                    word_29D54[selw.sel].f2e = 0;
                    word_29D54[selw.sel].f30 = 0;
                    word_29D54[selw.sel].f38 = word_29D54[selw.sel].f36 =
                        word_29D54[selw.sel].u32.w.f32 = word_29D54[selw.sel].u32.w.f34 = 0;
                    word_29D54[selw.sel].f3a = 2;
                    word_29D54[selw.sel].f3c = 3;
                    word_29D54[selw.sel].f40 = word_29D54[selw.sel].f3e = 0;
                    word_29D54[selw.sel].f42 = 4;
                    word_29D54[selw.sel].f4e = word_29D54[selw.sel].f4c =
                        word_29D54[selw.sel].f4a = word_29D54[selw.sel].f48 =
                        word_29D54[selw.sel].f46 = word_29D54[selw.sel].f44 = 0;
                }
                word_25722[selw.sel][0x13] = 2;
                sub_14622((void *)(intptr_t)word_256FA, 0x2D, 0x1A, 0x113, 0x45);
                *(int16 *)(sprParmsTab + word_256E2 + 0xC) = 4;
                *(int16 *)(sprParmsTab + word_256E2 + 4) = 6;
                sub_14622((void *)(intptr_t)word_256E2, word_25722[selw.sel][0],
                          word_25722[selw.sel][1], word_25722[selw.sel][2],
                          word_25722[selw.sel][3]);
                sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + word_2591C[word_29D54[selw.sel].f20]));
                sub_15189((char *)(f19_dseg + 0xB96A), word_29D54[selw.sel].name);
                sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0x23,
                          (selw.sel << 3) + 0x60);
                sub_13E7C(word_29D54[selw.sel].u32.coord, (char *)(f19_dseg + 0xB96A));
                sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0xB4,
                          (selw.sel << 3) + 0x60);
                sub_13FB3(word_29D54[selw.sel].f36, (char *)(f19_dseg + 0xB96A));
                sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A), 0xE0,
                          (selw.sel << 3) + 0x60);
                sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + word_2592A[word_29D54[selw.sel].f4e]));
                len = sub_13E38((int16 *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A));
                sub_13B76((void *)(intptr_t)word_256E2, (char *)(f19_dseg + 0xB96A),
                          ((uint16)(0x23 - len) >> 1) + 0xF5, (selw.sel << 3) + 0x60);
                *(int16 *)(sprParmsTab + word_256E2 + 0xC) = 1;
            }
        } else
            looping = 0;
    }
    sub_14EDA();
    gameData->f0 = word_29D54[selw.sel].f0;
    sub_1513B((char *)gameData + 2, word_29D54[selw.sel].name);
    gameData->f20 = word_29D54[selw.sel].f20;
    gameData->f22 = word_29D54[selw.sel].f22;
    gameData->f24 = word_29D54[selw.sel].f24;
    gameData->f26 = word_29D54[selw.sel].f26;
    gameData->f28 = word_29D54[selw.sel].f28;
    gameData->f2a = word_29D54[selw.sel].f2a;
    gameData->f2c = word_29D54[selw.sel].f2c;
    gameData->f2e = word_29D54[selw.sel].f2e;
    gameData->f30 = word_29D54[selw.sel].f30;
    gameData->u32.coord = word_29D54[selw.sel].u32.coord;
    gameData->f36 = word_29D54[selw.sel].f36;
    gameData->theater = word_29D54[selw.sel].f38;
    gameData->isCampaignMission = word_29D54[selw.sel].f3a;
    gameData->flags3c = word_29D54[selw.sel].f3c;
    gameData->flags3e = word_29D54[selw.sel].f3e;
    gameData->flags40 = word_29D54[selw.sel].f40;
    gameData->f42 = word_29D54[selw.sel].f42;
    gameData->f44 = word_29D54[selw.sel].f44;
    gameData->f46 = word_29D54[selw.sel].f46;
    gameData->f48 = word_29D54[selw.sel].f48;
    gameData->f4a = word_29D54[selw.sel].f4a;
    gameData->f4c = word_29D54[selw.sel].f4c;
    gameData->f4e = word_29D54[selw.sel].f4e;
    byte_29B50 = 0;
    word_2B38A = 0;
    byte_2C160 = 0xF;
    if (byte_2D06B == 1) {
        sub_10882();
        if (byte_212BA != 0)
            sub_146E3();
        sub_1DCAC(0);
    }
}

/* seg000:0xa68c — main select screen: word_2C7D4 resource block, five
 * per-theater overlay calls, item rows (or a single row) by word_2D066->f22,
 * centered strings for gameData->f3a..f40, then the select widget. */
void f19_sub_1A68C(void) {
    int16 v;                    /* [bp-2]  */
    int16 i;                    /* [bp-4]  */
    int16 dy;                   /* [bp-6]  */
    int16 len;                  /* [bp-0a] */
    int16 result;               /* [bp-8]  */

    f19_sub_108B7();
    switch (word_2C7D4) {
    case 0:
        if (rtcEnabled == 1) {
            sub_14746((char *)(f19_dseg + 0x59B0), word_2D2CA, word_2D2CC);
            sub_161CC(0x6A, 1, word_2D2C6);
            word_22324 = 0;
        }
        gfx_unknown2b(8);
        sub_14746((char *)(f19_dseg + 0x59BA), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        gfx_unknown2b(0xB);
        f19_loadSpriteRes((char *)(f19_dseg + 0x59C3), word_2D26E);
        v = word_2D26E;
        ovlCall_c53();
        ovlCall_bea(word_2D06C);
        break;
    case 1:
        if (rtcEnabled == 1) {
            sub_161CC(0x6A, 1, word_2D2C6);
            word_22324 = 0;
        }
        v = word_2D26E;
        if (byte_298E9 == 0) {
            gfx_unknown2b(8);
            sub_14746((char *)(f19_dseg + 0x59CC), word_2C972, word_2C974);
            sub_14BEE(word_2D064, word_2D270);
            byte_298E9 = 1;
        }
        if (byte_2B384 == 0) {
            gfx_unknown2b(0xB);
            f19_loadSpriteRes((char *)(f19_dseg + 0x59D5), word_2D26E);
            byte_2B384 = 1;
        }
        ovlCall_c53();
        ovlCall_bea(word_2D270);
        break;
    case 2:
        gfx_unknown2b(6);
        sub_14746((char *)(f19_dseg + 0x59DE), word_2C972, word_2C974);
        sub_14BEE(word_2D064, word_2D06C);
        if (rtcEnabled == 1) {
            sub_161CC(0x6A, 1, word_2D2C6);
            word_22324 = 0;
        }
        v = word_2D26E;
        ovlCall_c53();
        ovlCall_bea(word_2D06C);
        break;
    }
    word_25B46[0] = word_25B46[16] = word_25B46[32] = word_25B46[48] =
        word_25B46[64] = word_25B46[80] = word_25B46[96] = word_25B46[112] =
        word_25B46[128] = word_25B46[144] = v;
    switch (gameData->theater) {
    case 0:
        ovlCall_b4f(word_25B64);
        ovlCall_b4f(word_25B84);
        break;
    case 1:
        ovlCall_b4f(word_25BA4);
        ovlCall_b4f(word_25BC4);
        break;
    case 2:
        ovlCall_b4f(word_25BE4);
        ovlCall_b4f(word_25C04);
        break;
    case 3:
        ovlCall_b4f(word_25C24);
        ovlCall_b4f(word_25C44);
        break;
    case 4:
        ovlCall_b4f(word_25C64);
        ovlCall_b4f(word_25C84);
        break;
    }
    ovlCall_c4e(1);
    word_2B386 = 0x9AE;
    word_2CA46 = 0x9AA;
    ((int16 *)(f19_dseg + word_25CF6))[5] = ((int16)*word_209AA - 1) * ((int16 *)(f19_dseg + word_25CF6))[1] + ((int16 *)(f19_dseg + word_25CF6))[4];
    flag_29948 = 1;
    ((int16 *)(f19_dseg + word_25B2C))[2] = 0xF;
    sub_14584((int16 *)(f19_dseg + word_25B2C), 0x96, 0x82, 0x104, 0xBE);
    ((int16 *)(f19_dseg + word_25B2C))[2] = 6;
    if (word_2D066->f22 == 1) {
        dy = 0x82;
        for (i = 0; i < *(uint8 *)f19_farAt(word_2CA46); i++) {
            sub_13B50((int16 *)(f19_dseg + word_25B2C), ((struct MenuRow *)(f19_dseg + word_2B386))[i], 0xA0, dy);
            dy += 0xB;
        }
        ((int16 *)(f19_dseg + word_25B44))[2] = 9;
        sub_13B76((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0x59E7), 0x98, 0x48);
        ((int16 *)(f19_dseg + word_25CF6))[4] = 0x84;
    } else {
        sub_13B50((int16 *)(f19_dseg + word_25B2C), ((struct MenuRow *)(f19_dseg + word_2B386))[1], 0xA0, 0x8D);
        ((int16 *)(f19_dseg + word_25B44))[2] = 9;
        sub_13B76((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0x5A08), 0x98, 0x48);
        ((int16 *)(f19_dseg + word_25CF6))[4] = 0x8F;
    }
    sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A23));
    len = sub_13E38((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A));
    sub_13B76((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A), 0x98 + (0x82 - len) / 2, 8);
    ((int16 *)(f19_dseg + word_25B44))[2] = 0;
    switch (gameData->isCampaignMission) {
    case 0:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A37)); break;
    case 1:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A40)); break;
    case 2:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A4C)); break;
    }
    len = sub_13E38((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A));
    sub_13B76((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A), 0x98 + (0x82 - len) / 2, 0x12);
    switch (gameData->flags3c) {
    case 0:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A5D)); break;
    case 1:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A71)); break;
    case 2:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A81)); break;
    case 3:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5A95)); break;
    }
    len = sub_13E38((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A));
    sub_13B76((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A), 0x98 + (0x82 - len) / 2, 0x1C);
    switch (gameData->flags3e) {
    case 0:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5AA5)); break;
    case 1:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5AB5)); break;
    case 2:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5AC7)); break;
    case 3:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5AD9)); break;
    }
    len = sub_13E38((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A));
    sub_13B76((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A), 0x98 + (0x82 - len) / 2, 0x26);
    switch (gameData->flags40) {
    case 0:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5AE9)); break;
    case 1:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5AF4)); break;
    case 2:  sub_15120((char *)(f19_dseg + 0xB96A), (char *)(f19_dseg + 0x5B02)); break;
    }
    len = sub_13E38((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A));
    sub_13B76((int16 *)(f19_dseg + word_25B44), (char *)(f19_dseg + 0xB96A), 0x98 + (0x82 - len) / 2, 0x30);
    ((int16 *)(f19_dseg + word_25B2C))[2] = 0xF;
    sub_14E9C();
    word_25CE6 = 2;
    sub_10924((char *)(f19_dseg + 0x5C86), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), 0xC8,
              ((int16 *)(f19_dseg + word_25CF6))[1] + 0x84, (int16 *)(f19_dseg + word_25B2C), 0);
    result = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x5C86), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), word_25CF6, (int16 *)(f19_dseg + word_25B2C), 0);
    if (result == 1)
        byte_2C160 = 4;
    else
        byte_2C160 = 1;
    sub_125EA();
    sub_14EDA();
    byte_2CA62 = 0;
    sub_14622((int16 *)(f19_dseg + word_25B2C), 0x98, 0x48, 0x117, 0x4E);
    ovlCall_c8a();
    if (rtcEnabled == 1)
        sub_1685C(word_2A0C4);
}

/* seg000:0xaca0 — single-column theater list: draws items, highlights the
 * current theater row, runs the select widget. */
void f19_sub_1ACA0(void) {
    int16 unused;
    int16 row;
    int16 y;
    int16 result;

    f19_sub_108B7();
    word_2B386 = 0x7EE;
    word_2CA46 = 0x7EA;
    word_2CA48 = 0x802;
    ((int16 *)(f19_dseg + word_25F7C))[5] = ((int16)*word_207EA - 1) * ((int16 *)(f19_dseg + word_25F7C))[1] + ((int16 *)(f19_dseg + word_25F7C))[4];
    sub_14622((int16 *)(f19_dseg + word_25D30), 0x96, 0x82, 0x104, 0xBE);
    selInitTab[0] = selInitTab[15] = selInitTab[30] = selInitTab[45] =
        selInitTab[60] = selInitTab[75] = selInitTab[90] = selInitTab[105] =
        selInitTab[120] = selInitTab[135] = word_2D26E;
    flag_29948 = 1;
    ((int16 *)(f19_dseg + word_25D30))[2] = 6;
    y = 0x82;
    row = 0;
    while (row < *(uint8 *)f19_farAt(word_2CA46)) {
        sub_13B50((int16 *)(f19_dseg + word_25D30), ((struct MenuRow *)(f19_dseg + word_2B386))[row], 0xA6, y);
        y += 0xB;
        row++;
    }
    ((int16 *)(f19_dseg + word_25D48))[2] = 9;
    sub_13B76((int16 *)(f19_dseg + word_25D48), (char *)(f19_dseg + 0x5CF8), 0x98, 0x3C);
    ((int16 *)(f19_dseg + word_25D48))[2] = 0;
    ((int16 *)(f19_dseg + word_25D30))[2] = 0xF;
    menuSelTab[(uint16)gameData->theater].sel = 2;
    sub_10924((char *)(f19_dseg + 0x5E76), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), 0xC8,
              (uint16)gameData->theater * ((int16 *)(f19_dseg + word_25F7C))[1] + 0x84, (int16 *)(f19_dseg + word_25D30), 1);
    sub_14E9C();
    result = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x5E76), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), word_25F7C, (int16 *)(f19_dseg + word_25D30), 1);
    gameData->theater = result;
    sub_125EA();
    sub_14EDA();
    byte_2C160 = 9;
}

/* seg000:0xae22 — campaign-mission list: same skeleton as f19_sub_1ACA0 plus a
 * second row-table stamp for the chosen entry; result → isCampaignMission. */
void f19_sub_1AE22(void) {
    int16 unused;
    int16 row;
    int16 y;
    int16 result;

    f19_sub_108B7();
    word_2B386 = 0x826;
    word_2CA46 = 0x822;
    word_2CA48 = 0x832;
    word_2D276 = 0x816;
    word_2C7D2 = 0x1A;
    ((int16 *)(f19_dseg + word_26050))[5] = ((int16)*word_20822 - 1) * ((int16 *)(f19_dseg + word_26050))[1] + ((int16 *)(f19_dseg + word_26050))[4];
    ((int16 *)(f19_dseg + word_25F94))[2] = 0xF;
    sub_14584((int16 *)(f19_dseg + word_25F94), 0xA6, 0x82, 0xF0, 0xC7);
    ((int16 *)(f19_dseg + word_25F94))[2] = 6;
    y = 0x82;
    row = 0;
    while (row < *(uint8 *)f19_farAt(word_2CA46)) {
        sub_13B50((int16 *)(f19_dseg + word_25F94), ((struct MenuRow *)(f19_dseg + word_2B386))[row], 0xA6, y);
        y += 0xB;
        row++;
    }
    ((int16 *)(f19_dseg + word_25F94))[2] = 0xF;
    menuSelTab2[gameData->isCampaignMission].sel = 2;
    sub_10924((char *)(f19_dseg + 0x5FAE), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), 0xC8,
              (uint16)gameData->isCampaignMission * ((int16 *)(f19_dseg + word_26050))[1] + 0x84,
              (int16 *)(f19_dseg + word_25F94), 2);
    sub_14E9C();
    result = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x5FAE), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), word_26050, (int16 *)(f19_dseg + word_25F94), 2);
    gameData->isCampaignMission = result;
    ((int16 *)(f19_dseg + word_25FAC))[2] = 9;
    sub_13B50((int16 *)(f19_dseg + word_25FAC), ((struct MenuRow *)(f19_dseg + word_2D276))[result], word_2C7D2, 0x49);
    word_2C7D2 = ((int16 *)(f19_dseg + word_25FAC))[4] + 4;
    ((int16 *)(f19_dseg + word_25FAC))[2] = 0;
    sub_125EA();
    sub_14EDA();
    byte_2C160 = 3;
}

/* seg000:0xafa8 — difficulty/tension list: same skeleton plus a briefing-
 * string block (guarded by byte_2D060) and a second stamp row; result →
 * gameData->flags3c (dseg:0x3c). */
void f19_sub_1AFA8(void) {
    int16 row;
    int16 y;
    int16 result;

    f19_sub_108B7();
    initTab2[0] = initTab2[16] = initTab2[32] = initTab2[48] =
        initTab2[64] = initTab2[80] = initTab2[96] = initTab2[112] =
        initTab2[128] = initTab2[144] = word_2D272;
    if (byte_2D060 != 0) {
        gfx_unknown2b(6);
        sub_14A5F((char *)(f19_dseg + 0x6052), word_2D06C);
        sub_14746((char *)(f19_dseg + 0x605B), word_2D2CA, word_2D2CC);
        byte_2D060 = 0;
    }
    word_2B386 = 0x866;
    word_2CA46 = 0x862;
    word_2CA48 = 0x876;
    word_2D276 = 0x852;
    ((int16 *)(f19_dseg + word_262A8))[5] = ((int16)*word_20862 - 1) * ((int16 *)(f19_dseg + word_262A8))[1] + ((int16 *)(f19_dseg + word_262A8))[4];
    ((int16 *)(f19_dseg + word_2607A))[2] = 0xF;
    sub_14584((int16 *)(f19_dseg + word_2607A), 0x96, 0x82, 0x104, 0xBE);
    ((int16 *)(f19_dseg + word_2607A))[2] = 6;
    y = 0x82;
    row = 0;
    while (row < *(uint8 *)f19_farAt(word_2CA46)) {
        sub_13B50((int16 *)(f19_dseg + word_2607A), ((struct MenuRow *)(f19_dseg + word_2B386))[row], 0xA0, y);
        y += 0xB;
        row++;
    }
    ((int16 *)(f19_dseg + word_2607A))[2] = 0xF;
    menuSelTab3[(uint16)gameData->flags3c].sel = 2;
    sub_10924((char *)(f19_dseg + 0x61D4), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), 0xC8,
              (uint16)gameData->flags3c * ((int16 *)(f19_dseg + word_262A8))[1] + 0x84,
              (int16 *)(f19_dseg + word_2607A), 3);
    sub_14E9C();
    result = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x61D4), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), word_262A8, (int16 *)(f19_dseg + word_2607A), 3);
    ((int16 *)(f19_dseg + word_26092))[2] = 9;
    sub_13B50((int16 *)(f19_dseg + word_26092), ((struct MenuRow *)(f19_dseg + word_2D276))[result], word_2C7D2, 0x49);
    word_2C7D2 = ((int16 *)(f19_dseg + word_26092))[4] + 4;
    ((int16 *)(f19_dseg + word_26092))[2] = 0;
    gameData->flags3c = result;
    sub_125EA();
    sub_14EDA();
    byte_2C160 = 0xA;
}

/* seg000:0xb184 — next menu level: same skeleton as f19_sub_1AE22; result →
 * gameData->flags3e (dseg:0x3e). */
void f19_sub_1B184(void) {
    int16 unused;
    int16 row;
    int16 y;
    int16 result;

    f19_sub_108B7();
    word_2B386 = 0x89A;
    word_2CA46 = 0x896;
    word_2CA48 = 0x8AA;
    word_2D276 = 0x886;
    ((int16 *)(f19_dseg + word_263AE))[5] = ((int16)*word_20896 - 1) * ((int16 *)(f19_dseg + word_263AE))[1] + ((int16 *)(f19_dseg + word_263AE))[4];
    ((int16 *)(f19_dseg + word_262C0))[2] = 0xF;
    sub_14584((int16 *)(f19_dseg + word_262C0), 0x96, 0x82, 0x104, 0xBE);
    ((int16 *)(f19_dseg + word_262C0))[2] = 6;
    y = 0x82;
    row = 0;
    while (row < *(uint8 *)f19_farAt(word_2CA46)) {
        sub_13B50((int16 *)(f19_dseg + word_262C0), ((struct MenuRow *)(f19_dseg + word_2B386))[row], 0xA0, y);
        y += 0xB;
        row++;
    }
    ((int16 *)(f19_dseg + word_262C0))[2] = 0xF;
    menuSelTab4[gameData->flags3e].sel = 2;
    sub_10924((char *)(f19_dseg + 0x62DA), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), 0xC8,
              (uint16)gameData->flags3e * ((int16 *)(f19_dseg + word_263AE))[1] + 0x84,
              (int16 *)(f19_dseg + word_262C0), 4);
    sub_14E9C();
    result = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x62DA), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), word_263AE, (int16 *)(f19_dseg + word_262C0), 4);
    gameData->flags3e = result;
    ((int16 *)(f19_dseg + word_262D8))[2] = 9;
    sub_13B50((int16 *)(f19_dseg + word_262D8), ((struct MenuRow *)(f19_dseg + word_2D276))[result], word_2C7D2, 0x49);
    word_2C7D2 = ((int16 *)(f19_dseg + word_262D8))[4] + 4;
    ((int16 *)(f19_dseg + word_262D8))[2] = 0;
    sub_125EA();
    sub_14EDA();
    byte_2C160 = 0xB;
}

/* seg000:0xb304 — final menu level: same skeleton, no stamp row; result →
 * gameData->flags40 (dseg:0x40); rtc bail-out when rtcEnabled==1. */
void f19_sub_1B304(void) {
    int16 unused;
    int16 row;
    int16 y;
    int16 result;

    f19_sub_108B7();
    word_2B386 = 0x8BE;
    word_2CA46 = 0x8BA;
    word_2CA48 = 0x8CA;
    ((int16 *)(f19_dseg + word_26480))[5] = ((int16)*word_208BA - 1) * ((int16 *)(f19_dseg + word_26480))[1] + ((int16 *)(f19_dseg + word_26480))[4];
    ((int16 *)(f19_dseg + word_263C6))[2] = 0xF;
    sub_14584((int16 *)(f19_dseg + word_263C6), 0x96, 0x82, 0x104, 0xBE);
    ((int16 *)(f19_dseg + word_263C6))[2] = 6;
    y = 0x82;
    row = 0;
    while (row < *(uint8 *)f19_farAt(word_2CA46)) {
        sub_13B50((int16 *)(f19_dseg + word_263C6), ((struct MenuRow *)(f19_dseg + word_2B386))[row], 0xA0, y);
        y += 0xB;
        row++;
    }
    ((int16 *)(f19_dseg + word_263C6))[2] = 0xF;
    menuSelTab5[(uint16)gameData->flags40].sel = 2;
    sub_10924((char *)(f19_dseg + 0x63DE), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), 0xC8,
              (uint16)gameData->flags40 * ((int16 *)(f19_dseg + word_26480))[1] + 0x84,
              (int16 *)(f19_dseg + word_263C6), 5);
    sub_14E9C();
    result = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x63DE), word_2CA48, *(uint8 *)f19_farAt(word_2CA46), word_26480, (int16 *)(f19_dseg + word_263C6), 5);
    gameData->flags40 = result;
    if (rtcEnabled == 1)
        sub_1685C(word_2A0C4);
    sub_125EA();
    sub_14EDA();
    byte_2C160 = 4;
}

/* seg000:0xb452 — mission-setup screen: builds the theater subtitle, marks
 * objects for the pick lists, draws the 7-entry option list, then runs the
 * select widget loop toggling flags; byte_2C160 picks the next page. */
void f19_sub_1B452(void) {
    int16 redraw;               /* [bp-4]  */
    int16 v;                    /* [bp-6]  */
    int16 i;                    /* [bp-8]  */
    int16 j;                    /* [bp-0c] */
    int16 loop;                 /* [bp-0a] */
    char a[2];                  /* [bp-2]  dead init */
    int16 selrow;               /* [bp-18] */
    char c9[2];                 /* [bp-16] dead init */
    int16 nn;                   /* [bp-14] */
    char esc[3];                /* [bp-12] dead init */
    int16 k;                    /* [bp-0e] */
    char vv2[2];                /* [bp-1a] dead init */

    f19_sub_108B7();
    a[0] = 0xD; a[1] = 0;
    esc[0] = 9; esc[1] = 0xA; esc[2] = 0;
    vv2[0] = 0x8E; vv2[1] = 0;
    c9[0] = 0x8F; c9[1] = 0;
    word_2BE4A = 0x9B6;
    sub_15152((char *)(f19_dseg + 0xB96A), word_209B6(gameData->theater));
    if (gameData->theater != byte_2B388)
        byte_298F0 = 1;
    gfx_unknown2b(9);
    if (byte_2CA62 == 1) {
        selrow = 6;
        ovlCall_c53();
        sub_14622((int16 *)(f19_dseg + word_26572), 8, 0, 0x13F, 0xB1);
        switch (word_2C7D4) {
        case 0:  v = word_2D26E; break;
        case 1:  v = word_2D2C8; break;
        case 2:  v = word_2D2C8; break;
        }
    } else {
        selrow = 0;
        byte_2CA62 = 0;
        switch (word_2C7D4) {
        case 0:
            f19_loadSpriteRes((char *)(f19_dseg + 0xB96A), word_2D26E);
            v = word_2D26E;
            ovlCall_c53();
            sub_14622((int16 *)(f19_dseg + word_2655A), 0, 0, 0x13F, 0xC7);
            break;
        case 1:
            if (byte_298F0 == 1) {
                f19_loadSpriteRes((char *)(f19_dseg + 0xB96A), word_2D2C8);
                byte_298F0 = 0;
            }
            v = word_2D2C8;
            ovlCall_c53();
            sub_14622((int16 *)(f19_dseg + word_2655A), 0, 0, 0x13F, 0xC7);
            break;
        case 2:
            if (byte_298F0 == 1) {
                f19_loadSpriteRes((char *)(f19_dseg + 0xB96A), word_2D2C8);
                byte_298F0 = 0;
            }
            v = word_2D2C8;
            ovlCall_c53();
            sub_14622((int16 *)(f19_dseg + word_2655A), 0, 0, 0x13F, 0xC7);
            break;
        }
    }
    word_26574[0] = word_26574[16] = word_26574[32] = word_26574[48] =
        word_26574[64] = word_26574[80] = word_26574[96] = word_26574[112] =
        word_26574[128] = word_26574[144] = v;
    flag_29948 = 0;
    ovlCall_b4f(word_26592);
    ovlCall_b4f(word_265B2);
    ((int16 *)(f19_dseg + word_2655A))[2] = 0xC;
    sub_13B76((int16 *)(f19_dseg + word_2655A), (char *)(f19_dseg + 0x64E3), 0x1E, 1);
    ((int16 *)(f19_dseg + word_2655A))[2] = 0;
    sub_13B76((int16 *)(f19_dseg + word_2655A), (char *)(f19_dseg + 0x6515), 0x6F, 1);
    ovlCall_c8a();
    ovlCall_c58();
    if (byte_2CA6A == 1) {
        byte_2C977 = tileMarksOn = ringMode = unitMarksOn = 0;
        sub_13B76((int16 *)(f19_dseg + word_26572), (char *)(f19_dseg + 0x652B), 0xF5, 0x1E);
        ovlCall_c8a();
        f19_missionGenerate();
        byte_2CA6A = 0;
        byte_2C976 = 0;
        for (i = 0; i < 0x10; i++) {
            for (j = 0; j < 0x10; j++)
                if ((tileMarkMap[j * 16 + i] & 0x10) != 0)
                    byte_2C976 = 1;
        }
        for (k = 0; k <= objectCount; k++)
            objectActive[k] = 0;
        k = f19_randMul(3);
        while (k != 0) {
            nn = f19_randMul(objectCount);
            if (nn == word_2B948) continue;
            if (nn == word_2B95A) continue;
            if (f19_worldObjects[nn].pad4 != 0 &&
                (f19_worldObjects[nn].targetFlags & 0x500) == 0) {
                objectActive[nn] = 1;
                k--;
            }
        }
        for (k = 0; k <= objectCount; k++) {
            if (objectActive[k] == 1) {
                objectActive[k] = f19_randMul(3) + 1;
                if (objectActive[k] == 1)
                    f19_worldObjects[k].pad4 = 0;
            }
        }
    }
    ((int16 *)(f19_dseg + word_2655A))[2] = 6;
    nn = 0x6E;
    for (k = 0; k < 7; k++) {
        sub_13B76((int16 *)(f19_dseg + word_2655A), (char *)(f19_dseg + word_26820[k]), 0xEC, nn);
        nn += 0xA;
    }
    f19_selectNextObject();
    f19_selectNextUnit();
    redraw = 1;
    ovlCall_c8a();
    sub_14E9C();
    loop = 1;
    do {
        if (redraw == 1) {
            word_265B6 = 0x12D;
            word_265D6 = 0x12D;
            word_26676 = 0x11E;
            word_26696 = 0x11E;
            ovlCall_b4f(word_26592);
            f19_drawRoutePath();
            f19_drawThreatRings();
            f19_drawSiteMarkers();
            f19_drawUnitMarkers();
            f19_drawTileMarkers();
        }
        menuSelTab6[selrow].sel = 2;
        sub_10924((char *)(f19_dseg + 0x66B4), word_2CA48, 7, 0xFA, selrow * 0xA + 0x6F,
                  (int16 *)(f19_dseg + word_26572));
        selrow = f19_sub_10AE8((struct SelRow *)(f19_dseg + 0x66B4), word_2CA48, 7, word_2681E, (int16 *)(f19_dseg + word_26572));
        redraw = 1;
        switch (selrow + 1) {
        case 1:
            loop = 0;
            byte_2C160 = 5;
            word_2CA6C = 1;
            break;
        case 2:
            ringMode = ++ringMode % 3;
            break;
        case 3:
            byte_2C977 = (byte_2C977 == 0);
            break;
        case 4:
            unitMarksOn = (unitMarksOn == 0);
            break;
        case 5:
            loop = 0;
            byte_2C160 = 5;
            word_2CA6C = 2;
            break;
        case 6:
            tileMarksOn = (tileMarksOn == 0);
            break;
        case 7:
            byte_2C160 = 6;
            loop = 0;
            break;
        }
        if (word_2D066->f72 == 1) {
            while (ovlCall_ccb(0) != 0)
                ;
            byte_20A1A = 0;
            while (byte_20A1A <= 5)
                timerYield();
            while (ovlCall_ccb(0) != 0)
                ;
        }
    } while (loop != 0);
    sub_14EDA();
    byte_2CA62 = 0;
}
