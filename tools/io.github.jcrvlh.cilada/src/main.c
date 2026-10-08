/**
 * @file main.c
 * @brief CILADA — mini-jogo de mesa: segure um furo e torça pra não ser a cilada.
 *
 * Releitura do "pula pirata" pro KIT. Uma grade de furos (3x3 ou 4x4) com uma
 * cilada escondida. Na sua vez, você segura o dedo num furo até o anel encher:
 * seguro, passa o KIT; cilada, sai da roda e escolhe quem começa a próxima
 * rodada (grade nova). Sobrou um, ganhou. Com cartas ligadas, alguns furos
 * seguros escondem ESPIA / APONTA / +1 / ESCUDO. De vez em quando um furo
 * treme — com peso 2 na cilada: pista de verdade, nunca certeza.
 *
 * Decisões que não são óbvias:
 *  - Segurar, não tocar: o anel enche em HOLD_MS. Evita abrir furo sem querer
 *    ao passar o KIT de mão em mão, e é o "enfiar a espada no barril". Soltar
 *    ou arrastar (o tileview rouba o toque -> PRESS_LOST) cancela.
 *  - O KIT não sabe quem está jogando: só conta aberturas que faltam na vez
 *    (o +1 vale 2), escudos guardados na mesa e jogadores na roda. Quem tem o
 *    escudo, quem aponta e quem saiu a mesa resolve.
 *  - A lógica é aplicada e salva ANTES da animação; o timer só mostra. Fechar
 *    a Tool no meio de um salto não perde nem duplica nada.
 *  - Sem lv_anim na tabela de símbolos: animações são lv_timer + translate /
 *    border_width.
 *  - Lógica pura em cilada_game.c (inteiro puro, sem float — o .so não
 *    resolve __divsf3).
 */
#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "cilada_game.h"

#include <stdio.h>
#include <string.h>

#ifndef KIT_SDK_STUBS

/* ----------------------------------------------------------------------- */

#define C_ACCENT      KIT_COLOR_RED
#define C_ON_ACCENT   KIT_COLOR_ON_COLOR

#define GRID_SIDE     310                                    /* 3x94+2x14 = 4x70+3x10 */
#define GRID_X        ((KIT_UI_SCREEN_W - GRID_SIDE) / 2)    /* 29 */
#define GRID_Y        (KIT_UI_PAGE_H - GRID_SIDE - 10)       /* 40 — faixa de status em cima */
#define RING          4                                      /* borda do furo fechado */
/* Folga da caixa da grade em volta dos furos: o salto da cilada (translate_y
 * até -30) e o tremor (+-4) não podem sair da caixa — o LVGL só invalida a
 * área dentro do pai, e o pedaço de fora ficaria sem redesenhar. */
#define M_SIDE        8
#define M_TOP         36
#define M_BOT         4

#define HOLD_MS        600   /* segurar até o anel encher */
#define HOLD_TICK_MS   30
#define STATUS_MS      2000  /* "PASSE O KIT" antes da instrução da vez seguinte */
#define SPY_MS         900   /* quanto tempo a espiada fica na tela */
#define CARD_DELAY_MS  380   /* furo vira carta, depois o overlay */
#define TRAP_DELAY_MS  950   /* salto da cilada, depois o overlay */
#define JUMP_TICK_MS   40
#define TREMOR_TICK_MS 40

#define PLAYERS_MIN     2
#define PLAYERS_MAX     12
#define PLAYERS_DEFAULT 4

#define K_NP    "cil_np"
#define K_GRID  "cil_grid"
#define K_CARDS "cil_cards"
#define K_ST    "cil_st"
#define K_CARD  "cil_card"
#define K_R0    "cil_r0"
#define K_R1    "cil_r1"
#define K_M     "cil_m"

typedef enum {
    ST_IDLE = 0,    /* sem partida: número de jogadores + COMEÇAR */
    ST_PLAY,        /* grade na tela; segurar abre (ou espia) */
    ST_CARD,        /* overlay da carta achada */
    ST_TRAP,        /* caiu na cilada com escudo na mesa: "quem caiu tem o escudo?" */
    ST_ROUND_END,   /* alguém saiu; PRÓXIMA RODADA */
    ST_OVER,        /* sobrou um */
    ST_COUNT
} cil_state_t;

static const char *const GRID_LABELS[]  = { "3X3", "4X4" };
static const char *const CARDS_LABELS[] = { "COM CARTAS", "SEM CARTAS" };

static const char *const CARD_NAME[CILADA_CARD_COUNT] = { "ESPIA", "APONTA", "+1", "ESCUDO" };
static const char *const CARD_BTN[CILADA_CARD_COUNT]  = { "ESPIAR", "OK", "OK", "GUARDAR" };
static const char *const CARD_TEXT[CILADA_CARD_COUNT] = {
    "Vire o KIT pra voc\xC3\xAA e segure um furo: s\xC3\xB3 voc\xC3\xAA v\xC3\xAA "
    "se \xC3\xA9 seguro. Pode mentir.",
    "Voc\xC3\xAA escolhe quem joga depois de voc\xC3\xAA.",
    "Quem jogar depois de voc\xC3\xAA abre 2 furos.",
    "Se voc\xC3\xAA cair na cilada nesta rodada, fica no jogo e ela se esconde "
    "em outro furo.",
};

