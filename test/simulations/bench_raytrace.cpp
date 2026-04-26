#include <stdio.h>
#include <math.h>
#include <time.h>
#include "fiber.h"
#include <initializer_list>

/* ---- image dimensions ---- */
#define WIDTH      1024
#define HEIGHT     1024
#define MAX_DEPTH   15
#define AA_SAMPLES   4

/* ---- vector math ---- */
typedef struct { float x, y, z; } vec3;

static vec3  vadd(vec3 a, vec3 b)     { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static vec3  vsub(vec3 a, vec3 b)     { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static vec3  vscale(vec3 a, float t)  { return {a.x*t,   a.y*t,   a.z*t};   }
static float vdot(vec3 a, vec3 b)     { return a.x*b.x + a.y*b.y + a.z*b.z; }
static vec3  vnorm(vec3 a)            { float l=sqrtf(vdot(a,a)); return vscale(a,1.0f/l); }

/* ---- scene ---- */
typedef struct {
    vec3  center;
    float radius;
    vec3  color;
    float reflectivity;
} sphere_t;

static sphere_t scene[] = {
    {{ 0.0f,  -1.0f,  3.0f}, 1.0f, {1.0f, 0.2f, 0.2f}, 0.5f},
    {{ 2.0f,   0.0f,  4.0f}, 1.0f, {0.2f, 0.2f, 1.0f}, 0.6f},
    {{-2.0f,   0.0f,  4.0f}, 1.0f, {0.2f, 1.0f, 0.2f}, 0.4f},
    {{ 0.0f,   1.0f,  6.0f}, 1.0f, {1.0f, 0.8f, 0.0f}, 0.5f},
    {{ 1.0f,   0.5f,  2.5f}, 0.5f, {1.0f, 1.0f, 1.0f}, 0.7f},
    {{-1.0f,   0.5f,  2.5f}, 0.5f, {0.8f, 0.3f, 0.8f}, 0.7f},
    {{ 0.0f, -5001.0f, 0.0f}, 5000.0f, {0.8f,0.8f,0.8f}, 0.3f},
};
#define NUM_SPHERES 7
static vec3 light_pos  = { 5.0f,  5.0f, -3.0f};
static vec3 light_pos2 = {-3.0f,  3.0f,  1.0f};

/* ---- output buffer ---- */
static unsigned char image[HEIGHT][WIDTH][3];

/* ---- ray-sphere intersection ---- */
static float intersect(vec3 orig, vec3 dir, sphere_t *s) {
    vec3  oc   = vsub(orig, s->center);
    float a    = vdot(dir, dir);
    float b    = 2.0f * vdot(oc, dir);
    float c    = vdot(oc, oc) - s->radius * s->radius;
    float disc = b*b - 4*a*c;
    if (disc < 0) return -1.0f;
    float t = (-b - sqrtf(disc)) / (2*a);
    return t > 0.001f ? t : -1.0f;
}

/* ---- trace one ray ---- */
static vec3 trace(vec3 orig, vec3 dir, int depth) {
    vec3 bg = {0.1f, 0.1f, 0.2f};
    if (depth <= 0) return bg;

    float    t_min = 1e9f;
    sphere_t *hit  = nullptr;
    for (int i = 0; i < NUM_SPHERES; i++) {
        float t = intersect(orig, dir, &scene[i]);
        if (t > 0 && t < t_min) { t_min = t; hit = &scene[i]; }
    }
    if (!hit) return bg;

    vec3  p      = vadd(orig, vscale(dir, t_min));
    vec3  normal = vnorm(vsub(p, hit->center));

    // two lights
    float diffuse = 0.0f;
    vec3  lights[] = {light_pos, light_pos2};
    for (auto &lp : lights) {
        vec3  to_light  = vnorm(vsub(lp, p));
        float in_shadow = 0.0f;
        for (int i = 0; i < NUM_SPHERES; i++)
            if (intersect(p, to_light, &scene[i]) > 0) { in_shadow = 1.0f; break; }
        diffuse += fmaxf(0.0f, vdot(normal, to_light)) * (1.0f - in_shadow) * 1.0f;
    }

    vec3 color = vscale(hit->color, diffuse + 0.3f);  // more ambient

    // reflection
    if (hit->reflectivity > 0.0f) {
        vec3 refl = vsub(dir, vscale(normal, 2.0f * vdot(dir, normal)));
        vec3 rc   = trace(p, refl, depth - 1);
        color = vadd(vscale(color, 1.0f - hit->reflectivity),
                     vscale(rc,    hit->reflectivity));
    }

    return color;
}

/* ---- tile fiber ---- */
typedef struct {
    int tile_x, tile_y, tile_size;
} tile_arg_t;

static tile_arg_t tile_args[WIDTH * HEIGHT];  // enough for 1x1 tiles

void render_tile(void *arg) {
    tile_arg_t *t  = (tile_arg_t*)arg;
    int         ts = t->tile_size;
    for (int y = t->tile_y; y < t->tile_y + ts && y < HEIGHT; y++) {
        for (int x = t->tile_x; x < t->tile_x + ts && x < WIDTH; x++) {
            vec3 accum = {0, 0, 0};
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float u = (x + (sx + 0.5f) * 0.25f - WIDTH  / 2.0f) / (float)HEIGHT;
                    float v = (y + (sy + 0.5f) * 0.25f - HEIGHT / 2.0f) / (float)HEIGHT;
                    vec3 orig = {0.0f, 0.0f, -5.0f};
                    vec3 dir  = vnorm({u, -v, 1.0f});
                    vec3 c    = trace(orig, dir, MAX_DEPTH);
                    accum = vadd(accum, c);
                }
            }
            accum = vscale(accum, 1.0f / 16.0f);
            image[y][x][0] = (unsigned char)(fminf(accum.x, 1.0f) * 255);
            image[y][x][1] = (unsigned char)(fminf(accum.y, 1.0f) * 255);
            image[y][x][2] = (unsigned char)(fminf(accum.z, 1.0f) * 255);
        }
    }
}

