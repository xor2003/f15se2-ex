#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include "r3d_gl.h"
#include "shared/common.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

extern bool setGamePath(const char *path);
extern void decodePic(SDL_IOStream *handle, int segment);
extern void picBlit(SDL_IOStream *handle, int pageIndex);
extern int loadReplacementPngToPage(const char *filename, int page);
extern int loadReplacementPngToSprite(const char *filename, int segment);
extern int loadReplacementPngToHiResTitle(const char *filename);
extern int asound_testCompareReplacementCues(void);
extern int asound_testCompareReplacementIntroMusic(void);

#if !defined(F15_ORIGINAL_ASSETS)
#define F15_ORIGINAL_ASSETS ""
#endif

#if !defined(F15_CONVERTED_ASSETS)
#define F15_CONVERTED_ASSETS ""
#endif

#if !defined(F15_ASSET_TOOL_COMMAND)
#define F15_ASSET_TOOL_COMMAND ""
#endif

namespace {

struct Counts {
    int structured = 0;
    int images = 0;
    int fonts = 0;
    int sounds = 0;
    int music = 0;
    int shapes3d = 0;
    int skippedNonRenderable3d = 0;
};

void require(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

std::string upperString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return value;
}

void setReplacementRoot(const std::filesystem::path *root) {
#if !defined(_WIN32)
    if (root) setenv("F15_REPLACEMENT_ROOT", root->string().c_str(), 1);
    else unsetenv("F15_REPLACEMENT_ROOT");
#else
    (void)root;
#endif
}

bool isRuntimeStructuredAsset(const std::filesystem::path &path) {
    const std::string ext = upperString(path.extension().string());
    return ext == ".WLD" || ext == ".3DT" || ext == ".3DG";
}

bool isImageAsset(const std::filesystem::path &path) {
    const std::string ext = upperString(path.extension().string());
    const std::string name = upperString(path.filename().string());
    /* 1.PIC..4.PIC are DEMO.EXE-only VGGA pictures. The current game runtime
     * has no DEMO loader path to call, so comparing them through showPicFile
     * would test the wrong legacy loader. They remain covered by the full
     * converter validator until the DEMO loader is ported. */
    if (name == "1.PIC" || name == "2.PIC" || name == "3.PIC" || name == "4.PIC") {
        return false;
    }
    return ext == ".PIC" || ext == ".SPR";
}

bool is3d3Asset(const std::filesystem::path &path) {
    return upperString(path.extension().string()) == ".3D3";
}

std::vector<unsigned char> readOpenFileBytes(const std::string &logicalName) {
    SDL_IOStream *io = openFile(logicalName.c_str(), 0);
    require(io != nullptr, "openFile failed for " + logicalName);

    std::vector<unsigned char> out;
    unsigned char chunk[4096];
    for (;;) {
        const size_t got = SDL_ReadIO(io, chunk, sizeof(chunk));
        if (got > 0) {
            out.insert(out.end(), chunk, chunk + got);
            continue;
        }
        break;
    }
    fileClose(io);
    require(!out.empty(), "openFile returned empty data for " + logicalName);
    return out;
}

std::vector<unsigned char> readHostFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

uint16_t rd16(const std::vector<unsigned char> &data, size_t off) {
    require(off + 2 <= data.size(), "short read while parsing 3D3");
    return (uint16_t)data[off] | ((uint16_t)data[off + 1] << 8);
}

std::vector<unsigned char> surfacePixels(SDL_Surface *surface) {
    require(surface != nullptr, "missing SDL surface");
    std::vector<unsigned char> out((size_t)surface->w * (size_t)surface->h);
    if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
    for (int y = 0; y < surface->h; y++) {
        const unsigned char *row = (const unsigned char *)surface->pixels + (size_t)y * surface->pitch;
        std::memcpy(out.data() + (size_t)y * surface->w, row, (size_t)surface->w);
    }
    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
    return out;
}

std::vector<unsigned char> activePaletteRgb() {
    SDL_Palette *palette = gfx_getPalette();
    std::vector<unsigned char> out(256u * 3u);
    require(palette != nullptr && palette->ncolors > 0, "missing active palette");
    const int count = palette->ncolors < 256 ? palette->ncolors : 256;
    for (int i = 0; i < count; i++) {
        out[(size_t)i * 3u] = palette->colors[i].r;
        out[(size_t)i * 3u + 1u] = palette->colors[i].g;
        out[(size_t)i * 3u + 2u] = palette->colors[i].b;
    }
    return out;
}

void clearSurface(SDL_Surface *surface) {
    require(surface != nullptr, "missing SDL surface");
    if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
    std::memset(surface->pixels, 0, (size_t)surface->pitch * surface->h);
    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
}

uint32_t pngCrc32(const unsigned char *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

uint32_t pngAdler32(const std::vector<unsigned char> &data) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (unsigned char value : data) {
        a = (a + value) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void pngAppendU32(std::vector<unsigned char> &out, uint32_t value) {
    out.push_back((unsigned char)((value >> 24) & 0xff));
    out.push_back((unsigned char)((value >> 16) & 0xff));
    out.push_back((unsigned char)((value >> 8) & 0xff));
    out.push_back((unsigned char)(value & 0xff));
}

void pngAppendChunk(std::vector<unsigned char> &out, const char type[4], const std::vector<unsigned char> &payload) {
    pngAppendU32(out, (uint32_t)payload.size());
    const size_t typePos = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    pngAppendU32(out, pngCrc32(out.data() + typePos, 4 + payload.size()));
}

void writeTinyRgbaPng(const std::filesystem::path &path) {
    const unsigned width = 2;
    const unsigned height = 2;
    std::vector<unsigned char> raw = {
        0, 255, 0, 0, 255, 0, 255, 0, 255,   /* filter + red, green */
        0, 0, 0, 255, 255, 255, 255, 0, 255, /* filter + blue, yellow */
    };
    std::vector<unsigned char> idat = {0x78, 0x01, 0x01};
    const uint16_t len = (uint16_t)raw.size();
    idat.push_back((unsigned char)(len & 0xff));
    idat.push_back((unsigned char)((len >> 8) & 0xff));
    idat.push_back((unsigned char)((~len) & 0xff));
    idat.push_back((unsigned char)(((~len) >> 8) & 0xff));
    idat.insert(idat.end(), raw.begin(), raw.end());
    pngAppendU32(idat, pngAdler32(raw));

    std::vector<unsigned char> ihdr;
    pngAppendU32(ihdr, width);
    pngAppendU32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});

    std::vector<unsigned char> png = {137, 80, 78, 71, 13, 10, 26, 10};
    pngAppendChunk(png, "IHDR", ihdr);
    pngAppendChunk(png, "IDAT", idat);
    pngAppendChunk(png, "IEND", {});

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), (std::streamsize)png.size());
}

