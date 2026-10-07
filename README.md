# can-frame-codec

CAN 2.0A/B frame encode/decode, bus arbitration comparison, and a nominal
bit-timing calculator — in C11, pure software, no hardware in sight.

I kept reaching for a quick way to sanity-check CAN frames on my laptop
without dragging a transceiver and a scope into it. This is the "does my
byte layout even make sense" tool: pack a frame, unpack it, see who would
win arbitration, and get real BRP/TSEG numbers for a given clock instead
of guessing from an app note.

## What it does

- **Frame codec** (`src/can_frame.c`): validate and serialize 11-bit and
  29-bit frames (data + RTR) to a compact binary form, and parse them back.
  Truncated input, reserved flag bits, and out-of-range IDs are rejected
  with `-EINVAL`; short buffers get `-ENOSPC`.
- **Arbitration** (`can_frame_arbitration`): compare two frames the way the
  bus does — dominant bit wins. Lower base ID wins; standard beats extended
  on a tied base ID; data beats remote on a tied ID.
- **Bit timing** (`src/bit_timing.c`): give it the CAN peripheral clock and
  the bit rate you want, and it exhaustively searches BRP / TSEG1 / TSEG2 /
  SJW for the best hit — preferring a 75–87.5% sample point, then smallest
  rate error, then the biggest resync budget. No floats anywhere.

## Build and test

```
make          # builds demo, smoke_test, full_test
make test     # runs both suites
make clean
```

Everything compiles under `gcc -Wall -Wextra -Werror -std=c11 -pedantic`.
The full suite is ~1700 checks: every DLC 0–8 round-tripped (std/ext,
data/RTR), ID boundaries, a multi-frame stream decode, every truncation
length rejected, 500 reproducible randomized round-trips, a full
arbitration priority chain, and bit-timing solutions checked for internal
consistency against textbook clock/rate pairs.

Run `./demo` for a quick tour: encoded wire bytes, an arbitration race,
and solved bit timings.

## Porting to real hardware

The wire format here is a host-side convenience, not the CAN bitstream —
a real controller still owns SOF, CRC, ACK, and bit stuffing. Two pieces
transfer directly, though:

- `can_frame_arbitration()` is exactly the priority rule your TX mailbox
  sorting or software scheduler needs.
- `can_bit_timing_solve()` hands you register-ready numbers: BRP, TSEG1,
  TSEG2, SJW map straight onto BTR0/BTR1 (SJA1000-style) or the NBTP
  register on Bosch M_CAN — just shift them into your part's bitfields.

## Layout

```
src/can_frame.c   frame encode/decode + arbitration
src/bit_timing.c  nominal bit-time solver
app/main.c        demo: wire bytes, arbitration race, timing table
tests/full_test.c the real test suite (smoke_test.c was day one's)
docs/screenshots  real terminal output from the build sessions
```

License: MIT. Written from scratch; mistakes are mine.
