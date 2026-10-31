#define _POSIX_C_SOURCE 200809L
#include "recode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t) != 0)
        exit(1);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static void require(enum rc_status status)
{
    if (status != RC_OK) {
        fprintf(stderr,"benchmark: %s\n",rc_status_string(status));
        exit(1);
    }
}

/* An indirect volatile pointer prevents the caller loop from replacing calls
 * with a checksum formula, while allowing ordinary optimization of each body. */
static uint64_t identity(uint64_t x) { return x; }
static uint64_t arithmetic(uint64_t x) { return x + 32; }
static uint64_t loop32(uint64_t x) { (void)x; return 0; }
static const char *modes[] = {"direct-c","native-off","native-on","interpret","fixed","adaptive"};

static void workload(const char *name, const uint8_t *bytes, size_t length, rc_function expected)
{
    const uint64_t counts[] = {1,32,100000};
    /* Registration includes decode, validation, owned IR copy and handle setup. */
    struct rc_runtime_config config = rc_runtime_default_config();
    struct rc_runtime *r = NULL;
    require(rc_runtime_create(&config,&r,NULL));
    uint64_t registration = 0;
    for (unsigned i = 0; i < 200; ++i) {
        rc_handle h;
        uint64_t start = now();
        require(rc_runtime_register(r,bytes,length,&h,NULL));
        registration += now() - start;
        require(rc_runtime_remove(r,h,NULL));
    }
    require(rc_runtime_destroy(&r,NULL));
    printf("registration,%s,mean_ns,%.1f\n",name,(double)registration / 200);
    for (unsigned mode = 0; mode < 2; ++mode) {
        uint64_t compilation = 0;
        size_t emitted = 0;
        for (unsigned i = 0; i < 200; ++i) {
            struct rc_code code = {0};
            uint64_t start = now();
            require(rc_compile(bytes,length,mode != 0,&code,NULL));
            compilation += now() - start;
            emitted = code.size;
            require(rc_code_free(&code,NULL));
        }
        printf("compile,%s,%s,mean_ns,%.1f,emitted,%zu\n",name,modes[mode+1],(double)compilation / 200,emitted);
    }
    for (size_t n = 0; n < sizeof(counts)/sizeof(counts[0]); ++n) {
        uint64_t calls = counts[n], checksum_expected = 0;
        for (uint64_t i = 0; i < calls; ++i)
            checksum_expected += expected(i & 31);
        for (unsigned round = 0; round < 5; ++round) {
            for (unsigned order = 0; order < 6; ++order) {
                unsigned mode = (round + order) % 6;
                struct rc_code code = {0};
                rc_function volatile direct = expected;
                rc_handle h = 0;
                uint64_t total_start = now();
                if (mode == 1 || mode == 2)
                    require(rc_compile(bytes,length,mode == 2,&code,NULL));
                if (mode >= 3) {
                    config = rc_runtime_default_config();
                    config.policy = mode == 3 ? RC_POLICY_INTERPRET : (mode == 4 ? RC_POLICY_FIXED : RC_POLICY_ADAPTIVE);
                    require(rc_runtime_create(&config,&r,NULL));
                    require(rc_runtime_register(r,bytes,length,&h,NULL));
                }
                uint64_t start = now(), checksum = 0;
                for (uint64_t i = 0; i < calls; ++i) {
                    uint64_t value;
                    if (mode >= 3)
                        require(rc_runtime_call(r,h,i & 31,&value,NULL));
                    else
                        value = mode == 0 ? direct(i & 31) : code.function(i & 31);
                    checksum += value;
                }
                uint64_t end = now();
                if (checksum != checksum_expected) {
                    fputs("checksum mismatch\n",stderr); exit(1);
                }
                struct rc_function_stats fs = {0};
                struct rc_runtime_stats rs = {0};
                if (r != NULL) {
                    require(rc_runtime_function_stats(r,h,&fs,NULL));
                    require(rc_runtime_get_stats(r,&rs,NULL));
                }
                printf("run,%s,%s,calls,%" PRIu64 ",round,%u,ns_per_call,%.3f,total_us,%.3f,interpreted,%" PRIu64
                    ",jitted,%" PRIu64 ",compile_ns,%" PRIu64 ",hits,%" PRIu64 ",misses,%" PRIu64
                    ",evictions,%" PRIu64 ",peak_mapping,%zu,metadata,%zu,program,%zu,postponed,%" PRIu64 ",checksum,%" PRIu64 "\n",
                    name,modes[mode],calls,round+1,(double)(end-start)/(double)calls,(double)(end-total_start)/1000,
                    fs.interpreted,fs.jitted,fs.compilation_ns,rs.cache_hits,rs.cache_misses,rs.evictions,
                    rs.peak_executable_bytes,rs.metadata_bytes,rs.program_bytes,fs.policy_postponed,checksum);
                if (r != NULL)
                    require(rc_runtime_destroy(&r,NULL));
                require(rc_code_free(&code,NULL));
            }
        }
    }
    /* Time the transition call separately, with statistics queries outside timing. */
    config = rc_runtime_default_config(); config.policy = RC_POLICY_FIXED;
    require(rc_runtime_create(&config,&r,NULL));
    rc_handle h;
    require(rc_runtime_register(r,bytes,length,&h,NULL));
    for (uint64_t i = 0; i <= config.hot_threshold; ++i) {
        uint64_t value, start = now();
        require(rc_runtime_call(r,h,i & 31,&value,NULL));
        uint64_t duration = now() - start;
        if (value != expected(i & 31)) exit(1);
        if (i == 0 || i + 1 == config.hot_threshold || i == config.hot_threshold)
            printf("transition,%s,call,%" PRIu64 ",ns,%" PRIu64 "\n",name,i+1,duration);
    }
    require(rc_runtime_destroy(&r,NULL));
}

