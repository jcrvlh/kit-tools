/**
 * @file quique_game.c
 * @brief Lógica pura do QUIQUE (ver quique_game.h). Inteiro puro.
 */
#include "quique_game.h"

#include <string.h>

/* --- números do jogo (ponto de partida — o balanceamento sai jogando) ----- */

#define SPEED0          70     /* módulo base da velocidade, subpx/passo (~275 px/s a 16 ms) */
#define SPEED_STEP_PCT  6      /* +6% a cada 10 rebatidas */
#define SPEED_PROG_MAX  160    /* teto da progressão, % */
#define SPEED_MAX_PCT   270    /* teto absoluto: 70 * 2,7 ≈ 12 px/passo */
#define SERVE_STEPS     45     /* bola parada na raquete antes do saque */
#define IMA_STEPS       30     /* Ímã: tempo grudada */
#define PICK_EVERY      10
#define PICK_EVERY_FAST 5      /* Ganância */
#define DEADZONE_CDEG   80
#define LEAK_DIV        2048   /* centro vaza devagar pra tirar a deriva do giroscópio */
#define SLOW_STEPS      125    /* Freio: ~2 s */
#define SLOW_PCT        40
#define BLACKOUT_STEPS  60     /* Apagão: ~1 s */
#define SLIP_EVERY      1250   /* Sacode: ~20 s */
#define SLIP_STEPS      90
#define SLIP_PX         50
#define NEAR_STEPS      18     /* Metrônomo: avisa ~0,3 s antes */
#define PREVIEW_GAP     8      /* passos entre pontinhos do Cometa */

const qq_card_info_t QQ_CARDS[QC_COUNT] = {
    [QC_TABUA]     = { "TÁBUA",     "raquete 40% maior",       "bola 15% mais rápida",   3 },
    [QC_AGULHA]    = { "AGULHA",    "pontos em dobro",         "raquete 35% menor",              2 },
    [QC_IMA]       = { "ÍMÃ",       "a bola gruda na raquete", "raquete 20% menor",              1 },
    [QC_GEMEA]     = { "GÊMEA",     "2ª raquete ao lado",      "vão entre as duas",         1 },
    [QC_ELASTICO]  = { "ELÁSTICO",  "estica quando você mexe", "encolhe quando para",       1 },
    [QC_CHUMBO]    = { "CHUMBO",    "bola 25% mais lenta",     "bola menor",                3 },
    [QC_MELANCIA]  = { "MELANCIA",  "bola maior",              "cai em curva",              2 },
    [QC_GEMEAS]    = { "GÊMEAS",    "rebatida vale 2",         "duas bolas em jogo",        1 },
    [QC_COMETA]    = { "COMETA",    "mostra a trajetória",     "bola 20% mais rápida",   1 },
    [QC_FANTASMA]  = { "FANTASMA",  "ganha 1 vida",            "bola some no meio",         2 },
    [QC_TIRO]      = { "TIRO",      "centro da raquete vale 3", "borda com ângulo extremo",  1 },
    [QC_ESPELHO]   = { "ESPELHO",   "50% mais pontos",         "controle invertido",        1 },
    [QC_MOLA]      = { "MOLA",      "a raquete corre mais",    "tremor amplificado",       2 },
    [QC_PENA]      = { "PENA",      "movimento sem tremor",    "raquete com atraso",        1 },
    [QC_PRUMO]     = { "PRUMO",     "ganha 1 escudo",          "o eixo do controle troca",     1 },
    [QC_SACODE]    = { "SACODE",    "chacoalhe: cortada (3x)", "a raquete escorrega",       2 },
    [QC_FREIO]     = { "FREIO",     "chacoalhe: câmera lenta", "bola 25% mais rápida",   2 },
    [QC_APAGAO]    = { "APAGÃO",    "ganha 1 vida",            "a tela apaga às vezes",     2 },
    [QC_NEBLINA]   = { "NEBLINA",   "50% mais pontos",         "o topo fica coberto",       2 },
    [QC_MUDO]      = { "MUDO",      "25% mais pontos",         "a bola não faz som",        1 },
    [QC_METRONOMO] = { "METRÔNOMO", "bipe antes da bola chegar", "velocidade em ondas",     1 },
    [QC_PRENSA]    = { "PRENSA",    "pontos em dobro",         "a parede desce",            3 },
    [QC_PORTAL]    = { "PORTAL",    "passou no portal: vale 3", "laterais viram portais",    1 },
    [QC_REBOTE]    = { "REBOTE",    "a parede devolve reto",   "às vezes, ângulo maluco",   1 },
    [QC_FAXINA]    = { "FAXINA",    "tira o ônus mais antigo", "tira o bônus mais antigo",  3 },
    [QC_EXORCISMO] = { "EXORCISMO", "tira todos os ônus",      "fica com 1 vida",           2 },
    [QC_VIDRO]     = { "VIDRO",     "ganha 2 vidas",           "cada bola perdida tira 2",  1 },
    [QC_FENIX]     = { "FÊNIX",     "revive uma vez",          "perde 25% no placar final",      1 },
    [QC_GANANCIA]  = { "GANÂNCIA",  "4 cartas por escolha",    "escolhas a cada 5",         1 },
};

