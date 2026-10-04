/**
 * @file test_dobra.c
 * @brief Testes da lógica pura do DOBRA (compilação desktop, sem LVGL).
 */
#include "dobra_game.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t rng_rand(int32_t lo, int32_t hi) { return lo + rand() % (hi - lo + 1); }
static int32_t rng_lo(int32_t lo, int32_t hi)   { (void)hi; return lo; }

static void set_board(dobra_game_t *g, const uint8_t b[DOBRA_CELLS])
{
    memset(g, 0, sizeof *g);
    memcpy(g->cell, b, DOBRA_CELLS);
    g->undo_left = -1;
}

static void test_merge_rules(void)
{
    dobra_game_t g;
    uint8_t out[DOBRA_CELLS];

    /* 2 2 2 2 -> 4 4 . .  (cada peça junta uma vez só) */
    uint8_t b1[DOBRA_CELLS] = { 1,1,1,1 };
    set_board(&g, b1);
    dobra_move_t mv = dobra_preview(&g, DOBRA_LEFT, out);
    assert(mv.moved && out[0] == 2 && out[1] == 2 && out[2] == 0 && out[3] == 0);
    assert(mv.gained == 8 && mv.top_merge == 2);
    assert(mv.dest[0] == 0 && mv.dest[1] == 0 && mv.dest[2] == 1 && mv.dest[3] == 1);

    /* 4 2 2 . -> 4 4 . .  (não junta em cascata 4+4 na mesma jogada) */
    uint8_t b2[DOBRA_CELLS] = { 2,1,1,0 };
    set_board(&g, b2);
    mv = dobra_preview(&g, DOBRA_LEFT, out);
    assert(out[0] == 2 && out[1] == 2 && out[2] == 0);

    /* 2 2 4 . pra DIREITA -> . . 4 4 (junta do lado pra onde escorre) */
    uint8_t b3[DOBRA_CELLS] = { 1,1,2,0 };
    set_board(&g, b3);
    mv = dobra_preview(&g, DOBRA_RIGHT, out);
    assert(out[3] == 2 && out[2] == 2 && out[1] == 0 && out[0] == 0);

    /* coluna: 2 . 2 4 pra CIMA -> 4 4 . . */
    uint8_t b4[DOBRA_CELLS] = { 1,0,0,0, 0,0,0,0, 1,0,0,0, 2,0,0,0 };
    set_board(&g, b4);
    mv = dobra_preview(&g, DOBRA_UP, out);
    assert(out[0] == 2 && out[4] == 2 && out[8] == 0 && out[12] == 0);
    assert(mv.dest[8] == 0 && mv.dest[12] == 4);

    /* encostado sem par: jogada não conta */
    uint8_t b5[DOBRA_CELLS] = { 1,2,3,4 };
    set_board(&g, b5);
    mv = dobra_preview(&g, DOBRA_LEFT, out);
    assert(!mv.moved);
    printf("ok  regras de junção\n");
}

static void test_move_spawn_undo(void)
{
    dobra_game_t g;
    uint8_t b[DOBRA_CELLS] = { 1,1 };
    set_board(&g, b);
    g.undo_left = 1;

    dobra_move_t mv = dobra_move(&g, DOBRA_LEFT, rng_lo);
    assert(mv.moved && g.score == 4);
    int tiles = 0;
    for (int i = 0; i < DOBRA_CELLS; i++) tiles += g.cell[i] != 0;
    assert(tiles == 2);                       /* a junção + a peça nova */

    /* jogada que não mexe não entra no histórico nem sorteia */
    uint8_t before[DOBRA_CELLS];
    memcpy(before, g.cell, DOBRA_CELLS);
    uint8_t len = g.hist_len;
    dobra_move_t none = { 0 };
    uint8_t out[DOBRA_CELLS];
    for (int d = 0; d < 4; d++) {
        dobra_move_t p = dobra_preview(&g, (dobra_dir_t)d, out);
        if (!p.moved) { none = dobra_move(&g, (dobra_dir_t)d, rng_lo); break; }
    }
    (void)none;
    assert(g.hist_len == len && !memcmp(before, g.cell, DOBRA_CELLS));

    assert(dobra_undo(&g));
    assert(g.cell[0] == 1 && g.cell[1] == 1 && g.score == 0 && g.undo_left == 0);
    assert(!dobra_undo(&g));                  /* acabaram os "desfazer" */
    printf("ok  jogada, peça nova e desfazer\n");
}

static void test_undo_unlimited_ring(void)
{
    dobra_game_t g;
    memset(&g, 0, sizeof g);
    srand(7);
    dobra_new_game(&g, -1, rng_rand);
    int moves = 0;
    for (int i = 0; i < 400 && dobra_can_move(&g); i++)
        if (dobra_move(&g, (dobra_dir_t)(rand() % 4), rng_rand).moved) moves++;
    int undos = 0;
    while (dobra_undo(&g)) undos++;
    int expect = moves < DOBRA_UNDO_CAP ? moves : DOBRA_UNDO_CAP;
    assert(undos == expect && g.undo_left == -1);
    printf("ok  desfazer ilimitado (%d jogadas, %d desfeitas)\n", moves, undos);
}

static void test_game_over_and_goal(void)
{
    dobra_game_t g;
    uint8_t full[DOBRA_CELLS] = { 1,2,1,2, 2,1,2,1, 1,2,1,2, 2,1,2,1 };
    set_board(&g, full);
    assert(!dobra_can_move(&g));
    g.cell[15] = 2;                           /* par vertical com a casa de cima */
    assert(dobra_can_move(&g));

    uint8_t b[DOBRA_CELLS] = { 7,7 };
    set_board(&g, b);
    g.goal_exp = 8;
    assert(!dobra_goal_reached(&g));
    dobra_move(&g, DOBRA_LEFT, rng_lo);
    assert(dobra_max_exp(&g) == 8 && dobra_goal_reached(&g));
    g.won = true;
    assert(!dobra_goal_reached(&g));
    g.goal_exp = 0; g.won = false;            /* infinito nunca "chega" */
    assert(!dobra_goal_reached(&g));
    printf("ok  fim de jogo e meta\n");
}

static void test_pack(void)
{
    dobra_game_t a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    for (int i = 0; i < DOBRA_CELLS; i++) a.cell[i] = (uint8_t)((i * 7) % 18);
    int32_t w[3];
    dobra_pack(&a, w);
    assert(w[0] >= 0 && w[1] >= 0 && w[2] >= 0);
    assert(dobra_unpack(&b, w) && !memcmp(a.cell, b.cell, DOBRA_CELLS));
    int32_t zero[3] = { 0, 0, 0 };
    assert(!dobra_unpack(&b, zero));
    printf("ok  empacotar/desempacotar\n");
}

int main(void)
{
    test_merge_rules();
    test_move_spawn_undo();
    test_undo_unlimited_ring();
    test_game_over_and_goal();
    test_pack();
    printf("todos os testes passaram\n");
    return 0;
}
