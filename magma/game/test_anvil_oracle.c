#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>

static int parse_int(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < -32768 || value > 4095)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_stack(int argc, char **argv, int *at, ICStack *out)
{
    int item, count, meta, repair, name, n;
    if (*at + 6 > argc
            || !parse_int(argv[(*at)++], &item)
            || !parse_int(argv[(*at)++], &count)
            || !parse_int(argv[(*at)++], &meta)
            || !parse_int(argv[(*at)++], &repair)
            || !parse_int(argv[(*at)++], &name)
            || !parse_int(argv[(*at)++], &n)
            || item < 0 || count < 0 || count > 64 || meta < 0
            || repair < 0 || name < 0 || n < 0 || n > IC_MAX_ENCHANTS
            || *at + n * 2 > argc)
        return 0;
    *out = count == 0 ? ic_empty() : ic_mk(item, count, meta);
    out->repair_cost = repair;
    out->custom_name = name;
    out->n_enchants = n;
    for (int i = 0; i < n; ++i) {
        int id, level;
        if (!parse_int(argv[(*at)++], &id)
                || !parse_int(argv[(*at)++], &level)
                || id < 0 || level <= 0)
            return 0;
        out->enchants[i].id = (i16)id;
        out->enchants[i].level = (i16)level;
    }
    return 1;
}

static void print_stack(const ICStack *stack)
{
    printf("{\"item\":%d,\"count\":%d,\"meta\":%d,"
           "\"repair\":%d,\"name\":%d,\"enchants\":[",
           stack->item, stack->count, stack->meta,
           stack->repair_cost, stack->custom_name);
    for (int i = 0; i < stack->n_enchants; ++i) {
        if (i) putchar(',');
        printf("{\"id\":%d,\"level\":%d}",
               stack->enchants[i].id, stack->enchants[i].level);
    }
    printf("]}");
}

int main(int argc, char **argv)
{
    GmAnvilLive anvil;
    int at = 1, creative, desired_name;
    if (argc < 15
            || !parse_int(argv[at++], &creative)
            || !parse_int(argv[at++], &desired_name)
            || (creative != 0 && creative != 1)
            || desired_name < 0) {
        fprintf(stderr, "invalid anvil oracle arguments\n");
        return 2;
    }
    gm_anvil_live_init(&anvil);
    anvil.open = 1;
    if (!parse_stack(argc, argv, &at, &anvil.slots[0])
            || !parse_stack(argc, argv, &at, &anvil.slots[1])
            || at != argc) {
        fprintf(stderr, "invalid anvil oracle stack\n");
        return 2;
    }
    anvil.repaired_name = desired_name;
    gm_anvil_live_recompute(&anvil, creative);
    printf("{\"ok\":true,\"maximum_cost\":%d,\"material_cost\":%d,"
           "\"output\":", anvil.maximum_cost, anvil.material_cost);
    print_stack(&anvil.slots[2]);
    printf(",\"left\":");
    print_stack(&anvil.slots[0]);
    printf(",\"right\":");
    print_stack(&anvil.slots[1]);
    puts("}");
    return 0;
}