/* --- util ---------------------------------------------------------------- */

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static const int16_t SIN5[19] = {
    0, 89, 178, 265, 350, 433, 512, 587, 658, 724,
    784, 839, 887, 928, 962, 989, 1008, 1020, 1024
};

int32_t qq_sin1024(int32_t deg)
{
    deg = clamp(deg, 0, 90);
    int32_t i = deg / 5, f = deg % 5;
    if (i >= 18) return 1024;
    return SIN5[i] + ((SIN5[i + 1] - SIN5[i]) * f) / 5;
}

int32_t qq_cos1024(int32_t deg) { return qq_sin1024(90 - clamp(deg, 0, 90)); }

/* ângulo assinado em graus (0 = reto pra cima) -> velocidade base */
static void aim(qq_ball_t *b, int32_t deg, int32_t mag)
{
    int32_t a = iabs(deg);
    int32_t vx = (mag * qq_sin1024(a)) / 1024;
    b->vx = deg < 0 ? -vx : vx;
    b->vy = -(mag * qq_cos1024(a)) / 1024;
}

static void recompute(qq_game_t *g)
{
    memset(g->bon, 0, sizeof g->bon);
    memset(g->onu, 0, sizeof g->onu);
    for (int i = 0; i < g->nhist; i++) {
        const qq_inst_t *in = &g->hist[i];
        if (in->bonus_on) g->bon[in->card]++;
        if (in->onus_on)  g->onu[in->card]++;
    }
    /* sem o ônus das Gêmeas, a 2ª bola sai */
    if (!g->onu[QC_GEMEAS]) g->ball[1].on = false;
}

/* --- leituras derivadas -------------------------------------------------- */

int32_t qq_paddle_w(const qq_game_t *g)
{
    int32_t pct = 100 + 40 * g->bon[QC_TABUA] - 35 * g->onu[QC_AGULHA] - 20 * g->onu[QC_IMA];
    /* Elástico: `rate` em px*FP por passo (mão parada ~0, rápida ~50+) */
    if (g->bon[QC_ELASTICO]) pct += clamp((g->rate * 40) / 48, 0, 40);
    if (g->onu[QC_ELASTICO]) pct -= 30 - clamp((g->rate * 30) / 24, 0, 30);
    pct = clamp(pct, 40, 220);
    return (QQ_PADDLE_W * pct) / 100;
}

static bool twin_paddles(const qq_game_t *g) { return g->bon[QC_GEMEA] > 0; }

static int32_t twin_gap(const qq_game_t *g) { return g->onu[QC_GEMEA] ? QQ_GEMEA_GAP : 0; }

/* largura total ocupada (px) */
static int32_t paddle_span(const qq_game_t *g)
{
    int32_t w = qq_paddle_w(g);
    int32_t span = twin_paddles(g) ? 2 * w + twin_gap(g) : w;
    return clamp(span, 20, QQ_W - 20);
}

int qq_paddles(const qq_game_t *g, int32_t left_px[2])
{
    int32_t w = qq_paddle_w(g);
    int32_t c = g->paddle / QQ_FP;
    int32_t span = paddle_span(g);
    /* a largura muda depois do clamp (Elástico, carta nova): segura na tela aqui */
    int32_t l = clamp(c - span / 2, 0, QQ_W - span);
    left_px[0] = l;
    if (!twin_paddles(g)) { left_px[1] = l; return 1; }
    left_px[1] = l + span - w;
    return 2;
}

