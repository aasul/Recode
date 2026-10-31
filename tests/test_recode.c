#include "recode.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static uint64_t random_state = UINT64_C(0xc08077121d4b69e3);

static uint64_t random_u64(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

static uint64_t reference_immediate(uint32_t bits)
{
    int64_t value = bits;
    if (value > INT32_MAX)
        value -= INT64_C(4294967296);
    return (uint64_t)value;
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8));
}

static void expect_decode_error(const uint8_t *bytes, size_t length,
                                 enum rc_status status, size_t offset)
{
    struct rc_program program = {0};
    struct rc_error error = {0};
    CHECK(rc_decode(bytes, length, &program, &error) == status);
    CHECK(error.status == status && error.offset == offset);
    CHECK(program.insns == NULL && program.count == 0);
    struct rc_code code = {0};
    CHECK(rc_compile(bytes, length, true, &code, &error) == status);
    CHECK(code.mapping == NULL && code.function == NULL && code.size == 0);
}

static void test_decode(void)
{
    const uint8_t bytes[] = {
        0x48,0x89,0xf8, 0xb8,0xff,0xff,0xff,0xff,
        0x48,0x05,0,0,0,0x80, 0x48,0x2d,0xff,0xff,0xff,0x7f,
        0x48,0x81,0xc0,0xff,0xff,0xff,0xff,
        0x48,0x81,0xe8,0,0,0,0x80,
        0x48,0x83,0xc0,0x80, 0x48,0x83,0xe8,0xff,
        0x48,0x8b,0xc7, 0x90,0xc3
    };
    const struct rc_insn expected[] = {
        {RC_INPUT,0}, {RC_CONST,UINT32_MAX}, {RC_ADD,UINT32_C(0x80000000)},
        {RC_SUB,INT32_MAX}, {RC_ADD,UINT32_MAX}, {RC_SUB,UINT32_C(0x80000000)},
        {RC_ADD,UINT32_C(0xffffff80)}, {RC_SUB,UINT32_MAX},
        {RC_INPUT,0}, {RC_NOP,0}, {RC_RET,0}
    };
    struct rc_program program = {0};
    struct rc_error error;
    CHECK(rc_decode(bytes, sizeof(bytes), &program, &error) == RC_OK);
    CHECK(error.status == RC_OK && error.system_errno == 0);
    CHECK(program.count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < program.count; ++i) {
        CHECK(program.insns[i].op == expected[i].op);
        CHECK(program.insns[i].immediate == expected[i].immediate);
    }
    rc_program_free(&program);
    rc_program_free(&program);
    expect_decode_error(NULL, 0, RC_MISSING_RET, 0);
    expect_decode_error(NULL, 1, RC_ARGUMENT, 0);
    const uint8_t uninitialized[] = {0xc3};
    expect_decode_error(uninitialized, sizeof(uninitialized), RC_UNINITIALIZED, 0);
    const uint8_t arithmetic_first[] = {0x90,0x48,0x05,1,0,0,0,0xc3};
    expect_decode_error(arithmetic_first, sizeof(arithmetic_first), RC_UNINITIALIZED, 1);
    const uint8_t missing[] = {0x48,0x89,0xf8};
    expect_decode_error(missing, sizeof(missing), RC_MISSING_RET, 3);
    const uint8_t trailing[] = {0x48,0x89,0xf8,0xc3,0x90};
    expect_decode_error(trailing, sizeof(trailing), RC_TRAILING, 4);
    const uint8_t unsupported[][4] = {
        {0x0f,0x0b,0,0}, {0x48,0x89,0xf9,0}, {0x48,0x81,0x00,0},
        {0x48,0x83,0xf8,0}, {0x48,0x8b,0x07,0}, {0x48,0xff,0xc0,0},
        {0x66,0x90,0,0}, {0xe3,0,0,0}, {0x48,0xb8,0,0}, {0xc2,0,0,0}
    };
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i)
        expect_decode_error(unsupported[i], sizeof(unsupported[i]), RC_UNSUPPORTED, 0);
    CHECK(rc_decode(bytes, SIZE_MAX, &program, &error) == RC_MEMORY);
    CHECK(rc_decode(bytes, sizeof(bytes), NULL, &error) == RC_ARGUMENT);
    CHECK(rc_decode(bytes, sizeof(bytes), &program, NULL) == RC_OK);
    CHECK(rc_decode(bytes, sizeof(bytes), &program, &error) == RC_ARGUMENT);
    rc_program_free(&program);
}

