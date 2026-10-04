/**
 * @file dobra_game.h
 * @brief Lógica pura do DOBRA — sem LVGL, testável no desktop.
 *
 * O tabuleiro guarda EXPOENTES, não valores: 0 = casa vazia, 1 = 2, 2 = 4,
 * ..., 11 = 2048. Assim cada casa cabe em 5 bits (persistência em 3 int32)
 * e nada aqui precisa de divisão — só shift. Inteiro puro: a Tool roda como
 * .so e o elf_loader não resolve float nem divisão de 64 bits.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DOBRA_N        4
#define DOBRA_CELLS    (DOBRA_N * DOBRA_N)
#define DOBRA_EXP_MAX  31          /* teto de 5 bits — inalcançável num 4x4 */
#define DOBRA_UNDO_CAP 32          /* jogadas guardadas pro "desfazer ilimitado" */

typedef enum {
    DOBRA_UP = 0,
    DOBRA_DOWN,
    DOBRA_LEFT,
    DOBRA_RIGHT,
} dobra_dir_t;

/** O que aconteceu numa jogada — o suficiente pra UI animar. */
typedef struct {
    bool    moved;                 /* false = jogada não mudou nada (ignorar) */
    int8_t  dest[DOBRA_CELLS];     /* casa de destino de cada peça de origem (-1 = vazia) */
    bool    merged[DOBRA_CELLS];   /* casa (destino) onde duas peças viraram uma */
    uint8_t top_merge;             /* maior expoente criado por junção (0 = nenhuma) */
    uint32_t gained;               /* pontos ganhos */
} dobra_move_t;

typedef struct {
    uint8_t  cell[DOBRA_CELLS];
    uint32_t score;
} dobra_snap_t;

typedef struct {
    uint8_t  cell[DOBRA_CELLS];    /* expoentes, linha a linha (índice = y*4 + x) */
    uint32_t score;
    uint8_t  goal_exp;             /* 0 = infinito; 8 = 256, 9 = 512, 10 = 1024 */
    bool     won;                  /* já celebrou a meta nesta partida */
    int8_t   undo_left;            /* -1 = ilimitado */

    dobra_snap_t hist[DOBRA_UNDO_CAP];  /* anel de jogadas anteriores */
    uint8_t  hist_head;
    uint8_t  hist_len;
} dobra_game_t;

/** Fonte de aleatoriedade: inteiro em [lo, hi]. Na Tool, a Random API. */
typedef int32_t (*dobra_rng_t)(int32_t lo, int32_t hi);

/** Zera o tabuleiro e põe as duas peças iniciais. Mantém goal_exp; recebe a
 *  quantidade de "desfazer" da partida (-1 = ilimitado). */
void dobra_new_game(dobra_game_t *g, int8_t undo_left, dobra_rng_t rng);

/** Faz a jogada. Se algo mexeu: guarda o estado anterior pro desfazer,
 *  aplica e sorteia uma peça nova (90% um 2, 10% um 4). */
dobra_move_t dobra_move(dobra_game_t *g, dobra_dir_t dir, dobra_rng_t rng);

/** Só calcula a jogada, sem alterar `g` (nem sortear). */
dobra_move_t dobra_preview(const dobra_game_t *g, dobra_dir_t dir, uint8_t out[DOBRA_CELLS]);

/** Volta uma jogada. false se não há histórico ou os "desfazer" acabaram. */
bool dobra_undo(dobra_game_t *g);

/** Ainda existe alguma jogada possível? */
bool dobra_can_move(const dobra_game_t *g);

/** Maior expoente no tabuleiro. */
uint8_t dobra_max_exp(const dobra_game_t *g);

/** A meta acabou de ser atingida (e ainda não foi celebrada)? */
bool dobra_goal_reached(const dobra_game_t *g);

/** Empacota as 16 casas em 3 int32 (5 bits cada, 6+6+4). */
void dobra_pack(const dobra_game_t *g, int32_t out[3]);

/** Desempacota; false se o resultado for inválido (tabuleiro vazio). */
bool dobra_unpack(dobra_game_t *g, const int32_t in[3]);