void writeTinyIndexedPng(const std::filesystem::path &path) {
    std::vector<unsigned char> raw = {0, 1}; /* filter + one palette index */
    std::vector<unsigned char> idat = {0x78, 0x01, 0x01};
    const uint16_t len = (uint16_t)raw.size();
    idat.push_back((unsigned char)(len & 0xff));
    idat.push_back((unsigned char)((len >> 8) & 0xff));
    idat.push_back((unsigned char)((~len) & 0xff));
    idat.push_back((unsigned char)(((~len) >> 8) & 0xff));
    idat.insert(idat.end(), raw.begin(), raw.end());
    pngAppendU32(idat, pngAdler32(raw));

    std::vector<unsigned char> ihdr;
    pngAppendU32(ihdr, 1);
    pngAppendU32(ihdr, 1);
    ihdr.insert(ihdr.end(), {8, 3, 0, 0, 0});
    std::vector<unsigned char> plte = {0, 0, 0, 255, 255, 255};

    std::vector<unsigned char> png = {137, 80, 78, 71, 13, 10, 26, 10};
    pngAppendChunk(png, "IHDR", ihdr);
    pngAppendChunk(png, "PLTE", plte);
    pngAppendChunk(png, "IDAT", idat);
    pngAppendChunk(png, "IEND", {});

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), (std::streamsize)png.size());
}

