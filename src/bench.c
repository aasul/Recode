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

int main(void)
{
    uint8_t bytes[3 + 64 * 6 + 1] = {0x48,0x89,0xf8};
    size_t length = 3;
    /* 32 pairs add 7/sub 6, giving x + 32. */
    for (unsigned i = 0; i < 64; ++i) {
        bytes[length++] = 0x48;
        bytes[length++] = (i & 1) == 0 ? 0x05 : 0x2d;
        bytes[length++] = (i & 1) == 0 ? 7 : 6;
        for (unsigned j = 0; j < 3; ++j)
            bytes[length++] = 0;
    }
    bytes[length++] = 0xc3;
    const unsigned compilations = 1000;
    const uint64_t calls = UINT64_C(10000000);
    struct rc_code code[2] = {{0}};
    struct rc_error error;
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
        printf("%s: compile %.3f us/function (%u compilations, excludes release); %zu emitted bytes\n",
               mode != 0 ? "optimized" : "unoptimized", compilation_seconds * 1e6 / compilations,
               compilations, code[mode].size);
    }
    for (unsigned round = 0; round < 5; ++round) {
        for (unsigned order = 0; order < 2; ++order) {
            unsigned mode = (round + order) % 2;
            uint64_t checksum = 0;
            double start = now();
            for (uint64_t i = 0; i < calls; ++i)
                checksum += code[mode].function(i);
            double seconds = now() - start;
            uint64_t expected = calls * (calls - 1) / 2 + calls * 32;
            if (checksum != expected) {
                fputs("benchmark checksum mismatch\n", stderr);
                return 1;
            }
            printf("round %u %s: %.3f ns/call; checksum=%" PRIu64 "\n", round + 1,
                   mode != 0 ? "optimized" : "unoptimized", seconds * 1e9 / (double)calls, checksum);
        }
    }
    for (unsigned mode = 0; mode < 2; ++mode)
        require(rc_code_free(&code[mode], &error), &error);
    return 0;
}
