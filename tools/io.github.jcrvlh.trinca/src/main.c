/**
 * @file main.c
 * @brief TRINCA — caça-níquel honesto, só pra brincar com a sorte.
 *
 * Três rolos, sete símbolos, nenhuma aposta. Toque em GIRAR (ou chacoalhe) e
 * os rolos param da esquerda pra direita. Uma barra mostra se a sessão está
 * com mais ou menos sorte que o acaso puro — o percentil exato, calculado em
 * trinca_game.c. A lógica pura (sorteio, chances, contador) mora lá.
 *
 * Decisões que não são óbvias:
 *  - Honestidade: o resultado é sorteado no toque (uma parada uniforme por
 *    rolo) e a animação rola a FAIXA REAL até ela. Nada de quase-acerto
 *    fabricado: o que passa na tela é a faixa de verdade.
 *  - Animação: sem lv_anim na tabela de símbolos. Um lv_timer avança a posição
 *    de cada rolo (px, sobre a faixa de 32 × PITCH) com ease-out cúbico em
 *    inteiro e, no fim, um quique curto. A pintura é função só da posição:
 *    4 imagens por rolo, reposicionadas a cada quadro. Duração e voltas de
 *    cada rolo são sorteadas a cada giro (a ordem de parada fica), depois do
 *    resultado e sem olhar pra ele — não dizem nada sobre o que vai sair.
 *  - Fora da linha do meio o símbolo esmaece por cor (recolor misturado com
 *    o fundo do rolo), não por sombra por cima: assim nada cobre o contorno
 *    amarelo, que é um objeto à parte desenhado por cima do rolo.
 *  - Som do giro (normal): `KIT_SFX_BOTTLE_SPIN`, a catraca de madeira da
 *    Garrafa (~1,9 s, desacelerando, fecha com um "parou"). Um efeito pronto
 *    roda inteiro na task de áudio, com silêncio ativo entre os tiques; uma
 *    catraca de bipes soltos vindos do timer deixava o DMA esvaziar entre um
 *    e outro e estalava. Por isso o ÚLTIMO rolo para fixo em BOTTLE_MS,
 *    casado com o "parou"; os dois primeiros seguem sorteados.
 *  - Rápida: a catraca não cabe em ~1 s — giro em silêncio e só um "clac"
 *    suave (bipe <= 14 ms, a faixa de ~1/3 da amplitude) por rolo.
 *  - Volume do KIT não é tocado: `set_volume` é global e não há como ler o
 *    valor pra devolver.
 *  - Barra de sorte: olha pra trás, nunca pra frente. O texto é sempre
 *    retrospectivo ("mais sorte que 91%") — nada de "tá devendo".
 */
#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "trinca_game.h"
#include "symbols.h"

#include <stdio.h>
#include <string.h>

#ifndef KIT_SDK_STUBS

/* ----------------------------------------------------------------------- */

#define T_ACCENT     KIT_COLOR_YELLOW

#define REEL_W       104
#define REEL_GAP     12
#define REELS_X      KIT_UI_PAD                                   /* 16 */
#define REELS_Y      30
#define PITCH        80                                           /* uma parada */
#define WIN_H        (2 * PITCH)                                  /* 160: 1 cheia + 2 meias */
#define WIN_MID      (WIN_H / 2)
#define REEL_RADIUS  16
#define FRAME_W      4                                            /* contorno da dupla/trinca */
#define DIM_STEPS    6                                            /* degraus de esmaecer */
#define DIM_MIN      64                                           /* força do símbolo mais longe (/255) */
#define STRIP_PX     (TRINCA_STOPS * PITCH)                       /* 2560 */
#define SLOTS        4                                            /* casas desenhadas por rolo */

#define ROW_Y        204                                          /* AZAR · legenda · SORTE */
#define BAR_Y        234
#define BAR_H        10
#define MARK_W       10
#define MARK_H       26

#define ANIM_MS      20
#define BOUNCE_FR    6
#define BOUNCE_PX    10
#define BLINK_MS     70     /* passo da comemoração: 1 nota; o contorno pisca a cada 2 */
#define SAVE_MS      1500

/* som: tudo <= 14 ms cai na faixa suave do firmware (~1/3 da amplitude) */
#define SOFT_MS      12
#define CLAC_HZ      520     /* rolo parou (só na rápida) */

/* KIT_SFX_BOTTLE_SPIN: 20 tiques de 8 ms com silêncio abrindo de 14 a 140 ms
 * (~1,83 s) e o "parou" de 80 ms. Somando os 80 ms que o amplificador leva pra
 * acordar quando o áudio estava parado, o "parou" cai entre 1,83 e 1,99 s —
 * o último rolo para no meio disso. */
#define BOTTLE_MS    1900
#define REEL_MIN_GAP 250     /* rolo do meio: ao menos isso depois do 1º e antes do último */

