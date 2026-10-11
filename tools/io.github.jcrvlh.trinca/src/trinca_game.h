/**
 * @file trinca_game.h
 * @brief Lógica pura da TRINCA — sorteio, resultado e contador de sorte.
 *        Sem LVGL, testável no desktop.
 *
 * Cada rolo é a mesma FAIXA de 32 paradas: 6 símbolos comuns com 5 paradas
 * cada e a estrela (rara) com 2. Um giro sorteia uma parada uniforme por rolo,
 * independente dos outros — a chance de cada símbolo é exatamente a fração da
 * faixa que ele ocupa, e a animação só rola a faixa real até a parada sorteada.
 *
 * Contador de sorte: cada giro vale pontos (nada 0, dupla 1, trinca 3 — a
 * trinca rara conta como trinca). A distribuição EXATA da soma de n giros é
 * mantida por convolução incremental, e o percentil sai dela, sem aproximação
 * normal (que mentiria nas pontas, onde a trinca mora). Mede os últimos
 * TRINCA_WINDOW giros da sessão.
 *
 * Inteiro puro de 32 bits: a Tool roda como .so e o elf_loader não resolve
 * float nem divisão/multiplicação de 64 bits.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TRINCA_REELS      3
#define TRINCA_SYMBOLS    7          /* 0..5 comuns, 6 = estrela (rara) */
#define TRINCA_RARE       6
#define TRINCA_STOPS      32         /* paradas por rolo */
#define TRINCA_ONE        32768      /* 32^3: denominador exato das chances */

#define TRINCA_WINDOW     300        /* giros que a barra de sorte mede */
#define TRINCA_MIN_SPINS  20         /* abaixo disto o percentil é ruído */
#define TRINCA_PTS_MAX    3          /* pontos de uma trinca */
#define TRINCA_DIST_LEN   (TRINCA_PTS_MAX * TRINCA_WINDOW + 1)

/** A faixa: símbolo de cada parada (igual nos 3 rolos). */
extern const uint8_t TRINCA_STRIP[TRINCA_STOPS];

typedef enum {
    TRINCA_NADA = 0,
    TRINCA_DUPLA,
    TRINCA_TRINCA,       /* três iguais, comum */
    TRINCA_RARA,         /* três estrelas */
} trinca_kind_t;

/** Fonte de aleatoriedade: inteiro em [lo, hi]. Na Tool, a Random API. */
typedef int32_t (*trinca_rng_t)(int32_t lo, int32_t hi);

/** Quantas paradas da faixa mostram o símbolo `sym`. */
int trinca_weight(uint8_t sym);

/** Sorteia uma parada por rolo (uniforme em 0..TRINCA_STOPS-1). */
void trinca_spin(uint8_t stops[TRINCA_REELS], trinca_rng_t rng);

/** Resultado dos 3 símbolos na linha. */
trinca_kind_t trinca_classify(const uint8_t sym[TRINCA_REELS]);

/** Resultado das 3 paradas (consulta a faixa). */
trinca_kind_t trinca_classify_stops(const uint8_t stops[TRINCA_REELS]);

/** Pontos do contador de sorte: nada 0, dupla 1, trinca/rara 3. */
int trinca_points(trinca_kind_t k);

/**
 * Chance exata de cada resultado, em 1/TRINCA_ONE (somam TRINCA_ONE).
 * Calculada a partir da faixa, não digitada à mão.
 */
void trinca_odds(uint32_t out[4]);

/** Contador de sorte da sessão. ~4 KB — a Tool guarda um `static`. */
typedef struct {
    uint32_t dist[TRINCA_DIST_LEN];   /* P(soma = s) para n_dist giros, escala 2^30 */
    uint16_t n_dist;                  /* giros já convoluídos (<= TRINCA_WINDOW) */
    uint8_t  ring[TRINCA_WINDOW];     /* pontos dos últimos giros */
    uint16_t ring_head;
    uint16_t window_sum;              /* soma dos pontos na janela */
    uint32_t kernel[TRINCA_PTS_MAX + 1];  /* P(pontos = p) de 1 giro, em 1/TRINCA_ONE */

    uint32_t spins;                   /* giros na sessão */
    uint32_t duplas, trincas, raras;  /* resultados na sessão (raras ⊂ trincas) */
} trinca_luck_t;

void trinca_luck_reset(trinca_luck_t *l);

/** Registra um giro. */
void trinca_luck_add(trinca_luck_t *l, trinca_kind_t k);

/** Giros que a barra está medindo agora (sessão, até TRINCA_WINDOW). */
int trinca_luck_n(const trinca_luck_t *l);

/**
 * Percentil (meio-posto) da soma de pontos da janela, em milésimos:
 * P(S < s) + P(S = s)/2 sob o acaso puro. 500 = sorte mediana. -1 enquanto
 * a janela tem menos de TRINCA_MIN_SPINS giros.
 */
int trinca_luck_permille(const trinca_luck_t *l);

/** Valor esperado de `count` eventos de chance `odds`/TRINCA_ONE em `spins`
 *  giros, em décimos (ex: 285 = 28,5). Exato até ~6 milhões de giros. */
uint32_t trinca_expected_x10(uint32_t spins, uint32_t odds);
