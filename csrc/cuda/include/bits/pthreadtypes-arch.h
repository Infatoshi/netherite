/* The device build of the tick: glibc's x86-64 pthreadtypes-arch.h without
 * the architecture test (nvptx64 is not __x86_64__, and the header's other
 * branch is i386's: other sizes and a regparm attribute nvptx rejects). The
 * sizes are the host's, so a struct holding a pthread type has one layout
 * on both sides; the device calls no pthread function (nw-stub flags one). */
#if defined(NW_MESH_DEVICE)
/* the mesher's build reads the system's, as it did from its own copy of
 * these stand-ins (cuda/include/pthread.h) */
#include_next <bits/pthreadtypes-arch.h>
#elif !defined(_BITS_PTHREADTYPES_ARCH_H)
#define _BITS_PTHREADTYPES_ARCH_H 1
#include <bits/wordsize.h>
#define __SIZEOF_PTHREAD_MUTEX_T 40
#define __SIZEOF_PTHREAD_ATTR_T 56
#define __SIZEOF_PTHREAD_RWLOCK_T 56
#define __SIZEOF_PTHREAD_BARRIER_T 32
#define __SIZEOF_PTHREAD_MUTEXATTR_T 4
#define __SIZEOF_PTHREAD_COND_T 48
#define __SIZEOF_PTHREAD_CONDATTR_T 4
#define __SIZEOF_PTHREAD_RWLOCKATTR_T 8
#define __SIZEOF_PTHREAD_BARRIERATTR_T 4
#define __LOCK_ALIGNMENT
#define __ONCE_ALIGNMENT
#endif
