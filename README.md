# can-frame-codec

CAN 2.0A/B frame codec in C11 — pure software, no hardware.

Day 1: frame struct, encode/decode to a compact binary form, and
arbitration comparison (who wins the bus). Day 2 adds a bit-timing
calculator and the full test suite.

## Build

```
make        # builds demo + smoke_test
make test   # runs the smoke test
make clean
```

Strict build: `gcc -Wall -Wextra -Werror -std=c11 -pedantic`.
