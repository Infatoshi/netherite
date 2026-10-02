/* The device mesher's build (cuda/meshing, NW_MESH_DEVICE): render_blocks.c's
 * one use of pthreads is pthread_once over its block class table; glibc's
 * pthread.h does not parse for nvptx64 (regparm). The device runtime
 * (meshing/kernels.c) runs the once function in its setup kernel, before any
 * mesher thread. The tick's build reads glibc's (with
 * bits/pthreadtypes-arch.h here). */
#if !defined(NW_MESH_DEVICE)
#include_next <pthread.h>
#elif !defined(NW_MESH_PTHREAD_H)
#define NW_MESH_PTHREAD_H
typedef int pthread_once_t;
#define PTHREAD_ONCE_INIT 0
int pthread_once(pthread_once_t *once, void (*fn)(void));
#endif