static const char RULES[] =
    "Todo mundo em roda, passando o KIT de m\xC3\xA3o em m\xC3\xA3o.\n\n"
    "1. No AJUSTE, escolha quantos jogadores, o tamanho da grade e se joga com "
    "cartas. Toque em COME\xC3\x87" "AR.\n\n"
    "2. Na sua vez, segure o dedo num furo at\xC3\xA9 o anel encher. Furo "
    "seguro: passe o KIT pro pr\xC3\xB3ximo.\n\n"
    "3. Um dos furos \xC3\xA9 a CILADA. Quem abrir sai da roda e escolhe quem "
    "come\xC3\xA7" "a a pr\xC3\xB3xima rodada, com uma grade nova.\n\n"
    "4. \xC3\x80s vezes um furo treme. O KIT gosta de provocar: ele treme mais "
    "na cilada do que nos outros, mas nunca \xC3\xA9 certeza.\n\n"
    "5. Com cartas, alguns furos seguros escondem uma:\n"
    "ESPIA - voc\xC3\xAA segura um furo e s\xC3\xB3 voc\xC3\xAA v\xC3\xAA se "
    "\xC3\xA9 seguro. Pode mentir.\n"
    "APONTA - voc\xC3\xAA escolhe quem joga depois de voc\xC3\xAA.\n"
    "+1 - quem jogar depois de voc\xC3\xAA abre 2 furos.\n"
    "ESCUDO - guarde. Se cair na cilada nesta rodada, voc\xC3\xAA fica e a "
    "cilada se esconde em outro furo.\n\n"
    "6. Sobrou um: esse ganhou.";

/* ------------------------------------------------------------------ estado */

static const kit_api_table_t *s_api;
static lv_obj_t *s_screen;
static kit_ui_shell_t  s_shell;
static kit_ui_chips_t  s_grid_chips;
static kit_ui_chips_t  s_cards_chips;
static kit_ui_action_t s_action;

static cilada_round_t s_r;
static cil_state_t    s_state;
static int            s_card_shown;     /* carta do overlay ST_CARD */
static int            s_players_cfg = PLAYERS_DEFAULT;
static int            s_grid_idx;       /* 0 = 3x3, 1 = 4x4 */
static bool           s_cards_on = true;
static int            s_players_init;
static int            s_players_left;
static bool           s_owed;           /* abriu um, falta outro (+1) */
static bool           s_busy;           /* animação em curso: ignora segurar */
static bool           s_end_armed;      /* ENCERRAR PARTIDA pede 2º toque */

/* JOGO */
static lv_obj_t *s_idle_box, *s_idle_count, *s_idle_info;
static lv_obj_t *s_play_box, *s_flash_box, *s_status_lbl, *s_chance_lbl, *s_grid;
static lv_obj_t *s_hole[CILADA_MAX_CELLS];
static lv_obj_t *s_hole_lbl[CILADA_MAX_CELLS];

/* AJUSTE */
static lv_obj_t *s_np_lbl, *s_np_minus, *s_np_plus, *s_lock_note, *s_end_btn, *s_end_lbl;

/* overlay */
static lv_obj_t *s_ov, *s_ov_shape, *s_ov_kicker, *s_ov_title, *s_ov_body, *s_ov_note;
static lv_obj_t *s_ov_b1, *s_ov_b1_lbl, *s_ov_b2, *s_ov_b2_lbl;

/* timers */
static lv_timer_t *s_hold_timer, *s_tremor_timer, *s_jump_timer, *s_delay_timer, *s_status_timer;
static void (*s_delay_fn)(void);
static int s_hold_idx = -1, s_hold_ms;
static int s_tremor_frame;
static int s_jump_idx = -1, s_jump_frame;

/* -------------------------------------------------------------------- util */

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

