#ifndef F15_NET_SERIALIZE_H
#define F15_NET_SERIALIZE_H
/*
 * serialize.h - explicit little-endian wire encode/decode.
 *
 * The wire format is defined field-by-field (plan §26): no raw struct dumps,
 * no padding, no host-endian shortcuts. All multi-byte integers are written
 * least-significant byte first.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct NetWriter {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int overflow; /* sticky: set once any write ran out of space */
};

static inline void nwInit(struct NetWriter *w, void *buf, size_t cap) {
    w->buf = (uint8_t *)buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = 0;
}

static inline void nwBytes(struct NetWriter *w, const void *src, size_t n) {
    if (w->len + n > w->cap) {
        w->overflow = 1;
        return;
    }
    memcpy(w->buf + w->len, src, n);
    w->len += n;
}

static inline void nwU8(struct NetWriter *w, uint8_t v) { nwBytes(w, &v, 1); }

static inline void nwU16(struct NetWriter *w, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    nwBytes(w, b, 2);
}

static inline void nwU32(struct NetWriter *w, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    nwBytes(w, b, 4);
}

static inline void nwI16(struct NetWriter *w, int16_t v) { nwU16(w, (uint16_t)v); }
static inline void nwI32(struct NetWriter *w, int32_t v) { nwU32(w, (uint32_t)v); }
static inline void nwU64(struct NetWriter *w, uint64_t v) {
    nwU32(w, (uint32_t)v);
    nwU32(w, (uint32_t)(v >> 32));
}

/* Fixed-size string field: NUL-terminated, zero-padded to n bytes. */
static inline void nwStr(struct NetWriter *w, const char *s, size_t n) {
    size_t i = 0;
    if (s) {
        for (; i + 1 < n && s[i]; i++)
            nwU8(w, (uint8_t)s[i]);
    }
    for (; i < n; i++)
        nwU8(w, 0);
}

struct NetReader {
    const uint8_t *buf;
    size_t cap;
    size_t pos;
    int underrun; /* sticky: set once any read ran past the end */
};

static inline void nrInit(struct NetReader *r, const void *buf, size_t cap) {
    r->buf = (const uint8_t *)buf;
    r->cap = cap;
    r->pos = 0;
    r->underrun = 0;
}

static inline size_t nrLeft(const struct NetReader *r) { return r->cap - r->pos; }

static inline void nrBytes(struct NetReader *r, void *dst, size_t n) {
    if (r->pos + n > r->cap) {
        r->underrun = 1;
        memset(dst, 0, n);
        r->pos = r->cap;
        return;
    }
    memcpy(dst, r->buf + r->pos, n);
    r->pos += n;
}

static inline uint8_t nrU8(struct NetReader *r) {
    uint8_t v = 0;
    nrBytes(r, &v, 1);
    return v;
}

static inline uint16_t nrU16(struct NetReader *r) {
    uint8_t b[2] = {0, 0};
    nrBytes(r, b, 2);
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

static inline uint32_t nrU32(struct NetReader *r) {
    uint8_t b[4] = {0, 0, 0, 0};
    nrBytes(r, b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

static inline int16_t nrI16(struct NetReader *r) { return (int16_t)nrU16(r); }
static inline int32_t nrI32(struct NetReader *r) { return (int32_t)nrU32(r); }
static inline uint64_t nrU64(struct NetReader *r) {
    return (uint64_t)nrU32(r) | ((uint64_t)nrU32(r) << 32);
}

/* Read n bytes into a NUL-terminated buffer of size n+1 (dst must have room). */
static inline void nrStr(struct NetReader *r, char *dst, size_t n) {
    nrBytes(r, dst, n);
    dst[n] = 0;
}

static inline int nrSkip(struct NetReader *r, size_t n) {
    if (r->pos + n > r->cap) {
        r->underrun = 1;
        return 0;
    }
    r->pos += n;
    return 1;
}

#endif /* F15_NET_SERIALIZE_H */