/* Trinca: pentatônica de dó subindo duas oitavas, um trinado mi-sol e o dó
 * agudo pra fechar — uma nota por passo do timer. Cada nota são 2 bipes
 * suaves colados (24 ms): corpo de nota sem sair da faixa baixa de volume. A
 * fila de bipes do firmware tem 6 lugares e não espera, então nunca mais que
 * 3 bipes por passo. */
static const uint16_t CASCADE_HZ[] = {
    523, 659, 784, 1047, 1319, 1568, 2093,      /* dó mi sol, 2 oitavas */
    2637, 3136, 2637, 3136,                     /* trinado */
    2093,                                       /* dó, um tico mais longo */
};
#define CASCADE_N  ((int)(sizeof CASCADE_HZ / sizeof CASCADE_HZ[0]))

/* Tempo de cada rolo, sorteado a cada giro: o 1º para em [first], cada
 * seguinte [gap] depois do anterior — a ordem esquerda → direita fica. No
 * normal o último é fixo (BOTTLE_MS) e o do meio cai entre os dois. Voltas
 * inteiras na faixa também sorteadas, então a velocidade muda de rolo pra
 * rolo e de giro pra giro. [normal, rápida]. */
static const int SPEED_FIRST[2][2] = { { 750, 1050 }, { 420, 600 } };
static const int SPEED_GAP[2][2]   = { { 0, 0 },      { 150, 340 } };   /* normal usa BOTTLE_MS */
static const int SPEED_LOOPS[2][2] = { { 2, 5 },      { 1, 3 } };

static const uint32_t SYM_COLOR[TRINCA_SYMBOLS] = {
    KIT_COLOR_BLUE,    /* círculo */
    KIT_COLOR_YELLOW,  /* triângulo */
    KIT_COLOR_RED,     /* quadrado */
    KIT_COLOR_GREEN,   /* losango */
    KIT_COLOR_TEXT,    /* cruz */
    KIT_COLOR_TEXT,    /* anel */
    KIT_COLOR_YELLOW,  /* estrela (rara) */
};

static const char *const SHAKE_LABELS[] = { "SIM", "NÃO" };
static const char *const SPEED_LABELS[] = { "NORMAL", "RÁPIDA" };

static const char RULES[] =
    "Toque em GIRAR, nos rolos ou chacoalhe o KIT.\n\n"
    "2 iguais na linha do meio: DUPLA.\n"
    "3 iguais: TRINCA.\n"
    "3 estrelas: TRINCA RARA.\n\n"
    "Não tem aposta nem prêmio. É só sorte.\n\n"
    "CHANCES\n"
    "Dupla: 38%\n"
    "Trinca: 1 em 43\n"
    "Rara: 1 em 4.096\n\n"
    "É sorteio de verdade: o resultado sai no toque e os rolos só mostram "
    "onde ele caiu. Nada de quase-acerto armado.\n\n"
    "A BARRA\n"
    "Compara a sua sorte da sessão com o acaso puro (dupla vale 1, trinca "
    "vale 3). Aparece depois de 20 giros.\n\n"
    "Ela só olha pra trás. Azar acumulado não deixa a próxima trinca mais "
    "perto: ninguém \"tá devendo\".\n\n"
    "HISTÓRICO\n"
    "No AJUSTE: o que saiu pra você ao lado do que o acaso daria.";

/* ----------------------------------------------------------------------- */

typedef struct {
    lv_obj_t *win;
    lv_obj_t *img[SLOTS];
    int8_t    shown[SLOTS];     /* símbolo em cada imagem (-1 = nenhum ainda) */
    int8_t    shown_dim[SLOTS]; /* degrau de esmaecer aplicado */
    int32_t   pos;              /* px sobre a faixa, em [0, STRIP_PX) quando parado */
    int32_t   from, dist;       /* giro em curso */
    int       dur, frame;       /* em quadros de ANIM_MS */
    bool      landed;
} reel_t;

static const kit_api_table_t *s_api;
static lv_obj_t *s_screen;
static kit_ui_shell_t  s_shell;
static kit_ui_chips_t  s_shake_chips;
static kit_ui_chips_t  s_speed_chips;
static kit_ui_action_t s_action;

static reel_t        s_reel[TRINCA_REELS];
static uint8_t       s_target[TRINCA_REELS];
static trinca_luck_t s_luck;
static uint32_t      s_odds[4];

static bool  s_spinning;
static int   s_shake_idx;            /* 0 = gira chacoalhando */
static int   s_speed_idx;            /* 0 = normal */
static int   s_landed;               /* rolos que já pararam neste giro */
static uint64_t s_spin_t0;           /* início do giro (ms), pra casar com o som */
static int   s_spin_fr;              /* contador de quadros, se não houver relógio */

/* vida deste KIT */
static int32_t s_life_spins, s_life_duplas, s_life_trincas, s_life_raras, s_life_rara1;
static bool    s_dirty;

