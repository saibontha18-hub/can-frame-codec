#ifndef BIT_TIMING_H
#define BIT_TIMING_H

#include <stdint.h>

/*
 * CAN nominal bit-time calculator. Given the clock feeding the CAN cell
 * and the bit rate you want, it hunts for a (BRP, TSEG1, TSEG2, SJW)
 * combination that hits the target.
 *
 * Bit time = Sync(1 TQ) + TSEG1 + TSEG2, all in time quanta; one TQ is
 * BRP clock cycles. Sample point = (1 + TSEG1) / (1 + TSEG1 + TSEG2),
 * and every CAN app note on earth says to keep it around 75-87.5%.
 *
 * This knows nothing about any particular controller's register layout —
 * it just finds good numbers, which you then stuff into BTR0/BTR1 or
 * whatever your part calls them.
 */

typedef struct {
    uint16_t brp;             /* baud-rate prescaler, 1..256 */
    uint8_t  tseg1;           /* prop + phase_seg1, in TQ, 1..16 */
    uint8_t  tseg2;           /* phase_seg2, in TQ, 1..8 */
    uint8_t  sjw;             /* sync jump width, in TQ, 1..4 (<= tseg2) */
    uint32_t actual_bps;      /* bit rate the settings really give */
    uint16_t sample_permille; /* sample point: 750 = 75.0% */
    uint32_t error_ppm;       /* |actual - target| / target, in ppm */
} can_bt_solution_t;

/*
 * Find the best bit-timing settings for `clock_hz` / `target_bps`.
 * Writes the winning solution to `sol` and returns 0. Returns -EINVAL
 * when `sol` is NULL, the clock or target is 0, or the target exceeds
 * the clock (not even 1 TQ per bit is possible).
 *
 * "Best" means: smallest rate error, preferring solutions whose sample
 * point lands in 75.0-87.5%. Ties break toward the larger SJW, which
 * tolerates more oscillator slop on a long bus.
 */
int can_bit_timing_solve(uint32_t clock_hz, uint32_t target_bps,
                          can_bt_solution_t *sol);

#endif /* BIT_TIMING_H */
