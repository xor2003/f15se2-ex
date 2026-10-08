/* START.EXE — theater/object data loader (seg000:0x5460).
 * Builds the EGA row-offset table word_2C7D8 (y/4*160 + plane<<13), opens the
 * object data file, unpacks big-endian records into the 0x5c-stride table at
 * 0x2326 + 0x49-stride name table at 0x2dee, delta-decodes packed 11-bit
 * pairs into word_21718, then blits per-record icons/markers and sets up the
 * screen buffers for the active gfx mode (0xB800/0xA800/0xA400/0xA000).
 * This module compiles /Ot — the original emits branch-target alignment nops. */
#include "f19.h"
#include "f19stvars.h"

#define word_22322 (*(uint16 *)((uint8 *)f19_stSpace.m_word_21718 + 3082))

extern int16 resFileOpen(const char *path, int16 mode);     /* seg000:0x47b6 */
extern int16 resFileClose(int16 handle);                    /* seg000:0x47da */
extern void  sub_149A1(int16 h);                            /* buf refill */
extern int16 sub_15AD2(int32 a);                            /* alloc-wrap */
extern int16 f19_bufReadFile(uint8 *buf, int16 n, int16 fd);    /* seg000:0x5414 */
extern int16 sub_153F2(int16 a, int16 fd);                  /* pos/seek helper */
extern int16 sub_15B22(int16 pos, int16 n);                 /* seg000:0x5b22 */
extern int16 sub_15B02(int16 n);                            /* seg000:0x5b02 */
extern void  sub_16C0E(void);                               /* stream refill */
extern void  sub_169BE(int16 a, int16 b, int16 c, int16 d); /* xor blit */
extern void  sub_16AE7(int16 a, int16 b, int16 c, int16 d); /* marker blit */
extern void  sub_168C8(int16 a, int16 b, int16 c, int16 d,
                       int16 e, int16 f, int16 g, int16 h); /* icon blit */
extern void  sub_16D93(int16 a, int16 b, int16 c, int16 d,
                       int16 e, int16 f);
extern void  sub_16D90(int16 a, int16 b, int16 c, int16 d,
                       int16 e, int16 f);
extern void  sub_16DC2(int16 a, int16 b, int16 c, int16 d,
                       int16 e, int16 f);
extern void  sub_16E0C(int16 a, int16 b, int16 c, int16 d,
                       int16 e, int16 f);
extern void  far ovlCall_ba9(void);                         /* overlay 1000:0ba9 */


