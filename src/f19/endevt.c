/* ported from f19ru src_end/endevt.c — see that file for seg000 offsets */
/* END.EXE — debrief event-dispatch cluster (seg000:5d1b-8531 family + the
 * sub_10010 master driver). These are the per-event-type debrief renderers
 * the original END.EXE calls from its mission-result dispatch. */
#include "f19en.h"




extern void   openBlitClosePic(const char *name, int16 page);   /* seg000:0x15df */
extern int16  loadFileSection(const char *name, int16 b, int16 c); /* 0x12c6 */
extern int16  drawMapView(int16 viewY, int16 viewX, int16 sel);    /* 0x27f6 */
extern void   drawStringAt(int16 *pageNum, const char *str, int16 x, int16 y); /* 0x7c6 */
extern void   setTimerIrqHandler(void);         /* sub_13626 */
extern void   restoreTimerIrqHandler(void);     /* sub_13664 */
extern void   waitForKeyOrJoy2(void);           /* seg000:0x0702 */
extern void   freeBuffer(uint16 segment);       /* seg000:0x2e86 */
extern void   far gfx_setFadeSteps(int16 n);    /* 9D9:1481 */
extern void   far gfx_waitRetrace(void);        /* 9D9:14A9 */
extern void   far gfx_commitPage(void);         /* 9D9:14E0 */
extern void   far gfx_flipPage(void);           /* 9D9:14AE */

/* seg000:7248 — missionResult = terrain type at the aircraft's grid tile;
 * returns it (the sub_10010 dispatch tests ax). */
int16 sub_17248(void)
{
    int16 tx, ty;
    tx = commData->posX >> 11;
    ty = commData->posY >> 11;
    return (missionResult = gridFlags[ty][tx] & 3);
}



extern int16  randomRange(int16 maxVal);        /* seg000:0x0cfe */
extern void   far gfx_setDac(int16 n);          /* 9D9:14A4 */
extern void   drawWrappedText(int16 *page, char *str, uint16 maxWidth,
                              int16 x, int16 y, int16 lineHeight); /* 0x965 */

/* MenuItem fields as END.EXE addresses them (0x32 stride) */

extern void   openDecodeClosePic(const char *name, int16 page); /* 0x1615 */
extern void   sub_10E50(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2); /* clearRect dup */
extern int16  far gfx_getBufSize(void);         /* 9D9:13C3 */
extern void   far gfx_blitSprite(int16 *spr);   /* 9D9:13A5 */
extern int16  far misc_jump_5d_readJoy(int16 a);/* 9D9:1521 */
extern void   processMenuItems(MenuItem *items, int16 unused, int16 itemCount,
                               int16 cursorStartX, int16 cursorStartY, int16 *gfxPage);
extern int16  selectMenuItem(MenuItem *items, int16 unused, int16 itemCount,
                             int16 *inputState, int16 *gfxPage);
extern void   animateFlightPath(int16 *gfxPage);/* 0x4f7b */
extern int32  calcMissionScore(int16 n);        /* 0x5666 */


/* seg000:7334 — score-tally/menu screen: alloc res page, draw item labels,
 * menu loop driving animateFlightPath, then update pilot stats */
void sub_17334(void)
{
    char  m3[2], w1[3], m2[2], z2[2];
    int16 pos, cont, u1, y1, k3;

    m3[0] = 0xd;  m3[1] = 0;
    w1[0] = 9;    w1[1] = 0xa; w1[2] = 0;
    m2[0] = 0x8e; m2[1] = 0;
    z2[0] = 0x8f; z2[1] = 0;
    gfx_setFadeSteps(9);
    openDecodeClosePic((const char *)f19_dsegAt((uint16)word_1F858[pilotRec->field38]),
                       word_23C72 = allocBuffer(gfx_getBufSize()));
    pos = word_23C72;
    gfx_setFadeSteps(8);
    loadPicFromFileAt((const char *)f19_dsegAt(0x5853), 1, 0);
    evtItems[0].page = pos;  evtItems[1].page = pos;
    evtItems[2].page = pos;  evtItems[3].page = pos;
    evtItems[4].page = pos;  evtItems[5].page = pos;
    evtItems[6].page = pos;  evtItems[7].page = pos;
    evtItems[8].page = pos;  evtItems[9].page = pos;
    evtItems[10].page = pos; evtItems[11].page = pos;
    gfx_waitRetrace();
    sub_10E50(evtItemWin(0), 0, 0, 0x13f, 0xc7);
    gfx_blitSprite((int16 *)f19_dsegAt((uint16)word_1F684));
    gfx_blitSprite((int16 *)f19_dsegAt((uint16)word_1F6A4));
    evtItemWin(0)[2] = 0xc;
    drawStringAt(evtItemWin(0), (const char *)f19_dsegAt(0x585f), 0x1e, 1);
    evtItemWin(0)[2] = 0;
    drawStringAt(evtItemWin(0), (const char *)f19_dsegAt(0x5891), 0x6a, 1);
    evtItemWin(0)[2] = 6;
    y1 = 0x96;
    u1 = 0;
    do {
        drawStringAt(evtItemWin(0), (const char *)f19_dsegAt((uint16)word_1F860[u1]), 0xec, y1);
        y1 += 0xa;
        u1++;
    } while (u1 < 2);
    k3 = 0;
    byte_23796 = 1;
    word_18EA6 = 0;
    gfx_commitPage();
    gfx_flipPage();
    setTimerIrqHandler();
    cont = 1;
    do {
        menuItems[k3].state = 2;
        processMenuItems(menuItems, word_2377E, 2, 0xfa,
                         0x97 + k3 * 0xa, word_1F664);
        k3 = selectMenuItem(menuItems, word_2377E, 2,
                             word_1F856, word_1F664);
        switch (k3) {
        case 0:
            animateFlightPath(word_1F664);
            if (byte_22450 == 1)
                k3 = 1;
            break;
        case 1:
            cont = 0;
            break;
        }
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            byte_1DF6A = 0;
            while (byte_1DF6A <= 5)
                ;
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    } while (cont != 0);
    restoreTimerIrqHandler();
    word_23B6A = calcMissionScore(word_22F10);
    if (commData->trainingFlag == 0) {
        pilotRec->awardPoints = word_23B6A;
        if (pilotRec->bestScore < (int16)word_23B6A)
            pilotRec->bestScore = (int16)word_23B6A;
        pilotRec->totalScore += word_23B6A;
    } else
        pilotRec->awardPoints = 0;
    freeBuffer(word_23C72);
}

