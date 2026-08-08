# renderkernels

Bit-exact Minecraft 1.11.2 render kernels, extracted as a reusable C library from the
verified render-opt `candidate.c` programs
(`mc-1.11.2-env/java/render-opt/kernels/<NN_name>/candidate.c`). Each kernel's compute path is
copied VERBATIM (same float/int op order and constants); only the stdin/stdout/`main()`
harness was dropped and replaced by the `rk_*` prototypes in `rk.h`. One `.c` per kernel so
each candidate's file-static helpers keep internal linkage.

Everything must be compiled with `-ffp-contract=off` (mandatory for bit-exactness).

## Public API

See `rk.h`. Ported kernels: 07, 08 (mipmap blend/gen), 12, 13 (AO brightness + helpers),
14, 15 (light query + combine/pack), 18 (biome color blend), 21 (should-side-render),
27 (translucent sort), 28 (vertex pack), 31, 32, 33, 34 (FaceBakery make-quad / fill-vertex
/ rotate / facing+normal).

## Build

```sh
# one object per kernel
for f in renderkernels/rk_*.c; do
  gcc -O2 -ffp-contract=off -Wall -Wextra -I. -c "$f" -o "${f%.c}.o"
done
# link the self-test
gcc -O2 -ffp-contract=off -Wall -Wextra -I. renderkernels/test_rk.c renderkernels/rk_*.o \
    -o renderkernels/test_rk -lm
./renderkernels/test_rk        # exits nonzero on any bitwise mismatch
```

(Run from the repo root `magma/` so `-I.` finds nothing else; `rk_*.c` include `"rk.h"`
via the compiler's implicit same-directory search. A caller outside this dir uses
`-Irenderkernels`.)

## Verification

`test_rk.c` reads the render-opt golden inputs and asserts each `rk_*` output equals the
golden output BITWISE (exact ints / exact IEEE-754 raw bits):

- Kernels 12, 14, 18, 21 use the captured `golden/inputs.txt` + `golden/golden.txt` under
  `render-opt/kernels/<dir>/golden/` (live-hook captures from real Minecraft).
- Kernels 13, 15, 28 ship only a `Golden.java` + `gen_inputs.py` (no captured pair), so
  `testdata/k{13,15,28}_{inputs,golden}.txt` were regenerated here: inputs from the kernel's
  own `gen_inputs.py`, goldens from running its verbatim `Golden.java`. The compare is still
  against real MC arithmetic, bit-for-bit.

Kernels 07, 08, 27, 31, 32, 33, 34 are not in `test_rk.c` (their goldens are `Golden.java`
scripts, awkward to embed) but were cross-checked separately: the `rk_*` output is bitwise
identical to the original verified `candidate.c` binary over each kernel's full
`gen_inputs.py` stream (07: 40000 lines, 08: 61380, 27: 4000, 31/32/33: 20000, 34: 30000).
