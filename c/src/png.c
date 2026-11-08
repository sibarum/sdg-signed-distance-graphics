/*
 * png.c: PNG pages both ways with nothing but the C library. Decodes 8-bit grey, grey and alpha, RGB, RGBA and palette
 * images, not interlaced, as the format's Java reader does (vexelray-fields' Png); encodes RGBA as stored deflate
 * blocks, which every PNG reader takes. The inflate is RFC 1951 whole: stored, fixed and dynamic Huffman blocks.
 */
#include "sdg.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------ inflate */

typedef struct {
    const uint8_t *in;
    size_t len, pos;
    uint32_t bits;
    int nbits;
    uint8_t *out;
    size_t out_len, out_cap;
    int error;
} inflater;

static int bit(inflater *s, int n, uint32_t *v) {
    while (s->nbits < n) {
        if (s->pos >= s->len) {
            s->error = 1;
            return -1;
        }
        s->bits |= (uint32_t) s->in[s->pos++] << s->nbits;
        s->nbits += 8;
    }
    *v = s->bits & ((1u << n) - 1u);
    s->bits >>= n;
    s->nbits -= n;
    return 0;
}

static int put(inflater *s, uint8_t b) {
    if (s->out_len >= s->out_cap) {
        s->error = 1;   /* more than the image holds: a damaged stream */
        return -1;
    }
    s->out[s->out_len++] = b;
    return 0;
}

/* A canonical Huffman code: counts per length, and symbols ordered by code. */
typedef struct {
    uint16_t count[16];
    uint16_t symbol[320];
} huffman;

static int build(huffman *h, const uint8_t *lengths, int n) {
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    offs[1] = 0;
    for (int i = 1; i < 15; i++) {
        offs[i + 1] = (uint16_t) (offs[i] + h->count[i]);
    }
    for (int i = 0; i < n; i++) {
        if (lengths[i]) {
            h->symbol[offs[lengths[i]]++] = (uint16_t) i;
        }
    }
    return 0;
}

static int decode(inflater *s, const huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        uint32_t b;
        if (bit(s, 1, &b)) {
            return -1;
        }
        code |= (int) b;
        int count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->error = 1;
    return -1;
}

static const uint16_t LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                      99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5,
                                      5, 0};
static const uint16_t DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11,
                                       11, 12, 12, 13, 13};

static int codes(inflater *s, const huffman *lit, const huffman *dist) {
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) {
            return -1;
        }
        if (sym < 256) {
            if (put(s, (uint8_t) sym)) {
                return -1;
            }
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) {
                s->error = 1;
                return -1;
            }
            uint32_t e;
            if (bit(s, LEN_EXTRA[sym], &e)) {
                return -1;
            }
            size_t len = LEN_BASE[sym] + e;
            int d = decode(s, dist);
            if (d < 0 || d >= 30) {
                s->error = 1;
                return -1;
            }
            if (bit(s, DIST_EXTRA[d], &e)) {
                return -1;
            }
            size_t back = DIST_BASE[d] + e;
            if (back > s->out_len) {
                s->error = 1;
                return -1;
            }
            for (size_t i = 0; i < len; i++) {
                if (put(s, s->out[s->out_len - back])) {
                    return -1;
                }
            }
        }
    }
}

static int dynamic(inflater *s, huffman *lit, huffman *dist) {
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint32_t hlit, hdist, hclen;
    if (bit(s, 5, &hlit) || bit(s, 5, &hdist) || bit(s, 4, &hclen)) {
        return -1;
    }
    hlit += 257;
    hdist += 1;
    hclen += 4;
    uint8_t lengths[320] = {0};
    for (uint32_t i = 0; i < hclen; i++) {
        uint32_t v;
        if (bit(s, 3, &v)) {
            return -1;
        }
        lengths[ORDER[i]] = (uint8_t) v;
    }
    huffman lens;
    build(&lens, lengths, 19);
    memset(lengths, 0, sizeof lengths);
    uint32_t n = 0;
    while (n < hlit + hdist) {
        int sym = decode(s, &lens);
        if (sym < 0) {
            return -1;
        }
        if (sym < 16) {
            lengths[n++] = (uint8_t) sym;
        } else {
            uint32_t rep, len = 0;
            if (sym == 16) {
                if (n == 0 || bit(s, 2, &rep)) {
                    s->error = 1;
                    return -1;
                }
                len = lengths[n - 1];
                rep += 3;
            } else if (sym == 17) {
                if (bit(s, 3, &rep)) {
                    return -1;
                }
                rep += 3;
            } else {
                if (bit(s, 7, &rep)) {
                    return -1;
                }
                rep += 11;
            }
            if (n + rep > hlit + hdist) {
                s->error = 1;
                return -1;
            }
            while (rep--) {
                lengths[n++] = (uint8_t) len;
            }
        }
    }
    build(lit, lengths, (int) hlit);
    build(dist, lengths + hlit, (int) hdist);
    return 0;
}

