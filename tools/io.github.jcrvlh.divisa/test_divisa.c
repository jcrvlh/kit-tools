/**
 * test_divisa.c — testes da lógica pura (divisa_game.c), sem LVGL.
 *
 *   cmake -B build -S . -DKIT_SDK_PATH=<kit>/tools-sdk
 *   cmake --build build && ctest --test-dir build
 */
#include "divisa_game.h"

#include <stdio.h>

static int s_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static uint32_t s_seed = 0xD1715Au;
static int32_t rng(int32_t lo, int32_t hi)
{
    s_seed ^= s_seed << 13; s_seed ^= s_seed >> 17; s_seed ^= s_seed << 5;
    return lo + (int32_t)(s_seed % (uint32_t)(hi - lo + 1));
}

/* Justiça: os dois lados sempre somam o mesmo tempo, e as janelas alternam. */
static void test_plan(void)
{
    int opens[2] = { 0, 0 };
    for (int it = 0; it < 5000; it++) {
        div_plan_t p;
        const div_pace_t *pace = (it & 1) ? &DIV_PACE_FRENETIC : &DIV_PACE_NORMAL;
        div_plan_round(&p, it, pace, rng);
        CHECK(p.pairs >= DIV_PAIRS_MIN && p.pairs <= DIV_PAIRS_MAX);
        CHECK(div_windows(&p) == 2 * p.pairs);
        CHECK(div_side_total_ms(&p, DIV_RED) == div_side_total_ms(&p, DIV_BLUE));
        for (int w = 0; w < div_windows(&p); w++) {
            int ms = div_window_ms(&p, w);
            CHECK(ms >= pace->win_min && ms <= pace->win_max);
            /* dentro do par, um de cada lado */
            if (w & 1) CHECK(div_window_side(&p, w) != div_window_side(&p, w - 1));
            /* o par seguinte é aberto pelo outro */
            if (w >= 2 && !(w & 1)) CHECK(div_window_side(&p, w) != div_window_side(&p, w - 2));
        }
        opens[div_window_side(&p, 0)]++;
        CHECK(div_window_ms(&p, div_windows(&p)) == 0);
    }
    /* rodadas consecutivas alternam quem abre: equilíbrio exato em 5000 rodadas */
    CHECK(opens[0] == opens[1]);
}

static void test_net(void)
{
    div_score_t s = { { 30, 12 }, { 5, 20 } };
    CHECK(div_net(&s, DIV_RED) == 25);
    CHECK(div_net(&s, DIV_BLUE) == 0);      /* desconto não deixa negativo */
}

/* Apuração intercalada: soma bate, divisa fica no meio no empate técnico. */
static void test_count(void)
{
    int a = 37, b = 31;
    int red = 0, blue = 0, h = 448;
    int n = div_count_ticks(a, b);
    CHECK(n == 37);
    for (int k = 0; k < n; k++) {
        int ms = div_tick_ms(k, a, b);
        CHECK(ms >= 40 && ms <= 260);
        if (k < a) red++;
        if (k < b) blue++;
        if (k < b) CHECK(div_top_px(red, blue, h) == h / 2);   /* ainda empatado */
    }
    CHECK(red == a && blue == b);
    CHECK(div_top_px(red, blue, h) < h / 2);                 /* vermelho levou */
    /* acelera no empate, desacelera na virada */
    CHECK(div_tick_ms(0, a, b) > div_tick_ms(b - 1, a, b));
    CHECK(div_tick_ms(b, a, b) < div_tick_ms(a - 1, a, b));
    CHECK(div_count_ticks(0, 0) == 0);
    CHECK(div_tick_ms(0, 0, 5) >= 90);                       /* sem empate técnico */
}

static void test_percent(void)
{
    for (int r = 0; r < 300; r += 7)
        for (int b = 0; b < 300; b += 11) {
            int pr = div_percent(r, b, DIV_RED), pb = div_percent(r, b, DIV_BLUE);
            CHECK(pr + pb == 100);
            CHECK(pr >= 0 && pr <= 100);
            if (r > 0 && b > 0) CHECK(pr > 0 && pr < 100);    /* 100% só com o outro zerado */
            int px = div_top_px(r, b, 448);
            CHECK(px >= 0 && px <= 448);
        }
    CHECK(div_percent(0, 0, DIV_RED) == 50);
    CHECK(div_percent(10, 0, DIV_RED) == 100);
    CHECK(div_percent(199, 1, DIV_RED) == 99);
    CHECK(div_winner(5, 4) == DIV_RED && div_winner(4, 5) == DIV_BLUE && div_winner(3, 3) == DIV_TIE);
}

int main(void)
{
    test_plan();
    test_net();
    test_count();
    test_percent();
    if (s_fail) { printf("%d falha(s)\n", s_fail); return 1; }
    printf("ok\n");
    return 0;
}
