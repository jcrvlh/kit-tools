/**
 * @file main.c
 * @brief Repete — jogo de memória de mesa pro KIT (estilo "Genius"/Simon).
 *
 * O KIT mostra uma sequência com as 3 formas do KIT — círculo (azul),
 * triângulo (amarelo), quadrado (vermelho) — e você repete tocando nelas.
 * Acertou a sequência inteira, ela cresce +1. Errou, acabou.
 *
 * Modos (na página AJUSTE):
 *   - Clássico  : sequência +1 por rodada, velocidade fixa.
 *   - Velocidade: igual, mas o playback acelera a cada rodada.
 *   - Inverso   : repetir a sequência de trás pra frente.
 *
 * Recorde por modo, persistido. Linguagem visual "Brutalist Bauhaus"
 * (kit_theme.h / kit_fonts.h). Toda a UI atrás de #ifndef KIT_SDK_STUBS.
 *
 * Runtime das Tools do catálogo: só inteiro de 32 bits (nada de float), só a
 * whitelist de símbolos LVGL (ver tools-sdk/docs/tool_lvgl_runtime.md), e
 * tool_destroy zera todo ponteiro estático.
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KIT_SDK_STUBS

/* --- métricas (tela 368 × 448) ---------------------------------------- */
#define SCREEN_W      368
#define SCREEN_H      448
#define PAD           16
#define CONTENT       (SCREEN_W - 2 * PAD)   /* 336 */
#define TITLEBAR      88
#define PAGE_H        (SCREEN_H - TITLEBAR)
#define CHIP          56
#define KEY_SIZE      104
#define KEY_GAP       12
#define BTN_H         76
#define BTN_MARGIN    18
#define PAGES         3
#define SHAPES        3

/* --- modos ----------------------------------------------------------- */
enum { MODE_CLASSIC = 0, MODE_SPEED = 1, MODE_INVERSE = 2, MODE_COUNT = 3 };
static const char *MODE_NAME[MODE_COUNT] = { "CLASSICO", "VELOCIDADE", "INVERSO" };
static const char *HI_KEY[MODE_COUNT]    = { "repete_hi0", "repete_hi1", "repete_hi2" };
#define K_MODE  "repete_mode"

/* --- formas -------------------------------------------------------------
 * Ordem: 0 círculo, 1 triângulo, 2 quadrado — associação Bauhaus/Kandinsky
 * (círculo→azul, triângulo→amarelo, quadrado→vermelho). */
static const char *SHAPE_GLYPH[SHAPES] = { KIT_ICON_CIRCLE, KIT_ICON_TRIANGLE, KIT_ICON_SQUARE };
static const uint32_t SHAPE_COLOR[SHAPES] = { KIT_COLOR_BLUE, KIT_COLOR_YELLOW, KIT_COLOR_RED };
static const uint32_t SHAPE_ON[SHAPES]    = { KIT_COLOR_ON_COLOR, KIT_COLOR_ON_YELLOW, KIT_COLOR_ON_COLOR };
static const uint16_t SHAPE_FREQ[SHAPES]  = { 330, 523, 784 };   /* grave / médio / agudo */

/* --- estado do jogo ------------------------------------------------- */
enum { ST_IDLE = 0, ST_PLAYBACK, ST_INPUT, ST_OVER };

#define MAX_SEQ  100

static const kit_api_table_t *s_api = NULL;

static int      s_mode  = MODE_CLASSIC;
static int      s_hi[MODE_COUNT] = { 0, 0, 0 };

static uint8_t  s_seq[MAX_SEQ];
static int      s_round     = 0;    /* comprimento atual da sequência */
static int      s_state     = ST_IDLE;
static int      s_play_idx  = 0;    /* passo do playback */
static bool     s_play_on   = false;/* fase liga/desliga do playback */
static int      s_in_idx    = 0;    /* passo da resposta do jogador */
static int      s_expected  = -1;   /* forma certa no momento do erro */

/* --- objetos LVGL (todos zerados em tool_destroy) ------------------- */
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];
static lv_obj_t *s_mode_chip[MODE_COUNT];
static lv_obj_t *s_keys[SHAPES];
static lv_obj_t *s_key_lbl[SHAPES];
static lv_obj_t *s_status_lbl = NULL;
static lv_obj_t *s_score_lbl = NULL;
static lv_obj_t *s_btn = NULL;
static lv_obj_t *s_btn_lbl = NULL;