/* seg000:6076 — end-of-mission summary panel: pick picture + random message
 * table by outcome flags, then the shared draw/input tail */
void sub_16076(void)
{
    int16 a0, a1, a2, a3, a4, a5, a6;

    if (commData->bailout != 0 || commData->trainingFlag == 1)
        return;
    gfx_setFadeSteps(0xa);
    gfx_waitRetrace();
    if (promotionDone == 0 && awardCode == 0 && target1Scored == 0
        && target2Scored == 0 && pilotRec->missionCount != 0x63) {
        openBlitClosePic((const char *)f19_dsegAt(0x4eba), word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection((const char *)f19_dsegAt(0x4ec6), word_23C78, word_23C7A);
            word_1295C = drawMapView(0x19, 2, word_23C76);
        }
        gfx_blitToCurrent(word_23C6C);
        word_2379E = (int16 *)f19_dsegAt(0x4516);
        word_1ED12[2] = 0xf;
        drawWrappedText(word_1ED12, (char *)f19_dsegAt((uint16)word_2379E[randomRange(word_1E2A4)]),
                        0x10e, 0x28, 0xa8, 8);
    } else if ((target1Scored == 1 || target2Scored == 1)
               && missionResult == 0 && promotionDone == 0 && awardCode == 0) {
        openBlitClosePic((const char *)f19_dsegAt(0x4ed0), word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection((const char *)f19_dsegAt(0x4edc), word_23C78, word_23C7A);
            word_1295C = drawMapView(0x19, 2, word_23C76);
        }
        gfx_blitToCurrent(word_23C6C);
        word_2379E = (int16 *)f19_dsegAt(0x4a2c);
        word_1ED12[2] = 0xf;
        drawWrappedText(word_1ED12, (char *)f19_dsegAt((uint16)word_2379E[randomRange(word_1E7BA)]),
                        0x10e, 0x28, 0xa8, 8);
    } else if (pilotRec->missionCount == 0x63 && pilotRec->rank == 6) {
        openBlitClosePic((const char *)f19_dsegAt(0x4ee6), word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection((const char *)f19_dsegAt(0x4ef2), word_23C78, word_23C7A);
            word_1295C = drawMapView(0x11, 1, word_23C76);
        }
        gfx_blitToCurrent(word_23C6C);
        word_2379E = (int16 *)f19_dsegAt(0x4663);
        word_1ED12[2] = 0xf;
        drawWrappedText(word_1ED12, (char *)f19_dsegAt((uint16)*word_2379E), 0x10e, 0x28, 0xa0, 8);
    } else if (pilotRec->missionCount == 0x63 && pilotRec->rank != 6) {
        openBlitClosePic((const char *)f19_dsegAt(0x4efc), word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection((const char *)f19_dsegAt(0x4f08), word_23C78, word_23C7A);
            word_1295C = drawMapView(0x11, 1, word_23C76);
        }
        gfx_blitToCurrent(word_23C6C);
        word_2379E = (int16 *)f19_dsegAt(0x46f6);
        word_1ED12[2] = 0xf;
        drawWrappedText(word_1ED12, (char *)f19_dsegAt((uint16)*word_2379E), 0x10e, 0x28, 0xa0, 8);
    } else if (promotionDone == 1 || awardCode != 0) {
        openBlitClosePic((const char *)f19_dsegAt(0x4f12), word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection((const char *)f19_dsegAt(0x4f1e), word_23C78, word_23C7A);
            word_1295C = drawMapView(0x11, 1, word_23C76);
        }
        gfx_blitToCurrent(word_23C6C);
        word_2379E = (int16 *)f19_dsegAt(0x478c);
        word_1ED12[2] = 0xf;
        drawWrappedText(word_1ED12, (char *)f19_dsegAt((uint16)word_2379E[randomRange(word_1E51A)]),
                        0x10e, 0x28, 0xa0, 8);
    } else if (commData->field2c < 3) {
        openBlitClosePic((const char *)f19_dsegAt(0x4f28), word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection((const char *)f19_dsegAt(0x4f33), word_23C78, word_23C7A);
            word_1295C = drawMapView(0x19, 1, word_23C76);
        }
        gfx_blitToCurrent(word_23C6C);
        word_2379E = (int16 *)f19_dsegAt(0x4b46);
        word_1ED12[2] = 0xf;
        drawWrappedText(word_1ED12, (char *)f19_dsegAt((uint16)word_2379E[randomRange(word_1E8D4)]),
                        0x10e, 0x28, 0xa8, 8);
    } else {
        openBlitClosePic((const char *)f19_dsegAt(0x4f3c), word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection((const char *)f19_dsegAt(0x4f47), word_23C78, word_23C7A);
            word_1295C = drawMapView(0x19, 1, word_23C76);
        }
        gfx_blitToCurrent(word_23C6C);
        word_2379E = (int16 *)f19_dsegAt(0x4cb4);
        word_1ED12[2] = 0xf;
        drawWrappedText(word_1ED12, (char *)f19_dsegAt((uint16)word_2379E[randomRange(word_1EA42)]),
                        0x10e, 0x28, 0xa8, 8);
    }
    gfx_setDac(1);
    word_1ED12[2] = 9;
    drawStringAt(word_1ED12, (const char *)f19_dsegAt(0x4f50), 0x64, 0xc1);
    gfx_commitPage();
    setTimerIrqHandler();
    waitForKeyOrJoy2();
    restoreTimerIrqHandler();
    if (word_18EA2 == 1)
        freeBuffer(word_226BC);
}

