/**
 * @file cilada_game.c
 * @brief Lógica pura da CILADA (ver cilada_game.h).
 */
#include "cilada_game.h"

#define BIT(i) ((uint16_t)(1u << (i)))

static int clamp_n(int n) { return n == 16 ? 16 : 9; }

bool cilada_is_open(const cilada_round_t *r, int i)
{
    return i >= 0 && i < r->n && (r->opened & BIT(i)) != 0;
}

int cilada_closed(const cilada_round_t *r)
{
    int c = 0;
    for (int i = 0; i < r->n; i++) if (!(r->opened & BIT(i))) c++;
    return c;
}

int cilada_card_at(const cilada_round_t *r, int i)
{
    for (int c = 0; c < CILADA_CARD_COUNT; c++)
        if (r->card_at[c] == i) return c;
    return -1;
}

/* k-ésimo furo fechado (0-based), pulando `skip`; -1 se não existe. */
static int nth_closed(const cilada_round_t *r, int k, int skip)
{
    for (int i = 0; i < r->n; i++) {
        if ((r->opened & BIT(i)) || i == skip) continue;
        if (k-- == 0) return i;
    }
    return -1;
}

void cilada_new_round(cilada_round_t *r, int n, bool cards, cilada_rng_t rng)
{
    r->n = (uint8_t)clamp_n(n);
    r->opened = 0;
    r->trap = (int8_t)rng(0, r->n - 1);
    r->opens_left = 1;
    r->next_extra = 0;
    r->shields = 0;
    r->spy = false;
    r->point = false;
    r->tremor = -1;
    r->defused = -1;
    for (int c = 0; c < CILADA_CARD_COUNT; c++) r->card_at[c] = -1;

    if (cards) {
        /* embaralha os tipos e pega os primeiros: no máximo 1 de cada */
        int types[CILADA_CARD_COUNT] = { 0, 1, 2, 3 };
        for (int i = CILADA_CARD_COUNT - 1; i > 0; i--) {
            int j = (int)rng(0, i);
            int t = types[i]; types[i] = types[j]; types[j] = t;
        }
        int k = (r->n == 16) ? 3 : 2;
        uint16_t used = BIT(r->trap);
        for (int c = 0; c < k; c++) {
            int free_cells = 0;
            for (int i = 0; i < r->n; i++) if (!(used & BIT(i))) free_cells++;
            int pick = (int)rng(0, free_cells - 1);
            for (int i = 0; i < r->n; i++) {
                if (used & BIT(i)) continue;
                if (pick-- == 0) {
                    r->card_at[types[c]] = (int8_t)i;
                    used |= BIT(i);
                    break;
                }
            }
        }
    }
    cilada_roll_tremor(r, rng);
}

cilada_hit_t cilada_open(cilada_round_t *r, int i, int *card)
{
    if (card) *card = -1;
    if (i < 0 || i >= r->n || (r->opened & BIT(i))) return CILADA_INVALID;

    r->opened |= BIT(i);
    if (r->tremor == i) r->tremor = -1;
    if (i == r->trap) return CILADA_TRAP;

    if (r->opens_left) r->opens_left--;
    int c = cilada_card_at(r, i);
    if (c < 0) return CILADA_SAFE;

    switch (c) {
    case CILADA_ESPIA:  r->spy = true;        break;
    case CILADA_APONTA: r->point = true;      break;
    case CILADA_MAIS1:  r->next_extra = 1;    break;
    case CILADA_ESCUDO: if (r->shields < 3) r->shields++; break;
    }
    if (card) *card = c;
    return CILADA_CARD;
}

bool cilada_spy(cilada_round_t *r, int i)
{
    r->spy = false;
    return i == r->trap;
}

bool cilada_turn_over(const cilada_round_t *r)
{
    return r->opens_left == 0 && !r->spy;
}

void cilada_next_turn(cilada_round_t *r, cilada_rng_t rng)
{
    r->opens_left = (uint8_t)(1 + r->next_extra);
    r->next_extra = 0;
    r->point = false;
    r->spy = false;
    cilada_roll_tremor(r, rng);
}

