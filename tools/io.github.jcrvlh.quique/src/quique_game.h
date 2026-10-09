/**
 * @file quique_game.h
 * @brief Lógica pura do QUIQUE — sem LVGL, testável no desktop.
 *
 * Ping-pong solo contra a parede. A raquete segue a inclinação do KIT
 * (ângulo do giroscópio -> posição, não velocidade). A cada N rebatidas o
 * jogo pausa e oferece cartas abertas: cada carta traz um bônus E um ônus,
 * e os efeitos acumulam. Três vidas; acabou, o placar vai (ou não) pro top-5.
 *
 * Inteiro puro: a Tool roda como .so e o elf_loader não resolve float nem
 * divisão de 64 bits. Posições em subpixels (QQ_FP por pixel), ângulos do
 * giroscópio em centigraus, percentuais inteiros.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* --- geometria (px) ----------------------------------------------------- */

#define QQ_W            368
#define QQ_H            448
#define QQ_FP           16           /* subpixels por pixel */
#define QQ_WALL_Y       84           /* parede de cima (abaixo do placar) */
#define QQ_PRENSA_PX    40           /* quanto a parede desce por Prensa */
#define QQ_PADDLE_Y     408          /* topo da raquete */
#define QQ_PADDLE_H     14
#define QQ_PADDLE_W     84           /* largura base */
#define QQ_GEMEA_GAP    28           /* vão entre as duas raquetes da Gêmea */
#define QQ_SHIELD_Y     438          /* linha do escudo */
#define QQ_BALL_R       8            /* raio base */

#define QQ_MAX_BALLS    2
#define QQ_MAX_HIST     64           /* cartas pegas numa partida */
#define QQ_MAX_OFFER    4
#define QQ_PREVIEW_N    6            /* pontinhos do rastro do Cometa */
#define QQ_START_LIVES  3
#define QQ_MAX_LIVES    9

/* --- cartas ------------------------------------------------------------- */

typedef enum {
    /* raquete */
    QC_TABUA = 0, QC_AGULHA, QC_IMA, QC_GEMEA, QC_ELASTICO,
    /* bola */
    QC_CHUMBO, QC_MELANCIA, QC_GEMEAS, QC_COMETA, QC_FANTASMA, QC_TIRO,
    /* controle */
    QC_ESPELHO, QC_MOLA, QC_PENA, QC_PRUMO, QC_SACODE, QC_FREIO,
    /* percepção */
    QC_APAGAO, QC_NEBLINA, QC_MUDO, QC_METRONOMO,
    /* parede */
    QC_PRENSA, QC_PORTAL, QC_REBOTE,
    /* meta */
    QC_FAXINA, QC_EXORCISMO, QC_VIDRO, QC_FENIX, QC_GANANCIA,
    QC_COUNT
} qq_card_t;

typedef struct {
    const char *name;     /* CAIXA ALTA, UTF-8, até 10 letras */
    const char *bonus;    /* uma linha curta */
    const char *onus;     /* uma linha curta */
    uint8_t     max;      /* quantas vezes pode ser pega numa partida */
} qq_card_info_t;

extern const qq_card_info_t QQ_CARDS[QC_COUNT];

/* --- estado ------------------------------------------------------------- */

typedef enum { QS_IDLE = 0, QS_PLAY, QS_PICK, QS_OVER } qq_state_t;

/** Eventos de um passo (bitmask) — a UI toca som / acende efeito. */
enum {
    QE_HIT      = 1u << 0,    /* rebateu na raquete */
    QE_WALL     = 1u << 1,    /* bateu na parede de cima */
    QE_SIDE     = 1u << 2,    /* bateu numa lateral */
    QE_MISS     = 1u << 3,    /* perdeu uma bola (e vida) */
    QE_SHIELD   = 1u << 4,    /* o escudo salvou */
    QE_PICK     = 1u << 5,    /* hora de escolher carta (estado vira QS_PICK) */
    QE_OVER     = 1u << 6,    /* fim de jogo (estado vira QS_OVER) */
    QE_PHOENIX  = 1u << 7,    /* a Fênix reviveu */
    QE_SMASH    = 1u << 8,    /* cortada (Sacode) disparada */
    QE_SMASH_PT = 1u << 9,    /* cortada bateu na parede: +pontos */
    QE_SLOWMO   = 1u << 10,   /* câmera lenta (Freio) disparada */
    QE_NEAR     = 1u << 11,   /* Metrônomo: a bola está chegando */
    QE_BLACKOUT = 1u << 12,   /* Apagão começou */
    QE_SLIP     = 1u << 13,   /* Sacode: a raquete escorregou */
    QE_PORTAL   = 1u << 14,   /* a bola atravessou um portal */
    QE_SERVE    = 1u << 15,   /* bola saiu da raquete (saque / ímã) */
    QE_CENTER   = 1u << 16,   /* Tiro: rebatida no centro */
};

/** Inteiro em [lo, hi] inclusivo — injetado pela UI (TRNG do KIT). */
typedef int32_t (*qq_rng_t)(int32_t lo, int32_t hi);

typedef struct {
    bool    on;
    int32_t x, y;           /* centro, subpixels */
    int32_t vx, vy;         /* subpixels por passo (antes do fator de velocidade) */
    int16_t stuck;          /* >0: grudada na raquete por N passos */
    int32_t stuck_off;      /* deslocamento x em relação ao centro da raquete */
    bool    smash;          /* cortada: vale pontos ao bater na parede */
    bool    portal;         /* atravessou portal desde a última rebatida */
    bool    near_sent;      /* Metrônomo: já avisou nesta descida */
} qq_ball_t;

