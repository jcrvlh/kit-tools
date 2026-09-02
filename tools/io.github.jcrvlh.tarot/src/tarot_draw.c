/**
 * @file tarot_draw.c
 * @brief Implementação do sorteio puro (ver tarot_draw.h).
 */
#include "tarot_draw.h"

int tarot_draw(tarot_pick_t *out, int n, bool reversed_enabled,
               int deck_count, tarot_rng_fn rng)
{
    if (!out || !rng || n < 1 || deck_count < 1) return 0;
    if (n > TAROT_MAX_PICKS) n = TAROT_MAX_PICKS;
    if (n > deck_count)      n = deck_count;

    /* Fisher–Yates parcial: só embaralhamos as `n` primeiras posições de um
     * pool [0..deck_count-1] mantido implicitamente. Como não queremos alocar
     * deck_count ints na pilha de uma Tool, usamos a técnica de "swap contra um
     * mapa esparso": para tiragens de até 3 cartas, um caminho O(n^2) sobre o
     * resultado já garante distinção sem estrutura auxiliar. */
    int picked = 0;
    while (picked < n) {
        int cand = rng(0, deck_count - 1);
        bool dup = false;
        for (int i = 0; i < picked; i++) {
            if (out[i].index == cand) { dup = true; break; }
        }
        if (dup) continue;
        out[picked].index    = cand;
        out[picked].reversed = reversed_enabled && (rng(1, 100) <= TAROT_REVERSED_CHANCE);
        picked++;
    }
    return picked;
}