/* --- timers -------------------------------------------------------- */
static lv_timer_t *s_pb_timer    = NULL;   /* playback da sequência */
static lv_timer_t *s_blip_timer  = NULL;   /* apaga a forma tocada pelo jogador */
static lv_timer_t *s_gap_timer   = NULL;   /* pausa entre rodadas / marco */
static lv_timer_t *s_shake_timer = NULL;   /* tremida da tela no erro */

/* --- helpers ------------------------------------------------------- */
static lv_obj_t *add_label(lv_obj_t *parent, const char *txt, uint32_t color,
                           const lv_font_t *font, int letter_space)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    if (letter_space) lv_obj_set_style_text_letter_space(l, letter_space, 0);
    return l;
}

static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void kill_timer(lv_timer_t **t)
{
    if (*t) { lv_timer_delete(*t); *t = NULL; }
}

/* --- persistência ------------------------------------------------- */
static void load_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    int32_t v;
    if (s_api->storage->get_i32(K_MODE, &v) == KIT_OK && v >= 0 && v < MODE_COUNT)
        s_mode = (int)v;
    for (int m = 0; m < MODE_COUNT; m++)
        if (s_api->storage->get_i32(HI_KEY[m], &v) == KIT_OK && v >= 0 && v < MAX_SEQ)
            s_hi[m] = (int)v;
}

static void save_mode(void)
{
    if (s_api && s_api->storage) s_api->storage->set_i32(K_MODE, s_mode);
}

static void save_hi(int m)
{
    if (s_api && s_api->storage) s_api->storage->set_i32(HI_KEY[m], s_hi[m]);
}

/* --- áudio ------------------------------------------------------- */
static void shape_beep(int i)
{
    if (s_api && s_api->audio) s_api->audio->beep(SHAPE_FREQ[i], 130);
}

static void sfx(kit_sfx_t e)
{
    if (s_api && s_api->audio) s_api->audio->sfx(e);
}