int32_t qq_ball_r(const qq_game_t *g)
{
    return clamp(QQ_BALL_R - 2 * g->onu[QC_CHUMBO] + 4 * g->bon[QC_MELANCIA], 4, 18);
}

int32_t qq_wall_y(const qq_game_t *g) { return QQ_WALL_Y + QQ_PRENSA_PX * g->onu[QC_PRENSA]; }

int32_t qq_fog_y(const qq_game_t *g)
{
    int32_t wy = qq_wall_y(g);
    if (!g->onu[QC_NEBLINA]) return wy;
    int32_t pct = g->onu[QC_NEBLINA] >= 2 ? 50 : 35;
    return wy + ((g->paddle_y - wy) * pct) / 100;
}

bool qq_silent(const qq_game_t *g)  { return g->onu[QC_MUDO] > 0; }
bool qq_portals(const qq_game_t *g) { return g->onu[QC_PORTAL] > 0; }

bool qq_ball_visible(const qq_game_t *g, int i)
{
    const qq_ball_t *b = &g->ball[i];
    if (!b->on) return false;
    if (g->blackout_t > 0) return false;
    if (g->onu[QC_FANTASMA] && !b->stuck) {
        int32_t wy = qq_wall_y(g), field = g->paddle_y - wy;
        int32_t y = b->y / QQ_FP;
        int32_t lo = wy + (field * 40) / 100, hi = wy + (field * 65) / 100;
        if (g->onu[QC_FANTASMA] >= 2) { lo = wy + (field * 30) / 100; hi = wy + (field * 72) / 100; }
        if (y > lo && y < hi) return false;
    }
    return true;
}

int32_t qq_mult_pct(const qq_game_t *g)
{
    int32_t m = 100 + 100 * g->bon[QC_AGULHA] + 50 * g->bon[QC_ESPELHO] + 50 * g->bon[QC_NEBLINA]
              + 25 * g->bon[QC_MUDO] + 100 * g->bon[QC_PRENSA];
    return clamp(m, 100, 800);
}

int32_t qq_speed_pct(const qq_game_t *g)
{
    int32_t prog = clamp(100 + SPEED_STEP_PCT * (g->hits / 10), 100, SPEED_PROG_MAX);
    int32_t card = 100 + 15 * g->onu[QC_TABUA] - 25 * g->bon[QC_CHUMBO]
                 + 20 * g->onu[QC_COMETA] + 25 * g->onu[QC_FREIO];
    card = clamp(card, 50, 200);
    int32_t s = (prog * card) / 100;
    if (g->onu[QC_METRONOMO]) {
        int32_t w = g->tick % 180;
        int32_t tri = w < 90 ? w : 180 - w;          /* 0..90 */
        s = (s * (80 + (tri * 40) / 90)) / 100;      /* ±20% em ondas de ~3 s */
    }
    if (g->slow_t > 0) s = (s * SLOW_PCT) / 100;
    return clamp(s, 30, SPEED_MAX_PCT);
}

int32_t qq_final_score(const qq_game_t *g)
{
    return g->onu[QC_FENIX] ? (g->score * 3) / 4 : g->score;
}

int qq_onus_active(const qq_game_t *g)
{
    int n = 0;
    for (int i = 0; i < g->nhist; i++) if (g->hist[i].onus_on) n++;
    return n;
}

/* --- ciclo --------------------------------------------------------------- */

static int32_t ball_y_on_paddle(const qq_game_t *g)
{
    return g->paddle_y * QQ_FP - qq_ball_r(g) * QQ_FP;
}

static void serve(qq_game_t *g, int i, int16_t steps)
{
    qq_ball_t *b = &g->ball[i];
    b->on = true;
    b->stuck = steps;
    b->stuck_off = (i == 1) ? 24 * QQ_FP : 0;
    b->x = g->paddle + b->stuck_off;
    b->y = ball_y_on_paddle(g);
    b->vx = 0;
    b->vy = -SPEED0;
    b->smash = b->portal = b->near_sent = false;
}