static void test_bounded_reads(void)
{
    long page = sysconf(_SC_PAGESIZE);
    CHECK(page > 0);
    size_t size = (size_t)page;
    uint8_t *mapping = mmap(NULL, size * 2, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    CHECK(mprotect(mapping + size, size, PROT_NONE) == 0);
    const uint8_t instructions[][7] = {
        {0xb8,1,2,3,4}, {0x48,0x89,0xf8}, {0x48,0x8b,0xc7},
        {0x48,0x05,1,2,3,4}, {0x48,0x2d,1,2,3,4},
        {0x48,0x81,0xc0,1,2,3,4}, {0x48,0x81,0xe8,1,2,3,4},
        {0x48,0x83,0xc0,0x80}, {0x48,0x83,0xe8,0x80}
    };
    const size_t lengths[] = {5,3,3,6,6,7,7,4,4};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        for (size_t prefix = 1; prefix < lengths[i]; ++prefix) {
            uint8_t *start = mapping + size - prefix;
            memcpy(start, instructions[i], prefix);
            expect_decode_error(start, prefix, RC_TRUNCATED, 0);
        }
    }
    CHECK(munmap(mapping, size * 2) == 0);
}

static void check_fold(uint32_t first, enum rc_op second_op, uint32_t second,
                        size_t expected_count, enum rc_op folded_op, uint32_t folded)
{
    struct rc_insn insns[] = {{RC_INPUT,0}, {RC_ADD,first}, {RC_NOP,0},
                              {second_op,second}, {RC_RET,0}};
    struct rc_program program = {insns, 5};
    CHECK(rc_optimize(&program, NULL) == RC_OK);
    CHECK(program.count == expected_count);
    if (expected_count == 3) {
        CHECK(program.insns[1].op == folded_op);
        CHECK(program.insns[1].immediate == folded);
    }
    if (expected_count == 4) {
        CHECK(program.insns[1].op == RC_ADD && program.insns[1].immediate == first);
        CHECK(program.insns[2].op == second_op && program.insns[2].immediate == second);
    }
    size_t count = program.count;
    CHECK(rc_optimize(&program, NULL) == RC_OK);
    CHECK(program.count == count);
}

static void test_optimizer(void)
{
    check_fold(7, RC_SUB, 2, 3, RC_ADD, 5);
    check_fold(7, RC_SUB, 7, 2, RC_NOP, 0);
    check_fold(UINT32_MAX, RC_ADD, 1, 2, RC_NOP, 0);
    check_fold(INT32_MAX, RC_ADD, 1, 3, RC_SUB, UINT32_C(0x80000000));
    check_fold(UINT32_C(0x80000000), RC_SUB, 1, 4, RC_NOP, 0);
    check_fold(INT32_MAX, RC_ADD, INT32_MAX, 4, RC_NOP, 0);
    check_fold(UINT32_C(0x80000000), RC_ADD, UINT32_C(0x80000000), 4, RC_NOP, 0);
    struct rc_insn insns[] = {{RC_NOP,0}, {RC_INPUT,0}, {RC_ADD,2},
        {RC_CONST,7}, {RC_SUB,2}, {RC_INPUT,0}, {RC_ADD,0}, {RC_RET,0}};
    struct rc_program program = {insns, 8};
    CHECK(rc_optimize(&program, NULL) == RC_OK && program.count == 6);
    CHECK(program.insns[1].op == RC_ADD && program.insns[1].immediate == 2);
    CHECK(program.insns[2].op == RC_CONST && program.insns[2].immediate == 7);
    CHECK(program.insns[3].op == RC_ADD && program.insns[3].immediate == UINT32_C(0xfffffffe));
    CHECK(program.insns[4].op == RC_INPUT);
}