/* --- render de uma "tecla" (forma) ----------------------------- */
static void key_set_lit(int i, bool lit)
{
    lv_obj_t *k = s_keys[i];
    if (!k) return;
    if (lit) {
        lv_obj_set_style_bg_color(k, lv_color_hex(SHAPE_COLOR[i]), 0);
        lv_obj_set_style_border_width(k, 3, 0);
        lv_obj_set_style_border_color(k, lv_color_hex(SHAPE_ON[i]), 0);
        lv_obj_set_style_translate_y(k, -4, 0);
        lv_obj_set_style_text_color(s_key_lbl[i], lv_color_hex(SHAPE_ON[i]), 0);
    } else {
        lv_obj_set_style_bg_color(k, lv_color_hex(KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_border_width(k, 0, 0);
        lv_obj_set_style_translate_y(k, 0, 0);
        lv_obj_set_style_text_color(s_key_lbl[i], lv_color_hex(SHAPE_COLOR[i]), 0);
    }
}

static void keys_all_lit(bool lit)
{
    for (int i = 0; i < SHAPES; i++) key_set_lit(i, lit);
}

/* --- status ---------------------------------------------------- */
static void set_status(const char *txt) { if (s_status_lbl) lv_label_set_text(s_status_lbl, txt); }

static void set_score(const char *txt)
{
    if (!s_score_lbl) return;
    lv_label_set_text(s_score_lbl, txt ? txt : "");
}

static void set_btn(const char *txt, bool enabled)
{
    if (s_btn_lbl) lv_label_set_text(s_btn_lbl, txt);
    if (!s_btn) return;
    if (enabled) {
        lv_obj_add_flag(s_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(s_btn, LV_OPA_COVER, 0);
    } else {
        lv_obj_remove_flag(s_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(s_btn, LV_OPA_40, 0);
    }
}

/* --- timing do playback -------------------------------------- */
static uint32_t lit_ms(void)
{
    if (s_mode != MODE_SPEED) return 420;
    int v = 420 - 26 * (s_round - 1);
    return (uint32_t)(v < 150 ? 150 : v);
}

static uint32_t gap_ms(void)
{
    if (s_mode != MODE_SPEED) return 170;
    int v = 170 - 10 * (s_round - 1);
    return (uint32_t)(v < 80 ? 80 : v);
}

/* --- máquina de estados ------------------------------------- */
static void start_playback(void);

static void pb_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_play_on) {
        /* fim da fase "acesa" — apaga e avança */
        keys_all_lit(false);
        s_play_on = false;
        s_play_idx++;
        lv_timer_set_period(s_pb_timer, gap_ms());
        return;
    }
    /* fim da fase "apagada" */
    if (s_play_idx >= s_round) {
        kill_timer(&s_pb_timer);
        s_state = ST_INPUT;
        s_in_idx = 0;
        set_status("SUA VEZ");
        return;
    }
    int i = s_seq[s_play_idx];
    key_set_lit(i, true);
    shape_beep(i);
    s_play_on = true;
    lv_timer_set_period(s_pb_timer, lit_ms());
}

static void start_playback(void)
{
    kill_timer(&s_pb_timer);
    kill_timer(&s_blip_timer);
    keys_all_lit(false);
    s_state = ST_PLAYBACK;
    s_play_idx = 0;
    s_play_on = false;
    char b[24];
    snprintf(b, sizeof b, "RODADA %d", s_round);
    set_status(b);
    set_score("");
    s_pb_timer = lv_timer_create(pb_tick_cb, 520, NULL);   /* respiro antes do 1º */
}

static void next_round_cb(lv_timer_t *t)
{
    (void)t;
    kill_timer(&s_gap_timer);
    keys_all_lit(false);
    if (s_round >= MAX_SEQ) {           /* memória perfeita — encerra vitorioso */
        s_state = ST_OVER;
        set_status("MEMORIA PERFEITA");
        set_score("VOCE ZEROU");
        set_btn("JOGAR DE NOVO", true);
        sfx(KIT_SFX_REVEAL);
        return;
    }
    s_seq[s_round] = (uint8_t)s_api->random->range(0, SHAPES - 1);
    s_round++;
    start_playback();
}

static void milestone_off_cb(lv_timer_t *t)
{
    (void)t;
    kill_timer(&s_gap_timer);
    keys_all_lit(false);
    s_gap_timer = lv_timer_create(next_round_cb, 360, NULL);
    lv_timer_set_repeat_count(s_gap_timer, 1);
}

static void round_cleared(void)
{
    int done = s_round;                 /* rodadas concluídas */
    if (done > s_hi[s_mode]) { s_hi[s_mode] = done; save_hi(s_mode); }

    if (done % 5 == 0) {
        set_status("MUITO BEM");
        keys_all_lit(true);
        sfx(KIT_SFX_CONFIRM);
        s_gap_timer = lv_timer_create(milestone_off_cb, 460, NULL);
    } else {
        set_status("CERTO");
        s_gap_timer = lv_timer_create(next_round_cb, 500, NULL);
    }
    lv_timer_set_repeat_count(s_gap_timer, 1);
}

/* tremida da tela ------------------------------------------------ */
static const int SHAKE_DX[] = { -10, 8, -6, 4, -2, 0 };
static int s_shake_step = 0;

static void shake_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_shake_step >= (int)(sizeof SHAKE_DX / sizeof SHAKE_DX[0])) {
        if (s_tv) lv_obj_set_style_translate_x(s_tv, 0, 0);
        kill_timer(&s_shake_timer);
        s_shake_step = 0;
        return;
    }
    if (s_tv) lv_obj_set_style_translate_x(s_tv, SHAKE_DX[s_shake_step], 0);
    s_shake_step++;
}

static void screen_shake(void)
{
    kill_timer(&s_shake_timer);
    s_shake_step = 0;
    s_shake_timer = lv_timer_create(shake_tick_cb, 45, NULL);
}

static void game_over(void)
{
    kill_timer(&s_pb_timer);
    kill_timer(&s_blip_timer);
    s_state = ST_OVER;

    int score = s_round - 1;            /* rodadas concluídas antes do erro */
    if (score < 0) score = 0;

    /* mostra a forma certa, apaga as outras */
    for (int i = 0; i < SHAPES; i++) key_set_lit(i, s_expected >= 0 && i == s_expected);

    char b[32];
    if (score > 0 && score == s_hi[s_mode]) {
        set_status("NOVO RECORDE");
        snprintf(b, sizeof b, "%d FORMAS", score);
    } else {
        set_status("ERROU");
        snprintf(b, sizeof b, "VOCE FEZ %d  \xC2\xB7  RECORDE %d", score, s_hi[s_mode]);
    }
    set_score(b);
    set_btn("JOGAR DE NOVO", true);
    sfx(KIT_SFX_ESTOURO_POP);
    screen_shake();
}

static void blip_off_cb(lv_timer_t *t)
{
    (void)t;
    kill_timer(&s_blip_timer);
    if (s_state == ST_INPUT) keys_all_lit(false);
}

