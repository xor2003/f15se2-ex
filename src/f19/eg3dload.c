#include "f19eg.h"
#include "f19egvars.h"

/* eg3dload.c — 3D3 model file loader (F19) */

void strcpyFromDot(char *dst, const char *src); /* sub_11068 */
void drawStringBothPages(const char *text, int16 x, int16 y, int16 color); /* sub_191B4 */
void load3DG(void);
void load3D3(char *fileName);
void load3DT(char *fileName);
int  getch(void);

extern FILE *fileHandle;            /* word_354C8 */
struct TileSceneObject;
struct TargetSlot { int16 flags; uint8 _pad[0x10]; }; /* 0x12 bytes */

/* ==== seg000:0x0abe ==== */
void load3DAll(void) {
    load3DG();
    load3D3(regnFile);
    load3DT(regnFile);
    g_unusedLoadDoneFlag = 0;
}

#pragma pack(1)
struct TileSceneObject {
    int16 x, y, z;
    uint8 shape;
};
#pragma pack()

/* ==== seg000:0x104c ==== */
void printError(char *msg) {
    drawStringBothPages(msg, 0, 0x60, 0xF);
    getch();
}

/* ==== seg000:0x1068 ==== */
void strcpyFromDot(char *dst, const char *src) {
    char ch;
    while ((ch = *dst) != '.' && ch != 0) {
        dst++;
    }
    strcpy(dst, src);
}

/* ==== seg000:0x0adc ==== */
void load3D3(char *fileName) {
    char FAR *objDataEnd;
    char FAR *dstPtr;
    struct SREGS sregs;
    int16 slot, subCount, sub;
    int16 chunk;
    strcpyFromDot(fileName, ".3D3");
    fileHandle = fopen(fileName, "rb");
    fread(&sign3d3, 2, 1, fileHandle);
    fread(&size3d3, 2, 1, fileHandle);
    fread(buf3d3, 2, size3d3, fileHandle);
    fread(&size3d3_2, 2, 1, fileHandle);
    objDataEnd = g_world3dData + size3d3_2;
    buf3d3[size3d3] = size3d3_2;
    segread(&sregs);
    for (dstPtr = g_world3dData; size3d3_2 > 0; size3d3_2 -= 0x800, dstPtr += 0x800) {
        chunk = (size3d3_2 >= 0x800) ? 0x800 : size3d3_2;
        fread(flt15_buf2, 1, chunk, fileHandle);
        movedata(sregs.ds, PTR_OFF(flt15_buf2), FP_SEG(dstPtr), FP_OFF(dstPtr), chunk);
    }
    fread(&size3d3_3, 1, 1, fileHandle);
    if (size3d3_3 != 0) {
        fread(buf3d3_1, 1, size3d3_3, fileHandle);
        fread(buf3d3_2, 1, size3d3_3, fileHandle);
        fread(buf3d3_3, 1, size3d3_3, fileHandle);
        fread(&size3d3_4, 1, 1, fileHandle);
        fread(g_modelVertX, 2, size3d3_4, fileHandle);
        fread(&size3d3_5, 1, 1, fileHandle);
        fread(g_modelVertY, 2, size3d3_5, fileHandle);
        fread(&size3d3_6, 1, 1, fileHandle);
        fread(g_modelVertZ, 2, size3d3_6, fileHandle);
    }
    fclose(fileHandle);
    for (slot = 0; slot < 2; slot++) {
        if ((subCount = g_targetSlots[slot].flags >> 8) != 0) {
            fileHandle = fopen("photo.3d3", "rb");
            fread(&sign3d3, 2, 1, fileHandle);
            fread(&size3d3_7, 2, 1, fileHandle);
            fread(g_modelOffsetTable, 2, size3d3_7, fileHandle);
            fread(&size3d3_2, 2, 1, fileHandle);
            g_modelOffsetTable[size3d3_7] = size3d3_2;
            for (sub = 0; sub <= subCount; sub++) {
                chunk = g_modelOffsetTable[sub + 1] - g_modelOffsetTable[sub];
                while (chunk > 0x800) {
                    fread(flt15_buf2, 1, 0x800, fileHandle);
                    chunk -= 0x800;
                }
                segread(&sregs);
                fread(flt15_buf2, 1, chunk, fileHandle);
                movedata(sregs.ds, PTR_OFF(flt15_buf2), FP_SEG(objDataEnd), FP_OFF(objDataEnd), chunk);
            }
            objDataEnd += chunk;
            if (slot == 0) {
                buf3d3[size3d3 + 1] = buf3d3[size3d3] + chunk;
            }
            fclose(fileHandle);
        }
    }
    if (objDataEnd - g_world3dData > 0x7530) {
        drawStringBothPages("ObjData overflow", 0, 0x60, 0xF);
    }
}

