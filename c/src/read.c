/*
 * read.c: a compiled drawing (.sdgb) read: the signature, then one intrabuf payload (intrabuf docs/wire-format.md),
 * of the schema vexelray-fields' dev.vexelray.fields.sdg.schema records describe (spec §4.2). Every message is FULL:
 * a nested one is framed by its minor and its length, so a field this does not know is skipped, as the format promises.
 * Then every rectangle and number is checked (spec §4.4).
 */
#include "sdg.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t SIGNATURE[8] = {0x89, 'S', 'D', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

typedef struct {
    const uint8_t *p;
    size_t end, pos;
    char *err;
    size_t err_size;
    int failed;
} reader;

static int fail(reader *r, const char *fmt, ...) {
    if (!r->failed && r->err && r->err_size) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(r->err, r->err_size, fmt, ap);
        va_end(ap);
    }
    r->failed = 1;
    return -1;
}

static uint64_t varint(reader *r) {
    uint64_t v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (r->pos >= r->end) {
            fail(r, "a damaged SDG file: cut short");
            return 0;
        }
        uint8_t b = r->p[r->pos++];
        v |= (uint64_t) (b & 0x7F) << shift;
        if (!(b & 0x80)) {
            return v;
        }
    }
    fail(r, "a damaged SDG file: a varint too long");
    return 0;
}

static int32_t zigzag32(reader *r) {
    uint64_t v = varint(r);
    return (int32_t) ((v >> 1) ^ (~(v & 1) + 1));
}

static int need(reader *r, size_t n) {
    if (r->end - r->pos < n) {
        return fail(r, "a damaged SDG file: cut short");
    }
    return 0;
}

static uint32_t fixed32(reader *r) {
    if (need(r, 4)) {
        return 0;
    }
    const uint8_t *b = r->p + r->pos;
    r->pos += 4;
    return (uint32_t) b[0] | (uint32_t) b[1] << 8 | (uint32_t) b[2] << 16 | (uint32_t) b[3] << 24;
}

