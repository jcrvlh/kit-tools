/**
 * @file dobra_game.c
 * @brief Lógica pura do DOBRA — ver dobra_game.h.
 */
#include "dobra_game.h"

#include <string.h>

/* Índice da k-ésima casa da linha `l`, contando a partir da borda para onde
 * as peças escorrem. Todas as direções viram o mesmo "empurra pra frente". */
static int line_idx(dobra_dir_t dir, int l, int k)
{
    switch (dir) {
    case DOBRA_LEFT:  return l * DOBRA_N + k;
    case DOBRA_RIGHT: return l * DOBRA_N + (DOBRA_N - 1 - k);
    case DOBRA_UP:    return k * DOBRA_N + l;
    default:          return (DOBRA_N - 1 - k) * DOBRA_N + l;   /* DOWN */
    }
}

dobra_move_t dobra_preview(const dobra_game_t *g, dobra_dir_t dir, uint8_t out[DOBRA_CELLS])
{
    dobra_move_t mv;
    memset(&mv, 0, sizeof mv);
    memset(mv.dest, -1, sizeof mv.dest);
    memset(out, 0, DOBRA_CELLS);

    for (int l = 0; l < DOBRA_N; l++) {
        int  w = -1;              /* última posição (k) ocupada nesta linha */
        bool w_merged = false;    /* a peça em w já é fruto de junção nesta jogada */
        for (int k = 0; k < DOBRA_N; k++) {
            int src = line_idx(dir, l, k);
            uint8_t e = g->cell[src];
            if (!e) continue;

            if (w >= 0 && !w_merged && out[line_idx(dir, l, w)] == e && e < DOBRA_EXP_MAX) {
                int d = line_idx(dir, l, w);
                uint8_t ne = (uint8_t)(e + 1);
                out[d] = ne;
                mv.merged[d] = true;
                mv.dest[src] = (int8_t)d;
                mv.gained += 1u << ne;
                if (ne > mv.top_merge) mv.top_merge = ne;
                mv.moved = true;
                w_merged = true;
            } else {
                w++;
                int d = line_idx(dir, l, w);
                out[d] = e;
                mv.dest[src] = (int8_t)d;
                if (d != src) mv.moved = true;
                w_merged = false;
            }
        }
    }
    return mv;
}

static void spawn(dobra_game_t *g, dobra_rng_t rng)
{
    int empty[DOBRA_CELLS];
    int n = 0;
    for (int i = 0; i < DOBRA_CELLS; i++)
        if (!g->cell[i]) empty[n++] = i;
    if (!n) return;
    int pick = (int)rng(0, n - 1);
    if (pick < 0 || pick >= n) pick = 0;
    g->cell[empty[pick]] = (rng(0, 9) == 0) ? 2 : 1;   /* 10% um 4, senão um 2 */
}

void dobra_new_game(dobra_game_t *g, int8_t undo_left, dobra_rng_t rng)
{
    uint8_t goal = g->goal_exp;
    memset(g, 0, sizeof *g);
    g->goal_exp = goal;
    g->undo_left = undo_left;
    spawn(g, rng);
    spawn(g, rng);
}

dobra_move_t dobra_move(dobra_game_t *g, dobra_dir_t dir, dobra_rng_t rng)
{
    uint8_t out[DOBRA_CELLS];
    dobra_move_t mv = dobra_preview(g, dir, out);
    if (!mv.moved) return mv;

    dobra_snap_t *s = &g->hist[g->hist_head];
    memcpy(s->cell, g->cell, DOBRA_CELLS);
    s->score = g->score;
    g->hist_head = (uint8_t)((g->hist_head + 1) % DOBRA_UNDO_CAP);
    if (g->hist_len < DOBRA_UNDO_CAP) g->hist_len++;

    memcpy(g->cell, out, DOBRA_CELLS);
    g->score += mv.gained;
    spawn(g, rng);
    return mv;
}

bool dobra_undo(dobra_game_t *g)
{
    if (!g->hist_len || g->undo_left == 0) return false;
    g->hist_head = (uint8_t)((g->hist_head + DOBRA_UNDO_CAP - 1) % DOBRA_UNDO_CAP);
    g->hist_len--;
    const dobra_snap_t *s = &g->hist[g->hist_head];
    memcpy(g->cell, s->cell, DOBRA_CELLS);
    g->score = s->score;
    if (g->undo_left > 0) g->undo_left--;
    return true;
}

bool dobra_can_move(const dobra_game_t *g)
{
    for (int y = 0; y < DOBRA_N; y++) {
        for (int x = 0; x < DOBRA_N; x++) {
            uint8_t e = g->cell[y * DOBRA_N + x];
            if (!e) return true;
            if (x + 1 < DOBRA_N && g->cell[y * DOBRA_N + x + 1] == e) return true;
            if (y + 1 < DOBRA_N && g->cell[(y + 1) * DOBRA_N + x] == e) return true;
        }
    }
    return false;
}

uint8_t dobra_max_exp(const dobra_game_t *g)
{
    uint8_t m = 0;
    for (int i = 0; i < DOBRA_CELLS; i++)
        if (g->cell[i] > m) m = g->cell[i];
    return m;
}

bool dobra_goal_reached(const dobra_game_t *g)
{
    return g->goal_exp && !g->won && dobra_max_exp(g) >= g->goal_exp;
}

void dobra_pack(const dobra_game_t *g, int32_t out[3])
{
    for (int w = 0; w < 3; w++) {
        uint32_t v = 0;
        for (int j = 0; j < 6; j++) {
            int i = w * 6 + j;
            if (i < DOBRA_CELLS) v |= (uint32_t)(g->cell[i] & 0x1F) << (5 * j);
        }
        out[w] = (int32_t)v;
    }
}

bool dobra_unpack(dobra_game_t *g, const int32_t in[3])
{
    uint8_t cell[DOBRA_CELLS];
    bool any = false;
    for (int i = 0; i < DOBRA_CELLS; i++) {
        uint32_t v = (uint32_t)in[i / 6];
        cell[i] = (uint8_t)((v >> (5 * (i % 6))) & 0x1F);
        if (cell[i]) any = true;
    }
    if (!any) return false;
    memcpy(g->cell, cell, DOBRA_CELLS);
    g->hist_len = 0;
    g->hist_head = 0;
    return true;
}
