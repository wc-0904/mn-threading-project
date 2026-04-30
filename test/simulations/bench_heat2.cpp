#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "fiber.h"

/* ---- configuration ---- */
#define GRID_W        512
#define GRID_H        512
#define NUM_TIMESTEPS 400
#define NUM_STRIPS     64
#define LIGHT_PASSES    1
#define HEAVY_PASSES    4
#define FRAME_INTERVAL 10   // 40 frames total → smoother GIF

/* ---- double-buffered grid ---- */
static float grid_a[GRID_H][GRID_W];
static float grid_b[GRID_H][GRID_W];

/* ---- strip descriptor ---- */
typedef struct {
    float (*src)[GRID_W];
    float (*dst)[GRID_W];
    int row_start;
    int row_end;
    int passes;
} strip_args_t;

static strip_args_t strip_args[NUM_STRIPS];

/* ---- stencil ---- */
static void compute_strip(void *arg) {
    strip_args_t *s = (strip_args_t *)arg;
    float (*src)[GRID_W] = s->src;
    float (*dst)[GRID_W] = s->dst;

    for (int r = s->row_start; r < s->row_end; r++) {
        for (int c = 0; c < GRID_W; c++) {
            if (r == 0 || r == GRID_H - 1 || c == 0 || c == GRID_W - 1) {
                dst[r][c] = src[r][c];
                continue;
            }
            float lap = src[r-1][c] + src[r+1][c]
                      + src[r][c-1] + src[r][c+1]
                      - 4.0f * src[r][c];
            dst[r][c] = src[r][c] + 0.24f * lap;
        }
    }

    volatile float sink = 0.0f;
    for (int pass = 1; pass < s->passes; pass++)
        for (int r = s->row_start; r < s->row_end; r++)
            for (int c = 0; c < GRID_W; c++)
                sink += dst[r][c];
}

/* ---- coordinator ---- */
typedef struct {
    int   num_timesteps;
    float (*frame_buf)[GRID_H][GRID_W];
    int   num_frames;
} coord_args_t;

static void coordinator(void *arg) {
    coord_args_t *ca = (coord_args_t *)arg;
    int frame_idx = 0;

    for (int t = 0; t < ca->num_timesteps; t++) {
        float (*src)[GRID_W] = (t % 2 == 0) ? grid_a : grid_b;
        float (*dst)[GRID_W] = (t % 2 == 0) ? grid_b : grid_a;

        for (int s = 0; s < NUM_STRIPS; s++) {
            strip_args[s].src = src;
            strip_args[s].dst = dst;
        }

        counter_t *c = create_counter(NUM_STRIPS);
        for (int s = 0; s < NUM_STRIPS; s++)
            spawn_with_counter(compute_strip, &strip_args[s], c);
        wait_for_counter(c, 0);
        free_counter(c);

        if (ca->frame_buf && t % FRAME_INTERVAL == 0 && frame_idx < ca->num_frames) {
            memcpy(ca->frame_buf[frame_idx], dst,
                   sizeof(float) * GRID_H * GRID_W);
            frame_idx++;
        }
    }
}

/* ---- grid initialization ---- */
// Places a smooth Gaussian hot spot at (cy, cx) with given sigma and peak temp.
static void place_spot(float grid[GRID_H][GRID_W],
                       int cy, int cx, float sigma, float peak) {
    int rad = (int)(3.5f * sigma);
    for (int r = cy - rad; r <= cy + rad; r++) {
        for (int c = cx - rad; c <= cx + rad; c++) {
            if (r < 0 || r >= GRID_H || c < 0 || c >= GRID_W) continue;
            float dr = r - cy, dc = c - cx;
            float v = peak * expf(-(dr*dr + dc*dc) / (2.f * sigma * sigma));
            if (v > grid[r][c]) grid[r][c] = v;
        }
    }
}

static void init_grid() {
    memset(grid_a, 0, sizeof(grid_a));
    int cy = GRID_H / 2, cx = GRID_W / 2;

    // 1. hot center
    place_spot(grid_a, cy, cx, 22.f, 1.0f);

    // 2. inner ring: 6 spots at radius 110, temperature 0.85
    for (int i = 0; i < 6; i++) {
        float angle = i * (float)M_PI / 3.f;
        int ry = cy + (int)(110.f * sinf(angle));
        int rx = cx + (int)(110.f * cosf(angle));
        place_spot(grid_a, ry, rx, 18.f, 0.85f);
    }

    // 3. outer ring: 12 spots at radius 210, temperature 0.65, rotated 15°
    for (int i = 0; i < 12; i++) {
        float angle = i * (float)M_PI / 6.f + (float)M_PI / 12.f;
        int ry = cy + (int)(210.f * sinf(angle));
        int rx = cx + (int)(210.f * cosf(angle));
        place_spot(grid_a, ry, rx, 13.f, 0.65f);
    }
}

/* ---- strip layout ---- */
static void setup_strips(bool uniform) {
    int rows_per_strip = GRID_H / NUM_STRIPS;
    int heavy_lo = NUM_STRIPS / 3;
    int heavy_hi = 2 * NUM_STRIPS / 3;

    for (int s = 0; s < NUM_STRIPS; s++) {
        strip_args[s].row_start = s * rows_per_strip;
        strip_args[s].row_end   = (s == NUM_STRIPS - 1) ? GRID_H
                                                         : (s + 1) * rows_per_strip;
        strip_args[s].passes    = uniform ? LIGHT_PASSES
                                          : (s >= heavy_lo && s < heavy_hi)
                                                ? HEAVY_PASSES : LIGHT_PASSES;
    }
}

