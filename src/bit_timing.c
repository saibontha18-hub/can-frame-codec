#include "bit_timing.h"

#include <errno.h>

/* Controller-independent limits most CAN cells agree on. */
#define BT_TQ_MIN    8u   /* sync + tseg1 + tseg2, in TQ */
#define BT_TQ_MAX    25u
#define BT_TSEG1_MAX 16u
#define BT_TSEG2_MAX 8u
#define BT_SJW_MAX   4u
#define BT_BRP_MAX   256u

/* Preferred sample-point window, in permille. */
#define BT_SP_LO     750u
#define BT_SP_HI     875u

int can_bit_timing_solve(uint32_t clock_hz, uint32_t target_bps,
                          can_bt_solution_t *sol)
{
    can_bt_solution_t best;
    int have_best = 0;
    int best_rank = 0;

    if (!sol || clock_hz == 0 || target_bps == 0 || target_bps > clock_hz)
        return -EINVAL;

    /*
     * Exhaustive search: the space is tiny (256 brp x 18 tq x segment
     * splits), so just try everything and keep the winner. Brute force
     * beats cleverness when the table fits in your head.
     */
    for (uint32_t brp = 1; brp <= BT_BRP_MAX; brp++) {
        for (uint32_t tq = BT_TQ_MIN; tq <= BT_TQ_MAX; tq++) {
            uint64_t denom = (uint64_t)brp * tq;
            uint32_t actual = (uint32_t)(clock_hz / denom);
            uint32_t diff = actual > target_bps ? actual - target_bps
                                                : target_bps - actual;
            /* achieved rate vs target, in ppm — no floats needed */
            uint32_t error_ppm =
                (uint32_t)(((uint64_t)diff * 1000000u) / target_bps);

            if (actual == 0)
                continue;

            /*
             * Split the bit time: sync is always 1 TQ, then divide the
             * rest between tseg1 and tseg2. Walk the split so the sample
             * point sweeps the whole range.
             */
            for (uint32_t tseg1 = 1; tseg1 <= BT_TSEG1_MAX; tseg1++) {
                uint32_t tseg2, sjw, sample;
                int in_window, rank;

                if (tseg1 + 1 >= tq)
                    break;
                tseg2 = tq - 1 - tseg1;
                if (tseg2 < 1 || tseg2 > BT_TSEG2_MAX)
                    continue;

                sjw = tseg2 < BT_SJW_MAX ? tseg2 : BT_SJW_MAX;
                sample = (uint16_t)((1000u * (1u + tseg1)) / tq);
                in_window = sample >= BT_SP_LO && sample <= BT_SP_HI;
                /*
                 * Rank: sample point inside 75-87.5% first, then smallest
                 * rate error, then the biggest resync budget (more SJW
                 * forgives oscillator slop on a long bus).
                 */
                rank = in_window ? 0 : 1;
                if (!have_best || rank < best_rank ||
                    (rank == best_rank &&
                     (error_ppm < best.error_ppm ||
                      (error_ppm == best.error_ppm && sjw > best.sjw)))) {
                    best.brp = (uint16_t)brp;
                    best.tseg1 = (uint8_t)tseg1;
                    best.tseg2 = (uint8_t)tseg2;
                    best.sjw = (uint8_t)sjw;
                    best.actual_bps = actual;
                    best.sample_permille = sample;
                    best.error_ppm = error_ppm;
                    best_rank = rank;
                    have_best = 1;
                }
            }
        }
    }

    if (!have_best)
        return -EINVAL; /* unreachable with sane inputs, but stay honest */
    *sol = best;
    return 0;
}
