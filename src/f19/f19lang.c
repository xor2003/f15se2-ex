/* F-19 English/Russian localization.
 *
 * The port keeps the original English literals in the code and translates at
 * the draw/measure choke points through gfx_setStringTranslator.  The Russian
 * text in f19_strings_ru.h is the authentic Boriss Software (1990) translation,
 * recovered from the KOI-7/Volapuk transliteration embedded in the Russian
 * release EXEs, re-encoded as UTF-8.  Cyrillic codepoints render through the
 * authentic Boriss glyph shapes (fontdata_ru.h) via the extra-glyph path in
 * drawStringCore.
 *
 * Matching is longest-prefix-first: composite strings the game builds with
 * strcpy/strcat (" Hit by MiG-29") are translated fragment by fragment, which
 * is exactly how the original's briefing sentences were assembled too.
 */
#include "f19.h"
#include "f19lang.h"
#include "f19_strings_ru.h"
#include <stdlib.h>
#include <string.h>

extern void gfx_setStringTranslator(const char *(*fn)(const char *));

static int g_f19Lang = -1;                   /* -1 unset, 0 en, 1 ru */
static uint8 *g_ruKeyLens;                   /* lazily computed key lengths */

int f19_language(void) {
    if (g_f19Lang < 0) {
        const char *e = getenv("F19_LANG");
        g_f19Lang = (e && (e[0] == 'r' || e[0] == 'R')) ? 1 : 0;
    }
    return g_f19Lang;
}

/* Already-localized text (contains UTF-8 Cyrillic) passes through unchanged —
 * guards nested translate calls (wrap -> per-line draw) and RU strings that
 * embed Latin words. */
static int asciiLower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
}

/* ASCII-case-insensitive compare of `len` bytes (keys are pure ASCII). */
static int asciiCaseMatch(const char *s, const char *key, int len) {
    int j;
    for (j = 0; j < len; j++) {
        if (s[j] == 0) return 0;
        if (asciiLower((uint8)s[j]) != asciiLower((uint8)key[j])) return 0;
    }
    return 1;
}

/* Copy RU text, folding UTF-8 Cyrillic to uppercase when `upper` (mirrors the
 * KOI-7 strupr behavior of the original HUD draw paths).  Length-stable. */
static int emitRu(char *out, int cap, const char *ru, int upper) {
    int oi = 0;
    const uint8 *p = (const uint8 *)ru;
    while (*p && oi < cap - 2) {
        if (upper && p[0] == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) {
            out[oi++] = (char)0xD0; out[oi++] = (char)(p[1] - 0x20); p += 2;
        } else if (upper && p[0] == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) {
            out[oi++] = (char)0xD0; out[oi++] = (char)(p[1] + 0x20); p += 2;
        } else if (upper && p[0] == 0xD1 && p[1] == 0x91) {
            out[oi++] = (char)0xD0; out[oi++] = (char)0x81; p += 2;
        } else {
            out[oi++] = (char)*p++;
        }
    }
    return oi;
}

static int containsCyrillic(const char *s) {
    const uint8 *p = (const uint8 *)s;
    for (; p[0] && p[1]; p++) {
        if ((p[0] == 0xD0 || p[0] == 0xD1) && (p[1] & 0xC0) == 0x80)
            return 1;
    }
    return 0;
}

const char *f19_translate(const char *s) {
    /* Ring of static buffers: measure-then-draw sequences can hold several
     * live translations at once (wrap keeps one for its whole scan). */
    static char ring[16][3072];
    static int ri;
    char *out;
    int i, oi;

    if (!s || !f19_language() || containsCyrillic(s))
        return s;
    if (!g_ruKeyLens) {
        int k;
        g_ruKeyLens = (uint8 *)malloc(f19_ruStringCount);
        for (k = 0; k < f19_ruStringCount; k++) {
            size_t l = strlen(f19_ruStrings[k].en);
            g_ruKeyLens[k] = (uint8)(l > 255 ? 255 : l);
        }
    }

    out = ring[ri];
    ri = (ri + 1) & 15;
    oi = 0;
    for (i = 0; s[i] != 0 && oi < (int)sizeof(ring[0]) - 2;) {
        const char *best = NULL;
        int bestLen = 0, k;
        for (k = 0; k < f19_ruStringCount; k++) {
            int len = g_ruKeyLens[k];
            if (len > bestLen && len <= 255 &&
                asciiCaseMatch(s + i, f19_ruStrings[k].en, len)) {
                best = f19_ruStrings[k].ru;
                bestLen = len;
            }
        }
        if (best) {
            /* Mirror the source case: an all-uppercase EN fragment (HUD text is
             * strupr'd before drawing) emits uppercase Cyrillic — exactly what
             * the Boriss KOI-7 bytes produced through the same strupr path. */
            int allUp = 1, j;
            for (j = 0; j < bestLen; j++) {
                uint8 b = (uint8)s[i + j];
                if (b >= 'a' && b <= 'z') { allUp = 0; break; }
            }
            oi += emitRu(out + oi, (int)sizeof(ring[0]) - 2 - oi, best, allUp);
            i += bestLen;
        } else {
            out[oi++] = s[i++];
        }
    }
    out[oi] = 0;
    return out;
}

void f19_langInit(void) {
    gfx_setStringTranslator(f19_translate);
}

/* Codepoint count for layout math that used strlen on pure-ASCII text
 * (HUD message centering, x offsets).  Counts every non-continuation byte. */
int f19_textChars(const char *s) {
    int n = 0;
    const uint8 *p = (const uint8 *)s;
    if (!s) return 0;
    for (; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}
