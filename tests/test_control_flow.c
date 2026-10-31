#include "internal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static uint64_t seed = UINT64_C(0x68a435e083fa6719);

static uint32_t random_u32(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (uint32_t)seed;
}

static uint32_t read32(const uint8_t *p)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= (uint32_t)p[i] << (8 * i);
    return value;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}

static int64_t signed_value(uint32_t bits, unsigned width)
{
    int64_t value = bits;
    if ((value & (INT64_C(1) << (width - 1))) != 0)
        value -= INT64_C(1) << width;
    return value;
}

/* This interpreter walks source bytes, independently of the decoder and IR.
 * Its step bound protects tests only; translated functions have no such bound. */
static uint64_t interpret(const uint8_t *bytes, size_t length, uint64_t argument)
{
    uint64_t rax = 0;
    bool zero = false;
    size_t pc = 0;
    for (unsigned step = 0; step < 10000; ++step) {
        CHECK(pc < length);
        uint8_t op = bytes[pc++];
        if (op == 0xc3)
            return rax;
        if (op == 0x90)
            continue;
        if (op == 0xb8) {
            CHECK(length - pc >= 4);
            rax = read32(bytes + pc);
            pc += 4;
        } else if (op == 0xe9 || op == 0xeb || op == 0x74 || op == 0x75 || op == 0x0f) {
            bool take = true;
            unsigned width = op == 0xe9 || op == 0x0f ? 32 : 8;
            if (op == 0x0f) {
                CHECK(pc < length);
                op = bytes[pc++];
                CHECK(op == 0x84 || op == 0x85);
            }
            if (op == 0x74 || op == 0x84)
                take = zero;
            if (op == 0x75 || op == 0x85)
                take = !zero;
            CHECK(length - pc >= width / 8);
            uint32_t bits = width == 8 ? bytes[pc] : read32(bytes + pc);
            pc += width / 8;
            if (take) {
                int64_t target = (int64_t)pc + signed_value(bits, width);
                CHECK(target >= 0 && (uint64_t)target < length);
                pc = (size_t)target;
            }
        } else {
            CHECK(op == 0x48 && pc < length);
            op = bytes[pc++];
            if (op == 0x89 || op == 0x8b) {
                CHECK(pc < length && bytes[pc] == (op == 0x89 ? 0xf8 : 0xc7));
                ++pc;
                rax = argument;
            } else if (op == 0x85) {
                CHECK(pc < length && bytes[pc++] == 0xc0);
                zero = rax == 0;
            } else {
                bool subtract = op == 0x2d || op == 0x3d;
                bool compare = op == 0x3d;
                unsigned width = 32;
                if (op == 0x81 || op == 0x83) {
                    CHECK(pc < length);
                    uint8_t modrm = bytes[pc++];
                    CHECK(modrm == 0xc0 || modrm == 0xe8 || modrm == 0xf8);
                    subtract = modrm != 0xc0;
                    compare = modrm == 0xf8;
                    width = op == 0x83 ? 8 : 32;
                } else {
                    CHECK(op == 0x05 || op == 0x2d || op == 0x3d);
                }
                CHECK(length - pc >= width / 8);
                uint32_t bits = width == 8 ? bytes[pc] : read32(bytes + pc);
                pc += width / 8;
                uint64_t immediate = (uint64_t)signed_value(bits, width);
                uint64_t result = subtract ? rax - immediate : rax + immediate;
                zero = result == 0;
                if (!compare)
                    rax = result;
            }
        }
    }
    CHECK(false);
    return 0;
}

