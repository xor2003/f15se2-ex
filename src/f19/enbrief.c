/* ported from f19ru src_end/enbrief.c — see that file for seg000 offsets */
/* END.EXE — debrief/map routines (f15se2 enbrief.c lineage) */
#include "f19en.h"
#include <stdio.h>

/* MenuItem fields as END.EXE addresses them */



extern void far gfx_switchColor(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2, int16 fromColor, int16 toColor);
extern int16 far gfx_calcRowAddr(int16 x, int16 y);
extern void far gfx_setBlitOffset(int16 v);
extern void far gfx_setOvlVal1(int16 v);
extern void far gfx_setOvlVal2(int16 v);
extern void far gfx_nop23(void);
extern void drawLineWrapper(void);
extern void drawClippedLine(int16 x1, int16 y1, int16 x2, int16 y2);
extern void mystrcpy(char *dst, const char *src);
extern void mystrcat(char *dst, const char *src);               /* seg000:0x3923 */
extern void my_ltoa(int32 value, char *buf);                    /* seg000:0x0acc textfmt.c */
extern void drawWrappedText(int16 *page, char *str, uint16 maxWidth, int16 x, int16 y, int16 lineHeight); /* seg000:0x0965 */
extern void drawStringAt(int16 *pageNum, const char *string, int16 x, int16 y); /* seg000:0x07c6 */
extern int16 stringWidth(int16 *item, uint8 *str);              /* seg000:0x0a88 */
extern void far gfx_copyRect(int16 a, int16 b, int16 c, int16 d, int16 e, int16 f, int16 g, int16 h);
extern void setTimerIrqHandler(void);       /* sub_13626 — int 21/35h+25h asm */
extern void restoreTimerIrqHandler(void);   /* sub_13664 — int 21/25h asm */


extern void far gfx_commitPage(void);
extern void far gfx_setFadeSteps(int16 n);          /* 9D9:1481 */
extern void far gfx_waitRetrace(void);              /* 9D9:14A9 */
extern void far gfx_flipPage(void);                 /* 9D9:14AE */
extern void openBlitClosePic(const char *name, int16 page); /* seg000:0x15df */
extern void waitForKeyOrJoy(void);                  /* seg000:0x067b */
extern void processDebriefInput(int16 *inputState, MenuItem *item, int16 *gfxPage);
extern void drawMenuItem(const MenuItem *items, uint16 index, int16 *gfxPage);
extern void blinkWidget(MenuItem *item, int16 *gfxPage);

#define MENUITEM_TYPE_MASK   0x0007
#define MENUITEM_SELECTABLE  0x0008
#define MENUITEM_ENABLED     0x0100
#define MENUITEM_HAS_SPRITE  0x0800
#define MENUITEM_SPRITE_BLINK 0x1000

#define KEYCODE_ENTER      0x000d
#define KEYCODE_ESC        0x001b
#define KEYCODE_ALTQ       0x1000
#define KEYCODE_LEFTARROW  0x4b00
#define KEYCODE_RIGHTARROW 0x4d00
#define KEYCODE_UPARROW    0x4800
#define KEYCODE_DNARROW    0x5000
#define JOY_DEADZONE_LO    0x4e
#define JOY_DEADZONE_HI    0xb2






/* calcMissionScore externs — score counters, award tables, multipliers */

/* drawMenuItem (seg000:0x44a8) globals */



extern void sub_10E50(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2); /* clearRect dup */
extern int32 calcMissionScore(int16 n);              /* calcMissionScore — skeleton */
extern void sub_15D1B(void);                  /* event popup — skeleton */

extern void far pollJoystick(void);          /* 9C7:2F */
extern int16 far misc_jump_5a_keybuf(void);
extern int16 far misc_jump_5b_getkey(void);
extern int16 far misc_jump_5d_readJoy(int16 a);
extern void far gfx_blitSprite(struct BlinkSprite *spr);   /* 9D9:13A5 */
extern void cleanup(void);                   /* sub_10398 */
extern void restoreCbreakHandler(void);      /* sub_11264 */
extern void exit(int16 code);                /* sub_18AD2 */
extern void drawEventSprite(uint16 rec);      /* sub_14DDE */

/* seg000:0x3ff5 */
int16 isPointInRect(MenuItem *p) {
    if (p->hitX1 <= cursorX && p->hitX2 >= cursorX &&
        p->hitY1 <= cursorY && p->hitY2 >= cursorY)
        return 1;
    else
        return 0;
}

/* seg000:0x54ba */
int16 mapToScreenX(int16 mapCoord) {
    return ((uint16)mapCoord << 7) / 146;
}

/* seg000:0x54cf */
int16 mapToScreenY(int16 mapCoord) {
    return ((uint16)mapCoord << 7) / 0xc3;
}

/* seg000:0x564f */
void drawMapPixel(int16 x, int16 y, int16 color) {
    (void)color;
    drawClippedLine(x, y, x, y);
}

/* seg000:0x0cd9 */
void timerWait(uint16 ticks) {
    timerCounter = 0;
    setTimerIrqHandler();
    while (ticks >= timerCounter)
        timerYield();        /* native: pump 60Hz counters, don't peg a core */
    restoreTimerIrqHandler();
}

/* seg000:0x3b8c */
void processMenuItems(MenuItem *items, int16 unused, int16 itemCount, int16 cursorStartX, int16 cursorStartY, int16 *gfxPage) {
    char p[2]; char a[2]; int16 b; char c[2]; int16 d; int16 e; char f[2];
    int16 g; int16 h; int16 i; int16 j; int16 k; int16 l; int16 m; int16 n; int16 o;
    int16 w;
    register int16 *ip;
    (void)unused;
    (void)b; (void)e; (void)g; (void)h; (void)i; (void)j;
    (void)k; (void)l; (void)m; (void)n; (void)o; (void)w;
    p[0] = 0x0d; p[1] = 0;
    c[0] = 0x89; c[1] = 0;
    a[0] = 0x8d; a[1] = 0;
    f[0] = 0x80; f[1] = 0;
    d = 0;
    goto TEST;
    for (;;) {
NORM:   ip = &items[d].state;
        if (*ip != 3) *ip = 0;
STEP:   d++;
TEST:   if (d >= itemCount) break;
        ip = &items[d].state;
        if (*ip != 2) goto NORM;
        selectedMenuItem = d;
        *ip = 0;
        blinkWidget(&items[d], gfxPage);
        drawMenuItem(items, d, gfxPage);
        goto STEP;
    }
    cursorX = cursorStartX;
    cursorY = cursorStartY;
}