/* seg000:0x5460 */
int16 f19_sub_15460(const char *name, int16 unused)
{
    uint8  *at;
    int16   db;
    int16   ct;
    int16   i;
    int16   j;
    uint8  *nm;

    word_21716 = 0;
    byte_22318 = 0;
    byte_22319 = 0;
    byte_2231A = 0;
    byte_2231B = 0;
    byte_2231C = 0;
    byte_2231D = 0;
    byte_2231E = 0;
    for (i = 0; i < 200; i++)
        word_2C7D8[i] = (i / 4) * 160 + ((i & 3) << 13);

    word_2D05C = resFileOpen(name, 0);
    sub_149A1(word_2D05C);
    word_21714 = 0;
    word_2A0C4 = sub_15AD2(0xFFFFL);
    word_2D2F4 = word_2A0C4 + 0x800;
    if (word_2B83E == 2)
        ovlCall_ba9();
    word_216D6 = word_2A0C4;
    word_216CA = word_2D2F4;
    word_21712 = *(uint16 *)(((uint8 *)f19_dsegAt(word_21708)));
    word_22320 = 0x1B18;
    word_22322 = 0x1F18;
    f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), 0x20, word_2D05C);

    /* expand 16 packed palette words (2 nibble-colors per byte) */
    for (i = 0; i < 16; i++) {
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2] & 0xF0;
        byte_27D37[i] = (j >> 4) | j;
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2] & 0x0F;
        byte_27D47[i] = (j << 4) | j;
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2 + 1] & 0xF0;
        byte_27D57[i] = (j >> 4) | j;
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2 + 1] & 0x0F;
        byte_27D67[i] = (j << 4) | j;
    }

    /* 6-byte header: record count + flag byte + two BE words + 7-byte skip */
    f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), 6, word_2D05C);
    word_2367C = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0];
    byte_2231A = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[1];
    word_2367E = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[3];
    word_23680 = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[4] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[5];
    f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), 7, word_2D05C);

    /* object records: 11-byte attr + per-index 7-byte chunks */
    for (i = 0; i < word_2367C; i++) {
        at = (uint8 *)(((uint8 *)f19_stSpace.m_clipTable)) + i * 0x5C;
        nm = (uint8 *)(((uint8 *)(f19_stSpace.m_g_flagTable2CFE + 0xF0))) + i * 0x49;
        f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), 0x0B, word_2D05C);
        *(int16 *)at       = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[1];
        ((int16 *)at)[1]   = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[3];
        ((int16 *)at)[2]   = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[4] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[5];
        ((int16 *)at)[3]   = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[6] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[7];
        at[8]    = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[8];
        at[9]    = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[9];
        at[0x0A] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0x0A];
        *nm = at[9];
        ((int16 *)at)[0x2C] = sub_153F2(at[0x0A], word_2D05C);
        at[9] = 0;
        at[0x0A] = 0;
        at[0x0B] = 0;
        at[0x0C] = 0;
        at[0x0D] = 1;
        at[0x17] = 0;
        at[0x5A] = 1;
        for (j = 0; j < *nm; j++) {
            f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), 7, word_2D05C);
            ((int16 *)at)[0x11 + j] = *(int16 *)word_22320;
            at[0x46 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2];
            nm[1 + j]    = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[3];
            nm[0x13 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[4];
            nm[0x25 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[5];
            nm[0x37 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[6];
        }
    }

    /* delta tables: count bytes, then count*2 low bytes, then sign/Hi bits */
    f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), 3, word_2D05C);
    ct = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[1];
    byte_2231C = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2];
    f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), ct * 2, word_2D05C);
    for (i = 0; i < ct * 2; i++)
        word_21718[i] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i];
    f19_bufReadFile((uint8 *)(((uint8 *)f19_dsegAt(word_22320))), ct, word_2D05C);
    for (i = 0; i < ct; i++) {
        j = i * 2;
        word_21718[j] =
            (((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 0x70) << 4) | word_21718[j]) *
            ((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 0x80) ? 0xFFFF : 1);
        word_21718[j + 1] =
            (((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 7) << 8) | word_21718[j + 1]) *
            ((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 8) ? 0xFFFF : 1);
    }

    i = byte_2231A;
    byte_2231A = 0;
    sub_169BE(word_2CA4E, word_2CA50, word_2367E, word_23680);
    byte_2231A = (uint8)i;
    sub_168C8(word_21708, word_2CA4E, word_2CA50, word_21706,
              word_2CA4E, word_2CA50, word_2367E, word_23680);

    /* per-record base icon blit */
    for (i = 0; i < word_2367C; i++) {
        at = (uint8 *)(((uint8 *)f19_stSpace.m_clipTable)) + i * 0x5C;
        sub_168C8(word_21706, *(int16 *)at + word_2CA4E,
                  ((int16 *)at)[1] + word_2CA50, word_21708,
                  ((int16 *)at)[0x11], at[0x46],
                  ((int16 *)at)[2], ((int16 *)at)[3]);
    }

    /* per-record per-index marker blits */
    for (i = 0; i < word_2367C; i++) {
        at = (uint8 *)(((uint8 *)f19_stSpace.m_clipTable)) + i * 0x5C;
        nm = (uint8 *)(((uint8 *)(f19_stSpace.m_g_flagTable2CFE + 0xF0))) + i * 0x49;
        for (j = 1; j < *nm; j++) {
            sub_168C8(word_21708, ((int16 *)at)[0x10 + j],
                      at[0x45 + j], word_21708,
                      ((int16 *)at)[0x11 + j], at[0x46 + j],
                      ((int16 *)at)[2], ((int16 *)at)[3]);
            sub_169BE(((int16 *)at)[0x11 + j] + nm[1 + j],
                      at[0x46 + j] + nm[0x13 + j],
                      nm[0x25 + j], nm[0x37 + j]);
        }
    }

    resFileClose(word_2D05C);
    switch (word_2B83E) {
    case 0:
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = *(int16 *)(((uint8 *)f19_dsegAt(word_21706)));
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xB800;
        sub_16D93(0x28, 0x19, *(uint16 *)(((uint8 *)f19_dsegAt(word_21708))), 0,
                  *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))), 0);
        break;
    case 1:
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xB800;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = *(uint16 *)(((uint8 *)f19_dsegAt(word_21708)));
        break;
    case 2:
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xA800;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = 0xA400;
        sub_16D90(0x28, 0x19, *(uint16 *)(((uint8 *)f19_dsegAt(word_21708))), 0,
                  *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))), 0);
        *(int16 *)(((uint8 *)f19_dsegAt(word_21706))) = 0xA800;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xA000;
        break;
    case 3:
        sub_16DC2(0x28, 0x19, *(uint16 *)(((uint8 *)f19_dsegAt(word_21708))), 0,
                  *(int16 *)(((uint8 *)f19_dsegAt(word_21706))), 0);
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xA000;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = *(uint16 *)(((uint8 *)f19_dsegAt(word_21708)));
        sub_16E0C(0x28, 0x19, *(int16 *)(((uint8 *)f19_dsegAt(word_21706))), 0,
                  *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))), 0);
        break;
    }
    return 1;
}

