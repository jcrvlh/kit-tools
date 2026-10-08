/**
 * test_cilada.c — testes da lógica pura (cilada_game.c), sem LVGL.
 *
 *   cmake -B build -S . -DKIT_SDK_PATH=<kit>/tools-sdk
 *   cmake --build build && ctest --test-dir build
 */
#include "cilada_game.h"

#include <stdio.h>
#include <stdlib.h>

static int s_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

/* xorshift determinístico — o teste não depende do rand() da libc */
static uint32_t s_seed = 0x1234567u;
static int32_t rng(int32_t lo, int32_t hi)
{
    s_seed ^= s_seed << 13; s_seed ^= s_seed >> 17; s_seed ^= s_seed << 5;
    return lo + (int32_t)(s_seed % (uint32_t)(hi - lo + 1));
}

static void test_new_round(void)
{
    for (int it = 0; it < 2000; it++) {
        int n = (it & 1) ? 16 : 9;
        bool cards = (it % 3) != 0;
        cilada_round_t r;
        cilada_new_round(&r, n, cards, rng);
        CHECK(r.n == n);
        CHECK(r.trap >= 0 && r.trap < n);
        CHECK(r.opened == 0 && r.opens_left == 1 && r.shields == 0 && r.defused == -1);
        int k = 0;
        for (int c = 0; c < CILADA_CARD_COUNT; c++) {
            if (r.card_at[c] < 0) continue;
            k++;
            CHECK(r.card_at[c] < n);
            CHECK(r.card_at[c] != r.trap);
            for (int d = 0; d < c; d++) CHECK(r.card_at[d] != r.card_at[c]);
        }
        CHECK(k == (cards ? (n == 16 ? 3 : 2) : 0));
        CHECK(r.tremor == -1 || (r.tremor >= 0 && r.tremor < n));
    }
}

/* Abre todos os furos seguros em ordem; só a cilada sobra. */
static void test_open_all(void)
{
    cilada_round_t r;
    cilada_new_round(&r, 9, true, rng);
    int safe = 0, cards = 0;
    for (int i = 0; i < 9; i++) {
        if (i == r.trap) continue;
        int card;
        r.opens_left = 1;
        cilada_hit_t h = cilada_open(&r, i, &card);
        CHECK(h == CILADA_SAFE || h == CILADA_CARD);
        if (h == CILADA_CARD) { cards++; CHECK(card >= 0); } else safe++;
        CHECK(cilada_open(&r, i, NULL) == CILADA_INVALID);   /* não abre duas vezes */
    }
    CHECK(cards == 2 && safe == 6);
    CHECK(cilada_closed(&r) == 1);
    CHECK(cilada_open(&r, r.trap, NULL) == CILADA_TRAP);
    CHECK(cilada_closed(&r) == 0);
}

static void place(cilada_round_t *r, int trap, int espia, int aponta, int mais1, int escudo)
{
    cilada_new_round(r, 9, false, rng);
    r->trap = (int8_t)trap;
    r->card_at[CILADA_ESPIA] = (int8_t)espia;
    r->card_at[CILADA_APONTA] = (int8_t)aponta;
    r->card_at[CILADA_MAIS1] = (int8_t)mais1;
    r->card_at[CILADA_ESCUDO] = (int8_t)escudo;
}

static void test_cards_and_turns(void)
{
    cilada_round_t r;
    int card;
    place(&r, 8, 0, 1, 2, 3);

    /* +1: a vez seguinte abre 2 */
    CHECK(cilada_open(&r, 2, &card) == CILADA_CARD && card == CILADA_MAIS1);
    CHECK(cilada_turn_over(&r));
    cilada_next_turn(&r, rng);
    CHECK(r.opens_left == 2 && r.next_extra == 0);

    /* primeira abertura não fecha a vez; a segunda (ESPIA) deixa espiada pendente */
    CHECK(cilada_open(&r, 4, &card) == CILADA_SAFE);
    CHECK(!cilada_turn_over(&r) && r.opens_left == 1);
    CHECK(cilada_open(&r, 0, &card) == CILADA_CARD && card == CILADA_ESPIA);
    CHECK(r.spy && !cilada_turn_over(&r));
    CHECK(cilada_spy(&r, 8) == true);              /* espiou a cilada */
    CHECK(cilada_closed(&r) == 6);                 /* espiar não abre */
    CHECK(cilada_turn_over(&r));
    cilada_next_turn(&r, rng);
    CHECK(r.opens_left == 1);

    /* APONTA liga point; passar a vez desliga */
    CHECK(cilada_open(&r, 1, &card) == CILADA_CARD && card == CILADA_APONTA);
    CHECK(r.point && cilada_turn_over(&r));
    cilada_next_turn(&r, rng);
    CHECK(!r.point);

    /* ESCUDO guarda; cair na cilada gasta e a cilada muda pra um furo fechado */
    CHECK(cilada_open(&r, 3, &card) == CILADA_CARD && card == CILADA_ESCUDO);
    CHECK(r.shields == 1);
    cilada_next_turn(&r, rng);
    CHECK(cilada_open(&r, 8, &card) == CILADA_TRAP);
    CHECK(cilada_use_shield(&r, rng));
    CHECK(r.shields == 0 && r.defused == 8);
    CHECK(r.trap != 8 && !cilada_is_open(&r, r.trap));
    CHECK(cilada_turn_over(&r));
}