/* seg000:0x3c2e */
int16 selectMenuItem(MenuItem *items, int16 unused, int16 itemCount, int16 *inputState, int16 *gfxPage) {
    char p[2]; int16 a; int16 b; char c[2]; int16 d; char e[2]; int16 f;
    int16 g; char h[2]; int16 i; char j[12]; int16 k; int16 l; int16 m; int16 n; int16 o;
    (void)unused;
    (void)d; (void)g; (void)j; (void)k; (void)l; (void)m; (void)n; (void)o;
    p[0] = 0x0d; p[1] = 0;
    e[0] = 0x89; e[1] = 0;
    c[0] = 0x8d; c[1] = 0;
    h[0] = 0x80; h[1] = 0;
    gfx_commitPage();
    colorAnimEnabled = 0;
    i = 0;
    while (isPointInRect(&items[i]) == 0 && i < itemCount)
        i++;
    joyRepeatFlag = 0;
    for (;;) {
        do {
            gfx_commitPage();
            if ((items[i].flags & MENUITEM_ENABLED) == 0) {
                colorAnimEnabled = 1;
            }
            processDebriefInput(inputState, &items[i], gfxPage);
        } while (inputChanged == 0 && enterPressed == 0);
        if (enterPressed != 0) {
            if (i != selectedMenuItem) {
                i = 0;
                while (isPointInRect(&items[i]) == 0 && i < itemCount)
                    i++;
            }
            if (items[selectedMenuItem].colorTableIdx == 0) {
                b = 0x0b;
                a = 9;
                gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 0x0b, 9);
                b = 3;
                gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 3, a);
                b = 0x0d;
                gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 0x0d, a);
            }
            return i;
        }
        i = 0;
        while (isPointInRect(&items[i]) == 0 && i < itemCount)
            i++;
        if (i != selectedMenuItem) {
            if ((items[i].flags & MENUITEM_SELECTABLE) != 0) {
                for (f = 0; f < itemCount; f++) {
                    if (items[f].state != 0 &&
                        items[i].unk_2c == items[f].unk_2c) {
                        blinkWidget(&items[f], gfxPage);
                    }
                }
                if (items[selectedMenuItem].colorTableIdx == 0) {
                    b = 9;
                    a = 6;
                    gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 9, 6);
                    b = 3;
                    gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 3, a);
                    b = 0x0d;
                    gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 0x0d, a);
                    b = 0x0b;
                    gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 0x0b, a);
                }
                if (items[selectedMenuItem].colorTableIdx == 1) {
                    b = 8;
                    a = 7;
                    gfx_switchColor(gfxPage, items[selectedMenuItem].colorX1, items[selectedMenuItem].colorY1, items[selectedMenuItem].colorX2, items[selectedMenuItem].colorY2, 8, 7);
                }
                blinkWidget(&items[i], gfxPage);
            }
            selectedMenuItem = i;
            drawMenuItem(items, i, gfxPage);
        }
    }
    return i;
}

/* seg000:0x401d */
void processDebriefInput(int16 *cursorBounds, MenuItem *menuItem, int16 *gfxPage) {
    int16 a;
    int16 b;
    int16 c;
    int16 d;
    int16 e;
    int8 f;
    int16 g;
    int16 h;
    int16 i;
    (void)a; (void)g; (void)i;

    colorTablePtr = (uint16 *)f19_dsegAt((uint16)menuItem->colorTableIdx * 14
                                         + f19_dsegOff(colorStyleTable));
    timerCounter2 = 0;
    d = e = 0;
    inputChanged = enterPressed = animDone = f = 0;
    if (joyRepeatFlag == 1) {
        timerCounter = 0;
        f = 1;
    }

    if (commData->setupUseJoy == 1) {
        d = misc_jump_5d_readJoy(0);
        e = misc_jump_5d_readJoy(1);
        pollJoystick();
    }

    for (;;) {
        if ((int8)misc_jump_5a_keybuf() == 0
            || d != 0
            || e != 0
            || joyAxisX < JOY_DEADZONE_LO
            || joyAxisX > JOY_DEADZONE_HI
            || joyAxisY < JOY_DEADZONE_LO
            || joyAxisY > JOY_DEADZONE_HI) {
            if (f != 1)
                goto input_done;
        }
        if (joyRepeatFlag == 1) {
            if (timerCounter > 0x0f) {
                f = 0;
                joyRepeatFlag = 0;
            }
        }

        if (commData->setupUseJoy == 1) {
            d = misc_jump_5d_readJoy(0);
            e = misc_jump_5d_readJoy(1);
            pollJoystick();
        }

        if (quitFlag != 0) {
            cleanup();
            restoreCbreakHandler();
            exit(0);
        }

        if (colorAnimEnabled == 1) {
            if (timerCounter2 > 6) {
                timerCounter2 = 0;
                c = colorTablePtr[colorAnimIdx + 1] >> 4;
                b = colorTablePtr[colorAnimIdx + 1] & 0xf;
                gfx_switchColor(gfxPage, menuItem->colorX1, menuItem->colorY1,
                                menuItem->colorX2, menuItem->colorY2, c, b);
                colorAnimIdx++;
                colorAnimIdx = (uint16)colorAnimIdx % *colorTablePtr;
            }
        }

        if (!(menuItem->flags & MENUITEM_HAS_SPRITE)) continue;
        if (!(menuItem->flags & MENUITEM_SPRITE_BLINK)) continue;
        if (timerCounter3 <= 0x12) continue;
        timerCounter3 = 0;
        if (spriteToggle != 0) {
            switch (flightRecords[curRecordIdx].status & 0x3f) {
            case 1:
            case 12:
                spriteAirBlink->dstX = mapToScreenX(flightRecords[curRecordIdx].mapX) + mapWinX1 - 2;
                spriteAirBlink->dstY = mapToScreenY(flightRecords[curRecordIdx].mapY) + mapWinY1 - 2;
                if (slotInfoTable[(flightRecords[curRecordIdx].unitId & 0x7f) << 4] & 8) {
                    spriteAirBlink->srcX = 0x11e;
                } else {
                    spriteAirBlink->srcX = 0x12d;
                }
                gfx_blitSprite(spriteAirBlink);
                break;
            case 2:
                spriteSamBlink->dstX = mapToScreenX(flightRecords[curRecordIdx].mapX) + mapWinX1 - 2;
                spriteSamBlink->dstY = mapToScreenY(flightRecords[curRecordIdx].mapY) + mapWinY1 - 2;
                gfx_blitSprite(spriteSamBlink);
                break;
            case 4:
            case 5:
            case 6:
            case 7:
            case 10:
            case 11:
                spriteWaypointBlink->dstX = mapToScreenX(flightRecords[curRecordIdx].mapX) + mapWinX1;
                spriteWaypointBlink->dstY = mapToScreenY(flightRecords[curRecordIdx].mapY) + mapWinY1;
                gfx_blitSprite(spriteWaypointBlink);
                break;
            case 3:
            case 8:
                spriteGroundBlink->dstX = mapToScreenX(flightRecords[curRecordIdx].mapX) + mapWinX1 - 2;
                spriteGroundBlink->dstY = mapToScreenY(flightRecords[curRecordIdx].mapY) + mapWinY1 - 2;
                gfx_blitSprite(spriteGroundBlink);
                break;
            case 9:
            default:
                break;
            }
        } else {
            drawEventSprite(curRecordIdx);
        }
        spriteToggle = (spriteToggle == 0);
    }

input_done:
    if ((int8)misc_jump_5a_keybuf() == 0) {
        h = misc_jump_5b_getkey();
    } else {
        if (d == 1) {
            h = KEYCODE_ENTER;
        } else if (e == 1) {
            h = KEYCODE_ESC;
        } else if (joyAxisX < JOY_DEADZONE_LO) {
            h = KEYCODE_LEFTARROW;
            joyRepeatFlag = 1;
        } else if (joyAxisX > JOY_DEADZONE_HI) {
            h = KEYCODE_RIGHTARROW;
            joyRepeatFlag = 1;
        } else if (joyAxisY < JOY_DEADZONE_LO) {
            h = KEYCODE_UPARROW;
            joyRepeatFlag = 1;
        } else if (joyAxisY > JOY_DEADZONE_HI) {
            h = KEYCODE_DNARROW;
            joyRepeatFlag = 1;
        }
    }

    if ((int8)h == KEYCODE_ENTER) {
        enterPressed = 1;
    }
    if (h == KEYCODE_ALTQ) {
        quitFlag = 1;
        enterPressed = 1;
    }
    if (h == KEYCODE_UPARROW) {
        cursorY -= cursorBounds[1];
        if (cursorBounds[4] > (int16)cursorY) {
            cursorY = cursorBounds[4];
        }
        inputChanged = 1;
    }
    if (h == KEYCODE_DNARROW) {
        cursorY += cursorBounds[1];
        if (cursorY > (uint16)cursorBounds[5]) {
            cursorY = cursorBounds[5];
        }
        inputChanged = 1;
    }
    if (h == KEYCODE_RIGHTARROW) {
        cursorX += cursorBounds[0];
        if (cursorX > (uint16)cursorBounds[3]) {
            cursorX = cursorBounds[3];
        }
        inputChanged = 1;
    }
    if (h == KEYCODE_LEFTARROW) {
        cursorX -= cursorBounds[0];
        if (cursorBounds[2] > (int16)cursorX) {
            cursorX = cursorBounds[2];
        }
        if (cursorBounds[4] > (int16)cursorY) {
            cursorX += cursorBounds[0];
        }
        inputChanged = 1;
    }

    if (menuItem->flags & MENUITEM_HAS_SPRITE) {
        if (menuItem->flags & MENUITEM_SPRITE_BLINK) {
            drawEventSprite(curRecordIdx);
        }
    }
}