void qq_start(qq_game_t *g, int32_t range_cdeg, int8_t dir, qq_rng_t rng)
{
    memset(g, 0, sizeof *g);
    g->rng = rng;
    g->range_cdeg = clamp(range_cdeg, 800, 9000);
    g->dir = dir < 0 ? -1 : 1;
    g->lives = QQ_START_LIVES;
    g->next_pick = PICK_EVERY;
    g->paddle = g->target = g->prev_target = g->touch_x = (QQ_W / 2) * QQ_FP;
    g->ctl = QQ_CTL_TILT;
    g->gain_pct = 100;
    g->paddle_y = QQ_PADDLE_Y;
    g->shield_y = QQ_SHIELD_Y;
    g->floor_y = QQ_H;
    for (int i = 0; i < 8; i++) g->delay[i] = g->paddle;
    g->slip_wait = SLIP_EVERY;
    g->state = QS_PLAY;
    serve(g, 0, SERVE_STEPS);
}

static int32_t rnd(qq_game_t *g, int32_t lo, int32_t hi)
{
    return g->rng ? g->rng(lo, hi) : lo;
}

/* inclinação (giroscópio): ângulo -> posição */
static int32_t tilt_target(qq_game_t *g, int32_t lat, int32_t pitch, int32_t span)
{
    int32_t raw = g->onu[QC_PRUMO] ? pitch : lat;
    int32_t *z16 = g->onu[QC_PRUMO] ? &g->zero_pitch16 : &g->zero_lat16;
    *z16 += (raw * 16 - *z16) / LEAK_DIV;
    int32_t tilt = (raw - *z16 / 16) * g->dir;
    if (g->onu[QC_ESPELHO]) tilt = -tilt;

    int32_t range = g->range_cdeg;
    for (int i = 0; i < g->bon[QC_MOLA]; i++) range = (range * 60) / 100;
    if (range < 800) range = 800;

    if (!g->onu[QC_MOLA]) {
        if (iabs(tilt) <= DEADZONE_CDEG) tilt = 0;
        else tilt += tilt > 0 ? -DEADZONE_CDEG : DEADZONE_CDEG;
    }
    tilt = clamp(tilt, -range, range);

    int32_t half = ((QQ_W - span) / 2) * QQ_FP;
    return (QQ_W / 2) * QQ_FP + (tilt * half) / range;
}

/* toque: posição ABSOLUTA — a raquete fica em cima do dedo. O ganho amplia a
 * partir do centro pra raquete chegar na borda antes de o dedo chegar no
 * canto arredondado da tela. Sem dedo, a raquete fica onde está. */
static int32_t touch_target(qq_game_t *g, int32_t x, int32_t y, int32_t span)
{
    if (x >= 0) {
        int32_t px = x;
        if (g->onu[QC_PRUMO])            /* Prumo: a altura do dedo vira o lado */
            px = ((y - QQ_WALL_Y) * QQ_W) / (QQ_H - QQ_WALL_Y);
        if (g->onu[QC_ESPELHO]) px = QQ_W - px;
        int32_t gain = g->gain_pct;
        for (int i = 0; i < g->bon[QC_MOLA]; i++) gain = (gain * 125) / 100;
        g->touch_x = (QQ_W / 2) * QQ_FP + ((px - QQ_W / 2) * gain * QQ_FP) / 100;
    }
    int32_t lo = (span / 2) * QQ_FP, hi = (QQ_W - span / 2) * QQ_FP;
    g->touch_x = clamp(g->touch_x, lo, hi);
    return g->touch_x;
}

