/**
 * @file gazua_game.c
 * @brief Lógica pura do GAZUA (ver gazua_game.h).
 */
#include "gazua_game.h"

#include <stdio.h>
#include <string.h>

#define C(k, a, b, n, e, l) { (uint8_t)(k), (uint8_t)(a), (uint8_t)(b), (uint8_t)(n), (uint8_t)(e), (uint8_t)(l) }

/* A ordem desta tabela faz parte da semente: mexer nela muda o puzzle do dia. */
const gz_card_t GZ_CARDS[] = {
    C(GZ_K_VS, 0, 1, 2, 1, 0), C(GZ_K_VS, 0, 3, 3, 1, 0), C(GZ_K_VS, 0, 4, 3, 1, 0), C(GZ_K_PAR, 0, 0, 2, 1, 0),
    C(GZ_K_VS, 1, 1, 2, 1, 0), C(GZ_K_VS, 1, 3, 3, 1, 0), C(GZ_K_VS, 1, 4, 3, 1, 0), C(GZ_K_PAR, 1, 0, 2, 1, 0),
    C(GZ_K_VS, 2, 1, 2, 1, 0), C(GZ_K_VS, 2, 3, 3, 1, 0), C(GZ_K_VS, 2, 4, 3, 1, 0), C(GZ_K_PAR, 2, 0, 2, 1, 0),
    C(GZ_K_CMP, 0, 1, 3, 1, 0), C(GZ_K_CMP, 0, 2, 3, 1, 0), C(GZ_K_CMP, 1, 2, 3, 1, 0),
    C(GZ_K_COUNT, 0, 1, 3, 0, 1), C(GZ_K_COUNT, 0, 3, 3, 0, 1), C(GZ_K_COUNT, 0, 4, 3, 0, 1),
    C(GZ_K_EVENS, 0, 0, 4, 0, 0),
    C(GZ_K_MIN, 0, 0, 3, 0, 1),
    C(GZ_K_MAX, 0, 0, 3, 0, 1),
    C(GZ_K_SUMPAR, 0, 0, 2, 1, 0),
    C(GZ_K_SUM01, 0, 0, 3, 0, 1),
    C(GZ_K_REP, 0, 0, 3, 0, 1),
    C(GZ_K_ORDER, 0, 0, 3, 0, 0),
};
const int GZ_NCARDS = (int)(sizeof GZ_CARDS / sizeof GZ_CARDS[0]);

#define NCARDS_MAX 32
static gz_mask_t s_mask[NCARDS_MAX][GZ_MAX_OPTS];
static bool s_ready;

/* ------------------------------------------------------------ códigos */

int gz_code_index(const uint8_t c[GZ_SHAPES])
{
    return (c[0] - 1) * 25 + (c[1] - 1) * 5 + (c[2] - 1);
}

void gz_code_from_index(int idx, uint8_t c[GZ_SHAPES])
{
    c[0] = (uint8_t)(idx / 25 + 1);
    c[1] = (uint8_t)((idx / 5) % 5 + 1);
    c[2] = (uint8_t)(idx % 5 + 1);
}

/* ------------------------------------------------------------- regras */

static int cmp3(int x, int y) { return x < y ? 0 : (x == y ? 1 : 2); }   /* <, =, > */

bool gz_eval(int card, int opt, const uint8_t c[GZ_SHAPES])
{
    const gz_card_t *k = &GZ_CARDS[card];
    int a = k->a, b = k->b;
    switch (k->kind) {
    case GZ_K_VS:
        if (b == 1) return opt == 0 ? c[a] == 1 : c[a] > 1;
        return cmp3(c[a], b) == opt;
    case GZ_K_PAR:
        return (c[a] % 2 == 0) == (opt == 0);
    case GZ_K_CMP:
        return cmp3(c[a], c[b]) == opt;
    case GZ_K_COUNT: {
        int n = (c[0] == b) + (c[1] == b) + (c[2] == b);
        return n == opt;
    }
    case GZ_K_EVENS: {
        int n = (c[0] % 2 == 0) + (c[1] % 2 == 0) + (c[2] % 2 == 0);
        return n == opt;
    }
    case GZ_K_MIN:
        for (int j = 0; j < GZ_SHAPES; j++) if (j != opt && c[opt] >= c[j]) return false;
        return true;
    case GZ_K_MAX:
        for (int j = 0; j < GZ_SHAPES; j++) if (j != opt && c[opt] <= c[j]) return false;
        return true;
    case GZ_K_SUMPAR:
        return ((c[0] + c[1] + c[2]) % 2 == 0) == (opt == 0);
    case GZ_K_SUM01:
        return cmp3(c[0] + c[1], 6) == opt;
    case GZ_K_REP: {
        int eq = (c[0] == c[1]) + (c[0] == c[2]) + (c[1] == c[2]);   /* 0, 1 ou 3 */
        int distinct = eq == 0 ? 3 : (eq == 1 ? 2 : 1);
        return distinct == 3 - opt;
    }
    case GZ_K_ORDER: {
        bool up = c[0] < c[1] && c[1] < c[2];
        bool down = c[0] > c[1] && c[1] > c[2];
        return opt == 0 ? up : (opt == 1 ? down : (!up && !down));
    }
    }
    return false;
}