/* ---- spawner fiber ---- */
typedef struct { int tile_size; } spawner_arg_t;
static spawner_arg_t spawner_arg;

void render_spawner(void *arg) {
    int ts  = ((spawner_arg_t*)arg)->tile_size;
    int idx = 0;
    for (int y = 0; y < HEIGHT; y += ts)
        for (int x = 0; x < WIDTH;  x += ts) {
            tile_args[idx] = {x, y, ts};
            spawn(render_tile, &tile_args[idx]);
            idx++;
        }
}

/* ---- write PPM ---- */
static void write_ppm(const char *filename) {
    FILE *f = fopen(filename, "wb");
    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int y = 0; y < HEIGHT; y++)
        fwrite(image[y], 1, WIDTH * 3, f);
    fclose(f);
    printf("  image saved: %s\n", filename);
}

/* ---- timing ---- */
static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ---- single benchmark run ---- */
static double run(int n_workers, int tile_size, bool save) {
    spawner_arg.tile_size = tile_size;
    int tiles = ((WIDTH+tile_size-1)/tile_size) * ((HEIGHT+tile_size-1)/tile_size);

    reset_scheduler_stats();
    scheduler_init(n_workers);

    long long t0 = now_ns();
    spawn(render_spawner, &spawner_arg);
    scheduler_run(n_workers);
    long long t1 = now_ns();

    // ---- add this block ----
    int local[NUM_WORKERS]={}, stolen[NUM_WORKERS]={};
    long long cycles[NUM_WORKERS]={};
    get_per_worker_stats(local, stolen, cycles);
    long long max_cycles = 0, min_cycles = (long long)1e18;
    for (int i = 0; i < n_workers; i++) {
        if (cycles[i] > max_cycles) max_cycles = cycles[i];
        if (cycles[i] > 0 && cycles[i] < min_cycles) min_cycles = cycles[i];
    }
    double imbalance = min_cycles > 0 ? (double)max_cycles / min_cycles : 0.0;
    // ---- end block ----

    double ms = (t1 - t0) / 1e6;
    printf("workers=%-2d  tile=%3dx%-3d  tiles=%-6d  time=%7.1f ms  "
           "steals=%-6ld  success=%.1f%%  imbalance=%.2fx\n",
           n_workers, tile_size, tile_size, tiles, ms,
           steal_successes.load(),
           steal_attempts.load() > 0
               ? 100.0 * steal_successes.load() / steal_attempts.load()
               : 0.0,
           imbalance);

    if (save) write_ppm("render.ppm");
    return ms;
}

int main() {
    printf("=== Parallel Raytracer ===\n");
    printf("%dx%d image  %d spheres  max_depth=%d\n\n",
           WIDTH, HEIGHT, NUM_SPHERES, MAX_DEPTH);

    printf("--- Iteration 1: tile size sweep (workers=8) ---\n");
    printf("Finding optimal task granularity...\n");
    int    best_tile = 1;
    double best_ms   = 1e9;
    int tile_sizes[] = {1, 2, 4, 8, 16};   // stop at 16
    for (int ti = 0; ti < 5; ti++) {
        double ms = run(8, tile_sizes[ti], false);
        if (ms < best_ms) { best_ms = ms; best_tile = tile_sizes[ti]; }
    }
    printf("  -> optimal tile size: %dx%d (%.1f ms)\n\n", best_tile, best_tile, best_ms);

    printf("--- Iteration 2: worker scaling (tile=%dx%d) ---\n", best_tile, best_tile);
    double t1_ms = 0;
    int worker_counts[] = {1, 2, 4, 8};
    for (int wi = 0; wi < 4; wi++) {
        double ms = run(worker_counts[wi], best_tile, false);
        if (worker_counts[wi] == 1) t1_ms = ms;
        printf("  -> speedup: %.2fx\n", t1_ms / ms);
    }
    printf("\n");

    printf("--- Final render (workers=8, tile=%dx%d) ---\n", best_tile, best_tile);
    run(8, best_tile, true);

    return 0;
}