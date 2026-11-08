/*
 * render.c: the spec's §2, one sample a pixel, as vexelray-fields' SdgRender draws it, which is the reference: per
 * layer, the distance read between texels (channels first, then the median), from texels to units by the stencil's
 * scale and to pixels by the view's, cut half a pixel either side; times the paint's alpha and the layer's opacity, of
 * the paint's colour, over what is under it. A group (1.2) is drawn apart into a buffer of its own, its mask's too, and
 * laid over once. The arithmetic is SdgRender's, float where it is float and double where it is double, so the two
 * agree to the last step of a byte.
 */
#include "sdg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const sdg_drawing *d;
    int width, height;
    double origin_x, origin_y, upp;
    sdg_role_fn roles;
    void *role_context;
} view;

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float texel(const sdg_page *p, int x, int y, int c) {
    return p->rgba[((size_t) y * p->width + x) * 4 + c] / 255.0f;
}

/* Channel c of a rectangle at (u, v) in its texels, read between texel centres, clamped half a texel in. */
static float bilinear(const sdg_page *p, int x, int y, int w, int h, double u, double v, int c) {
    double fx = clampd(u, 0.5, w - 0.5) - 0.5, fy = clampd(v, 0.5, h - 0.5) - 0.5;
    int x0 = (int) floor(fx), y0 = (int) floor(fy);
    int x1 = x0 + 1 < w - 1 ? x0 + 1 : w - 1, y1 = y0 + 1 < h - 1 ? y0 + 1 : h - 1;
    float ax = (float) (fx - x0), ay = (float) (fy - y0);
    float a = texel(p, x + x0, y + y0, c), b = texel(p, x + x1, y + y0, c);
    float d = texel(p, x + x0, y + y1, c), e = texel(p, x + x1, y + y1, c);
    return (a + (b - a) * ax) * (1 - ay) + (d + (e - d) * ax) * ay;
}

static float median3(float a, float b, float c) {
    float lo = a < b ? a : b, hi = a < b ? b : a;
    return c < lo ? lo : c > hi ? hi : c;
}

/* The stencil's signed distance at canvas point (cx, cy), in its texels; -infinity past its rectangle. */
static double distance(const sdg_stencil *s, const sdg_page *p, double cx, double cy) {
    double u = (cx - s->origin_x) * s->scale, v = (cy - s->origin_y) * s->scale;
    if (u < 0 || v < 0 || u > s->width || v > s->height) {
        return -INFINITY;
    }
    if (s->kind == SDG_STENCIL_SDF) {
        return (bilinear(p, s->x, s->y, s->width, s->height, u, v, s->channel) - 0.5) * s->range;
    }
    float r = bilinear(p, s->x, s->y, s->width, s->height, u, v, 0);
    float g = bilinear(p, s->x, s->y, s->width, s->height, u, v, 1);
    float b = bilinear(p, s->x, s->y, s->width, s->height, u, v, 2);
    return (median3(r, g, b) - 0.5) * s->range;
}

static void paint_at(const view *vw, const sdg_paint *paint, double cx, double cy, float out[4]) {
    if (paint->kind == SDG_PAINT_SOLID) {
        uint32_t c = paint->rgba;
        if (paint->role && vw->roles) {
            uint32_t themed;
            if (vw->roles(vw->role_context, paint->role, &themed)) {
                c = themed;
            }
        }
        out[0] = (c >> 24) / 255.0f;
        out[1] = (c >> 16 & 0xFF) / 255.0f;
        out[2] = (c >> 8 & 0xFF) / 255.0f;
        out[3] = (c & 0xFF) / 255.0f;
        return;
    }
    const sdg_page *p = &vw->d->pages[paint->page];
    double u = (cx - paint->origin_x) * paint->scale, v = (cy - paint->origin_y) * paint->scale;
    for (int c = 0; c < 4; c++) {
        out[c] = bilinear(p, paint->x, paint->y, paint->width, paint->height, u, v, c);
    }
}

static int known(const sdg_layer *l) {
    return !l->is_group && l->stencil.kind != SDG_STENCIL_UNKNOWN && l->paint.kind != SDG_PAINT_UNKNOWN;
}