/* ==== seg000:0x4dde drawEventSprite — position + blit the always-on sprite
 * for flightRecords[rec].status&0x3f into the debrief map window. Jump-table
 * switch; the gfx_blitSprite tail is shared across cases. ==== */
void drawEventSprite(uint16 rec) {
    switch (flightRecords[rec].status & 0x3f) {
    case 1:
    case 12:
        spriteAir->dstX = mapToScreenX(flightRecords[rec].mapX) + mapWinX1 - 2;
        spriteAir->dstY = mapToScreenY(flightRecords[rec].mapY) + mapWinY1 - 2;
        if (slotInfoTable[(flightRecords[curRecordIdx].unitId & 0x7f) << 4] & 8) {
            spriteAir->srcX = 0x11e;
        } else {
            spriteAir->srcX = 0x12d;
        }
        gfx_blitSprite(spriteAir);
        break;
    case 2:
        spriteSam->dstX = mapToScreenX(flightRecords[rec].mapX) + mapWinX1 - 2;
        spriteSam->dstY = mapToScreenY(flightRecords[rec].mapY) + mapWinY1 - 2;
        gfx_blitSprite(spriteSam);
        break;
    case 4:
    case 5:
    case 6:
    case 7:
    case 10:
    case 11:
        spriteWaypoint->dstX = mapToScreenX(flightRecords[rec].mapX) + mapWinX1;
        spriteWaypoint->dstY = mapToScreenY(flightRecords[rec].mapY) + mapWinY1;
        gfx_blitSprite(spriteWaypoint);
        break;
    case 3:
    case 8:
        spriteGround->dstX = mapToScreenX(flightRecords[rec].mapX) + mapWinX1 - 2;
        spriteGround->dstY = mapToScreenY(flightRecords[rec].mapY) + mapWinY1 - 2;
        gfx_blitSprite(spriteGround);
        break;
    case 9:
    default:
        break;
    }
}

void blinkWidget(MenuItem *item, int16 *gfxPage) {
    int16 toColor;
    int16 fromColor;
    if (item->state == 0) {
        item->state = 1;
        fromColor = (uint16)item->colorPair >> 4;
        toColor = item->colorPair & 0xF;
        if (item->colorPair != 0) {
            gfx_switchColor(gfxPage, item->colorX1, item->colorY1, item->colorX2, item->colorY2, fromColor, toColor);
        }
    } else {
        item->state = 0;
        fromColor = item->colorPair & 0xF;
        toColor = (uint16)item->colorPair >> 4;
    }
    if (item->colorPair != 0) {
        gfx_switchColor(gfxPage, item->colorX1, item->colorY1, item->colorX2, item->colorY2, fromColor, toColor);
    }
}

void plotMapPoint(int16 x, int16 y, int16 color, int16 unused) {
    int16 sx, sy;
    (void)unused;
    sx = mapToScreenX(x);
    sy = mapToScreenY(y);
    if (color != -1 &&
        (uint16)sx >= (uint16)mapWinX1 &&
        (uint16)sx < (uint16)mapWinX2 &&
        (uint16)sy >= (uint16)mapWinY1 &&
        (uint16)sy < (uint16)mapWinY2) {
        drawMapPixel(sx, sy, color);
    }
}

void drawClippedLineEx(int16 x1, int16 y1, int16 x2, int16 y2, int16 wx1, int16 wx2, int16 wy1, int16 wy2, int16 flag) {
    int16 w, h;
    (void)flag;
    w = wx2 - wx1;
    h = wy2 - wy1;
    gfx_setBlitOffset(gfx_calcRowAddr(wx1, wy1));
    clipMaxX = w - 1;
    clipMaxY = h - 1;
    gfx_setOvlVal1(clipMaxY);
    gfx_setOvlVal2(clipMaxX);
    lineX1 = x1;
    lineY1 = y1;
    lineX2 = x2;
    lineY2 = y2;
    drawLineWrapper();
    gfx_nop23();
    clipMaxX = 319;
    clipMaxY = 199;
    gfx_setOvlVal1(199);
    gfx_setOvlVal2(clipMaxX);
    gfx_setBlitOffset(0);
}

void drawClippedLine(int16 x1, int16 y1, int16 x2, int16 y2) {
    drawClippedLineEx(x1, y1, x2, y2, mapWinX1, mapWinX2, mapWinY1, mapWinY2, 1);
}

void drawFlightLine(int16 p1, int16 p2, int16 p3, int16 p4) {
    drawClippedLineEx(mapToScreenX(p1), mapToScreenY(p2), mapToScreenX(p3), mapToScreenY(p4), mapWinX1, mapWinX2, mapWinY1, mapWinY2, 1);
}

/* ==== seg000:0x52f9 drawFlightPath — draw the mission route polyline over
 * flightRecords (loop1: lines), then the event sprites (loop2).
 * Returns final index - 1. ==== */
extern void far gfx_setColor(uint8 c);          /* 9D9:13F5 (slot 0x21) */

uint16 drawFlightPath(int16 *gfxPage, uint16 maxRecord) {
    int16 p, a, b, c, d, e, f, g, h, i, j, k, l, m, n;
    (void)e; (void)f; (void)g; (void)h; (void)i;
    (void)j; (void)k; (void)l; (void)m; (void)n;
    a = -1;
    while ((flightRecords[++a].status & 0x3f) != 0) {
        if ((uint16)a > maxRecord)
            goto drawSprites;
        gfx_setColor(0);
        if (a == 0) {
            plotMapPoint(flightRecords[0].mapX, flightRecords[0].mapY, 0, 0);
            b = flightRecords[0].mapX;
            d = flightRecords[0].mapY;
        } else {
            p = flightRecords[a].mapX;
            c = flightRecords[a].mapY;
            drawFlightLine(p, c, b, d);
            b = p;
            d = c;
        }
    }
drawSprites:
    a = -1;
    while ((flightRecords[++a].status & 0x3f) != 0) {
        if ((uint16)a > maxRecord)
            goto done;
        if ((flightRecords[a].status & 0x3f) != 9)
            drawEventSprite(a);
    }
done:
    a--;
    return a;
}

char *formatFlightTime(int16 timeValue, char *buffer) {
    int16 hours, miscBits, minutes, seconds;

    miscBits = targetBlock.target1MiscBits[0] + targetBlock.target2MiscBits[0];
    nightMission = ((char)miscBits & 3) == 0;
    if (targetBlock.target1Type[0] == 1 || targetBlock.target2Type[0] == 1) {
        nightMission = 0;
    }
    if (targetBlock.target1Type[0] == 4 || targetBlock.target2Type[0] == 4) {
        nightMission = 1;
    }
    timeValue += (miscBits & 0xF) << 8;
    mystrcpy(buffer, "00:00:00");
    hours = (uint16)timeValue / 1800;
    buffer[0] += nightMission + 1;
    buffer[1] += hours % 10;
    minutes = ((uint16)timeValue / 30) % 60;
    buffer[3] += minutes / 10;
    buffer[4] += minutes % 10;
    seconds = ((uint16)timeValue * 2) % 60;
    buffer[6] += seconds / 10;
    buffer[7] += seconds % 10;
    return buffer;
}

/* ==== seg000:0x27f6 — set the map view origin (screen cell col/row via
 * mapViewX1/mapViewY1) then run the big map view dispatcher runMapView(sel);
 * return its status as 1/0. ==== */
extern int16 runMapView(int16 sel);                     /* 0x2192 */

int16 drawMapView(int16 viewY, int16 viewX, int16 sel) {
    mapViewX1 = viewX;
    mapViewY1 = viewY;
    if (runMapView(sel))
        return 1;
    return 0;
}

