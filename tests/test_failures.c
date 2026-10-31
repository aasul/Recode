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

void *__wrap_malloc(size_t size)
{
    return fail_malloc ? NULL : __real_malloc(size);
}

void *__wrap_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    CHECK(protection == (PROT_READ | PROT_WRITE));
    if (fail_mmap) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return __real_mmap(address, size, protection, flags, fd, offset);
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
    return __real_munmap(address, size);
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
    CHECK(unmap_calls == 3 && protection_calls == 2);
    puts("allocation and system-call failure paths passed; RW allocation and RX transition verified");
    return 0;
}