extern void   mystrcpy(char *dst, const char *src);             /* 0x38ba */
extern void   mystrcat(char *dst, const char *src);             /* 0x3923 */
extern void   farStrcpy(char *dst, char far *src);              /* 0x38ec */
extern int16  stringWidth(int16 *item, uint8 *str);             /* 0x0a88 */
extern void   waitForKeyOrJoy(void);            /* seg000:0x067b */

/* seg000:7094 — draw "<rank><name>" centered; pilot-name debrief screen */
void sub_17094(void)
{
    int16 width;
    char  nameBuf[0x64];
    int16 page;
    char  tmpStr[0x14];

    byte_199F4 = 1;
    gfx_setFadeSteps(3);
    openBlitClosePic((const char *)f19_dsegAt(0x56d6), word_23C6C);
    page = word_23C6C;
    gfx_waitRetrace();
    gfx_blitToCurrent(page);
    mystrcpy(nameBuf, (const char *)f19_dsegAt((uint16)rankNames[pilotRec->rank]));
    farStrcpy(tmpStr, (char far *)pilotRec + 2);
    mystrcat(nameBuf, tmpStr);
    width = stringWidth(word_19704, (uint8 *)nameBuf);
    drawStringAt(word_19704, nameBuf, (int16)(((uint16)(0x84 - width)) >> 1) + 0xb9, 0xc1);
    gfx_commitPage();
    gfx_flipPage();
    waitForKeyOrJoy();
}


/* seg000:714c — draw wrapped mission-text panel and wait for input */
void sub_1714C(void)
{
    int16 page;
    char  buf[0xc6];

    gfx_setFadeSteps(7);
    openBlitClosePic((const char *)f19_dsegAt(0x5706), word_23C6C);
    page = word_23C6C;
    gfx_waitRetrace();
    if (word_18EA2 == 1) {
        loadFileSection((const char *)f19_dsegAt(0x5711), word_23C78, word_23C7A);
        word_1295C = drawMapView(0x1c, 0x21, word_23C76);
    }
    gfx_blitToCurrent(page);
    mystrcpy(buf, (const char *)f19_dsegAt(0x571a));
    mystrcat(buf, (const char *)f19_dsegAt(0x5755));
    word_1981E[2] = 0xf;
    drawWrappedText(word_1981E, buf, 0xf0, 0x27, 0xa8, 7);
    word_1981E[2] = 9;
    drawStringAt(word_1981E, (const char *)f19_dsegAt(0x577d), 0x64, 0xc1);
    word_1981E[2] = 0;
    gfx_commitPage();
    gfx_flipPage();
    setTimerIrqHandler();
    waitForKeyOrJoy2();
    restoreTimerIrqHandler();
    if (word_18EA2 == 1)
        freeBuffer(word_226BC);
}

/* seg000:7280 — draw the event-type map/scene panel and wait for input */
void sub_17280(void)
{
    int16 saved;
    gfx_setFadeSteps(2);
    openBlitClosePic((const char *)f19_dsegAt(0x57c0), word_23C6C);
    saved = word_23C6C;
    gfx_waitRetrace();
    if (word_18EA2 == 1) {
        loadFileSection((const char *)f19_dsegAt(0x57c9), word_23C78, word_23C7A);
        word_1295C = drawMapView(0x10, 0x5a, word_23C76);
    }
    gfx_blitToCurrent(saved);
    word_19806[2] = 1;
    drawStringAt(word_19806, (const char *)f19_dsegAt(0x57d4), 0x87, 0xc1);
    word_19806[2] = 0;
    gfx_commitPage();
    gfx_flipPage();
    setTimerIrqHandler();
    waitForKeyOrJoy2();
    restoreTimerIrqHandler();
    if (word_18EA2 == 1)
        freeBuffer(word_226BC);
}

/* ==== seg000:5d1b — debrief map event popup: erase the previous icon, remap
 * flightRecords[cur]'s status to an icon kind, clamp the popup into the map
 * window quadrants, then blit the icon sprite. Called from drawMenuItem. ==== */
extern int16  mapToScreenX(int16 v);             /* seg000:0x54ba */
extern int16  mapToScreenY(int16 v);             /* seg000:0x54cf */
extern void   far gfx_copyRect(int16 a, int16 b, int16 c, int16 d,
                               int16 e, int16 f, int16 g, int16 h); /* 9D9:1422 */

