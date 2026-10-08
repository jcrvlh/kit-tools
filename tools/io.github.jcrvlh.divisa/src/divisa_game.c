/**
 * @file divisa_game.c
 * @brief Lógica pura da DIVISA (ver divisa_game.h).
 */
#include "divisa_game.h"

const div_pace_t DIV_PACE_NORMAL   = { 600, 1500 };
const div_pace_t DIV_PACE_FRENETIC = { 400, 900 };

void div_plan_round(div_plan_t *p, int round_idx, const div_pace_t *pace, div_rng_t rng)
{
    p->pairs = (uint8_t)rng(DIV_PAIRS_MIN, DIV_PAIRS_MAX);
    p->first = (uint8_t)(round_idx & 1);
    for (int k = 0; k < DIV_PAIRS_MAX; k++)
        p->win_ms[k] = (k < p->pairs) ? (uint16_t)rng(pace->win_min, pace->win_max) : 0;
}

int div_windows(const div_plan_t *p) { return 2 * p->pairs; }

int div_window_side(const div_plan_t *p, int w)
{
    int k = w >> 1;
    int opener = (p->first + k) & 1;          /* quem abre alterna a cada par */
    return (w & 1) ? 1 - opener : opener;
}

int div_window_ms(const div_plan_t *p, int w)
{
    int k = w >> 1;
    return (k >= 0 && k < p->pairs) ? p->win_ms[k] : 0;
}

int div_side_total_ms(const div_plan_t *p, int side)
{
    int t = 0;
    for (int w = 0; w < div_windows(p); w++)
        if (div_window_side(p, w) == side) t += div_window_ms(p, w);
    return t;
}

int div_net(const div_score_t *s, int side)
{
    int v = s->taps[side] - s->pen[side];
    return v > 0 ? v : 0;
}

int div_count_ticks(int a, int b) { return a > b ? a : b; }

int div_tick_ms(int k, int a, int b)
{
    int common = a < b ? a : b;
    int n = div_count_ticks(a, b);
    if (k < common) {
        /* empate técnico: acelera de 120 pra 40 ms */
        return 120 - (80 * k) / (common > 0 ? common : 1);
    }
    /* um já acabou: a virada desacelera de 90 pra 260 ms */
    int extra = n - common;
    int j = k - common;
    return 90 + (170 * j) / (extra > 0 ? extra : 1);
}

int div_top_px(int red, int blue, int h)
{
    int tot = red + blue;
    if (tot <= 0) return h / 2;
    return (h * blue) / tot;
}

int div_percent(int red, int blue, int side)
{
    int tot = red + blue;
    if (tot <= 0) return 50;
    int pr = (red * 100 + tot / 2) / tot;     /* arredonda o vermelho */
    if (pr == 100 && blue > 0) pr = 99;       /* 100% só se o outro zerou */
    if (pr == 0 && red > 0) pr = 1;
    return side == DIV_RED ? pr : 100 - pr;
}

int div_winner(int red, int blue)
{
    if (red > blue) return DIV_RED;
    if (blue > red) return DIV_BLUE;
    return DIV_TIE;
}
