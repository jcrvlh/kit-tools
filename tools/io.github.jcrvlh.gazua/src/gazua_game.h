/**
 * @file gazua_game.h
 * @brief Lógica pura do GAZUA — cartas, gerador de puzzle e deduções.
 *        Sem LVGL, testável no desktop.
 *
 * O código secreto são 3 números de 1 a 5, um por forma (triângulo,
 * quadrado, círculo): 125 códigos ao todo. Cada verificador é uma CARTA com
 * 2 a 4 regras possíveis e UMA regra secreta. Testar um código num
 * verificador responde só SIM ou NÃO.
 *
 * Gerador: sorteia 4 cartas e aceita o conjunto só se, entre TODAS as
 * combinações de uma regra por carta, exatamente uma fecha um código único
 * sem nenhum verificador sobrando (tirar qualquer um deixaria mais de um
 * código). É isso que garante que o puzzle se resolve por dedução. Com só
 * 125 códigos, cada regra vira uma máscara de 125 bits e a busca é força
 * bruta: poucos milissegundos.
 *
 * Determinístico por semente (mulberry32): o puzzle do dia sai igual em
 * todo KIT e a partida salva é reconstruída só com semente + dificuldade.
 *
 * Inteiro puro de 32 bits: a Tool roda como .so e o elf_loader não resolve
 * float nem aritmética de 64 bits.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GZ_SHAPES     3          /* 0 triângulo, 1 quadrado, 2 círculo */
#define GZ_DIGITS     5
#define GZ_CODES      125
#define GZ_VERIFIERS  4          /* verificadores por puzzle */
#define GZ_MAX_OPTS   4          /* regras possíveis por carta, no máximo */

/* Nos rótulos, "#0" "#1" "#2" marcam a forma (a UI troca por um bitmap). */
#define GZ_SHAPE_MARK '#'

typedef enum {
    GZ_K_VS = 0,     /* forma a comparada com a constante b */
    GZ_K_PAR,        /* forma a par / ímpar */
    GZ_K_CMP,        /* forma a comparada com a forma b */
    GZ_K_COUNT,      /* quantos dígitos iguais a b */
    GZ_K_EVENS,      /* quantos pares */
    GZ_K_MIN,        /* qual forma é a menor (estrita) */
    GZ_K_MAX,        /* qual forma é a maior (estrita) */
    GZ_K_SUMPAR,     /* soma par / ímpar */
    GZ_K_SUM01,      /* triângulo + quadrado comparado com 6 */
    GZ_K_REP,        /* nada repete / dois iguais / três iguais */
    GZ_K_ORDER,      /* crescente / decrescente / sem ordem */
} gz_kind_t;

typedef struct {
    uint8_t kind;    /* gz_kind_t */
    uint8_t a, b;
    uint8_t nopt;
    uint8_t easy;    /* entra na dificuldade FÁCIL */
    uint8_t long_face; /* rótulo de capa comprido: a UI usa a fonte menor */
} gz_card_t;

extern const gz_card_t GZ_CARDS[];
extern const int GZ_NCARDS;

typedef struct { uint32_t w[4]; } gz_mask_t;

typedef struct {
    uint8_t card[GZ_VERIFIERS];      /* índice em GZ_CARDS */
    uint8_t secret[GZ_VERIFIERS];    /* regra secreta de cada carta */
    uint8_t code[GZ_SHAPES];         /* 1..5 */
} gz_puzzle_t;

/** Prepara as máscaras (chame uma vez antes de tudo). */
void gz_init(void);

/** A regra `opt` da carta `card` aceita o código? */
bool gz_eval(int card, int opt, const uint8_t code[GZ_SHAPES]);

/** Índice 0..124 <-> código. */
int  gz_code_index(const uint8_t code[GZ_SHAPES]);
void gz_code_from_index(int idx, uint8_t code[GZ_SHAPES]);

/**
 * Gera o puzzle da semente. `easy` restringe às cartas fáceis.
 * Sempre devolve true com as cartas atuais (a busca tem teto alto).
 */
bool gz_generate(gz_puzzle_t *p, bool easy, uint32_t seed);

/** Resposta do verificador `v` para `code` (usa a regra secreta). */
bool gz_answer(const gz_puzzle_t *p, int v, const uint8_t code[GZ_SHAPES]);

/** Rótulo da capa da carta (com marcas #N). */
void gz_face(int card, char *out, size_t n);
/** Rótulo da regra `opt` da carta. */
void gz_opt_label(int card, int opt, char *out, size_t n);

/* --- deduções a partir do que o jogador viu (nunca olham a resposta) --- */

/** Um teste já feito: código + verificador + resposta. */
typedef struct {
    uint8_t code[GZ_SHAPES];
    uint8_t v;
    uint8_t yes;
} gz_obs_t;

/** A regra `opt` do verificador `v` contradiz algum teste já visto? */
bool gz_contradicted(const gz_puzzle_t *p, int v, int opt,
                     const gz_obs_t *obs, int nobs);

/**
 * Confere um palpite contra o que o jogador viu: para cada verificador,
 * alguma regra ainda possível precisa aceitar o palpite. Devolve uma máscara
 * de bits com os verificadores que NÃO batem (0 = bate com tudo).
 */
uint8_t gz_guess_conflicts(const gz_puzzle_t *p, const uint8_t guess[GZ_SHAPES],
                           const gz_obs_t *obs, int nobs);

/** Dias desde 2026-01-01 (1 = 1º de janeiro de 2026). */
int32_t gz_day_number(int y, int m, int d);
