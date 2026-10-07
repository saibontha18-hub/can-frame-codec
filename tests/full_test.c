/*
 * full_test: the whole codec plus bit timing, checked properly.
 * Codec: exhaustive ID/DLC round-trips, a byte-stream decode, and a
 * few hundred randomized frames. Bit timing: a few textbook clock/rate
 * pairs where the answer is known, plus input validation.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "bit_timing.h"
#include "can_frame.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do {                                            \
        checks++;                                                   \
        if (!(cond)) {                                              \
            printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            failures++;                                             \
        }                                                           \
    } while (0)

/* ---- frame codec ---- */

static int frames_equal(const can_frame_t *a, const can_frame_t *b)
{
    if (a->id != b->id || a->extended != b->extended || a->rtr != b->rtr ||
        a->dlc != b->dlc)
        return 0;
    return memcmp(a->data, b->data, a->rtr ? 0 : a->dlc) == 0;
}

static void test_all_dlc_roundtrip(void)
{
    /* Every legal DLC, standard and extended, data and remote. */
    for (uint32_t dlc = 0; dlc <= 8; dlc++) {
        for (int ext = 0; ext <= 1; ext++) {
            for (int rtr = 0; rtr <= 1; rtr++) {
                can_frame_t tx = {
                    .id = ext ? 0x1ABCDEFu : 0x456,
                    .extended = (bool)ext,
                    .rtr = (bool)rtr,
                    .dlc = (uint8_t)dlc,
                };
                can_frame_t rx;
                uint8_t wire[16];
                int n, m;

                for (uint32_t i = 0; i < dlc; i++)
                    tx.data[i] = (uint8_t)(0x30 + i);

                n = can_frame_encode(&tx, wire, sizeof(wire));
                CHECK(n == (int)((ext ? 5u : 3u) + (rtr ? 0u : dlc)));
                m = can_frame_decode(&rx, wire, (size_t)n);
                CHECK(m == n);
                CHECK(frames_equal(&tx, &rx));
            }
        }
    }
}

static void test_id_boundaries(void)
{
    /* 0x000, 0x001, 0x7FE, 0x7FF for standard; same idea for 29-bit. */
    static const uint32_t std_ids[] = { 0x000u, 0x001u, 0x7FEu, 0x7FFu };
    static const uint32_t ext_ids[] = {
        0x00000000u, 0x00000001u, 0x1FFFFFFEu, 0x1FFFFFFFu,
    };
    size_t i;

    for (i = 0; i < 4; i++) {
        can_frame_t tx = {
            .id = std_ids[i], .extended = false, .rtr = false,
            .dlc = 1, .data = { 0x5A },
        };
        can_frame_t rx;
        uint8_t wire[16];
        int n = can_frame_encode(&tx, wire, sizeof(wire));

        CHECK(n == 4);
        CHECK(can_frame_decode(&rx, wire, (size_t)n) == 4);
        CHECK(rx.id == std_ids[i]);
    }
    for (i = 0; i < 4; i++) {
        can_frame_t tx = {
            .id = ext_ids[i], .extended = true, .rtr = false,
            .dlc = 1, .data = { 0x5A },
        };
        can_frame_t rx;
        uint8_t wire[16];
        int n = can_frame_encode(&tx, wire, sizeof(wire));

        CHECK(n == 6);
        CHECK(can_frame_decode(&rx, wire, (size_t)n) == 6);
        CHECK(rx.id == ext_ids[i]);
    }
}

static void test_stream_decode(void)
{
    /* Three frames back to back in one buffer, like a log capture. */
    can_frame_t f[3] = {
        { .id = 0x100, .dlc = 2, .data = { 1, 2 } },
        { .id = 0x18DAF110u, .extended = true, .dlc = 8,
          .data = { 8, 7, 6, 5, 4, 3, 2, 1 } },
        { .id = 0x7FF, .rtr = true, .dlc = 4 },
    };
    uint8_t stream[64];
    size_t off = 0, pos = 0;
    int i;

    for (i = 0; i < 3; i++) {
        int n = can_frame_encode(&f[i], stream + off, sizeof(stream) - off);
        CHECK(n > 0);
        off += (size_t)n;
    }

    for (i = 0; i < 3; i++) {
        can_frame_t rx;
        int n = can_frame_decode(&rx, stream + pos, off - pos);
        CHECK(n > 0);
        CHECK(frames_equal(&f[i], &rx));
        pos += (size_t)n;
    }
    CHECK(pos == off);
}

static void test_decode_rejects(void)
{
    can_frame_t rx;
    uint8_t wire[16];

    /* DLC nibble of 9..15 is illegal (classic CAN stops at 8). */
    wire[0] = 0x09; wire[1] = 0x01; wire[2] = 0x23;
    memset(wire + 3, 0, 9);
    CHECK(can_frame_decode(&rx, wire, sizeof(wire)) == -EINVAL);

    /* Standard frame carrying an 11-bit-overflow ID. */
    wire[0] = 0x08; wire[1] = 0x08; wire[2] = 0x00; /* id 0x800 */
    memset(wire + 3, 0, 8);
    CHECK(can_frame_decode(&rx, wire, sizeof(wire)) == -EINVAL);

    /* Extended frame with bit 29..31 set. */
    wire[0] = 0x88;
    wire[1] = 0x20; wire[2] = 0x00; wire[3] = 0x00; wire[4] = 0x00;
    memset(wire + 5, 0, 8);
    CHECK(can_frame_decode(&rx, wire, sizeof(wire)) == -EINVAL);

    /* Every truncation length of a full 8-byte standard frame. */
    {
        can_frame_t tx = { .id = 0x123, .dlc = 8,
                           .data = { 1, 2, 3, 4, 5, 6, 7, 8 } };
        int full = can_frame_encode(&tx, wire, sizeof(wire));
        int len;

        CHECK(full == 11);
        for (len = 0; len < full; len++)
            CHECK(can_frame_decode(&rx, wire, (size_t)len) == -EINVAL);
    }
}