void writeTinyPaletteLessIndexedPng(const std::filesystem::path &path) {
    std::vector<unsigned char> raw = {0, 1}; /* filter + one palette index */
    std::vector<unsigned char> idat = {0x78, 0x01, 0x01};
    const uint16_t len = (uint16_t)raw.size();
    idat.push_back((unsigned char)(len & 0xff));
    idat.push_back((unsigned char)((len >> 8) & 0xff));
    idat.push_back((unsigned char)((~len) & 0xff));
    idat.push_back((unsigned char)(((~len) >> 8) & 0xff));
    idat.insert(idat.end(), raw.begin(), raw.end());
    pngAppendU32(idat, pngAdler32(raw));

    std::vector<unsigned char> ihdr;
    pngAppendU32(ihdr, 1);
    pngAppendU32(ihdr, 1);
    ihdr.insert(ihdr.end(), {8, 3, 0, 0, 0});

    std::vector<unsigned char> png = {137, 80, 78, 71, 13, 10, 26, 10};
    pngAppendChunk(png, "IHDR", ihdr);
    pngAppendChunk(png, "IDAT", idat);
    pngAppendChunk(png, "IEND", {});

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), (std::streamsize)png.size());
}

void compareTruecolorTitle640ReplacementPath(Counts &counts) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("f15-title640-rgba-" + std::to_string((long long)SDL_GetTicks()));
    const std::filesystem::path titlePath = root / "TITLE640.png";
    writeTinyRgbaPng(titlePath);
    setReplacementRoot(&root);
    require(loadReplacementPngToHiResTitle("TITLE640.PIC") != 0,
            "truecolor TITLE640 PNG replacement should load");
    SDL_Surface *present = gfx_testGetHiResPresentSurface();
    require(present != nullptr, "truecolor TITLE640 replacement should install a hi-res present surface");
    require(present != gfx_getHiResSurface(), "truecolor TITLE640 replacement should not present the indexed fallback surface");
    require(present->format == SDL_PIXELFORMAT_RGBA32, "truecolor TITLE640 replacement should preserve RGBA present format");
    require(present->w == 2 && present->h == 2,
            "truecolor TITLE640 replacement should preserve source resolution while presenting in the hi-res title rectangle");
    writeTinyIndexedPng(titlePath);
    require(loadReplacementPngToHiResTitle("TITLE640.PIC") != 0,
            "indexed TITLE640 PNG replacement should load after truecolor replacement");
    present = gfx_testGetHiResPresentSurface();
    require(present == gfx_getHiResSurface(),
            "indexed TITLE640 replacement should clear stale truecolor present surface");
    require(present->format == SDL_PIXELFORMAT_INDEX8,
            "indexed TITLE640 replacement should present the indexed hi-res surface");

    writeTinyRgbaPng(titlePath);
    require(loadReplacementPngToHiResTitle("TITLE640.PIC") != 0,
            "second truecolor TITLE640 PNG replacement should load");
    require(gfx_testGetHiResPresentSurface() != gfx_getHiResSurface(),
            "second truecolor TITLE640 replacement should reinstall an RGBA present surface");
    writeTinyPaletteLessIndexedPng(titlePath);
    require(loadReplacementPngToHiResTitle("TITLE640.PIC") == 0,
            "palette-less indexed TITLE640 PNG should be rejected");
    require(gfx_testGetHiResPresentSurface() == gfx_getHiResSurface(),
            "rejected indexed TITLE640 replacement should clear stale truecolor present surface");

    gfx_setHiResReplacementSurface(NULL);
    setReplacementRoot(nullptr);
    std::filesystem::remove_all(root);
    counts.images++;
}

