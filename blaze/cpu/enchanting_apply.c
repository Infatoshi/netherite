/* One real ContainerEnchantment.enchantItem boundary for oracle comparison. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/enchant_table.h"

int main(int argc, char **argv)
{
    if (argc != 8) {
        fprintf(stderr, "item xp_seed power button level lapis player_seed\n");
        return 2;
    }
    int item = atoi(argv[1]);
    int xp_seed = atoi(argv[2]);
    int power = atoi(argv[3]);
    int button = atoi(argv[4]);
    int level = atoi(argv[5]);
    int lapis = atoi(argv[6]);
    long long player_seed = strtoll(argv[7], NULL, 10);
    int kind = et_item_kind_from_id(item);
    EtOffer offer;
    JavaRandom player_random;
    jrand_set(&player_random, (i64)player_seed);
    et_compute_offers(xp_seed, power, kind, &offer);

    printf("{\"offers\":[");
    for (int i = 0; i < 3; ++i)
        printf("%s[%d,%d,%d]", i ? "," : "",
               offer.levels[i], offer.clue_id[i], offer.clue_lvl[i]);
    printf("],");

    int applied = 0;
    EtData list[ET_MAX_LIST];
    int n = 0;
    if (kind >= 0 && button >= 0 && button < 3
            && offer.levels[button] > 0
            && lapis >= button + 1 && level >= button + 1
            && level >= offer.levels[button]) {
        JavaRandom offer_random;
        n = et_get_enchantment_list(
            &offer_random, xp_seed, button, offer.levels[button],
            kind, list, ET_MAX_LIST);
        if (n > 0) {
            applied = 1;
            lapis -= button + 1;
            level -= button + 1;
            xp_seed = (i32)jrand_next(&player_random, 32);
            if (et_item_is_book(kind)) item = 403;
        }
    }
    printf("\"applied\":%s,\"item\":%d,\"count\":1,\"meta\":0,",
           applied ? "true" : "false", item);
    printf("\"enchants\":[");
    for (int i = 0; i < n; ++i)
        printf("%s[%d,%d]", i ? "," : "", list[i].id, list[i].level);
    printf("],\"lapis\":%d,\"level\":%d,\"xp_seed\":%d,",
           lapis, level, xp_seed);
    printf("\"player_seed48\":%llu}\n",
           (unsigned long long)player_random.seed);
    return 0;
}