static void show(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static int cols(void)      { return s_r.n == 16 ? 4 : 3; }
static int cell_size(void) { return cols() == 4 ? 70 : 94; }
static int cell_gap(void)  { return cols() == 4 ? 10 : 14; }

static bool in_match(void) { return s_state != ST_IDLE && s_state != ST_OVER; }

static void keep_awake(bool on) { kit_ui_keep_awake(on); }

/* ------------------------------------------------------------ persistência */

static void save_settings(void)
{
    set_i32(K_NP, s_players_cfg);
    set_i32(K_GRID, s_grid_idx);
    set_i32(K_CARDS, s_cards_on ? 1 : 0);
}

static void save_match(void)
{
    int32_t w[2];
    cilada_pack(&s_r, w);
    set_i32(K_ST, s_state);
    set_i32(K_CARD, s_card_shown);
    set_i32(K_R0, w[0]);
    set_i32(K_R1, w[1]);
    set_i32(K_M, s_players_init | (s_players_left << 8));
}

static void load_all(void)
{
    int32_t v = get_i32(K_NP, PLAYERS_DEFAULT);
    s_players_cfg = (v >= PLAYERS_MIN && v <= PLAYERS_MAX) ? (int)v : PLAYERS_DEFAULT;
    v = get_i32(K_GRID, 0);
    s_grid_idx = (v == 1) ? 1 : 0;
    s_cards_on = get_i32(K_CARDS, 1) != 0;

    s_state = ST_IDLE;
    s_card_shown = -1;
    memset(&s_r, 0, sizeof s_r);
    s_r.n = 9;
    s_r.trap = 0;
    s_r.tremor = -1;
    s_r.defused = -1;
    for (int c = 0; c < CILADA_CARD_COUNT; c++) s_r.card_at[c] = -1;

    int32_t st = get_i32(K_ST, ST_IDLE);
    if (st <= ST_IDLE || st >= ST_COUNT) return;

    int32_t w[2] = { get_i32(K_R0, 0), get_i32(K_R1, 0) };
    int32_t m = get_i32(K_M, 0);
    int init = m & 0xFF, left = (m >> 8) & 0xFF;
    cilada_round_t r;
    if (!cilada_unpack(&r, w) || init < PLAYERS_MIN || init > PLAYERS_MAX ||
        left < 1 || left > init) return;

    s_r = r;
    s_players_init = init;
    s_players_left = left;
    s_state = (cil_state_t)st;
    s_card_shown = (int)get_i32(K_CARD, -1);
    if (s_state == ST_CARD && (s_card_shown < 0 || s_card_shown >= CILADA_CARD_COUNT))
        s_state = ST_PLAY;
    /* fechou no meio da espiada: a vez já tinha acabado na lógica */
    if (s_state == ST_PLAY && cilada_turn_over(&s_r)) cilada_next_turn(&s_r, rng);
    if (s_state == ST_PLAY) cilada_roll_tremor(&s_r, rng);
}

/* ------------------------------------------------------------------- som */

static void sfx_beat(void)  { kit_ui_beep(220, 35); }                  /* "tum" do coração */
static void sfx_safe(void)  { kit_ui_beep(1047, 30); kit_ui_beep(1568, 45); }   /* alívio */
static void sfx_trap(void)
{
    /* mola subindo: o pirata saltando do barril */
    kit_ui_beep(300, 25);
    kit_ui_beep(500, 25);
    kit_ui_beep(800, 25);
    kit_ui_beep(1300, 25);
    kit_ui_beep(1900, 90);
}

/* ------------------------------------------------------------- timers util */

static void kill_timer(lv_timer_t **t)
{
    if (*t) { lv_timer_delete(*t); *t = NULL; }
}

static void delay_cb(lv_timer_t *t)
{
    (void)t;
    kill_timer(&s_delay_timer);
    void (*fn)(void) = s_delay_fn;
    s_delay_fn = NULL;
    if (fn) fn();
}

static void run_later(uint32_t ms, void (*fn)(void))
{
    kill_timer(&s_delay_timer);
    s_delay_fn = fn;
    s_delay_timer = lv_timer_create(delay_cb, ms, NULL);
}

/* ---------------------------------------------------------------- pintura */

static void paint_hole(int i)
{
    lv_obj_t *h = s_hole[i];
    lv_obj_t *l = s_hole_lbl[i];
    lv_obj_set_style_translate_x(h, 0, 0);
    lv_obj_set_style_translate_y(h, 0, 0);
    show(l, false);

    if (!cilada_is_open(&s_r, i)) {
        /* fechado: anel paper sobre preto */
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(h, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_obj_set_style_border_width(h, RING, 0);
        return;
    }
    lv_obj_set_style_border_width(h, 0, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);

    if (i == s_r.trap || i == s_r.defused) {
        /* cilada revelada: bola vermelha. Desarmada fica apagada. */
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(h, lv_color_hex(C_ACCENT), 0);
        if (i != s_r.trap) lv_obj_set_style_bg_opa(h, LV_OPA_40, 0);
        return;
    }
    /* aberto: disco apagado; carta achada mostra o nome */
    lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(h, lv_color_hex(KIT_COLOR_SURFACE_ALT), 0);
    int c = cilada_card_at(&s_r, i);
    if (c >= 0) {
        lv_label_set_text(l, CARD_NAME[c]);
        show(l, true);
    }
}

static void layout_grid(void)
{
    int n = cols(), cs = cell_size(), gap = cell_gap();
    for (int i = 0; i < CILADA_MAX_CELLS; i++) {
        bool used = i < s_r.n;
        show(s_hole[i], used);
        if (!used) continue;
        lv_obj_set_size(s_hole[i], cs, cs);
        lv_obj_set_pos(s_hole[i], M_SIDE + (i % n) * (cs + gap), M_TOP + (i / n) * (cs + gap));
        lv_obj_set_ext_click_area(s_hole[i], gap / 2);
    }
}

static void paint_grid(void)
{
    for (int i = 0; i < s_r.n; i++) paint_hole(i);
}

static void paint_chance(void)
{
    int closed = cilada_closed(&s_r);
    if (closed < 1) closed = 1;
    lv_label_set_text_fmt(s_chance_lbl, "1 EM %d", closed);
    lv_obj_set_style_text_color(s_chance_lbl,
        lv_color_hex(closed <= 3 ? C_ACCENT : KIT_COLOR_TEXT), 0);
}

static void set_status(const char *txt, uint32_t color)
{
    lv_label_set_text(s_status_lbl, txt);
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(color), 0);
}

static void paint_status(void)
{
    kill_timer(&s_status_timer);
    paint_chance();
    if (s_r.spy)                 set_status("ESPIE UM FURO", C_ACCENT);
    else if (s_r.opens_left >= 2) set_status("ABRA 2 FUROS", KIT_COLOR_TEXT);
    else if (s_owed)             set_status("ABRA MAIS 1", KIT_COLOR_TEXT);
    else                         set_status("SEGURE UM FURO", KIT_COLOR_TEXT);
}

static void status_timer_cb(lv_timer_t *t) { (void)t; paint_status(); }

/* mostra `txt` por STATUS_MS e volta pra instrução da vez */
static void status_flash(const char *txt)
{
    paint_chance();
    set_status(txt, KIT_COLOR_TEXT);
    kill_timer(&s_status_timer);
    s_status_timer = lv_timer_create(status_timer_cb, STATUS_MS, NULL);
}

static void paint_idle(void)
{
    lv_label_set_text_fmt(s_idle_count, "%d", s_players_cfg);
    lv_label_set_text_fmt(s_idle_info, "%s \xC2\xB7 %s",
                          GRID_LABELS[s_grid_idx], CARDS_LABELS[s_cards_on ? 0 : 1]);
}

static void dim(lv_obj_t *o, bool off)
{
    lv_obj_set_style_opa(o, off ? LV_OPA_30 : LV_OPA_COVER, 0);
}

static void lock_chips(kit_ui_chips_t *c, bool locked)
{
    for (int i = 0; i < c->count; i++) {
        if (locked) lv_obj_remove_flag(c->chip[i], LV_OBJ_FLAG_CLICKABLE);
        else        lv_obj_add_flag(c->chip[i], LV_OBJ_FLAG_CLICKABLE);
        dim(c->chip[i], locked);
    }
}

static void paint_end_btn(void)
{
    lv_obj_set_style_bg_color(s_end_btn,
        lv_color_hex(s_end_armed ? C_ACCENT : KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(s_end_lbl,
        lv_color_hex(s_end_armed ? C_ON_ACCENT : KIT_COLOR_TEXT), 0);
    lv_label_set_text(s_end_lbl, s_end_armed ? "TOQUE DE NOVO" : "ENCERRAR PARTIDA");
}

static void paint_ajuste(void)
{
    bool locked = in_match();
    lv_label_set_text_fmt(s_np_lbl, "%d", s_players_cfg);
    dim(s_np_minus, locked || s_players_cfg <= PLAYERS_MIN);
    dim(s_np_plus,  locked || s_players_cfg >= PLAYERS_MAX);
    lock_chips(&s_grid_chips, locked);
    lock_chips(&s_cards_chips, locked);
    show(s_lock_note, locked);
    show(s_end_btn, locked);
    if (!locked) s_end_armed = false;
    paint_end_btn();
}

/* ---------------------------------------------------------------- overlay */

static void ov_button_style(lv_obj_t *b, lv_obj_t *lbl, uint32_t bg, bool filled, uint32_t fg)
{
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, filled ? 0 : 3, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(fg), 0);
}

static void paint_overlay(void)
{
    if (s_state == ST_IDLE || s_state == ST_PLAY) { show(s_ov, false); return; }

    bool red = (s_state == ST_TRAP || s_state == ST_ROUND_END);
    uint32_t bg = red ? C_ACCENT : KIT_COLOR_BG;
    uint32_t fg = red ? C_ON_ACCENT : KIT_COLOR_TEXT;
    lv_obj_set_style_bg_color(s_ov, lv_color_hex(bg), 0);
    lv_obj_set_style_text_color(s_ov_title, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(s_ov_body, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(s_ov_note, lv_color_hex(fg), 0);
    lv_obj_set_style_text_font(s_ov_title, &kit_display_72, 0);
    show(s_ov_kicker, false);
    show(s_ov_note, false);
    show(s_ov_b2, false);
    show(s_ov_shape, true);

    switch (s_state) {
    case ST_CARD: {
        int c = s_card_shown;
        show(s_ov_shape, false);
        show(s_ov_kicker, true);
        lv_label_set_text(s_ov_kicker, "VOC\xC3\x8A ACHOU UMA CARTA");
        /* display_72 não tem '+': o "+1" vai no 120, que tem 0-9 - + */
        if (c == CILADA_MAIS1) lv_obj_set_style_text_font(s_ov_title, &kit_display_120, 0);
        lv_label_set_text(s_ov_title, CARD_NAME[c]);
        lv_label_set_text(s_ov_body, CARD_TEXT[c]);
        lv_label_set_text(s_ov_b1_lbl, CARD_BTN[c]);
        ov_button_style(s_ov_b1, s_ov_b1_lbl, C_ACCENT, true, C_ON_ACCENT);
        break;
    }
    case ST_TRAP:
        lv_obj_set_style_radius(s_ov_shape, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_ov_shape, lv_color_hex(KIT_COLOR_BG), 0);
        lv_label_set_text(s_ov_title, "CILADA");
        lv_label_set_text(s_ov_body, "Quem caiu tem o ESCUDO?");
        lv_label_set_text(s_ov_b1_lbl, "TENHO ESCUDO");
        ov_button_style(s_ov_b1, s_ov_b1_lbl, KIT_COLOR_BG, true, KIT_COLOR_TEXT);
        show(s_ov_b2, true);
        lv_label_set_text(s_ov_b2_lbl, "N\xC3\x83O, SA\xC3\x8D");
        ov_button_style(s_ov_b2, s_ov_b2_lbl, C_ACCENT, false, C_ON_ACCENT);
        break;
    case ST_ROUND_END:
        lv_obj_set_style_radius(s_ov_shape, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_ov_shape, lv_color_hex(KIT_COLOR_BG), 0);
        lv_label_set_text(s_ov_title, "CILADA");
        lv_label_set_text(s_ov_body,
            "Quem caiu sai da roda e escolhe quem come\xC3\xA7" "a a pr\xC3\xB3xima.");
        show(s_ov_note, true);
        lv_label_set_text_fmt(s_ov_note, "%d NA RODA", s_players_left);
        lv_label_set_text(s_ov_b1_lbl, "PR\xC3\x93XIMA RODADA");
        ov_button_style(s_ov_b1, s_ov_b1_lbl, KIT_COLOR_BG, true, KIT_COLOR_TEXT);
        break;
    case ST_OVER:
        lv_obj_set_style_radius(s_ov_shape, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_ov_shape, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_label_set_text(s_ov_title, "VENCEU");
        lv_label_set_text(s_ov_body, "Quem sobrou na roda ganhou a partida.");
        lv_label_set_text(s_ov_b1_lbl, "JOGAR DE NOVO");
        ov_button_style(s_ov_b1, s_ov_b1_lbl, C_ACCENT, true, C_ON_ACCENT);
        break;
    default:
        break;
    }
    show(s_ov, true);
}

/* -------------------------------------------------------------- tremor */

static void tremor_reset(void)
{
    s_tremor_frame = 0;
    for (int i = 0; i < CILADA_MAX_CELLS; i++)
        if (s_hole[i] && i != s_jump_idx) lv_obj_set_style_translate_x(s_hole[i], 0, 0);
}

static void tremor_cb(lv_timer_t *t)
{
    static const int8_t JIT[] = { 4, -4, 3, -3, 2, -2, 0 };
    int i = s_r.tremor;
    bool can = s_state == ST_PLAY && !s_busy && i >= 0 && i < s_r.n &&
               i != s_hold_idx && !cilada_is_open(&s_r, i);
    if (!can) {
        if (s_tremor_frame) tremor_reset();
        lv_timer_set_period(t, 500);
        return;
    }
    if (s_tremor_frame < (int)sizeof JIT) {
        lv_obj_set_style_translate_x(s_hole[i], JIT[s_tremor_frame], 0);
        s_tremor_frame++;
        lv_timer_set_period(t, TREMOR_TICK_MS);
    } else {
        s_tremor_frame = 0;
        lv_obj_set_style_translate_x(s_hole[i], 0, 0);
        lv_timer_set_period(t, (uint32_t)rng(1600, 2600));
    }
}

/* ------------------------------------------------------------ fluxo do jogo */

static void show_mode(void)
{
    bool idle = (s_state == ST_IDLE);
    show(s_idle_box, idle);
    kit_ui_action_show(&s_action, idle);
    show(s_play_box, !idle);
    if (idle) paint_idle();
}

static void new_round(void)
{
    cilada_new_round(&s_r, s_grid_idx ? 16 : 9, s_cards_on, rng);
    s_owed = false;
    s_busy = false;
    s_state = ST_PLAY;
    tremor_reset();
    layout_grid();
    paint_grid();
    paint_status();
    save_match();
}

static void start_match(void)
{
    s_players_init = s_players_left = s_players_cfg;
    new_round();
    keep_awake(true);
    show_mode();
    paint_overlay();
    paint_ajuste();
    kit_ui_confirm();
}

/* Fim de uma abertura segura (ou da espiada): fecha a vez ou pede mais uma. */
static void finish_open(void)
{
    if (cilada_turn_over(&s_r)) {
        bool point = s_r.point;
        cilada_next_turn(&s_r, rng);
        s_owed = false;
        tremor_reset();
        save_match();
        status_flash(point ? "APONTE ALGU\xC3\x89M" : "PASSE O KIT");
    } else {
        s_owed = s_r.opens_left > 0;
        save_match();
        paint_status();
    }
}

static void eliminate(void)
{
    if (s_players_left > 0) s_players_left--;
    s_state = (s_players_left <= 1) ? ST_OVER : ST_ROUND_END;
    if (s_state == ST_OVER) keep_awake(false);
    save_match();
}

/* overlay do estado atual, depois da animação */
static void reveal_state(void)
{
    s_busy = false;
    s_jump_idx = -1;
    paint_overlay();
    if (s_state == ST_OVER) kit_ui_sfx(KIT_SFX_ONBOARD_DONE);
    paint_ajuste();
}

static void jump_cb(lv_timer_t *t)
{
    (void)t;
    static const int8_t JUMP[] = { -12, -22, -28, -30, -26, -18, -8, 0, -6, -3, 0 };
    if (s_jump_idx < 0 || s_jump_frame >= (int)sizeof JUMP) {
        kill_timer(&s_jump_timer);
        return;
    }
    lv_obj_set_style_translate_y(s_hole[s_jump_idx], JUMP[s_jump_frame], 0);
    if (s_jump_frame == 3) lv_obj_set_style_bg_opa(s_flash_box, LV_OPA_TRANSP, 0);
    s_jump_frame++;
}

static void trap_hit(int i)
{
    s_busy = true;
    paint_hole(i);
    sfx_trap();

    /* clarão na página + a bola vermelha salta do furo */
    lv_obj_set_style_bg_color(s_flash_box, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_flash_box, LV_OPA_40, 0);
    s_jump_idx = i;
    s_jump_frame = 0;
    kill_timer(&s_jump_timer);
    s_jump_timer = lv_timer_create(jump_cb, JUMP_TICK_MS, NULL);

    /* lógica já resolvida e salva; o overlay só vem depois do salto */
    if (s_r.shields > 0) { s_state = ST_TRAP; save_match(); }
    else                 eliminate();
    run_later(TRAP_DELAY_MS, reveal_state);
}

static int s_spy_idx = -1;

static void spy_done(void)
{
    s_busy = false;
    if (s_spy_idx >= 0) paint_hole(s_spy_idx);
    s_spy_idx = -1;
    finish_open();
}

static void spy_reveal(int i)
{
    bool trap = cilada_spy(&s_r, i);
    save_match();
    s_busy = true;
    s_spy_idx = i;
    lv_obj_t *h = s_hole[i];
    lv_obj_set_style_border_width(h, 0, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    if (trap) {
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(h, lv_color_hex(C_ACCENT), 0);
        set_status("CILADA", C_ACCENT);
    } else {
        lv_obj_set_style_bg_color(h, lv_color_hex(KIT_COLOR_TEXT), 0);
        set_status("SEGURO", KIT_COLOR_TEXT);
    }
    kit_ui_click();   /* mesmo som nos dois casos: o som não entrega a resposta */
    run_later(SPY_MS, spy_done);
}

static void card_reveal(void) { reveal_state(); }

static void hold_complete(int i)
{
    kill_timer(&s_status_timer);
    if (s_r.spy) { spy_reveal(i); return; }

    int card = -1;
    switch (cilada_open(&s_r, i, &card)) {
    case CILADA_SAFE:
        paint_hole(i);
        sfx_safe();
        finish_open();
        break;
    case CILADA_CARD:
        paint_hole(i);
        kit_ui_sfx(KIT_SFX_REVEAL);
        s_state = ST_CARD;
        s_card_shown = card;
        s_busy = true;
        save_match();
        paint_chance();
        run_later(CARD_DELAY_MS, card_reveal);
        break;
    case CILADA_TRAP:
        trap_hit(i);
        break;
    default:
        paint_hole(i);
        break;
    }
}

/* --- segurar ------------------------------------------------------------ */

static int hold_beats(void)
{
    int closed = cilada_closed(&s_r);
    if (closed <= 3) return 4;
    if (closed * 2 <= s_r.n) return 3;
    return 2;
}

static void hold_stop(void)
{
    kill_timer(&s_hold_timer);
    s_hold_idx = -1;
}

static void hold_tick_cb(lv_timer_t *t)
{
    (void)t;
    int i = s_hold_idx;
    if (i < 0) { hold_stop(); return; }
    int prev = s_hold_ms;
    s_hold_ms += HOLD_TICK_MS;

    /* batimento acelera quando sobram poucos furos (a chance real não muda) */
    int beats = hold_beats();
    for (int k = 1; k < beats; k++) {
        int at = (k * HOLD_MS) / beats;
        if (prev < at && s_hold_ms >= at) sfx_beat();
    }

    if (s_hold_ms >= HOLD_MS) {
        hold_stop();
        hold_complete(i);
        return;
    }
    int half = cell_size() / 2;
    lv_obj_set_style_border_width(s_hole[i], RING + ((half - RING) * s_hold_ms) / HOLD_MS, 0);
}

static void hole_press_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_state != ST_PLAY || s_busy || s_hold_idx >= 0) return;
    if (i >= s_r.n || cilada_is_open(&s_r, i)) return;
    if (i == s_r.tremor) lv_obj_set_style_translate_x(s_hole[i], 0, 0);
    s_hold_idx = i;
    s_hold_ms = 0;
    sfx_beat();
    s_hold_timer = lv_timer_create(hold_tick_cb, HOLD_TICK_MS, NULL);
}

static void hole_release_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i != s_hold_idx) return;
    hold_stop();                 /* soltou antes de encher: nada acontece */
    paint_hole(i);
}

/* --- botões ------------------------------------------------------------- */

static void action_cb(lv_event_t *e)
{
    (void)e;
    if (s_state == ST_IDLE) start_match();
}

static void ov_b1_cb(lv_event_t *e)
{
    (void)e;
    if (s_busy) return;
    switch (s_state) {
    case ST_CARD:
        s_state = ST_PLAY;
        paint_overlay();
        kit_ui_click();
        if (s_card_shown == CILADA_ESPIA) { save_match(); paint_status(); }
        else                              finish_open();
        break;
    case ST_TRAP:
        kit_ui_confirm();
        if (cilada_use_shield(&s_r, rng)) {
            s_state = ST_PLAY;
            paint_overlay();
            paint_grid();
            finish_open();
        } else {
            new_round();   /* não sobrou furo: grade nova, ninguém sai */
            paint_overlay();
            status_flash("GRADE NOVA");
        }
        break;
    case ST_ROUND_END:
        kit_ui_confirm();
        new_round();
        paint_overlay();
        break;
    case ST_OVER:
        kit_ui_click();
        s_state = ST_IDLE;
        save_match();
        paint_overlay();
        show_mode();
        paint_ajuste();
        break;
    default:
        break;
    }
}

static void ov_b2_cb(lv_event_t *e)
{
    (void)e;
    if (s_busy || s_state != ST_TRAP) return;
    kit_ui_click();
    eliminate();
    reveal_state();
}

static void np_step_cb(lv_event_t *e)
{
    if (in_match()) return;
    int v = s_players_cfg + (int)(intptr_t)lv_event_get_user_data(e);
    if (v < PLAYERS_MIN || v > PLAYERS_MAX) return;
    s_players_cfg = v;
    kit_ui_click();
    save_settings();
    paint_ajuste();
    paint_idle();
}

static void grid_cb(int idx, void *user)
{
    (void)user;
    s_grid_idx = idx;
    save_settings();
    paint_idle();
}

static void cards_cb(int idx, void *user)
{
    (void)user;
    s_cards_on = (idx == 0);
    save_settings();
    paint_idle();
}

static void end_cb(lv_event_t *e)
{
    (void)e;
    if (!in_match()) return;
    if (!s_end_armed) {
        s_end_armed = true;
        kit_ui_click();
        paint_end_btn();
        return;
    }
    s_end_armed = false;
    hold_stop();
    kill_timer(&s_delay_timer);
    kill_timer(&s_jump_timer);
    kill_timer(&s_status_timer);
    s_delay_fn = NULL;
    s_jump_idx = s_spy_idx = -1;
    s_busy = false;
    s_state = ST_IDLE;
    keep_awake(false);
    save_match();
    kit_ui_sfx(KIT_SFX_BACK);
    paint_overlay();
    show_mode();
    paint_ajuste();
    kit_ui_shell_open(&s_shell, 1);
}

static void on_page(int page, void *user)
{
    (void)user;
    if (page != 0 && s_end_armed) { s_end_armed = false; paint_end_btn(); }
}

/* ---------------------------------------------------------------- AJUSTE */

static void section_label(lv_obj_t *p, const char *txt)
{
    kit_ui_label(p, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

static lv_obj_t *step_btn(lv_obj_t *parent, const char *sym, int delta)
{
    lv_obj_t *b = kit_ui_rect(parent, 80, 80, KIT_COLOR_SURFACE, 16);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_add_event_cb(b, np_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)delta);
    lv_obj_center(kit_ui_label(b, sym, KIT_COLOR_TEXT, &kit_display_44, 0));
    return b;
}

static void build_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_right(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_top(p, 8, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);   /* remove_style_all + plain: readiciona antes do dir */
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    /* partida em andamento: aviso e ENCERRAR em cima (escondidos fora dela) */
    s_lock_note = kit_ui_label(p, "PARTIDA EM ANDAMENTO", KIT_COLOR_TEXT, &kit_mono_16, 2);
    s_end_btn = kit_ui_rect(p, KIT_UI_CONTENT, 84, KIT_COLOR_SURFACE, 42);
    lv_obj_add_flag(s_end_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_end_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(s_end_btn, 8);
    lv_obj_add_event_cb(s_end_btn, end_cb, LV_EVENT_CLICKED, NULL);
    s_end_lbl = kit_ui_label(s_end_btn, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_center(s_end_lbl);

    section_label(p, "JOGADORES");
    lv_obj_t *row = kit_ui_box(p);
    lv_obj_set_size(row, lv_pct(100), 80);
    kit_ui_flex(row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 16);
    s_np_minus = step_btn(row, "-", -1);
    s_np_lbl = kit_ui_label(row, "", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_set_width(s_np_lbl, 76);
    lv_obj_set_style_text_align(s_np_lbl, LV_TEXT_ALIGN_CENTER, 0);
    s_np_plus = step_btn(row, "+", 1);

    section_label(p, "GRADE");
    kit_ui_chips(&s_grid_chips, p, GRID_LABELS, 2, s_grid_idx, C_ACCENT, grid_cb, NULL);

    section_label(p, "CARTAS");
    kit_ui_chips(&s_cards_chips, p, CARDS_LABELS, 2, s_cards_on ? 0 : 1, C_ACCENT, cards_cb, NULL);
}

/* ------------------------------------------------------------------ JOGO */

static void build_idle(lv_obj_t *tile)
{
    s_idle_box = kit_ui_box(tile);
    lv_obj_set_size(s_idle_box, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(s_idle_box, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 10, 0);
    /* centrado no espaço acima do botão (só o botão é irmão nesse estado) */
    lv_obj_align(s_idle_box, LV_ALIGN_CENTER, 0, -(KIT_UI_BTN_H + KIT_UI_BTN_MARGIN) / 2);

    kit_ui_label(s_idle_box, "JOGADORES", KIT_COLOR_TEXT, &kit_mono_20, 2);
    s_idle_count = kit_ui_label(s_idle_box, "", KIT_COLOR_TEXT, &kit_display_120, 0);
    s_idle_info = kit_ui_label(s_idle_box, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);

    kit_ui_action_button(&s_action, tile, C_ACCENT, action_cb);
    kit_ui_action_set(&s_action, "COME\xC3\x87" "AR");
}

static void build_play(lv_obj_t *tile)
{
    s_play_box = kit_ui_box(tile);
    lv_obj_set_size(s_play_box, KIT_UI_SCREEN_W, KIT_UI_PAGE_H);
    lv_obj_set_pos(s_play_box, 0, 0);

    /* clarão da cilada: fundo da página, normalmente transparente */
    s_flash_box = kit_ui_box(s_play_box);
    lv_obj_set_size(s_flash_box, KIT_UI_SCREEN_W, KIT_UI_PAGE_H);
    lv_obj_set_style_bg_opa(s_flash_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_flash_box, 24, 0);

    s_status_lbl = kit_ui_label(s_play_box, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_LEFT, GRID_X, 6);
    s_chance_lbl = kit_ui_label(s_play_box, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_align(s_chance_lbl, LV_ALIGN_TOP_RIGHT, -GRID_X, 6);

    /* caixa com folga (M_*): o salto e o tremor acontecem dentro dela */
    s_grid = kit_ui_box(s_play_box);
    lv_obj_set_size(s_grid, GRID_SIDE + 2 * M_SIDE, GRID_SIDE + M_TOP + M_BOT);
    lv_obj_set_pos(s_grid, GRID_X - M_SIDE, GRID_Y - M_TOP);

    for (int i = 0; i < CILADA_MAX_CELLS; i++) {
        lv_obj_t *h = kit_ui_box(s_grid);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(h, hole_press_cb,   LV_EVENT_PRESSED,    (void *)(intptr_t)i);
        lv_obj_add_event_cb(h, hole_release_cb, LV_EVENT_RELEASED,   (void *)(intptr_t)i);
        lv_obj_add_event_cb(h, hole_release_cb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
        s_hole[i] = h;
        s_hole_lbl[i] = kit_ui_label(h, "", KIT_COLOR_TEXT, &kit_mono_16, 0);
        lv_obj_center(s_hole_lbl[i]);
    }
}

static void build_jogo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    build_play(tile);
    build_idle(tile);
}

static lv_obj_t *ov_button(lv_obj_t *parent, int h, const lv_font_t *font, int ls,
                           lv_event_cb_t cb, lv_obj_t **lbl)
{
    lv_obj_t *b = kit_ui_rect(parent, KIT_UI_CONTENT, h, KIT_COLOR_BG, h / 2);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    *lbl = kit_ui_label(b, "", KIT_COLOR_TEXT, font, ls);
    lv_obj_center(*lbl);
    return b;
}

static void build_overlay(void)
{
    /* tela cheia (por cima da titlebar também): container só porque é tela
     * cheia; clicável pra engolir o toque na grade */
    s_ov = kit_ui_box(s_screen);
    lv_obj_set_size(s_ov, KIT_UI_SCREEN_W, KIT_UI_SCREEN_H);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);
    kit_ui_flex(s_ov, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 12, 0);

    s_ov_shape = kit_ui_rect(s_ov, 64, 64, KIT_COLOR_BG, LV_RADIUS_CIRCLE);
    s_ov_kicker = kit_ui_label(s_ov, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    s_ov_title = kit_ui_label(s_ov, "", KIT_COLOR_TEXT, &kit_display_72, 0);
    s_ov_body = kit_ui_text(s_ov, "", KIT_COLOR_TEXT, &kit_sans_28, KIT_UI_CONTENT);
    s_ov_note = kit_ui_label(s_ov, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_t *gap = kit_ui_box(s_ov);
    lv_obj_set_size(gap, 1, 4);
    s_ov_b1 = ov_button(s_ov, KIT_UI_BTN_H, &kit_mono_26, 3, ov_b1_cb, &s_ov_b1_lbl);
    s_ov_b2 = ov_button(s_ov, 64, &kit_mono_20, 2, ov_b2_cb, &s_ov_b2_lbl);
    show(s_ov, false);
}

#endif /* !KIT_SDK_STUBS */

/* ===================================================================== */

#ifdef KIT_SDK_STUBS

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Cilada stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#else

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    s_busy = false;
    s_owed = false;
    s_end_armed = false;
    s_hold_idx = s_jump_idx = s_spy_idx = -1;
    s_delay_fn = NULL;
    load_all();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "CILADA", C_ACCENT, 3);
    kit_ui_shell_tiles(&s_shell, on_page, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_jogo(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);
    build_overlay();

    layout_grid();
    paint_grid();
    if (s_state == ST_PLAY) paint_status();
    else                    paint_chance();
    show_mode();
    paint_overlay();
    paint_ajuste();
    paint_idle();
    if (in_match()) keep_awake(true);

    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);   /* abre no JOGO */
    s_tremor_timer = lv_timer_create(tremor_cb, 1000, NULL);

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    kill_timer(&s_hold_timer);
    kill_timer(&s_tremor_timer);
    kill_timer(&s_jump_timer);
    kill_timer(&s_delay_timer);
    kill_timer(&s_status_timer);
    keep_awake(false);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell = (kit_ui_shell_t){0};
    s_grid_chips = (kit_ui_chips_t){0};
    s_cards_chips = (kit_ui_chips_t){0};
    s_action = (kit_ui_action_t){0};
    s_idle_box = s_idle_count = s_idle_info = NULL;
    s_play_box = s_flash_box = s_status_lbl = s_chance_lbl = s_grid = NULL;
    memset(s_hole, 0, sizeof s_hole);
    memset(s_hole_lbl, 0, sizeof s_hole_lbl);
    s_np_lbl = s_np_minus = s_np_plus = s_lock_note = s_end_btn = s_end_lbl = NULL;
    s_ov = s_ov_shape = s_ov_kicker = s_ov_title = s_ov_body = s_ov_note = NULL;
    s_ov_b1 = s_ov_b1_lbl = s_ov_b2 = s_ov_b2_lbl = NULL;
    s_delay_fn = NULL;
    s_hold_idx = s_jump_idx = s_spy_idx = -1;
    s_busy = false;
    kit_ui_bind(NULL);
    s_api = NULL;
}

#endif
