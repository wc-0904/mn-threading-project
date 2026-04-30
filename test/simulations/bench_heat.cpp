#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "fiber.h"

/* ---- configuration ---- */
#define GRID_W        512
#define GRID_H        512
#define NUM_TIMESTEPS 200
#define NUM_STRIPS     64   // horizontal strips; each is one fiber per timestep
#define LIGHT_PASSES    1   // outer two-thirds of strips
#define HEAVY_PASSES    4   // middle third, simulates higher thermal complexity
#define FRAME_INTERVAL 20   // capture every Nth timestep for visualization

/* ---- double-buffered grid ---- */
static float grid_a[GRID_H][GRID_W];
static float grid_b[GRID_H][GRID_W];

/* ---- strip descriptor ---- */
typedef struct {
    float (*src)[GRID_W];   // set by coordinator before each spawn
    float (*dst)[GRID_W];
    int row_start;          // inclusive
    int row_end;            // exclusive
    int passes;             // 1=light, 4=heavy (creates load imbalance)
} strip_args_t;

static strip_args_t strip_args[NUM_STRIPS];

/* ---- stencil ---- */
static void compute_strip(void *arg) {
    strip_args_t *s = (strip_args_t *)arg;
    float (*src)[GRID_W] = s->src;
    float (*dst)[GRID_W] = s->dst;

    // primary Jacobi diffusion step: src -> dst
    for (int r = s->row_start; r < s->row_end; r++) {
        for (int c = 0; c < GRID_W; c++) {
            if (r == 0 || r == GRID_H - 1 || c == 0 || c == GRID_W - 1) {
                dst[r][c] = src[r][c];  // Dirichlet boundary: fixed temperature
                continue;
            }
            float lap = src[r-1][c] + src[r+1][c]
                      + src[r][c-1] + src[r][c+1]
                      - 4.0f * src[r][c];
            dst[r][c] = src[r][c] + 0.24f * lap;
        }
    }

    // extra read passes for heavy strips — creates genuine load imbalance.
    // volatile prevents the compiler from eliminating the loop.
    volatile float sink = 0.0f;
    for (int pass = 1; pass < s->passes; pass++)
        for (int r = s->row_start; r < s->row_end; r++)
            for (int c = 0; c < GRID_W; c++)
                sink += dst[r][c];
}

/* ---- coordinator fiber ---- */
// Each timestep: set strip src/dst, spawn all strips with a counter,
// block on wait_for_counter (barrier), optionally snapshot the grid.
// File I/O is NOT done here — the fiber stack is only 64KB. Instead,
// snapshots are memcpy'd into frame_buf and the main thread writes PPMs.

typedef struct {
    int    num_timesteps;
    float  (*frame_buf)[GRID_H][GRID_W];  // null = no capture; heap-allocated by main
    int    num_frames;
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

        // snapshot grid into frame buffer (memcpy is safe inside a fiber)
        if (ca->frame_buf && t % FRAME_INTERVAL == 0 && frame_idx < ca->num_frames) {
            memcpy(ca->frame_buf[frame_idx], dst,
                   sizeof(float) * GRID_H * GRID_W);
            frame_idx++;
        }
    }
}

/* ---- grid initialization ---- */
static void init_grid() {
    memset(grid_a, 0, sizeof(grid_a));
    int spots[4][2] = {
        { GRID_H / 4,     GRID_W / 4     },
        { GRID_H / 4,     3 * GRID_W / 4 },
        { 3 * GRID_H / 4, GRID_W / 2     },
        { GRID_H / 2,     GRID_W / 2     },
    };
    for (int i = 0; i < 4; i++)
        for (int dr = -5; dr <= 5; dr++)
            for (int dc = -5; dc <= 5; dc++)
                grid_a[spots[i][0] + dr][spots[i][1] + dc] = 1.0f;
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
        if (uniform)
            strip_args[s].passes = LIGHT_PASSES;
        else
            strip_args[s].passes = (s >= heavy_lo && s < heavy_hi)
                                       ? HEAVY_PASSES : LIGHT_PASSES;
    }
}

