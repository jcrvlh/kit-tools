/**
 * test_duo.c — lógica pura do Duo: sorteio com inéditas primeiro, ciclo novo
 * quando acabam, filtro do COMBINA, % e frases. Roda nativo (ctest).
 */
#include "duo_game.h"
#include "duo_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_fail;
#define CHECK(c, ...) do { if (!(c)) { s_fail++; printf("FALHOU %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned s_seed = 12345;
static int rng(int lo, int hi)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return lo + (int)((s_seed >> 8) % (unsigned)(hi - lo + 1));
}

static bool has(const int *v, int n, int x)
{
    for (int i = 0; i < n; i++) if (v[i] == x) return true;
    return false;
}

static void test_no_repeat_until_exhausted(void)
{
    for (int t = 0; t < DUO_THEME_COUNT; t++) {
        const duo_theme_t *th = &DUO_THEMES[t];
        for (int mode = 0; mode < 2; mode++) {
            int valid = 0;
            for (int i = 0; i < th->count; i++)
                if (mode == DUO_MODE_CONHECE || !(th->q[i].flags & DUO_F_ONLY_CONHECE)) valid++;

            uint32_t seen[DUO_SEEN_WORDS] = {0};
            int used[DUO_MAX_THEME_Q] = {0};
            int out[DUO_MAX_Q];
            int total = 0;
            /* rodadas de 10 até cobrir o tema: nenhuma repete antes disso */
            while (total + 10 <= valid) {
                int n = duo_pick(seen, th, mode, 10, out, rng);
                CHECK(n == 10, "tema %d: pediu 10, veio %d", t, n);
                for (int k = 0; k < n; k++) {
                    CHECK(!used[out[k]], "tema %d modo %d: repetiu %d antes de acabar", t, mode, out[k]);
                    used[out[k]] = 1;
                    CHECK(mode == DUO_MODE_CONHECE || !(th->q[out[k]].flags & DUO_F_ONLY_CONHECE),
                          "COMBINA sorteou pergunta só do ME CONHECE");
                    for (int j = 0; j < k; j++) CHECK(out[j] != out[k], "repetida na mesma rodada");
                }
                total += n;
            }
            /* a rodada que atravessa o fim: sem repetida dentro dela */
            int n = duo_pick(seen, th, mode, 10, out, rng);
            CHECK(n == 10, "virada de ciclo: veio %d", n);
            for (int k = 0; k < n; k++)
                for (int j = 0; j < k; j++) CHECK(out[j] != out[k], "repetida na virada");
            /* as inéditas que sobraram entraram primeiro */
            int left = valid - total;
            for (int i = 0, seen_left = 0; i < th->count; i++) {
                bool ok = mode == DUO_MODE_CONHECE || !(th->q[i].flags & DUO_F_ONLY_CONHECE);
                if (ok && !used[i]) { seen_left++; CHECK(has(out, n, i), "inédita %d ficou de fora", i); }
                if (i == th->count - 1) CHECK(seen_left == left, "contagem de inéditas");
            }
        }
    }
}

static void test_percent_and_phrase(void)
{
    CHECK(duo_percent(6, 7) == 85, "6/7");
    CHECK(duo_percent(7, 7) == 100, "7/7");
    CHECK(duo_percent(0, 5) == 0, "0/5");
    CHECK(duo_percent(1, 3) == 33, "1/3");
    CHECK(strcmp(duo_phrase(DUO_MODE_CONHECE, 100), "Leu a mente") == 0, "frase 100");
    CHECK(strcmp(duo_phrase(DUO_MODE_CONHECE, 85), "Conhece de cor") == 0, "frase 85");
    CHECK(strcmp(duo_phrase(DUO_MODE_CONHECE, 0), "Quase estranhos") == 0, "frase 0");
    CHECK(strcmp(duo_phrase(DUO_MODE_COMBINA, 50), "Combinam") == 0, "frase combina 50");
}

static void test_bank(void)
{
    int who = 0;
    for (int t = 0; t < DUO_THEME_COUNT; t++) {
        const duo_theme_t *th = &DUO_THEMES[t];
        CHECK(th->count > 0 && th->count <= DUO_MAX_THEME_Q, "tema %d: %d perguntas", t, th->count);
        for (int i = 0; i < th->count; i++) {
            const duo_q_t *q = &th->q[i];
            CHECK(q->n == 2 || q->n == 4, "n=%d", q->n);
            for (int o = 0; o < q->n; o++) CHECK(q->opt[o][0] != '\0', "opção vazia: %s", q->q);
            if (duo_is_who(q)) who++;
        }
    }
    CHECK(who > 0, "nenhuma Quem...?");
}

int main(void)
{
    test_bank();
    test_no_repeat_until_exhausted();
    test_percent_and_phrase();
    if (s_fail) { printf("%d falha(s)\n", s_fail); return 1; }
    printf("ok\n");
    return 0;
}
