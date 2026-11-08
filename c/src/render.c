/*
 * render.c: the spec's §2, one sample a pixel, as vexelray-fields' SdgRender draws it, which is the reference: per
 * layer, the distance read between texels (channels first, then the median), from texels to units by the stencil's
 * scale and to pixels by the view's, cut half a pixel either side; times the paint's alpha and the layer's opacity, of
 * the paint's colour, over what is under it. A group (1.2) is drawn apart into a buffer of its own, its mask's too, and
 * laid over once. The arithmetic is SdgRender's, float where it is float and double where it is double, so the two
 * agree to the last step of a byte.
 *
 * It is SdgRender's arithmetic, not its order of work, and the differences are each one the arithmetic allows, so the
 * pixels stay the same, byte for byte (tests/fast.c holds this against tests/reference.c, a plain transcription):
 *
 *  - A pixel's texels and weights along x depend on its column only, and along y on its row, so each layer works
 *    them out once a column and once a row (tap), not once a pixel.
 *  - A bilinear read lies between the least and the greatest of its four texels, and the median of three channels
 *    between the medians of their least and their greatest. So where every texel an 8 x 8 tile of pixels reads is far
 *    enough outside the shape, every pixel of the tile is covered exactly 0, and the tile is skipped; where every one
 *    is far enough inside, exactly 1, and the distance is not read. "Far enough" leaves SLACK for float rounding,
 *    which is a few millionths; texels are 1/255 apart.
 *  - A theme's colour for a role is asked once a layer, before drawing, not once a pixel.
 *  - The picture can be drawn in bands of rows, on as many threads; a band computes each of its pixels as the whole
 *    picture would, so the bands put together are the picture.
 */
#include "sdg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define TILE 8
#define SLACK 1e-4f
#define MAX_THREADS 64
#define BAND_PIXELS 8192

/* A byte's value as a texel channel: exactly byte / 255.0f, as SdgRender divides. */
#define U1(k) (k) / 255.0f
#define U4(k) U1(k), U1((k) + 1), U1((k) + 2), U1((k) + 3)
#define U16(k) U4(k), U4((k) + 4), U4((k) + 8), U4((k) + 12)
#define U64(k) U16(k), U16((k) + 16), U16((k) + 32), U16((k) + 48)
static const float UNIT[256] = {U64(0), U64(64), U64(128), U64(192)};

typedef struct {
    const sdg_drawing *d;
    int width, height;          /* the whole picture */
    int row0, row1;             /* the band drawn: rows row0 to row1 - 1; its buffers are row1 - row0 rows tall */
    double origin_x, origin_y, upp;
    const uint32_t *solid;      /* each layer's solid colour, its role's if the theme has one, by its place in the tree */
} view;

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float median3(float a, float b, float c) {
    float lo = a < b ? a : b, hi = a < b ? b : a;
    return c < lo ? lo : c > hi ? hi : c;
}

/* Where one column (or row) of pixels reads a rectangle: its two texels along that axis, and the weight between. */
typedef struct {
    int lo, hi;
    float w;
} tap;

/* A rectangle of a page as pixels n0..n1-1 along each axis read it, and which of them fall inside it. */
typedef struct {
    const uint8_t *texels;      /* the rectangle's top-left texel */
    size_t stride;              /* bytes a page row */
    tap *tx, *ty;
    unsigned char *inx, *iny;
} grid;

/* Pixel n's tap: its centre o + (n + 0.5) * upp, read by a rectangle size texels long starting at org, at scale. */
static void taps(tap *t, unsigned char *in, int n0, int n1, double o, double upp, double org, double scale,
                 int size) {
    for (int n = n0; n < n1; n++) {
        double u = (o + (n + 0.5) * upp - org) * scale;
        in[n - n0] = !(u < 0 || u > size);
        double f = clampd(u, 0.5, size - 0.5) - 0.5;
        int lo = (int) floor(f);
        t[n - n0].lo = lo;
        t[n - n0].hi = lo + 1 < size - 1 ? lo + 1 : size - 1;
        t[n - n0].w = (float) (f - lo);
    }
}