static lv_obj_t *s_stats_lbl;
static lv_obj_t *s_caption_lbl;
static lv_obj_t *s_bar_fill;
static lv_obj_t *s_bar_mark;

static lv_obj_t *s_frame[TRINCA_REELS];   /* contorno por cima de cada rolo */
static lv_obj_t *s_life_you[4];
static lv_obj_t *s_life_exp[4];
static lv_obj_t *s_life_note;
static lv_obj_t *s_reset_btn;
static lv_obj_t *s_reset_lbl;
static bool      s_reset_armed;

static lv_obj_t *s_overlay;
static lv_obj_t *s_ov_line;

static lv_timer_t *s_anim_timer;
static lv_timer_t *s_blink_timer;
static lv_timer_t *s_save_timer;
static int         s_blink_n;
static bool        s_hl[TRINCA_REELS];     /* rolos que fazem parte da dupla/trinca */

/* ---------------------------------------------------------------- util */

static int32_t rng(int32_t lo, int32_t hi) { return kit_ui_rnd(lo, hi); }

static int32_t get_i32(const char *key, int32_t def)
{
    int32_t v;
    if (s_api && s_api->storage && s_api->storage->get_i32(key, &v) == KIT_OK) return v;
    return def;
}

static void set_i32(const char *key, int32_t v)
{
    if (s_api && s_api->storage) s_api->storage->set_i32(key, v);
}

/* 1234567 -> "1.234.567" */
static void fmt_int(char *out, size_t n, uint32_t v)
{
    char tmp[16];
    int len = snprintf(tmp, sizeof tmp, "%u", (unsigned)v);
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < n; i++) {
        if (i > 0 && (len - i) % 3 == 0 && o + 2 < n) out[o++] = '.';
        out[o++] = tmp[i];
    }
    out[o] = '\0';
}

/* décimos -> "28,5" (com milhar na parte inteira) */
static void fmt_x10(char *out, size_t n, uint32_t x10)
{
    char ip[16];
    fmt_int(ip, sizeof ip, x10 / 10);
    snprintf(out, n, "%s,%u", ip, (unsigned)(x10 % 10));
}

/* bipe na faixa suave do firmware */
static void soft(uint16_t hz) { kit_ui_beep(hz, SOFT_MS); }

/* ------------------------------------------------------- persistência */

static void save_life(void)
{
    set_i32("tr_giros", s_life_spins);
    set_i32("tr_duplas", s_life_duplas);
    set_i32("tr_trincas", s_life_trincas);
    set_i32("tr_raras", s_life_raras);
    set_i32("tr_rara1", s_life_rara1);
    s_dirty = false;
}

static int32_t non_neg(int32_t v) { return v < 0 ? 0 : v; }

static void load_all(void)
{
    int32_t v = get_i32("tr_shake", 0);
    s_shake_idx = (v == 1) ? 1 : 0;
    v = get_i32("tr_speed", 0);
    s_speed_idx = (v == 1) ? 1 : 0;

    s_life_spins   = non_neg(get_i32("tr_giros", 0));
    s_life_duplas  = non_neg(get_i32("tr_duplas", 0));
    s_life_trincas = non_neg(get_i32("tr_trincas", 0));
    s_life_raras   = non_neg(get_i32("tr_raras", 0));
    s_life_rara1   = non_neg(get_i32("tr_rara1", 0));
}

static void save_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_dirty && !s_spinning) save_life();
}

/* -------------------------------------------------------------- rolos */

static int32_t wrap(int32_t p)
{
    p %= STRIP_PX;
    return p < 0 ? p + STRIP_PX : p;
}

/* Cor do símbolo misturada com o fundo do rolo: degrau 0 = cor cheia (linha
 * do meio), DIM_STEPS = só DIM_MIN/255 da cor. */
static uint32_t dim_color(uint32_t c, int step)
{
    int f = 255 - step * (255 - DIM_MIN) / DIM_STEPS;
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int a = (int)((c >> sh) & 0xFF), b = (int)((KIT_COLOR_SURFACE >> sh) & 0xFF);
        out |= (uint32_t)(b + (a - b) * f / 255) << sh;
    }
    return out;
}

/* Pinta o rolo a partir da posição: a parada c (= pos / PITCH) desce `frac`
 * px a partir do meio; a c+1 vem logo acima, a c-1 logo abaixo. */
