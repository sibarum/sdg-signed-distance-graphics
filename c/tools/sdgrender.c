/*
 * sdgrender: draws a compiled SDG drawing to a PNG.
 *
 *     sdgrender drawing.sdgb out.png 512              the canvas fitted into 512 x 512
 *     sdgrender drawing.sdgb out.png 512 384          into 512 x 384
 *     sdgrender drawing.sdgb out.png 512 512 10 10 0.05   pixel (i, j) at canvas (10 + (i + .5) * .05, ...)
 */
#include "sdg.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = len > 0 ? malloc((size_t) len) : NULL;
    if (b && fread(b, 1, (size_t) len, f) != (size_t) len) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *n = (size_t) len;
    return b;
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5 && argc != 8) {
        fprintf(stderr, "usage: sdgrender drawing.sdgb out.png width [height [originX originY unitsPerPixel]]\n");
        return 2;
    }
    size_t n;
    uint8_t *bytes = slurp(argv[1], &n);
    if (!bytes) {
        fprintf(stderr, "sdgrender: cannot read %s\n", argv[1]);
        return 1;
    }
    sdg_drawing d;
    char err[256];
    if (sdg_read(bytes, n, &d, err, sizeof err)) {
        fprintf(stderr, "sdgrender: %s: %s\n", argv[1], err);
        free(bytes);
        return 1;
    }
    free(bytes);
    int w = atoi(argv[3]), h = argc >= 5 ? atoi(argv[4]) : w;
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768) {
        fprintf(stderr, "sdgrender: a picture %d x %d\n", w, h);
        sdg_free(&d);
        return 2;
    }
    uint8_t *rgba = malloc((size_t) w * h * 4);
    int rc = !rgba ? -1 : argc == 8
            ? sdg_render(&d, w, h, atof(argv[5]), atof(argv[6]), atof(argv[7]), NULL, NULL, rgba)
            : sdg_render_fit(&d, w, h, rgba);
    sdg_free(&d);
    uint8_t *png;
    size_t len;
    if (rc || sdg_png_encode(rgba, w, h, &png, &len)) {
        fprintf(stderr, "sdgrender: out of memory\n");
        free(rgba);
        return 1;
    }
    free(rgba);
    FILE *f = fopen(argv[2], "wb");
    if (!f || fwrite(png, 1, len, f) != len) {
        fprintf(stderr, "sdgrender: cannot write %s\n", argv[2]);
        free(png);
        return 1;
    }
    fclose(f);
    free(png);
    return 0;
}