static void read_input(qq_game_t *g, int32_t a, int32_t b)
{
    int32_t span = paddle_span(g);
    int32_t target = (g->ctl == QQ_CTL_TOUCH) ? touch_target(g, a, b, span)
                                              : tilt_target(g, a, b, span);

    if (g->slip_t > 0) {
        int32_t ph = SLIP_STEPS - g->slip_t;
        int32_t tri = ph < SLIP_STEPS / 2 ? ph : SLIP_STEPS - ph;
        target += g->slip_dir * (tri * SLIP_PX * QQ_FP) / (SLIP_STEPS / 2);
    }

    int32_t raw_target = target;
    if (g->onu[QC_MOLA]) target += (target - g->prev_target) * 2;   /* tremor amplificado */
    g->prev_target = raw_target;

    if (g->onu[QC_PENA]) {                                           /* ~100 ms de atraso */
        int32_t out = g->delay[g->delay_i];
        g->delay[g->delay_i] = target;
        g->delay_i = (uint8_t)((g->delay_i + 1) % 6);
        target = out;
    }
    g->target = target;

    int32_t prev = g->paddle;
    if (g->bon[QC_PENA]) g->paddle += (target - g->paddle) / 3;
    else                 g->paddle = target;

    int32_t lo = (span / 2) * QQ_FP, hi = (QQ_W - span / 2) * QQ_FP;
    g->paddle = clamp(g->paddle, lo, hi);
    g->rate = (g->rate * 7 + iabs(g->paddle - prev)) / 8;
}

/* rebate a bola `b` na raquete de centro `pc` (px*FP) e meia-largura `hw` (px) */
static void bounce_paddle(qq_game_t *g, qq_ball_t *b, int32_t pc, int32_t hw)
{
    int32_t r = qq_ball_r(g);
    int32_t off = ((b->x - pc) * 1024) / ((hw + r) * QQ_FP);
    off = clamp(off, -1024, 1024);
    int32_t maxang = g->onu[QC_TIRO] ? 75 : 60;
    aim(b, (off * maxang) / 1024, SPEED0);
}

static int32_t hit_points(qq_game_t *g, qq_ball_t *b, int32_t off1024, uint32_t *ev)
{
    int32_t base = 1 + (g->bon[QC_GEMEAS] ? 1 : 0);
    if (g->bon[QC_TIRO] && iabs(off1024) < 205) { base += 2; *ev |= QE_CENTER; }
    if (g->bon[QC_PORTAL] && b->portal) base += 2;
    int32_t p = (base * qq_mult_pct(g)) / 100;
    return p < 1 ? 1 : p;
}

static void lose_ball(qq_game_t *g, int i, uint32_t *ev)
{
    *ev |= QE_MISS;
    g->lives -= g->onu[QC_VIDRO] ? 2 : 1;
    if (g->lives <= 0) {
        if (g->bon[QC_FENIX] && !g->phoenix_used) {
            g->phoenix_used = true;
            g->lives = 1;
            *ev |= QE_PHOENIX;
        } else {
            g->lives = 0;
            g->state = QS_OVER;
            *ev |= QE_OVER;
            return;
        }
    }
    serve(g, i, SERVE_STEPS);
}

