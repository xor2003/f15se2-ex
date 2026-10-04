#include "f19eg.h"

int16 sinMul(int16, int16);
int16 cosMul(int16, int16);
/* seg000 routines — UI/pic helpers (ported, verified vs original) */

/* TODO: real decls come from generated EGAME.EXE.h; these stand-ins keep the
 * module self-contained for portcheck. */

void setDrawColor(int16 color);
void drawLine(int16 x1, int16 y1, int16 x2, int16 y2);
/* ==== seg000:0x0504 ==== */
void loadColorPalette(int16 idx) {
    memcpy(colorLut, g_colorPalettes + idx * 16, 16);
}

/* ==== seg000:0x978c ==== */
void drawMapMarkerBox(int16 unused1, int16 unused2, int16 color) {
    setDrawColor(color);
    drawLine(g_mapX - 4, g_mapY - 3, g_mapX + 4, g_mapY - 3);
    drawLine(g_mapX + 4, g_mapY - 3, g_mapX + 4, g_mapY + 3);
    drawLine(g_mapX + 4, g_mapY + 3, g_mapX - 4, g_mapY + 3);
    drawLine(g_mapX - 4, g_mapY + 3, g_mapX - 4, g_mapY - 3);
}

/* seg000:0xe40a openBlitClosePic — stays asm (0xE1xx pic/file cluster) */

/* ==== seg000:0x9810 ==== */
void projectMapPoint(int16 mapX, int16 mapY) {
    int16 scaledX, scaledY;
    char shift;
    g_projDepth = 0;
    shift = 7 - (char)g_radarScopeRange;
    scaledX = (mapX - g_viewX_) >> shift;
    scaledY = (g_viewY_ - mapY) >> shift;
    g_vprojX = cosMul(g_ourHead, scaledX) - sinMul(g_ourHead, scaledY);
    g_vprojY = cosMul(g_ourHead, scaledY) + sinMul(g_ourHead, scaledX);
    g_vprojX += 0x5C;
    g_vprojY = -g_vprojY + 0xAC;
    if (g_vprojX < 0x2C || g_vprojX > 0x8C)
        g_projDepth = -1;
    if (g_vprojY < 0x7F || g_vprojY > 0xC0)
        g_projDepth = -1;
}

/* sprite/param block for blitGaugeSprite — dseg 0x2828 */
struct GaugeParams {
    int16 bufPtr, srcX, srcY, page, dstX, dstY, width, height;
};

/* ==== seg000:0x98bc ==== */
void blitGaugeSprite(int16 srcCol, int16 srcRow, int16 destX, int16 destY) {
    gaugeSpriteParams.bufPtr = gfxBufPtr;
    gaugeSpriteParams.srcX = srcCol * 8 + 1;
    gaugeSpriteParams.srcY = srcRow * 8 + 0x29;
    gaugeSpriteParams.page = (g_drawPage != 0);
    gaugeSpriteParams.dstX = destX - 3;
    gaugeSpriteParams.dstY = destY - 3;
    gaugeSpriteParams.width = 7;
    gaugeSpriteParams.height = 7;
    f19_blitSpriteParams(&gaugeSpriteParams, 0);
}

/* sprite-blit descriptor for blitSprite — dseg 0x5856 */
struct SpriteParams {
    int16 bufPtr, srcX, srcY, page, dstX, dstY, width, height;  /* +0x00..+0x0E */
    int16 pad16[4];         /* +0x10..+0x17 */
    uint8 flags;            /* +0x18 */
    uint8 transparent;      /* +0x19 */
};
void far gfx_blitSpriteOpaque(int16 *params);   /* sub_2F197 */

/* ==== seg000:0x9912 ==== */
void blitSprite(int16 destX, int16 destY, int16 srcX, int16 srcY, int16 width, int16 height, int16 transparent) {
    blitSpriteParams.bufPtr = gfxBufPtr;
    blitSpriteParams.srcX = srcX;
    blitSpriteParams.srcY = srcY;
    blitSpriteParams.page = (g_drawPage != 0);
    blitSpriteParams.dstX = destX;
    blitSpriteParams.dstY = destY;
    blitSpriteParams.width = width;
    blitSpriteParams.height = height;
    blitSpriteParams.transparent = transparent;
    if (transparent != 0) {
        blitSpriteParams.flags = 1;
        f19_blitSpriteParams(&blitSpriteParams, 0);
    } else {
        blitSpriteParams.flags = 0x10;
        f19_blitSpriteParams(&blitSpriteParams, 1);
    }
}

int16 clampRange(int16 v, int16 lo, int16 hi);   /* sub_1D1FA */
void fillRectBoth(int16 x1, int16 y1, int16 x2, int16 y2);  /* sub_18FB2 */
void formatTwoDigit(int16 val);  /* sub_19E0F */

/* ==== seg000:0x9d5e ==== */
void drawStatusBar(int16 val, int16 color) {
    val = clampRange(val, 0, 0x2710);
    if (val > 0x6F) {
        setDrawColor(color);
        fillRectBoth(0xB4, 0xBB, 0xB4 + val / 0x70, 0xC1);
    }
}

/* ==== seg000:0x9da3 ==== */
void formatMissionClock(uint16 time) {
    time += g_missionTick;
    strcpy(g_nameBuf, ":");
    formatTwoDigit(time / 0x708);
    g_nameBuf[0] += *(char *)&g_nightMode + 1;
    strcat(g_nameBuf, ":");
    formatTwoDigit(time / 0x1E);
    strcat(g_nameBuf, ":");
    formatTwoDigit(time << 1);
}

