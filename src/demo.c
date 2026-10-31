#include "recode.h"

#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    /* mov rax,rdi; add rax,7; sub rax,2; nop; ret */
    const uint8_t input[] = {0x48,0x89,0xf8, 0x48,0x05,7,0,0,0,
                             0x48,0x2d,2,0,0,0, 0x90,0xc3};
    struct rc_code code = {0};
    struct rc_error error;
    enum rc_status status = rc_compile(input, sizeof(input), true, &code, &error);
    if (status != RC_OK) {
        fprintf(stderr, "%s at %zu (errno %d)\n", rc_status_string(status),
                error.offset, error.system_errno);
        return 1;
    }
    uint64_t result = code.function(37);
    printf("translated %zu bytes into %zu bytes; f(37) = %" PRIu64 "\n",
           sizeof(input), code.size, result);
    status = rc_code_free(&code, &error);
    if (status != RC_OK)
        fprintf(stderr, "%s (errno %d)\n", rc_status_string(status), error.system_errno);
    return status == RC_OK && result == 42 ? 0 : 1;
}
