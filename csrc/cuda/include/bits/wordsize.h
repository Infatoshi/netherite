/* The device build of the tick (cuda/tick/tick.mk): glibc's headers are read
 * for an nvptx64 target, which is LP64 like x86-64; this is x86-64's
 * wordsize.h without the architecture test. */
#define __WORDSIZE 64
#define __WORDSIZE_TIME64_COMPAT32 1
#define __SYSCALL_WORDSIZE 64