typedef struct {
    uint8_t card;
    bool    bonus_on;
    bool    onus_on;
} qq_inst_t;

typedef struct {
    qq_state_t state;
    qq_rng_t   rng;

    /* ajustes da partida */
    int32_t range_cdeg;     /* inclinação que leva a raquete à borda */
    int8_t  dir;            /* +1 / -1 (sentido do eixo no aparelho) */

    /* placar */
    int32_t score;
    int32_t hits;           /* rebatidas na raquete */
    int32_t wall_hits;
    int32_t next_pick;      /* rebatida em que abre a próxima escolha */
    int8_t  lives;
    int8_t  shield;
    bool    phoenix_used;

    /* cartas */
    qq_inst_t hist[QQ_MAX_HIST];
    uint8_t   nhist;
    uint8_t   taken[QC_COUNT];
    uint8_t   bon[QC_COUNT];     /* bônus ativos por carta */
    uint8_t   onu[QC_COUNT];     /* ônus ativos por carta */
    uint8_t   offer[QQ_MAX_OFFER];
    uint8_t   noffer;

    /* cargas / timers */
    uint8_t  smash_charges;
    uint8_t  slow_charges;
    int16_t  slow_t;           /* passos de câmera lenta restantes */
    int16_t  blackout_t;       /* passos de apagão restantes */
    int16_t  slip_t;           /* passos do escorregão restantes */
    int8_t   slip_dir;
    int32_t  slip_wait;        /* passos até o próximo escorregão */
    int32_t  tick;

    /* entrada */
    int32_t zero_roll16, zero_pitch16;   /* centro (cdeg*16), vaza devagar */
    int32_t target;            /* centro-alvo da raquete (px*FP) */
    int32_t prev_target;
    int32_t delay[8];          /* Pena: atraso */
    uint8_t delay_i;
    int32_t paddle;            /* centro da raquete (px*FP) */
    int32_t rate;              /* px*FP por passo, suavizado (Elástico) */

    qq_ball_t ball[QQ_MAX_BALLS];
} qq_game_t;

/* --- ciclo -------------------------------------------------------------- */

/** Zera tudo e começa a partida (bola grudada na raquete pro saque). */
void qq_start(qq_game_t *g, int32_t range_cdeg, int8_t dir, qq_rng_t rng);

/**
 * Um passo de simulação (~16 ms). `roll`/`pitch` são os ângulos crus do
 * giroscópio em centigraus. Retorna QE_* (bitmask).
 */
uint32_t qq_step(qq_game_t *g, int32_t roll_cdeg, int32_t pitch_cdeg);

/** O KIT foi chacoalhado (Sacode / Freio). Retorna QE_*. */
uint32_t qq_shake(qq_game_t *g);

/** Sorteia a oferta de cartas (preenche g->offer / g->noffer). */
void qq_make_offer(qq_game_t *g);

/** Pega a carta `idx` da oferta e volta pro jogo. Retorna QE_* (ex.: vidas). */
uint32_t qq_take_offer(qq_game_t *g, int idx);

/** Pega uma carta direto (usado pela oferta e pelos testes). */
void qq_take(qq_game_t *g, qq_card_t c);

/* --- leituras derivadas (UI) -------------------------------------------- */

int32_t qq_paddle_w(const qq_game_t *g);         /* px, de cada raquete */
int     qq_paddles(const qq_game_t *g, int32_t left_px[2]);   /* 1 ou 2; x da borda esquerda */
int32_t qq_ball_r(const qq_game_t *g);           /* px */
int32_t qq_wall_y(const qq_game_t *g);           /* px */
int32_t qq_fog_y(const qq_game_t *g);            /* px: neblina cobre de wall_y até aqui (== wall_y: sem) */
bool    qq_ball_visible(const qq_game_t *g, int i);
bool    qq_silent(const qq_game_t *g);           /* Mudo */
bool    qq_portals(const qq_game_t *g);
int32_t qq_mult_pct(const qq_game_t *g);         /* multiplicador de pontos, % */
int32_t qq_speed_pct(const qq_game_t *g);        /* fator de velocidade atual, % */
int32_t qq_final_score(const qq_game_t *g);      /* com a conta da Fênix */
int     qq_onus_active(const qq_game_t *g);      /* nº de ônus ligados */

/** Rastro do Cometa: até QQ_PREVIEW_N pontos (px) da trajetória da bola 0. */
int     qq_preview(const qq_game_t *g, int16_t xs[], int16_t ys[]);

/**
 * Texto curto com as cartas ativas ("TÁBUA×2 · ESPELHO"), pra faixa do topo.
 * Cartas com bônus e ônus desligados (pela Faxina) não aparecem.
 */
void    qq_fx_text(const qq_game_t *g, char *out, int cap);

/* --- top-5 -------------------------------------------------------------- */

#define QQ_HS_N 5
/** Insere `score` no top-5 decrescente. Retorna a posição (0..4) ou -1. */
int     qq_hs_insert(int32_t hs[QQ_HS_N], int32_t score);

/* --- trigonometria inteira (exposta pros testes) ------------------------- */

/** sin/cos de `deg` (0..90) × 1024. */
int32_t qq_sin1024(int32_t deg);
int32_t qq_cos1024(int32_t deg);
