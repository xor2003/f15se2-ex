/* START.EXE — roster save (seg000:0xa376). EN variant: when doFcbSearch finds
 * the disk write-protected, draws the warning and waits for a key/joy release
 * instead of writing the file. */
#include "f19.h"
#include "f19stvars.h"
#include <stdio.h>

extern struct GD0 *gameData;
extern struct CommJoy *commData;

#define hallfameBuf ((HallfameEntry *)f19_stSpace.m_word_29D54)
extern int16 *uiPage;
extern int16 *page1Num;
#define blinkColors ((int16 *)f19_stSpace.m_blinkColors)

struct GD0 { int16 pilotIdx; };
typedef struct { char data[0x50]; } HallfameEntry;
struct CommJoy { int8 pad[0x72]; int16 joyPresent; };

extern int16 doFcbSearch(void);             /* seg000:0x40de asm */
extern void sub_14089(int16 n);             /* seg000:0x4089 waitTicks */
extern void mystrcpy(char *d, const char *s);
extern int16 f19_stringWidth(int16 *page, const uint8 *s);
extern void drawStringAt(int16 *page, const char *s, int16 x, int16 y);
extern int16 far misc_jump_5a_keybuf(void);
extern int16 far misc_jump_5b_getkey(void);
extern int16 far misc_jump_5d_readJoy(int16 n);
extern int16 far gfx_blitToCurrent(int16 pagePtr);

void f19_saveHallfame(void) {
    FILE *file;
    int16 idx;
    int16 w;
    hallfameCount = gameData->pilotIdx;
    if (doFcbSearch() != 0) {
        file = fopen("Roster.Fil", "wb");
        fwrite(&hallfameCount, 2, 1, file);
        for (idx = 0; idx < 0xa; idx++) {
            fwrite(&hallfameBuf[idx], 0x50, 1, file);
        }
        fclose(file);
    }
    else {
        uiPage[6] = 4;
        uiPage[2] = 0;
        uiPage[3] = 0xf;
        gfx_blitToCurrent(savedPage);
        mystrcpy(scrStrBuf, "Original disk in drive.  Roster file will not be saved.");
        w = f19_stringWidth(uiPage, (uint8*)scrStrBuf);
        drawStringAt(uiPage, scrStrBuf, (0x140 - w) / 2, 0x64);
        uiPage[2] = 9;
        mystrcpy(scrStrBuf, "Press Selector to continue.");
        w = f19_stringWidth(uiPage, (uint8*)scrStrBuf);
        drawStringAt(uiPage, scrStrBuf, (0x140 - w) / 2, 0x96);
        if (commData->joyPresent == 1) {
            while (misc_jump_5a_keybuf() != 0) {
                if (misc_jump_5d_readJoy(0) != 0) break;
            }
            if (misc_jump_5a_keybuf() == 0) misc_jump_5b_getkey();
        }
        else {
            misc_jump_5b_getkey();
        }
        if (commData->joyPresent == 1) {
            while (misc_jump_5d_readJoy(0) != 0) ;
            sub_14089(5);
            while (misc_jump_5d_readJoy(0) != 0) ;
        }
        gfx_blitToCurrent(savedPage);
    }
}

/* seg000:0xa4fb — load Roster.Fil hall-of-fame records (mirrors f19_saveHallfame) */
void f19_loadHallfame() {
    FILE *file;
    int16 idx;
    file = fopen("Roster.Fil", "rb");
    fread(&hallfameCount, 2, 1, file);
    for (idx = 0; idx < 0xa; idx++) {
        fread(&hallfameBuf[idx], 0x50, 1, file);
    }
    fclose(file);
}

/* seg000:0xa555 — pilot-name entry on the roster screen. Text input into a
 * local buffer with a blinking cursor; returns a near ptr to the buf (the
 * caller copies it before the frame is reused). f15 f19_pilotNameInput lineage. */
extern void far misc_jump_5e_clearKeyFlags(void);
extern void sub_14622(void *o, int16 x0, int16 y0, int16 x1, int16 y1);
extern int16 sub_13E38(int16 *page, char *s); /* seg000:0x3e38 f19_stringWidth */

char *f19_pilotNameInput(int16 *page, int16 x, int16 y, int16 maxLen,
                     int16 unused, int16 h) {
    uint8 keyCode;
    int16 blinkIdx;
    char  buf[0x50];
    int16 cursor;
    int16 len;

    misc_jump_5e_clearKeyFlags();
    keyCode = 0x18;
    while (keyCode != 0x1b) {
        switch (keyCode) {
        case 0x18:
        case 0x1b:
            len = 0;
            buf[0] = 0;
            sub_14622(page, x, y, x + 0xa8, y + h);
            drawStringAt(page, buf, x, y);
            cursor = page[4];
            break;
        case 8:
            if (len > 0) {
                len--;
                buf[len] = 0;
                sub_14622(page, x, y, x + 0xa8, y + h);
                drawStringAt(page, buf, x, y);
                cursor = page[4];
            }
            break;
        default:
            if (keyCode >= 0x20 && keyCode <= 0x7f && len < maxLen &&
                sub_13E38(page1Num, buf) <= 0x67) {
                buf[len++] = keyCode;
                buf[len] = 0;
                sub_14622(page, x, y, x + 0xa8, y + h);
                drawStringAt(page, buf, x, y);
                cursor = page[4];
            }
            break;
        }
        blinkIdx = 0;
        while (misc_jump_5a_keybuf() != 0) {
            if (blinkTimer > 6) {
                blinkTimer = 0;
                page[2] = blinkColors[blinkIdx];
                drawStringAt(page, "_", cursor, y);
                blinkIdx = ++blinkIdx % 6;
            }
        }
        page[2] = blinkColors[0];
        drawStringAt(page, " ", cursor, y);
        keyCode = misc_jump_5b_getkey();
        if (keyCode == 0xd) break;
    }
    return buf;
}