/* A zlib stream (RFC 1950) inflated into out, exactly out_cap bytes expected. */
static int inflate_zlib(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap) {
    if (len < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) {
        return -1;
    }
    inflater s = {in, len, 2, 0, 0, out, 0, out_cap, 0};
    uint32_t last;
    do {
        uint32_t type;
        if (bit(&s, 1, &last) || bit(&s, 2, &type)) {
            return -1;
        }
        if (type == 0) {
            s.bits = 0;
            s.nbits = 0;
            if (s.pos + 4 > s.len) {
                return -1;
            }
            size_t n = s.in[s.pos] | (s.in[s.pos + 1] << 8);
            size_t nn = s.in[s.pos + 2] | (s.in[s.pos + 3] << 8);
            s.pos += 4;
            if ((n ^ 0xFFFF) != nn || s.pos + n > s.len) {
                return -1;
            }
            for (size_t i = 0; i < n; i++) {
                if (put(&s, s.in[s.pos++])) {
                    return -1;
                }
            }
        } else if (type == 1) {
            huffman lit, dist;
            uint8_t lengths[320];
            for (int i = 0; i < 144; i++) lengths[i] = 8;
            for (int i = 144; i < 256; i++) lengths[i] = 9;
            for (int i = 256; i < 280; i++) lengths[i] = 7;
            for (int i = 280; i < 288; i++) lengths[i] = 8;
            build(&lit, lengths, 288);
            for (int i = 0; i < 30; i++) lengths[i] = 5;
            build(&dist, lengths, 30);
            if (codes(&s, &lit, &dist)) {
                return -1;
            }
        } else if (type == 2) {
            huffman lit, dist;
            if (dynamic(&s, &lit, &dist) || codes(&s, &lit, &dist)) {
                return -1;
            }
        } else {
            return -1;
        }
    } while (!last);
    return s.error || s.out_len != out_cap ? -1 : 0;
}

/* ------------------------------------------------------------------------------------------------ PNG */

static const uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

static uint32_t crc_table[256];
static int crc_ready;

static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t c) {
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) {
                v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            }
            crc_table[i] = v;
        }
        crc_ready = 1;
    }
    c ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