/* seg000:0x5b68 — stream-side twin of f19_sub_15460: same row table, palette
 * expansion, record unpack, delta decode and blit loops, but pulls bytes
 * through the buffered stream (seg_2D274) via sub_15B22/sub_16C0E and hands
 * per-record chunks off through sub_15B02. The marker blitter is sub_16AE7. */
int16 f19_sub_15B68(int16 seg)
{
    uint8  *at;
    int16   db;
    int16   ct;
    int16   i;
    int16   j;
    uint8  *nm;

    word_21716 = 0;
    byte_22318 = 0;
    byte_22319 = 0;
    byte_2231A = 0;
    byte_2231B = 0;
    byte_2231C = 0;
    byte_2231D = 0;
    byte_2231E = 0;
    for (i = 0; i < 200; i++)
        word_2C7D8[i] = (i / 4) * 160 + ((i & 3) << 13);

    word_2D05A = 0;
    seg_2D274 = seg;
    sub_16C0E();
    word_21714 = 0;
    word_2A0C4 = sub_15AD2(0xFFFFL);
    word_2D2F4 = word_2A0C4 + 0x800;
    if (word_2B83E == 2)
        ovlCall_ba9();
    word_216D6 = word_2A0C4;
    word_216CA = word_2D2F4;
    word_21712 = *(uint16 *)(((uint8 *)f19_dsegAt(word_21708)));
    word_22320 = 0x1B18;
    word_22322 = 0x1F18;
    sub_15B22(0x1B18, 0x20);

    /* expand 16 packed palette words (2 nibble-colors per byte) */
    for (i = 0; i < 16; i++) {
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2] & 0xF0;
        byte_27D37[i] = (j >> 4) | j;
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2] & 0x0F;
        byte_27D47[i] = (j << 4) | j;
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2 + 1] & 0xF0;
        byte_27D57[i] = (j >> 4) | j;
        j = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i * 2 + 1] & 0x0F;
        byte_27D67[i] = (j << 4) | j;
    }

    /* 6-byte header: record count + flag byte + two BE words + 7-byte skip */
    sub_15B22((int16)word_22320, 6);
    word_2367C = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0];
    byte_2231A = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[1];
    word_2367E = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[3];
    word_23680 = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[4] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[5];
    sub_15B22((int16)word_22320, 7);

    /* object records: 11-byte attr + per-index 7-byte chunks */
    for (i = 0; i < word_2367C; i++) {
        at = (uint8 *)(((uint8 *)f19_stSpace.m_clipTable)) + i * 0x5C;
        nm = (uint8 *)(((uint8 *)(f19_stSpace.m_g_flagTable2CFE + 0xF0))) + i * 0x49;
        sub_15B22((int16)word_22320, 0x0B);
        *(int16 *)at       = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[1];
        ((int16 *)at)[1]   = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[3];
        ((int16 *)at)[2]   = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[4] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[5];
        ((int16 *)at)[3]   = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[6] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[7];
        at[8]    = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[8];
        at[9]    = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[9];
        at[0x0A] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0x0A];
        *nm = at[9];
        ((int16 *)at)[0x2C] = sub_15B02(at[0x0A]);
        at[9] = 0;
        at[0x0A] = 0;
        at[0x0B] = 0;
        at[0x0C] = 0;
        at[0x0D] = 1;
        at[0x17] = 0;
        at[0x5A] = 1;
        for (j = 0; j < *nm; j++) {
            sub_15B22((int16)word_22320, 7);
            ((int16 *)at)[0x11 + j] = *(int16 *)word_22320;
            at[0x46 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2];
            nm[1 + j]    = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[3];
            nm[0x13 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[4];
            nm[0x25 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[5];
            nm[0x37 + j] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[6];
        }
    }

    /* delta tables: count bytes, then count*2 low bytes, then sign/Hi bits */
    sub_15B22((int16)word_22320, 3);
    ct = (((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[0] << 8) + ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[1];
    byte_2231C = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[2];
    sub_15B22((int16)word_22320, ct * 2);
    for (i = 0; i < ct * 2; i++)
        word_21718[i] = ((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i];
    sub_15B22((int16)word_22320, ct);
    for (i = 0; i < ct; i++) {
        j = i * 2;
        word_21718[j] =
            (((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 0x70) << 4) | word_21718[j]) *
            ((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 0x80) ? 0xFFFF : 1);
        word_21718[j + 1] =
            (((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 7) << 8) | word_21718[j + 1]) *
            ((((uint8 *)(((uint8 *)f19_dsegAt(word_22320))))[i] & 8) ? 0xFFFF : 1);
    }

    i = byte_2231A;
    byte_2231A = 0;
    sub_16AE7(word_2CA4E, word_2CA50, word_2367E, word_23680);
    byte_2231A = (uint8)i;
    sub_168C8(word_21708, word_2CA4E, word_2CA50, word_21706,
              word_2CA4E, word_2CA50, word_2367E, word_23680);

    /* per-record base icon blit */
    for (i = 0; i < word_2367C; i++) {
        at = (uint8 *)(((uint8 *)f19_stSpace.m_clipTable)) + i * 0x5C;
        sub_168C8(word_21706, *(int16 *)at + word_2CA4E,
                  ((int16 *)at)[1] + word_2CA50, word_21708,
                  ((int16 *)at)[0x11], at[0x46],
                  ((int16 *)at)[2], ((int16 *)at)[3]);
    }

    /* per-record per-index marker blits */
    for (i = 0; i < word_2367C; i++) {
        at = (uint8 *)(((uint8 *)f19_stSpace.m_clipTable)) + i * 0x5C;
        nm = (uint8 *)(((uint8 *)(f19_stSpace.m_g_flagTable2CFE + 0xF0))) + i * 0x49;
        for (j = 1; j < *nm; j++) {
            sub_168C8(word_21708, ((int16 *)at)[0x10 + j],
                      at[0x45 + j], word_21708,
                      ((int16 *)at)[0x11 + j], at[0x46 + j],
                      ((int16 *)at)[2], ((int16 *)at)[3]);
            sub_16AE7(((int16 *)at)[0x11 + j] + nm[1 + j],
                      at[0x46 + j] + nm[0x13 + j],
                      nm[0x25 + j], nm[0x37 + j]);
        }
    }

    switch (word_2B83E) {
    case 0:
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = *(int16 *)(((uint8 *)f19_dsegAt(word_21706)));
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xB800;
        sub_16D93(0x28, 0x19, *(uint16 *)(((uint8 *)f19_dsegAt(word_21708))), 0,
                  *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))), 0);
        break;
    case 1:
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xB800;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = *(uint16 *)(((uint8 *)f19_dsegAt(word_21708)));
        break;
    case 2:
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xA800;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = 0xA400;
        sub_16D90(0x28, 0x19, *(uint16 *)(((uint8 *)f19_dsegAt(word_21708))), 0,
                  *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))), 0);
        *(int16 *)(((uint8 *)f19_dsegAt(word_21706))) = 0xA800;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xA000;
        break;
    case 3:
        sub_16DC2(0x28, 0x19, *(uint16 *)(((uint8 *)f19_dsegAt(word_21708))), 0,
                  *(int16 *)(((uint8 *)f19_dsegAt(word_21706))), 0);
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170A))) = 0xA000;
        *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))) = *(uint16 *)(((uint8 *)f19_dsegAt(word_21708)));
        sub_16E0C(0x28, 0x19, *(int16 *)(((uint8 *)f19_dsegAt(word_21706))), 0,
                  *(uint16 *)(((uint8 *)f19_dsegAt(word_2170C))), 0);
        break;
    }
    return 1;
}
