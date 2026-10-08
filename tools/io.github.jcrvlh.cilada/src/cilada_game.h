/**
 * @file cilada_game.h
 * @brief Lógica pura da CILADA — sem LVGL, testável no desktop.
 *
 * Uma rodada é uma grade de furos (3x3 ou 4x4). Um deles é a cilada; com
 * cartas ligadas, alguns furos seguros escondem uma carta. O KIT não sabe quem
 * está segurando o aparelho — só conta quantas aberturas faltam na vez atual
 * e quantos escudos estão guardados na mesa.
 *
 * Inteiro puro: a Tool roda como .so e o elf_loader não resolve float nem
 * divisão de 64 bits.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CILADA_MAX_CELLS 16

/* Probabilidade (permilagem) de um furo tremer numa vez. */
#define CILADA_TREMOR_PM 500

typedef enum {
    CILADA_ESPIA = 0,   /* quem acha segura um furo e só ele vê se é seguro */
    CILADA_APONTA,      /* quem acha escolhe quem joga depois dele */
    CILADA_MAIS1,       /* quem jogar depois abre 2 furos */
    CILADA_ESCUDO,      /* salva quem cair na cilada nesta rodada */
    CILADA_CARD_COUNT
} cilada_card_t;

typedef enum {
    CILADA_INVALID = 0, /* furo fora da grade ou já aberto */
    CILADA_SAFE,
    CILADA_CARD,
    CILADA_TRAP,
} cilada_hit_t;

/** Inteiro em [lo, hi] inclusivo — injetado pela UI (TRNG do KIT). */
typedef int32_t (*cilada_rng_t)(int32_t lo, int32_t hi);

typedef struct {
    uint8_t  n;                          /* 9 ou 16 */
    uint16_t opened;                     /* bit i = furo i aberto */
    int8_t   trap;                       /* índice da cilada */
    int8_t   card_at[CILADA_CARD_COUNT]; /* furo de cada carta, -1 = fora da rodada */
    uint8_t  opens_left;                 /* aberturas que faltam na vez atual */
    uint8_t  next_extra;                 /* +1 pendente pra próxima vez */
    uint8_t  shields;                    /* escudos guardados na mesa */
    bool     spy;                        /* a próxima segurada só espia */
    bool     point;                      /* quem fechar a vez aponta o próximo */
    int8_t   tremor;                     /* furo que treme nesta vez, -1 = nenhum */
    int8_t   defused;                    /* furo onde um escudo desarmou a cilada, -1 = nenhum */
} cilada_round_t;

/** Grade nova: sorteia a cilada e (se `cards`) 2 cartas no 3x3, 3 no 4x4. */
void cilada_new_round(cilada_round_t *r, int n, bool cards, cilada_rng_t rng);

bool cilada_is_open(const cilada_round_t *r, int i);
int  cilada_closed(const cilada_round_t *r);
/** Carta escondida no furo `i`, ou -1. */
int  cilada_card_at(const cilada_round_t *r, int i);

/**
 * Abre o furo `i` e aplica o efeito. Em SAFE/CARD consome uma abertura da vez;
 * em CARD devolve a carta em `*card` (ESPIA liga `spy`, APONTA liga `point`,
 * MAIS1 liga `next_extra`, ESCUDO soma em `shields`).
 */
cilada_hit_t cilada_open(cilada_round_t *r, int i, int *card);

/** Espia o furo fechado `i` (não abre). Desliga `spy`. true = é a cilada. */
bool cilada_spy(cilada_round_t *r, int i);

/** A vez acabou: sem aberturas sobrando e sem espiada pendente. */
bool cilada_turn_over(const cilada_round_t *r);

/** Passa a vez: aplica o +1 pendente, zera `point` e sorteia o tremor. */
void cilada_next_turn(cilada_round_t *r, cilada_rng_t rng);

/**
 * Gasta um escudo depois de alguém abrir a cilada: o furo fica marcado em
 * `defused`, a cilada se esconde em outro furo fechado (de preferência sem
 * carta) e a vez de quem caiu acaba. Devolve false se não sobrou furo fechado
 * — aí a UI começa uma grade nova com os mesmos jogadores.
 */
bool cilada_use_shield(cilada_round_t *r, cilada_rng_t rng);

/**
 * Sorteia o furo que treme nesta vez (ou nenhum). Peso 2 pra cilada, 1 pros
 * outros furos fechados: tremer é pista de verdade, mas nunca certeza.
 */
void cilada_roll_tremor(cilada_round_t *r, cilada_rng_t rng);

/** Serializa a rodada em 2 int32 (bit 30 de w[0] marca "válido"). */
void cilada_pack(const cilada_round_t *r, int32_t w[2]);
/** Restaura; false se os dados não formam uma rodada válida. */
bool cilada_unpack(cilada_round_t *r, const int32_t w[2]);