static int grid_make(grid *g, const view *vw, const sdg_page *p, int x, int y, int w, int h, double org_x,
                     double org_y, double scale, int i0, int i1, int j0, int j1) {
    size_t nx = (size_t) (i1 - i0), ny = (size_t) (j1 - j0);
    g->tx = malloc((nx + ny) * (sizeof(tap) + 1));
    if (!g->tx) {
        return -1;
    }
    g->ty = g->tx + nx;
    g->inx = (unsigned char *) (g->ty + ny);
    g->iny = g->inx + nx;
    g->texels = p->rgba + ((size_t) y * p->width + x) * 4;
    g->stride = (size_t) p->width * 4;
    taps(g->tx, g->inx, i0, i1, vw->origin_x, vw->upp, org_x, scale, w);
    taps(g->ty, g->iny, j0, j1, vw->origin_y, vw->upp, org_y, scale, h);
    return 0;
}

static void grid_free(grid *g) {
    free(g->tx);
}

/* Channel c read between texels, as SdgRender's bilinear reads it. */
static float grid_at(const grid *g, const tap *tx, const tap *ty, int c) {
    const uint8_t *r0 = g->texels + ty->lo * g->stride, *r1 = g->texels + ty->hi * g->stride;
    float a = UNIT[r0[tx->lo * 4 + c]], b = UNIT[r0[tx->hi * 4 + c]];
    float d = UNIT[r1[tx->lo * 4 + c]], e = UNIT[r1[tx->hi * 4 + c]];
    float ax = tx->w, ay = ty->w;
    return (a + (b - a) * ax) * (1 - ay) + (d + (e - d) * ax) * ay;
}

/* The stencil's value between texels (0.5 on the edge), before it becomes a distance. */
static float stencil_value(const sdg_stencil *s, const grid *g, const tap *tx, const tap *ty) {
    if (s->kind == SDG_STENCIL_SDF) {
        return grid_at(g, tx, ty, s->channel);
    }
    return median3(grid_at(g, tx, ty, 0), grid_at(g, tx, ty, 1), grid_at(g, tx, ty, 2));
}

/* How much of a pixel a stencil value covers: its distance in texels, to units, to pixels, cut at half a pixel. */
static float cover_of(float m, const sdg_stencil *s, double upp) {
    double d = (m - 0.5) * s->range;
    return (float) clampd(0.5 + d / s->scale / upp, 0, 1);
}

/* The least and the greatest value the stencil can read from texels x0..x1, y0..y1 of its rectangle. */
static void stencil_bounds(const sdg_stencil *s, const grid *g, int x0, int x1, int y0, int y1, float *lo,
                           float *hi) {
    int c0 = s->kind == SDG_STENCIL_SDF ? s->channel : 0, c1 = s->kind == SDG_STENCIL_SDF ? s->channel : 2;
    uint8_t mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
    for (int y = y0; y <= y1; y++) {
        const uint8_t *row = g->texels + y * g->stride;
        for (int x = x0; x <= x1; x++) {
            for (int c = c0; c <= c1; c++) {
                uint8_t v = row[x * 4 + c];
                if (v < mn[c - c0]) mn[c - c0] = v;
                if (v > mx[c - c0]) mx[c - c0] = v;
            }
        }
    }
    if (c0 == c1) {
        *lo = UNIT[mn[0]];
        *hi = UNIT[mx[0]];
    } else {
        *lo = median3(UNIT[mn[0]], UNIT[mn[1]], UNIT[mn[2]]);
        *hi = median3(UNIT[mx[0]], UNIT[mx[1]], UNIT[mx[2]]);
    }
}

static int known(const sdg_layer *l) {
    return !l->is_group && l->stencil.kind != SDG_STENCIL_UNKNOWN && l->paint.kind != SDG_PAINT_UNKNOWN;
}

enum { OUTSIDE, INSIDE, EDGE };