bool cilada_use_shield(cilada_round_t *r, cilada_rng_t rng)
{
    if (r->shields) r->shields--;
    r->opens_left = 0;
    r->spy = false;
    r->defused = r->trap;
    int closed = cilada_closed(r);
    if (closed <= 0) return false;

    /* prefere furo fechado sem carta; se só sobraram cartas, a cilada come uma */
    int plain = 0;
    for (int i = 0; i < r->n; i++)
        if (!(r->opened & BIT(i)) && cilada_card_at(r, i) < 0) plain++;
    if (plain > 0) {
        int pick = (int)rng(0, plain - 1);
        for (int i = 0; i < r->n; i++) {
            if ((r->opened & BIT(i)) || cilada_card_at(r, i) >= 0) continue;
            if (pick-- == 0) { r->trap = (int8_t)i; break; }
        }
    } else {
        r->trap = (int8_t)nth_closed(r, (int)rng(0, closed - 1), -1);
        int c = cilada_card_at(r, r->trap);
        if (c >= 0) r->card_at[c] = -1;
    }
    return true;
}

void cilada_roll_tremor(cilada_round_t *r, cilada_rng_t rng)
{
    r->tremor = -1;
    int closed = cilada_closed(r);
    if (closed < 2) return;                       /* só a cilada: tremer entregaria */
    if (rng(0, 999) >= CILADA_TREMOR_PM) return;

    /* bilhete extra pra cilada: closed + 1 bilhetes no total */
    int pick = (int)rng(0, closed);
    if (pick == closed) { r->tremor = r->trap; return; }
    r->tremor = (int8_t)nth_closed(r, pick, -1);
}

/* --- persistência --------------------------------------------------------
 * w[0]: bits 0-15 opened | 16-19 trap | 20 grade 4x4 | 21-22 opens_left |
 *       23 next_extra | 24-25 shields | 26 spy | 27 point | 30 válido
 * w[1]: 4 x 5 bits: furo+1 de cada carta (0 = fora da rodada) |
 *       bits 20-24: furo+1 desarmado por escudo (0 = nenhum)
 */
#define VALID_BIT (1u << 30)

void cilada_pack(const cilada_round_t *r, int32_t w[2])
{
    uint32_t a = (uint32_t)r->opened
               | ((uint32_t)(r->trap & 0xF) << 16)
               | ((r->n == 16 ? 1u : 0u) << 20)
               | ((uint32_t)(r->opens_left & 0x3) << 21)
               | ((uint32_t)(r->next_extra & 0x1) << 23)
               | ((uint32_t)(r->shields & 0x3) << 24)
               | ((r->spy ? 1u : 0u) << 26)
               | ((r->point ? 1u : 0u) << 27)
               | VALID_BIT;
    uint32_t b = 0;
    for (int c = 0; c < CILADA_CARD_COUNT; c++)
        b |= (uint32_t)((r->card_at[c] + 1) & 0x1F) << (5 * c);
    b |= (uint32_t)((r->defused + 1) & 0x1F) << 20;
    w[0] = (int32_t)a;
    w[1] = (int32_t)b;
}

bool cilada_unpack(cilada_round_t *r, const int32_t w[2])
{
    uint32_t a = (uint32_t)w[0], b = (uint32_t)w[1];
    if (!(a & VALID_BIT)) return false;

    cilada_round_t t;
    t.n = (a >> 20) & 1u ? 16 : 9;
    t.opened = (uint16_t)(a & 0xFFFF);
    t.trap = (int8_t)((a >> 16) & 0xF);
    t.opens_left = (uint8_t)((a >> 21) & 0x3);
    t.next_extra = (uint8_t)((a >> 23) & 0x1);
    t.shields = (uint8_t)((a >> 24) & 0x3);
    t.spy = ((a >> 26) & 1u) != 0;
    t.point = ((a >> 27) & 1u) != 0;
    t.tremor = -1;
    t.defused = (int8_t)((int)((b >> 20) & 0x1F) - 1);

    if (t.trap >= t.n || t.defused >= t.n) return false;
    if (t.defused >= 0 && !(t.opened & (1u << t.defused))) return false;
    if (t.n == 9 && (t.opened & 0xFE00)) return false;
    for (int c = 0; c < CILADA_CARD_COUNT; c++) {
        int v = (int)((b >> (5 * c)) & 0x1F) - 1;
        if (v >= t.n || v == t.trap) return false;
        t.card_at[c] = (int8_t)v;
    }
    *r = t;
    return true;
}