static void test_shield_edges(void)
{
    cilada_round_t r;
    /* só sobrou a cilada: o escudo não tem onde esconder -> grade nova */
    place(&r, 4, -1, -1, -1, -1);
    r.opened = (uint16_t)(0x1FF & ~(1u << 4));
    r.shields = 1;
    CHECK(cilada_open(&r, 4, NULL) == CILADA_TRAP);
    CHECK(!cilada_use_shield(&r, rng));

    /* só sobraram furos com carta: a cilada come a carta */
    place(&r, 4, 5, -1, -1, -1);
    r.opened = (uint16_t)(0x1FF & ~((1u << 4) | (1u << 5)));
    r.shields = 1;
    CHECK(cilada_open(&r, 4, NULL) == CILADA_TRAP);
    CHECK(cilada_use_shield(&r, rng));
    CHECK(r.trap == 5 && r.card_at[CILADA_ESPIA] == -1);

    /* com furo comum fechado, nunca cai em cima de carta */
    for (int it = 0; it < 500; it++) {
        place(&r, 0, 1, 2, -1, -1);
        r.shields = 1;
        cilada_open(&r, 0, NULL);
        CHECK(cilada_use_shield(&r, rng));
        CHECK(r.trap != 1 && r.trap != 2 && r.trap != 0);
    }
}

/* O tremor cai na cilada com peso 2: ~2/(fechados+1) das vezes que treme. */
static void test_tremor_weight(void)
{
    cilada_round_t r;
    int shakes = 0, on_trap = 0;
    for (int it = 0; it < 200000; it++) {
        cilada_new_round(&r, 9, false, rng);
        if (r.tremor < 0) continue;
        shakes++;
        if (r.tremor == r.trap) on_trap++;
        CHECK(!cilada_is_open(&r, r.tremor));
    }
    int frac_pm = (on_trap * 1000) / shakes;          /* esperado 200 (2/10) */
    int shake_pm = (shakes * 1000) / 200000;          /* esperado 500 */
    printf("tremor: %d%% das vezes, %d.%d%% na cilada\n", shake_pm / 10, frac_pm / 10, frac_pm % 10);
    CHECK(frac_pm > 190 && frac_pm < 210);
    CHECK(shake_pm > 490 && shake_pm < 510);

    /* com só a cilada fechada, nunca treme */
    place(&r, 3, -1, -1, -1, -1);
    r.opened = (uint16_t)(0x1FF & ~(1u << 3));
    for (int it = 0; it < 100; it++) { cilada_roll_tremor(&r, rng); CHECK(r.tremor == -1); }
}

static void test_pack(void)
{
    for (int it = 0; it < 3000; it++) {
        cilada_round_t a, b;
        cilada_new_round(&a, (it & 1) ? 16 : 9, it % 2 == 0, rng);
        for (int k = 0; k < 5; k++) {
            int i = rng(0, a.n - 1);
            if (i != a.trap) cilada_open(&a, i, NULL);
        }
        a.next_extra = (uint8_t)(it & 1);
        a.opens_left = (uint8_t)rng(0, 2);
        a.shields = (uint8_t)rng(0, 1);
        a.point = (it & 4) != 0;
        if (it % 7 == 0) {
            /* desarma por escudo de vez em quando */
            a.shields = 1;
            cilada_open(&a, a.trap, NULL);
            cilada_use_shield(&a, rng);
        }
        int32_t w[2];
        cilada_pack(&a, w);
        CHECK(cilada_unpack(&b, w));
        CHECK(a.n == b.n && a.opened == b.opened && a.trap == b.trap);
        CHECK(a.opens_left == b.opens_left && a.next_extra == b.next_extra);
        CHECK(a.shields == b.shields && a.spy == b.spy && a.point == b.point);
        CHECK(a.defused == b.defused);
        for (int c = 0; c < CILADA_CARD_COUNT; c++) CHECK(a.card_at[c] == b.card_at[c]);
    }
    int32_t zero[2] = { 0, 0 };
    cilada_round_t r;
    CHECK(!cilada_unpack(&r, zero));   /* nada salvo */
}

int main(void)
{
    test_new_round();
    test_open_all();
    test_cards_and_turns();
    test_shield_edges();
    test_tremor_weight();
    test_pack();
    if (s_fail) { printf("%d falha(s)\n", s_fail); return 1; }
    printf("ok\n");
    return 0;
}
