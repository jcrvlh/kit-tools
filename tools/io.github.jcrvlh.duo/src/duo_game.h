/**
 * @file duo_game.h
 * @brief Lógica pura do Duo (sem LVGL): sorteio das perguntas com inéditas
 *        primeiro, placar e frase do resultado. Inteiro puro — o .so não
 *        resolve float nem divisão de 64 bits.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DUO_THEME_COUNT 4
#define DUO_MAX_Q       10   /* perguntas por rodada (3, 5, 7 ou 10) */
#define DUO_SEEN_WORDS  3    /* bitmap de "já vista": até 96 perguntas por tema */
#define DUO_MAX_THEME_Q (DUO_SEEN_WORDS * 32)

enum { DUO_MODE_CONHECE = 0, DUO_MODE_COMBINA = 1 };

#define DUO_F_ONLY_CONHECE 0x01   /* fato que os dois sabem: fora do COMBINA */

/** Uma pergunta do banco. `opt` em CAIXA ALTA (vai em mono/display).
 *  `fit72`: bit i ligado = a opção i cabe no kit_display_72 (até 5
 *  caracteres do charset dele) — decide a fonte grande. */
typedef struct {
    const char *q;
    const char *opt[4];
    uint8_t     n;       /* 2 ou 4 */
    uint8_t     fit72;
    uint8_t     flags;   /* DUO_F_* */
} duo_q_t;

typedef struct {
    const duo_q_t *q;
    int            count;
} duo_theme_t;

typedef int (*duo_rng_t)(int lo, int hi);   /* inteiro em [lo, hi] */

/**
 * Sorteia `n` perguntas do tema `t` válidas no `mode`, as inéditas primeiro.
 * `seen` é o bitmap persistido do tema: as escolhidas são marcadas. Quando
 * as inéditas acabam, começa um ciclo novo (o bitmap zera) e a rodada
 * completa com as já vistas. Devolve quantas escreveu em `out`.
 */
int duo_pick(uint32_t seen[DUO_SEEN_WORDS], const duo_theme_t *t, int mode,
             int n, int *out, duo_rng_t rng);

/** % inteiro, arredondado pra baixo (6 de 7 = 85). */
int duo_percent(int hits, int n);

/** Frase curta do resultado, por modo. */
const char *duo_phrase(int mode, int pct);

/** Pergunta "Quem...?" (opções P1 / P2). */
bool duo_is_who(const duo_q_t *q);
