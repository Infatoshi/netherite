/* The environment's scratch stack: a local too large for a stack frame (over
 * 4 KB; make -C csrc stack gates it) lives here instead, taken when its
 * variable is declared and given back when the variable leaves scope, so the
 * tick's own stack stays small (a GPU thread's stack is per thread, times
 * every environment) and the big locals are the environment's memory.
 *
 *     struct surv_state *keep ENV_LOCAL = envstack_take(sizeof *keep);
 *     int *count ENV_LOCAL = envstack_take(2048 * sizeof *count);
 *
 * The memory is not cleared (a local is not either). Takes nest as calls
 * and scopes do, last in first out; do not assign the variable again. Past
 * ENVSTACK_CAP the program stops. */
#ifndef NETHERITE_ENVSTACK_H
#define NETHERITE_ENVSTACK_H

#include <stddef.h>

#define ENVSTACK_CAP (256u << 10)

void *envstack_take(size_t n);
void *envstack_zeroed(size_t n);   /* taken and cleared (a local = {0}) */
void envstack_give(void *var);
#define ENV_LOCAL __attribute__((cleanup(envstack_give)))

#endif