/* ==== seg000:0xf7c ==== */
void load3DG(void) {
    int16 unused_1, unused_2, unused_3;
    strcpyFromDot(regnFile, ".3dG");
    fileHandle = fopen(regnFile, "rb");
    fread(&sign3dg, 2, 1, fileHandle);
    fread(buf1_3dg, 1, 0x10, fileHandle);
    fread(buf1_3dg, 1, 0x100, fileHandle);
    fread(buf2_3dg, 1, 0x200, fileHandle);
    fread(buf3_3dg, 1, 0x200, fileHandle);
    fread(buf4_3dg, 1, 0x200, fileHandle);
    fclose(fileHandle);
    memcpy(g_topLodGrid, g_theaterGrids + ((int16)g_viewParamsFar[0x1C] << 6), 0x40);
}

/* ==== seg000:0xe1a ==== */
#define OBJ(off) ((struct TileSceneObject *)(buf_3dt + (off)))
void load3DT(char *fileName) {
    int16 cat, tile, obj, byteOff, shape;
    strcpyFromDot(fileName, ".3dT");
    fileHandle = fopen(fileName, "rb");
    fread(&sign3dt, 2, 1, fileHandle);
    fread(sizes3dt, 2, 5, fileHandle);
    for (cat = 0; cat < 5; cat++) {
        fread(matrix3dt[cat], 2, sizes3dt[cat], fileHandle);
    }
    byteOff = 0;
    for (cat = 0; cat < 5; cat++) {
        for (tile = 0; sizes3dt[cat] > tile; tile++) {
            matrix3dt_2[cat][tile] = f19_dsegOff(OBJ(byteOff));
            for (obj = 0; matrix3dt[cat][tile] > obj; obj++) {
                fread(&OBJ(byteOff)->x, 2, 1, fileHandle);
                fread(&OBJ(byteOff)->y, 2, 1, fileHandle);
                fread(&OBJ(byteOff)->z, 2, 1, fileHandle);
                fread(&shape, 2, 1, fileHandle);
                OBJ(byteOff)->shape = (uint8)shape;
                byteOff += sizeof(struct TileSceneObject);
            }
        }
    }
    fclose(fileHandle);
}

/* ==== seg000:0xcb8c ==== */
void load15Flt3d3(void) {
    char FAR *dst;
    struct SREGS sregs;
    int16 size3d3_2;
    int16 chunk;
    strcpyFromDot(regnStr, ".3D3");
    fileHandle = fopen(regnStr, "rb");
    if (fileHandle == NULL) {
        printError("Open Error on *.3D3");
        return;
    }
    fread(&sign3d3, 2, 1, fileHandle);
    fread(&size3d3, 2, 1, fileHandle);
    fread(buf3d3, 2, size3d3, fileHandle);
    fread(&size3d3_2, 2, 1, fileHandle);
    segread(&sregs);
    for (dst = g_world3dData; size3d3_2 > 0; size3d3_2 -= 0x800, dst += 0x800) {
        chunk = (size3d3_2 > 0x800) ? 0x800 : size3d3_2;
        fread(flt15_buf2, 1, chunk, fileHandle);
        movedata(sregs.ds, PTR_OFF(flt15_buf2), FP_SEG(dst), FP_OFF(dst), chunk);
    }
    fclose(fileHandle);
}
