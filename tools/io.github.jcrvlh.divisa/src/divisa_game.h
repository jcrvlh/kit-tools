/**
 * @file divisa_game.h
 * @brief Lógica pura da DIVISA — sem LVGL, testável no desktop.
 *
 * Duas pessoas, uma metade da tela cada. Cada rodada é uma sequência de PARES
 * de janelas curtas: no par, um toca, depois o outro, pelo MESMO tempo
 * sorteado. Quem abre o par alterna. O número de pares também é sorteado, então
 * ninguém sabe quando a rodada acaba — e os dois somam exatamente o mesmo
 * tempo de toque. O placar fica escondido até a apuração.
 *
 * Inteiro puro: a Tool roda como .so e o elf_loader não resolve float nem
 * divisão de 64 bits.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DIV_RED   0          /* metade de baixo */
#define DIV_BLUE  1          /* metade de cima */
#define DIV_TIE  (-1)

#define DIV_MAX_ROUNDS 5
#define DIV_PAIRS_MIN  4
#define DIV_PAIRS_MAX  7

/** Inteiro em [lo, hi] inclusivo — injetado pela UI (TRNG do KIT). */
typedef int32_t (*div_rng_t)(int32_t lo, int32_t hi);

/** Ritmo: faixa de duração (ms) de cada janela. */
typedef struct { uint16_t win_min, win_max; } div_pace_t;
extern const div_pace_t DIV_PACE_NORMAL;      /* 600-1500 ms */
extern const div_pace_t DIV_PACE_FRENETIC;    /* 400-900 ms  */

typedef struct {
    uint8_t  pairs;                      /* pares de janelas da rodada */
    uint8_t  first;                      /* quem abre o 1º par */
    uint16_t win_ms[DIV_PAIRS_MAX];      /* duração de cada par (igual pros dois) */
} div_plan_t;

typedef struct {
    int16_t taps[2];                     /* toques válidos na própria vez */
    int16_t pen[2];                      /* descontos por encostar fora da vez */
} div_score_t;

/** Sorteia a rodada `round_idx` (quem abre alterna entre rodadas também; a UI
 *  soma uma moeda da partida em `round_idx` pra não ser sempre o mesmo lado). */
void div_plan_round(div_plan_t *p, int round_idx, const div_pace_t *pace, div_rng_t rng);

/** Total de janelas da rodada (2 por par). */
int  div_windows(const div_plan_t *p);
/** Lado e duração da janela `w` (0-based). */
int  div_window_side(const div_plan_t *p, int w);
int  div_window_ms(const div_plan_t *p, int w);
/** Tempo total de toque de um lado na rodada (igual pros dois, por construção). */
int  div_side_total_ms(const div_plan_t *p, int side);

/** Toques líquidos de um lado na rodada (nunca negativo). */
int  div_net(const div_score_t *s, int side);

/* --- apuração -----------------------------------------------------------
 * Por rodada, conta os toques líquidos dos dois em ticks intercalados: no
 * tick k, soma 1 pro vermelho se k < a e 1 pro azul se k < b. A divisa fica
 * parada perto do meio enquanto os dois ainda têm toque pra contar e só
 * pende quando o menor acaba — o suspense é real, não teatro.
 */

/** Ticks pra apurar uma rodada com líquidos (a, b). */
int  div_count_ticks(int a, int b);
/** Intervalo (ms) antes do tick k: acelera no empate, desacelera na virada. */
int  div_tick_ms(int k, int a, int b);

/** Altura (px) do território AZUL (de cima) numa tela de `h` px. */
int  div_top_px(int red, int blue, int h);
/** Percentual inteiro de `side`; os dois sempre somam 100. */
int  div_percent(int red, int blue, int side);
/** DIV_RED, DIV_BLUE ou DIV_TIE. */
int  div_winner(int red, int blue);