static float f32(reader *r) {
    uint32_t u = fixed32(r);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static double f64(reader *r) {
    if (need(r, 8)) {
        return 0;
    }
    uint64_t u = 0;
    for (int i = 7; i >= 0; i--) {
        u = u << 8 | r->p[r->pos + i];
    }
    r->pos += 8;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

/* A string, UTF-8, as a NUL-terminated copy; a bytes field the same, its length out. */
static char *bytes(reader *r, size_t *out_len) {
    uint64_t n = varint(r);
    if (r->failed || n > r->end - r->pos) {
        fail(r, "a damaged SDG file: a string or bytes past the end");
        return NULL;
    }
    char *s = malloc((size_t) n + 1);
    if (!s) {
        fail(r, "out of memory");
        return NULL;
    }
    memcpy(s, r->p + r->pos, (size_t) n);
    s[n] = 0;
    r->pos += (size_t) n;
    if (out_len) {
        *out_len = (size_t) n;
    }
    return s;
}

/*
 * A FULL message's frame: its minor and its body's length, nested; the root's are given by the header instead. The
 * body is a bitset (its length first) and the fields. *bits gets the bitset's first 32 bits; *end where the body ends.
 */
typedef struct {
    uint64_t minor;
    size_t end;
    uint32_t bits;
} frame;

static int open_body(reader *r, frame *f) {
    uint64_t bitset = varint(r);
    if (r->failed || bitset > f->end - r->pos) {
        return fail(r, "a damaged SDG file: a bitset past its message");
    }
    f->bits = 0;
    for (uint64_t i = 0; i < bitset && i < 4; i++) {
        f->bits |= (uint32_t) r->p[r->pos + i] << (8 * i);
    }
    r->pos += (size_t) bitset;
    return 0;
}

static int open_nested(reader *r, frame *f) {
    f->minor = varint(r);
    uint64_t len = varint(r);
    if (r->failed || len > r->end - r->pos) {
        return fail(r, "a damaged SDG file: a message past the end");
    }
    f->end = r->pos + (size_t) len;
    return open_body(r, f);
}

/* Past the fields this knows: to the end of the message, newer fields skipped. */
static int close_nested(reader *r, const frame *f) {
    if (r->pos > f->end) {
        return fail(r, "a damaged SDG file: a message read past its length");
    }
    r->pos = f->end;
    return 0;
}

static int read_stencil(reader *r, sdg_stencil *s) {
    frame f;
    if (open_nested(r, &f)) {
        return -1;
    }
    char *kind = bytes(r, NULL);
    if (!kind) {
        return -1;
    }
    s->kind = !strcmp(kind, "sdf") ? SDG_STENCIL_SDF : !strcmp(kind, "msdf") ? SDG_STENCIL_MSDF : SDG_STENCIL_UNKNOWN;
    free(kind);
    s->page = zigzag32(r);
    s->channel = zigzag32(r);
    s->x = zigzag32(r);
    s->y = zigzag32(r);
    s->width = zigzag32(r);
    s->height = zigzag32(r);
    s->origin_x = f64(r);
    s->origin_y = f64(r);
    s->scale = f64(r);
    s->range = f32(r);
    return r->failed ? -1 : close_nested(r, &f);
}

static int read_paint(reader *r, sdg_paint *p) {
    frame f;
    if (open_nested(r, &f)) {
        return -1;
    }
    char *kind = bytes(r, NULL);
    if (!kind) {
        return -1;
    }
    int solid_kind = !strcmp(kind, "solid"), sampled_kind = !strcmp(kind, "sampled");
    free(kind);
    p->kind = SDG_PAINT_UNKNOWN;
    p->role = NULL;
    if (f.bits & 1) {   /* solid, present */
        frame s;
        if (open_nested(r, &s)) {
            return -1;
        }
        p->rgba = fixed32(r);
        if (s.minor >= 1 && (s.bits & 1)) {   /* role, since 1 */
            p->role = bytes(r, NULL);
            if (!p->role) {
                return -1;
            }
        }
        if (close_nested(r, &s)) {
            return -1;
        }
        if (solid_kind) {
            p->kind = SDG_PAINT_SOLID;
        }
    }
    if (f.bits & 2) {   /* sampled, present */
        frame s;
        if (open_nested(r, &s)) {
            return -1;
        }
        p->page = zigzag32(r);
        p->x = zigzag32(r);
        p->y = zigzag32(r);
        p->width = zigzag32(r);
        p->height = zigzag32(r);
        p->origin_x = f64(r);
        p->origin_y = f64(r);
        p->scale = f64(r);
        if (r->failed || close_nested(r, &s)) {
            return -1;
        }
        if (sampled_kind) {
            p->kind = SDG_PAINT_SAMPLED;
        }
    }
    if ((solid_kind && !(f.bits & 1)) || (sampled_kind && !(f.bits & 2))) {
        return fail(r, "a damaged SDG file: a %s paint without its values", solid_kind ? "solid" : "sampled");
    }
    return close_nested(r, &f);
}

static int read_layers(reader *r, sdg_layers *out, int depth);

static int read_layer(reader *r, sdg_layer *l, int depth) {
    frame f;
    if (open_nested(r, &f)) {
        return -1;
    }
    l->name = bytes(r, NULL);
    if (!l->name) {
        return -1;
    }
    l->opacity = f32(r);
    if (read_stencil(r, &l->stencil) || read_paint(r, &l->paint)) {
        return -1;
    }
    if (f.minor >= 1 && (f.bits & 1)) {   /* group, since 1 */
        if (depth >= SDG_MAX_DEPTH) {
            return fail(r, "layer \"%s\" nests groups past %d deep", l->name, SDG_MAX_DEPTH);
        }
        frame g;
        if (open_nested(r, &g)) {
            return -1;
        }
        l->is_group = 1;
        if (read_layers(r, &l->children, depth + 1)) {
            return -1;
        }
        if (g.bits & 1) {
            l->has_clip = 1;
            if (read_stencil(r, &l->clip)) {
                return -1;
            }
        }
        if (g.bits & 2) {
            frame m;
            if (open_nested(r, &m)) {
                return -1;
            }
            char *kind = bytes(r, NULL);
            if (!kind) {
                return -1;
            }
            l->has_mask = 1;
            l->mask_kind = !strcmp(kind, "luminance") ? SDG_MASK_LUMINANCE
                         : !strcmp(kind, "alpha") ? SDG_MASK_ALPHA : SDG_MASK_UNKNOWN;
            free(kind);
            if (read_layers(r, &l->mask, depth + 1) || close_nested(r, &m)) {
                return -1;
            }
        }
        if (close_nested(r, &g)) {
            return -1;
        }
    }
    return close_nested(r, &f);
}

static int read_layers(reader *r, sdg_layers *out, int depth) {
    uint64_t n = varint(r);
    if (r->failed || n > r->end - r->pos) {   /* each layer is at least a byte */
        return fail(r, "a damaged SDG file: a list longer than the file");
    }
    out->items = calloc(n ? (size_t) n : 1, sizeof(sdg_layer));
    if (!out->items) {
        return fail(r, "out of memory");
    }
    out->count = (int) n;
    for (uint64_t i = 0; i < n; i++) {
        if (read_layer(r, &out->items[i], depth)) {
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ checks */

static int check_rect(reader *r, const sdg_drawing *d, const char *what, const char *name, int page, int x, int y,
                      int w, int h, double ox, double oy, double scale) {
    if (page < 0 || page >= d->page_count) {
        return fail(r, "layer \"%s\"'s %s reads page %d, and there are %d", name, what, page, d->page_count);
    }
    const sdg_page *p = &d->pages[page];
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || (long long) x + w > p->width || (long long) y + h > p->height) {
        return fail(r, "layer \"%s\"'s %s is %d x %d at (%d, %d), past its page's %d x %d", name, what, w, h, x, y,
                    p->width, p->height);
    }
    if (!isfinite(ox) || !isfinite(oy) || !(scale > 0) || !isfinite(scale)) {
        return fail(r, "layer \"%s\"'s %s is placed at (%g, %g) at a scale of %g", name, what, ox, oy, scale);
    }
    return 0;
}

static int check_stencil(reader *r, const sdg_drawing *d, const char *what, const char *name, const sdg_stencil *s) {
    if (check_rect(r, d, what, name, s->page, s->x, s->y, s->width, s->height, s->origin_x, s->origin_y, s->scale)) {
        return -1;
    }
    if (s->channel < 0 || s->channel > 3) {
        return fail(r, "layer \"%s\"'s %s reads channel %d", name, what, s->channel);
    }
    if (!(s->range > 0) || !isfinite(s->range)) {
        return fail(r, "layer \"%s\"'s %s has a range of %g", name, what, s->range);
    }
    return 0;
}

static int check_layers(reader *r, const sdg_drawing *d, const sdg_layers *ls) {
    for (int i = 0; i < ls->count; i++) {
        const sdg_layer *l = &ls->items[i];
        if (!(l->opacity >= 0 && l->opacity <= 1)) {
            return fail(r, "layer \"%s\" of opacity %g, outside 0 to 1", l->name, l->opacity);
        }
        if (l->is_group) {
            if ((l->has_clip && check_stencil(r, d, "clip", l->name, &l->clip))
                || (l->has_mask && check_layers(r, d, &l->mask)) || check_layers(r, d, &l->children)) {
                return -1;
            }
            continue;
        }
        if (check_stencil(r, d, "stencil", l->name, &l->stencil)) {
            return -1;
        }
        if (l->paint.kind == SDG_PAINT_SAMPLED && check_rect(r, d, "paint", l->name, l->paint.page, l->paint.x,
                l->paint.y, l->paint.width, l->paint.height, l->paint.origin_x, l->paint.origin_y, l->paint.scale)) {
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the drawing */

int sdg_read(const uint8_t *bytes_in, size_t length, sdg_drawing *out, char *err, size_t err_size) {
    memset(out, 0, sizeof *out);
    reader r = {bytes_in, length, 0, err, err_size, 0};
    if (length < 8 + 8 || memcmp(bytes_in, SIGNATURE, 8) != 0) {
        return fail(&r, "not a compiled SDG drawing: no signature");
    }
    r.pos = 8;
    uint8_t magic = bytes_in[r.pos++], flags = bytes_in[r.pos++];
    if (magic != 0xB1) {
        return fail(&r, "not a compiled SDG drawing: an intrabuf payload of wire version %d", magic & 0x0F);
    }
    if (flags & 0xF0 || ((flags >> 2) & 3) != 2) {
        return fail(&r, "a damaged SDG file: header flags 0x%02x", flags);
    }
    r.pos += 4;   /* schema id */
    uint64_t major = varint(&r);
    (void) varint(&r);   /* minor: the root's fields are all of its first version */
    if (flags & 2) {
        r.pos += 4;   /* fingerprint */
    }
    if (r.failed) {
        return -1;
    }
    if (major != SDG_MAJOR) {
        return fail(&r, "an SDG drawing of version %llu, this reads %d.x", (unsigned long long) major, SDG_MAJOR);
    }
    frame root = {0, length, 0};
    if (open_body(&r, &root)) {
        return -1;
    }
    out->width = f64(&r);
    out->height = f64(&r);
    uint64_t pages = varint(&r);
    if (r.failed || pages > length - r.pos) {
        sdg_free(out);
        return fail(&r, "a damaged SDG file: a list longer than the file");
    }
    out->pages = calloc(pages ? (size_t) pages : 1, sizeof(sdg_page));
    if (!out->pages) {
        return fail(&r, "out of memory");
    }
    out->page_count = (int) pages;
    for (uint64_t i = 0; i < pages; i++) {
        frame f;
        size_t n;
        if (open_nested(&r, &f)) {
            sdg_free(out);
            return -1;
        }
        char *png = bytes(&r, &n);
        if (!png || close_nested(&r, &f)) {
            free(png);
            sdg_free(out);
            return -1;
        }
        int rc = sdg_png_decode((const uint8_t *) png, n, &out->pages[i].width, &out->pages[i].height,
                                &out->pages[i].rgba);
        free(png);
        if (rc) {
            sdg_free(out);
            return fail(&r, "page %llu is not a PNG this reads (8 bits a channel, not interlaced)",
                        (unsigned long long) i);
        }
    }
    if (read_layers(&r, &out->layers, 0)) {
        sdg_free(out);
        return -1;
    }
    if (!(out->width > 0) || !(out->height > 0) || !isfinite(out->width) || !isfinite(out->height)
        || check_layers(&r, out, &out->layers)) {
        if (!r.failed) {
            fail(&r, "a canvas %g x %g", out->width, out->height);
        }
        sdg_free(out);
        return -1;
    }
    return 0;
}

static void free_layers(sdg_layers *ls) {
    for (int i = 0; i < ls->count; i++) {
        sdg_layer *l = &ls->items[i];
        free(l->name);
        free(l->paint.role);
        free_layers(&l->children);
        free_layers(&l->mask);
    }
    free(ls->items);
    ls->items = NULL;
    ls->count = 0;
}

void sdg_free(sdg_drawing *d) {
    for (int i = 0; i < d->page_count; i++) {
        free(d->pages[i].rgba);
    }
    free(d->pages);
    free_layers(&d->layers);
    memset(d, 0, sizeof *d);
}

sdg_drawing *sdg_open(const uint8_t *bytes_in, size_t length, char *err, size_t err_size) {
    sdg_drawing *d = malloc(sizeof *d);
    if (!d) {
        if (err && err_size) {
            snprintf(err, err_size, "out of memory");
        }
        return NULL;
    }
    if (sdg_read(bytes_in, length, d, err, err_size)) {
        free(d);
        return NULL;
    }
    return d;
}

void sdg_close(sdg_drawing *d) {
    if (d) {
        sdg_free(d);
        free(d);
    }
}

void sdg_release(void *buffer) {
    free(buffer);
}
