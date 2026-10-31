#include "recode.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

void *__real_malloc(size_t size);
void *__real_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset);
int __real_mprotect(void *address, size_t size, int protection);
int __real_munmap(void *address, size_t size);

static bool fail_malloc;
static bool fail_mmap;
static bool fail_mprotect;
static bool fail_munmap;
static unsigned unmap_calls;
static unsigned protection_calls;
static size_t malloc_calls, fail_malloc_at;
static unsigned live_mappings;

void *__wrap_malloc(size_t size)
{
    ++malloc_calls;
    return fail_malloc || malloc_calls == fail_malloc_at ? NULL : __real_malloc(size);
}

void *__wrap_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    CHECK(protection == (PROT_READ | PROT_WRITE));
    if (fail_mmap) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    void *mapping = __real_mmap(address, size, protection, flags, fd, offset);
    if (mapping != MAP_FAILED)
        ++live_mappings;
    return mapping;
}

int __wrap_mprotect(void *address, size_t size, int protection)
{
    ++protection_calls;
    CHECK(protection == (PROT_READ | PROT_EXEC));
    if (fail_mprotect) {
        errno = EACCES;
        return -1;
    }
    return __real_mprotect(address, size, protection);
}

int __wrap_munmap(void *address, size_t size)
{
    ++unmap_calls;
    if (fail_munmap) {
        errno = EINVAL;
        return -1;
    }
    int result = __real_munmap(address, size);
    if (result == 0)
        --live_mappings;
    return result;
}

