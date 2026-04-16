/*
 * CUDA-GL interop PoC for Netherite.
 *
 * Validates the end-to-end chain we will use to ship agent observations
 * directly between the JVM (which owns the GL context + PBO) and a separate
 * Python training process (which owns its own CUDA context):
 *
 *   1. Producer (JVM-side analog): create a GL PBO and put pixel data in it.
 *   2. Producer: register the PBO with CUDA (cudaGraphicsGLRegisterBuffer).
 *   3. Producer: map it, get the device pointer.
 *   4. Producer: cudaMemcpy DtoD from PBO into a cudaMalloc-backed buffer
 *      (cudaIpcGetMemHandle requires cudaMalloc'd memory, NOT GL-mapped
 *      pointers - see CUDA Runtime API docs).
 *   5. Producer: cudaIpcGetMemHandle on the cudaMalloc buffer; persist the
 *      64-byte handle to a tmp file.
 *   6. Producer: spawn the consumer as a separate process (via execvp), so
 *      the consumer has a fresh CUDA context (CUDA does not tolerate
 *      cudaSetDevice after a bare fork()).
 *   7. Consumer (Python-side analog): cudaIpcOpenMemHandle, cudaMemcpy
 *      DtoH, verify bytes match the producer's pattern.
 *
 * If both processes exit 0, the architecture is validated.
 *
 * Build (on anvil):  make -C csrc/cuda_interop poc
 * Run    (on anvil): DISPLAY=:2 csrc/cuda_interop/build/poc
 */

#include <cuda_runtime.h>

/* GLEW must come before any other GL header (cuda_gl_interop.h pulls in
 * <GL/gl.h>, which would otherwise trip GLEW's "gl.h before glew.h" guard). */
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cuda_gl_interop.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define WIDTH 160
#define HEIGHT 90
#define BYTES_PER_PIXEL 4
#define FRAME_BYTES ((size_t)WIDTH * HEIGHT * BYTES_PER_PIXEL)
#define HANDLE_PATH "/tmp/netherite-poc-handle.bin"

#define CHECK_CUDA(call) do {                                                  \
    cudaError_t _e = (call);                                                   \
    if (_e != cudaSuccess) {                                                   \
        fprintf(stderr, "[CUDA] %s:%d %s -> %s\n",                             \
                __FILE__, __LINE__, #call, cudaGetErrorString(_e));            \
        return 1;                                                              \
    }                                                                          \
} while (0)

#define CHECK_GLFW(cond, msg) do {                                             \
    if (!(cond)) {                                                             \
        const char *desc = NULL;                                               \
        glfwGetError(&desc);                                                   \
        fprintf(stderr, "[GLFW] %s: %s\n", msg, desc ? desc : "(no detail)");  \
        return 1;                                                              \
    }                                                                          \
} while (0)

static unsigned char expected_byte(size_t i) {
    /* Deterministic, non-uniform pattern so any DMA truncation/offset shows up. */
    return (unsigned char)((i * 131u + 17u) & 0xff);
}

static int write_handle(const cudaIpcMemHandle_t *handle) {
    FILE *f = fopen(HANDLE_PATH, "wb");
    if (!f) {
        perror("[parent] fopen handle");
        return 1;
    }
    size_t n = fwrite(handle, sizeof(*handle), 1, f);
    fclose(f);
    if (n != 1) {
        fprintf(stderr, "[parent] short write of handle\n");
        return 1;
    }
    return 0;
}

static int read_handle(cudaIpcMemHandle_t *handle) {
    FILE *f = fopen(HANDLE_PATH, "rb");
    if (!f) {
        perror("[child] fopen handle");
        return 1;
    }
    size_t n = fread(handle, sizeof(*handle), 1, f);
    fclose(f);
    if (n != 1) {
        fprintf(stderr, "[child] short read of handle\n");
        return 1;
    }
    return 0;
}

/*
 * Consumer mode: simulates the Python training process.
 * Reopens an IPC handle from a file the producer wrote, copies the device
 * buffer to host, and verifies the pattern. Exits 0 on success, 1 on mismatch.
 *
 * Started via execvp from the producer so that CUDA inits cleanly here.
 */
static int run_consumer(void) {
    cudaIpcMemHandle_t handle;
    if (read_handle(&handle) != 0) return 1;

    CHECK_CUDA(cudaSetDevice(0));

    void *opened = NULL;
    CHECK_CUDA(cudaIpcOpenMemHandle(&opened, handle,
                                    cudaIpcMemLazyEnablePeerAccess));

    unsigned char *host = malloc(FRAME_BYTES);
    if (!host) {
        fprintf(stderr, "[child] malloc failed\n");
        return 1;
    }
    CHECK_CUDA(cudaMemcpy(host, opened, FRAME_BYTES, cudaMemcpyDeviceToHost));

    int errs = 0;
    size_t first_err = 0;
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        if (host[i] != expected_byte(i)) {
            if (errs == 0) first_err = i;
            errs++;
        }
    }
    if (errs == 0) {
        fprintf(stderr, "[child] verified %zu bytes, OK\n", FRAME_BYTES);
    } else {
        fprintf(stderr,
                "[child] %d byte mismatches (first at %zu: got 0x%02x, want 0x%02x)\n",
                errs, first_err, host[first_err], expected_byte(first_err));
    }

    free(host);
    CHECK_CUDA(cudaIpcCloseMemHandle(opened));
    return errs == 0 ? 0 : 1;
}

