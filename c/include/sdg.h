/*
 * sdg.h: SDG (signed distance graphics) in C.
 *
 * Reads a compiled drawing (.sdgb, one intrabuf payload: the spec's §4.2) and rasterises it exactly as the spec's §2
 * says a reader draws, layers and groups (SDG 1.2) alike. Plain C11, no dependency beyond the C library: the pages'
 * PNGs are decoded by its own inflate.
 *
 * The spec is vexelray-fields' docs/sdg.md; the reference is its SdgRender (Java), and conformance/ in this repository
 * holds drawings and the pictures SdgRender draws of them, which this library is tested against.
 */
#ifndef SDG_H
#define SDG_H

#include <stddef.h>
#include <stdint.h>

/* What a shared build exports (SDG_SHARED_BUILD while building it, SDG_SHARED when using it), and nothing for a static one. */
#if defined(_WIN32) && defined(SDG_SHARED_BUILD)
#define SDG_API __declspec(dllexport)
#elif defined(_WIN32) && defined(SDG_SHARED)
#define SDG_API __declspec(dllimport)
#elif defined(SDG_SHARED_BUILD)
#define SDG_API __attribute__((visibility("default")))
#else
#define SDG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The version of the format this reads: a newer major is refused, a newer minor read as far as it is known. */
#define SDG_MAJOR 1
#define SDG_MINOR 2

/* The deepest groups may nest in a drawing this takes. */
#define SDG_MAX_DEPTH 16

/* RGBA8 texels, top row first: what every stencil and sampled paint reads. */
typedef struct {
    int width, height;
    uint8_t *rgba;
} sdg_page;

typedef enum { SDG_STENCIL_SDF, SDG_STENCIL_MSDF, SDG_STENCIL_UNKNOWN } sdg_stencil_kind;

/* Where a layer is: a rectangle of a page holding signed distances (spec §3.1). */
typedef struct {
    sdg_stencil_kind kind;
    int page, channel, x, y, width, height;
    double origin_x, origin_y, scale, range;
} sdg_stencil;

typedef enum { SDG_PAINT_SOLID, SDG_PAINT_SAMPLED, SDG_PAINT_UNKNOWN } sdg_paint_kind;

/* What colour a layer is where it covers (spec §3.2). */
typedef struct {
    sdg_paint_kind kind;
    uint32_t rgba;          /* solid: straight 0xRRGGBBAA */
    char *role;             /* solid: a theme colour's name to draw instead, or NULL */
    int page, x, y, width, height;  /* sampled */
    double origin_x, origin_y, scale;
} sdg_paint;

typedef struct sdg_layer sdg_layer;

typedef struct {
    sdg_layer *items;
    int count;
} sdg_layers;

typedef enum { SDG_MASK_LUMINANCE, SDG_MASK_ALPHA, SDG_MASK_UNKNOWN } sdg_mask_kind;

/* A layer: a shape (a stencil and a paint), or since 1.2 a group of layers drawn apart and laid over once. */
struct sdg_layer {
    char *name;
    float opacity;
    int is_group;
    sdg_stencil stencil;    /* a shape's */
    sdg_paint paint;        /* a shape's */
    sdg_layers children;    /* a group's */
    int has_clip;
    sdg_stencil clip;       /* a group's clip, if has_clip */
    int has_mask;
    sdg_mask_kind mask_kind;
    sdg_layers mask;        /* a group's mask's layers, if has_mask */
};

typedef struct {
    double width, height;   /* the canvas, in units; y down */
    sdg_page *pages;
    int page_count;
    sdg_layers layers;      /* back to front */
} sdg_drawing;

/*
 * Reads a compiled drawing: the eight-byte signature, then one intrabuf payload. Every rectangle is checked to lie in
 * its page and every number to be one (spec §4.4) before it is returned, so drawing it never reads out of bounds.
 * Returns 0, and fills *out; or -1, with a reason in err (if not NULL), and *out empty.
 */
SDG_API int sdg_read(const uint8_t *bytes, size_t length, sdg_drawing *out, char *err, size_t err_size);

/* Everything sdg_read made. */
SDG_API void sdg_free(sdg_drawing *drawing);

/*
 * A theme's colour for a role, for a solid paint that names one: return 1 and set *rgba (0xRRGGBBAA) to draw it in
 * that colour, or 0 to draw the paint's own. NULL draws every paint as authored.
 */
typedef int (*sdg_role_fn)(void *context, const char *role, uint32_t *rgba);

/*
 * Draws the drawing into rgba (width * height * 4 bytes, straight RGBA8, top row first), on transparent: pixel (i, j)
 * is the canvas point (origin_x + (i + 0.5) * units_per_pixel, origin_y + (j + 0.5) * units_per_pixel). Exactly as
 * the spec's §2 says, one sample a pixel. Returns 0, or -1 if memory ran out.
 */
SDG_API int sdg_render(const sdg_drawing *drawing, int width, int height, double origin_x, double origin_y,
               double units_per_pixel, sdg_role_fn roles, void *role_context, uint8_t *rgba);

/* sdg_render with the canvas fitted into width * height pixels, its aspect kept and centred. */
SDG_API int sdg_render_fit(const sdg_drawing *drawing, int width, int height, uint8_t *rgba);

/* A PNG of 8-bit channels, not interlaced, any colour type, as RGBA8: 0, and *rgba malloc'd; or -1. */
SDG_API int sdg_png_decode(const uint8_t *png, size_t length, int *width, int *height, uint8_t **rgba);

/* RGBA8 as a PNG (stored, not compressed): 0, and *png malloc'd with *length; or -1. */
SDG_API int sdg_png_encode(const uint8_t *rgba, int width, int height, uint8_t **png, size_t *length);

/*
 * The same as sdg_read and sdg_free, for a caller that cannot lay out sdg_drawing itself (a foreign-function binding):
 * the drawing on the heap, or NULL with a reason in err. Close it with sdg_close.
 */
SDG_API sdg_drawing *sdg_open(const uint8_t *bytes, size_t length, char *err, size_t err_size);
SDG_API void sdg_close(sdg_drawing *drawing);

/* A buffer from this library (a PNG sdg_png_encode made, pixels sdg_png_decode made) freed by its own allocator. */
SDG_API void sdg_release(void *buffer);

#ifdef __cplusplus
}
#endif

#endif
