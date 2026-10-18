/**
 * @file duo_game.c
 * @brief Lógica pura do Duo — ver duo_game.h.
 */
#include "duo_game.h"

#include <string.h>

static bool seen_get(const uint32_t *s, int i) { return (s[i >> 5] >> (i & 31)) & 1u; }
static void seen_set(uint32_t *s, int i)       { s[i >> 5] |= 1u << (i & 31); }

/* embaralha os `k` primeiros de `v` (Fisher-Yates parcial) */
static void shuffle_head(int *v, int len, int k, duo_rng_t rng)
{
    for (int i = 0; i < k && i < len - 1; i++) {
        int j = rng(i, len - 1);
        int t = v[i]; v[i] = v[j]; v[j] = t;
    }
}

static bool eligible(const duo_theme_t *t, int mode, int i)
{
    return mode == DUO_MODE_CONHECE || !(t->q[i].flags & DUO_F_ONLY_CONHECE);
}

int duo_pick(uint32_t seen[DUO_SEEN_WORDS], const duo_theme_t *t, int mode,
             int n, int *out, duo_rng_t rng)
{
    int pool[DUO_MAX_THEME_Q];
    int got = 0, len = 0, valid = 0;
    int count = t->count > DUO_MAX_THEME_Q ? DUO_MAX_THEME_Q : t->count;

    for (int i = 0; i < count; i++) if (eligible(t, mode, i)) valid++;
    if (n > valid) n = valid;
    if (n <= 0) return 0;

    /* 1. inéditas */
    for (int i = 0; i < count; i++)
        if (eligible(t, mode, i) && !seen_get(seen, i)) pool[len++] = i;
    shuffle_head(pool, len, n, rng);
    for (; got < n && got < len; got++) out[got] = pool[got];

    /* 2. acabaram: ciclo novo, completa com as já vistas que não estão na rodada */
    if (got < n) {
        memset(seen, 0, DUO_SEEN_WORDS * sizeof(uint32_t));
        len = 0;
        for (int i = 0; i < count; i++) {
            if (!eligible(t, mode, i)) continue;
            bool taken = false;
            for (int k = 0; k < got; k++) if (out[k] == i) { taken = true; break; }
            if (!taken) pool[len++] = i;
        }
        shuffle_head(pool, len, n - got, rng);
        for (int k = 0; got < n && k < len; k++) out[got++] = pool[k];
    }

    for (int k = 0; k < got; k++) seen_set(seen, out[k]);
    return got;
}

int duo_percent(int hits, int n)
{
    if (n <= 0) return 0;
    if (hits < 0) hits = 0;
    if (hits > n) hits = n;
    return (hits * 100) / n;
}

typedef struct { int min; const char *txt; } duo_band_t;

/* No COMBINA bater por acaso é mais difícil (4 opções), então os cortes são outros */
static const duo_band_t BANDS_CONHECE[] = {
    { 100, "Leu a mente" },
    {  80, "Conhece de cor" },
    {  60, "Conhece bem" },
    {  40, "Mais ou menos" },
    {  20, "Precisam conversar" },
    {   0, "Quase estranhos" },
};
static const duo_band_t BANDS_COMBINA[] = {
    {  90, "Almas g\xC3\xAAmeas" },
    {  70, "Muito parecidos" },
    {  50, "Combinam" },
    {  30, "Opostos se atraem?" },
    {   0, "\xC3\x81gua e vinho" },
};

const char *duo_phrase(int mode, int pct)
{
    const duo_band_t *b = (mode == DUO_MODE_COMBINA) ? BANDS_COMBINA : BANDS_CONHECE;
    int n = (mode == DUO_MODE_COMBINA)
        ? (int)(sizeof BANDS_COMBINA / sizeof BANDS_COMBINA[0])
        : (int)(sizeof BANDS_CONHECE / sizeof BANDS_CONHECE[0]);
    for (int i = 0; i < n; i++)
        if (pct >= b[i].min) return b[i].txt;
    return b[n - 1].txt;
}

bool duo_is_who(const duo_q_t *q)
{
    return q && q->n == 2 && strcmp(q->opt[0], "P1") == 0 && strcmp(q->opt[1], "P2") == 0;
}