/* ==== seg000:0x281b serviceTick — every-7th-tick record animation gate.
 * Naked fn (no bp frame): no args, no pushes. START sub_161F1 twin. ==== */
extern void tickRecords(void);                          /* 0x2832 */

void serviceTick(void) {
    if (tickByte > 6) {
        tickByte = 0;
        if (tickArm != 0)
            tickRecords();
    }
}

/* ==== seg000:0x2832 — walk 0x5C-stride record table at dseg+0x295e;
 * for flagged records sum bytes +8/+9, wrap >0xff by -0x100, call the
 * per-record worker tickRecAnim(recOff), store sum back at +9. ==== */

/* ==== seg000:0x288b — per-record animation worker ====
 * Interprets the record's text-DSL sprite script (word_19DE8 = rec+0x58 is the
 * script's dseg offset) one opcode per call.  A channel stack is threaded
 * through byte index rec+0xC: per channel a strPos (byte_19DA7 = rec+0x17) and
 * a repeat counter (byte_19D9D = rec+0xD).  Opcodes: a digit-run stores N-1
 * into the channel's repeat counter; '<' '>' wrap-adjust frame counter cntA
 * (word_19D9A = rec+0xA) against maxA (word_19D94 = rec+4), '^' '_' do the same
 * for cntB/maxB (rec+0xB / rec+6); 'A'..'Z' draw the frame (op&0x1f)-1 via
 * sub_12F27 — one clipped blit when both counters are 0 else a 2x2 panning
 * tile offset by cntA/cntB; '(' opens a '(alt|alt:N|…)' weighted-random group
 * (channel push + parseCmd scan + randomRange%count + forward re-scan),
 * ')' / '|' pop back to the saved strPos while a repeat remains else skip to
 * the matching ')'.  Returns after one draw (doneFlag) or group re-entry. ==== */
/* record fields as tickRecAnim addresses them (recOff = record byte offset) */
extern int16 *word_1BAD2;      /* sprite slot A (draw op src) — a seg-slot ptr */
extern int16 *word_1BAD4;      /* sprite slot B (draw op dst) — a seg-slot ptr */
extern void   sub_12F27(int16 *p1, int16 a2, int16 a3, int16 *p4,
                        int16 a5, int16 a6, int16 a7, int16 a8);
extern int16  randomRange(int16 maxVal);
int16  parseCmd(uint8 *base, int16 *pc);

void tickRecAnim(struct AnimRec *recOff) {
    /* local names chosen so MSC's name-hash slot order matches the ref frame */
    int16 num, opb, chni, sp, fn, depth, tmpf, dnf, accb;
    uint8 nxt;
    uint8 *strm;

    dnf = 0;
    strm = (uint8 *)f19_dsegAt((uint16)recOff->strOff);
    chni = recOff->chanIdx;
    if (recOff->repeat[chni] != 0) {
        sp = recOff->strPos[chni];
        recOff->repeat[chni]--;
    } else {
        sp = recOff->strPos[chni] + 1;
    }
    goto looptest;

    for (;;) {
readop:                                     /* loc_128D0 */
        opb = strm[sp++];
    if (opb >= '0' && opb <= '9') {
        num = opb - '0';
        goto digpeek;                       /* loc_12915 */
digbody:                                    /* loc_128F2 — accumulate a digit */
        if (nxt > '9')
            goto digdone;
        num = num * 10 + strm[sp++] - '0';
digpeek:                                    /* loc_12915 — peek next char */
        nxt = strm[sp];
        if (nxt >= '0')
            goto digbody;
digdone:                                    /* loc_12924 */
        recOff->repeat[chni] = num - 1;
        goto looptest;
    }

coloncheck:                                 /* loc_12935 */
    if (opb == ':') {
colonpeek:                                  /* loc_1293B — ':' skips a digit run */
        nxt = strm[sp];
        if (nxt < '0')
            goto looptest;
        if (nxt > '9')
            goto looptest;
        sp++;                               /* loc_12954 */
        goto colonpeek;
    }
    else
        goto coldispatch;                   /* loc_12959 */

coldispatch:                                /* loc_12959 — <>^_ counter ops */
    if (opb == '<' || opb == '>' || opb == '^' || opb == '_') {
        switch (opb) {                      /* loc_12974 — re-dispatch */
        case '>':                           /* loc_1298E */
            recOff->cntA--;
            if (recOff->cntA == 0xFF)
                recOff->cntA = recOff->maxA - 1;
            goto looptest;
        case '<':                           /* loc_129AD */
            recOff->cntA++;
            if (recOff->cntA == recOff->maxA)
                recOff->cntA = 0;
            goto looptest;
        case '_':                           /* loc_129C9 */
            recOff->cntB--;
            if (recOff->cntB == 0xFF)
                recOff->cntB = recOff->maxB - 1;
            goto looptest;
        case '^':                           /* loc_129E8 */
            recOff->cntB++;
            if (recOff->cntB == recOff->maxB)
                recOff->cntB = 0;
            goto looptest;
        }
        goto looptest;                      /* loc_12CB1 via 298B */
    }
    goto lettercheck;                       /* loc_12A04 */

lettercheck:                                /* loc_12A04 — 'A'..'Z' */
    if (opb >= 'A' && opb <= 'Z') {
        fn = (opb & 0x1F) - 1;
        if ((recOff->cntA | recOff->cntB) == 0) {
            /* loc_12A31 — single clipped blit (base pos, full extent) */
            sub_12F27((int16 *)word_1BAD4, recOff->frameW[fn],
                      recOff->frameB[fn], (int16 *)word_1BAD2,
                      recOff->posX + word_23786,
                      recOff->posY + word_23788,
                      recOff->maxA, recOff->maxB);
        } else {
            /* loc_12A5B — 2x2 panning tile */
            sub_12F27((int16 *)word_1BAD4, recOff->frameW[fn],
                      recOff->frameB[fn], (int16 *)word_1BAD2,
                      recOff->maxA + recOff->posX + word_23786 - recOff->cntA,
                      recOff->posY + recOff->maxB + word_23788 - recOff->cntB,
                      recOff->cntA, recOff->cntB);
            /* loc_12AA7 */
            sub_12F27((int16 *)word_1BAD4, recOff->frameW[fn] + recOff->cntA,
                      recOff->frameB[fn], (int16 *)word_1BAD2,
                      recOff->posX + word_23786,
                      recOff->posY + recOff->maxB + word_23788 - recOff->cntB,
                      recOff->maxA - recOff->cntA, recOff->cntB);
            /* loc_12B0F */
            sub_12F27((int16 *)word_1BAD4, recOff->frameW[fn],
                      recOff->frameB[fn] + recOff->cntB, (int16 *)word_1BAD2,
                      recOff->maxA + recOff->posX + word_23786 - recOff->cntA,
                      recOff->posY + word_23788,
                      recOff->cntA, recOff->maxB - recOff->cntB);
            /* loc_12B52 — shared 'push word_1BAD4; call' tail for loc_12A31 */
            sub_12F27((int16 *)word_1BAD4, recOff->frameW[fn] + recOff->cntA,
                      recOff->frameB[fn] + recOff->cntB, (int16 *)word_1BAD2,
                      recOff->posX + word_23786,
                      recOff->posY + word_23788,
                      recOff->maxA - recOff->cntA, recOff->maxB - recOff->cntB);
        }
        recOff->strPos[chni] = sp - 1;      /* 2B9E — save pos, done */
        dnf = 1;
        goto looptest;
    }
    /* loc_12BB4 — group ops: ( ) | \0 */
    if (opb == '(') {                       /* loc_12BBA — open random group */
        recOff->strPos[chni++] = sp - 1;
        accb = 1;
        word_22F0A = sp;
grp_count:                                  /* loc_12BD6 — count alternatives */
        fn = parseCmd(strm, &word_22F0A);
        accb += fn;
        if (fn != 0)
            goto grp_count;
        accb = randomRange(-1) % accb;      /* pick weighted index */
        word_22F0A = sp;
        goto grp_test;
grp_skip:                                   /* loc_12C08 — walk to chosen alt */
        accb -= parseCmd(strm, &word_22F0A);
grp_test:                                   /* loc_12C18 */
        if (accb > 0)
            goto grp_skip;
        sp = word_22F0A;
        goto looptest;
    }
    if (opb == '|' || opb == ')') {         /* loc_12C30 — pop channel */
        chni--;
        if (recOff->repeat[chni] == 0) {    /* ==0 — skip to matching ')' */
            sp = recOff->strPos[chni] + 1;
            depth = 1;
            do {                            /* loc_12C4D */
                depth += (strm[sp] == '(');
                depth -= (strm[sp] == ')');
                sp++;
            } while (depth > 0);
            goto looptest;
        }
        /* loc_12C7C — still repeating */
        recOff->repeat[chni]--;
        sp = recOff->strPos[chni];
        goto looptest;
    }
    if (opb == 0) {                         /* loc_12C93 — end of script */
        chni = 0;
        recOff->strPos[0] = 0;
        recOff->repeat[0] = 0;
        sp = 0;
        goto looptest;
    }
    goto looptest;

looptest:                                   /* loc_12CB1 */
        if (dnf != 0)
            break;
        goto readop;
    }
    recOff->chanIdx = chni;                 /* loc_12CBA */
}