static void step_ball(qq_game_t *g, int i, uint32_t *ev)
{
    qq_ball_t *b = &g->ball[i];
    if (!b->on) return;
    int32_t r = qq_ball_r(g) * QQ_FP;

    if (b->stuck > 0) {
        b->x = clamp(g->paddle + b->stuck_off, r, QQ_W * QQ_FP - r);
        b->y = ball_y_on_paddle(g);
        if (--b->stuck == 0) {
            if (b->stuck_off == 0 && i == 0) aim(b, rnd(g, -15, 15), SPEED0);
            else bounce_paddle(g, b, g->paddle, paddle_span(g) / 2);
            *ev |= QE_SERVE;
        }
        return;
    }

    /* Melancia: gravidade */
    if (g->onu[QC_MELANCIA] && g->tick % 3 == 0) {
        b->vy += g->onu[QC_MELANCIA];
        if (b->vy > 3 * SPEED0) b->vy = 3 * SPEED0;
    }

    int32_t sp = qq_speed_pct(g);
    int32_t dx = (b->vx * sp) / 100, dy = (b->vy * sp) / 100;
    if (dy == 0) dy = b->vy < 0 ? -1 : 1;
    int32_t py = b->y;
    b->x += dx;
    b->y += dy;

    /* laterais */
    if (qq_portals(g)) {
        if (b->x < 0)              { b->x += QQ_W * QQ_FP; b->portal = true; *ev |= QE_PORTAL; }
        else if (b->x >= QQ_W * QQ_FP) { b->x -= QQ_W * QQ_FP; b->portal = true; *ev |= QE_PORTAL; }
    } else {
        if (b->x - r < 0)                { b->x = r; b->vx = iabs(b->vx); *ev |= QE_SIDE; }
        else if (b->x + r > QQ_W * QQ_FP) { b->x = QQ_W * QQ_FP - r; b->vx = -iabs(b->vx); *ev |= QE_SIDE; }
    }

    /* parede de cima */
    int32_t wy = qq_wall_y(g) * QQ_FP;
    if (b->vy < 0 && b->y - r <= wy) {
        b->y = wy + r;
        g->wall_hits++;
        *ev |= QE_WALL;
        if (g->onu[QC_REBOTE] && g->wall_hits % 10 == 0) {
            int32_t a = rnd(g, 45, 70);
            aim(b, rnd(g, 0, 1) ? a : -a, SPEED0);
            b->vy = -b->vy;
        } else if (g->bon[QC_REBOTE]) {
            b->vx = 0;
            b->vy = SPEED0;
        } else {
            b->vy = iabs(b->vy);
        }
        if (b->smash) {
            b->smash = false;
            b->vy = SPEED0;
            int32_t p = (3 * qq_mult_pct(g)) / 100;
            g->score += p;
            *ev |= QE_SMASH_PT;
        }
    }

    /* Metrônomo: aviso antes de chegar */
    if (b->vy > 0 && g->bon[QC_METRONOMO] && !b->near_sent) {
        int32_t vy = (b->vy * sp) / 100;
        if (vy > 0) {
            int32_t eta = (g->paddle_y * QQ_FP - (b->y + r)) / vy;
            if (eta >= 0 && eta <= NEAR_STEPS) { b->near_sent = true; *ev |= QE_NEAR; }
        }
    }

    /* raquete(s) */
    int32_t top = g->paddle_y * QQ_FP;
    if (b->vy > 0 && py + r <= top + 2 * QQ_FP && b->y + r >= top) {
        int32_t lp[2];
        int n = qq_paddles(g, lp);
        int32_t w = (n == 2) ? qq_paddle_w(g) : paddle_span(g);
        for (int k = 0; k < n; k++) {
            int32_t l = lp[k] * QQ_FP, rr = (lp[k] + w) * QQ_FP;
            if (b->x + r < l || b->x - r > rr) continue;
            int32_t pc = (l + rr) / 2;
            int32_t off = ((b->x - pc) * 1024) / ((w / 2 + qq_ball_r(g)) * QQ_FP);
            b->y = top - r;
            bounce_paddle(g, b, pc, w / 2);
            g->score += hit_points(g, b, off, ev);
            g->hits++;
            b->portal = b->smash = b->near_sent = false;
            *ev |= QE_HIT;
            if (g->bon[QC_IMA]) {
                b->stuck = IMA_STEPS;
                b->stuck_off = b->x - g->paddle;
            }
            if (g->onu[QC_APAGAO]) {
                int every = g->onu[QC_APAGAO] >= 2 ? 8 : 15;
                if (g->hits % every == 0) { g->blackout_t = BLACKOUT_STEPS; *ev |= QE_BLACKOUT; }
            }
            return;
        }
    }

    /* escudo */
    if (g->shield > 0 && b->vy > 0 && b->y + r >= g->shield_y * QQ_FP) {
        b->y = g->shield_y * QQ_FP - r;
        b->vy = -iabs(b->vy);
        g->shield--;
        *ev |= QE_SHIELD;
        return;
    }

    if (b->y - r > g->floor_y * QQ_FP) lose_ball(g, i, ev);
}

void qq_use_touch(qq_game_t *g, int32_t gain_pct)
{
    g->ctl = QQ_CTL_TOUCH;
    g->gain_pct = clamp(gain_pct, 50, 400);
    g->paddle_y = QQ_PADDLE_Y_TOUCH;
    g->shield_y = QQ_SHIELD_Y_TOUCH;
    g->floor_y = QQ_STRIP_Y;
    for (int i = 0; i < QQ_MAX_BALLS; i++)
        if (g->ball[i].on && g->ball[i].stuck) g->ball[i].y = ball_y_on_paddle(g);
}

