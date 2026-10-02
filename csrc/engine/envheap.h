/* The engine's allocations go to the current environment's image heap
 * (image.h) while it is on, and to the process's malloc otherwise. The
 * Makefile includes this header ahead of every source (-include), so
 * malloc, calloc, realloc, free and strdup are these names; a block is freed
 * where it came from, whichever environment is current. It includes no
 * system header (a source's own feature macros must come first), so the C
 * library's declarations that follow declare these names. Memory the
 * process shares between environments (nbtjson.c's interned keys) takes
 * proc_malloc and proc_free, the C library's own. */
#ifndef NETHERITE_ENVHEAP_H
#define NETHERITE_ENVHEAP_H

void *env_malloc(__SIZE_TYPE__ n);
void *env_calloc(__SIZE_TYPE__ n, __SIZE_TYPE__ size);
void *env_realloc(void *p, __SIZE_TYPE__ n);
void env_free_mem(void *p);
char *env_strdup(const char *s);

void *proc_malloc(__SIZE_TYPE__ n);
void *proc_calloc(__SIZE_TYPE__ n, __SIZE_TYPE__ size);
void proc_free(void *p);

/* object-like: the name is renamed wherever it appears, so a struct member
 * called free and a function pointer set to free name these too */
#define malloc env_malloc
#define calloc env_calloc
#define realloc env_realloc
#define free env_free_mem
#define strdup env_strdup

#endif
