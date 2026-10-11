/**
 * @file trinca_game.c
 * @brief Lógica pura da TRINCA (ver trinca_game.h).
 */
#include "trinca_game.h"

#include <string.h>

/* 6 comuns × 5 paradas + estrela × 2 = 32. Nenhum símbolo encosta em si
 * mesmo (nem na volta 31 → 0): girando, a faixa não "gagueja". */
const uint8_t TRINCA_STRIP[TRINCA_STOPS] = {
    0, 1, 2, 3, 4, 5,
    0, 1, 6, 2, 3, 4,
    5, 0, 1, 2, 3, 4,
    5, 0, 1, 2, 6, 3,
    4, 5, 0, 1, 2, 3,
    4, 5,
};

#define DIST_ONE  (1u << 30)

int trinca_weight(uint8_t sym)
{
    int w = 0;
    for (int i = 0; i < TRINCA_STOPS; i++) if (TRINCA_STRIP[i] == sym) w++;
    return w;
}

void trinca_spin(uint8_t stops[TRINCA_REELS], trinca_rng_t rng)
{
    for (int r = 0; r < TRINCA_REELS; r++)
        stops[r] = (uint8_t)rng(0, TRINCA_STOPS - 1);
}

trinca_kind_t trinca_classify(const uint8_t s[TRINCA_REELS])
{
    if (s[0] == s[1] && s[1] == s[2])
        return s[0] == TRINCA_RARE ? TRINCA_RARA : TRINCA_TRINCA;
    if (s[0] == s[1] || s[1] == s[2] || s[0] == s[2]) return TRINCA_DUPLA;
    return TRINCA_NADA;
}

trinca_kind_t trinca_classify_stops(const uint8_t stops[TRINCA_REELS])
{
    uint8_t s[TRINCA_REELS];
    for (int r = 0; r < TRINCA_REELS; r++) s[r] = TRINCA_STRIP[stops[r] % TRINCA_STOPS];
    return trinca_classify(s);
}

int trinca_points(trinca_kind_t k)
{
    switch (k) {
    case TRINCA_DUPLA:  return 1;
    case TRINCA_TRINCA:
    case TRINCA_RARA:   return TRINCA_PTS_MAX;
    default:            return 0;
    }
}

void trinca_odds(uint32_t out[4])
{
    /* 7^3 combinações de símbolo, cada uma com peso w_a·w_b·w_c (soma 32^3) */
    int w[TRINCA_SYMBOLS];
    for (int i = 0; i < TRINCA_SYMBOLS; i++) w[i] = trinca_weight((uint8_t)i);
    out[0] = out[1] = out[2] = out[3] = 0;
    for (int a = 0; a < TRINCA_SYMBOLS; a++)
        for (int b = 0; b < TRINCA_SYMBOLS; b++)
            for (int c = 0; c < TRINCA_SYMBOLS; c++) {
                uint8_t s[3] = { (uint8_t)a, (uint8_t)b, (uint8_t)c };
                out[trinca_classify(s)] += (uint32_t)(w[a] * w[b] * w[c]);
            }
}

/* ------------------------------------------------------- contador de sorte */

void trinca_luck_reset(trinca_luck_t *l)
{
    memset(l, 0, sizeof *l);
    l->dist[0] = DIST_ONE;

    uint32_t o[4];
    trinca_odds(o);
    l->kernel[0] = o[TRINCA_NADA];
    l->kernel[1] = o[TRINCA_DUPLA];
    l->kernel[TRINCA_PTS_MAX] = o[TRINCA_TRINCA] + o[TRINCA_RARA];
}

/* d · k / 32768 sem 64 bits: d <= 2^30 e k <= 2^15 dariam 45 bits. Parte alta
 * e baixa de d em 15 bits cada; os dois produtos cabem em 32. */
static uint32_t scale(uint32_t d, uint32_t k)
{
    return (d >> 15) * k + (((d & 0x7FFFu) * k) >> 15);
}

static void convolve_one(trinca_luck_t *l)
{
    /* nova[s] = Σ_p velha[s-p]·K[p]. Descendo de s, cada velha[s-p] (p >= 0)
     * ainda não foi sobrescrita — dá pra fazer no lugar. */
    int top = TRINCA_PTS_MAX * (l->n_dist + 1);
    for (int s = top; s >= 0; s--) {
        uint32_t v = 0;
        for (int p = 0; p <= TRINCA_PTS_MAX; p++) {
            if (!l->kernel[p] || s - p < 0) continue;
            v += scale(l->dist[s - p], l->kernel[p]);
        }
        l->dist[s] = v;
    }
    l->n_dist++;
}

void trinca_luck_add(trinca_luck_t *l, trinca_kind_t k)
{
    uint8_t pts = (uint8_t)trinca_points(k);

    l->spins++;
    if (k == TRINCA_DUPLA) l->duplas++;
    if (k == TRINCA_TRINCA || k == TRINCA_RARA) l->trincas++;
    if (k == TRINCA_RARA) l->raras++;

    /* janela deslizante: depois de cheia, o giro mais velho sai da soma e a
     * distribuição (de TRINCA_WINDOW giros) fica como está */
    if (l->n_dist < TRINCA_WINDOW) convolve_one(l);
    else                           l->window_sum -= l->ring[l->ring_head];
    l->ring[l->ring_head] = pts;
    l->window_sum += pts;
    l->ring_head = (uint16_t)((l->ring_head + 1) % TRINCA_WINDOW);
}

int trinca_luck_n(const trinca_luck_t *l)
{
    return l->spins < TRINCA_WINDOW ? (int)l->spins : TRINCA_WINDOW;
}

int trinca_luck_permille(const trinca_luck_t *l)
{
    if (trinca_luck_n(l) < TRINCA_MIN_SPINS) return -1;

    int top = TRINCA_PTS_MAX * l->n_dist;
    int s = l->window_sum;
    uint32_t below = 0, total = 0;
    for (int i = 0; i <= top; i++) {
        if (i < s) below += l->dist[i];
        total += l->dist[i];
    }
    /* o truncamento da convolução perde um tiquinho de massa: divide pelo
     * total de fato, não por 2^30. >>12 deixa espaço pro ×1000 em 32 bits. */
    uint32_t num = (below + l->dist[s] / 2) >> 12;
    uint32_t den = total >> 12;
    if (!den) return 500;
    int pm = (int)((num * 1000u + den / 2) / den);
    return pm < 0 ? 0 : (pm > 1000 ? 1000 : pm);
}

uint32_t trinca_expected_x10(uint32_t spins, uint32_t odds)
{
    uint32_t q = spins >> 15, r = spins & 0x7FFFu;
    uint32_t lo = r * odds;                       /* <= 2^30 */
    return q * odds * 10u + (lo >> 15) * 10u + ((lo & 0x7FFFu) * 10u >> 15);
}