void compareTruecolorPageReplacementDoesNotHijackTitleSurface(Counts &counts) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("f15-page-rgba-" + std::to_string((long long)SDL_GetTicks()));
    const std::filesystem::path picPath = root / "DESK.png";
    writeTinyRgbaPng(picPath);

    gfx_setHiResReplacementSurface(NULL);
    setReplacementRoot(&root);
    require(loadReplacementPngToPage("DESK.PIC", 0) != 0,
            "truecolor normal PIC PNG replacement should load");
    SDL_Surface *pageReplacement = gfx_testGetPageReplacementSurface();
    require(pageReplacement != nullptr,
            "truecolor normal PIC PNG replacement should install a truecolor page background");
    require(pageReplacement->format == SDL_PIXELFORMAT_RGBA32,
            "truecolor normal PIC PNG replacement should preserve RGBA page background format");
    require(pageReplacement->w == 2 && pageReplacement->h == 2,
            "truecolor normal PIC PNG replacement should preserve source resolution");
    require(gfx_testGetHiResPresentSurface() == gfx_getHiResSurface(),
            "truecolor normal PIC PNG replacement must not install the TITLE640 present surface");
    gfx_setPageReplacementSurface(NULL);

    setReplacementRoot(nullptr);
    std::filesystem::remove_all(root);
    counts.images++;
}

void compareStructuredAssets(const std::filesystem::path &originalRoot,
                             const std::filesystem::path &convertedRoot,
                             Counts &counts) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(originalRoot)) {
        if (!entry.is_regular_file() || !isRuntimeStructuredAsset(entry.path())) continue;
        const std::string logicalName = std::filesystem::relative(entry.path(), originalRoot).generic_string();
        setReplacementRoot(nullptr);
        const std::vector<unsigned char> legacy = readOpenFileBytes(logicalName);
        setReplacementRoot(&convertedRoot);
        const std::vector<unsigned char> replacement = readOpenFileBytes(logicalName);
        setReplacementRoot(nullptr);
        require(legacy == replacement, "runtime structured loader mismatch: " + logicalName);
        counts.structured++;
    }
}

void compareImageAsset(const std::filesystem::path &originalRoot,
                       const std::filesystem::path &convertedRoot,
                       const std::filesystem::path &assetPath,
                       Counts &counts) {
    const std::string logicalName = std::filesystem::relative(assetPath, originalRoot).generic_string();
    const std::string upperName = upperString(assetPath.filename().string());
    const bool isSpr = upperString(assetPath.extension().string()) == ".SPR";
    const bool isTitle640 = upperName == "TITLE640.PIC";
    SDL_Surface *surface = nullptr;
    int spriteHandle = 0;

    if (isTitle640) {
        surface = gfx_getHiResSurface();
    } else if (isSpr) {
        spriteHandle = gfx_allocSpriteBuf();
        require(spriteHandle != 0, "could not allocate sprite buffer for " + logicalName);
        surface = gfx_getSpriteSurface(spriteHandle);
    } else {
        surface = gfx_getCurPageSurface();
    }

    clearSurface(surface);
    const std::vector<unsigned char> paletteBeforeLegacy = activePaletteRgb();
    setReplacementRoot(nullptr);
    SDL_IOStream *legacyIo = openFile(logicalName.c_str(), 0);
    require(legacyIo != nullptr, "failed to open legacy image " + logicalName);
    if (isTitle640) picBlit(legacyIo, 0);
    else if (isSpr) decodePic(legacyIo, spriteHandle);
    else showPicFile(legacyIo, 0);
    fileClose(legacyIo);
    const std::vector<unsigned char> legacyPixels = surfacePixels(surface);
    const std::vector<unsigned char> legacyPalette = activePaletteRgb();

    clearSurface(surface);
    setReplacementRoot(&convertedRoot);
    int loaded = 0;
    if (isTitle640) loaded = loadReplacementPngToHiResTitle(logicalName.c_str());
    else if (isSpr) loaded = loadReplacementPngToSprite(logicalName.c_str(), spriteHandle);
    else loaded = loadReplacementPngToPage(logicalName.c_str(), 0);
    setReplacementRoot(nullptr);
    require(loaded != 0, "missing or failed PNG replacement loader for " + logicalName);
    const std::vector<unsigned char> replacementPixels = surfacePixels(surface);
    const std::vector<unsigned char> replacementPalette = activePaletteRgb();

    if (spriteHandle) gfx_freeSpriteBuf(spriteHandle);
    require(legacyPixels == replacementPixels, "image runtime loader pixel mismatch: " + logicalName);
    /* Some original PIC/SPR files contain only indices and rely on the caller's
     * current DAC. Only assert runtime DAC equivalence when the legacy load
     * actually changed palette state; embedded PNG palette correctness for all
     * images is covered by the full validator. */
    if (legacyPalette != paletteBeforeLegacy) {
        require(legacyPalette == replacementPalette, "image runtime loader palette mismatch: " + logicalName);
    }
    counts.images++;
}