/* ---- PPM output — called from main thread only, never from a fiber ---- */
static void temp_to_rgb(float t, int *r, int *g, int *b) {
    t = fmaxf(0.0f, fminf(1.0f, t));
    // blue -> white -> red
    if (t < 0.5f) {
        float s = t / 0.5f;
        *r = (int)(255 * s); *g = (int)(255 * s); *b = 255;
    } else {
        float s = (t - 0.5f) / 0.5f;
        *r = 255; *g = (int)(255 * (1.0f - s)); *b = (int)(255 * (1.0f - s));
    }
}

static void save_ppm(int frame_idx, float grid[GRID_H][GRID_W]) {
    char fname[64];
    snprintf(fname, sizeof(fname), "frame_%04d.ppm", frame_idx * FRAME_INTERVAL);
    FILE *f = fopen(fname, "w");
    if (!f) { perror(fname); return; }
    fprintf(f, "P3\n%d %d\n255\n", GRID_W, GRID_H);
    for (int r = 0; r < GRID_H; r++) {
        for (int c = 0; c < GRID_W; c++) {
            int rv, gv, bv;
            temp_to_rgb(grid[r][c], &rv, &gv, &bv);
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
    fprintf(stderr, "workers=%-3d load=%-9s stealing=%-5s  time=%7.1f ms"
           "  steals=%ld  attempts=%ld",
           n_workers,
           uniform ? "uniform" : "variable",
           steal   ? "on"      : "off",
           ms,
           steal_successes.load(),
           steal_attempts.load());
    if (steal_attempts.load() > 0)
        fprintf(stderr, "  success=%.1f%%", 100.0 * steal_successes / steal_attempts);
    fprintf(stderr, "\n");
}

int main(int argc, char **argv) {
    bool save_frames = (argc > 1 && strcmp(argv[1], "--save") == 0);

    fprintf(stderr, "=== Heat Diffusion Benchmark ===\n");
    fprintf(stderr, "grid=%dx%d  timesteps=%d  strips=%d  "
           "light_passes=%d  heavy_passes=%d\n\n",
           GRID_W, GRID_H, NUM_TIMESTEPS, NUM_STRIPS, LIGHT_PASSES, HEAVY_PASSES);

    int worker_counts[] = { 1, 2, 4, 8 };
    int nw = (int)(sizeof(worker_counts) / sizeof(worker_counts[0]));

    fprintf(stderr, "-- Uniform (no load imbalance) --\n");
    for (int i = 0; i < nw; i++)
        run(worker_counts[i], true, true, nullptr, 0);
    fprintf(stderr, "\n");

    fprintf(stderr, "-- Variable (middle 1/3 of strips: %dx heavier) --\n", HEAVY_PASSES);
    for (int i = 0; i < nw; i++)
        run(worker_counts[i], false, true, nullptr, 0);
    fprintf(stderr, "\n");

    fprintf(stderr, "-- Variable, stealing disabled --\n");
    for (int i = 0; i < nw; i++)
        run(worker_counts[i], false, false, nullptr, 0);
    fprintf(stderr, "\n");

    if (save_frames) {
        int num_frames = NUM_TIMESTEPS / FRAME_INTERVAL;
        // heap-allocate so 10MB doesn't go on the stack
        float (*frame_buf)[GRID_H][GRID_W] =
            (float (*)[GRID_H][GRID_W]) malloc(sizeof(float) * num_frames * GRID_H * GRID_W);

        fprintf(stderr, "-- Capturing %d frames (8 workers, variable load) --\n", num_frames);
        run(8, false, true, frame_buf, num_frames);

        fprintf(stderr, "Writing PPM files...\n");
        for (int i = 0; i < num_frames; i++) {
            save_ppm(i, frame_buf[i]);
            fprintf(stderr, "  wrote frame_%04d.ppm\n", i * FRAME_INTERVAL);
        }
        fprintf(stderr, "\nStitch with:\n");
        fprintf(stderr, "  convert -delay 8 -loop 0 frame_*.ppm heat.gif\n");
        fprintf(stderr, "  explorer.exe heat.gif\n");

        free(frame_buf);
    }

    return 0;
}