/* ---- fire colormap ---- */
// black -> deep purple -> red -> orange -> bright yellow
static void temp_to_rgb(float t, int *r, int *g, int *b) {
    t = fmaxf(0.f, fminf(1.f, t));
    typedef struct { float t; int r, g, b; } stop_t;
    static const stop_t stops[] = {
        {0.00f,   0,   0,   0},
        {0.20f,  80,   0, 100},
        {0.45f, 220,  20,   0},
        {0.70f, 255, 140,   0},
        {1.00f, 255, 255, 180},
    };
    for (int i = 0; i < 4; i++) {
        if (t <= stops[i+1].t) {
            float s = (t - stops[i].t) / (stops[i+1].t - stops[i].t);
            *r = (int)(stops[i].r + s * (stops[i+1].r - stops[i].r));
            *g = (int)(stops[i].g + s * (stops[i+1].g - stops[i].g));
            *b = (int)(stops[i].b + s * (stops[i+1].b - stops[i].b));
            return;
        }
    }
    *r = 255; *g = 255; *b = 180;
}

/* ---- PPM output — main thread only ---- */
static void save_ppm(int frame_idx, float grid[GRID_H][GRID_W]) {
    char fname[64];
    snprintf(fname, sizeof(fname), "heat2_frame_%04d.ppm", frame_idx * FRAME_INTERVAL);
    FILE *f = fopen(fname, "w");
    if (!f) { perror(fname); return; }
    fprintf(f, "P3\n%d %d\n255\n", GRID_W, GRID_H);
    for (int row = 0; row < GRID_H; row++) {
        for (int col = 0; col < GRID_W; col++) {
            int rv, gv, bv;
            temp_to_rgb(grid[row][col], &rv, &gv, &bv);
            fprintf(f, "%d %d %d ", rv, gv, bv);
        }
        fputc('\n', f);
    }
    fclose(f);
}

/* ---- timing ---- */
static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ---- benchmark driver ---- */
static double baseline_ms = 0.0;

static void run(int n_workers, bool uniform, bool steal,
                float (*frame_buf)[GRID_H][GRID_W], int num_frames) {
    init_grid();
    setup_strips(uniform);
    stealing_enabled = steal;
    reset_scheduler_stats();
    scheduler_init(n_workers);

    coord_args_t ca = { NUM_TIMESTEPS, frame_buf, num_frames };

    long long t0 = now_ns();
    spawn(coordinator, &ca);
    scheduler_run(n_workers);
    long long t1 = now_ns();

    double ms = (t1 - t0) / 1e6;
    if (baseline_ms == 0.0) baseline_ms = ms;

    printf("workers=%-3d load=%-9s stealing=%-5s  time=%7.1f ms"
           "  speedup=%4.2fx  steals=%-6ld  attempts=%-7ld",
           n_workers,
           uniform ? "uniform" : "variable",
           steal ? "on" : "off",
           ms,
           baseline_ms / ms,
           steal_successes.load(),
           steal_attempts.load());
    if (steal_attempts.load() > 0)
        printf("  success=%.1f%%", 100.0 * steal_successes / steal_attempts);
    printf("\n");
}

int main(int argc, char **argv) {
    bool save_frames = (argc > 1 && strcmp(argv[1], "--save") == 0);

    printf("=== Heat Diffusion Benchmark 2 ===\n");
    printf("grid=%dx%d  timesteps=%d  strips=%d  "
           "light_passes=%d  heavy_passes=%d\n",
           GRID_W, GRID_H, NUM_TIMESTEPS, NUM_STRIPS, LIGHT_PASSES, HEAVY_PASSES);
    printf("init: 1 center + 6-spot inner ring + 12-spot outer ring (Gaussian profiles)\n\n");

    int worker_counts[] = { 1, 2, 4, 8 };
    int nw = (int)(sizeof(worker_counts) / sizeof(worker_counts[0]));

    // baseline_ms set on first call (N=1, uniform, stealing on)
    printf("-- Uniform (no load imbalance) --\n");
    for (int i = 0; i < nw; i++)
        run(worker_counts[i], true, true, nullptr, 0);
    printf("\n");

    baseline_ms = 0.0;  // reset so variable baseline is also N=1
    printf("-- Variable (middle 1/3 strips: %dx heavier) --\n", HEAVY_PASSES);
    for (int i = 0; i < nw; i++)
        run(worker_counts[i], false, true, nullptr, 0);
    printf("\n");

    baseline_ms = 0.0;
    printf("-- Variable, stealing disabled --\n");
    for (int i = 0; i < nw; i++)
        run(worker_counts[i], false, false, nullptr, 0);
    printf("\n");

    if (save_frames) {
        int num_frames = NUM_TIMESTEPS / FRAME_INTERVAL;
        float (*frame_buf)[GRID_H][GRID_W] =
            (float (*)[GRID_H][GRID_W]) malloc(
                sizeof(float) * num_frames * GRID_H * GRID_W);

        baseline_ms = 0.0;
        printf("-- Capturing %d frames (8 workers, variable load) --\n", num_frames);
        run(8, false, true, frame_buf, num_frames);

        printf("Writing heat2_frame_*.ppm ...\n");
        for (int i = 0; i < num_frames; i++)
            save_ppm(i, frame_buf[i]);
        printf("Done.\n");

        free(frame_buf);
    }

    return 0;
}
