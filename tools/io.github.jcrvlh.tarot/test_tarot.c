/**
 * @file test_tarot.c
 * @brief Testes da lógica pura de sorteio (tarot_draw.c) e sanidade do baralho.
 *
 * Build nativo — não usa LVGL nem a API do KIT.
 *   cmake -B build -S . -DKIT_SDK_PATH=<kit>/tools-sdk && cmake --build build
 *   ./build/test_tarot
 */
#include "tarot_draw.h"
#include "tarot_deck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int rng_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(rand() % (unsigned)(hi - lo + 1));
}

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do {                                        \
        if (cond) { passed++; }                                      \
        else { failed++; printf("  FALHOU: %s\n", (msg)); }          \
    } while (0)

int main(void)
{
    srand((unsigned)time(NULL));
    printf("=== test_tarot ===\n");
    printf("cartas no baralho: %d\n", tarot_deck_count);

    /* --- Sanidade do baralho: nenhum campo NULL, nome não vazio ---------- */
    for (int i = 0; i < tarot_deck_count; i++) {
        const tarot_card_t *c = &tarot_deck[i];
        CHECK(c->name && c->name[0], "carta com name vazio");
        CHECK(c->arcana && c->about && c->keywords, "carta com campo base NULL");
        CHECK(c->upright && c->reversed, "carta sem significado");
        CHECK(c->as_past && c->as_present && c->as_future, "carta sem lente de posição");
    }

    /* --- Uma carta ----------------------------------------------------- */
    for (int t = 0; t < 2000; t++) {
        tarot_pick_t p[1];
        int n = tarot_draw(p, 1, false, tarot_deck_count, rng_range);
        CHECK(n == 1, "tarot_draw(1) não devolveu 1 carta");
        CHECK(p[0].index >= 0 && p[0].index < tarot_deck_count, "índice fora do baralho");
        CHECK(!p[0].reversed, "carta invertida com reversed_enabled=false");
    }

    /* --- Três cartas: sem repetição ---------------------------------------- */
    for (int t = 0; t < 5000; t++) {
        tarot_pick_t p[3];
        int n = tarot_draw(p, 3, true, tarot_deck_count, rng_range);
        CHECK(n == 3, "tarot_draw(3) não devolveu 3 cartas");
        CHECK(p[0].index != p[1].index && p[1].index != p[2].index &&
              p[0].index != p[2].index, "carta repetida numa tiragem de 3");
    }

    /* --- Chance de inversão perto de TAROT_REVERSED_CHANCE% ------------- */
    {
        int rev = 0, total = 20000;
        for (int t = 0; t < total; t++) {
            tarot_pick_t p[1];
            tarot_draw(p, 1, true, tarot_deck_count, rng_range);
            if (p[0].reversed) rev++;
        }
        double pct = 100.0 * rev / total;
        printf("invertidas: %.1f%% (alvo %d%%)\n", pct, TAROT_REVERSED_CHANCE);
        CHECK(pct > TAROT_REVERSED_CHANCE - 5 && pct < TAROT_REVERSED_CHANCE + 5,
              "frequência de inversão fora de ±5 pontos do alvo");
    }

    /* --- Argumentos inválidos ---------------------------------------------- */
    {
        tarot_pick_t p[3];
        CHECK(tarot_draw(NULL, 1, false, tarot_deck_count, rng_range) == 0, "out=NULL deveria dar 0");
        CHECK(tarot_draw(p, 1, false, tarot_deck_count, NULL) == 0, "rng=NULL deveria dar 0");
        CHECK(tarot_draw(p, 0, false, tarot_deck_count, rng_range) == 0, "n=0 deveria dar 0");
        CHECK(tarot_draw(p, 99, false, tarot_deck_count, rng_range) == TAROT_MAX_PICKS,
              "n grande deveria saturar em TAROT_MAX_PICKS");
    }

    printf("\n%d ok, %d falhas\n", passed, failed);
    return failed ? 1 : 0;
}