static void compare(const uint8_t *bytes, size_t length, const uint64_t *inputs, size_t count)
{
    /* Prefixing the original preserves all relative distances and permits IBT entry. */
    uint8_t *original = mmap(NULL, length + 4, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(original != MAP_FAILED);
    const uint8_t entry[] = {0xf3,0x0f,0x1e,0xfa};
    memcpy(original, entry, 4);
    memcpy(original + 4, bytes, length);
    CHECK(mprotect(original, length + 4, PROT_READ | PROT_EXEC) == 0);
    rc_function source;
    _Static_assert(sizeof(source) == sizeof(original), "function pointer size");
    memcpy(&source, &original, sizeof(source));
    for (unsigned mode = 0; mode < 2; ++mode) {
        struct rc_code code = {0};
        struct rc_error error;
        enum rc_status status = rc_compile(bytes, length, mode != 0, &code, &error);
        if (status != RC_OK)
            fprintf(stderr, "compile: %s at %zu\n", rc_status_string(status), error.offset);
        CHECK(status == RC_OK);
        struct rc_program program = {0};
        CHECK(rc_decode(bytes, length, &program, NULL) == RC_OK);
        struct rc_runtime_config config = rc_runtime_default_config();
        config.policy = RC_POLICY_FIXED;
        config.hot_threshold = 2;
        config.optimize = mode != 0;
        struct rc_runtime *runtime = NULL;
        rc_handle handle;
        CHECK(rc_runtime_create(&config, &runtime, NULL) == RC_OK);
        CHECK(rc_runtime_register(runtime, bytes, length, &handle, NULL) == RC_OK);
        struct rc_runtime *tracing = NULL;
        rc_handle trace_handle;
        config.policy = RC_POLICY_TRACE;
        config.trace_hot_threshold = 2;
        CHECK(rc_runtime_create(&config,&tracing,NULL) == RC_OK);
        CHECK(rc_runtime_register(tracing,bytes,length,&trace_handle,NULL) == RC_OK);
        for (size_t i = 0; i < count; ++i) {
            uint64_t expected = interpret(bytes, length, inputs[i]);
            CHECK(source(inputs[i]) == expected);
            CHECK(code.function(inputs[i]) == expected);
            uint64_t result;
            CHECK(rc_interpret(&program, inputs[i], 10000, &result, NULL) == RC_OK);
            CHECK(result == expected);
            CHECK(rc_runtime_call(runtime, handle, inputs[i], &result, NULL) == RC_OK);
            CHECK(result == expected);
            CHECK(rc_runtime_call(tracing,trace_handle,inputs[i],&result,NULL) == RC_OK);
            CHECK(result == expected);
        }
        CHECK(rc_runtime_destroy(&tracing,NULL) == RC_OK);
        CHECK(rc_runtime_destroy(&runtime, NULL) == RC_OK);
        rc_program_free(&program);
        CHECK(rc_code_free(&code, NULL) == RC_OK);
    }
    CHECK(munmap(original, length + 4) == 0);
}

static void decode_error(const uint8_t *bytes, size_t length, enum rc_status expected, size_t offset)
{
    struct rc_program program = {0};
    struct rc_error error;
    enum rc_status status = rc_decode(bytes, length, &program, &error);
    if (status != expected || error.offset != offset)
        fprintf(stderr, "decode %02x length %zu: %s at %zu, expected %s at %zu\n",
                bytes[0], length, rc_status_string(status), error.offset,
                rc_status_string(expected), offset);
    CHECK(status == expected);
    CHECK(error.offset == offset && error.status == expected);
    CHECK(program.insns == NULL && program.count == 0);
}

static void test_validation(void)
{
    const uint8_t invalid[][10] = {
        {0xeb,0xfe}, {0xeb,0xfd}, {0xeb,0}, {0xe9,0,0,0,0x80},
        {0xe9,0xff,0xff,0xff,0x7f},
        {0xb8,0,0,0,0,0xeb,0xfa}, /* Target is inside MOV. */
        {0x48,0x89,0xf8,0x74,0,0xc3},
        {0xeb,0,0x48,0x85,0xc0,0xc3},
        {0x48,0x89,0xf8,0xc3,0xc3},
        {0x48,0x89,0xf8,0x48,0x85,0xc0,0x74,0xf8},
        {0x48,0x85,0xc1}, {0x48,0x81,0xf9}, {0x0f,0x86}
    };
    const size_t lengths[] = {2,2,2,5,5,7,6,6,5,8,3,3,2};
    const enum rc_status statuses[] = {RC_OK,RC_BRANCH,RC_BRANCH,RC_BRANCH,RC_BRANCH,
        RC_BRANCH,RC_UNDEFINED_ZF,RC_UNINITIALIZED,RC_TRAILING,RC_MISSING_RET,
        RC_UNSUPPORTED,RC_UNSUPPORTED,RC_UNSUPPORTED};
    const size_t offsets[] = {0,0,0,0,0,5,3,2,4,8,0,0,0};
    for (size_t i = 1; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
        decode_error(invalid[i], lengths[i], statuses[i], offsets[i]);
    struct rc_program loop = {0};
    CHECK(rc_decode(invalid[0], 2, &loop, NULL) == RC_OK);
    rc_program_free(&loop);

    /* The entry path skips initialization, although the backedge supplies it. */
    struct rc_insn rax_loop[] = {{RC_JMP,2},{RC_INPUT,0},{RC_ADD,1},{RC_JMP,1}};
    struct rc_program p = {rax_loop,4};
    struct rc_error error;
    CHECK(rc_validate(&p, &error) == RC_UNINITIALIZED && error.offset == 2);
    struct rc_insn zf_loop[] = {{RC_INPUT,0},{RC_JMP,3},{RC_TEST,0},{RC_JNZ,2},{RC_RET,0}};
    p = (struct rc_program){zf_loop,5};
    CHECK(rc_validate(&p, &error) == RC_UNDEFINED_ZF && error.offset == 3);
    /* RAX and ZF remain defined through both predecessors of the join. */
    struct rc_insn join[] = {{RC_INPUT,0},{RC_TEST,0},{RC_JZ,5},
        {RC_CONST,7},{RC_JMP,6},{RC_NOP,0},{RC_RET,0}};
    p = (struct rc_program){join,7};
    CHECK(rc_validate(&p, NULL) == RC_OK);
    join[0] = (struct rc_insn){RC_JMP,2};
    CHECK(rc_validate(&p, &error) == RC_UNDEFINED_ZF && error.offset == 2);
    struct rc_insn missing_rax[] = {{RC_JMP,2},{RC_INPUT,0},{RC_RET,0}};
    p = (struct rc_program){missing_rax,3};
    CHECK(rc_validate(&p, &error) == RC_UNINITIALIZED && error.offset == 2);
    struct rc_insn bad_target[] = {{RC_JMP,UINT32_MAX}};
    p = (struct rc_program){bad_target,1};
    CHECK(rc_optimize(&p, &error) == RC_INVALID_IR && error.offset == 0);
    CHECK(bad_target[0].immediate == UINT32_MAX);
    p.count = SIZE_MAX;
    CHECK(rc_validate(&p, NULL) == RC_MEMORY);

    uint8_t boundary[] = {0x48,0x89,0xf8,0x48,0x85,0xc0,0x74,0,
        0xb8,0,0,0,0,0xc3};
    for (size_t target = 0; target <= sizeof(boundary); ++target) {
        boundary[7] = (uint8_t)(target - 8);
        bool valid = target == 0 || target == 3 || target == 6 || target == 8 || target == 13;
        struct rc_program decoded = {0};
        CHECK(rc_decode(boundary, sizeof(boundary), &decoded, &error) ==
              (valid ? RC_OK : RC_BRANCH));
        if (!valid)
            CHECK(error.offset == 6);
        rc_program_free(&decoded);
    }
    struct rc_insn cycle[512];
    cycle[0] = (struct rc_insn){RC_JMP,511};
    for (uint32_t i = 1; i < 512; ++i)
        cycle[i] = (struct rc_insn){RC_JMP,i == 1 ? 511 : i - 1};
    p = (struct rc_program){cycle,512};
    CHECK(rc_validate(&p, NULL) == RC_OK);

    /* Exercise both facts at a merge through entry and a loop predecessor. */
    struct rc_insn valid_loop[] = {{RC_INPUT,0},{RC_TEST,0},{RC_JZ,7},
        {RC_SUB,1},{RC_JNZ,3},{RC_CONST,9},{RC_JMP,8},{RC_CONST,4},{RC_RET,0}};
    p = (struct rc_program){valid_loop,9};
    struct rc_cfg cfg = {0};
    CHECK(rc_analyze(&p, &cfg, NULL) == RC_OK);
    CHECK(cfg.count == 5);
    CHECK(cfg.blocks[cfg.block_for[3]].successor_count == 2);
    CHECK(cfg.blocks[cfg.block_for[3]].successors[0] == cfg.block_for[3]);
    CHECK(cfg.blocks[cfg.block_for[8]].defined == (RC_RAX_DEFINED | RC_ZF_DEFINED));
    rc_cfg_free(&cfg);
}

static void test_truncation(void)
{
    const uint8_t forms[][7] = {{0xeb,0},{0xe9,0,0,0,0},{0x74,0},{0x75,0},
        {0x0f,0x84,0,0,0,0},{0x0f,0x85,0,0,0,0},
        {0x48,0x3d,0,0,0,0},{0x48,0x81,0xf8,0,0,0,0},{0x48,0x85,0xc0}};
    const size_t lengths[] = {2,5,2,2,6,6,6,7,3};
    long page = sysconf(_SC_PAGESIZE);
    CHECK(page > 0);
    size_t size = (size_t)page;
    uint8_t *memory = mmap(NULL, size * 2, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED && mprotect(memory + size, size, PROT_NONE) == 0);
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        for (size_t n = 1; n < lengths[i]; ++n) {
            uint8_t *start = memory + size - n;
            memcpy(start, forms[i], n);
            decode_error(start, n, RC_TRUNCATED, 0);
        }
    }
    CHECK(munmap(memory, size * 2) == 0);
}

static void test_encodings(void)
{
    const uint64_t inputs[] = {0,1,2,3,UINT64_MAX,UINT64_MAX-1,
        UINT64_C(0x8000000000000000),UINT64_C(0x7fffffffffffffff)};
    /* A conditional with two return paths, for both conditions and widths. */
    for (unsigned near = 0; near < 2; ++near) {
        for (unsigned nz = 0; nz < 2; ++nz) {
            uint8_t b[32] = {0x48,0x89,0xf8,0x48,0x85,0xc0};
            size_t n = 6;
            if (near != 0) {
                b[n++] = 0x0f;
                b[n++] = nz != 0 ? 0x85 : 0x84;
                put32(b + n, 6);
                n += 4;
            } else {
                b[n++] = nz != 0 ? 0x75 : 0x74;
                b[n++] = 6;
            }
            const uint8_t tails[] = {0xb8,7,0,0,0,0xc3,0xb8,9,0,0,0,0xc3};
            memcpy(b + n, tails, sizeof(tails));
            compare(b, n + sizeof(tails), inputs, sizeof(inputs) / sizeof(inputs[0]));
        }
    }
    /* Backward JZ/JNZ and JMP in both widths, with forward entry jumps. */
    const uint64_t positive[] = {1,2,3,17,32};
    for (unsigned near = 0; near < 2; ++near) {
        uint8_t loop[32] = {0x48,0x89,0xf8,0x48,0x83,0xe8,1};
        size_t n = 7;
        if (near != 0)
            loop[n++] = 0x0f;
        loop[n++] = near != 0 ? 0x85 : 0x75;
        size_t patch = n;
        n += near != 0 ? 4 : 1;
        if (near != 0)
            put32(loop + patch, (uint32_t)(3 - n));
        else
            loop[patch] = (uint8_t)(3 - n);
        loop[n++] = 0xc3;
        compare(loop, n, positive, sizeof(positive) / sizeof(positive[0]));

        uint8_t jump[40] = {0x48,0x89,0xf8};
        n = 3;
        jump[n++] = near != 0 ? 0xe9 : 0xeb;
        size_t forward = n;
        n += near != 0 ? 4 : 1;
        size_t body = n;
        const uint8_t sub[] = {0x48,0x83,0xe8,1};
        memcpy(jump + n, sub, sizeof(sub)); n += sizeof(sub);
        size_t condition = n;
        const uint8_t test[] = {0x48,0x85,0xc0,0x74,0};
        memcpy(jump + n, test, sizeof(test)); n += sizeof(test);
        jump[n++] = near != 0 ? 0xe9 : 0xeb;
        size_t backward = n;
        n += near != 0 ? 4 : 1;
        jump[condition + 4] = (uint8_t)(n - (condition + 5));
        jump[n++] = 0xc3;
        if (near != 0) {
            put32(jump + forward, (uint32_t)(condition - (forward + 4)));
            put32(jump + backward, (uint32_t)(body - (backward + 4)));
        } else {
            jump[forward] = (uint8_t)(condition - (forward + 1));
            jump[backward] = (uint8_t)(body - (backward + 1));
        }
        compare(jump, n, positive, sizeof(positive) / sizeof(positive[0]));
        uint8_t jz[] = {0x48,0x89,0xf8,0x48,0x83,0xc0,1,0x74,0xfa,0xc3};
        uint8_t jz_near[] = {0x48,0x89,0xf8,0x48,0x83,0xc0,1,0x0f,0x84,0xf6,0xff,0xff,0xff,0xc3};
        compare(near != 0 ? jz_near : jz, near != 0 ? sizeof(jz_near) : sizeof(jz), inputs, 8);
    }
    const uint8_t nested[] = {0x48,0x89,0xf8,0x48,0x3d,0,0,0,0,0x74,9,
        0x48,0x81,0xf8,1,0,0,0,0x75,6,0xb8,7,0,0,0,0xc3,0xb8,9,0,0,0,0xc3};
    compare(nested, sizeof(nested), inputs, 8);
}

static void test_flags_and_optimizer(void)
{
    const uint64_t inputs[] = {0,1,2,UINT64_MAX,UINT64_MAX-1,
        UINT64_C(0x8000000000000000),UINT64_C(0x7fffffffffffffff),UINT64_C(0xffffffff80000000)};
    const uint32_t immediates[] = {0,1,INT32_MAX,UINT32_C(0x80000000),UINT32_MAX};
    for (size_t a = 0; a < 5; ++a) {
        for (size_t b = 0; b < 5; ++b) {
            for (unsigned subtract = 0; subtract < 2; ++subtract) {
                uint8_t bytes[] = {0x48,0x89,0xf8,0x48,0x85,0xc0,
                    0x48,0x05,0,0,0,0,0x48,0x2d,0,0,0,0,
                    0xb8,77,0,0,0,0x74,6,0xb8,7,0,0,0,0xc3,0xb8,9,0,0,0,0xc3};
                put32(bytes + 8, immediates[a]);
                bytes[13] = subtract != 0 ? 0x2d : 0x05;
                put32(bytes + 14, immediates[b]);
                compare(bytes, sizeof(bytes), inputs, 8);
            }
        }
    }
    /* CMP has no register result, including sign-extended negative immediates. */
    for (unsigned form = 0; form < 2; ++form) {
        for (size_t i = 0; i < 5; ++i) {
            uint8_t bytes[40] = {0x48,0x89,0xf8,0x48};
            size_t n = 4;
            bytes[n++] = form != 0 ? 0x81 : 0x3d;
            if (form != 0)
                bytes[n++] = 0xf8;
            put32(bytes + n, immediates[i]); n += 4;
            const uint8_t tails[] = {0x74,1,0xc3,0xb8,9,0,0,0,0xc3};
            memcpy(bytes + n, tails, sizeof(tails));
            compare(bytes, n + sizeof(tails), inputs, 8);
        }
    }
    struct rc_insn live[] = {{RC_INPUT,0},{RC_ADD,1},{RC_SUB,1},
        {RC_CONST,77},{RC_JMP,5},{RC_JZ,7},{RC_RET,0},{RC_RET,0}};
    struct rc_program p = {live,8};
    CHECK(rc_optimize(&p, NULL) == RC_OK && p.count == 7);
    CHECK(live[1].op == RC_ADD && live[1].immediate == 0);
    CHECK(live[3].op == RC_JMP && live[3].immediate == 4);
    CHECK(rc_validate(&p, NULL) == RC_OK);
    struct rc_insn overwritten[] = {{RC_INPUT,0},{RC_ADD,1},{RC_SUB,1},
        {RC_JMP,4},{RC_TEST,0},{RC_JZ,7},{RC_RET,0},{RC_RET,0}};
    p = (struct rc_program){overwritten,8};
    CHECK(rc_optimize(&p, NULL) == RC_OK && p.count == 6);
    CHECK(overwritten[1].op == RC_JMP && overwritten[1].immediate == 2);
    CHECK(rc_validate(&p, NULL) == RC_OK);
    /* The target splits a run; a predecessor can enter at SUB. */
    struct rc_insn split[] = {{RC_INPUT,0},{RC_TEST,0},{RC_JZ,4},
        {RC_ADD,1},{RC_SUB,1},{RC_RET,0}};
    p = (struct rc_program){split,6};
    CHECK(rc_optimize(&p, NULL) == RC_OK && p.count == 6);
    CHECK(split[3].op == RC_ADD && split[3].immediate == 1);
    CHECK(split[4].op == RC_ADD && split[4].immediate == UINT32_MAX);
    /* Liveness crosses the entry/backedge join at the zero-delta run. */
    const uint8_t loop[] = {0x48,0x89,0xf8,0xeb,4,0x48,0x83,0xe8,1,
        0x48,0x83,0xc0,1,0x48,0x83,0xe8,1,0x75,0xf2,0xc3};
    const uint64_t positive[] = {1,2,7,32};
    compare(loop, sizeof(loop), positive, 4);
    struct rc_insn empty_block[] = {{RC_INPUT,0},{RC_JMP,2},{RC_NOP,0},
        {RC_ADD,1},{RC_SUB,1},{RC_RET,0}};
    p = (struct rc_program){empty_block,6};
    CHECK(rc_optimize(&p, NULL) == RC_OK && p.count == 3);
    CHECK(empty_block[1].immediate == 2 && empty_block[2].op == RC_RET);
    CHECK(rc_validate(&p, NULL) == RC_OK);
}

static void test_relocation(void)
{
    struct rc_insn insns[] = {{RC_INPUT,0},{RC_SUB,1},{RC_JNZ,1},{RC_JMP,4},{RC_RET,0}};
    struct rc_program p = {insns,5};
    const uint8_t expected[] = {0xf3,0x0f,0x1e,0xfa,0x48,0x89,0xf8,
        0x48,0x2d,1,0,0,0,0x0f,0x85,0xf4,0xff,0xff,0xff,0xe9,0,0,0,0,0xc3};
    size_t length;
    uint8_t emitted[sizeof(expected) + 1];
    memset(emitted, 0xaa, sizeof(emitted));
    CHECK(rc_emit(&p, NULL, 0, &length, NULL) == RC_OK && length == sizeof(expected));
    CHECK(rc_emit(&p, emitted, length - 1, &length, NULL) == RC_CAPACITY);
    for (size_t i = 0; i < sizeof(emitted); ++i)
        CHECK(emitted[i] == 0xaa);
    CHECK(rc_emit(&p, emitted, sizeof(emitted), &length, NULL) == RC_OK);
    CHECK(memcmp(emitted, expected, length) == 0 && emitted[length] == 0xaa);
    struct rc_program decoded = {0};
    CHECK(rc_decode(emitted + 4, length - 4, &decoded, NULL) == RC_OK);
    CHECK(decoded.count == p.count && decoded.insns[2].immediate == 1);
    rc_program_free(&decoded);
    /* A short input jump expands past 127 emitted bytes after imm8 canonicalization. */
    uint8_t large[140] = {0x48,0x89,0xf8,0x48,0x85,0xc0,0x74,120};
    size_t n = 8;
    for (unsigned i = 0; i < 30; ++i) {
        const uint8_t add[] = {0x48,0x83,0xc0,1};
        memcpy(large + n, add, sizeof(add)); n += sizeof(add);
    }
    large[n++] = 0xc3;
    CHECK(rc_decode(large, n, &decoded, NULL) == RC_OK);
    uint8_t output[256];
    CHECK(rc_emit(&decoded, output, sizeof(output), &length, NULL) == RC_OK);
    CHECK(output[10] == 0x0f && output[11] == 0x84 && read32(output + 12) == 180);
    rc_program_free(&decoded);
    const uint64_t inputs[] = {0,1,UINT64_MAX};
    compare(large, n, inputs, 3);
}

static void test_random_programs(void)
{
    for (unsigned trial = 0; trial < 1000; ++trial) {
        uint8_t bytes[512] = {0x48,0x89,0xf8,0x48,0x85,0xc0};
        size_t offsets[49], patches[48], targets[48];
        size_t patch_count = 0, length = 6;
        for (size_t i = 0; i < 48; ++i) {
            offsets[i] = length;
            unsigned choice = random_u32() % 10;
            if (choice < 3) {
                bytes[length++] = 0x0f;
                bytes[length++] = (choice & 1) != 0 ? 0x84 : 0x85;
                patches[patch_count] = length;
                targets[patch_count++] = i + 1 + random_u32() % (48 - i);
                length += 4;
            } else if (choice < 6) {
                bytes[length++] = 0x48;
                bytes[length++] = choice == 3 ? 0x05 : (choice == 4 ? 0x2d : 0x3d);
                put32(bytes + length, random_u32()); length += 4;
            } else if (choice == 6) {
                bytes[length++] = 0x48; bytes[length++] = 0x85; bytes[length++] = 0xc0;
            } else if (choice == 7) {
                bytes[length++] = 0xb8;
                put32(bytes + length, random_u32()); length += 4;
            } else if (choice == 8) {
                bytes[length++] = 0x48; bytes[length++] = 0x89; bytes[length++] = 0xf8;
            } else {
                bytes[length++] = 0x90;
            }
        }
        offsets[48] = length;
        bytes[length++] = 0xc3;
        for (size_t i = 0; i < patch_count; ++i)
            put32(bytes + patches[i], (uint32_t)(offsets[targets[i]] - (patches[i] + 4)));
        uint64_t inputs[16] = {0,1,UINT64_MAX,UINT64_C(0x8000000000000000)};
        for (size_t i = 4; i < 16; ++i)
            inputs[i] = (uint64_t)random_u32() << 32 | random_u32();
        compare(bytes, length, inputs, 16);
    }
    for (unsigned trial = 0; trial < 200; ++trial) {
        /* Bounded countdowns also vary the folding and compare operations in a loop. */
        uint8_t bytes[] = {0x48,0x89,0xf8,0x48,0x05,0,0,0,0,
            0x48,0x2d,0,0,0,0,0x48,0x83,0xe8,1,
            0x48,0x3d,0,0,0,0,0x75,0xe8,0xc3};
        uint32_t immediate = random_u32();
        put32(bytes + 5, immediate); put32(bytes + 11, immediate);
        uint64_t inputs[] = {1 + random_u32() % 64,1 + random_u32() % 64};
        compare(bytes, sizeof(bytes), inputs, 2);
    }
}

static void run_child(void (*test)(void))
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        alarm(20);
        test();
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_nontermination(void)
{
    const uint8_t infinite[] = {0xeb,0xfe};
    struct rc_code code = {0};
    CHECK(rc_compile(infinite, sizeof(infinite), true, &code, NULL) == RC_OK);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        alarm(1);
        (void)code.function(0);
        _exit(1);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM);
    CHECK(rc_code_free(&code, NULL) == RC_OK);
    struct rc_program program = {0};
    CHECK(rc_decode(infinite, sizeof(infinite), &program, NULL) == RC_OK);
    uint64_t result = 19;
    CHECK(rc_interpret(&program, 0, 100, &result, NULL) == RC_BUDGET && result == 19);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        alarm(1);
        (void)rc_interpret(&program, 0, 0, &result, NULL);
        _exit(1);
    }
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM);
    rc_program_free(&program);
    struct rc_runtime_config config = rc_runtime_default_config();
    config.policy = RC_POLICY_TRACE; config.trace_hot_threshold = 1;
    struct rc_runtime *runtime = NULL;
    rc_handle handle;
    CHECK(rc_runtime_create(&config,&runtime,NULL) == RC_OK);
    CHECK(rc_runtime_register(runtime,infinite,sizeof(infinite),&handle,NULL) == RC_OK);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        alarm(1);
        (void)rc_runtime_call(runtime,handle,0,&result,NULL);
        _exit(1);
    }
    CHECK(waitpid(child,&status,0) == child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM);
    CHECK(rc_runtime_destroy(&runtime,NULL) == RC_OK);
}

int main(void)
{
    test_validation();
    test_truncation();
    run_child(test_encodings);
    run_child(test_flags_and_optimizer);
    run_child(test_relocation);
    run_child(test_random_programs);
    test_nontermination();
    puts("CFG, ZF, relocation, branches, isolated native comparisons and deterministic control-flow programs passed");
    return 0;
}