void sub_15D1B(void)
{
    int16  n;
    uint16 m;

    if (popupVisible == 1) {
        gfx_copyRect(1, 0, 0x96, 0, popupX, popupY, 0x30, 0x28);
        popupVisible = 0;
    }
    n = flightRecords[word_18EA6].status & 0x3f;
    switch (n) {
    case 1:
        if (slotInfoTable[(flightRecords[word_18EA6].unitId & 0x7f) << 4] & 8)
            n = 0xf;
        else
            n = 0;
        break;
    case 2:
    case 12:
        n = 2;
        break;
    case 3:
        n = 1;
        break;
    case 4:
        n = 6;
        break;
    case 5:
        n = 3;
        break;
    case 6:
        n = 4;
        break;
    case 7:
        n = 5;
        break;
    case 8:
        if (word_18EA6 == 0) {
            n = 8;
        } else if (commData->landingType == 3) {
            byte_23796 = 1;
            n = 7;
        } else if (commData->landingType == 1) {
            byte_23796 = 1;
            n = 0xe;
        } else if (missionResult == 0) {
            byte_23796 = 1;
            n = 0xb;
        } else {
            byte_23796 = 1;
            n = 0xd;
        }
        break;
    case 9:
        break;
    case 10:
        n = 0xa;
        break;
    case 11:
        if (flightRecords[word_18EA6].status & 0x80) {
            m = 0;
        } else if (flightRecords[word_18EA6].status & 0x40) {
            m = 1;
        }
        if (word_1AAD8[word_22A12[m].w0].f0 == 3)
            n = 9;
        else
            n = 0xc;
        break;
    }
    if (mapToScreenX(flightRecords[word_18EA6].mapX) + mapWinX1 < 0x73 &&
        mapToScreenY(flightRecords[word_18EA6].mapY) + mapWinY1 < 0x59) {
        popupX = mapToScreenX(flightRecords[word_18EA6].mapX) + mapWinX1 + 0xa;
        popupY = mapToScreenY(flightRecords[word_18EA6].mapY) + mapWinY1 + 0xa;
    } else if (mapToScreenX(flightRecords[word_18EA6].mapX) + mapWinX1 >= 0x73 &&
               mapToScreenY(flightRecords[word_18EA6].mapY) + mapWinY1 < 0x59) {
        popupX = mapToScreenX(flightRecords[word_18EA6].mapX) + mapWinX1 - 0x3a;
        popupY = mapToScreenY(flightRecords[word_18EA6].mapY) + mapWinY1 + 0xa;
    } else if (mapToScreenX(flightRecords[word_18EA6].mapX) + mapWinX1 >= 0x73 &&
               mapToScreenY(flightRecords[word_18EA6].mapY) + mapWinY1 >= 0x59) {
        popupX = mapToScreenX(flightRecords[word_18EA6].mapX) + mapWinX1 - 0x3a;
        popupY = mapToScreenY(flightRecords[word_18EA6].mapY) + mapWinY1 - 0x28;
    } else {
        popupX = mapToScreenX(flightRecords[word_18EA6].mapX) + mapWinX1 + 0xa;
        popupY = mapToScreenY(flightRecords[word_18EA6].mapY) + mapWinY1 - 0x28;
    }
    gfx_copyRect(0, popupX, popupY, 1, 0, 0x96, 0x30, 0x28);
    gfx_copyRect(1, word_1E280[n], word_1E25C[n], 0, popupX, popupY, 0x30, 0x28);
    popupVisible = 1;
}

/* seg000:0x6486 — mission-evaluation narrative: grave/bad/good pic, then the
 * after-action text assembled into a malloc'd scratch buffer. */
extern void   my_itoa(int16 value, char *buf);           /* seg000:0x0c03 */
extern void  *malloc(size_t size);                       /* seg000:0x8c20 — CRT */
extern void   free(void *p);                             /* seg000:0x8c0e — CRT */

