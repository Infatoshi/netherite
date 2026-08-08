/* CANDIDATE: C reimplementation of MC's fastInvSqrt. Must BITWISE-match golden/Golden.java.
 * This one is already an exact port (tier-0 smoke test). For real kernels you start with a
 * correct-but-slow port here, confirm the match, THEN optimize while keeping the match.
 * Reads one double per line from stdin; prints raw IEEE-754 bits (hex) of each result. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static double fast_inv_sqrt(double p) {
    double d0 = 0.5 * p;
    int64_t i;
    memcpy(&i, &p, sizeof i);
    i = 6910469410427058090LL - (i >> 1);   /* arithmetic shift, matches Java signed >> */
    memcpy(&p, &i, sizeof p);
    p = p * (1.5 - d0 * p * p);
    return p;
}

int main(void) {
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        double x;
        if (sscanf(line, "%lf", &x) != 1) continue;
        double r = fast_inv_sqrt(x);
        uint64_t b;
        memcpy(&b, &r, sizeof b);
        printf("%llx\n", (unsigned long long)b);
    }
    return 0;
}
