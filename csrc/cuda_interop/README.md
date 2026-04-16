# csrc/cuda_interop

CUDA-GL interop layer for shipping agent observations directly from the JVM's
OpenGL framebuffer into a Python training process's CUDA tensor, with no CPU
hop.

## Status

**PoC validated on anvil (RTX 3090 + CUDA 13.2).** End-to-end chain proven:
160x90 RGBA buffer (57,600 bytes) round-tripped GL PBO -> CUDA -> IPC handle ->
separate process -> verified bit-exact. See `poc_run.log`.

Not yet wired into `FrameGrabber.java` or `netherite_env.py`.

## Why

Current frame path: `GPU render -> PBO -> glMapBuffer (CPU) -> shmem (CPU) ->
Python mmap (CPU) -> numpy -> GPU (training)`.

Target frame path: `GPU render -> PBO -> cudaGraphicsResourceGetMappedPointer
(GPU) -> DtoD copy into IPC-able cudaMalloc buffer -> Python opens via
cudaIpcOpenMemHandle -> torch.as_tensor (still on GPU)`.

## Architecture constraint

`cudaIpcGetMemHandle` only works on memory allocated with `cudaMalloc`, NOT on
device pointers returned by `cudaGraphicsResourceGetMappedPointer`. This is why
we need an extra `cudaMemcpy(..., cudaMemcpyDeviceToDevice)` from the
GL-mapped PBO into a separate IPC-able buffer. The DtoD copy is essentially
free compared to the existing DtoH + shmem path.

## Build

Linux + CUDA only. `nvcc` and `libglfw3-dev` required.

```bash
cd csrc/cuda_interop
make poc
```

## Run (anvil)

```bash
DISPLAY=:2 csrc/cuda_interop/build/poc
```

Expected output: `[parent] PoC PASSED` (child exit 0).

## Implementation notes

- `cudaGraphicsResourceGetMappedPointer` returns a device pointer, but
  `cudaIpcGetMemHandle` rejects it. The handle must come from a separate
  `cudaMalloc`'d buffer; the GL-mapped pointer is only used as the source of
  a `cudaMemcpyDeviceToDevice` into that buffer. This costs one DtoD per
  frame (~zero overhead vs the existing GPU->CPU shmem path).
- The consumer must run in a process started via `exec` (not just `fork`).
  CUDA contexts do not survive a bare fork: `cudaSetDevice` after fork
  returns `cudaErrorInitializationError`. The PoC fork+execvp's itself with
  a `consumer` argv to model the real architecture (separate Python process).
- GLEW must be included before any header that pulls in `<GL/gl.h>` -
  `cuda_gl_interop.h` does, so `<GL/glew.h>` has to come first.