/* ------------------------------------------------------------ máscaras */

static void m_all(gz_mask_t *m)
{
    m->w[0] = m->w[1] = m->w[2] = 0xFFFFFFFFu;
    m->w[3] = (1u << (GZ_CODES - 96)) - 1u;
}

static void m_and(gz_mask_t *m, const gz_mask_t *o)
{
    for (int i = 0; i < 4; i++) m->w[i] &= o->w[i];
}

static int m_count(const gz_mask_t *m)
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t x = m->w[i];
        while (x) { x &= x - 1u; n++; }
    }
    return n;
}

static int m_first(const gz_mask_t *m)
{
    for (int k = 0; k < GZ_CODES; k++)
        if ((m->w[k >> 5] >> (k & 31)) & 1u) return k;
    return -1;
}

void gz_init(void)
{
    if (s_ready) return;
    memset(s_mask, 0, sizeof s_mask);
    for (int card = 0; card < GZ_NCARDS && card < NCARDS_MAX; card++)
        for (int o = 0; o < GZ_CARDS[card].nopt; o++)
            for (int k = 0; k < GZ_CODES; k++) {
                uint8_t c[GZ_SHAPES];
                gz_code_from_index(k, c);
                if (gz_eval(card, o, c)) s_mask[card][o].w[k >> 5] |= 1u << (k & 31);
            }
    s_ready = true;
}

/* ------------------------------------------------------------- gerador */