void tickRecords(void) {
    int16 rp, total, i;
    for (i = 0; i < recCount; i++) {
        rp = i * 0x5C + 0x295E;
        if (recActive[rp] != 0) {
            total = recField9[rp] + recField8[rp];
            if (total > 0xFF)
                total -= 0x100;
            tickRecAnim((struct AnimRec *)f19_dsegAt((uint16)rp));
            recField9[rp] = (uint8)total;
        }
    }
}

/* ==== seg000:0x2cc9 — parse a '(…|…)'/':N' command char stream via the
 * cursor pointer pp (read col idx, advance *pp each step).
 * Returns 0 on ')', 1 on '|', N-1 after ':N', skips nested parens. ==== */
int16 parseCmd(uint8 *base, int16 *pc) {
    int8 c, ch2;
    int16 dig, depth, i;
    i = 0;
    for (;;) {
        c = base[*pc];
        (*pc)++;
        if (c == ')')
            return 0;
        if (c == '|')
            return 1;
        if (c == ':') {
            dig = 0;
            goto test;
        body:
            if (ch2 > '9')
                goto done;
            dig = dig * 0xA + ch2 - '0';
            (*pc)++;
        test:
            if ((ch2 = base[*pc]) >= '0')
                goto body;
        done:
            return dig - 1;
        }
        if (c != '(')
            continue;
        depth = 1;
        do {
            depth += (base[*pc] == '(');
            depth -= (base[*pc] == ')');
            (*pc)++;
        } while (depth > 0);
    }
}

/* ==== seg000:0x2d8d — deactivate records whose bbox intersects the
 * (x,y,w,h) rect shifted by the map view origin ==== */

void clearActiveInRect(int16 x, int16 y, int16 w, int16 h) {
    int16 c, d, e, i, xmax;
    struct MapRect *rp;
    x -= mapViewX1;
    y -= mapViewY1;
    for (i = 0; i < recCount; i++) {
        rp = &recTable[i];
        if (rp->rx > x)
            xmax = recTable[i].rx;
        else
            xmax = x;
        if (rp->ry > y)
            e = rp->ry;
        else
            e = y;
        d = (rp->rw + rp->rx <= x + w) ? rp->rw + rp->rx : x + w;
        c = (rp->ry + rp->rh <= y + h) ? rp->ry + rp->rh : y + h;
        if (xmax < d && e < c)
            rp->active = 0;
    }
}

/* ==== seg000:0x2e27 — clear field9 of all 30 records ==== */
void resetRecField9(void) {
    int16 r, i;
    for (i = 0; i < 0x1E; i++) {
        r = i * 0x5C + 0x295E;
        recField9[r] = 0;
    }
}

/* ==== seg000:0x8532 — promotion eligibility scan ====
 * Scans rank thresholds top-down while i >= rank; on qualification either
 * defers (award screen open) or commits the rank bump. Rank 5 (General)
 * requires exactly 99 missions. The cold gate block sits in the loop's
 * init-jump gap — reached only by backward jumps, hence the for(;;) label. */
void checkPromotion(void) {
    int16 i, j;
    if (commData->trainingFlag == 1)
        return;
    i = 5;
    goto test;
    for (;;) {
gate:
        if (pilotRec->rank >= 5)
            return;
        if (promoScreenOpen != 1)
            goto promote;
        promotionPending = 1;
        return;
step:
        i--;
test:
        if ((int16)pilotRec->rank <= i) {
            if ((uint32)promoScoreMin[i] <= (uint32)pilotRec->totalScore &&
                promoMissionsMin[i] <= pilotRec->missionCount &&
                (uint32)pilotRec->totalScore / (uint32)pilotRec->missionCount >=
                (uint32)promoAvgMin[i])
                goto qual;
            goto step;
        }
        return;
    }
qual:
    if (pilotRec->rank != 5)
        goto gate;
    if (pilotRec->missionCount == 99)
        goto promote;
    goto gate;
promote:
    promotionDone = 1;
    pilotRec->rank++;
}

/* ==== seg000:0x85e3 — award/medal eligibility check ====
 * Frameless: no locals, no args. Runs after checkPromotion in the debrief
 * flow. Mission ribbons (5-9/10+/30-59/60+ missions) only outside training;
 * the point-tier awards (codes 1-5) and the code-6 flag award queue via
 * awardQueued when the promo screen is up or in training, else awardCode. */
void checkAwardCodes(void) {
    if (pilotRec->flag44 == 0 && commData->trainingFlag == 1 &&
        (target1Scored == 1 || target2Scored == 1)) {
        pilotRec->flag44 = 1;
        awardTrained = 1;
    }
    if (commData->trainingFlag == 0)
        goto ribbons;
    goto noRibbons;
ribbons:
    if (pilotRec->missionCount >= 5 && pilotRec->missionCount < 10 &&
        pilotRec->flag46 == 0) {
        pilotRec->flag46 = 1;
        missionRibbon = 1;
    }
    if (pilotRec->missionCount >= 10 && pilotRec->flag48 == 0) {
        pilotRec->flag46 = 0;
        pilotRec->flag48 = 1;
        missionRibbon = 2;
    }
    if (pilotRec->missionCount >= 30 && pilotRec->missionCount < 60 &&
        pilotRec->flag4a == 0) {
        pilotRec->flag4a = 1;
        missionRibbon = 3;
    }
    if (pilotRec->missionCount >= 60 && pilotRec->flag4c == 0) {
        pilotRec->flag4a = 0;
        pilotRec->flag4c = 1;
        missionRibbon = 4;
    }
noRibbons:
    if (pilotRec->flag22 == 0 && commData->commField36 >= 7 &&
        (commData->commFlags34 & 8) != 0 && (commData->commFlags34 & 4) != 0) {
        pilotRec->flag22 = 1;
        award6Flag = 1;
        awardCode = 6;
    }
    if (pilotRec->flag2c == 0 && pilotRec->awardPoints > 0x4B0) {
        if (promoScreenOpen == 1 || commData->trainingFlag == 1) {
            awardQueued = 5;
            return;
        }
        awardCode = 5;
        pilotRec->flag2c = 1;
        return;
    }
    if (900 * pilotRec->award2a + 900 <= pilotRec->awardPoints) {
        if (promoScreenOpen == 1 || commData->trainingFlag == 1) {
            awardQueued = 4;
            return;
        }
        pilotRec->award2a++;
        awardCode = 4;
        return;
    }
    if (600 * pilotRec->award28 + 600 <= pilotRec->awardPoints) {
        if (promoScreenOpen == 1 || commData->trainingFlag == 1) {
            awardQueued = 3;
            return;
        }
        pilotRec->award28++;
        awardCode = 3;
        return;
    }
    if (300 * pilotRec->award26 + 300 <= pilotRec->awardPoints) {
        if (promoScreenOpen == 1 || commData->trainingFlag == 1) {
            awardQueued = 2;
            return;
        }
        pilotRec->award26++;
        awardCode = 2;
        return;
    }
    if (pilotRec->award24 < 9 && 100 * pilotRec->award24 + 100 <= pilotRec->awardPoints) {
        if (promoScreenOpen == 1 || commData->trainingFlag == 1) {
            awardQueued = 1;
            return;
        }
        pilotRec->award24++;
        awardCode = 1;
        return;
    }
}

