#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include "fiber.h"

/* ---- configuration ---- */
#define NUM_NODES  1000000   // graph size
#define DEGREE          16   // outgoing edges per node (fixed, random targets)
#define CHUNK_SIZE    2048   // frontier nodes per fiber; sized so MAX_CHUNKS < MAX_FIBERS
#define SOURCE           0   // BFS start node
#define MAX_LEVELS      64   // safety cap on BFS depth

#define MAX_CHUNKS  ((NUM_NODES + CHUNK_SIZE - 1) / CHUNK_SIZE)

/* ---- graph (CSR, heap-allocated in gen_graph) ---- */
static int *adj;        // [NUM_NODES * DEGREE] — neighbor lists packed flat
static int *adj_start;  // [NUM_NODES + 1]     — adj_start[u]..adj_start[u+1]

/* ---- BFS state ---- */
// visited[] is atomic so concurrent fibers can CAS-mark nodes without a lock.
static std::atomic<int> visited[NUM_NODES];

static int *frontier_a;               // double-buffered frontier arrays
static int *frontier_b;
static int *g_current;                // points at active frontier
static int *g_next;                   // points at next-level staging buffer
static int  g_frontier_size;
static std::atomic<int> g_next_size;  // concurrent append index into g_next

/* ---- per-level stats (written by coordinator, read by main after run) ---- */
static int  g_num_levels;
static int  g_total_visited;
static int  level_frontier[MAX_LEVELS];   // frontier size entering each level
static int  level_chunks[MAX_LEVELS];     // fibers spawned for each level
static long level_steals[MAX_LEVELS];     // steals that occurred during each level

/* ---- fiber args pool ---- */
typedef struct {
    int *nodes;
    int  count;
} bfs_chunk_t;

// MAX_CHUNKS bfs_chunk_t at most per level; reused across levels since
// wait_for_counter guarantees all fibers are done before coordinator continues.
static bfs_chunk_t chunk_pool[MAX_CHUNKS];

