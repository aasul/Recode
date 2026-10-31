#define _GNU_SOURCE
#include "internal.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

_Static_assert(sizeof(rc_function) == sizeof(void *), "x86-64 function pointer size");

enum rc_status rc_compile(const uint8_t *bytes, size_t length, bool optimize,
                          struct rc_code *code, struct rc_error *error)
{
    if (code == NULL || code->mapping != NULL || code->size != 0 || code->function != NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    struct rc_program program = {0};
    enum rc_status status = rc_decode(bytes, length, &program, error);
    if (status != RC_OK)
        return status;
    if (optimize)
        status = rc_optimize(&program, error);
    size_t needed = 0;
    if (status == RC_OK)
        status = rc_emit(&program, NULL, 0, &needed, error);
    if (status != RC_OK) {
        rc_program_free(&program);
        return status;
    }
    status = rc_map_program(&program, needed, code, error);
    rc_program_free(&program);
    return status;
}

enum rc_status rc_map_program(const struct rc_program *program, size_t needed,
                               struct rc_code *code, struct rc_error *error)
{
    void *mapping = mmap(NULL, needed, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        int saved_errno = errno;
        return rc_fail(error, RC_SYSTEM, 0, saved_errno);
    }
    size_t emitted;
    enum rc_status status = rc_emit(program, mapping, needed, &emitted, error);
    if (status == RC_OK && mprotect(mapping, needed, PROT_READ | PROT_EXEC) != 0)
        status = rc_fail(error, RC_SYSTEM, 0, errno);
    if (status != RC_OK) {
        if (munmap(mapping, needed) != 0) {
            *code = (struct rc_code){mapping, needed, NULL};
            return rc_fail(error, RC_SYSTEM, 0, errno);
        }
        return status;
    }
    /* Linux x86-64 has coherent instruction/data caches. Its ABI permits this
     * pointer representation; memcpy avoids an ISO C object/function cast. */
    rc_function function;
    memcpy(&function, &mapping, sizeof(function));
    *code = (struct rc_code){mapping, needed, function};
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_code_free(struct rc_code *code, struct rc_error *error)
{
    if (code == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    if (code->mapping != NULL && munmap(code->mapping, code->size) != 0)
        return rc_fail(error, RC_SYSTEM, 0, errno);
    *code = (struct rc_code){0};
    return rc_fail(error, RC_OK, 0, 0);
}