/* ==== seg000:0x883c sub_1883C — pilot-death debrief screen (landingType 1).
 * flag.pic + bailout-cause message, centered or wrapped. ==== */
void sub_1883C(void) {
    int16 p;
    uint16 w;
    char  msg[0xc8];

    gfx_setFadeSteps(3);
    openBlitClosePic("flag.pic", word_23C6C);
    p = word_23C6C;
    gfx_waitRetrace();
    gfx_blitToCurrent(p);
    gfx_flipPage();
    switch (commData->bailout) {
    case 1:
        mystrcpy(msg, "Flying into the ground has proved to be hazardous to your health.");
        break;
    case 2:
        mystrcpy(msg, "Flying into that hill has proved to be hazardous to your health.");
        break;
    case 3:
        mystrcpy(msg, "Your plane crashed because the main fuel tanks were empty.");
        break;
    case 4:
        mystrcpy(msg, "Your aircraft, destroyed by enemy missiles, crashed before you ejected.");
        break;
    case 5:
        mystrcpy(msg, "Your plane crashed onto the runway, cartwheeled and exploded.");
        break;
    case 6:
        mystrcpy(msg, "Unfortunately, your attempt to eject from the aircraft failed.");
        break;
    }
    word_207B2[2] = 0xf;
    w = stringWidth(word_207B2, (uint8 *)msg);
    if (w < 0x13d)
        drawStringAt(word_207B2, msg, (0x13e - w) / 2 + 1, 0xa5);
    else
        drawWrappedText(word_207B2, msg, 0x12c, 0x21, 0xa, 7);
    word_207B2[2] = 9;
    drawStringAt(word_207B2, "Press Selector to continue", 0x64, 0xc1);
    word_207B2[2] = 0;
    gfx_commitPage();
    waitForKeyOrJoy();
}

/* ==== seg000:0x44a8 drawMenuItem — debrief detail panel. Type-7 items draw
 * the mission-complete summary (route replay + overall rating); blink items
 * draw the current flightRecords event text (switch on status&0x3f), the
 * PRIMARY/SECNDRY objective tags and the cumulative rating, then the "next
 * mission event" prompt. f15 enbrief.c drawMenuItem lineage. Locals
 * c,e,f,g,h,i,j,k,l are unused frame fillers matching the original frame. ==== */
void drawMenuItem(const MenuItem *items, uint16 index, int16 *gfxPage) {
    char p[2];
    char a[2];
    char b[2];
    char d[2];
    int16 c, e, f, g, h, i, j, k, l;
    uint16 m;
    char numBuf[22];
    uint16 n;

    p[0] = 0x0a;
    p[1] = 0;
    b[0] = 0x89;
    b[1] = 0;
    a[0] = 0x8d;
    a[1] = 0;
    d[0] = 0x80;
    d[1] = 0;
    (void)c; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; (void)l;

    if ((items[index].flags & MENUITEM_HAS_SPRITE) != 0) {
        if ((items[index].flags & MENUITEM_TYPE_MASK) == 7) {
            sub_10E50(gfxPage, 0xeb, 0xa, 0x13f, 0x95);
            gfxPage[2] = 0;
            mystrcpy(scoreString, b);
            mystrcat(scoreString, "Press Selector to exit Debriefing");
            drawWrappedText(gfxPage, scoreString, 80, 240, 130, 8);
            sub_10E50(gfxPage, 0xf0, 0x64, 0x12c, 0x7e);
            if (popupVisible == 1) {
                gfx_copyRect(1, 0, 0x96, 0, popupX, popupY, 0x30, 0x28);
                popupVisible = 0;
            }
            curRecordIdx = 0;
            totalFlightRecords = drawFlightPath(gfxPage, 0x270f);
            missionScore = calcMissionScore(totalFlightRecords);
            mystrcpy(scoreString, "\x8d");
            mystrcat(scoreString, "OVERALL");
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x64);
            mystrcpy(scoreString, "MISSION RATING");
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x6c);
            mystrcpy(scoreString, "\x80");
            my_ltoa(missionScore, numBuf);
            mystrcat(scoreString, numBuf);
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x74);
            ejectedFlag = 1;
        }
        if ((items[index].flags & MENUITEM_SPRITE_BLINK) == 0)
            return;
        if (ejectedFlag == 1) {
            ejectedFlag = 0;
            popupVisible = 0;
            gfx_blitSprite(spriteMapArea);
            curRecordIdx = prevDrawX = prevDrawY = 0;
            sub_10E50(gfxPage, 0xeb, 0xa, 0x13f, 0x95);
            missionScore = calcMissionScore(0x100);
            mystrcpy(scoreString, "\x8d");
            mystrcat(scoreString, "OVERALL");
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x64);
            mystrcpy(scoreString, "MISSION RATING");
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x6c);
            mystrcpy(scoreString, "\x80");
            my_ltoa(missionScore, numBuf);
            mystrcat(scoreString, numBuf);
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x74);
        }
        sub_10E50(gfxPage, 0xeb, 0xa, 0x13f, 0x63);
        gfxPage[2] = 0x0d;
        mystrcpy(scoreString, "MISSION EVENT");
        n = stringWidth(gfxPage, (uint8 *)scoreString);
        drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x14);
        mystrcpy(scoreString, "TIME: \x80");
        mystrcat(scoreString, formatFlightTime(flightTimeTable[curRecordIdx * 3], numBuf));
        drawStringAt(gfxPage, scoreString, 0xf0, 0x1e);
        m = flightRecords[curRecordIdx].unitId & 0x7f;
        switch (flightRecords[curRecordIdx].status & 0x3f) {
        case 1:
        case 12:
            if (worldObjects[m].unitRef != 0) {
                mystrcpy(scoreString, worldStrings[worldObjects[m].unitRef]);
                mystrcat(scoreString, " ");
                mystrcat(scoreString, worldStrings[worldObjects[m].objectIdx & 0x7f]);
                mystrcat(scoreString, " destroyed");
            } else {
                mystrcpy(scoreString, worldStrings[worldObjects[m].objectIdx & 0x7f]);
                mystrcat(scoreString, " destroyed");
            }
            break;
        case 3:
            mystrcpy(scoreString, planeArray[m].name);
            mystrcat(scoreString, " ");
            mystrcat(scoreString, &planeArray[m].name[7]);
            mystrcat(scoreString, " shot down");
            break;
        case 2:
            mystrcpy(scoreString, worldStrings[m]);
            mystrcat(scoreString, " destroyed");
            break;
        case 11:
            mystrcpy(scoreString, "Cargo delivered");
            break;
        case 10:
            if (worldObjects[m].unitRef != 0) {
                mystrcpy(scoreString, worldStrings[worldObjects[m].unitRef]);
                mystrcat(scoreString, " ");
                mystrcat(scoreString, worldStrings[worldObjects[m].objectIdx & 0x7f]);
                mystrcat(scoreString, " photographed");
            } else {
                mystrcpy(scoreString, worldStrings[worldObjects[m].objectIdx & 0x7f]);
                mystrcat(scoreString, " photographed");
            }
            break;
        case 5:
            mystrcpy(scoreString, "Hit by ");
            mystrcat(scoreString, samWeaponTable[m].name);
            mystrcat(scoreString, " missile");
            break;
        case 7:
            if (worldObjects[m].unitRef != 0) {
                mystrcpy(scoreString, worldStrings[worldObjects[m].unitRef]);
                mystrcat(scoreString, " ");
                mystrcat(scoreString, worldStrings[worldObjects[m].objectIdx & 0x7f]);
                mystrcat(scoreString, " Track");
            } else {
                mystrcpy(scoreString, worldStrings[worldObjects[m].objectIdx & 0x7f]);
                mystrcat(scoreString, " Radar Track");
            }
            break;
        case 6:
            mystrcpy(scoreString, planeArray[m].name);
            mystrcat(scoreString, " ");
            mystrcat(scoreString, &planeArray[m].name[7]);
            mystrcat(scoreString, " Visual ID");
            break;
        case 4:
            mystrcpy(scoreString, wpnNames[commData->slotWpn[m]]);
            mystrcat(scoreString, " ");
            mystrcat(scoreString, wpnNames[commData->slotWpn[m]] + 0x0a);
            mystrcat(scoreString, " released");
            break;
        case 8:
            if (curRecordIdx == 0) {
                mystrcpy(scoreString, "Takeoff point:");
                if (worldObjects[targetBlock.waypointData].unitRef != 0) {
                    mystrcat(scoreString, worldStrings[worldObjects[targetBlock.waypointData].unitRef]);
                } else {
                    mystrcat(scoreString, worldStrings[(uint8)worldObjects[targetBlock.waypointData].objectIdx]);
                }
            } else {
                mystrcpy(scoreString, "Mission end:\n");
                switch (commData->landingType) {
                case 1:
                    mystrcat(scoreString, "Crashed");
                    break;
                case 2:
                    if (commData->bailout == 0 && missionResult != 0) {
                        mystrcat(scoreString, "Good Bailout");
                    } else if (commData->bailout == 0 && missionResult == 0) {
                        mystrcat(scoreString, "Captured");
                    } else {
                        mystrcat(scoreString, "Bailed & Died");
                    }
                    break;
                case 3:
                    mystrcat(scoreString, "Good Landing");
                    break;
                }
            }
            break;
        }
        drawWrappedText(gfxPage, scoreString, 80, 240, 0x26, 8);
        if ((uint8)flightRecords[curRecordIdx].status & 0x80) {
            mystrcpy(scoreString, "\x8c" "PRIMARY OBJECTIVE");
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, gfxPage[5]);
        }
        if ((uint8)flightRecords[curRecordIdx].status & 0x40) {
            mystrcpy(scoreString, "\x8c" "SECNDRY OBJECTIVE");
            n = stringWidth(gfxPage, (uint8 *)scoreString);
            drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, gfxPage[5]);
        }
        missionScore = calcMissionScore(curRecordIdx);
        mystrcpy(scoreString, "\x8d");
        mystrcat(scoreString, "CUMULATIVE");
        n = stringWidth(gfxPage, (uint8 *)scoreString);
        drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x46);
        mystrcpy(scoreString, "MISSION RATING");
        n = stringWidth(gfxPage, (uint8 *)scoreString);
        drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x4e);
        mystrcpy(scoreString, "\x80");
        my_ltoa(missionScore, numBuf);
        mystrcat(scoreString, numBuf);
        n = stringWidth(gfxPage, (uint8 *)scoreString);
        drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x56);
        sub_15D1B();
        mystrcpy(scoreString, b);
        mystrcat(scoreString, "Press Selector for next mission event");
        drawWrappedText(gfxPage, scoreString, 80, 240, 130, 8);
    }
}

