#ifndef RECODE_INTERNAL_H
#define RECODE_INTERNAL_H

#include "recode.h"

static inline enum rc_status rc_fail(struct rc_error *error, enum rc_status status,
                                     size_t offset, int system_errno)
{
    if (error != NULL)
        *error = (struct rc_error){status, offset, system_errno};
    return status;
}

static inline uint64_t rc_extend(uint32_t value)
{
    return (value & UINT32_C(0x80000000)) != 0
        ? UINT64_C(0xffffffff00000000) | value : value;
}

#endif
