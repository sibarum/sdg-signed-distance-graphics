/*
 * sdgbench: how long the library takes to draw a compiled drawing, fitted into a square, at each size given: the
 * median of the runs (after warm-up runs), on one thread and on the threads asked for.
 *
 *     sdgbench drawing.sdgb 64 256 1024             one thread, then one a processor
 *     sdgbench -t 4 -n 30 drawing.sdgb 1024         one thread, then four; 30 runs each
 *
 * A quick look while working on the rasterizer; the published comparison is vexelray-gui-format-bench's (JMH).
 */
#include "sdg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static double now_ms(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int by_value(const void *a, const void *b) {
    double x = *(const double *) a, y = *(const double *) b;
    return x < y ? -1 : x > y;
}

static double median_ms(const sdg_drawing *d, int side, int threads, int runs, uint8_t *rgba) {
    double upp = d->width / side > d->height / side ? d->width / side : d->height / side;
    double ox = (d->width - side * upp) / 2, oy = (d->height - side * upp) / 2;
    double *t = malloc(sizeof(double) * runs);
    for (int r = -runs / 3 - 1; r < runs; r++) {
        double t0 = now_ms();
        sdg_render_threaded(d, side, side, ox, oy, upp, NULL, NULL, threads, rgba);
        if (r >= 0) {
            t[r] = now_ms() - t0;
        }
    }
    qsort(t, runs, sizeof(double), by_value);
    double m = t[runs / 2];
    free(t);
    return m;
}

int main(int argc, char **argv) {
    int threads = 0, runs = 15, a = 1;
    for (; a + 1 < argc && argv[a][0] == '-'; a += 2) {
        if (!strcmp(argv[a], "-t")) {
            threads = atoi(argv[a + 1]);
        } else if (!strcmp(argv[a], "-n")) {
            runs = atoi(argv[a + 1]);
        }
    }
    if (a + 1 >= argc || runs < 1) {
        fprintf(stderr, "usage: sdgbench [-t threads] [-n runs] drawing.sdgb size...\n");
        return 2;
    }
    size_t n;
    uint8_t *bytes = slurp(argv[a], &n);
    sdg_drawing d;
    char err[256] = "cannot read it";
    if (!bytes || sdg_read(bytes, n, &d, err, sizeof err)) {
        fprintf(stderr, "sdgbench: %s: %s\n", argv[a], err);
        return 1;
    }
    free(bytes);
    printf("%-10s %12s %12s\n", "size", "1 thread", threads > 0 ? "threads" : "all threads");
    for (int i = a + 1; i < argc; i++) {
        int side = atoi(argv[i]);
        uint8_t *rgba = malloc((size_t) side * side * 4);
        double one = median_ms(&d, side, 1, runs, rgba);
        double all = median_ms(&d, side, threads, runs, rgba);
        printf("%-10d %9.3f ms %9.3f ms\n", side, one, all);
        free(rgba);
    }
    sdg_free(&d);
    return 0;
}