void sub_16486(void)
{
    char    eol[2];                          /* "\r" */
    char    pname[0x20];                     /* pilot name */
    char   *out;                             /* scratch text */
    char    numstr[0x10];                    /* itoa/ltoa scratch */
    char    nbuf[2];                         /* 0x8f tag */
    char    rankname[0x38];                  /* rank + name */
    char    pfx[2];                         /* 0x8e tag */
    register uint16 kills;

    eol[0] = 0xd;  eol[1] = 0;
    pfx[0] = 0x8e; pfx[1] = 0;
    nbuf[0] = 0x8f; nbuf[1] = 0;
    farStrcpy(pname, (char far *)pilotRec + 2);
    out = (char *)malloc(0x3e8);
    word_23B6A = calcMissionScore(0x100);

    if (commData->bailout != 0 && commData->trainingFlag == 0) {
        gfx_setFadeSteps(8);
        openBlitClosePic("grave.pic", word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection("gravec.pak", word_23C78, word_23C7A);
            word_1295C = drawMapView(0x21, 0x5e, word_23C76);
        }
    }
    if ((commData->bailout == 0 && target1Scored == 0 && target2Scored == 0)
        || (commData->bailout != 0 && commData->trainingFlag == 1)) {
        gfx_setFadeSteps(9);
        openBlitClosePic("bad.pic", word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection("badc.pak", word_23C78, word_23C7A);
            word_1295C = drawMapView(3, 0xe, word_23C76);
        }
    }
    if (commData->bailout == 0 && (target1Scored == 1 || target2Scored == 1)) {
        gfx_setFadeSteps(9);
        openBlitClosePic("good.pic", word_23C6C);
        if (word_18EA2 == 1) {
            loadFileSection("goodc.pak", word_23C78, word_23C7A);
            word_1295C = drawMapView(3, 0x20, word_23C76);
        }
    }
    gfx_waitRetrace();
    gfx_blitToCurrent(word_23C6C);
    gfx_flipPage();
    word_1F426[2] = 9;
    drawStringAt(word_1F426, "Press Selector to continue", 0x50, 0xc0);
    mystrcpy(rankname, (const char *)f19_dsegAt((uint16)rankNames[pilotRec->rank]));
    mystrcat(rankname, pname);

    if (commData->bailout != 0 && commData->trainingFlag == 1) {
        mystrcpy(out, "If this had been an actual combat mission, ");
        mystrcat(out, rankname);
        mystrcat(out, " would now be dead");
        if (target1Scored == 1 || target2Scored == 1) {
            mystrcat(out, ", but the ");
            mystrcat(out, pfx);
            if (target1Scored == 1 && target2Scored == 1)
                mystrcat(out, "primary and secondary targets were ");
            else if (target1Scored == 1)
                mystrcat(out, "primary target was ");
            else
                mystrcat(out, "secondary target was ");
            mystrcat(out, "successfully destroyed.  ");
        } else {
            mystrcat(out, " and the ");
            mystrcat(out, pfx);
            mystrcat(out, "primary target remains intact.  ");
        }
        mystrcat(out, nbuf);
    } else if (commData->bailout != 0) {
        mystrcpy(out, rankname);
        if (target1Scored == 1 || target2Scored == 1) {
            mystrcat(out, " was killed in action ");
            mystrcat(out, pfx);
            mystrcat(out, "after accomplishing his mission.  ");
        } else {
            mystrcat(out, " died in the line of duty, ");
            mystrcat(out, pfx);
            mystrcat(out, "mission incomplete.  ");
        }
        mystrcat(out, nbuf);
    } else if (commData->landingType != 1 && missionResult == 0) {
        mystrcpy(out, "After an embarrassing international incident, ");
        mystrcat(out, rankname);
        mystrcat(out, " returned to his squadron.  ");
        if (target1Scored == 1 || target2Scored == 1) {
            mystrcat(out, pfx);
            mystrcat(out, "Destroying the ");
            if (target1Scored == 1 && target2Scored == 1)
                mystrcat(out, "primary and secondary targets ");
            else if (target1Scored == 1)
                mystrcat(out, "primary objective ");
            else
                mystrcat(out, "secondary target ");
            mystrcat(out, nbuf);
            mystrcat(out, "helped calm the squadron commander.  ");
        } else {
            mystrcat(out, "The squadron commander was not pleased that the ");
            mystrcat(out, pfx);
            mystrcat(out, "primary target remains intact.  ");
            mystrcat(out, nbuf);
        }
    } else if (commData->landingType == 2 && missionResult != 0) {
        mystrcpy(out, rankname);
        if (target1Scored == 1 || target2Scored == 1) {
            mystrcat(out, " survived ejection after ");
            mystrcat(out, pfx);
            mystrcat(out, "successfully achieving his ");
            if (target1Scored == 1 && target2Scored == 1)
                mystrcat(out, "primary and secondary objectives.  ");
            else if (target1Scored == 1)
                mystrcat(out, "primary objective.  ");
            else
                mystrcat(out, "secondary objective.  ");
        } else {
            mystrcat(out, " survived ejection but ");
            mystrcat(out, pfx);
            mystrcat(out, "failed to accomplish the mission.  ");
        }
        mystrcat(out, nbuf);
    } else if (commData->landingType == 3) {
        mystrcpy(out, rankname);
        if (target1Scored == 1 || target2Scored == 1) {
            mystrcat(out, " landed safely and ");
            mystrcat(out, pfx);
            mystrcat(out, "accomplished his ");
            if (target1Scored == 1 && target2Scored == 1)
                mystrcat(out, "primary and secondary mission!  ");
            else if (target1Scored == 1)
                mystrcat(out, "primary mission!  ");
            else
                mystrcat(out, "secondary mission!  ");
        } else {
            mystrcat(out, " landed safely but ");
            mystrcat(out, pfx);
            mystrcat(out, "failed to accomplish his assigned objective.  ");
        }
        mystrcat(out, nbuf);
    }
    mystrcat(out, eol);

    mystrcat(out, "The ");
    mystrcat(out, pfx);
    mystrcat(out, "performance rating ");
    mystrcat(out, nbuf);
    mystrcat(out, "for this mission was ");
    mystrcat(out, pfx);
    my_ltoa(word_23B6A, numstr);
    mystrcat(out, numstr);
    mystrcat(out, nbuf);
    if (commData->trainingFlag == 0)
        mystrcat(out, ".  ");
    else
        mystrcat(out, ", but will not be recorded as this was a training flight.");
    mystrcat(out, eol);

    kills = ms_airKilled + ms_unauthAir;
    if (kills != 0) {
        if (kills > 1 && kills < 12) {
            my_itoa(kills, numstr);
            mystrcat(out, pfx);
            mystrcat(out, numstr);
            mystrcat(out, " enemy aircraft ");
            mystrcat(out, nbuf);
            mystrcat(out, "were ");
            mystrcat(out, pfx);
            mystrcat(out, "shot down.  ");
            mystrcat(out, nbuf);
        } else if (ms_airKilled + ms_unauthAir >= 12) {
            mystrcat(out, "In a rare display of dogfighting skills, ");
            my_itoa(ms_airKilled + ms_unauthAir, numstr);
            mystrcat(out, pfx);
            mystrcat(out, numstr);
            mystrcat(out, " enemy aircraft ");
            mystrcat(out, nbuf);
            mystrcat(out, "were ");
            mystrcat(out, pfx);
            mystrcat(out, "shot down! ");
            mystrcat(out, nbuf);
        } else if (ms_airKilled + ms_unauthAir == 1) {
            mystrcat(out, pfx);
            mystrcat(out, "One enemy plane ");
            mystrcat(out, nbuf);
            mystrcat(out, "was ");
            mystrcat(out, pfx);
            mystrcat(out, "shot down.  ");
            mystrcat(out, nbuf);
        }
    }
    mystrcat(out, eol);

    kills = ms_groundKilled + ms_unauthGround;
    if (kills != 0) {
        if (kills == 1) {
            mystrcat(out, pfx);
            mystrcat(out, "One enemy ground target ");
            mystrcat(out, nbuf);
            mystrcat(out, "was ");
        } else {
            my_itoa(ms_groundKilled + ms_unauthGround, numstr);
            mystrcat(out, pfx);
            mystrcat(out, numstr);
            mystrcat(out, " enemy ground installations ");
            mystrcat(out, nbuf);
            mystrcat(out, "were ");
        }
        mystrcat(out, pfx);
        mystrcat(out, "destroyed.  ");
        mystrcat(out, nbuf);
    }
    mystrcat(out, eol);

    if (pilotRec->field3a == 0
        && (ms_civilian != 0 || ms_unauthGround + ms_unauthAir != 0)) {
        mystrcat(out, "In ");
        mystrcat(out, pfx);
        mystrcat(out, "violation ");
        mystrcat(out, nbuf);
        mystrcat(out, "of your ");
        mystrcat(out, pfx);
        mystrcat(out, "Rules Of Engagement, ");
        if (ms_civilian != 0 && ms_unauthGround + ms_unauthAir != 0) {
            my_itoa(ms_civilian, numstr);
            mystrcat(out, numstr);
            mystrcat(out, " civilian and ");
            my_itoa(ms_unauthGround + ms_unauthAir, numstr);
            mystrcat(out, numstr);
            mystrcat(out, " unauthorized military ");
        } else if (ms_civilian != 0) {
            my_itoa(ms_civilian, numstr);
            mystrcat(out, numstr);
            mystrcat(out, " civilian ");
        } else {
            my_itoa(ms_unauthGround + ms_unauthAir, numstr);
            mystrcat(out, numstr);
            mystrcat(out, " unauthorized military ");
        }
        if (ms_civilian + ms_unauthGround + ms_unauthAir > 1) {
            mystrcat(out, "targets ");
            mystrcat(out, nbuf);
            mystrcat(out, "were ");
        } else {
            mystrcat(out, "target ");
            mystrcat(out, nbuf);
            mystrcat(out, "was ");
        }
        mystrcat(out, pfx);
        mystrcat(out, "destroyed.");
    }
    mystrcat(out, eol);

    if (pilotRec->field3a == 1 && ms_civilian != 0) {
        mystrcat(out, "In ");
        mystrcat(out, pfx);
        mystrcat(out, "violation ");
        mystrcat(out, nbuf);
        mystrcat(out, "of your ");
        mystrcat(out, pfx);
        mystrcat(out, "Rules Of Engagement, ");
        my_itoa(ms_civilian, numstr);
        mystrcat(out, numstr);
        mystrcat(out, " civilian ");
        if (ms_civilian > 1) {
            mystrcat(out, "targets ");
            mystrcat(out, nbuf);
            mystrcat(out, "were ");
        } else {
            mystrcat(out, "target ");
            mystrcat(out, nbuf);
            mystrcat(out, "was ");
        }
        mystrcat(out, pfx);
        mystrcat(out, "destroyed.");
    }
    mystrcat(out, eol);

    if (ms_friendlyAir != 0 || ms_friendlyGnd != 0) {
        mystrcat(out, pfx);
        mystrcat(out, "Friendly forces ");
        mystrcat(out, nbuf);
        mystrcat(out, "are ");
        mystrcat(out, pfx);
        mystrcat(out, "outraged by ");
        mystrcat(out, nbuf);
        mystrcat(out, "the ");
        mystrcat(out, pfx);
        mystrcat(out, "attack ");
        mystrcat(out, nbuf);
        mystrcat(out, "and destruction of their ");
        if (ms_friendlyAir != 0 && ms_friendlyGnd != 0)
            mystrcat(out, "aircraft and installations.  ");
        else if (ms_friendlyAir != 0)
            mystrcat(out, "aircraft.  ");
        else
            mystrcat(out, "installations.  ");
    }

    word_1F426[2] = 0xf;
    drawWrappedText(word_1F426, out, 0x12c, 0xa, 0x49, 8);
    gfx_commitPage();
    setTimerIrqHandler();
    waitForKeyOrJoy2();
    restoreTimerIrqHandler();
    free(out);
    if (word_18EA2 == 1)
        freeBuffer(word_226BC);
}