/* Deterministic PRNG so the "random" test is reproducible. */
static uint32_t lcg_state = 0xC0FFEEu;
static uint32_t lcg_next(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state >> 8;
}

static void test_random_roundtrip(void)
{
    int i;

    for (i = 0; i < 500; i++) {
        can_frame_t tx, rx;
        uint8_t wire[16];
        uint32_t dlc;
        int n;

        memset(&tx, 0, sizeof(tx));
        tx.extended = (lcg_next() & 1) != 0;
        tx.rtr = (lcg_next() & 4) != 0;
        tx.id = lcg_next() % (tx.extended ? 0x20000000u : 0x800u);
        dlc = lcg_next() % 9u;
        tx.dlc = (uint8_t)dlc;
        for (uint32_t b = 0; b < dlc; b++)
            tx.data[b] = (uint8_t)lcg_next();

        n = can_frame_encode(&tx, wire, sizeof(wire));
        CHECK(n > 0);
        CHECK(can_frame_decode(&rx, wire, (size_t)n) == n);
        CHECK(frames_equal(&tx, &rx));
    }
}

static void test_arbitration_ordering(void)
{
    /* A full priority chain: each frame must beat the next one. */
    can_frame_t chain[5] = {
        { .id = 0x100 },                                            /* lowest id */
        { .id = 0x101 },
        { .id = (0x101u << 18) | 0x3FFFFu, .extended = true },       /* same base, ext */
        { .id = (0x101u << 18) | 0x3FFFFu, .extended = true, .rtr = true },
        { .id = 0x200 },                                            /* highest id */
    };
    int i, j;

    for (i = 0; i < 5; i++) {
        for (j = i + 1; j < 5; j++) {
            CHECK(can_frame_arbitration(&chain[i], &chain[j]) < 0);
            CHECK(can_frame_arbitration(&chain[j], &chain[i]) > 0);
        }
        CHECK(can_frame_arbitration(&chain[i], &chain[i]) == 0);
    }

    /* Standard frame with base ID 0 vs extended with base ID 0. */
    {
        can_frame_t s = { .id = 0 };
        can_frame_t e = { .id = 0, .extended = true };
        CHECK(can_frame_arbitration(&s, &e) < 0);
    }
}

/* ---- bit timing ---- */

static void check_solution(uint32_t clock_hz, uint32_t target_bps,
                           uint32_t max_error_ppm)
{
    can_bt_solution_t s;
    uint32_t tq_total;

    CHECK(can_bit_timing_solve(clock_hz, target_bps, &s) == 0);
    CHECK(s.actual_bps > 0);
    CHECK(s.brp >= 1 && s.brp <= 256);
    CHECK(s.tseg1 >= 1 && s.tseg1 <= 16);
    CHECK(s.tseg2 >= 1 && s.tseg2 <= 8);
    CHECK(s.sjw >= 1 && s.sjw <= 4 && s.sjw <= s.tseg2);

    /* The numbers must be self-consistent with the clock. */
    tq_total = 1u + s.tseg1 + s.tseg2;
    CHECK(tq_total >= 8 && tq_total <= 25);
    CHECK(s.actual_bps == clock_hz / ((uint32_t)s.brp * tq_total));
    CHECK(s.sample_permille == (1000u * (1u + s.tseg1)) / tq_total);
    CHECK(s.error_ppm <= max_error_ppm);
}

static void test_bit_timing(void)
{
    can_bt_solution_t s;

    /* Textbook pairs — these divide exactly, error must be zero. */
    check_solution(48000000u, 500000u, 0);
    check_solution(16000000u, 125000u, 0);
    check_solution(8000000u, 1000000u, 0);
    check_solution(40000000u, 1000000u, 0);

    /* Oddball rate: 33333 bps off 48 MHz still lands well under 0.1%. */
    check_solution(48000000u, 33333u, 1000);

    /* Exact solutions should sit in the 75-87.5% sample window. */
    CHECK(can_bit_timing_solve(48000000u, 500000u, &s) == 0);
    CHECK(s.error_ppm == 0);
    CHECK(s.sample_permille >= 750 && s.sample_permille <= 875);
    CHECK(s.actual_bps == 500000u);

    /* Garbage in. */
    CHECK(can_bit_timing_solve(0, 500000u, &s) == -EINVAL);
    CHECK(can_bit_timing_solve(48000000u, 0, &s) == -EINVAL);
    CHECK(can_bit_timing_solve(1000u, 2000u, &s) == -EINVAL); /* target > clock */
    CHECK(can_bit_timing_solve(48000000u, 500000u, NULL) == -EINVAL);
}

int main(void)
{
    test_all_dlc_roundtrip();
    test_id_boundaries();
    test_stream_decode();
    test_decode_rejects();
    test_random_roundtrip();
    test_arbitration_ordering();
    test_bit_timing();

    printf("full: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