int sdg_png_decode(const uint8_t *png, size_t length, int *width, int *height, uint8_t **rgba) {
    if (length < 8 || memcmp(png, SIGNATURE, 8) != 0) {
        return -1;
    }
    size_t pos = 8;
    int w = 0, h = 0, type = -1, ended = 0;
    const uint8_t *palette = NULL, *trns = NULL;
    size_t palette_len = 0, trns_len = 0;
    uint8_t *idat = NULL;
    size_t idat_len = 0;
    while (!ended) {
        if (length - pos < 12) {
            free(idat);
            return -1;
        }
        uint32_t n = be32(png + pos);
        if (n > length - pos - 12) {
            free(idat);
            return -1;
        }
        const uint8_t *tag = png + pos + 4, *data = tag + 4;
        if (crc32_of(tag, 4 + (size_t) n, 0) != be32(data + n)) {
            free(idat);
            return -1;
        }
        if (!memcmp(tag, "IHDR", 4) && n >= 13) {
            w = (int) be32(data);
            h = (int) be32(data + 4);
            int depth = data[8];
            type = data[9];
            if (w <= 0 || h <= 0 || w > 65535 || h > 65535 || depth != 8 || data[10] || data[11] || data[12]
                || !(type == 0 || type == 2 || type == 3 || type == 4 || type == 6)) {
                free(idat);
                return -1;
            }
        } else if (!memcmp(tag, "PLTE", 4)) {
            palette = data;
            palette_len = n;
        } else if (!memcmp(tag, "tRNS", 4)) {
            trns = data;
            trns_len = n;
        } else if (!memcmp(tag, "IDAT", 4)) {
            uint8_t *grown = realloc(idat, idat_len + n + 1);
            if (!grown) {
                free(idat);
                return -1;
            }
            idat = grown;
            memcpy(idat + idat_len, data, n);
            idat_len += n;
        } else if (!memcmp(tag, "IEND", 4)) {
            ended = 1;
        } else if (tag[0] >= 'A' && tag[0] <= 'Z') {
            free(idat);
            return -1;   /* a critical chunk this does not know */
        }
        pos += 12 + (size_t) n;
    }
    if (type < 0 || (type == 3 && !palette)) {
        free(idat);
        return -1;
    }
    int channels = type == 0 || type == 3 ? 1 : type == 4 ? 2 : type == 2 ? 3 : 4;
    size_t stride = (size_t) w * channels;
    size_t raw_len = (size_t) h * (stride + 1);
    uint8_t *raw = malloc(raw_len);
    uint8_t *out = malloc((size_t) w * h * 4);
    if (!raw || !out || inflate_zlib(idat, idat_len, raw, raw_len)) {
        free(idat);
        free(raw);
        free(out);
        return -1;
    }
    free(idat);
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t) y * (stride + 1);
        uint8_t *prev = y > 0 ? row - (stride + 1) : NULL;
        int f = row[0];
        if (f > 4) {
            free(raw);
            free(out);
            return -1;
        }
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t) channels ? row[1 + i - channels] : 0;
            int b = prev ? prev[1 + i] : 0;
            int c = prev && i >= (size_t) channels ? prev[1 + i - channels] : 0;
            int add = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) >> 1 : paeth(a, b, c);
            row[1 + i] = (uint8_t) (row[1 + i] + add);
        }
        for (int x = 0; x < w; x++) {
            const uint8_t *s = row + 1 + (size_t) x * channels;
            uint8_t *d = out + ((size_t) y * w + x) * 4;
            switch (type) {
                case 0:
                    d[0] = d[1] = d[2] = s[0];
                    d[3] = trns && trns_len >= 2 && trns[0] == 0 && s[0] == trns[1] ? 0 : 255;
                    break;
                case 4:
                    d[0] = d[1] = d[2] = s[0];
                    d[3] = s[1];
                    break;
                case 2:
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    d[3] = trns && trns_len >= 6 && !trns[0] && !trns[2] && !trns[4] && s[0] == trns[1]
                           && s[1] == trns[3] && s[2] == trns[5] ? 0 : 255;
                    break;
                case 3:
                    if ((size_t) s[0] * 3 + 2 >= palette_len) {
                        free(raw);
                        free(out);
                        return -1;
                    }
                    d[0] = palette[s[0] * 3];
                    d[1] = palette[s[0] * 3 + 1];
                    d[2] = palette[s[0] * 3 + 2];
                    d[3] = trns && s[0] < trns_len ? trns[s[0]] : 255;
                    break;
                default:
                    memcpy(d, s, 4);
            }
        }
    }
    free(raw);
    *width = w;
    *height = h;
    *rgba = out;
    return 0;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static size_t chunk(uint8_t *p, const char *tag, const uint8_t *data, uint32_t n) {
    put32(p, n);
    memcpy(p + 4, tag, 4);
    if (n) {
        memcpy(p + 8, data, n);
    }
    put32(p + 8 + n, crc32_of(p + 4, 4 + (size_t) n, 0));
    return 12 + (size_t) n;
}

int sdg_png_encode(const uint8_t *rgba, int width, int height, uint8_t **png, size_t *length) {
    size_t stride = (size_t) width * 4 + 1, raw_len = stride * height;
    size_t blocks = raw_len / 65535 + 1;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    uint8_t *out = malloc(8 + 25 + 12 + z_len + 12);
    if (!z || !out) {
        free(z);
        free(out);
        return -1;
    }
    /* zlib: stored blocks of the filtered (filter 0) rows, and the Adler-32 of them */
    size_t zp = 0, done = 0;
    uint32_t a = 1, b = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;
    for (size_t blk = 0; blk < blocks; blk++) {
        size_t n = raw_len - done < 65535 ? raw_len - done : 65535;
        z[zp++] = blk == blocks - 1 ? 1 : 0;
        z[zp++] = (uint8_t) n;
        z[zp++] = (uint8_t) (n >> 8);
        z[zp++] = (uint8_t) ~n;
        z[zp++] = (uint8_t) (~n >> 8);
        for (size_t i = 0; i < n; i++, done++) {
            size_t row = done / stride, col = done % stride;
            uint8_t v = col == 0 ? 0 : rgba[row * (size_t) width * 4 + col - 1];
            z[zp++] = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
    }
    put32(z + zp, b << 16 | a);
    zp += 4;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t) width);
    put32(ihdr + 4, (uint32_t) height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    size_t p = 0;
    memcpy(out, SIGNATURE, 8);
    p += 8;
    p += chunk(out + p, "IHDR", ihdr, 13);
    p += chunk(out + p, "IDAT", z, (uint32_t) zp);
    p += chunk(out + p, "IEND", NULL, 0);
    free(z);
    *png = out;
    *length = p;
    return 0;
}
