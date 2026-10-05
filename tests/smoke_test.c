/*
 * smoke_test: just enough checks to prove day-1's codec isn't broken.
 * The full self-contained suite lands on day 2.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

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

static void test_roundtrip_std(void)
{
    can_frame_t tx = {
        .id = 0x123, .extended = false, .rtr = false, .dlc = 8,
        .data = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE },
    };
    can_frame_t rx;
    uint8_t wire[16];
    int n;

    n = can_frame_encode(&tx, wire, sizeof(wire));
    CHECK(n == 11);
    CHECK(wire[0] == 0x08 && wire[1] == 0x01 && wire[2] == 0x23);

    n = can_frame_decode(&rx, wire, (size_t)n);
    CHECK(n == 11);
    CHECK(rx.id == 0x123 && !rx.extended && !rx.rtr && rx.dlc == 8);
    CHECK(memcmp(rx.data, tx.data, 8) == 0);
}

static void test_roundtrip_ext(void)
{
    can_frame_t tx = {
        .id = 0x1FFFFFFFu, .extended = true, .rtr = false, .dlc = 2,
        .data = { 0xAA, 0x55 },
    };
    can_frame_t rx;
    uint8_t wire[16];
    int n;

    n = can_frame_encode(&tx, wire, sizeof(wire));
    CHECK(n == 7);
    CHECK(wire[0] == 0x82); /* ext flag + dlc 2 */

    n = can_frame_decode(&rx, wire, (size_t)n);
    CHECK(n == 7);
    CHECK(rx.id == 0x1FFFFFFFu && rx.extended && rx.dlc == 2);
    CHECK(rx.data[0] == 0xAA && rx.data[1] == 0x55);
}

static void test_rtr_carries_no_data(void)
{
    can_frame_t tx = {
        .id = 0x200, .extended = false, .rtr = true, .dlc = 8, .data = { 0 },
    };
    can_frame_t rx;
    uint8_t wire[16];
    int n;

    n = can_frame_encode(&tx, wire, sizeof(wire));
    CHECK(n == 3); /* flags + id only, no data bytes */

    n = can_frame_decode(&rx, wire, (size_t)n);
    CHECK(n == 3);
    CHECK(rx.rtr && rx.dlc == 8 && rx.id == 0x200);
}

static void test_error_paths(void)
{
    can_frame_t tx = {
        .id = 0x123, .extended = false, .rtr = false, .dlc = 8, .data = { 0 },
    };
    can_frame_t bad = tx;
    can_frame_t rx;
    uint8_t wire[16];
    uint8_t tiny[2];

    bad.dlc = 9;
    CHECK(can_frame_encode(&bad, wire, sizeof(wire)) == -EINVAL);

    bad = tx;
    bad.id = 0x800; /* doesn't fit in 11 bits */
    CHECK(can_frame_encode(&bad, wire, sizeof(wire)) == -EINVAL);

    CHECK(can_frame_encode(NULL, wire, sizeof(wire)) == -EINVAL);
    CHECK(can_frame_encode(&tx, wire, 0) == -ENOSPC);
    CHECK(can_frame_encode(&tx, tiny, sizeof(tiny)) == -ENOSPC);

    /* Reserved flag bits set. */
    wire[0] = 0x38; wire[1] = 0x01; wire[2] = 0x23;
    CHECK(can_frame_decode(&rx, wire, 11) == -EINVAL);

    /* Truncated input. */
    wire[0] = 0x08; wire[1] = 0x01; wire[2] = 0x23;
    CHECK(can_frame_decode(&rx, wire, 5) == -EINVAL);
    CHECK(can_frame_decode(&rx, wire, 0) == -EINVAL);
}

static void test_arbitration(void)
{
    can_frame_t lo = {
        .id = 0x100, .extended = false, .rtr = false, .dlc = 0, .data = { 0 },
    };
    can_frame_t hi = lo; hi.id = 0x200;
    can_frame_t ext = lo; ext.extended = true; ext.id = 0x100u << 18;
    can_frame_t remote = lo; remote.rtr = true;

    CHECK(can_frame_arbitration(&lo, &hi) < 0);   /* lower id wins */
    CHECK(can_frame_arbitration(&hi, &lo) > 0);
    CHECK(can_frame_arbitration(&lo, &lo) == 0);
    CHECK(can_frame_arbitration(&lo, &ext) < 0);  /* std beats ext, same base */
    CHECK(can_frame_arbitration(&lo, &remote) < 0); /* data beats remote */
}

int main(void)
{
    test_roundtrip_std();
    test_roundtrip_ext();
    test_rtr_carries_no_data();
    test_error_paths();
    test_arbitration();

    printf("smoke: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