/* ==== seg000:0x9e0f ==== */
void formatTwoDigit(int16 val) {
    val %= 60;
    if (val < 10)
        strcat(g_nameBuf, "0");
    strcat(g_nameBuf, itoa(val, g_itoaScratch, 10));
}

/* ==== seg000:0xab2d ==== */
void drawPanelGridText(int16 panel, int16 col, int16 row, const char *text, int16 color); /* sub_19071 */
struct Weapon {                       /* 14-byte stride @0x4894 (aNone/SAM spec table) */
    int8  name[8];          /* +0x00 */
    int16 lethality;        /* +0x08 max range (km) */
    int16 dangerTier;       /* +0x0A effective-range multiplier */
    int16 flags;            /* +0x0C bit0 = radar-guided (doppler) */
};
/* EN: no 'off' variable at all — MSC CSEs weaponIdx*14 into si; the
 * weaponIdx=weaponIdx self-assign evicts the cache so the second use
 * re-imuls (matches EN's repeated 'mov ax,0xe; imul [bp+4]; mov si,ax'). */
void drawWeaponRadarInfo(int16 weaponIdx, int16 row) {
    if (weaponIdx > 0) {
        strcpy(g_nameBuf, (char*)g_samSpecs + weaponIdx * 14);
        strcat(g_nameBuf, (*(int16*)((char*)g_samSpecs + weaponIdx * 14 + 12) & 1) ? " doppler" : " pulse");
        strcat(g_nameBuf, " radar");
        drawPanelGridText(2, 2, row++, g_nameBuf, 0xF);
        strcpy(g_nameBuf, "Max. range ");
        weaponIdx = weaponIdx;
        strcat(g_nameBuf, itoa(*(int16*)((char*)g_samSpecs + weaponIdx * 14 + 8), g_itoaScratch, 10));
        strcat(g_nameBuf, " km");
        drawPanelGridText(2, 2, row++, g_nameBuf, 7);
        strcpy(g_nameBuf, "Eff. range ");
        strcat(g_nameBuf, itoa(*(int16*)((char*)g_samSpecs + weaponIdx * 14 + 8) *
                               *(int16*)((char*)g_samSpecs + weaponIdx * 14 + 10) / 10,
                               g_itoaScratch, 10));
        strcat(g_nameBuf, " km");
        drawPanelGridText(2, 2, row++, g_nameBuf, 7);
    }
}

void far gfx_setColor(uint8 color); /* far: seg002 callee */
void far drawClipLineGlobal(void);

/* ==== seg000:0x1802 ==== */
void drawModelPoint(int16 x, int16 y) {
    g_lineX2 = g_lineX1 = x + g_viewCenterX;
    g_lineY2 = g_lineY1 = -y + g_viewCenterY;
    ++g_modelStreamPtr;
    gfx_setColor((uint8)*g_modelStreamPtr++);
    drawClipLineGlobal();
}

/* viewport parms struct — word_34646 points at it */
struct VpParms { int16 f[11]; };

int far gfx_calcRowAddr(int, int);  /* sub_2F160 */
void far gfx_setBlitOffset(int);          /* sub_2F0AC */
void far gfx_nop23(void);                     /* sub_2F0D9 */

/* ==== seg000:0x8d9a ==== */
void drawViewportLine(int16 x1, int16 y1, int16 x2, int16 y2) {
    int16 height, width;

    width = g_vpParms->f[10] - g_vpParms->f[9] + 1;
    height = g_vpParms->f[8] - g_vpParms->f[7] + 1;
    gfx_setBlitOffset(gfx_calcRowAddr(g_vpParms->f[9], g_vpParms->f[7]));
    g_clipMaxX = width - 1;
    g_clipMaxY = height - 1;
    gfx_setColor(g_vpParms->f[2]);
    g_lineX1 = x1;
    g_lineY1 = y1;
    g_lineX2 = x2;
    g_lineY2 = y2;
    drawClipLineGlobal();
    gfx_nop23();
}

uint8 far gfx_getDrawPage(void);            /* sub_2F10B */
void  far gfx_setDrawPage(int16 page);      /* sub_2F070 */

/* ==== seg000:0x8e12 ==== */
void drawClippedLineRegion(int16 sx1, int16 sy1, int16 sx2, int16 sy2,
                           int16 clipL, int16 clipR, int16 clipT, int16 clipB, int16 both) {
    int16 clipH, clipW;

    clipW = clipR - clipL + 1;
    clipH = clipB - clipT + 1;
    gfx_setBlitOffset(gfx_calcRowAddr(clipL, clipT));
    g_clipMaxX = clipW - 1;
    g_clipMaxY = clipH - 1;
    gfx_setColor(g_vpParms->f[2]);
    g_lineX1 = sx1 - clipL;
    g_lineY1 = sy1 - clipT;
    g_lineX2 = sx2 - clipL;
    g_lineY2 = sy2 - clipT;
    drawClipLineGlobal();
    gfx_nop23();
    if (both != 0) {
        g_drawPage = gfx_getDrawPage();
        gfx_setDrawPage(g_drawPage == 0);
        gfx_setColor(g_vpParms->f[2]);
        g_lineX1 = sx1 - clipL;
        g_lineY1 = sy1 - clipT;
        g_lineX2 = sx2 - clipL;
        g_lineY2 = sy2 - clipT;
        drawClipLineGlobal();
        gfx_setDrawPage(g_drawPage != 0);
        gfx_nop23();
    }
    g_clipMaxX = 0x13F;
    g_clipMaxY = 0xC7;
    gfx_setBlitOffset(0);
}