static void on_key(int i)
{
    if (s_state != ST_INPUT) return;

    int pos = (s_mode == MODE_INVERSE) ? (s_round - 1 - s_in_idx) : s_in_idx;
    s_expected = s_seq[pos];

    key_set_lit(i, true);
    shape_beep(i);
    kill_timer(&s_blip_timer);
    s_blip_timer = lv_timer_create(blip_off_cb, 170, NULL);
    lv_timer_set_repeat_count(s_blip_timer, 1);

    if (i != s_expected) { game_over(); return; }

    s_in_idx++;
    if (s_in_idx >= s_round) {
        s_state = ST_PLAYBACK;         /* trava input durante a pausa */
        round_cleared();
    }
}

/* --- callbacks LVGL --------------------------------------------- */
static void back_cb(lv_event_t *e)  { (void)e; if (s_api && s_api->system) s_api->system->exit(); }

static void key_cb(lv_event_t *e)
{
    on_key((int)(intptr_t)lv_event_get_user_data(e));
}

static void btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_state == ST_PLAYBACK || s_state == ST_INPUT) return;
    /* IDLE ou OVER → começa uma partida nova */
    kill_timer(&s_gap_timer);
    kill_timer(&s_blip_timer);
    kill_timer(&s_pb_timer);
    kill_timer(&s_shake_timer);
    if (s_tv) lv_obj_set_style_translate_x(s_tv, 0, 0);
    s_round = 1;
    s_seq[0] = (uint8_t)s_api->random->range(0, SHAPES - 1);
    s_expected = -1;
    set_btn("...", false);
    sfx(KIT_SFX_TOOL_OPEN);
    start_playback();
}

static void mode_cb(lv_event_t *e)
{
    int m = (int)(intptr_t)lv_event_get_user_data(e);
    if (m == s_mode) return;
    s_mode = m;
    save_mode();
    for (int k = 0; k < MODE_COUNT; k++) {
        bool sel = (k == s_mode);
        lv_obj_set_style_bg_color(s_mode_chip[k],
            lv_color_hex(sel ? KIT_COLOR_GREEN : KIT_COLOR_SURFACE), 0);
        lv_obj_t *lbl = lv_obj_get_child(s_mode_chip[k], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            lv_color_hex(sel ? KIT_COLOR_ON_COLOR : KIT_COLOR_TEXT), 0);
    }
    /* trocar de modo reinicia o placar da tela */
    if (s_state != ST_PLAYBACK && s_state != ST_INPUT) {
        s_state = ST_IDLE;
        keys_all_lit(false);
        char b[24];
        snprintf(b, sizeof b, "MODO %s", MODE_NAME[m]);
        set_status(b);
        snprintf(b, sizeof b, "RECORDE %d", s_hi[m]);
        set_score(b);
        set_btn("COMECAR", true);
    }
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    int act = 0;
    lv_obj_t *t = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < PAGES; i++) if (s_tiles[i] == t) act = i;
    for (int i = 0; i < PAGES; i++) {
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(i == act ? KIT_COLOR_GREEN : KIT_COLOR_LINE), 0);
        lv_obj_set_size(s_dots[i], i == act ? 20 : 8, 8);
    }
}

/* --- construção da tela --------------------------------------- */
static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, CHIP, CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, PAD, 16);
    lv_obj_center(add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    lv_obj_t *title = add_label(s_screen, "REPETE", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, PAD + CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -PAD, 40);
    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        s_dots[i] = d;
    }
}

/* Página 0 — AJUSTE: seletor de modo (2 chips + 1). */
static lv_obj_t *make_mode_chip(lv_obj_t *parent, int m)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 156, 84);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_bg_color(c,
        lv_color_hex(m == s_mode ? KIT_COLOR_GREEN : KIT_COLOR_SURFACE), 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(c, 8);
    lv_obj_add_event_cb(c, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)m);
    lv_obj_t *lbl = add_label(c, MODE_NAME[m],
        m == s_mode ? KIT_COLOR_ON_COLOR : KIT_COLOR_TEXT, &kit_mono_20, 1);
    lv_obj_center(lbl);
    return c;
}