static void test_ir_and_emitter(void)
{
    struct rc_insn insns[] = {{RC_INPUT,0}, {RC_CONST,UINT32_MAX},
                              {RC_ADD,UINT32_C(0x80000000)}, {RC_SUB,7}, {RC_NOP,0}, {RC_RET,0}};
    struct rc_program program = {insns, 6};
    const uint8_t expected[] = {0xf3,0x0f,0x1e,0xfa, 0x48,0x89,0xf8,
        0xb8,0xff,0xff,0xff,0xff, 0x48,0x05,0,0,0,0x80,
        0x48,0x2d,7,0,0,0, 0x90,0xc3};
    uint8_t bytes[sizeof(expected) + 1];
    memset(bytes, 0xaa, sizeof(bytes));
    size_t length = 0;
    CHECK(rc_emit(&program, NULL, 0, &length, NULL) == RC_OK && length == sizeof(expected));
    CHECK(rc_emit(&program, bytes, length - 1, &length, NULL) == RC_CAPACITY);
    for (size_t i = 0; i < sizeof(bytes); ++i)
        CHECK(bytes[i] == 0xaa);
    CHECK(rc_emit(&program, bytes, sizeof(bytes), &length, NULL) == RC_OK);
    CHECK(length == sizeof(expected) && memcmp(bytes, expected, length) == 0);
    CHECK(bytes[length] == 0xaa);
    struct rc_program decoded = {0};
    CHECK(rc_decode(bytes + 4, length - 4, &decoded, NULL) == RC_OK);
    CHECK(decoded.count == program.count);
    for (size_t i = 0; i < program.count; ++i) {
        CHECK(decoded.insns[i].op == program.insns[i].op);
        CHECK(decoded.insns[i].immediate == program.insns[i].immediate);
    }
    rc_program_free(&decoded);
    CHECK(rc_emit(&program, NULL, 1, &length, NULL) == RC_ARGUMENT);
    CHECK(rc_emit(&program, bytes, sizeof(bytes), NULL, NULL) == RC_ARGUMENT);
    CHECK(rc_validate(NULL, NULL) == RC_ARGUMENT);
    struct rc_program invalid = {NULL, 1};
    CHECK(rc_validate(&invalid, NULL) == RC_ARGUMENT);
    invalid = (struct rc_program){NULL, 0};
    CHECK(rc_validate(&invalid, NULL) == RC_MISSING_RET);
    struct rc_insn bad[] = {{RC_NOP,0}, {(enum rc_op)99,0}, {RC_RET,0}};
    invalid = (struct rc_program){bad, 3};
    struct rc_error error;
    CHECK(rc_optimize(&invalid, &error) == RC_INVALID_IR && error.offset == 1);
    CHECK(invalid.count == 3 && bad[0].op == RC_NOP);
    CHECK(rc_emit(&invalid, bytes, sizeof(bytes), &length, NULL) == RC_INVALID_IR);
    CHECK(length == 0);
    bad[1].op = RC_RET;
    CHECK(rc_validate(&invalid, NULL) == RC_UNINITIALIZED);
    bad[0].op = RC_INPUT;
    CHECK(rc_validate(&invalid, &error) == RC_TRAILING && error.offset == 2);
    invalid.count = 1;
    CHECK(rc_validate(&invalid, NULL) == RC_MISSING_RET);
}

static void test_permissions(const struct rc_code *code)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    CHECK(maps != NULL);
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), maps) != NULL) {
        uintptr_t start, end;
        char perms[5];
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, perms) == 3 &&
            (uintptr_t)code->mapping >= start && (uintptr_t)code->mapping < end) {
            CHECK(perms[0] == 'r' && perms[1] == '-' && perms[2] == 'x');
            found = true;
            break;
        }
    }
    CHECK(fclose(maps) == 0 && found);
}

static void compare_execution(const uint8_t *bytes, size_t length,
                               const uint64_t *inputs, const uint64_t *expected, size_t count)
{
    for (unsigned optimize = 0; optimize < 2; ++optimize) {
        struct rc_code code = {0};
        struct rc_error error;
        CHECK(rc_compile(bytes, length, optimize != 0, &code, &error) == RC_OK);
        for (size_t i = 0; i < count; ++i)
            CHECK(code.function(inputs[i]) == expected[i]);
        CHECK(rc_code_free(&code, &error) == RC_OK);
        CHECK(code.mapping == NULL && code.size == 0 && code.function == NULL);
        CHECK(rc_code_free(&code, NULL) == RC_OK);
    }
}