/* mulberry32: 32 bits, só soma/xor/shift/multiplicação de 32 bits */
static uint32_t mb_next(uint32_t *s)
{
    uint32_t t = (*s += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

bool gz_generate(gz_puzzle_t *p, bool easy, uint32_t seed)
{
    gz_init();
    int pool[NCARDS_MAX], npool = 0;
    for (int i = 0; i < GZ_NCARDS; i++)
        if (!easy || GZ_CARDS[i].easy) pool[npool++] = i;

    uint32_t st = seed;
    for (int tries = 0; tries < 20000; tries++) {
        int cs[GZ_VERIFIERS], n = 0;
        while (n < GZ_VERIFIERS) {
            int k = pool[mb_next(&st) % (uint32_t)npool];
            bool dup = false;
            for (int j = 0; j < n; j++) if (cs[j] == k) dup = true;
            if (!dup) cs[n++] = k;
        }

        int ch[GZ_VERIFIERS] = { 0 }, found[GZ_VERIFIERS] = { 0 }, count = 0, code_idx = -1;
        for (;;) {
            gz_mask_t m;
            m_all(&m);
            for (int v = 0; v < GZ_VERIFIERS; v++) m_and(&m, &s_mask[cs[v]][ch[v]]);
            if (m_count(&m) == 1) {
                bool needed = true;   /* nenhum verificador pode sobrar */
                for (int v = 0; v < GZ_VERIFIERS && needed; v++) {
                    gz_mask_t mm;
                    m_all(&mm);
                    for (int j = 0; j < GZ_VERIFIERS; j++)
                        if (j != v) m_and(&mm, &s_mask[cs[j]][ch[j]]);
                    if (m_count(&mm) <= 1) needed = false;
                }
                if (needed) {
                    if (++count > 1) break;
                    memcpy(found, ch, sizeof found);
                    code_idx = m_first(&m);
                }
            }
            int q = 0;
            while (q < GZ_VERIFIERS) {
                if (++ch[q] < GZ_CARDS[cs[q]].nopt) break;
                ch[q] = 0;
                q++;
            }
            if (q == GZ_VERIFIERS) break;
        }

        if (count == 1) {
            for (int v = 0; v < GZ_VERIFIERS; v++) {
                p->card[v] = (uint8_t)cs[v];
                p->secret[v] = (uint8_t)found[v];
            }
            gz_code_from_index(code_idx, p->code);
            return true;
        }
    }
    return false;
}

bool gz_answer(const gz_puzzle_t *p, int v, const uint8_t code[GZ_SHAPES])
{
    return gz_eval(p->card[v], p->secret[v], code);
}

/* -------------------------------------------------------------- rótulos */

static const char *const CMP_SYM[3] = { "<", "=", ">" };

void gz_face(int card, char *out, size_t n)
{
    const gz_card_t *k = &GZ_CARDS[card];
    switch (k->kind) {
    case GZ_K_VS:     snprintf(out, n, "#%d vs %d", k->a, k->b); break;
    case GZ_K_PAR:    snprintf(out, n, "#%d par?", k->a); break;
    case GZ_K_CMP:    snprintf(out, n, "#%d vs #%d", k->a, k->b); break;
    case GZ_K_COUNT:  snprintf(out, n, "quantos %d", k->b); break;
    case GZ_K_EVENS:  snprintf(out, n, "pares"); break;
    case GZ_K_MIN:    snprintf(out, n, "o menor"); break;
    case GZ_K_MAX:    snprintf(out, n, "o maior"); break;
    case GZ_K_SUMPAR: snprintf(out, n, "soma"); break;
    case GZ_K_SUM01:  snprintf(out, n, "#0+#1 vs 6"); break;
    case GZ_K_REP:    snprintf(out, n, "repete?"); break;
    case GZ_K_ORDER:  snprintf(out, n, "ordem"); break;
    default:          snprintf(out, n, "?"); break;
    }
}

void gz_opt_label(int card, int opt, char *out, size_t n)
{
    static const char *const NUM[4] = { "nenhum", "um", "dois", "três" };
    const gz_card_t *k = &GZ_CARDS[card];
    switch (k->kind) {
    case GZ_K_VS:
        if (k->b == 1) snprintf(out, n, "#%d %s 1", k->a, opt == 0 ? "=" : ">");
        else           snprintf(out, n, "#%d %s %d", k->a, CMP_SYM[opt], k->b);
        break;
    case GZ_K_PAR:    snprintf(out, n, "#%d é %s", k->a, opt == 0 ? "par" : "ímpar"); break;
    case GZ_K_CMP:    snprintf(out, n, "#%d %s #%d", k->a, CMP_SYM[opt], k->b); break;
    case GZ_K_COUNT:  snprintf(out, n, "%s %d", NUM[opt], k->b); break;
    case GZ_K_EVENS:  snprintf(out, n, "%s par%s", NUM[opt], opt >= 2 ? "es" : ""); break;
    case GZ_K_MIN:    snprintf(out, n, "#%d é o menor", opt); break;
    case GZ_K_MAX:    snprintf(out, n, "#%d é o maior", opt); break;
    case GZ_K_SUMPAR: snprintf(out, n, "soma %s", opt == 0 ? "par" : "ímpar"); break;
    case GZ_K_SUM01:  snprintf(out, n, "#0+#1 %s 6", CMP_SYM[opt]); break;
    case GZ_K_REP: {
        static const char *const R[3] = { "nada repete", "dois iguais", "três iguais" };
        snprintf(out, n, "%s", R[opt]);
        break;
    }
    case GZ_K_ORDER: {
        static const char *const O[3] = { "crescente", "decrescente", "sem ordem" };
        snprintf(out, n, "%s", O[opt]);
        break;
    }
    default: snprintf(out, n, "?"); break;
    }
}

/* ------------------------------------------------------------ deduções */

bool gz_contradicted(const gz_puzzle_t *p, int v, int opt, const gz_obs_t *obs, int nobs)
{
    for (int i = 0; i < nobs; i++)
        if (obs[i].v == v && gz_eval(p->card[v], opt, obs[i].code) != (obs[i].yes != 0))
            return true;
    return false;
}

uint8_t gz_guess_conflicts(const gz_puzzle_t *p, const uint8_t guess[GZ_SHAPES],
                           const gz_obs_t *obs, int nobs)
{
    uint8_t bad = 0;
    for (int v = 0; v < GZ_VERIFIERS; v++) {
        bool ok = false;
        for (int o = 0; o < GZ_CARDS[p->card[v]].nopt && !ok; o++)
            if (gz_eval(p->card[v], o, guess) && !gz_contradicted(p, v, o, obs, nobs)) ok = true;
        if (!ok) bad |= (uint8_t)(1u << v);
    }
    return bad;
}

/* --------------------------------------------------------------- datas */

/* dias desde 1970-01-01 (algoritmo civil de Howard Hinnant, inteiro puro) */
static int32_t days_civil(int y, int m, int d)
{
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int32_t gz_day_number(int y, int m, int d)
{
    return days_civil(y, m, d) - days_civil(2026, 1, 1) + 1;
}