static int run_producer(const char *self_path) {
    /* --- 1. GL context (invisible window on Xorg :2) --- */
    CHECK_GLFW(glfwInit(), "glfwInit");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    GLFWwindow *win = glfwCreateWindow(WIDTH, HEIGHT, "netherite-poc", NULL, NULL);
    CHECK_GLFW(win != NULL, "glfwCreateWindow");
    glfwMakeContextCurrent(win);

    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        fprintf(stderr, "[GLEW] init failed: %s\n", glewGetErrorString(glew_err));
        return 1;
    }
    /* GLEW intentionally trips a benign GL_INVALID_ENUM at init; flush it. */
    while (glGetError() != GL_NO_ERROR) { }

    /* --- 2. Create a PBO and fill it with the test pattern --- */
    GLuint pbo = 0;
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, FRAME_BYTES, NULL, GL_STREAM_READ);

    unsigned char *pattern = malloc(FRAME_BYTES);
    if (!pattern) {
        fprintf(stderr, "pattern malloc failed\n");
        return 1;
    }
    for (size_t i = 0; i < FRAME_BYTES; i++) pattern[i] = expected_byte(i);
    glBufferSubData(GL_PIXEL_PACK_BUFFER, 0, FRAME_BYTES, pattern);
    free(pattern);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    fprintf(stderr, "[parent] PBO id=%u, %zu bytes loaded\n",
            (unsigned)pbo, FRAME_BYTES);

    /* --- 3. CUDA setup, register the PBO --- */
    CHECK_CUDA(cudaSetDevice(0));

    struct cudaDeviceProp prop;
    CHECK_CUDA(cudaGetDeviceProperties(&prop, 0));
    fprintf(stderr, "[parent] device: %s (CC %d.%d, unifiedAddressing=%d)\n",
            prop.name, prop.major, prop.minor, prop.unifiedAddressing);
    if (!prop.unifiedAddressing) {
        fprintf(stderr, "[parent] device lacks unified addressing - IPC unsupported\n");
        return 1;
    }

    cudaGraphicsResource_t resource = NULL;
    CHECK_CUDA(cudaGraphicsGLRegisterBuffer(&resource, pbo,
                                            cudaGraphicsRegisterFlagsReadOnly));

    /* --- 4. Allocate the IPC-able destination buffer --- */
    void *ipc_buf = NULL;
    CHECK_CUDA(cudaMalloc(&ipc_buf, FRAME_BYTES));

    /* --- 5. Map PBO, copy DtoD into ipc_buf --- */
    CHECK_CUDA(cudaGraphicsMapResources(1, &resource, 0));

    void *pbo_devptr = NULL;
    size_t pbo_size = 0;
    CHECK_CUDA(cudaGraphicsResourceGetMappedPointer(&pbo_devptr, &pbo_size,
                                                    resource));
    fprintf(stderr, "[parent] PBO mapped: devptr=%p, size=%zu\n",
            pbo_devptr, pbo_size);
    if (pbo_size < FRAME_BYTES) {
        fprintf(stderr, "[parent] mapped size %zu < expected %zu\n",
                pbo_size, FRAME_BYTES);
        return 1;
    }

    CHECK_CUDA(cudaMemcpy(ipc_buf, pbo_devptr, FRAME_BYTES,
                          cudaMemcpyDeviceToDevice));
    CHECK_CUDA(cudaGraphicsUnmapResources(1, &resource, 0));

    /* --- 6. Get IPC handle, persist to a tmp file for the consumer --- */
    cudaIpcMemHandle_t handle;
    CHECK_CUDA(cudaIpcGetMemHandle(&handle, ipc_buf));
    fprintf(stderr, "[parent] cudaIpcGetMemHandle ok\n");
    if (write_handle(&handle) != 0) return 1;

    /* --- 7. Fork+exec the consumer (fresh CUDA context required) --- */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        /* Child: exec ourselves with the consumer flag. exec() wipes the
         * inherited (broken) CUDA state and gives us a clean address space. */
        char *argv[] = { (char *)self_path, "consumer", NULL };
        execvp(self_path, argv);
        perror("execvp");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    int child_rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    fprintf(stderr, "[parent] child exit=%d\n", child_rc);

    /* --- 8. Cleanup --- */
    CHECK_CUDA(cudaGraphicsUnregisterResource(resource));
    CHECK_CUDA(cudaFree(ipc_buf));
    glDeleteBuffers(1, &pbo);
    glfwDestroyWindow(win);
    glfwTerminate();
    unlink(HANDLE_PATH);

    if (child_rc != 0) {
        fprintf(stderr, "[parent] PoC FAILED (child rc=%d)\n", child_rc);
        return 1;
    }
    fprintf(stderr, "[parent] PoC PASSED\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "consumer") == 0) {
        return run_consumer();
    }
    return run_producer(argv[0]);
}