static int shape(const view *vw, const sdg_layer *layer, uint32_t solid, float *px) {
    if (!known(layer) || layer->opacity <= 0) {
        return 0;
    }
    const sdg_stencil *s = &layer->stencil;
    const sdg_paint *paint = &layer->paint;
    double ox = vw->origin_x, oy = vw->origin_y, upp = vw->upp;
    /* only the pixels whose centres can fall in the stencil's rectangle */
    double cx0 = s->origin_x, cy0 = s->origin_y;
    double cx1 = cx0 + s->width / s->scale, cy1 = cy0 + s->height / s->scale;
    int i0 = (int) floor((cx0 - ox) / upp - 0.5), j0 = (int) floor((cy0 - oy) / upp - 0.5);
    int i1 = (int) ceil((cx1 - ox) / upp + 0.5), j1 = (int) ceil((cy1 - oy) / upp + 0.5);
    if (i0 < 0) i0 = 0;
    if (j0 < vw->row0) j0 = vw->row0;
    if (i1 > vw->width) i1 = vw->width;
    if (j1 > vw->row1) j1 = vw->row1;
    if (i0 >= i1 || j0 >= j1) {
        return 0;
    }
    grid st, pt;
    if (grid_make(&st, vw, &vw->d->pages[s->page], s->x, s->y, s->width, s->height, s->origin_x, s->origin_y,
                  s->scale, i0, i1, j0, j1)) {
        return -1;
    }
    int sampled = paint->kind == SDG_PAINT_SAMPLED;
    if (sampled && grid_make(&pt, vw, &vw->d->pages[paint->page], paint->x, paint->y, paint->width, paint->height,
                             paint->origin_x, paint->origin_y, paint->scale, i0, i1, j0, j1)) {
        grid_free(&st);
        return -1;
    }
    float colour[4] = {(solid >> 24) / 255.0f, (solid >> 16 & 0xFF) / 255.0f, (solid >> 8 & 0xFF) / 255.0f,
                       (solid & 0xFF) / 255.0f};
    /* tiles only where a tile reads a few texels; further out, each pixel reads apart from its neighbours anyway */
    int tiled = s->range > 0 && s->scale > 0 && upp > 0 && TILE * upp * s->scale <= 6;
    int span = tiled ? TILE : (i1 - i0 > j1 - j0 ? i1 - i0 : j1 - j0);
    float opacity = layer->opacity;
    for (int tj = j0; tj < j1; tj += span) {
        int te_j = j1 - tj > span ? tj + span : j1;
        const tap *ya = &st.ty[tj - j0], *yb = &st.ty[te_j - 1 - j0];
        int rows_in = st.iny[tj - j0] && st.iny[te_j - 1 - j0];
        for (int ti = i0; ti < i1; ti += span) {
            int te_i = i1 - ti > span ? ti + span : i1;
            int kind = EDGE;
            if (tiled) {
                const tap *xa = &st.tx[ti - i0], *xb = &st.tx[te_i - 1 - i0];
                float lo, hi;
                stencil_bounds(s, &st, xa->lo, xb->hi, ya->lo, yb->hi, &lo, &hi);
                if (cover_of(hi + SLACK, s, upp) <= 0) {
                    kind = OUTSIDE;
                } else if (rows_in && st.inx[ti - i0] && st.inx[te_i - 1 - i0]
                           && cover_of(lo - SLACK, s, upp) >= 1) {
                    kind = INSIDE;
                }
            }
            if (kind == OUTSIDE) {
                continue;
            }
            for (int j = tj; j < te_j; j++) {
                const tap *ty = &st.ty[j - j0];
                if (kind == EDGE && !st.iny[j - j0]) {
                    continue;
                }
                float *row = px + (size_t) (j - vw->row0) * vw->width * 4;
                for (int i = ti; i < te_i; i++) {
                    float cover = 1.0f;
                    if (kind == EDGE) {
                        if (!st.inx[i - i0]) {
                            continue;
                        }
                        cover = cover_of(stencil_value(s, &st, &st.tx[i - i0], ty), s, upp);
                        if (cover <= 0) {
                            continue;
                        }
                    }
                    if (sampled) {
                        for (int c = 0; c < 4; c++) {
                            colour[c] = grid_at(&pt, &pt.tx[i - i0], &pt.ty[j - j0], c);
                        }
                    }
                    float a = cover * colour[3] * opacity;
                    float keep = 1 - a;
                    float *o = row + (size_t) i * 4;
                    o[0] = colour[0] * a + o[0] * keep;
                    o[1] = colour[1] * a + o[1] * keep;
                    o[2] = colour[2] * a + o[2] * keep;
                    o[3] = a + o[3] * keep;
                }
            }
        }
    }
    grid_free(&st);
    if (sampled) {
        grid_free(&pt);
    }
    return 0;
}

/* How many layers a tree holds, groups and what is in them, masks too. */
static int tree_size(const sdg_layers *layers) {
    int n = 0;
    for (int i = 0; i < layers->count; i++) {
        const sdg_layer *l = &layers->items[i];
        n += 1 + (l->is_group ? tree_size(&l->children) + tree_size(&l->mask) : 0);
    }
    return n;
}