/* sub_14F7B (seg000:0x4f7b) — animate the in-flight track in the debrief window.
 * Same routine as f15 animateFlightPath plus the campaign-mode gate that draws
 * track segments for EVENT_BOMB_HIT/EVENT_EJECTED records (status 7/6) when
 * pilotRec->field3a == 2. */
void animateFlightPath(int16 *gfxPage) {
    char numBuf[22];
    int16 n;
    uint8 evt;

    if (popupVisible == 1) {
        gfx_copyRect(1, 0, 0x96, 0, popupX, popupY, 0x30, 0x28);
        popupVisible = 0;
    }
top:
    sub_10E50(gfxPage, 0xe9, 0x1e, 0x13f, 0x45);
    drawStringAt(gfxPage, "\x80In-Flight", 0xf0, 0x26);
loop_top:
    if (flightRecords[++curRecordIdx].status & 0x3f) {
        if ((flightRecords[curRecordIdx].status & 0x3f) != 9) goto gate;
        sub_10E50(gfxPage, 0xf0, 0x1e, 0x13f, 0x25);
        mystrcpy(scoreString, "\x8dTIME: \x80");
        mystrcat(scoreString, formatFlightTime(flightTimeTable[curRecordIdx * 3], numBuf));
        drawStringAt(gfxPage, scoreString, 0xf0, 0x1e);
        gfx_setColor(0);
        if (prevDrawX == 0 && prevDrawY == 0) {
            drawFlightLine(flightRecords[0].mapX, flightRecords[0].mapY,
                           flightRecords[curRecordIdx].mapX, flightRecords[curRecordIdx].mapY);
            prevDrawX = flightRecords[curRecordIdx].mapX;
            prevDrawY = flightRecords[curRecordIdx].mapY;
        } else {
            lastDrawX = flightRecords[curRecordIdx].mapX;
            lastDrawY = flightRecords[curRecordIdx].mapY;
            drawFlightLine(lastDrawX, lastDrawY, prevDrawX, prevDrawY);
            prevDrawX = lastDrawX;
            prevDrawY = lastDrawY;
        }
        missionScore = calcMissionScore(curRecordIdx);
        mystrcpy(scoreString, "\x80");
        my_ltoa(missionScore, numBuf);
        mystrcat(scoreString, numBuf);
        n = stringWidth(gfxPage, (uint8 *)scoreString);
        sub_10E50(gfxPage, 0xe8, 0x56, 0x13f, 0x5e);
        drawStringAt(gfxPage, scoreString, 0xe8 + (0x57 - n) / 2, 0x56);
        timerCounter = 0;
wait_loop:
        if (timerCounter <= 5) { timerYield(); goto wait_loop; }
        goto loop_top;
    }
gate:
    if (pilotRec->field3a == 2) {
        evt = flightRecords[curRecordIdx].status & 0x3f;
        if (evt == 7 || evt == 6) {
            gfx_setColor(0);
            if (prevDrawX == 0 && prevDrawY == 0) {
                drawFlightLine(flightRecords[0].mapX, flightRecords[0].mapY,
                               flightRecords[curRecordIdx].mapX, flightRecords[curRecordIdx].mapY);
                prevDrawX = flightRecords[curRecordIdx].mapX;
                prevDrawY = flightRecords[curRecordIdx].mapY;
                goto trtail;
            } else {
                goto trseg;
            }
        }
    }
    goto done;
trseg:
    lastDrawX = flightRecords[curRecordIdx].mapX;
    lastDrawY = flightRecords[curRecordIdx].mapY;
    drawFlightLine(lastDrawX, lastDrawY, prevDrawX, prevDrawY);
    prevDrawX = lastDrawX;
    prevDrawY = lastDrawY;
trtail:
    goto top;
done:
    if (!(flightRecords[curRecordIdx].status & 0x3f)) {
        curRecordIdx--;
    }
    gfx_setColor(0);
    if (prevDrawX == 0 && prevDrawY == 0) {
        drawFlightLine(flightRecords[0].mapX, flightRecords[0].mapY,
                       flightRecords[curRecordIdx].mapX, flightRecords[curRecordIdx].mapY);
        prevDrawX = flightRecords[curRecordIdx].mapX;
        prevDrawY = flightRecords[curRecordIdx].mapY;
    } else {
        lastDrawX = flightRecords[curRecordIdx].mapX;
        lastDrawY = flightRecords[curRecordIdx].mapY;
        drawFlightLine(lastDrawX, lastDrawY, prevDrawX, prevDrawY);
        prevDrawX = lastDrawX;
        prevDrawY = lastDrawY;
    }
}