static void paint_reel(reel_t *r)
{
    int32_t p = wrap(r->pos);
    int c = (int)(p / PITCH), frac = (int)(p % PITCH);
    for (int k = 0; k < SLOTS; k++) {
        int rel = k - 1;                                   /* -1, 0, +1, +2 */
        int stop = (c + rel + TRINCA_STOPS) % TRINCA_STOPS;
        int sym = TRINCA_STRIP[stop];
        lv_obj_t *im = r->img[k];
        int yc = WIN_MID - rel * PITCH + frac;

        int d = yc > WIN_MID ? yc - WIN_MID : WIN_MID - yc;
        int dim = d * DIM_STEPS / PITCH;
        if (dim > DIM_STEPS) dim = DIM_STEPS;

        if (r->shown[k] != sym) {
            lv_image_set_src(im, SYM_IMG[sym]);
            r->shown[k] = (int8_t)sym;
            r->shown_dim[k] = -1;
        }
        if (r->shown_dim[k] != dim) {
            lv_obj_set_style_image_recolor(im, lv_color_hex(dim_color(SYM_COLOR[sym], dim)), 0);
            r->shown_dim[k] = (int8_t)dim;
        }
        lv_obj_set_pos(im, (REEL_W - SYM_IMG_SIZE) / 2, yc - SYM_IMG_SIZE / 2);
    }
}