/* ---- BFS worker fiber ---- */
// Processes a contiguous slice of the current frontier. For each node u in
// the slice, scans u's neighbors. Marks each unvisited neighbor with a CAS
// and claims a slot in g_next via atomic fetch_add.
static void bfs_worker(void *arg) {
    bfs_chunk_t *ch  = (bfs_chunk_t *)arg;
    int         *nxt = g_next;

    for (int i = 0; i < ch->count; i++) {
        int u = ch->nodes[i];
        for (int j = adj_start[u]; j < adj_start[u + 1]; j++) {
            int v = adj[j];
            int expected = 0;
            if (visited[v].compare_exchange_strong(
                    expected, 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                int slot = g_next_size.fetch_add(1, std::memory_order_relaxed);
                nxt[slot] = v;
            }
        }
    }
}

/* ---- coordinator fiber ---- */
// Drives the BFS level loop. Each level: partitions the current frontier into
// chunks, spawns one bfs_worker per chunk with a counter, blocks on
// wait_for_counter (barrier), then swaps frontiers and advances.
// Per-level stats go into global arrays; printf is done by main after run.
typedef struct {
    bool record_stats;
} coord_args_t;

static void bfs_coordinator(void *arg) {
    coord_args_t *ca = (coord_args_t *)arg;

    for (int lv = 0; lv < MAX_LEVELS && g_frontier_size > 0; lv++) {
        int fsz       = g_frontier_size;
        int nchunks   = (fsz + CHUNK_SIZE - 1) / CHUNK_SIZE;
        long pre_steals = steal_successes.load(std::memory_order_relaxed);

        if (ca->record_stats) {
            level_frontier[lv] = fsz;
            level_chunks[lv]   = nchunks;
        }

        g_next_size.store(0, std::memory_order_relaxed);

        for (int i = 0; i < nchunks; i++) {
            chunk_pool[i].nodes = g_current + i * CHUNK_SIZE;
            chunk_pool[i].count = (i == nchunks - 1)
                                      ? (fsz - i * CHUNK_SIZE) : CHUNK_SIZE;
        }

        counter_t *c = create_counter(nchunks);
        for (int i = 0; i < nchunks; i++)
            spawn_with_counter(bfs_worker, &chunk_pool[i], c);
        wait_for_counter(c, 0);
        free_counter(c);

        if (ca->record_stats)
            level_steals[lv] = steal_successes.load(std::memory_order_relaxed)
                                - pre_steals;

        int *tmp   = g_current;
        g_current  = g_next;
        g_next     = tmp;
        g_frontier_size = g_next_size.load(std::memory_order_relaxed);
        g_num_levels = lv + 1;
    }

    // count visited nodes (O(N) scan, safe inside a fiber — no stdio, no alloc)
    int total = 0;
    for (int i = 0; i < NUM_NODES; i++)
        total += visited[i].load(std::memory_order_relaxed);
    g_total_visited = total;
}

/* ---- graph generation ---- */
static void gen_graph() {
    srand(42);
    adj       = new int[NUM_NODES * DEGREE];
    adj_start = new int[NUM_NODES + 1];
    frontier_a = new int[NUM_NODES];
    frontier_b = new int[NUM_NODES];

    for (int u = 0; u < NUM_NODES; u++) {
        adj_start[u] = u * DEGREE;
        for (int d = 0; d < DEGREE; d++)
            adj[u * DEGREE + d] = rand() % NUM_NODES;
    }
    adj_start[NUM_NODES] = NUM_NODES * DEGREE;
}

/* ---- BFS reset between runs ---- */
static void reset_bfs() {
    for (int i = 0; i < NUM_NODES; i++)
        visited[i].store(0, std::memory_order_relaxed);
    visited[SOURCE].store(1, std::memory_order_relaxed);
    g_current       = frontier_a;
    g_next          = frontier_b;
    g_current[0]    = SOURCE;
    g_frontier_size = 1;
    g_next_size.store(0, std::memory_order_relaxed);
    g_num_levels    = 0;
    g_total_visited = 0;
}

/* ---- sequential BFS baseline ---- */
static int  seq_visited;
static int  seq_levels;

static void run_sequential() {
    bool *vis = new bool[NUM_NODES]();
    int  *q   = new int[NUM_NODES];
    int   head = 0, tail = 0, lv = 0;

    vis[SOURCE] = true;
    q[tail++]   = SOURCE;

    while (head < tail) {
        int end = tail;
        while (head < end) {
            int u = q[head++];
            for (int j = adj_start[u]; j < adj_start[u + 1]; j++) {
                int v = adj[j];
                if (!vis[v]) { vis[v] = true; q[tail++] = v; }
            }
        }
        lv++;
    }
    seq_visited = tail;
    seq_levels  = lv;
    delete[] vis;
    delete[] q;
}

/* ---- timing ---- */
static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ---- benchmark driver ---- */
static double seq_ms = 0.0;  // set after sequential run; used for speedup

static void run(int n_workers, bool steal, bool record_stats) {
    reset_bfs();
    stealing_enabled = steal;
    reset_scheduler_stats();
    scheduler_init(n_workers);

    coord_args_t ca = { record_stats };

    long long t0 = now_ns();
    spawn(bfs_coordinator, &ca);
    scheduler_run(n_workers);
    long long t1 = now_ns();

    double ms = (t1 - t0) / 1e6;
    printf("workers=%-3d stealing=%-5s  time=%7.2f ms  speedup=%4.2fx"
           "  steals=%-5ld  attempts=%-6ld",
           n_workers, steal ? "on" : "off",
           ms, seq_ms / ms,
           steal_successes.load(), steal_attempts.load());
    if (steal_attempts.load() > 0)
        printf("  success=%.1f%%", 100.0 * steal_successes / steal_attempts);
    printf("\n");
}

int main() {
    printf("=== Parallel BFS Benchmark ===\n");
    printf("nodes=%d  degree=%d  chunk_size=%d  source=%d\n\n",
           NUM_NODES, DEGREE, CHUNK_SIZE, SOURCE);

    gen_graph();

    // sequential baseline
    long long t0 = now_ns();
    run_sequential();
    long long t1 = now_ns();
    seq_ms = (t1 - t0) / 1e6;
    printf("SEQUENTIAL   time=%7.2f ms  visited=%-6d  levels=%d\n\n",
           seq_ms, seq_visited, seq_levels);

    int worker_counts[] = { 1, 2, 4, 8 };
    int nw = (int)(sizeof(worker_counts) / sizeof(worker_counts[0]));

    // -- run N=1 first so we can print per-level stats --
    printf("-- Parallel BFS (stealing on) --\n");
    run(1, true, /*record_stats=*/true);

    // per-level breakdown (printed from main — not from inside a fiber)
    printf("  per-level frontier breakdown:\n");
    printf("  %5s  %8s  %6s  %6s\n", "level", "frontier", "chunks", "steals");
    for (int l = 0; l < g_num_levels; l++)
        printf("  %5d  %8d  %6d  %6ld\n",
               l, level_frontier[l], level_chunks[l], level_steals[l]);
    printf("\n");

    for (int i = 1; i < nw; i++)
        run(worker_counts[i], true, false);
    printf("\n");

    // correctness check
    if (g_total_visited == seq_visited)
        printf("  PASS: parallel visited count matches sequential (%d)\n\n", seq_visited);
    else
        printf("  FAIL: parallel=%d  sequential=%d\n\n", g_total_visited, seq_visited);

    // -- stealing disabled --
    printf("-- Parallel BFS (stealing disabled) --\n");
    for (int i = 0; i < nw; i++)
        run(worker_counts[i], false, false);
    printf("\n");

    return 0;
}