/*
 * calcMissionScore
 *
 * Walks the flight log, accumulates a 32-bit score and updates the
 * mission-statistic counters.  The armed/unarmed ground- and air-unit
 * arms are deliberately shaped with labels and gotos so MSC 5.1 emits the
 * original dispatch topology: the armed arm jumps into the shared
 * score-add tail (loc_15814) and the award-unit arm jumps back into the
 * shared unauthorized-ground increment (loc_158AB).
 */
int32 calcMissionScore(int16 param)
{
    int16 unitId;
    int32 score;
    uint16 i;
    int16 cnt, ejected;

    word_2387A = 0x1318;
    word_22C34 = word_2379C = 0;
    ms_unauthAir = ms_unauthGround = ms_airKilled = ms_groundKilled =
        ms_friendlyAir = ms_friendlyGnd = ms_civilian = target1Scored = target2Scored = 0;
    cnt = 0;
    ejected = 0;
    score = 0;

    for (i = 0;
         i <= (uint16)param && flightRecords[i].status != 0;
         i++) {
        unitId = flightRecords[i].unitId;
        switch (flightRecords[i].status & 0x3f) {
        case 8:                                             /* bailout/landing */
            if (score < 0)
                score = 0;
            if (i != 0)
                ejected = 1;
            break;
        case 10:
            if (flightRecords[i].status & 0x80) {           /* primary target */
                score += awardPrim[pilotRec->field3a];
                target1Scored = 1;
                if (target2Scored == 1) {
                    score -= awardSec[0];
                    score += awardSec[1];
                }
            } else if (flightRecords[i].status & 0x40) {    /* secondary target */
                score += awardSec[target1Scored];
                target2Scored = 1;
            }
            break;
        case 11:
            if (flightRecords[i].status & 0x80) {
                score += awardPrim[pilotRec->field3a];
                target1Scored = 1;
                if (target2Scored == 1) {
                    score -= awardSec[0];
                    score += awardSec[1];
                }
            } else if (flightRecords[i].status & 0x40) {
                score += awardSec[target1Scored];
                target2Scored = 1;
            }
            break;
        case 9:                                             /* waypoint visit */
            if (gridFlags[flightRecords[i].mapY >> 4]
                         [flightRecords[i].mapX >> 4] & 3)
                break;
            if (pilotRec->field3a == 0) {
                score += 1;
            } else if (pilotRec->field3a == 1) {
                cnt++;
                if ((cnt & 3) == 0)
                    score += 1;
            }
            break;
        case 1:
        case 12:                                            /* ground-unit kill */
            if (*(int16 *)&slotInfoTable[unitId * 16] & 0x1000) {
                word_22C34--;
                score -= awardVisId[pilotRec->field3a];
            }
            if (flightRecords[i].status & 0x80) {
                score += awardPrim[pilotRec->field3a];
                target1Scored = 1;
                if (target2Scored == 1)
                    goto award_sec;
                ms_groundKilled++;
                break;
        award_sec:
                score -= awardSec[0];
                score += awardSec[1];
                ms_groundKilled++;
            } else if (flightRecords[i].status & 0x40) {
                score += awardSec[target1Scored];
                target2Scored = 1;
                ms_groundKilled++;
            } else {
                if (unitTypeTable[unitId & 0x7f] & 0x40) {
                    score += awardFriendlyGnd[pilotRec->field3a];
                    ms_friendlyGnd++;
                } else if (!(*(int16 *)&slotInfoTable[unitId * 16] & 0x500)) {
                    if (*(int16 *)&slotInfoTable[unitId * 16] & 0x1000) {
                        if (pilotRec->field3a == 0)
                            goto score_armed;
                    }
                    if (pilotRec->field3a != 0) {
                score_armed:
                        score += awardArmedGnd[pilotRec->field3a];
                        ms_groundKilled++;
                        break;
                    } else {
                        goto score_unarmed;
                    }
                score_unarmed:
                        score += awardUnarmedGnd[pilotRec->field3a];
                        if (awardUnarmedGnd[pilotRec->field3a] != 0) {
                    unauth_ground_inc:
                            ms_unauthGround++;
                        }
                        break;
                } else {
                    score += awardFriendlyGnd[pilotRec->field3a];
                    ms_friendlyGnd++;
                }
            }
            break;
        case 3:                                             /* air-unit kill */
            if (unitId & 0x80) {
                word_2379C--;
                score -= awardRadarId[pilotRec->field3a];
            }
            if (flightRecords[i].status & 0x80) {
                score += awardPrim[pilotRec->field3a];
                target1Scored = 1;
                if (target2Scored == 1) {
                    score -= awardSec[0];
                    score += awardSec[1];
                    ms_airKilled++;
                    break;
                }
                ms_airKilled++;
                break;
            } else if (flightRecords[i].status & 0x40) {
                score += awardSec[target1Scored];
                target2Scored = 1;
                ms_airKilled++;
                break;
            } else {
                if (planeObjects[unitId & 0x7f].validFlag == -1) {
                    score += award424c[pilotRec->field3a];
                    ms_friendlyAir++;
                    break;
                }
                if (unitId & 0x80) {
                    if (pilotRec->field3a == 0)
                        goto score_samchk;
                }
                if (pilotRec->field3a != 0) {
            score_samchk:
                    if (planeObjects[unitId & 0x7f].validFlag == 0)
                        score += award4246[pilotRec->field3a];
                    else if (samFlagTab[planeObjects[unitId & 0x7f].validFlag * 0x0e] & 4)
                        score += award423a[pilotRec->field3a];
                    else
                        score += award4240[pilotRec->field3a];
                    ms_airKilled++;
                    break;
                } else {
            score_uair:
                    score += award4252[pilotRec->field3a];
                    ms_unauthAir++;
                    break;
                }
            }
            break;
        case 2:                                             /* ground target */
            if (flightRecords[i].status & 0x80) {
                score += awardPrim[pilotRec->field3a];
                target1Scored = 1;
                ms_groundKilled++;
            } else if (flightRecords[i].status & 0x40) {
                score += awardSec[pilotRec->field3a];
                target2Scored = 1;
                ms_groundKilled++;
            } else {
                if (unitTypeTable[unitId & 0x7f] & 0x40) {
                    score += awardFriendlyGnd[pilotRec->field3a];
                    ms_friendlyGnd++;
                } else if (gridFlags[flightRecords[i].mapY >> 4]
                                       [flightRecords[i].mapX >> 4] & 3) {
                    score += awardFriendlyGnd[pilotRec->field3a];
                    ms_friendlyGnd++;
                } else if (unitTypeTable[unitId & 0x7f] & 0x80) {
                    score += awardCivilian[pilotRec->field3a];
                    ms_civilian++;
                } else {
                    score += awardUnit[pilotRec->field3a][unitTypeTable[unitId & 0x7f] & 0xf];
                    if (awardUnitChk[pilotRec->field3a] < 0)
                        goto unauth_fw;
                    ms_groundKilled++;
                    break;
                unauth_fw:
                    goto unauth_ground_inc;
                }
            }
            break;
        case 7:                                             /* visual id bonus */
            word_22C34++;
            score += awardVisId[pilotRec->field3a];
            break;
        case 6:                                             /* radar id bonus */
            word_2379C++;
            score += awardRadarId[pilotRec->field3a];
            break;
        }
    }

    score = multTheater[pilotRec->field38] * score / 8;
    score = multMission[pilotRec->field3c] * score / 8;
    score = multDiff[pilotRec->field3e] * score / 8;
    score = multUnk[pilotRec->field40] * score / 8;
    if (ejected == 1 && commData->landingType == 2)
        score = multResult[missionResult] * score / 8;
    if (commData->missionTime >= 0x1b58 && commData->missionTime < 0x2328)
        score = score * 9 / 8;
    if (commData->missionTime >= 0x2328 && commData->missionTime < 0x2cec)
        score = score * 10 / 8;
    if (commData->missionTime >= 0x2cec)
        score = score * 11 / 8;
    return score;
}