static void test_execution(void)
{
    const uint32_t immediates[] = {0,1,127,128,255,INT32_MAX,UINT32_C(0x80000000),UINT32_MAX};
    const uint64_t inputs[] = {0,1,UINT32_MAX,UINT64_C(0x80000000),
        UINT64_C(0x100000000),UINT64_C(0x7fffffffffffffff),UINT64_C(0x8000000000000000),UINT64_MAX};
    uint64_t expected[sizeof(inputs) / sizeof(inputs[0])];
    for (size_t a = 0; a < sizeof(immediates) / sizeof(immediates[0]); ++a) {
        for (size_t b = 0; b < sizeof(immediates) / sizeof(immediates[0]); ++b) {
            for (unsigned sub = 0; sub < 2; ++sub) {
                uint8_t bytes[] = {0x48,0x89,0xf8,0x48,0x05,0,0,0,0,0x48,0x05,0,0,0,0,0xc3};
                put_u32(bytes + 5, immediates[a]);
                bytes[10] = sub != 0 ? 0x2d : 0x05;
                put_u32(bytes + 11, immediates[b]);
                for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
                    expected[i] = inputs[i] + reference_immediate(immediates[a]);
                    if (sub != 0)
                        expected[i] -= reference_immediate(immediates[b]);
                    else
                        expected[i] += reference_immediate(immediates[b]);
                }
                compare_execution(bytes, sizeof(bytes), inputs, expected, sizeof(inputs) / sizeof(inputs[0]));
            }
        }
        uint8_t constant[] = {0xb8,0,0,0,0,0xc3};
        put_u32(constant + 1, immediates[a]);
        for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
            expected[i] = immediates[a];
        compare_execution(constant, sizeof(constant), inputs, expected, sizeof(inputs) / sizeof(inputs[0]));
    }
    for (unsigned immediate = 0; immediate < 256; ++immediate) {
        for (unsigned sub = 0; sub < 2; ++sub) {
            uint8_t small[] = {0x48,0x89,0xf8,0x48,0x83,0xc0,0,0xc3};
            small[5] = sub != 0 ? 0xe8 : 0xc0;
            small[6] = (uint8_t)immediate;
            int64_t signed_value = immediate < 128 ? (int64_t)immediate : (int64_t)immediate - 256;
            for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
                expected[i] = sub != 0 ? inputs[i] - (uint64_t)signed_value
                                       : inputs[i] + (uint64_t)signed_value;
            compare_execution(small, sizeof(small), inputs, expected, sizeof(inputs) / sizeof(inputs[0]));
        }
    }
    const uint8_t identity[] = {0x48,0x89,0xf8,0xc3};
    struct rc_code code = {0};
    CHECK(rc_compile(identity, sizeof(identity), true, &code, NULL) == RC_OK);
    test_permissions(&code);
    CHECK(rc_compile(identity, sizeof(identity), true, &code, NULL) == RC_ARGUMENT);
    CHECK(code.function(123) == 123);
    CHECK(rc_code_free(&code, NULL) == RC_OK);
    CHECK(rc_code_free(NULL, NULL) == RC_ARGUMENT);
    CHECK(rc_compile(identity, sizeof(identity), true, NULL, NULL) == RC_ARGUMENT);
}

static void test_random_execution(void)
{
    for (size_t trial = 0; trial < 1000; ++trial) {
        uint8_t bytes[512] = {0x48,0x89,0xf8};
        size_t length = 3;
        uint64_t inputs[32], expected[32];
        for (size_t i = 0; i < 32; ++i)
            expected[i] = inputs[i] = random_u64();
        for (size_t step = 0; step < 40; ++step) {
            unsigned choice = (unsigned)(random_u64() % 12);
            uint32_t immediate = (uint32_t)random_u64();
            if (choice == 0) {
                bytes[length++] = 0x90;
            } else if (choice == 1) {
                bytes[length++] = 0xb8;
                put_u32(bytes + length, immediate);
                length += 4;
                for (size_t i = 0; i < 32; ++i)
                    expected[i] = immediate;
            } else if (choice == 2) {
                bytes[length++] = 0x48;
                bytes[length++] = 0x8b;
                bytes[length++] = 0xc7;
                memcpy(expected, inputs, sizeof(inputs));
            } else {
                bool sub = (choice & 1) != 0;
                unsigned form = (unsigned)(random_u64() % 3);
                bytes[length++] = 0x48;
                if (form == 0) {
                    bytes[length++] = sub ? 0x2d : 0x05;
                } else {
                    bytes[length++] = form == 1 ? 0x81 : 0x83;
                    bytes[length++] = sub ? 0xe8 : 0xc0;
                }
                if (form == 2) {
                    bytes[length++] = (uint8_t)immediate;
                    immediate &= 255;
                    if (immediate >= 128)
                        immediate |= UINT32_C(0xffffff00);
                } else {
                    put_u32(bytes + length, immediate);
                    length += 4;
                }
                for (size_t i = 0; i < 32; ++i) {
                    if (sub)
                        expected[i] -= reference_immediate(immediate);
                    else
                        expected[i] += reference_immediate(immediate);
                }
            }
        }
        bytes[length++] = 0xc3;
        compare_execution(bytes, length, inputs, expected, 32);
    }
}

static void test_random_decode(void)
{
    for (size_t trial = 0; trial < 10000; ++trial) {
        size_t length = (size_t)(random_u64() % 65);
        uint8_t *bytes = malloc(length == 0 ? 1 : length);
        CHECK(bytes != NULL);
        for (size_t i = 0; i < length; ++i)
            bytes[i] = (uint8_t)random_u64();
        struct rc_program program = {0};
        struct rc_error error;
        enum rc_status status = rc_decode(bytes, length, &program, &error);
        CHECK(status == error.status);
        if (status == RC_OK)
            CHECK(rc_validate(&program, NULL) == RC_OK);
        else
            CHECK(program.insns == NULL && error.offset <= length);
        rc_program_free(&program);
        free(bytes);
    }
}

int main(void)
{
    test_decode();
    test_bounded_reads();
    test_optimizer();
    test_ir_and_emitter();
    test_execution();
    test_random_execution();
    test_random_decode();
    puts("decoder, IR, optimizer, emitter, permissions, boundaries, 1000 random programs and 10000 decoder inputs passed");
    return 0;
}
