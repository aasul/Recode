#ifndef RECODE_EMITTER_H
#define RECODE_EMITTER_H

#include <stddef.h>
#include <stdint.h>

/* NULL bytes counts the same encoding without writing. Callers must size and
 * validate the entire output before the writing pass; these helpers do not bound it. */
struct output { uint8_t *bytes; size_t count; };

static inline void byte(struct output *o, uint8_t value)
{
    if (o->bytes != NULL)
        o->bytes[o->count] = value;
    ++o->count;
}

static inline void word(struct output *o, uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        byte(o, (uint8_t)(value >> shift));
}

#endif