void compareImages(const std::filesystem::path &originalRoot,
                   const std::filesystem::path &convertedRoot,
                   Counts &counts) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(originalRoot)) {
        if (!entry.is_regular_file() || !isImageAsset(entry.path())) continue;
        compareImageAsset(originalRoot, convertedRoot, entry.path(), counts);
    }
}

void compareFonts(const std::filesystem::path &convertedRoot, Counts &counts) {
    /* These are the built-in runtime font slots with captured bitmaps/widths.
     * Enumerate the expected slots instead of discovering converted files, so a
     * missing replacement cannot make the test silently skip that font. */
    const int expectedFontIds[] = {0, 1, 3, 4, 5};

    for (int fontId : expectedFontIds) {
        char fontName[32] = {};
        char replacementPath[512] = {};
        unsigned char legacyBitmap[96 * 32] = {};
        unsigned char replacementBitmap[96 * 32] = {};
        unsigned char legacyWidths[96] = {};
        unsigned char replacementWidths[96] = {};
        int legacyHeight = 0, legacyWidth = 0, replacementHeight = 0, replacementWidth = 0;
        std::snprintf(fontName, sizeof(fontName), "font_%d", fontId);
        setReplacementRoot(&convertedRoot);
        const int hasReplacement = findReplacementAssetPath(fontName, ".bdf", replacementPath, sizeof(replacementPath)) ||
                                   findReplacementAssetPath(fontName, ".png", replacementPath, sizeof(replacementPath));
        setReplacementRoot(nullptr);
        require(hasReplacement != 0, "missing replacement font for font_" + std::to_string(fontId));
        setReplacementRoot(nullptr);
        require(gfx_testCopyBuiltinFont((uint16)fontId, legacyBitmap, sizeof(legacyBitmap), legacyWidths, sizeof(legacyWidths), &legacyHeight, &legacyWidth),
                "missing built-in font " + std::to_string(fontId));
        setReplacementRoot(&convertedRoot);
        require(gfx_testCopyEffectiveFont((uint16)fontId, replacementBitmap, sizeof(replacementBitmap), replacementWidths, sizeof(replacementWidths), &replacementHeight, &replacementWidth),
                "failed to load replacement font " + std::to_string(fontId));
        setReplacementRoot(nullptr);
        require(legacyHeight == replacementHeight && legacyWidth == replacementWidth,
                "font metrics mismatch for font_" + std::to_string(fontId));
        require(std::memcmp(legacyWidths, replacementWidths, sizeof(legacyWidths)) == 0,
                "font width mismatch for font_" + std::to_string(fontId));
        require(std::memcmp(legacyBitmap, replacementBitmap, (size_t)legacyHeight * 96u) == 0,
                "font glyph bitmap mismatch for font_" + std::to_string(fontId));
        counts.fonts++;
    }
}

void compareSoundsAndMusic(const std::filesystem::path &convertedRoot, Counts &counts) {
    setReplacementRoot(&convertedRoot);
    require(asound_testCompareReplacementCues() != 0, "sound cue WAV loader comparison failed");
    counts.sounds++;
    require(asound_testCompareReplacementIntroMusic() != 0, "intro music ASOUND loader comparison failed");
    counts.music++;
    setReplacementRoot(nullptr);
}

