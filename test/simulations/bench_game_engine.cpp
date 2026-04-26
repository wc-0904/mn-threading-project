#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <atomic>
#include "fiber.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                       */
/* ------------------------------------------------------------------ */
#define NUM_OBJECTS      100
#define NUM_FRAMES       200
#define HEAVY_FRAC       10
#define LIGHT_ANIM    50000L
#define HEAVY_ANIM   500000L
#define LIGHT_REND    25000L
#define HEAVY_REND   250000L

/* ------------------------------------------------------------------ */
/*  Job args                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    int object_id;
    int complexity;
} job_arg_t;

static job_arg_t anim_args[NUM_OBJECTS];
static job_arg_t rend_args[NUM_OBJECTS];

/* ------------------------------------------------------------------ */
/*  Per-frame state                                                     */
/* ------------------------------------------------------------------ */
static std::atomic<int> w_anim[NUM_WORKERS];
static std::atomic<int> w_rend[NUM_WORKERS];
static std::atomic<int> obj_anim_worker[NUM_OBJECTS];  // which worker ran each obj
static std::atomic<int> obj_rend_worker[NUM_OBJECTS];
static std::atomic<int> anim_done{0};
static std::atomic<int> rend_done{0};
static std::atomic<int> frame_steals{0};

static void reset_frame_stats() {
    for (int i = 0; i < NUM_WORKERS; i++) {
        w_anim[i].store(0);
        w_rend[i].store(0);
    }
    for (int i = 0; i < NUM_OBJECTS; i++) {
        obj_anim_worker[i].store(-1);
        obj_rend_worker[i].store(-1);
    }
    anim_done.store(0);
    rend_done.store(0);
    frame_steals.store(0);
}

/* ------------------------------------------------------------------ */
/*  Job functions                                                       */
/* ------------------------------------------------------------------ */
void animate_job(void *arg) {
    job_arg_t *j = (job_arg_t*)arg;
    long iters = j->complexity ? HEAVY_ANIM : LIGHT_ANIM;
    volatile long sum = 0;
    for (long i = 0; i < iters; i++) sum += i;
    int wid = get_worker_id();
    obj_anim_worker[j->object_id].store(wid);
    anim_done.fetch_add(1);
    if (wid >= 0 && wid < NUM_WORKERS) w_anim[wid].fetch_add(1);
}

void render_job(void *arg) {
    job_arg_t *j = (job_arg_t*)arg;
    long iters = j->complexity ? HEAVY_REND : LIGHT_REND;
    volatile long sum = 0;
    for (long i = 0; i < iters; i++) sum += i;
    int wid = get_worker_id();
    obj_rend_worker[j->object_id].store(wid);
    rend_done.fetch_add(1);
    if (wid >= 0 && wid < NUM_WORKERS) w_rend[wid].fetch_add(1);
}

/* ------------------------------------------------------------------ */
/*  Frame fiber                                                         */
/* ------------------------------------------------------------------ */
typedef struct { int n_workers; } frame_arg_t;
static frame_arg_t frame_arg;

void frame_fiber(void *arg) {
    counter_t *anim_c = create_counter(NUM_OBJECTS);
    for (int i = 0; i < NUM_OBJECTS; i++)
        spawn_with_counter(animate_job, &anim_args[i], anim_c);
    wait_for_counter(anim_c, 0);
    free_counter(anim_c);

    counter_t *rend_c = create_counter(NUM_OBJECTS);
    for (int i = 0; i < NUM_OBJECTS; i++)
        spawn_with_counter(render_job, &rend_args[i], rend_c);
    wait_for_counter(rend_c, 0);
    free_counter(rend_c);
}

/* ------------------------------------------------------------------ */
/*  ANSI colors — one per worker                                        */
/* ------------------------------------------------------------------ */
static const char* wcolors[] = {
    "\033[41m",   // W0 red
    "\033[42m",   // W1 green
    "\033[43m",   // W2 yellow
    "\033[44m",   // W3 blue
    "\033[45m",   // W4 magenta
    "\033[46m",   // W5 cyan
    "\033[100m",  // W6 dark gray
    "\033[103m",  // W7 bright yellow
};
#define RST   "\033[0m"
#define BOLD  "\033[1m"
#define GRN   "\033[32m"
#define YLW   "\033[33m"
#define CYN   "\033[36m"

