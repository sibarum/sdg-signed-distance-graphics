/*
 * fast: the library's renderer, with its speed-ups, held to tests/reference.c, the plain transcription of SdgRender,
 * byte for byte: every drawing of ../conformance/drawings fitted at sizes from 17 to 1024 pixels and at three zooms
 * (into a corner, the middle, and past an edge), on one thread and on many. Conformance holds the transcription to
 * SdgRender itself, at the sizes SdgRender was run at; this holds the fast renderer to it everywhere else.
 */
#include "sdg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int reference_render(const sdg_drawing *d, int width, int height, double origin_x, double origin_y,
                     double units_per_pixel, sdg_role_fn roles, void *role_context, uint8_t *rgba);

static const char *DRAWINGS[] = {"background-icon", "badge", "file-icon", "group-clip", "group-mask", "group-nested",
                                 "group-opacity", "mainframe", "rasterware", "traced-badge", "vexelray-logo"};

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

/* A theme that answers every role, so a paint naming one is drawn in a colour the paint does not carry. */
static int theme(void *context, const char *role, uint32_t *rgba) {
    (void) context;
    *rgba = 0x20A0E0C0u ^ (uint32_t) strlen(role);
    return 1;
}

static int check(const char *name, const sdg_drawing *d, int w, int h, double ox, double oy, double upp, int themed) {
    size_t n = (size_t) w * h * 4;
    uint8_t *want = malloc(n), *got = malloc(n);
    sdg_role_fn roles = themed ? theme : NULL;
    reference_render(d, w, h, ox, oy, upp, roles, NULL, want);
    int bad = 0;
    for (int threads = 1; threads <= 7 && !bad; threads += 6) {
        memset(got, 0xA5, n);
        sdg_render_threaded(d, w, h, ox, oy, upp, roles, NULL, threads, got);
        for (size_t o = 0; o < n; o++) {
            if (got[o] != want[o]) {
                printf("FAIL %s %dx%d at %g,%g by %g, %d thread(s)%s: pixel (%d, %d) channel %d is %d, not %d\n",
                       name, w, h, ox, oy, upp, threads, themed ? ", themed" : "", (int) (o / 4 % w),
                       (int) (o / 4 / w), (int) (o % 4), got[o], want[o]);
                bad = 1;
                break;
            }
        }
    }
    free(want);
    free(got);
    return bad;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../conformance";
    static const int SIDES[] = {17, 64, 100, 256, 333, 1024};
    int failed = 0, run = 0;
    for (size_t k = 0; k < sizeof DRAWINGS / sizeof *DRAWINGS; k++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/drawings/%s.sdgb", dir, DRAWINGS[k]);
        size_t n;
        uint8_t *bytes = slurp(path, &n);
        sdg_drawing d;
        char err[256] = "cannot read it";
        if (!bytes || sdg_read(bytes, n, &d, err, sizeof err)) {
            printf("FAIL %s: %s\n", path, err);
            free(bytes);
            failed++;
            continue;
        }
        free(bytes);
        double cw = d.width, ch = d.height;
        for (size_t s = 0; s < sizeof SIDES / sizeof *SIDES; s++) {
            int side = SIDES[s];
            double upp = cw / side > ch / side ? cw / side : ch / side;
            failed += check(DRAWINGS[k], &d, side, side, (cw - side * upp) / 2, (ch - side * upp) / 2, upp, 0);
            run++;
        }
        /* not square, so rows and columns differ */
        failed += check(DRAWINGS[k], &d, 300, 211, -cw * 0.1, 0, cw / 240, 0);
        /* zooms: a corner, the middle, past the right edge */
        failed += check(DRAWINGS[k], &d, 256, 256, 0, 0, cw / 2048, 0);
        failed += check(DRAWINGS[k], &d, 512, 384, cw * 0.4, ch * 0.4, cw / 4000, 0);
        failed += check(DRAWINGS[k], &d, 256, 256, cw * 0.8, ch * 0.3, cw / 800, 0);
        /* a theme for every role */
        failed += check(DRAWINGS[k], &d, 128, 128, 0, 0, cw / 128, 1);
        run += 5;
        sdg_free(&d);
    }
    printf("%d of %d cases the same as the reference, byte for byte\n", run - failed, run);
    return failed ? 1 : 0;
}
