/*
 * conformance: every case of ../conformance/cases.txt drawn by this library and held against the picture the
 * reference (vexelray-fields' SdgRender) drew of it. A line is
 *
 *     drawing.sdgb width height originX originY unitsPerPixel expected.png
 *
 * paths relative to the conformance folder. A case passes when no channel of any pixel is more than TOLERANCE off;
 * the run prints each case's worst and fails if any case does.
 */
#include "sdg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOLERANCE 1

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
    const char *dir = argc > 1 ? argv[1] : "../conformance";
    char path[1024];
    snprintf(path, sizeof path, "%s/cases.txt", dir);
    FILE *cases = fopen(path, "r");
    if (!cases) {
        fprintf(stderr, "conformance: no %s\n", path);
        return 2;
    }
    char line[1024], drawing[256], expected[256];
    int failed = 0, run = 0;
    while (fgets(line, sizeof line, cases)) {
        int w, h;
        double ox, oy, upp;
        if (line[0] == '#' || sscanf(line, "%255s %d %d %lf %lf %lf %255s", drawing, &w, &h, &ox, &oy, &upp,
                                     expected) != 7) {
            continue;
        }
        run++;
        size_t n;
        snprintf(path, sizeof path, "%s/%s", dir, drawing);
        uint8_t *bytes = slurp(path, &n);
        sdg_drawing d;
        char err[256] = "cannot read it";
        if (!bytes || sdg_read(bytes, n, &d, err, sizeof err)) {
            printf("FAIL %s: %s\n", drawing, err);
            free(bytes);
            failed++;
            continue;
        }
        free(bytes);
        uint8_t *got = malloc((size_t) w * h * 4);
        sdg_render(&d, w, h, ox, oy, upp, NULL, NULL, got);
        sdg_free(&d);
        snprintf(path, sizeof path, "%s/%s", dir, expected);
        uint8_t *png = slurp(path, &n), *want = NULL;
        int ww, wh;
        if (!png || sdg_png_decode(png, n, &ww, &wh, &want) || ww != w || wh != h) {
            printf("FAIL %s: cannot read %s\n", drawing, expected);
            free(png);
            free(got);
            failed++;
            continue;
        }
        free(png);
        int worst = 0;
        long off = 0;
        for (size_t o = 0; o < (size_t) w * h * 4; o++) {
            int diff = abs(got[o] - want[o]);
            /* a transparent pixel has no colour to compare */
            if (o % 4 != 3 && want[o - o % 4 + 3] == 0 && got[o - o % 4 + 3] == 0) {
                diff = 0;
            }
            worst = diff > worst ? diff : worst;
            off += diff > TOLERANCE;
        }
        printf("%s %-28s %4d x %-4d worst %d, %ld channels past %d\n", worst <= TOLERANCE ? "ok  " : "FAIL", expected,
               w, h, worst, off, TOLERANCE);
        failed += worst > TOLERANCE;
        free(got);
        free(want);
    }
    fclose(cases);
    printf("%d of %d cases %s\n", run - failed, run, failed ? "pass" : "pass: every one");
    return failed ? 1 : 0;
}