/* Each layer's solid colour by its place in the tree (a layer, then its group's children, then its mask's). */
static void resolve(const sdg_layers *layers, sdg_role_fn roles, void *context, uint32_t *out, int *next) {
    for (int i = 0; i < layers->count; i++) {
        const sdg_layer *l = &layers->items[i];
        uint32_t c = l->paint.rgba;
        if (!l->is_group && l->paint.kind == SDG_PAINT_SOLID && l->paint.role && roles) {
            uint32_t themed;
            if (roles(context, l->paint.role, &themed)) {
                c = themed;
            }
        }
        out[(*next)++] = c;
        if (l->is_group) {
            resolve(&l->children, roles, context, out, next);
            resolve(&l->mask, roles, context, out, next);
        }
    }
}

static int draw(const view *vw, const sdg_layers *layers, int *next, float *px);

/* A group: its layers into g, its mask's into m, then g * (opacity * k * c) over px. */
static int group(const view *vw, const sdg_layer *layer, int *next, float *px) {
    int masked_at = *next + tree_size(&layer->children), after = masked_at + tree_size(&layer->mask);
    if (layer->opacity <= 0 || (layer->has_mask && layer->mask_kind == SDG_MASK_UNKNOWN)) {
        *next = after;
        return 0;   /* nothing to see; a mask of a kind this does not know hides its group */
    }
    size_t n = (size_t) vw->width * (vw->row1 - vw->row0) * 4;
    float *g = calloc(n, sizeof(float));
    float *m = layer->has_mask ? calloc(n, sizeof(float)) : NULL;
    int failed = !g || (layer->has_mask && !m) || draw(vw, &layer->children, next, g);
    *next = masked_at;
    failed = failed || (m && draw(vw, &layer->mask, next, m));
    *next = after;
    int luminance = layer->has_mask && layer->mask_kind == SDG_MASK_LUMINANCE;
    const sdg_stencil *clip = layer->has_clip && layer->clip.kind != SDG_STENCIL_UNKNOWN ? &layer->clip : NULL;
    grid cg;
    failed = failed || (clip && grid_make(&cg, vw, &vw->d->pages[clip->page], clip->x, clip->y, clip->width,
                                          clip->height, clip->origin_x, clip->origin_y, clip->scale, 0, vw->width,
                                          vw->row0, vw->row1));
    if (failed) {
        free(g);
        free(m);
        return -1;
    }
    for (int j = vw->row0; j < vw->row1; j++) {
        int r = j - vw->row0;
        for (int i = 0; i < vw->width; i++) {
            size_t o = ((size_t) r * vw->width + i) * 4;
            if (g[o + 3] <= 0) {
                continue;
            }
            float f = layer->opacity;
            if (m) {
                f *= luminance ? 0.2126f * m[o] + 0.7152f * m[o + 1] + 0.0722f * m[o + 2] : m[o + 3];
            }
            if (clip) {
                /* outside the clip's rectangle the distance is -infinity: covered 0 */
                f *= cg.inx[i] && cg.iny[r] ? cover_of(stencil_value(clip, &cg, &cg.tx[i], &cg.ty[r]), clip, vw->upp)
                                            : 0.0f;
            }
            if (f <= 0) {
                continue;
            }
            float keep = 1 - g[o + 3] * f;
            for (int c = 0; c < 4; c++) {
                px[o + c] = g[o + c] * f + px[o + c] * keep;
            }
        }
    }
    if (clip) {
        grid_free(&cg);
    }
    free(g);
    free(m);
    return 0;
}

static int draw(const view *vw, const sdg_layers *layers, int *next, float *px) {
    for (int i = 0; i < layers->count; i++) {
        const sdg_layer *l = &layers->items[i];
        int me = (*next)++;
        if (l->is_group ? group(vw, l, next, px) : shape(vw, l, vw->solid[me], px)) {
            return -1;
        }
    }
    return 0;
}

