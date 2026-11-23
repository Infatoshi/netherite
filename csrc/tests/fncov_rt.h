/* The function coverage build's hook (make test-map): every object of
 * out/native/fncov is compiled with -finstrument-functions-once and
 * -include of this header, so each function (inlined copies included) calls
 * __cyg_profile_func_enter the first time it runs in a process, and the
 * hook appends the function's offset from the executable's start, 8 bytes,
 * to NETHERITE_FNCOV/PID.EXE (tests/testmap.c names them with nm). Without
 * the variable it does nothing. Every object carries a weak copy and the
 * linker keeps one, so no Makefile needs a link change (the history check
 * builds old trees this way); raw system calls, so it includes no system
 * header ahead of a source's own feature macros. Linux x86-64 only; never in
 * the normal build. */
#ifndef NETHERITE_FNCOV_RT_H
#define NETHERITE_FNCOV_RT_H
#if defined(__linux__) && defined(__x86_64__)

#define FNCOV_FN __attribute__((weak, no_instrument_function))
#define FNCOV_IN static inline __attribute__((no_instrument_function))

extern char __executable_start[];
extern char **environ;
__attribute__((weak)) int netherite_fncov_fd = -1;
__attribute__((weak)) int netherite_fncov_off;

FNCOV_IN long fncov_sys(long n, long a, long b, long c, long d)
{
    long r;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return r;
}

FNCOV_IN int fncov_open(void)
{
    const char *dir = 0;
    for (char **e = environ; e && *e && !dir; ++e)
    {
        const char *k = "NETHERITE_FNCOV=", *s = *e;
        while (*k && *s == *k) { ++s; ++k; }
        if (!*k) dir = s;
    }
    if (!dir || !*dir) return -1;
    char exe[1024], path[4096];
    long n = fncov_sys(89, (long)"/proc/self/exe", (long)exe, sizeof exe - 1, 0);   /* readlink */
    if (n <= 0) return -1;
    exe[n] = 0;
    const char *b = exe;
    for (const char *s = exe; *s; ++s)
        if (*s == '/') b = s + 1;
    long pid = fncov_sys(39, 0, 0, 0, 0);   /* getpid */
    char num[24];
    int k = 0;
    do { num[k++] = (char)('0' + pid % 10); pid /= 10; } while (pid && k < 23);
    unsigned p = 0;
    for (const char *s = dir; *s && p < sizeof path - 64; ) path[p++] = *s++;
    path[p++] = '/';
    while (k) path[p++] = num[--k];
    path[p++] = '.';
    for (const char *s = b; *s && p < sizeof path - 1; ) path[p++] = *s++;
    path[p] = 0;
    /* openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644) */
    long fd = fncov_sys(257, -100, (long)path, 01 | 0100 | 02000 | 02000000, 0644);
    return fd < 0 ? -1 : (int)fd;
}

FNCOV_FN void __cyg_profile_func_enter(void *fn, void *site)
{
    (void)site;
    if (netherite_fncov_off) return;
    if (netherite_fncov_fd < 0)
    {
        int fd = fncov_open();
        if (fd < 0) { netherite_fncov_off = 1; return; }
        /* two threads may both open: keep one, close the other */
        if (!__sync_bool_compare_and_swap(&netherite_fncov_fd, -1, fd)) fncov_sys(3, fd, 0, 0, 0);
    }
    unsigned long off = (unsigned long)((char *)fn - __executable_start);
    if (fncov_sys(1, netherite_fncov_fd, (long)&off, sizeof off, 0) != (long)sizeof off) netherite_fncov_off = 1;
}

FNCOV_FN void __cyg_profile_func_exit(void *fn, void *site)
{
    (void)fn;
    (void)site;
}

#endif
#endif
