#include "recode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

static void require(enum rc_status status, const struct rc_error *error)
{
    if (status != RC_OK) {
        fprintf(stderr, "%s at %zu (errno %d)\n", rc_status_string(status),
                error->offset, error->system_errno);
        exit(1);
    }
}

static uint64_t arithmetic_result(uint64_t x) { return x + 32; }
static uint64_t conditional_result(uint64_t x) { return x == 0 ? 9 : 7; }
static uint64_t loop_result(uint64_t x) { (void)x; return 0; }
static uint64_t paths_result(uint64_t x) { return x == 0 || x == 1 ? 7 : 9; }

static void benchmark(const char *name, const uint8_t *bytes, size_t length,
                        rc_function reference)
{
    const unsigned compilations = 1000;
    const uint64_t calls = UINT64_C(10000000);
    struct rc_code code[2] = {{0}};
    struct rc_error error;
    uint64_t cycle = 0;
    for (uint64_t i = 0; i < 32; ++i)
        cycle += reference(i);
    uint64_t expected = cycle * (calls / 32);
    for (unsigned mode = 0; mode < 2; ++mode) {
        double compilation_seconds = 0;
        for (unsigned i = 0; i < compilations; ++i) {
            struct rc_code temporary = {0};
            double start = now();
            enum rc_status status = rc_compile(bytes, length, mode != 0, &temporary, &error);
            compilation_seconds += now() - start;
            require(status, &error);
            require(rc_code_free(&temporary, &error), &error);
        }
        require(rc_compile(bytes, length, mode != 0, &code[mode], &error), &error);
        for (uint64_t i = 0; i < 32; ++i) {
            if (code[mode].function(i) != reference(i)) {
                fputs("benchmark result mismatch\n", stderr);
                exit(1);
            }
        }
        printf("%s %s: compile %.3f us/function (%u compilations, excludes release); %zu emitted bytes\n",
               name, mode != 0 ? "optimized" : "unoptimized", compilation_seconds * 1e6 / compilations,
               compilations, code[mode].size);
    }
    for (unsigned round = 0; round < 5; ++round) {
        for (unsigned order = 0; order < 2; ++order) {
            unsigned mode = (round + order) % 2;
            uint64_t checksum = 0;
            double start = now();
            for (uint64_t i = 0; i < calls; ++i)
                checksum += code[mode].function(i & 31);
            double seconds = now() - start;
            if (checksum != expected) {
                fputs("benchmark checksum mismatch\n", stderr);
                exit(1);
            }
            printf("%s round %u %s: %.3f ns/call; checksum=%" PRIu64 "\n", name, round + 1,
                   mode != 0 ? "optimized" : "unoptimized", seconds * 1e9 / (double)calls, checksum);
        }
    }
    for (unsigned mode = 0; mode < 2; ++mode)
        require(rc_code_free(&code[mode], &error), &error);
}

int main(void)
{
    uint8_t bytes[3 + 64 * 6 + 1] = {0x48,0x89,0xf8};
    size_t length = 3;
    for (unsigned i = 0; i < 64; ++i) {
        bytes[length++] = 0x48;
        bytes[length++] = (i & 1) == 0 ? 0x05 : 0x2d;
        bytes[length++] = (i & 1) == 0 ? 7 : 6;
        for (unsigned j = 0; j < 3; ++j)
            bytes[length++] = 0;
    }
    bytes[length++] = 0xc3;
    const uint8_t conditional[] = {0x48,0x89,0xf8,0x48,0x85,0xc0,0x74,6,
        0xb8,7,0,0,0,0xc3,0xb8,9,0,0,0,0xc3};
    const uint8_t loop[] = {0xb8,32,0,0,0,0x48,0x83,0xe8,1,0x75,0xfa,0xc3};
    const uint8_t paths[] = {0x48,0x89,0xf8,0x48,0x3d,0,0,0,0,0x74,9,
        0x48,0x81,0xf8,1,0,0,0,0x75,6,0xb8,7,0,0,0,0xc3,0xb8,9,0,0,0,0xc3};
    const uint8_t flags[] = {0x48,0x89,0xf8,0x48,0x85,0xc0,
        0x48,0x83,0xc0,1,0x48,0x83,0xe8,1,0xb8,77,0,0,0,0x74,6,
        0xb8,7,0,0,0,0xc3,0xb8,9,0,0,0,0xc3};
    benchmark("arithmetic", bytes, length, arithmetic_result);
    benchmark("conditional", conditional, sizeof(conditional), conditional_result);
    benchmark("loop32", loop, sizeof(loop), loop_result);
    benchmark("paths", paths, sizeof(paths), paths_result);
    benchmark("flags", flags, sizeof(flags), conditional_result);
    return 0;
}