extern void   checkPromotion(void);             /* seg000:0x8532 */
extern void   checkAwardCodes(void);            /* seg000:0x85e3 */
extern void   timerWait(uint16 ticks);          /* seg000:0x0cd9 dup */
extern void   clearRect(int16 *item, int16 x1, int16 y1, int16 x2, int16 y2); /* 0xdb2 */

/* seg000:75bc — post-mission awards ceremony: medal.pic backdrop + ribbon
 * sprite layout, then one text page per pending award flag (set by the
 * checkPromotion/checkAwardCodes calls embedded in the setup). */
void sub_175BC(void)
{
    char   eol[2];
    char   pname[0x16];
    int16  i;
    uint16 rows[8];
    char  *obuf;
    char   numbuf[8];
    char   pfx[2];
    int16  x7;
    char   pfxa[2];
    char   rbuf[0x20];

    if (commData->trainingFlag == 1 && target1Scored != 1 &&
        target2Scored != 1)
        return;

    word_23C72 = allocBuffer(gfx_getBufSize());
    eol[0] = 0xd;
    eol[1] = 0;
    pfxa[0] = 0x8e;
    pfxa[1] = 0;
    pfx[0] = 0x8f;
    pfx[1] = 0;
    awardStatGrid[0][0] = word_23C72;
    awardStatGrid[0][15] = word_23C72;
    awardStatGrid[1][15] = word_23C72;
    awardStatGrid[2][15] = word_23C72;
    awardStatGrid[3][15] = word_23C72;
    awardStatGrid[4][15] = word_23C72;
    awardStatGrid[5][15] = word_23C72;
    awardStatGrid[6][15] = word_23C72;
    awardStatGrid[0][30] = word_23C72;
    awardStatGrid[1][30] = word_23C72;
    awardStatGrid[2][30] = word_23C72;
    awardStatGrid[3][30] = word_23C72;
    awardStatGrid[4][30] = word_23C72;
    awardStatGrid[5][30] = word_23C72;
    awardStatGrid[6][30] = word_23C72;
    awardStatGrid[1][0] = word_23C72;
    awardStatGrid[2][0] = word_23C72;
    awardStatGrid[3][0] = word_23C72;
    awardStatGrid[4][0] = word_23C72;
    awardStatGrid[5][0] = word_23C72;
    awardStatGrid[6][0] = word_23C72;
    awardStatGrid[7][0] = word_23C72;
    ribbonItems[0][0] = word_23C72;
    ribbonItems[1][0] = word_23C72;
    ribbonItems[2][0] = word_23C72;
    ribbonItems[3][0] = word_23C72;
    ribbonItems[4][0] = word_23C72;
    ribbonItems[5][0] = word_23C72;
    ribbonItems[6][0] = word_23C72;
    ribbonItems[7][0] = word_23C72;
    ribbonItems[8][0] = word_23C72;
    ribbonItems[9][0] = word_23C72;
    ribbonItems[10][0] = word_23C72;
    ribbonItems[11][0] = word_23C72;
    ribbonItems[12][0] = word_23C72;
    ribbonItems[13][0] = word_23C72;
    ribbonItems[14][0] = word_23C72;
    ribbonItems[15][0] = word_23C72;
    ribbonItems[16][0] = word_23C72;
    awardQueued = awardCode = missionRibbon = promotionDone = award6Flag =
        awardTrained = promotionPending = 0;
    farStrcpy(pname, (char far *)pilotRec + 2);
    obuf = (char *)malloc(0x3e8);
    gfx_setFadeSteps(4);
    openBlitClosePic("medal.pic", word_23C6C);
    gfx_setFadeSteps(5);
    openDecodeClosePic("medal.spr", word_23C72);
    gfx_setDac(3);
    gfx_blitToCurrent(word_23C72);
    gfx_blitToCurrent(word_23C6C);
    gfx_setDac(2);
    gfx_blitSprite(f19en_sprTab(rankSpriteA, pilotRec->rank));
    gfx_blitSprite(f19en_sprTab(rankSpriteB, pilotRec->rank));
    gfx_blitSprite(f19en_sprTab(rankSpriteC, pilotRec->rank));
    mystrcpy(rbuf, (char *)f19_dsegAt((uint16)rankNames[pilotRec->rank]));
    mystrcat(rbuf, pname);
    checkPromotion();
    checkAwardCodes();
    mystrcpy(obuf, rbuf);

    if (awardCode != 0) {
        mystrcat(obuf, " was");
        if (commData->bailout != 0)
            mystrcat(obuf, ", posthumously,");
        mystrcat(obuf, pfxa);
        mystrcat(obuf, " awarded ");
        mystrcat(obuf, pfx);
        mystrcat(obuf, "the ");
        mystrcat(obuf, f19en_strTab(medalNames, awardCode));
        if (awardCode != 6 && award6Flag == 1) {
            mystrcat(obuf, " and");
            mystrcat(obuf, " the ");
            mystrcat(obuf, pfxa);
            mystrcat(obuf, "PURPLE HEART ");
            mystrcat(obuf, pfx);
            mystrcat(obuf, "for being wounded.  ");
        } else {
            mystrcat(obuf, ".  ");
        }
        if (stringWidth(awardTextItem, (uint8 *)obuf) > 0x26c) {
            awardTextItem[6] = 3;
            awardTextItem[2] = 9;
            drawStringAt(awardTextItem, "Press Selector to continue", 0x6e, 0xc2);
            awardTextItem[2] = 0xf;
            drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 7);
            awardTextItem[6] = 4;
        } else {
            awardTextItem[2] = 9;
            drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
            awardTextItem[2] = 0xf;
            drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        }
        if (awardCode != 0) {
            gfx_blitSprite(f19en_sprTab(medalSpriteTab, awardCode));
            if (award6Flag == 1 && awardCode != 6) {
                purpleHeartSpr[4] = 0xfd;
                purpleHeartSpr[5] = 0x4e;
            } else {
                purpleHeartSpr[4] = 0xc5;
                purpleHeartSpr[5] = 0x54;
            }
            if (award6Flag == 1)
                gfx_blitSprite(purpleHeartSpr);
        }
    }

    rows[0] = 0;
    if (pilotRec->flag44 == 1)
        rows[0]++;
    if (pilotRec->flag46 == 1)
        rows[0]++;
    if (pilotRec->flag48 == 1)
        rows[0]++;
    if (pilotRec->flag4a == 1)
        rows[0]++;
    if (pilotRec->flag4c == 1)
        rows[0]++;
    if (pilotRec->flag22 == 1 && award6Flag == 0)
        rows[0]++;
    if ((pilotRec->award24 == 1 && awardCode != 1) || pilotRec->award24 > 1)
        rows[0]++;
    if ((pilotRec->award26 == 1 && awardCode != 2) || pilotRec->award26 > 1)
        rows[0]++;
    if ((pilotRec->award28 == 1 && awardCode != 3) || pilotRec->award28 > 1)
        rows[0]++;
    if ((pilotRec->award2a == 1 && awardCode != 4) || pilotRec->award2a > 1)
        rows[0]++;
    if (pilotRec->flag2c == 1 && awardCode != 5)
        rows[0]++;

    i = 0;
    if (pilotRec->flag44 == 1) {
        ribbonItems[6][4] = ribbonIcons[rows[0]][0];
        ribbonItems[6][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[6]);
    }
    if (pilotRec->flag46 == 1) {
        ribbonItems[7][4] = ribbonIcons[rows[0]][i];
        ribbonItems[7][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[7]);
    }
    if (pilotRec->flag48 == 1) {
        ribbonItems[8][4] = ribbonIcons[rows[0]][i];
        ribbonItems[8][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[8]);
    }
    if (pilotRec->flag4a == 1) {
        ribbonItems[9][4] = ribbonIcons[rows[0]][i];
        ribbonItems[9][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[9]);
    }
    if (pilotRec->flag4c == 1) {
        ribbonItems[10][4] = ribbonIcons[rows[0]][i];
        ribbonItems[10][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[10]);
    }
    if (pilotRec->flag22 == 1 && award6Flag == 0) {
        ribbonItems[11][4] = ribbonIcons[rows[0]][i];
        ribbonItems[11][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[11]);
    }
    if ((pilotRec->award24 == 1 && awardCode != 1) || pilotRec->award24 > 1) {
        ribbonItems[12][4] = ribbonIcons[rows[0]][i];
        ribbonItems[12][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[12]);
    }
    if ((pilotRec->award26 == 1 && awardCode != 2) || pilotRec->award26 > 1) {
        ribbonItems[13][4] = ribbonIcons[rows[0]][i];
        ribbonItems[13][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[13]);
    }
    if ((pilotRec->award28 == 1 && awardCode != 3) || pilotRec->award28 > 1) {
        ribbonItems[14][4] = ribbonIcons[rows[0]][i];
        ribbonItems[14][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[14]);
    }
    if ((pilotRec->award2a == 1 && awardCode != 4) || pilotRec->award2a > 1) {
        ribbonItems[15][4] = ribbonIcons[rows[0]][i];
        ribbonItems[15][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[15]);
    }
    if (pilotRec->flag2c == 1 && awardCode != 5) {
        ribbonItems[16][4] = ribbonIcons[rows[0]][i];
        ribbonItems[16][5] = ribbonPos[rows[0]][i++];
        gfx_blitSprite(ribbonItems[16]);
    }
    gfx_blitSprite(awardStatGrid[0]);

    if (awardCode != 0) {
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }
    clearKeybuf();

    if (awardTrained == 1) {
        awardTextItem[2] = 0;
        clearRect(awardTextItem, 5, 0xad, 0x13c, 0xc7);
        mystrcpy(obuf, rbuf);
        mystrcat(obuf, " is ");
        mystrcat(obuf, "awarded the \x8eCombat Readiness Ribbon\x8f for ");
        mystrcat(obuf, "successfully completing this training mission.");
        awardTextItem[2] = 9;
        drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
        awardTextItem[2] = 0xf;
        drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }
    clearKeybuf();

    if (missionRibbon != 0) {
        awardTextItem[2] = 0;
        clearRect(awardTextItem, 5, 0xad, 0x13c, 0xc7);
        mystrcpy(obuf, rbuf);
        mystrcat(obuf, " is ");
        if (awardCode != 0 || awardTrained == 1)
            mystrcat(obuf, "also ");
        mystrcat(obuf, "awarded the \x8e");
        mystrcat(obuf, f19en_strTab(ribbonNames, missionRibbon));
        mystrcat(obuf, "\x8f for ");
        my_itoa(pilotRec->missionCount, numbuf);
        mystrcat(obuf, numbuf);
        mystrcat(obuf, " missions.");
        awardTextItem[2] = 9;
        drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
        awardTextItem[2] = 0xf;
        drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }
    clearKeybuf();

    if (awardQueued != 0) {
        awardTextItem[2] = 0;
        clearRect(awardTextItem, 5, 0xad, 0x13c, 0xc7);
        if (commData->trainingFlag == 1) {
            mystrcpy(obuf, "If this had been a real mission you would have rec");
            mystrcat(obuf, f19en_strTab(queuedAwardName, awardQueued));
            mystrcat(obuf, ".");
        } else {
            mystrcpy(obuf, "Because your capture exposed our stealth technolog");
            mystrcat(obuf, "commander will not recommend you for the ");
            mystrcat(obuf, f19en_strTab(queuedAwardName, awardQueued));
            mystrcat(obuf, ".");
        }
        if (stringWidth(awardTextItem, (uint8 *)obuf) > 0x26c) {
            awardTextItem[6] = 3;
            awardTextItem[2] = 9;
            drawStringAt(awardTextItem, "Press Selector to continue", 0x6e, 0xc2);
            awardTextItem[2] = 0xf;
            drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 7);
            awardTextItem[6] = 4;
        } else {
            awardTextItem[2] = 9;
            drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
            awardTextItem[2] = 0xf;
            drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        }
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }
    clearKeybuf();

    if (awardCode == 0 && promotionDone == 0 && awardTrained == 0 &&
        missionRibbon == 0 && awardQueued == 0 && promotionPending == 0) {
        mystrcpy(obuf, rbuf);
        mystrcat(obuf, " remained at his present rank.  ");
        awardTextItem[2] = 9;
        drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
        awardTextItem[2] = 0xf;
        drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }

    if (promotionDone == 1) {
        awardTextItem[2] = 0;
        clearRect(awardTextItem, 5, 0xad, 0x13c, 0xc7);
        awardTextItem[2] = 9;
        drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
        awardTextItem[2] = 0xf;
        mystrcpy(obuf, "For demonstrated skill and achievment, ");
        mystrcat(obuf, rbuf);
        if (promotionDone == 1) {
            mystrcat(obuf, " was ");
            if (commData->bailout != 0)
                mystrcat(obuf, "posthumously ");
            mystrcat(obuf, pfxa);
            mystrcat(obuf, "promoted ");
            mystrcat(obuf, pfx);
            mystrcat(obuf, "to the rank of ");
            mystrcat(obuf, f19en_strTab(newRankNames, pilotRec->rank));
        }
        gfx_blitSprite(f19en_sprTab(rankSpriteA, pilotRec->rank));
        gfx_blitSprite(f19en_sprTab(rankSpriteB, pilotRec->rank));
        gfx_blitSprite(f19en_sprTab(rankSpriteC, pilotRec->rank));
        drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }

    if (promotionPending == 1) {
        awardTextItem[2] = 0;
        clearRect(awardTextItem, 5, 0xad, 0x13c, 0xc7);
        mystrcpy(obuf, "The capture and public trial of ");
        mystrcat(obuf, rbuf);
        mystrcat(obuf, " will delay the promotion to ");
        mystrcat(obuf, f19en_strTab(nextRankNames, pilotRec->rank));
        mystrcat(obuf, ".");
        awardTextItem[2] = 9;
        drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
        awardTextItem[2] = 0xf;
        drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }
    clearKeybuf();

    if (pilotRec->missionCount == 0x63 && commData->bailout == 0) {
        awardTextItem[2] = 0;
        clearRect(awardTextItem, 5, 0xad, 0x13c, 0xc7);
        awardTextItem[2] = 9;
        drawStringAt(awardTextItem, "Press Selector to continue", 0x64, 0xc1);
        awardTextItem[2] = 0xf;
        mystrcpy(obuf, "Congratulations on the successful completion of yo");
        drawWrappedText(awardTextItem, obuf, 0x136, 5, 0xad, 8);
        gfx_commitPage();
        waitForKeyOrJoy();
        if (commData->setupUseJoy == 1) {
            while (misc_jump_5d_readJoy(0) != 0)
                ;
            timerWait(5);
            while (misc_jump_5d_readJoy(0) != 0)
                ;
        }
    }

    if (awardCode == 6)
        awardCode = 0;
    free(obuf);
    freeBuffer(word_23C72);
}
