/*
 * demo: exercise the frame codec — encode a few frames, decode one back,
 * run a small arbitration race, and solve bit timing for a few
 * textbook clock/bitrate pairs.
 */
#include <stdio.h>

#include "bit_timing.h"
#include "can_frame.h"

static void dump_bytes(const char *label, const uint8_t *b, int n)
{
    int i;

    printf("%-34s", label);
    for (i = 0; i < n; i++)
        printf(" %02X", b[i]);
    putchar('\n');
}

static void race(const char *label, const can_frame_t *a, const can_frame_t *b)
{
    int w = can_frame_arbitration(a, b);

    printf("%-34s -> %s wins\n", label,
           w < 0 ? "left" : (w > 0 ? "right" : "tie"));
}

int main(void)
{
    /* A plain 11-bit data frame. */
    can_frame_t tx = {
        .id = 0x123, .extended = false, .rtr = false, .dlc = 8,
        .data = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE },
    };
    /* A 29-bit frame (typical J1939-style ID). */
    can_frame_t ext = {
        .id = 0x18DAF110u, .extended = true, .rtr = false, .dlc = 4,
        .data = { 0x01, 0x02, 0x03, 0x04 },
    };
    /* A remote frame asking node 0x200 for 8 bytes. */
    can_frame_t rtr = {
        .id = 0x200, .extended = false, .rtr = true, .dlc = 8, .data = { 0 },
    };
    can_frame_t back;
    uint8_t wire[16];
    int n;

    n = can_frame_encode(&tx, wire, sizeof(wire));
    dump_bytes("std id=0x123 dlc=8", wire, n);

    n = can_frame_encode(&ext, wire, sizeof(wire));
    dump_bytes("ext id=0x18DAF110 dlc=4", wire, n);

    n = can_frame_encode(&rtr, wire, sizeof(wire));
    dump_bytes("rtr id=0x200 req=8", wire, n);

    /* Round-trip the first frame and check it survives. */
    n = can_frame_encode(&tx, wire, sizeof(wire));
    if (can_frame_decode(&back, wire, (size_t)n) == n &&
        back.id == tx.id && back.dlc == tx.dlc && !back.extended && !back.rtr)
        printf("round-trip id=0x123: OK (%d bytes)\n", n);
    else
        printf("round-trip id=0x123: MISMATCH\n");

    putchar('\n');
    printf("arbitration race (lower id wins the bus):\n");
    race("0x123 std data vs 0x200 std rtr", &tx, &rtr);

    {
        can_frame_t std_same = tx;            /* 0x123 standard */
        can_frame_t ext_same = ext;
        ext_same.id = (0x123u << 18) | 0x12345u; /* same base id, extended */

        race("0x123 std vs 0x123-base ext", &std_same, &ext_same);
    }

    {
        can_frame_t data = tx;
        can_frame_t remote = tx;
        remote.rtr = true;

        race("0x123 data vs 0x123 remote", &data, &remote);
    }

    putchar('\n');
    printf("bit timing (clock -> target):\n");

    {
        /* A few pairs every CAN bring-up hits at some point. */
        static const struct { uint32_t clock, rate; } pairs[] = {
            { 48000000u, 500000u },
            { 16000000u, 125000u },
            { 8000000u, 1000000u },
        };
        size_t i;

        printf("%-16s %-10s %4s %6s %6s %4s %9s %8s\n",
               "clock/rate", "achieved", "brp", "tseg1", "tseg2", "sjw",
               "sample%", "err ppm");
        for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
            can_bt_solution_t s;

            if (can_bit_timing_solve(pairs[i].clock, pairs[i].rate, &s) != 0) {
                printf("%u/%u: no solution\n", pairs[i].clock, pairs[i].rate);
                continue;
            }
            printf("%4uMHz/%6ukbps %7uHz %4u %6u %6u %4u %7u.%u %8u\n",
                   pairs[i].clock / 1000000u, pairs[i].rate / 1000u,
                   s.actual_bps, s.brp, s.tseg1, s.tseg2, s.sjw,
                   s.sample_permille / 10, s.sample_permille % 10,
                   s.error_ppm);
        }
    }

    return 0;
}
