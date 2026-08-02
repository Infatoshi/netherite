# Drop-ins: C kernels into live MC render path

JNI `.so` libs loaded by NetheriteMod (sin, lightmap, biome, AO). Modes: off / native / sabotage
via env or sidecar (see each `Q*Native.java`). Build with JDK8 headers +
`-ffp-contract=off`. Proof: Phase-A unit match + in-game INVOKED log + native vs sabotage
pixel diff. Whole-frame stripcheck under `../wholeframe/`. Catalog: `../SPEC.md`.