void compare3d3(const std::filesystem::path &originalRoot,
                const std::filesystem::path &convertedRoot,
                Counts &counts) {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(originalRoot)) {
        if (!entry.is_regular_file() || !is3d3Asset(entry.path())) continue;
        const std::string logicalName = std::filesystem::relative(entry.path(), originalRoot).generic_string();
        const std::vector<unsigned char> data = readHostFile(entry.path());
        if (data.size() < 6) continue;
        const uint16_t offsetWords = rd16(data, 2);
        const size_t tableStart = 4;
        const size_t tableBytes = (size_t)offsetWords * 2u;
        if (tableStart + tableBytes + 2 > data.size()) continue;
        const size_t modelSizePos = tableStart + tableBytes;
        const uint16_t modelSize = rd16(data, modelSizePos);
        const size_t modelStart = modelSizePos + 2;
        if (modelStart + modelSize > data.size()) continue;

        std::vector<uint16_t> offsets;
        for (uint16_t i = 0; i < offsetWords; i++) offsets.push_back(rd16(data, tableStart + (size_t)i * 2u));
        offsets.push_back(modelSize);

        for (size_t shape = 0; shape + 1 < offsets.size(); shape++) {
            if (offsets[shape] >= offsets[shape + 1] || offsets[shape + 1] > modelSize) continue;
            char replacementPath[512] = {};
            setReplacementRoot(&convertedRoot);
            const int hasReplacement = findReplacementShapeModelPath(logicalName.c_str(), (int)shape, ".glb", replacementPath, sizeof(replacementPath)) ||
                                       findReplacementShapeModelPath(logicalName.c_str(), (int)shape, ".glmesh", replacementPath, sizeof(replacementPath));
            const unsigned char *legacyShape = data.data() + modelStart + offsets[shape];
            const size_t legacyShapeSize = (size_t)(offsets[shape + 1] - offsets[shape]);
            if (!hasReplacement) {
                setReplacementRoot(nullptr);
                if (r3dgl_testLegacyShapeRenderable(legacyShape, legacyShapeSize)) {
                    require(false, "missing replacement for renderable 3D shape " + logicalName + " shape " + std::to_string(shape));
                }
                counts.skippedNonRenderable3d++;
                continue;
            }
            require(r3dgl_testCompareReplacementMesh(logicalName.c_str(), (int)shape, legacyShape, legacyShapeSize) != 0,
                    "3D runtime loader/source comparison failed for " + logicalName + " shape " + std::to_string(shape));
            setReplacementRoot(nullptr);
            counts.shapes3d++;
        }
    }
}

} // namespace

int main() {
    const std::filesystem::path originalRoot = F15_ORIGINAL_ASSETS;
    const std::filesystem::path convertedRoot = F15_CONVERTED_ASSETS;
    require(!originalRoot.empty() && std::filesystem::is_directory(originalRoot),
            "F15_ORIGINAL_ASSETS must point at an original game asset directory");
    require(!convertedRoot.empty() && std::filesystem::is_directory(convertedRoot),
            "F15_CONVERTED_ASSETS must point at a converted_assets_all directory");

    test_headless_init();
    gfx_videoInit();
    require(setGamePath(originalRoot.string().c_str()), "setGamePath failed for original assets");

#if !defined(_WIN32)
    if (std::string(F15_ASSET_TOOL_COMMAND).empty()) unsetenv("F15_ASSET_TOOL");
    else setenv("F15_ASSET_TOOL", F15_ASSET_TOOL_COMMAND, 1);
#endif

    Counts counts;
    compareTruecolorTitle640ReplacementPath(counts);
    compareTruecolorPageReplacementDoesNotHijackTitleSurface(counts);
    compareStructuredAssets(originalRoot, convertedRoot, counts);
    compareImages(originalRoot, convertedRoot, counts);
    compareFonts(convertedRoot, counts);
    compareSoundsAndMusic(convertedRoot, counts);
    compare3d3(originalRoot, convertedRoot, counts);

    gfx_videoShutdown();

    require(counts.structured > 0, "no structured assets were compared");
    require(counts.images > 0, "no images were compared");
    require(counts.fonts > 0, "no fonts were compared");
    require(counts.sounds > 0, "no sounds were compared");
    require(counts.music > 0, "no music was compared");
    require(counts.shapes3d > 0, "no 3D shapes were compared");

    std::cout << "asset_runtime_loader_validation_tests compared real loaders: "
              << "structured=" << counts.structured
              << ", images=" << counts.images
              << ", fonts=" << counts.fonts
              << ", sound_groups=" << counts.sounds
              << ", music_groups=" << counts.music
              << ", shapes3d=" << counts.shapes3d
              << ", skipped_non_renderable_3d=" << counts.skippedNonRenderable3d << '\n';
    return 0;
}
