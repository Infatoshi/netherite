/* The device build of the tick: glibc's gnu/stubs.h picks its list of
 * unimplemented functions by architecture and, for a target that is not
 * x86-64, wants the 32-bit list (libc6-dev-i386); nvptx64 is LP64 like
 * x86-64, so this takes x86-64's. Only __stub_ macros; the engine reads none. */
#include <gnu/stubs-64.h>