static void build_page_setup(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = plain_box(tile);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, PAD, 0);
    lv_obj_set_style_pad_right(p, PAD, 0);
    lv_obj_set_style_pad_top(p, 24, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 14, 0);

    add_label(p, "MODO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);

    lv_obj_t *row = plain_box(p);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    s_mode_chip[MODE_CLASSIC] = make_mode_chip(row, MODE_CLASSIC);
    s_mode_chip[MODE_SPEED]   = make_mode_chip(row, MODE_SPEED);

    s_mode_chip[MODE_INVERSE] = make_mode_chip(p, MODE_INVERSE);

    lv_obj_t *hint = add_label(p,
        "Velocidade acelera a cada rodada.\nInverso: repita de tras pra frente.",
        KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, CONTENT);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

/* Página 1 — JOGO. O botão é filho DESTE tile. */
static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *group = plain_box(tile);
    lv_obj_set_size(group, CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 16, 0);
    lv_obj_align(group, LV_ALIGN_CENTER, 0, -(BTN_H + BTN_MARGIN) / 2);

    s_status_lbl = add_label(group, "MODO CLASSICO", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_set_width(s_status_lbl, CONTENT);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *keyrow = plain_box(group);
    lv_obj_set_size(keyrow, CONTENT, KEY_SIZE);
    lv_obj_set_flex_flow(keyrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(keyrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(keyrow, KEY_GAP, 0);
    for (int i = 0; i < SHAPES; i++) {
        lv_obj_t *k = lv_obj_create(keyrow);
        lv_obj_set_size(k, KEY_SIZE, KEY_SIZE);
        lv_obj_set_style_radius(k, 22, 0);
        lv_obj_set_style_border_width(k, 0, 0);
        lv_obj_set_style_pad_all(k, 0, 0);
        lv_obj_set_style_bg_color(k, lv_color_hex(KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_remove_flag(k, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(k, 6);
        lv_obj_add_event_cb(k, key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *g = add_label(k, SHAPE_GLYPH[i], SHAPE_COLOR[i], &kit_display_44, 0);
        lv_obj_center(g);
        s_keys[i] = k;
        s_key_lbl[i] = g;
    }

    s_score_lbl = add_label(group, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    lv_obj_set_width(s_score_lbl, CONTENT);
    lv_obj_set_style_text_align(s_score_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_btn = lv_obj_create(tile);
    lv_obj_set_size(s_btn, CONTENT, BTN_H);
    lv_obj_set_style_radius(s_btn, BTN_H / 2, 0);
    lv_obj_set_style_border_width(s_btn, 0, 0);
    lv_obj_set_style_pad_all(s_btn, 0, 0);
    lv_obj_set_style_bg_color(s_btn, lv_color_hex(KIT_COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(s_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_btn, 8);
    lv_obj_align(s_btn, LV_ALIGN_BOTTOM_MID, 0, -BTN_MARGIN);
    lv_obj_add_event_cb(s_btn, btn_cb, LV_EVENT_CLICKED, NULL);
    s_btn_lbl = add_label(s_btn, "COMECAR", KIT_COLOR_ON_COLOR, &kit_mono_26, 3);
    lv_obj_center(s_btn_lbl);
}

/* Página 2 — COMO JOGA. */
static const char RULES[] =
    "1. Toque em COMECAR. O KIT acende as formas numa ordem — preste atencao.\n\n"
    "2. Sua vez: repita a ordem tocando nas formas (circulo, triangulo, quadrado).\n\n"
    "3. Acertou a sequencia inteira? Ela ganha mais uma forma. Errou uma, acabou.\n\n"
    "No AJUSTE voce troca o modo: Classico, Velocidade (acelera) ou Inverso (de tras pra frente).";

static void build_page_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = plain_box(tile);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, CONTENT);
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, SCREEN_W, PAGE_H);
    lv_obj_set_pos(s_tv, 0, TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);
    build_page_setup(s_tiles[0]);
    build_page_game(s_tiles[1]);
    build_page_help(s_tiles[2]);
}

/* --- ciclo de vida ------------------------------------------- */
KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    if (!s_api->random) return KIT_ERR_NOT_SUPPORTED;

    load_prefs();

    s_state = ST_IDLE;
    s_round = 0;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   /* abre no JOGO */
    tv_changed_cb(NULL);

    char b[24];
    snprintf(b, sizeof b, "MODO %s", MODE_NAME[s_mode]);
    set_status(b);
    snprintf(b, sizeof b, "RECORDE %d", s_hi[s_mode]);
    set_score(b);
    set_btn("COMECAR", true);

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    kill_timer(&s_pb_timer);
    kill_timer(&s_blip_timer);
    kill_timer(&s_gap_timer);
    kill_timer(&s_shake_timer);

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }
    s_tv = NULL;
    for (int i = 0; i < PAGES; i++)  { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < MODE_COUNT; i++) s_mode_chip[i] = NULL;
    for (int i = 0; i < SHAPES; i++) { s_keys[i] = NULL; s_key_lbl[i] = NULL; }
    s_status_lbl = s_score_lbl = s_btn = s_btn_lbl = NULL;

    s_state = ST_IDLE;
    s_round = 0;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo (CI / teste de lógica, sem UI) */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Repete stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
