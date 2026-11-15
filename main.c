/* Benchmark: parse one file many times, report the fastest run.
 * Usage: ./usfm [file] [iterations] */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <x86intrin.h>

#define USFM_IMPLEMENTATION
#include "usfm.h"

typedef struct {
    long cycles;
    struct timespec time;
} Timer;

static void getTime(Timer *timer)
{
    timer->cycles = (long)__rdtsc();
    if (clock_gettime(CLOCK_MONOTONIC, &timer->time) != 0)
    {
        exit(1);
    }
}

static void printTimeData(Timer start, Timer end, size_t size)
{
    long cycles = end.cycles - start.cycles;
    long nanoseconds = ((end.time.tv_sec - start.time.tv_sec) * (long)1e9) +
        (end.time.tv_nsec - start.time.tv_nsec);
    printf("%zu bytes, %ld cycles, %ld ns\n", size, cycles, nanoseconds);
    printf("%.2f cycles/byte, %.2f bytes/cycle, %.2f ns/byte, %.1f MiB/s\n",
        (double)cycles / (double)size, (double)size / (double)cycles,
        (double)nanoseconds / (double)size,
        ((double)size / (1024.0 * 1024.0)) / ((double)nanoseconds / 1e9));
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "build/corpus/WEB-STRONGS/25-JEReng-web.usfm";
    unsigned iterations = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 100u;
    FILE *f = fopen(path, "rb");
    if (f == NULL)
    {
        printf("Error: cannot open %s (run ./build.sh test once to unpack the corpus)\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *input = malloc(size > 0 ? (size_t)size : 1);
    if (input == NULL || (size > 0 && fread(input, 1, (size_t)size, f) != (size_t)size))
    {
        printf("Error: cannot read %s\n", path);
        return 1;
    }
    fclose(f);

    Timer best_start = {0}, best_end = {0};
    long best = -1;
    uint32_t nodes = 0, verses = 0, diagnostics = 0;
    for (unsigned i = 0; i < iterations; i++)
    {
        Timer start, end;
        USFM_Err err;
        getTime(&start);
        USFM_Ast *ast = USFM_Ast_Parse(input, (uint32_t)size, NULL, &err);
        getTime(&end);
        if (ast == NULL)
        {
            printf("Error: parse failed: %s\n", err.msg);
            return 1;
        }
        USFM_Ast_GetNodes(ast, &nodes);
        USFM_Ast_GetVerses(ast, &verses);
        USFM_Ast_GetDiagnostics(ast, &diagnostics);
        USFM_Ast_Free(ast);
        long cycles = end.cycles - start.cycles;
        if (best < 0 || cycles < best)
        {
            best = cycles;
            best_start = start;
            best_end = end;
        }
    }
    printf("%s: %u nodes, %u verses, %u diagnostics\n", path, nodes, verses, diagnostics);
    printf("fastest of %u parses (parse + tree build, excluding free):\n", iterations);
    printTimeData(best_start, best_end, (size_t)size);
    free(input);
    return 0;
}