/* ------------------------------------------------------------------ */
/*  Grid visualization                                                  */
/* ------------------------------------------------------------------ */
static void print_grid(int frame, double fps, double speedup,
                        long total_steals, double single_ms, double cur_ms) {
    printf("\033[2J\033[H");  // clear + home

    printf(BOLD "=== M:N Fiber Scheduler — Naughty Dog Game Engine ===" RST "\n");
    printf("Frame %3d/%d  |  " GRN "%.0f fps" RST "  |  "
           BOLD "Speedup: %.2fx" RST "  |  Steals: %ld\n\n",
           frame, NUM_FRAMES, fps, speedup, total_steals);

    // worker legend
    printf("Workers: ");
    for (int i = 0; i < NUM_WORKERS; i++)
        printf("%s W%d " RST " ", wcolors[i], i);
    printf("  \033[90m░ = pending  " BOLD "H" RST "\033[90m = heavy job\033[0m\n\n");

    // animate grid
    printf(BOLD "PHASE 1: ANIMATE" RST
           "  (%d/%d done)\n", anim_done.load(), NUM_OBJECTS);
    for (int row = 0; row < 10; row++) {
        printf("  ");
        for (int col = 0; col < 10; col++) {
            int   obj   = row * 10 + col;
            int   wid   = obj_anim_worker[obj].load();
            bool  heavy = (obj % HEAVY_FRAC == 0);
            const char *label = heavy ? "H" : "\xC2\xB7";  // H or ·
            if (wid >= 0 && wid < NUM_WORKERS)
                printf("%s %s " RST, wcolors[wid], label);
            else
                printf("\033[90m %s " RST, label);
        }
        printf("\n");
    }

    printf("\n");

    // render grid
    printf(BOLD "PHASE 2: RENDER" RST
           "   (%d/%d done)\n", rend_done.load(), NUM_OBJECTS);
    for (int row = 0; row < 10; row++) {
        printf("  ");
        for (int col = 0; col < 10; col++) {
            int   obj   = row * 10 + col;
            int   wid   = obj_rend_worker[obj].load();
            bool  heavy = (obj % HEAVY_FRAC == 0);
            const char *label = heavy ? "H" : "\xC2\xB7";
            if (wid >= 0 && wid < NUM_WORKERS)
                printf("%s %s " RST, wcolors[wid], label);
            else
                printf("\033[90m %s " RST, label);
        }
        printf("\n");
    }

    printf("\n");

    // per-worker bars
    printf(BOLD "Per-worker jobs (animate / render):\n" RST);
    int amax = 1, rmax = 1;
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (w_anim[i].load() > amax) amax = w_anim[i].load();
        if (w_rend[i].load() > rmax) rmax = w_rend[i].load();
    }
    for (int i = 0; i < NUM_WORKERS; i++) {
        int va = w_anim[i].load();
        int vr = w_rend[i].load();
        printf("  %sW%d" RST " [", wcolors[i], i);
        // animate bar
        int fa = va * 15 / amax;
        printf(GRN);
        for (int b = 0; b < fa;  b++) printf("\xe2\x96\x88");
        printf(RST);
        for (int b = fa; b < 15; b++) printf("\xe2\x96\x91");
        printf("] %2d  [", va);
        // render bar
        int fr = vr * 15 / rmax;
        printf(CYN);
        for (int b = 0; b < fr;  b++) printf("\xe2\x96\x88");
        printf(RST);
        for (int b = fr; b < 15; b++) printf("\xe2\x96\x91");
        printf("] %2d\n", vr);
    }

    printf("\n");
    printf("Single-threaded: %.1f ms/frame  |  "
           "Current: %.1f ms/frame  |  "
           BOLD "%.2fx speedup\n" RST,
           single_ms, cur_ms, single_ms / cur_ms);
    printf("10%% of objects are heavy (%dx work) — "
           "watch H cells change color as workers steal them\n",
           (int)(HEAVY_ANIM / LIGHT_ANIM));

    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  Timing                                                              */
/* ------------------------------------------------------------------ */
static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  Run benchmark                                                       */
/* ------------------------------------------------------------------ */
static double run_frames(int n_workers, bool visualize, double single_ms) {
    long long total_time = 0;
    long      total_steals = 0;

    for (int f = 0; f < NUM_FRAMES; f++) {
        reset_frame_stats();
        reset_scheduler_stats();
        scheduler_init(n_workers);

        long long t0 = now_ns();
        spawn(frame_fiber, &frame_arg);
        scheduler_run(n_workers);
        long long t1 = now_ns();

        double ms   = (t1 - t0) / 1e6;
        total_time += (t1 - t0);
        total_steals += steal_successes.load();
        frame_steals.store(steal_successes.load());

        if (visualize) {
            double avg_ms = total_time / 1e6 / (f + 1);
            print_grid(f + 1, 1000.0 / avg_ms, single_ms / avg_ms,
                       total_steals, single_ms, avg_ms);
            usleep(150000);  // 150ms per frame so it's visible
        }
    }

    return total_time / 1e6 / NUM_FRAMES;
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */
int main() {
    for (int i = 0; i < NUM_OBJECTS; i++) {
        int heavy = (i % HEAVY_FRAC == 0) ? 1 : 0;
        anim_args[i] = {i, heavy};
        rend_args[i] = {i, heavy};
    }

    printf("=== Game Engine Frame Loop Benchmark ===\n");
    printf("%d objects  |  %d heavy (%dx work)  |  %d frames\n\n",
           NUM_OBJECTS, NUM_OBJECTS / HEAVY_FRAC,
           (int)(HEAVY_ANIM / LIGHT_ANIM), NUM_FRAMES);

    // --- baseline: N=1, no stealing ---
    printf("Measuring single-threaded baseline...\n");
    stealing_enabled = false;
    double single_ms = run_frames(1, false, 1.0);
    printf("Baseline: %.1f ms/frame  (%.0f fps)\n\n", single_ms, 1000.0/single_ms);

    // --- stealing OFF ---
    printf("--- Without work stealing ---\n");
    stealing_enabled = false;
    int wc[] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) {
        double ms = run_frames(wc[i], false, single_ms);
        printf("N=%-2d  %6.1f ms/frame  %5.0f fps  speedup=%.2fx\n",
               wc[i], ms, 1000.0/ms, single_ms/ms);
    }
    printf("\n");

    // --- stealing ON ---
    printf("--- With work stealing ---\n");
    stealing_enabled = true;
    for (int i = 0; i < 4; i++) {
        double ms = run_frames(wc[i], false, single_ms);
        printf("N=%-2d  %6.1f ms/frame  %5.0f fps  speedup=%.2fx\n",
               wc[i], ms, 1000.0/ms, single_ms/ms);
    }
    printf("\n");

    // --- live visualization ---
    printf("--- Live Demo (N=8 with stealing) ---\n");
    printf("Watch H cells change color as workers steal heavy jobs...\n");
    sleep(2);
    stealing_enabled = true;
    run_frames(8, true, single_ms);

    return 0;
}