static void pressure(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    for (size_t mib = 4; mib <= 64; mib *= 4) {
        struct rc_runtime_config c = rc_runtime_default_config();
        c.policy = RC_POLICY_FIXED; c.hot_threshold = 2;
        c.executable_budget = mib * 1024 * 1024;
        size_t count = c.executable_budget / page + 32;
        rc_handle *handles = malloc(count * sizeof(*handles));
        if (handles == NULL) exit(1);
        struct rc_runtime *r = NULL;
        require(rc_runtime_create(&c,&r,NULL));
        uint64_t start = now(), registration_ns = 0, execution_ns = 0;
        for (size_t i = 0; i < count; ++i) {
            uint8_t bytes[] = {0xb8,0,0,0,0,0xc3};
            for (unsigned b = 0; b < 4; ++b)
                bytes[b+1] = (uint8_t)(i >> (8*b));
            uint64_t mark = now();
            require(rc_runtime_register(r,bytes,sizeof(bytes),&handles[i],NULL));
            registration_ns += now() - mark;
            mark = now();
            for (unsigned j = 0; j < 3; ++j) {
                uint64_t value;
                require(rc_runtime_call(r,handles[i],0,&value,NULL));
                if (value != i) exit(1);
            }
            execution_ns += now() - mark;
        }
        struct rc_runtime_stats s;
        require(rc_runtime_get_stats(r,&s,NULL));
        if (s.peak_executable_bytes > c.executable_budget || s.evictions != 32) exit(1);
        printf("pressure,budget_mib,%zu,functions,%zu,total_ms,%.3f,registration_ms,%.3f,execution_ms,%.3f,peak_mapping,%zu,metadata,%zu,program,%zu,hits,%" PRIu64 ",misses,%" PRIu64 ",evictions,%" PRIu64 "\n",
            mib,count,(double)(now()-start)/1e6,(double)registration_ns/1e6,(double)execution_ns/1e6,
            s.peak_executable_bytes,s.metadata_bytes,s.program_bytes,s.cache_hits,s.cache_misses,s.evictions);
        uint64_t mark = now();
        require(rc_runtime_clear(r,NULL));
        require(rc_runtime_get_stats(r,&s,NULL));
        if (s.executable_bytes != 0) exit(1);
        require(rc_runtime_destroy(&r,NULL));
        printf("cleanup,budget_mib,%zu,ms,%.3f,remaining_mapping,0\n",mib,(double)(now()-mark)/1e6);
        free(handles);
    }
}

int main(int argc, char **argv)
{
    printf("compiler,%s,page_size,%ld,clock,CLOCK_MONOTONIC,rounds,5\n",__VERSION__,sysconf(_SC_PAGESIZE));
    if (argc == 2 && strcmp(argv[1],"--pressure") == 0) {
        pressure(); return 0;
    }
    if (argc != 1) return 1;
    uint8_t bytes[388] = {0x48,0x89,0xf8};
    size_t length = 3;
    for (unsigned i = 0; i < 64; ++i) {
        bytes[length++] = 0x48; bytes[length++] = (i & 1) == 0 ? 0x05 : 0x2d;
        bytes[length++] = (i & 1) == 0 ? 7 : 6;
        for (unsigned j = 0; j < 3; ++j) bytes[length++] = 0;
    }
    bytes[length++] = 0xc3;
    const uint8_t id[] = {0x48,0x89,0xf8,0xc3};
    const uint8_t loop[] = {0xb8,32,0,0,0,0x48,0x83,0xe8,1,0x75,0xfa,0xc3};
    workload("arithmetic64",bytes,length,arithmetic);
    workload("identity",id,sizeof(id),identity);
    workload("loop32",loop,sizeof(loop),loop32);
    return 0;
}