static void paint_highlight(bool on)
{
    for (int i = 0; i < TRINCA_REELS; i++) {
        if (on && s_hl[i]) lv_obj_remove_flag(s_frame[i], LV_OBJ_FLAG_HIDDEN);
        else               lv_obj_add_flag(s_frame[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ease-out cúbico em 1/1024: 1 - (1 - x)^3 */
static int32_t ease_out(int frame, int dur)
{
    int32_t x = (int32_t)frame * 1024 / dur;
    if (x > 1024) x = 1024;
    int32_t u = 1024 - x;
    return 1024 - ((u * u) >> 10) * u / 1024;
}

/* ----------------------------------------------------- barra e placar */

static void paint_stats(void)
{
    char g[16];
    fmt_int(g, sizeof g, s_luck.spins);
    lv_label_set_text_fmt(s_stats_lbl, "%s %s  %u %s  %u %s",
        g, s_luck.spins == 1 ? "GIRO" : "GIROS",
        (unsigned)s_luck.duplas, s_luck.duplas == 1 ? "DUPLA" : "DUPLAS",
        (unsigned)s_luck.trincas, s_luck.trincas == 1 ? "TRINCA" : "TRINCAS");
}

static void paint_meter(void)
{
    int pm = trinca_luck_permille(&s_luck);
    if (pm < 0) {
        lv_label_set_text_fmt(s_caption_lbl, "MEDINDO %d/%d",
                              trinca_luck_n(&s_luck), TRINCA_MIN_SPINS);
        lv_obj_set_style_text_color(s_caption_lbl, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_obj_add_flag(s_bar_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_bar_fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* nunca "100%" nem "0%": é sempre "mais/menos que", não certeza */
    int pct = (pm + 5) / 10;
    if (pct < 1) pct = 1;
    if (pct > 99) pct = 99;
    if (pm >= 500) lv_label_set_text_fmt(s_caption_lbl, "MAIS SORTE QUE %d%%", pct);
    else           lv_label_set_text_fmt(s_caption_lbl, "MENOS SORTE QUE %d%%", 100 - pct);
    lv_obj_set_style_text_color(s_caption_lbl, lv_color_hex(KIT_COLOR_TEXT), 0);

    int span = KIT_UI_CONTENT - MARK_W;
    int mx = pm * span / 1000;                      /* canto esquerdo do marcador */
    int cx = span / 2;
    lv_obj_set_pos(s_bar_mark, REELS_X + mx, BAR_Y + BAR_H / 2 - MARK_H / 2);
    lv_obj_remove_flag(s_bar_mark, LV_OBJ_FLAG_HIDDEN);

    int a = mx < cx ? mx : cx, b = mx < cx ? cx : mx;
    lv_obj_set_pos(s_bar_fill, REELS_X + MARK_W / 2 + a, BAR_Y);
    lv_obj_set_width(s_bar_fill, b - a > 0 ? b - a : 1);
    lv_obj_remove_flag(s_bar_fill, LV_OBJ_FLAG_HIDDEN);
}

static void paint_life(void)
{
    char a[24], b[24];
    static const uint8_t KIND[4] = { 0, TRINCA_DUPLA, TRINCA_TRINCA, TRINCA_RARA };
    int32_t got[4] = { s_life_spins, s_life_duplas, s_life_trincas, s_life_raras };

    for (int i = 0; i < 4; i++) {
        fmt_int(a, sizeof a, (uint32_t)got[i]);
        lv_label_set_text(s_life_you[i], a);
        if (i == 0) continue;                       /* giros: o acaso não tem palpite */
        uint32_t odds = s_odds[KIND[i]];
        if (KIND[i] == TRINCA_TRINCA) odds += s_odds[TRINCA_RARA];   /* trinca inclui a rara */
        fmt_x10(b, sizeof b, trinca_expected_x10((uint32_t)s_life_spins, odds));
        lv_label_set_text(s_life_exp[i], b);
    }

    if (s_life_rara1 > 0) {
        fmt_int(a, sizeof a, (uint32_t)s_life_rara1);
        lv_label_set_text_fmt(s_life_note, "A 1ª trinca rara saiu no giro %s.", a);
    } else {
        lv_label_set_text(s_life_note, "Trinca rara: 1 em 4.096 giros.");
    }
}

/* ------------------------------------------------------- fluxo do giro */

static void blink_stop(void)
{
    if (s_blink_timer) { lv_timer_delete(s_blink_timer); s_blink_timer = NULL; }
}

static void cascade_note(int n)
{
    int reps = (n == CASCADE_N - 1) ? 3 : 2;
    for (int i = 0; i < reps; i++) soft(CASCADE_HZ[n]);
}

static void blink_cb(lv_timer_t *t)
{
    (void)t;
    s_blink_n++;
    paint_highlight((s_blink_n / 2) % 2 == 0);
    cascade_note(s_blink_n);
    if (s_blink_n >= CASCADE_N - 1) { blink_stop(); paint_highlight(true); }
}

static void show_rare(void)
{
    char n[16];
    fmt_int(n, sizeof n, (uint32_t)s_life_raras);
    lv_label_set_text_fmt(s_ov_line, "1 em 4.096 giros.\nEssa é a nº %s deste KIT.", n);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void finish_spin(void)
{
    s_spinning = false;
    kit_ui_action_show(&s_action, true);

    uint8_t sym[TRINCA_REELS];
    for (int i = 0; i < TRINCA_REELS; i++) sym[i] = TRINCA_STRIP[s_target[i]];
    trinca_kind_t k = trinca_classify(sym);

    trinca_luck_add(&s_luck, k);
    s_life_spins++;
    if (k == TRINCA_DUPLA) s_life_duplas++;
    if (k == TRINCA_TRINCA || k == TRINCA_RARA) s_life_trincas++;
    if (k == TRINCA_RARA) {
        s_life_raras++;
        if (!s_life_rara1) s_life_rara1 = s_life_spins;
    }
    s_dirty = true;

    for (int i = 0; i < TRINCA_REELS; i++) {
        int a = (i + 1) % TRINCA_REELS, b = (i + 2) % TRINCA_REELS;
        s_hl[i] = (k != TRINCA_NADA) && (sym[i] == sym[a] || sym[i] == sym[b]);
    }

    switch (k) {
    case TRINCA_DUPLA:
        paint_highlight(true);
        soft(784);
        soft(1175);
        break;
    case TRINCA_TRINCA:
    case TRINCA_RARA:
        s_blink_n = 0;
        paint_highlight(true);
        cascade_note(0);
        s_blink_timer = lv_timer_create(blink_cb, BLINK_MS, NULL);
        if (k == TRINCA_RARA) show_rare();     /* a cascata toca por trás da tela da estrela */
        break;
    default:
        break;
    }

    paint_stats();
    paint_meter();
    paint_life();
}

static void anim_stop(void)
{
    if (s_anim_timer) { lv_timer_delete(s_anim_timer); s_anim_timer = NULL; }
}

/* Quadro do giro pelo relógio, não pela contagem de chamadas: se a tela não
 * segurar 50 fps o rolo pula quadros em vez de atrasar — e o último continua
 * parando junto com o "parou" da catraca, que corre na task de áudio. */
static int spin_frame(void)
{
    if (s_api && s_api->time)
        return (int)((uint32_t)(s_api->time->get_millis() - s_spin_t0) / ANIM_MS);
    return ++s_spin_fr;
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    bool all_done = true;
    int now = spin_frame();

    for (int i = 0; i < TRINCA_REELS; i++) {
        reel_t *r = &s_reel[i];
        if (r->frame > r->dur + BOUNCE_FR) continue;
        r->frame = now > r->dur + BOUNCE_FR + 1 ? r->dur + BOUNCE_FR + 1 : now;

        int32_t p;
        if (r->frame <= r->dur) {
            p = r->from + r->dist * ease_out(r->frame, r->dur) / 1024;
        } else {
            /* quique: passa BOUNCE_PX da linha e volta */
            int f = r->frame - r->dur;
            int tri = f <= BOUNCE_FR / 2 ? f : BOUNCE_FR - f;
            p = r->from + r->dist + tri * BOUNCE_PX / (BOUNCE_FR / 2);
        }
        r->pos = p;

        if (r->frame >= r->dur && !r->landed) {
            r->landed = true;
            s_landed++;
            /* no normal o "parou" é da própria catraca; um bipe aqui ficaria
             * na fila atrás dela e sairia atrasado */
            if (s_speed_idx == 1) soft(CLAC_HZ);
        }
        if (r->frame > r->dur + BOUNCE_FR) r->pos = wrap(r->from + r->dist);
        else all_done = false;
        paint_reel(r);
    }

    if (all_done) { anim_stop(); finish_spin(); }
}

static void start_spin(void)
{
    if (s_spinning || !lv_obj_has_flag(s_overlay, LV_OBJ_FLAG_HIDDEN)) return;
    s_spinning = true;
    blink_stop();
    for (int i = 0; i < TRINCA_REELS; i++) s_hl[i] = false;
    paint_highlight(false);
    kit_ui_action_show(&s_action, false);

    /* o resultado sai AGORA; a animação só leva a faixa até ele */
    trinca_spin(s_target, rng);

    /* tempo e voltas de cada rolo: sorteados depois, sem olhar o resultado */
    const int *first = SPEED_FIRST[s_speed_idx], *gap = SPEED_GAP[s_speed_idx];
    const int *loops = SPEED_LOOPS[s_speed_idx];
    int stop_ms[TRINCA_REELS];
    stop_ms[0] = rng(first[0], first[1]);
    if (s_speed_idx == 0) {
        stop_ms[2] = BOTTLE_MS;
        stop_ms[1] = rng(stop_ms[0] + REEL_MIN_GAP, BOTTLE_MS - REEL_MIN_GAP);
    } else {
        for (int i = 1; i < TRINCA_REELS; i++) stop_ms[i] = stop_ms[i - 1] + rng(gap[0], gap[1]);
    }
    for (int i = 0; i < TRINCA_REELS; i++) {
        reel_t *r = &s_reel[i];
        int ms = stop_ms[i];
        int cur = (int)(wrap(r->pos) / PITCH);
        int delta = ((int)s_target[i] - cur + TRINCA_STOPS) % TRINCA_STOPS;
        r->from = (int32_t)cur * PITCH;
        r->dist = (int32_t)(rng(loops[0], loops[1]) * TRINCA_STOPS + delta) * PITCH;
        r->dur = ms / ANIM_MS;
        r->frame = 0;
        r->landed = false;
    }
    s_landed = 0;
    s_spin_fr = 0;
    s_spin_t0 = (s_api && s_api->time) ? s_api->time->get_millis() : 0;
    if (s_speed_idx == 0) kit_ui_sfx(KIT_SFX_BOTTLE_SPIN);
    anim_stop();
    s_anim_timer = lv_timer_create(anim_cb, ANIM_MS, NULL);
}

/* ----------------------------------------------------------- entradas */

static void spin_cb(lv_event_t *e)
{
    (void)e;
    start_spin();
}

static void on_shake(void *user)
{
    (void)user;
    if (s_shake_idx != 0 || kit_ui_shell_active(&s_shell) != 1) return;
    start_spin();
}

static void overlay_cb(lv_event_t *e)
{
    (void)e;
    kit_ui_click();
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* --------------------------------------------------------------- AJUSTE */

static void shake_cb(int idx, void *user)
{
    (void)user;
    s_shake_idx = idx;
    set_i32("tr_shake", idx);
}

static void speed_cb(int idx, void *user)
{
    (void)user;
    s_speed_idx = idx;
    set_i32("tr_speed", idx);
}

static void paint_reset_btn(void)
{
    lv_obj_set_style_bg_color(s_reset_btn,
        lv_color_hex(s_reset_armed ? KIT_COLOR_RED : KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(s_reset_lbl,
        lv_color_hex(s_reset_armed ? KIT_COLOR_ON_COLOR : KIT_COLOR_TEXT), 0);
    lv_label_set_text(s_reset_lbl, s_reset_armed ? "TOQUE DE NOVO" : "ZERAR HISTÓRICO");
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    if (s_spinning) return;
    if (!s_reset_armed) {
        s_reset_armed = true;
        kit_ui_click();
        paint_reset_btn();
        return;
    }
    s_reset_armed = false;
    paint_reset_btn();
    kit_ui_sfx(KIT_SFX_BACK);

    s_life_spins = s_life_duplas = s_life_trincas = s_life_raras = s_life_rara1 = 0;
    trinca_luck_reset(&s_luck);
    save_life();
    blink_stop();
    paint_highlight(false);
    paint_stats();
    paint_meter();
    paint_life();
}

static void section_label(lv_obj_t *p, const char *txt)
{
    kit_ui_label(p, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

/* Tabela do HISTÓRICO: nome | VOCÊ | ACASO, colunas de número à direita. */
#define COL_YOU   92
#define COL_EXP   104

static lv_obj_t *cell(lv_obj_t *row, const char *txt, uint32_t color,
                      const lv_font_t *font, int ls, int width)
{
    lv_obj_t *l = kit_ui_label(row, txt, color, font, ls);
    if (width) {
        lv_obj_set_width(l, width);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    } else {
        lv_obj_set_flex_grow(l, 1);
    }
    return l;
}

static lv_obj_t *table_row(lv_obj_t *card)
{
    lv_obj_t *row = kit_ui_box(card);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static void life_row(lv_obj_t *card, int i, const char *name)
{
    lv_obj_t *row = table_row(card);
    cell(row, name, KIT_COLOR_TEXT, &kit_sans_22, 0, 0);
    s_life_you[i] = cell(row, "", KIT_COLOR_TEXT, &kit_sans_22, 0, COL_YOU);
    s_life_exp[i] = cell(row, "", KIT_COLOR_TEXT, &kit_sans_22, 0, COL_EXP);
}

static void build_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_hor(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_top(p, 8, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    section_label(p, "GIRAR CHACOALHANDO");
    kit_ui_chips(&s_shake_chips, p, SHAKE_LABELS, 2, s_shake_idx, T_ACCENT, shake_cb, NULL);

    section_label(p, "VELOCIDADE");
    kit_ui_chips(&s_speed_chips, p, SPEED_LABELS, 2, s_speed_idx, T_ACCENT, speed_cb, NULL);

    section_label(p, "HISTÓRICO");
    lv_obj_t *card = kit_ui_rect(p, lv_pct(100), LV_SIZE_CONTENT, KIT_COLOR_SURFACE, 18);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hdr = table_row(card);
    cell(hdr, "", KIT_COLOR_TEXT, &kit_mono_16, 0, 0);
    cell(hdr, "VOCÊ", KIT_COLOR_TEXT, &kit_mono_16, 2, COL_YOU);
    cell(hdr, "ACASO", KIT_COLOR_TEXT, &kit_mono_16, 2, COL_EXP);

    life_row(card, 0, "Giros");
    life_row(card, 1, "Duplas");
    life_row(card, 2, "Trincas");
    life_row(card, 3, "Raras");
    s_life_note = kit_ui_text(card, "", KIT_COLOR_TEXT, &kit_sans_22, KIT_UI_CONTENT - 32);
    lv_obj_set_style_text_align(s_life_note, LV_TEXT_ALIGN_LEFT, 0);

    s_reset_btn = lv_obj_create(p);
    lv_obj_set_size(s_reset_btn, lv_pct(100), 84);
    lv_obj_set_style_radius(s_reset_btn, 42, 0);
    lv_obj_set_style_border_width(s_reset_btn, 0, 0);
    lv_obj_set_style_pad_all(s_reset_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_reset_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_reset_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_reset_btn, 8);
    lv_obj_add_event_cb(s_reset_btn, reset_cb, LV_EVENT_CLICKED, NULL);
    s_reset_lbl = kit_ui_label(s_reset_btn, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_center(s_reset_lbl);
    paint_reset_btn();
}

/* ----------------------------------------------------------------- JOGO */

static void build_overlay(lv_obj_t *tile)
{
    s_overlay = kit_ui_box(tile);
    lv_obj_set_size(s_overlay, KIT_UI_SCREEN_W, KIT_UI_PAGE_H);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, overlay_cb, LV_EVENT_CLICKED, NULL);
    kit_ui_flex(s_overlay, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 12, 0);

    lv_obj_t *star = lv_image_create(s_overlay);
    lv_image_set_src(star, SYM_IMG[TRINCA_RARE]);
    lv_obj_set_style_image_recolor_opa(star, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(star, lv_color_hex(T_ACCENT), 0);
    lv_obj_remove_flag(star, LV_OBJ_FLAG_CLICKABLE);

    kit_ui_label(s_overlay, "RARA", T_ACCENT, &kit_display_72, 0);
    kit_ui_label(s_overlay, "TRÊS ESTRELAS", KIT_COLOR_TEXT, &kit_mono_20, 2);
    s_ov_line = kit_ui_text(s_overlay, "", KIT_COLOR_TEXT, &kit_sans_22, KIT_UI_CONTENT);
    kit_ui_label(s_overlay, "TOQUE PARA FECHAR", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void build_jogo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    s_stats_lbl = kit_ui_label(tile, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0);
    lv_obj_align(s_stats_lbl, LV_ALIGN_TOP_MID, 0, 2);

    for (int i = 0; i < TRINCA_REELS; i++) {
        reel_t *r = &s_reel[i];
        lv_obj_t *w = kit_ui_rect(tile, REEL_W, WIN_H, KIT_COLOR_SURFACE, REEL_RADIUS);
        lv_obj_set_pos(w, REELS_X + i * (REEL_W + REEL_GAP), REELS_Y);
        kit_ui_tap(w, spin_cb, i);
        lv_obj_set_style_bg_opa(w, LV_OPA_COVER, LV_STATE_PRESSED);   /* rolo não "afunda" */
        r->win = w;

        for (int k = 0; k < SLOTS; k++) {
            lv_obj_t *im = lv_image_create(w);
            lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
            lv_obj_remove_flag(im, LV_OBJ_FLAG_CLICKABLE);
            r->img[k] = im;
            r->shown[k] = -1;
        }
        r->pos = (int32_t)rng(0, TRINCA_STOPS - 1) * PITCH;
        paint_reel(r);
    }

    /* contorno da dupla/trinca: irmão criado DEPOIS dos rolos, então é
     * desenhado por cima de tudo — nenhum símbolo come a linha */
    for (int i = 0; i < TRINCA_REELS; i++) {
        lv_obj_t *f = kit_ui_rect(tile, REEL_W, WIN_H, KIT_COLOR_BG, REEL_RADIUS);
        lv_obj_set_pos(f, REELS_X + i * (REEL_W + REEL_GAP), REELS_Y);
        lv_obj_set_style_bg_opa(f, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(f, lv_color_hex(T_ACCENT), 0);
        lv_obj_set_style_border_width(f, FRAME_W, 0);
        lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(f, LV_OBJ_FLAG_HIDDEN);
        s_frame[i] = f;
    }

    /* AZAR · legenda · SORTE, e a barra embaixo */
    lv_obj_t *l = kit_ui_label(tile, "AZAR", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_set_pos(l, REELS_X, ROW_Y);
    l = kit_ui_label(tile, "SORTE", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -REELS_X, ROW_Y);
    s_caption_lbl = kit_ui_label(tile, "", KIT_COLOR_TEXT, &kit_mono_16, 1);
    lv_obj_align(s_caption_lbl, LV_ALIGN_TOP_MID, 0, ROW_Y);

    lv_obj_t *track = kit_ui_rect(tile, KIT_UI_CONTENT, BAR_H, KIT_COLOR_SURFACE, BAR_H / 2);
    lv_obj_set_pos(track, REELS_X, BAR_Y);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *mid = kit_ui_rect(tile, 2, MARK_H - 6, KIT_COLOR_LINE, 0);
    lv_obj_set_pos(mid, REELS_X + KIT_UI_CONTENT / 2 - 1, BAR_Y + BAR_H / 2 - (MARK_H - 6) / 2);
    lv_obj_remove_flag(mid, LV_OBJ_FLAG_CLICKABLE);

    s_bar_fill = kit_ui_rect(tile, 1, BAR_H, T_ACCENT, BAR_H / 2);
    lv_obj_set_style_bg_opa(s_bar_fill, LV_OPA_40, 0);
    lv_obj_remove_flag(s_bar_fill, LV_OBJ_FLAG_CLICKABLE);
    s_bar_mark = kit_ui_rect(tile, MARK_W, MARK_H, T_ACCENT, MARK_W / 2);
    lv_obj_remove_flag(s_bar_mark, LV_OBJ_FLAG_CLICKABLE);

    kit_ui_action_button(&s_action, tile, T_ACCENT, spin_cb);
    kit_ui_action_set(&s_action, "GIRAR");

    build_overlay(tile);
}

static void on_page(int page, void *user)
{
    (void)user;
    (void)page;
    if (s_reset_armed) { s_reset_armed = false; paint_reset_btn(); }
}

#endif /* !KIT_SDK_STUBS */

/* ===================================================================== */

#ifdef KIT_SDK_STUBS

kit_err_t tool_init(kit_tool_ctx_t *ctx) { (void)ctx; return KIT_OK; }
void tool_destroy(void) {}

#else

kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    s_spinning = false;
    s_dirty = false;
    s_reset_armed = false;
    trinca_odds(s_odds);
    trinca_luck_reset(&s_luck);
    load_all();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "TRINCA", T_ACCENT, 3);
    kit_ui_shell_tiles(&s_shell, on_page, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_jogo(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);

    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);

    paint_stats();
    paint_meter();
    paint_life();

    if (s_api->imu) s_api->imu->register_shake_callback(on_shake, NULL);
    s_save_timer = lv_timer_create(save_timer_cb, SAVE_MS, NULL);

    lv_screen_load(s_screen);
    return KIT_OK;
}

void tool_destroy(void)
{
    anim_stop();
    blink_stop();
    if (s_save_timer) { lv_timer_delete(s_save_timer); s_save_timer = NULL; }
    if (s_api && s_dirty) save_life();
    if (s_api && s_api->imu) s_api->imu->register_shake_callback(NULL, NULL);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell = (kit_ui_shell_t){0};
    s_shake_chips = (kit_ui_chips_t){0};
    s_speed_chips = (kit_ui_chips_t){0};
    s_action = (kit_ui_action_t){0};
    memset(s_reel, 0, sizeof s_reel);
    memset(s_life_you, 0, sizeof s_life_you);
    memset(s_life_exp, 0, sizeof s_life_exp);
    memset(s_frame, 0, sizeof s_frame);
    s_stats_lbl = s_caption_lbl = s_bar_fill = s_bar_mark = NULL;
    s_life_note = s_reset_btn = s_reset_lbl = NULL;
    s_overlay = s_ov_line = NULL;
    s_spinning = false;
    s_api = NULL;
}

#endif