uint32_t qq_step(qq_game_t *g, int32_t a, int32_t b)
{
    if (g->state != QS_PLAY) return 0;
    uint32_t ev = 0;
    g->tick++;

    if (g->slow_t > 0) g->slow_t--;
    if (g->blackout_t > 0) g->blackout_t--;
    if (g->slip_t > 0) g->slip_t--;
    if (g->onu[QC_SACODE]) {
        if (--g->slip_wait <= 0) {
            g->slip_wait = SLIP_EVERY;
            g->slip_t = SLIP_STEPS;
            g->slip_dir = rnd(g, 0, 1) ? 1 : -1;
            ev |= QE_SLIP;
        }
    }

    read_input(g, a, b);

    for (int i = 0; i < QQ_MAX_BALLS && g->state == QS_PLAY; i++) step_ball(g, i, &ev);

    if (g->state == QS_PLAY && g->hits >= g->next_pick) {
        qq_make_offer(g);
        if (g->noffer > 0) { g->state = QS_PICK; ev |= QE_PICK; }
        else g->next_pick = g->hits + PICK_EVERY;
    }
    return ev;
}

uint32_t qq_shake(qq_game_t *g)
{
    if (g->state != QS_PLAY) return 0;
    bool down = false, up = false;
    for (int i = 0; i < QQ_MAX_BALLS; i++) {
        const qq_ball_t *b = &g->ball[i];
        if (!b->on || b->stuck) continue;
        if (b->vy > 0) down = true; else up = true;
    }
    /* Freio socorre a bola que desce; Sacode corta a que sobe */
    if (g->slow_charges && down && g->slow_t == 0) {
        g->slow_charges--;
        g->slow_t = SLOW_STEPS;
        return QE_SLOWMO;
    }
    if (g->smash_charges && up) {
        g->smash_charges--;
        for (int i = 0; i < QQ_MAX_BALLS; i++) {
            qq_ball_t *b = &g->ball[i];
            if (!b->on || b->stuck || b->vy >= 0) continue;
            b->vx = 0;
            b->vy = -(SPEED0 * 16) / 10;
            b->smash = true;
        }
        return QE_SMASH;
    }
    return 0;
}

/* --- cartas -------------------------------------------------------------- */

static bool eligible(const qq_game_t *g, int c)
{
    if (g->taken[c] >= QQ_CARDS[c].max) return false;
    int onus = qq_onus_active(g);
    if (c == QC_FAXINA && onus < 1) return false;
    if (c == QC_EXORCISMO && (onus < 3 || g->lives < 2)) return false;
    return true;
}

void qq_make_offer(qq_game_t *g)
{
    uint8_t pool[QC_COUNT];
    int n = 0;
    for (int c = 0; c < QC_COUNT; c++) if (eligible(g, c)) pool[n++] = (uint8_t)c;
    int want = g->bon[QC_GANANCIA] ? 4 : 3;
    if (want > n) want = n;
    for (int i = 0; i < want; i++) {
        int j = (int)rnd(g, i, n - 1);
        uint8_t t = pool[i]; pool[i] = pool[j]; pool[j] = t;
        g->offer[i] = pool[i];
    }
    g->noffer = (uint8_t)want;
}

static void faxina(qq_game_t *g, int self)
{
    for (int i = 0; i < g->nhist; i++)
        if (i != self && g->hist[i].onus_on) { g->hist[i].onus_on = false; break; }
    for (int i = 0; i < g->nhist; i++)
        if (i != self && g->hist[i].bonus_on) { g->hist[i].bonus_on = false; break; }
}

void qq_take(qq_game_t *g, qq_card_t c)
{
    if ((int)c < 0 || c >= QC_COUNT) return;
    g->taken[c]++;
    bool persistent = (c != QC_FAXINA && c != QC_EXORCISMO);
    int self = -1;
    if (g->nhist < QQ_MAX_HIST) {
        self = g->nhist;
        g->hist[g->nhist++] = (qq_inst_t){ (uint8_t)c, persistent, persistent };
    }

    switch (c) {
    case QC_FANTASMA: case QC_APAGAO: g->lives++; break;
    case QC_VIDRO:    g->lives += 2; break;
    case QC_PRUMO:    g->shield++; break;
    case QC_SACODE:   g->smash_charges += 3; break;
    case QC_FREIO:    g->slow_charges += 2; break;
    case QC_FAXINA:   faxina(g, self); break;
    case QC_EXORCISMO:
        for (int i = 0; i < g->nhist; i++) g->hist[i].onus_on = false;
        g->lives = 1;
        break;
    default: break;
    }
    if (g->lives > QQ_MAX_LIVES) g->lives = QQ_MAX_LIVES;
    recompute(g);

    if (c == QC_GEMEAS && g->onu[QC_GEMEAS] && !g->ball[1].on) serve(g, 1, SERVE_STEPS);
}

