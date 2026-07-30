# render-opt

Bit-verified C ports of MC 1.11.2 render compute (40 kernels). Lab is closed; product
rendering continues in `magma`. Catalog + status: `SPEC.md`. History: `../../docs/DEVLOG.md`.

```bash
uv run --no-project python harness/runner.py kernels/<dir>   # PASS = bitwise vs golden
```

Drop-ins: `dropin/`. Whole-frame: `wholeframe/`. Magma extracts a subset into
`../magma/renderkernels/`.
