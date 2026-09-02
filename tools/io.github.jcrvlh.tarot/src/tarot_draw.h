/**
 * @file tarot_draw.h
 * @brief Lógica pura de sorteio de cartas de tarot.
 *
 * Este módulo NÃO depende do LVGL nem da API do KIT — recebe a fonte de
 * aleatoriedade por ponteiro de função, para ser testável no build nativo
 * (ver test_tarot.c). No firmware, a `rng` é um wrapper de
 * `ctx->api->random->range`.
 */
#pragma once

#include <stdbool.h>

/** Chance (%) de uma carta sair invertida quando a opção está ligada. */
#define TAROT_REVERSED_CHANCE 50

/** Máximo de cartas numa tiragem da V1 (três cartas). */
#define TAROT_MAX_PICKS 3

/** Uma carta sorteada: índice no baralho + orientação. */
typedef struct {
    int  index;      /**< 0..deck_count-1 */
    bool reversed;   /**< true = invertida */
} tarot_pick_t;

/**
 * Fonte de aleatoriedade: inteiro uniforme em [lo, hi] (inclusive).
 * Compatível com a assinatura de `kit_random_api_t::range`.
 */
typedef int (*tarot_rng_fn)(int lo, int hi);

/**
 * Sorteia `n` cartas distintas (sem reposição) via Fisher–Yates parcial.
 *
 * @param out              vetor de saída com pelo menos `n` posições.
 * @param n                quantas cartas (1..TAROT_MAX_PICKS, e <= deck_count).
 * @param reversed_enabled se true, cada carta tem TAROT_REVERSED_CHANCE% de vir invertida.
 * @param deck_count       tamanho do baralho (ex.: tarot_deck_count).
 * @param rng              fonte de aleatoriedade (não pode ser NULL).
 * @return número de cartas efetivamente sorteadas (0 em caso de argumento inválido).
 */
int tarot_draw(tarot_pick_t *out, int n, bool reversed_enabled,
               int deck_count, tarot_rng_fn rng);