int main(void)
{
    const uint8_t bytes[] = {0x48,0x89,0xf8,0xc3};
    struct rc_code code = {0};
    struct rc_error error;
    fail_malloc = true;
    CHECK(rc_compile(bytes, sizeof(bytes), true, &code, &error) == RC_MEMORY);
    CHECK(code.mapping == NULL && unmap_calls == 0 && protection_calls == 0);
    fail_malloc = false;
    fail_mmap = true;
    CHECK(rc_compile(bytes, sizeof(bytes), true, &code, &error) == RC_SYSTEM);
    CHECK(error.system_errno == ENOMEM && code.mapping == NULL && unmap_calls == 0);
    fail_mmap = false;
    fail_mprotect = true;
    CHECK(rc_compile(bytes, sizeof(bytes), true, &code, &error) == RC_SYSTEM);
    CHECK(error.system_errno == EACCES && code.mapping == NULL);
    CHECK(unmap_calls == 1 && protection_calls == 1);
    fail_munmap = true;
    CHECK(rc_compile(bytes, sizeof(bytes), true, &code, &error) == RC_SYSTEM);
    CHECK(error.system_errno == EINVAL && code.mapping != NULL);
    CHECK(code.size != 0 && code.function == NULL);
    CHECK(rc_compile(bytes, sizeof(bytes), true, &code, NULL) == RC_ARGUMENT);
    fail_munmap = false;
    CHECK(rc_code_free(&code, &error) == RC_OK);
    fail_mprotect = false;
    CHECK(rc_compile(bytes, sizeof(bytes), true, &code, &error) == RC_OK);
    CHECK(code.function(UINT64_MAX) == UINT64_MAX);
    fail_munmap = true;
    CHECK(rc_code_free(&code, &error) == RC_SYSTEM);
    CHECK(error.system_errno == EINVAL && code.mapping != NULL && code.function != NULL);
    CHECK(code.function(42) == 42);
    fail_munmap = false;
    CHECK(rc_code_free(&code, &error) == RC_OK);
    CHECK(code.mapping == NULL && code.function == NULL && code.size == 0);
    CHECK(unmap_calls == 5 && protection_calls == 3);
    CHECK(live_mappings == 0);
    const uint8_t loop[] = {0x48,0x89,0xf8,0x48,0x83,0xe8,1,0x75,0xfa,0xc3};
    size_t optimized_allocations = 0;
    for (unsigned mode = 0; mode < 2; ++mode) {
        malloc_calls = 0;
        fail_malloc_at = 0;
        CHECK(rc_compile(loop, sizeof(loop), mode != 0, &code, &error) == RC_OK);
        size_t allocations = malloc_calls;
        CHECK(allocations > 0);
        if (mode != 0)
            optimized_allocations = allocations;
        CHECK(code.function(7) == 0);
        CHECK(rc_code_free(&code, NULL) == RC_OK);
        for (size_t failure = 1; failure <= allocations; ++failure) {
            malloc_calls = 0;
            fail_malloc_at = failure;
            enum rc_status status = rc_compile(loop, sizeof(loop), mode != 0, &code, &error);
            CHECK(status == RC_MEMORY && error.status == RC_MEMORY);
            CHECK(code.mapping == NULL && code.function == NULL && code.size == 0);
            CHECK(live_mappings == 0);
        }
        malloc_calls = 0;
        fail_malloc_at = allocations + 1;
        CHECK(rc_compile(loop, sizeof(loop), mode != 0, &code, &error) == RC_OK);
        CHECK(malloc_calls == allocations);
        CHECK(code.function(7) == 0);
        CHECK(rc_code_free(&code, NULL) == RC_OK);
    }
    fail_malloc_at = 0;
    CHECK(live_mappings == 0);
    struct rc_insn insns[] = {{RC_INPUT,0},{RC_ADD,1},{RC_SUB,1},{RC_RET,0}};
    struct rc_program program = {insns,4};
    struct rc_insn optimized_insns[] = {{RC_INPUT,0},{RC_ADD,1},{RC_SUB,1},{RC_RET,0}};
    struct rc_program optimized = {optimized_insns,4};
    malloc_calls = 0;
    fail_malloc_at = 0;
    CHECK(rc_optimize(&optimized, NULL) == RC_OK);
    size_t optimize_allocations = malloc_calls;
    CHECK(optimize_allocations > 0);
    for (size_t failure = 1; failure <= optimize_allocations; ++failure) {
        malloc_calls = 0;
        fail_malloc_at = failure;
        CHECK(rc_optimize(&program, NULL) == RC_MEMORY);
        CHECK(program.count == 4 && insns[1].op == RC_ADD && insns[2].op == RC_SUB);
    }
    size_t expected_length = 0;
    malloc_calls = 0;
    fail_malloc_at = 0;
    CHECK(rc_emit(&program, NULL, 0, &expected_length, NULL) == RC_OK);
    CHECK(expected_length > 0);
    size_t emit_allocations = malloc_calls;
    CHECK(emit_allocations > 0);
    for (size_t failure = 1; failure <= emit_allocations; ++failure) {
        uint8_t output[32];
        for (size_t i = 0; i < sizeof(output); ++i)
            output[i] = 0xaa;
        size_t length = 99;
        malloc_calls = 0;
        fail_malloc_at = failure;
        CHECK(rc_emit(&program, output, sizeof(output), &length, NULL) == RC_MEMORY);
        CHECK(length == 0);
        for (size_t i = 0; i < sizeof(output); ++i)
            CHECK(output[i] == 0xaa);
    }
    fail_malloc_at = 0;
    /* Find an allocation in emission after mmap, without assuming a fixed
     * allocation count across compiler versions or build configurations. */
    bool found_post_mapping_failure = false;
    for (size_t failure = 1; failure <= optimized_allocations; ++failure) {
        malloc_calls = 0;
        fail_malloc_at = failure;
        fail_munmap = true;
        enum rc_status status = rc_compile(loop, sizeof(loop), true, &code, &error);
        fail_munmap = false;
        fail_malloc_at = 0;
        if (code.mapping != NULL) {
            CHECK(status == RC_SYSTEM && error.system_errno == EINVAL);
            CHECK(code.size == 20 && code.function == NULL && live_mappings == 1);
            CHECK(rc_code_free(&code, NULL) == RC_OK && live_mappings == 0);
            found_post_mapping_failure = true;
            break;
        }
        CHECK(status == RC_MEMORY && error.status == RC_MEMORY);
        CHECK(code.function == NULL && code.size == 0 && live_mappings == 0);
    }
    CHECK(found_post_mapping_failure);
    puts("allocation and system-call failure paths passed; RW allocation and RX transition verified");
    return 0;
}
