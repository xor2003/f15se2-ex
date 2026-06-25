/*
 * egregsh.c — native/64-bit replacements for the egame-side cdecl spellings of
 * a few MGRAPHIC slots: gfx_dirtyRect (0x25), gfx_drawGlyphStr (the 0x01-0x06
 * clipped glyph engine) and gfx_setCurPageSegReg (0x0f).
 *
 * The DOS build reached these through egregsh.asm's cdecl->register shims and
 * the gfxFarTableExported trampoline. Both are gone — gfx_impl.c now provides
 * the real slot functions directly (the "Get things to compile" pass renamed
 * the *_impl bodies to their real slot names) — so these are thin forwarders to
 * the real names. gfx_drawLine is no longer defined here: gfx_impl.c owns it.
 */

#include "inttype.h"
#include "pointers.h"
#include "gfx_impl.h"
#include "slot.h"

/* Slot 0x25: flush the per-row dirty spans. eg3drast.c hands us the real span
 * buffer pointer; gfx_dirtyRect2 (gfx_impl.c) walks rows [yMin..yMax]. */
void FAR CDECL gfx_dirtyRect(int16 *spanBuf, int yMin, int yMax) { gfx_dirtyRect2(spanBuf, (uint16)yMin, (uint16)yMax); }

/* Slots 0x01-0x06: the clipped glyph engine. The egame HUD selects the clip
 * mode by slot index; map each to the real C glyph function. */
void FAR CDECL gfx_drawGlyphStr(int16 *desc, const char *str, int slot) {
    switch (slot) {
    case 0x01:
        gfx_fillDirty(desc, str);
        break;
    case 0x02:
        gfx_blitTransparent(desc, str);
        break;
    case 0x03:
        gfx_blitVariant(desc, str);
        break;
    case 0x04:
        gfx_copyBlock(desc, str);
        break;
    case 0x06:
    default:
        gfx_drawStringUnclipped(desc, str);
        break;
    }
}

/* Slot 0x0f: restore curPageSeg by value (was a cdecl->register shim). */
void FAR CDECL gfx_setCurPageSegReg(uint16 seg) { gfx_setCurPageSeg(seg); }
