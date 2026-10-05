#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * CAN 2.0A (standard, 11-bit ID) and 2.0B (extended, 29-bit ID) frame codec.
 *
 * This is the data-link payload layer: it validates frames and serializes
 * them to / from a compact binary form. It does not model the physical
 * bitstream (SOF, CRC field, ACK slot, bit stuffing) — that belongs in a
 * CAN controller peripheral. The wire format here is:
 *
 *   byte 0 : flags — bit7 = extended ID, bit6 = RTR, bits 3:0 = DLC
 *            (bits 5:4 are reserved and must be zero)
 *   standard: bytes 1-2 = 11-bit ID, big-endian
 *   extended: bytes 1-4 = 29-bit ID, big-endian
 *   then DLC data bytes (0-8) — omitted entirely for RTR frames, which
 *   carry no data; there DLC is just the requested length.
 */

#define CAN_STD_ID_MAX  0x7FFu
#define CAN_EXT_ID_MAX  0x1FFFFFFFu
#define CAN_MAX_DLC     8u

typedef struct {
    uint32_t id;       /* 11-bit standard ID, or 29-bit extended ID */
    bool extended;     /* false = CAN 2.0A, true = CAN 2.0B */
    bool rtr;          /* remote transmission request */
    uint8_t dlc;       /* data length, 0..8 (requested length for RTR) */
    uint8_t data[8];
} can_frame_t;

/*
 * Serialize `f` into `buf` (capacity `cap`).
 * Returns bytes written, or negative errno-style: -EINVAL on bad ID/DLC,
 * -ENOSPC when the buffer is too small.
 */
int can_frame_encode(const can_frame_t *f, uint8_t *buf, size_t cap);

/*
 * Parse one frame from `buf` (length `len`).
 * Returns bytes consumed, or -EINVAL on truncated input, bad flags,
 * out-of-range ID, or DLC mismatch.
 */
int can_frame_decode(can_frame_t *f, const uint8_t *buf, size_t len);

/*
 * Compare two frames the way CAN arbitration does: bit by bit, dominant
 * (0) wins. Returns -1 if `a` would win the bus, 1 if `b` would win,
 * 0 if they are arbitration-identical.
 *
 * Order: 11-bit base ID (lower wins); standard beats extended on equal
 * base ID (IDE bit is dominant for standard frames); then the remaining
 * 18 ID bits; a data frame beats a remote frame with the same ID.
 */
int can_frame_arbitration(const can_frame_t *a, const can_frame_t *b);

#endif /* CAN_FRAME_H */