static void shape(const view *vw, const sdg_layer *layer, float *px) {
    if (!known(layer) || layer->opacity <= 0) {
        return;
    }
    const sdg_stencil *s = &layer->stencil;
    const sdg_page *page = &vw->d->pages[s->page];
    double ox = vw->origin_x, oy = vw->origin_y, upp = vw->upp;
    /* only the pixels whose centres can fall in the stencil's rectangle */
    double cx0 = s->origin_x, cy0 = s->origin_y;
    double cx1 = cx0 + s->width / s->scale, cy1 = cy0 + s->height / s->scale;
    int i0 = (int) floor((cx0 - ox) / upp - 0.5), j0 = (int) floor((cy0 - oy) / upp - 0.5);
    int i1 = (int) ceil((cx1 - ox) / upp + 0.5), j1 = (int) ceil((cy1 - oy) / upp + 0.5);
    if (i0 < 0) i0 = 0;
    if (j0 < 0) j0 = 0;
    if (i1 > vw->width) i1 = vw->width;
    if (j1 > vw->height) j1 = vw->height;
    float colour[4];
    for (int j = j0; j < j1; j++) {
        double cy = oy + (j + 0.5) * upp;
        for (int i = i0; i < i1; i++) {
            double cx = ox + (i + 0.5) * upp;
            double d = distance(s, page, cx, cy);
            float cover = (float) clampd(0.5 + d / s->scale / upp, 0, 1);
            if (cover <= 0) {
                continue;
            }
            paint_at(vw, &layer->paint, cx, cy, colour);
            float a = cover * colour[3] * layer->opacity;
            float keep = 1 - a;
            size_t o = ((size_t) j * vw->width + i) * 4;
            px[o] = colour[0] * a + px[o] * keep;
            px[o + 1] = colour[1] * a + px[o + 1] * keep;
            px[o + 2] = colour[2] * a + px[o + 2] * keep;
            px[o + 3] = a + px[o + 3] * keep;
        }
    }
}

static int draw(const view *vw, const sdg_layers *layers, float *px);

/* A group: its layers into g, its mask's into m, then g * (opacity * k * c) over px. */
static int group(const view *vw, const sdg_layer *layer, float *px) {
    if (layer->opacity <= 0 || (layer->has_mask && layer->mask_kind == SDG_MASK_UNKNOWN)) {
        return 0;   /* nothing to see; a mask of a kind this does not know hides its group */
    }
    size_t n = (size_t) vw->width * vw->height * 4;
    float *g = calloc(n, sizeof(float));
    float *m = layer->has_mask ? calloc(n, sizeof(float)) : NULL;
    if (!g || (layer->has_mask && !m)) {
        free(g);
        free(m);
        return -1;
    }
    if (draw(vw, &layer->children, g) || (m && draw(vw, &layer->mask, m))) {
        free(g);
        free(m);
        return -1;
    }
    int luminance = layer->has_mask && layer->mask_kind == SDG_MASK_LUMINANCE;
    const sdg_stencil *clip = layer->has_clip && layer->clip.kind != SDG_STENCIL_UNKNOWN ? &layer->clip : NULL;
    const sdg_page *clip_page = clip ? &vw->d->pages[clip->page] : NULL;
    for (int j = 0; j < vw->height; j++) {
        double cy = vw->origin_y + (j + 0.5) * vw->upp;
        for (int i = 0; i < vw->width; i++) {
            size_t o = ((size_t) j * vw->width + i) * 4;
            if (g[o + 3] <= 0) {
                continue;
            }
            float f = layer->opacity;
            if (m) {
                f *= luminance ? 0.2126f * m[o] + 0.7152f * m[o + 1] + 0.0722f * m[o + 2] : m[o + 3];
            }
            if (clip) {
                double cx = vw->origin_x + (i + 0.5) * vw->upp;
                double d = distance(clip, clip_page, cx, cy);
                f *= (float) clampd(0.5 + d / clip->scale / vw->upp, 0, 1);
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
    free(g);
    free(m);
    return 0;
}

static int draw(const view *vw, const sdg_layers *layers, float *px) {
    for (int i = 0; i < layers->count; i++) {
        const sdg_layer *l = &layers->items[i];
        if (l->is_group) {
            if (group(vw, l, px)) {
                return -1;
            }
        } else {
            shape(vw, l, px);
        }
    }
    return 0;
}

/* Java's Math.round(float) on a value clamped to 0..1, times 255. */
static uint8_t byte8(float v) {
    return (uint8_t) floorf(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

int sdg_render(const sdg_drawing *d, int width, int height, double origin_x, double origin_y,
               double units_per_pixel, sdg_role_fn roles, void *role_context, uint8_t *rgba) {
    size_t n = (size_t) width * height * 4;
    float *px = calloc(n, sizeof(float));   /* premultiplied */
    if (!px) {
        return -1;
    }
    view vw = {d, width, height, origin_x, origin_y, units_per_pixel, roles, role_context};
    if (draw(&vw, &d->layers, px)) {
        free(px);
        return -1;
    }
    for (size_t o = 0; o < n; o += 4) {
        float a = px[o + 3];
        float inv = a > 0 ? 1 / a : 0;
        rgba[o] = byte8(px[o] * inv);
        rgba[o + 1] = byte8(px[o + 1] * inv);
        rgba[o + 2] = byte8(px[o + 2] * inv);
        rgba[o + 3] = byte8(a);
    }
    free(px);
    return 0;
}

int sdg_render_fit(const sdg_drawing *d, int width, int height, uint8_t *rgba) {
    double upp = fmax(d->width / width, d->height / height);
    double ox = (d->width - width * upp) / 2, oy = (d->height - height * upp) / 2;
    return sdg_render(d, width, height, ox, oy, upp, NULL, NULL, rgba);
}
