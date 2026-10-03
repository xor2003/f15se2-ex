/* START.EXE — view-origin setter at seg000:0x61a4, contiguous tail of the
 * f19_sub_15B68 extent (5b68-61cb). Stores the new origin then reloads the
 * object file through f19_sub_15460. This module compiles /Os — the original
 * emits the shared-epilogue tail (jmp over sub ax,ax) that /Ot inlines. */
#include "f19.h"

#define word_2CA4E (*(int16 *)(f19_dseg + 0xCA4E))
#define word_2CA50 (*(int16 *)(f19_dseg + 0xCA50))

extern int16 f19_sub_15460(const char *name, int16 pad);   /* seg000:0x5460 */

/* seg000:0x61a4 — the second arg word is pushed but unread by the callee. */
int16 f19_sub_161A4(int16 y, int16 x, char *name, int16 pad)
{
    word_2CA4E = x;
    word_2CA50 = y;
    if (f19_sub_15460(name, pad) != 0)
        return 1;
    return 0;
}
