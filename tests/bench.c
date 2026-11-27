/* Whole-corpus benchmark. Usage: bench [-n passes] DIR...
 * Reads every .usfm / .sfm file of each DIR into memory, then times
 * whole-directory passes of USFM_Ast_Parse + USFM_Ast_Free and reports
 * the fastest pass (1 warmup pass first). On Linux it also reports
 * user-space instructions, cycles and L1D read misses for that pass
 * (perf_event_open; shown as n/a when unavailable). Pin the process to
 * one CPU (taskset) for stable numbers on hybrid CPUs. */
#define _GNU_SOURCE 1
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdint.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define USFM_IMPLEMENTATION
#include "../usfm.h"

#define BENCH_MAX_FILES 1024
#define BENCH_COUNTERS 3

typedef struct Counters {
    int      fd[BENCH_COUNTERS];
    uint64_t value[BENCH_COUNTERS];
} Counters;

static void counters_open(Counters *c)
{
    int k;
    for (k = 0; k < BENCH_COUNTERS; k++) c->fd[k] = -1;
#if defined(__linux__)
    for (k = 0; k < BENCH_COUNTERS; k++) {
        struct perf_event_attr attr;
        memset(&attr, 0, sizeof attr);
        attr.size = sizeof attr;
        attr.disabled = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.read_format = PERF_FORMAT_TOTAL_TIME_RUNNING;
        if (k == 0) { attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_INSTRUCTIONS; }
        if (k == 1) { attr.type = PERF_TYPE_HARDWARE; attr.config = PERF_COUNT_HW_CPU_CYCLES; }
        if (k == 2) {
            attr.type = PERF_TYPE_HW_CACHE;
            attr.config = PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                          (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        }
        c->fd[k] = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
    }
#endif
}

static void counters_start(Counters *c)
{
    int k;
    for (k = 0; k < BENCH_COUNTERS; k++) {
        c->value[k] = 0;
#if defined(__linux__)
        if (c->fd[k] >= 0) {
            ioctl(c->fd[k], PERF_EVENT_IOC_RESET, 0);
            ioctl(c->fd[k], PERF_EVENT_IOC_ENABLE, 0);
        }
#endif
    }
}

static void counters_stop(Counters *c)
{
    int k;
    for (k = 0; k < BENCH_COUNTERS; k++) {
#if defined(__linux__)
        if (c->fd[k] >= 0) {
            ioctl(c->fd[k], PERF_EVENT_IOC_DISABLE, 0);
            /* value, then the time the event actually ran. On hybrid CPUs
             * a generic event only counts on one core type; if it never
             * ran (e.g. pinned to an E-core), report it as unavailable. */
            uint64_t buf[2];
            if (read(c->fd[k], buf, sizeof buf) != (ssize_t)sizeof buf || buf[1] == 0) {
                c->fd[k] = -1;
            } else {
                c->value[k] = buf[0];
            }
        }
#endif
    }
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static int is_usfm_name(const char *name)
{
    size_t l = strlen(name);
    return (l > 5 && strcasecmp(name + l - 5, ".usfm") == 0) ||
           (l > 4 && strcasecmp(name + l - 4, ".sfm") == 0);
}

static void bench_dir(const char *dir, int passes)
{
    static char *data[BENCH_MAX_FILES];
    static uint32_t len[BENCH_MAX_FILES];
    size_t n = 0, bytes = 0, i;
    double best = 1e300;
    uint64_t best_counters[BENCH_COUNTERS] = { 0, 0, 0 };
    Counters c;
    DIR *d = opendir(dir);
    struct dirent *e;
    int pass, k;
    if (d == NULL) {
        printf("%s: cannot open\n", dir);
        return;
    }
    while ((e = readdir(d)) != NULL && n < BENCH_MAX_FILES) {
        char path[4096];
        FILE *f;
        long size;
        if (!is_usfm_name(e->d_name)) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        f = fopen(path, "rb");
        if (f == NULL) continue;
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data[n] = (char *)malloc(size > 0 ? (size_t)size : 1u);
        if (data[n] == NULL || (size > 0 && fread(data[n], 1, (size_t)size, f) != (size_t)size)) {
            fclose(f);
            continue;
        }
        fclose(f);
        len[n] = (uint32_t)size;
        bytes += (size_t)size;
        n++;
    }
    closedir(d);
    counters_open(&c);
    for (pass = -1; pass < passes; pass++) {        /* pass -1 is the warmup */
        double t0, ms;
        counters_start(&c);
        t0 = now_ms();
        for (i = 0; i < n; i++) {
            USFM_Err err;
            USFM_Ast_Free(USFM_Ast_Parse(data[i], len[i], NULL, &err));
        }
        ms = now_ms() - t0;
        counters_stop(&c);
        if (pass >= 0 && ms < best) {
            best = ms;
            for (k = 0; k < BENCH_COUNTERS; k++) best_counters[k] = c.value[k];
        }
    }
    printf("%-26s %-12s %4zu files %8.2f MiB  %9.2f ms  %7.1f MiB/s", dir, USFM__SIMD_PATH, n,
           (double)bytes / 1048576.0, best, (double)bytes / 1048576.0 / (best / 1e3));
    if (c.fd[0] >= 0) printf("  %8.1fM instr", (double)best_counters[0] / 1e6); else printf("  instr n/a");
    if (c.fd[1] >= 0) printf("  %8.1fM cycles", (double)best_counters[1] / 1e6); else printf("  cycles n/a");
    if (c.fd[2] >= 0) printf("  %7.1fk L1D miss", (double)best_counters[2] / 1e3); else printf("  L1D n/a");
    printf("\n");
    for (i = 0; i < n; i++) free(data[i]);
}

int main(int argc, char **argv)
{
    int passes = 5, a = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        passes = atoi(argv[2]);
        a = 3;
    }
    if (a >= argc) {
        fprintf(stderr, "usage: %s [-n passes] DIR...\n", argv[0]);
        return 1;
    }
    for (; a < argc; a++) bench_dir(argv[a], passes > 0 ? passes : 1);
    return 0;
}