/* Java's Math.round(float) on a value clamped to 0..1, times 255. */
static uint8_t byte8(float v) {
    return (uint8_t) floorf(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/* Rows row0..row1-1, drawn and written to rgba. */
static int band(const view *whole, int row0, int row1, uint8_t *rgba) {
    view vw = *whole;
    vw.row0 = row0;
    vw.row1 = row1;
    size_t n = (size_t) vw.width * (row1 - row0) * 4;
    float *px = calloc(n, sizeof(float));   /* premultiplied */
    int next = 0;
    if (!px || draw(&vw, &vw.d->layers, &next, px)) {
        free(px);
        return -1;
    }
    uint8_t *out = rgba + (size_t) row0 * vw.width * 4;
    for (size_t o = 0; o < n; o += 4) {
        float a = px[o + 3];
        float inv = a > 0 ? 1 / a : 0;
        out[o] = byte8(px[o] * inv);
        out[o + 1] = byte8(px[o + 1] * inv);
        out[o + 2] = byte8(px[o + 2] * inv);
        out[o + 3] = byte8(a);
    }
    free(px);
    return 0;
}

/* The bands, taken one at a time by whichever thread is free. */
typedef struct {
    const view *vw;
    uint8_t *rgba;
    int rows, bands;
    volatile long next, failed;
} job;

static long take(volatile long *counter) {
#ifdef _WIN32
    return InterlockedIncrement(counter) - 1;
#else
    return __atomic_fetch_add(counter, 1, __ATOMIC_RELAXED);
#endif
}

static void work(job *jb) {
    long b;
    while ((b = take(&jb->next)) < jb->bands) {
        int row0 = (int) b * jb->rows;
        int row1 = row0 + jb->rows < jb->vw->height ? row0 + jb->rows : jb->vw->height;
        if (band(jb->vw, row0, row1, jb->rgba)) {
            take(&jb->failed);
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID jb) {
    work(jb);
    return 0;
}
#else
static void *worker(void *jb) {
    work(jb);
    return NULL;
}
#endif

static int processors(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int) info.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int) n : 1;
#endif
}

int sdg_render_threaded(const sdg_drawing *d, int width, int height, double origin_x, double origin_y,
                        double units_per_pixel, sdg_role_fn roles, void *role_context, int threads, uint8_t *rgba) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    int count = tree_size(&d->layers), next = 0;
    uint32_t *solid = malloc(sizeof(uint32_t) * (count > 0 ? count : 1));
    if (!solid) {
        return -1;
    }
    resolve(&d->layers, roles, role_context, solid, &next);
    view vw = {d, width, height, 0, height, origin_x, origin_y, units_per_pixel, solid};
    if (threads <= 0) {
        threads = processors();
    }
    if (threads > MAX_THREADS) {
        threads = MAX_THREADS;
    }
    /* four bands a thread, so one slow band does not keep the rest waiting; one band on one thread; and none so
       small that starting a thread costs more than it saves */
    int rows = threads == 1 ? height : (height + threads * 4 - 1) / (threads * 4);
    int least = (BAND_PIXELS + width - 1) / width;
    if (rows < least) {
        rows = least < height ? least : height;
    }
    job jb = {&vw, rgba, rows, (height + rows - 1) / rows, 0, 0};
    if (threads > jb.bands) {
        threads = jb.bands;
    }
#ifdef _WIN32
    HANDLE helpers[MAX_THREADS];
#else
    pthread_t helpers[MAX_THREADS];
#endif
    int started = 0;
    for (; started < threads - 1; started++) {
#ifdef _WIN32
        helpers[started] = CreateThread(NULL, 0, worker, &jb, 0, NULL);
        if (!helpers[started]) {
            break;
        }
#else
        if (pthread_create(&helpers[started], NULL, worker, &jb)) {
            break;
        }
#endif
    }
    work(&jb);   /* the caller's thread draws too, and on its own if no helper started */
    for (int t = 0; t < started; t++) {
#ifdef _WIN32
        WaitForSingleObject(helpers[t], INFINITE);
        CloseHandle(helpers[t]);
#else
        pthread_join(helpers[t], NULL);
#endif
    }
    free(solid);
    return jb.failed ? -1 : 0;
}

int sdg_render(const sdg_drawing *d, int width, int height, double origin_x, double origin_y,
               double units_per_pixel, sdg_role_fn roles, void *role_context, uint8_t *rgba) {
    return sdg_render_threaded(d, width, height, origin_x, origin_y, units_per_pixel, roles, role_context, 1, rgba);
}

int sdg_render_fit(const sdg_drawing *d, int width, int height, uint8_t *rgba) {
    double upp = fmax(d->width / width, d->height / height);
    double ox = (d->width - width * upp) / 2, oy = (d->height - height * upp) / 2;
    return sdg_render(d, width, height, ox, oy, upp, NULL, NULL, rgba);
}
