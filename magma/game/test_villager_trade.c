#include "game/villager_trade.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "villager trade check failed at line %d: %s\n", \
            __LINE__, #c); exit(1); } } while (0)

typedef struct { int profession; long long seed; } Fixture;

static const Fixture FIXTURES[] = {
    {0, 4096}, {0, 6144}, {0, 0}, {0, 256},
    {1, 0}, {2, 0}, {3, 0}, {3, 2},
    {4, 4096}, {4, 0}, {5, 0}
};

static void emit_oracle_rows(void)
{
    for (unsigned k = 0; k < sizeof FIXTURES / sizeof FIXTURES[0]; ++k) {
        JavaRandom random;
        GmVillagerTrade trade;
        jrand_set(&random, FIXTURES[k].seed);
        gm_villager_trade_init(&trade, FIXTURES[k].profession, &random);
        printf("T %d %lld %d %d\n", FIXTURES[k].profession,
               FIXTURES[k].seed, trade.career, trade.offer_count);
        for (int i = 0; i < trade.offer_count; ++i) {
            const GmVillagerOffer *offer = &trade.offers[i];
            printf("O %d %lld %d %d %d %d %d %d %d %d %d\n",
                   FIXTURES[k].profession, FIXTURES[k].seed, i,
                   offer->buy_a.item, offer->buy_a.count, offer->buy_a.meta,
                   offer->buy_b.item, offer->buy_b.count, offer->buy_b.meta,
                   offer->sell.item, offer->sell.count);
        }
        if (trade.offer_count > 0) {
            for (int use = 1; use <= 2; ++use) {
                ICStack first = trade.offers[0].buy_a;
                ICStack second = trade.offers[0].buy_b;
                ICStack output;
                float pitch = 0.0F;
                int xp = 0;
                CHECK(gm_villager_trade_execute(
                    &trade, 0, &first, &second, &output));
                int result = gm_villager_trade_use(
                    &trade, 0, &random, &pitch, &xp);
                union { float f; unsigned int u; } bits = {pitch};
                CHECK(result != 0);
                printf("U %d %lld %d %u %d %d\n",
                       FIXTURES[k].profession, FIXTURES[k].seed, use,
                       bits.u, xp, result == 2 ? 1 : 0);
            }
        }
    }
}

static void test_matching_and_execution(void)
{
    JavaRandom random;
    GmVillagerTrade trade;
    ICStack a, b, out;
    int offer;

    jrand_set(&random, 4096);
    gm_villager_trade_init(&trade, 0, &random);
    CHECK(trade.career == 1 && trade.offer_count == 4);

    a = trade.offers[0].buy_a;
    a.count += 5;
    a.n_enchants = 1;
    a.enchants[0].id = 16;
    a.enchants[0].level = 1;
    b = ic_empty();
    offer = gm_villager_trade_find(&trade, &a, &b, 0);
    CHECK(offer == 0); /* recipe without NBT accepts extra input NBT */
    CHECK(gm_villager_trade_execute(&trade, offer, &a, &b, &out));
    {
        float pitch;
        int xp;
        CHECK(gm_villager_trade_use(&trade, offer, &random, &pitch, &xp) == 2);
        CHECK(xp >= 8 && xp <= 11 && trade.time_until_reset == 40);
    }
    CHECK(a.count == 5 && out.item == 388 && out.count == 1);
    CHECK(trade.offers[0].uses == 1 && trade.wealth == 0);

    for (int i = 1; i < 7; ++i) {
        a = trade.offers[0].buy_a;
        b = ic_empty();
        CHECK(gm_villager_trade_execute(&trade, 0, &a, &b, &out));
        CHECK(gm_villager_trade_use(&trade, 0, &random, NULL, NULL) != 0);
    }
    a = trade.offers[0].buy_a;
    b = ic_empty();
    CHECK(!gm_villager_trade_execute(&trade, 0, &a, &b, &out));
    CHECK(gm_villager_trade_find(&trade, &a, &b, 0) == -1);

    jrand_set(&random, 6144);
    gm_villager_trade_init(&trade, 0, &random);
    CHECK(trade.career == 2 && trade.offer_count == 3);
    a = trade.offers[2].buy_b;  /* reversed two-input trade */
    b = trade.offers[2].buy_a;
    CHECK(gm_villager_trade_execute(&trade, 2, &a, &b, &out));
    CHECK(a.item == 0 && b.item == 0);
    CHECK(out.item == 350 && out.count == 6);
    CHECK(trade.wealth == 0);

    jrand_set(&random, 0);
    gm_villager_trade_init(&trade, 4, &random);
    CHECK(trade.career == 2 && trade.offer_count == 2);
    a = trade.offers[1].buy_a;
    b = ic_empty();
    CHECK(a.item == 388);
    CHECK(gm_villager_trade_execute(&trade, 1, &a, &b, &out));
    CHECK(trade.wealth == trade.offers[1].buy_a.count);

    a = ic_mk(1, 64, 0);
    b = ic_empty();
    CHECK(gm_villager_trade_find(&trade, &a, &b, 0) == -1);
}

int main(void)
{
    test_matching_and_execution();
    emit_oracle_rows();
    return 0;
}