uint32_t qq_take_offer(qq_game_t *g, int idx)
{
    if (g->state != QS_PICK || idx < 0 || idx >= g->noffer) return 0;
    qq_take(g, (qq_card_t)g->offer[idx]);
    g->noffer = 0;
    g->next_pick = g->hits + (g->onu[QC_GANANCIA] ? PICK_EVERY_FAST : PICK_EVERY);
    g->state = QS_PLAY;
    /* volta com as bolas paradas na raquete: ninguém perde vida no susto */
    for (int i = 0; i < QQ_MAX_BALLS; i++)
        if (g->ball[i].on) serve(g, i, SERVE_STEPS);
    return QE_SERVE;
}

/* --- Cometa -------------------------------------------------------------- */

int qq_preview(const qq_game_t *g, int16_t xs[], int16_t ys[])
{
    const qq_ball_t *src = &g->ball[0];
    if (!g->bon[QC_COMETA] || !src->on || src->stuck) return 0;
    int32_t x = src->x, y = src->y, vx = src->vx, vy = src->vy;
    int32_t r = qq_ball_r(g) * QQ_FP, wy = qq_wall_y(g) * QQ_FP;
    int32_t sp = qq_speed_pct(g), top = g->paddle_y * QQ_FP;
    bool portals = qq_portals(g);
    int n = 0;
    for (int s = 1; s <= QQ_PREVIEW_N * PREVIEW_GAP && n < QQ_PREVIEW_N; s++) {
        x += (vx * sp) / 100;
        y += (vy * sp) / 100;
        if (portals) {
            if (x < 0) x += QQ_W * QQ_FP; else if (x >= QQ_W * QQ_FP) x -= QQ_W * QQ_FP;
        } else {
            if (x - r < 0) { x = r; vx = iabs(vx); }
            else if (x + r > QQ_W * QQ_FP) { x = QQ_W * QQ_FP - r; vx = -iabs(vx); }
        }
        if (vy < 0 && y - r <= wy) { y = wy + r; vy = iabs(vy); }
        if (vy > 0 && y + r >= top) break;
        if (s % PREVIEW_GAP == 0) {
            xs[n] = (int16_t)(x / QQ_FP);
            ys[n] = (int16_t)(y / QQ_FP);
            n++;
        }
    }
    return n;
}

/* --- texto dos efeitos --------------------------------------------------- */

static void append(char *out, int cap, int *len, const char *s)
{
    while (*s && *len < cap - 1) out[(*len)++] = *s++;
    out[*len] = 0;
}

void qq_fx_text(const qq_game_t *g, char *out, int cap)
{
    int len = 0;
    if (cap <= 0) return;
    out[0] = 0;
    for (int c = 0; c < QC_COUNT; c++) {
        int n = 0;
        for (int i = 0; i < g->nhist; i++)
            if (g->hist[i].card == c && (g->hist[i].bonus_on || g->hist[i].onus_on)) n++;
        if (!n) continue;
        if (len) append(out, cap, &len, " · ");
        append(out, cap, &len, QQ_CARDS[c].name);
        if (n > 1) {
            char t[4] = { 'x', (char)('0' + (n > 9 ? 9 : n)), 0, 0 };
            append(out, cap, &len, t);
        }
    }
}

/* --- top-5 --------------------------------------------------------------- */

int qq_hs_insert(int32_t hs[QQ_HS_N], int32_t score)
{
    if (score <= 0) return -1;
    int pos = -1;
    for (int i = 0; i < QQ_HS_N; i++) if (score > hs[i]) { pos = i; break; }
    if (pos < 0) return -1;
    /* troca em cadeia em vez de deslocar o array: o GCC transforma o
     * deslocamento em memmove, que firmwares antigos não exportam */
    int32_t carry = score;
    for (int i = pos; i < QQ_HS_N; i++) {
        int32_t t = hs[i];
        hs[i] = carry;
        carry = t;
    }
    return pos;
}
