/**
 * @file main.c
 * @brief Tarot — Tool de tiragem e interpretação de tarot para o KIT.
 *
 * Layout no padrão das Tools nativas (ex.: Dados): titlebar fixa com o chip
 * de voltar e os pontos de página, um tileview que desliza na horizontal e um
 * botão-pílula fixo no rodapé.
 *
 *   MENU      [0] AJUSTES  ·  [1] PRINCIPAL (abre aqui; Ajustes fica à esquerda)
 *   SORTEIO   animação de embaralho (mística)
 *   RESULTADO 1 carta:  [0] A CARTA  ·  [1] O QUE É   ·  [2] NA LEITURA
 *             3 cartas: [0] PASSADO  ·  [1] PRESENTE  ·  [2] FUTURO  ·  [3] RESUMO
 *
 * Tirar carta: chacoalhar, tocar na tela principal ou o botão TIRAR (que só
 * aparece na PRINCIPAL). Ajustes: tiragem (1/3), baralho (22 maiores ou 78) e
 * cartas invertidas.
 * Textos grandes (kit_sans_22 no corpo), rolagem vertical quando precisa.
 * Áudio: escala Frígia dominante de Mi ("Hijaz", modo de sonoridade oculta) —
 * desce devagar no embaralho, floreio na revelação; deslizar entre tiles é
 * mudo. O travessão (—) é trocado por hífen
 * na hora de renderizar porque as fontes do KIT não trazem esse glifo.
 *
 * Sorteio puro em tarot_draw.c; dados das 78 cartas em tarot_deck.c.
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "tarot_draw.h"
#include "tarot_deck.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * Layout (368 × 448)
 * -------------------------------------------------------------------------- */
#define T_W          368
#define T_H          448
#define T_PAD        16
#define T_CONTENT    (T_W - 2 * T_PAD)               /* 336 */
#define T_TITLEBAR   88
#define T_FOOT       104
#define T_PAGE_H     (T_H - T_TITLEBAR - T_FOOT)     /* 256 — telas com rodapé fixo */
#define T_PAGE_H_FULL (T_H - T_TITLEBAR)             /* 360 — menu (rodapé só na PRINCIPAL) */
#define T_CHIP       56
#define T_BTN_H      76
#define T_BTN_MARGIN 18
#define T_ACCENT     KIT_COLOR_BLUE

/* Menu: AJUSTES fica à esquerda (tile 0), PRINCIPAL à direita (tile 1) e a
 * tela abre já na PRINCIPAL. */
#define T_TILE_FOCUS  1

/* "Só Arcanos Maiores": os 22 primeiros índices do baralho. */
#define T_MAJOR_COUNT 22

/* Embaralho — desacelera até assentar; o tempo mais longo dá suspense. */
#define T_SHUFFLE_TICKS   18
#define T_SHUFFLE_MS_MIN  55
#define T_SHUFFLE_MS_MAX  200

#ifndef KIT_SDK_STUBS

/* --------------------------------------------------------------------------
 * Estado
 * -------------------------------------------------------------------------- */
typedef enum { ST_MENU, ST_SHUFFLE, ST_RESULT } tarot_state_t;

static const kit_api_table_t *s_api = NULL;
static lv_obj_t *s_screen = NULL;

static lv_obj_t   *s_tv        = NULL;    /* tileview da tela corrente */
static lv_obj_t   *s_tiles[5]  = { 0 };
static lv_obj_t   *s_dots[5]   = { 0 };
static int         s_dot_n     = 0;
static lv_obj_t   *s_footer    = NULL;    /* botão-pílula do menu (some nos Ajustes) */
static lv_obj_t   *s_shuf_lbl  = NULL;
static lv_timer_t *s_anim      = NULL;
static int         s_anim_tick = 0;

static tarot_state_t s_state = ST_MENU;

static bool s_reversed_on = true;   /* config: cartas invertidas */
static int  s_pick_count  = 1;      /* config: 1 = uma carta, 3 = três */
static bool s_major_only  = false;  /* config: só os 22 Arcanos Maiores */

static tarot_pick_t s_picks[TAROT_MAX_PICKS];
static int          s_result_n = 0;

static const char *const POS_TITLE[3] = { "PASSADO", "PRESENTE", "FUTURO" };

/* Forward decls */
static void build_menu(void);
static void build_menu_at(int start_tile);
static void menu_sync_footer(void);
static void build_shuffle(void);
static void build_result(void);

/* --------------------------------------------------------------------------
 * RNG (adapta ctx->api->random->range à assinatura tarot_rng_fn)
 * -------------------------------------------------------------------------- */
static int rng_range(int lo, int hi)
{
    if (s_api && s_api->random) return (int)s_api->random->range(lo, hi);
    return lo;   /* inalcançável: "random" é permissão do manifest */
}

/* --------------------------------------------------------------------------
 * Áudio — bipes próprios na escala Frígia dominante de Mi ("Hijaz"), o modo
 * de sonoridade oculta/esotérica: E4 F4 G#4 A4 B4 C5 D5 E5, com o salto de 2ª
 * aumentada (F->G#) que dá o tom místico. Tudo >= 300 Hz, notas de 70–300 ms —
 * nada abaixo de 200 Hz nem curto demais, para não "estourar" no alto-falante.
 * A fila de bipes tem 6 posições; as frases ficam em 5 no máximo.
 * -------------------------------------------------------------------------- */
static void beep(int freq, int ms)
{
    if (s_api && s_api->audio) s_api->audio->beep((uint16_t)freq, (uint16_t)ms);
}

/* E4 F4 G#4 A4 B4 C5 D5 E5 — Frígia dominante de Mi, de baixo p/ cima. */
static const int HIJAZ[8] = { 330, 349, 415, 440, 494, 523, 587, 659 };

static void sfx_tap(void)   { beep(415, 24); }               /* G#4, curto */
static void sfx_back(void)  { beep(349, 22); beep(330, 36); } /* F4 -> E4 */

/* Embaralho: a escala Hijaz DESCE devagar, uma nota a cada poucos ticks — uma
 * ladainha hipnótica que some no grave. As últimas notas seguram mais tempo,
 * como um ritual desacelerando; a animação trava logo depois, no G#4 suspenso,
 * e a revelação resolve. Deslizar entre tiles é mudo de propósito. */
static void sfx_shuffle_note(int tick)
{
    if (tick % 3 != 0) return;                 /* ~6 notas nos 18 ticks */
    int step = tick / 3;                        /* 0,1,2,... */
    int i = 7 - step;                           /* E5 -> ... -> desce */
    if (i < 2) i = 2;                           /* não passa do G#4 grave */
    beep(HIJAZ[i], step >= 3 ? 150 : 85);       /* segura as últimas */
}

/* Revelação: floreio Hijaz com o salto F->G#. Sobe e abre na carta normal
 * (E F G# C5 E5, última longa); desce e fecha na invertida. */
static void sfx_reveal(bool reversed)
{
    static const int up[5] = { 330, 349, 415, 523, 659 };  /* E  F  G# C5 E5 */
    static const int dn[5] = { 659, 523, 415, 349, 330 };
    const int *seq = reversed ? dn : up;
    for (int i = 0; i < 5; i++) beep(seq[i], i == 4 ? 300 : 105);
}

/* --------------------------------------------------------------------------
 * Helpers LVGL
 * -------------------------------------------------------------------------- */
static uint32_t on_accent(void)
{
    return (T_ACCENT == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

/* As fontes do KIT trazem Latin-1 + U+2022, mas não o travessão (—) nem a seta
 * (→) — sem tratamento eles viram um retângulo vazado. Troca-os na hora de
 * renderizar por equivalentes que existem (hífen e o chevron FontAwesome). */
static const char *tr(const char *s)
{
    static char buf[1024];
    if (!s) return "";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && o + 4 < sizeof buf; ) {
        if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0x94 || p[2] == 0x93)) {
            buf[o++] = '-';                       /* — U+2014 / – U+2013 */
            p += 3;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) {
            buf[o++] = '.'; buf[o++] = '.'; buf[o++] = '.';   /* … U+2026 */
            p += 3;
        } else if (p[0] == 0xE2 && p[1] == 0x86 && p[2] == 0x92) {
            buf[o++] = 0xEF; buf[o++] = 0x83; buf[o++] = 0x9A; /* → U+2192 → chevron */
            p += 3;
        } else {
            buf[o++] = *p++;
        }
    }
    buf[o] = 0;
    return buf;
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *txt, uint32_t color,
                           const lv_font_t *font, int letter_space)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_label_set_text(l, tr(txt));
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    if (letter_space) lv_obj_set_style_text_letter_space(l, letter_space, 0);
    return l;
}

/* Parágrafo que quebra linha, com respiro entre linhas (legibilidade). */
static lv_obj_t *add_wrap(lv_obj_t *parent, const char *txt, uint32_t color,
                          const lv_font_t *font, lv_text_align_t align)
{
    lv_obj_t *l = add_label(parent, txt, color, font, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, T_CONTENT);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_style_text_line_space(l, 5, 0);
    return l;
}

static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void hairline(lv_obj_t *parent)
{
    lv_obj_t *sep = plain_box(parent);
    lv_obj_set_size(sep, T_CONTENT - 40, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(sep, 4, 0);
    lv_obj_set_style_pad_bottom(sep, 4, 0);
}

/* Coluna com rolagem vertical, ocupa um tile inteiro. */
static lv_obj_t *scroll_col(lv_obj_t *tile, int pad_top, int pad_bottom)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *c = lv_obj_create(tile);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(c, T_PAD, 0);
    lv_obj_set_style_pad_right(c, T_PAD, 0);
    lv_obj_set_style_pad_top(c, pad_top, 0);
    lv_obj_set_style_pad_bottom(c, pad_bottom, 0);
    lv_obj_set_style_pad_row(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_AUTO);
    return c;
}

/* Botão-pílula fixo no rodapé. */
static lv_obj_t *build_footer(lv_obj_t *scr, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_obj_create(scr);
    lv_obj_set_size(b, T_CONTENT, T_BTN_H);
    lv_obj_set_style_radius(b, T_BTN_H / 2, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(T_ACCENT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 10);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -T_BTN_MARGIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(add_label(b, text, on_accent(), &kit_mono_26, 3));
    return b;
}

/* --------------------------------------------------------------------------
 * Ciclo de telas
 * -------------------------------------------------------------------------- */
static void kill_anim(void)
{
    if (s_anim) { lv_timer_delete(s_anim); s_anim = NULL; }
    s_anim_tick = 0;
}

static lv_obj_t *new_screen(void)
{
    kill_anim();
    s_tv = NULL;
    s_dot_n = 0;
    s_footer = NULL;
    s_shuf_lbl = NULL;
    for (int i = 0; i < 5; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static void show_screen(lv_obj_t *scr)
{
    lv_obj_t *old = s_screen;
    s_screen = scr;
    lv_screen_load(scr);
    if (old) lv_obj_delete(old);
}

/* --------------------------------------------------------------------------
 * Titlebar + pontos de página
 * -------------------------------------------------------------------------- */
static void back_cb(lv_event_t *e)
{
    (void)e;
    sfx_back();
    if (s_state == ST_MENU) {
        if (s_api && s_api->system) s_api->system->exit();
    } else {
        build_menu();
    }
}

static void sync_dots(void)
{
    if (!s_tv || s_dot_n == 0) return;
    lv_obj_t *act = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < s_dot_n; i++) {
        bool on = (act == s_tiles[i]);
        lv_obj_set_size(s_dots[i], on ? 20 : 8, 8);
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(on ? T_ACCENT : KIT_COLOR_LINE), 0);
    }
}

static void build_titlebar(lv_obj_t *scr, const char *title, int n_dots)
{
    lv_obj_t *chip = lv_obj_create(scr);
    lv_obj_set_size(chip, T_CHIP, T_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, T_PAD, 16);
    lv_obj_center(add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    lv_obj_t *t = add_label(scr, title, KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, T_PAD + T_CHIP + 12, 30);

    s_dot_n = (n_dots > 5) ? 5 : n_dots;
    if (s_dot_n <= 0) return;

    lv_obj_t *row = plain_box(scr);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_align(row, LV_ALIGN_TOP_RIGHT, -T_PAD, 40);
    for (int i = 0; i < s_dot_n; i++) {
        lv_obj_t *d = plain_box(row);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(KIT_COLOR_LINE), 0);
        s_dots[i] = d;
    }
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    if (!s_tv) return;   /* ignora eventos disparados durante a construção */
    sync_dots();          /* deslizar é mudo — o bipe a cada swipe cansava */
    menu_sync_footer();
}

static void build_tileview(lv_obj_t *scr, int n_tiles, int page_h)
{
    lv_obj_t *tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, T_W, page_h);
    lv_obj_set_pos(tv, 0, T_TITLEBAR);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    for (int i = 0; i < n_tiles && i < 5; i++)
        s_tiles[i] = lv_tileview_add_tile(tv, (uint8_t)i, 0, LV_DIR_HOR);
    s_tv = tv;   /* só agora: libera o tv_changed_cb */
}

/* dica "deslize →" — último item da coluna. */
static void swipe_hint(lv_obj_t *col, const char *txt)
{
    lv_obj_t *h = add_label(col, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_set_style_pad_top(h, 6, 0);
}

/* --------------------------------------------------------------------------
 * Persistência
 * -------------------------------------------------------------------------- */
static void load_config(void)
{
    if (!s_api || !s_api->storage) return;
    int32_t v;
    if (s_api->storage->get_i32("reversed_on", &v) == KIT_OK) s_reversed_on = (v != 0);
    if (s_api->storage->get_i32("pick_count", &v) == KIT_OK && (v == 1 || v == 3))
        s_pick_count = (int)v;
    if (s_api->storage->get_i32("major_only", &v) == KIT_OK) s_major_only = (v != 0);
}

static void save_config(void)
{
    if (!s_api || !s_api->storage) return;
    s_api->storage->set_i32("reversed_on", s_reversed_on ? 1 : 0);
    s_api->storage->set_i32("pick_count", s_pick_count);
    s_api->storage->set_i32("major_only", s_major_only ? 1 : 0);
}

/* --------------------------------------------------------------------------
 * Render de uma carta (bloco completo, usado nas telas de leitura)
 * -------------------------------------------------------------------------- */
static void card_head(lv_obj_t *col, const tarot_pick_t *pick)
{
    const tarot_card_t *c = &tarot_deck[pick->index];
    /* kit_display_* é só dígitos/caixa-alta — nome da carta (acentos, minúsculas)
       vai no maior sans com Latin-1 completo. */
    add_wrap(col, c->name, KIT_COLOR_TEXT, &kit_sans_28, LV_TEXT_ALIGN_CENTER);
    add_wrap(col, c->arcana, KIT_COLOR_TEXT_MUTED, &kit_sans_22, LV_TEXT_ALIGN_CENTER);
    if (pick->reversed)
        add_label(col, "INVERTIDA", T_ACCENT, &kit_mono_20, 3);
    add_wrap(col, c->keywords, T_ACCENT, &kit_sans_22, LV_TEXT_ALIGN_CENTER);
}

/* --------------------------------------------------------------------------
 * TELA: MENU  —  [0] CONCENTRE-SE   [1] AJUSTES
 * ========================================================================== */
static const char *tirar_label(void)
{
    return s_pick_count == 3 ? "TIRAR AS TR\xC3\x8AS" : "TIRAR CARTA";
}

static void go_draw(void)
{
    if (s_state != ST_MENU) return;
    sfx_tap();
    build_shuffle();
}

static void footer_draw_cb(lv_event_t *e) { (void)e; go_draw(); }
static void focus_tap_cb(lv_event_t *e)   { (void)e; go_draw(); }

static void on_shake(void *ud)
{
    (void)ud;
    go_draw();
}

/* --- Ajustes: chips --- */
static void pick_chip_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_pick_count) return;
    s_pick_count = v;
    sfx_tap();
    save_config();
    build_menu_at(0);   /* reconstrói e permanece no tile de Ajustes */
}

static void rev_chip_cb(lv_event_t *e)
{
    bool v = (bool)(intptr_t)lv_event_get_user_data(e);
    if (v == s_reversed_on) return;
    s_reversed_on = v;
    sfx_tap();
    save_config();
    build_menu_at(0);
}

static void major_chip_cb(lv_event_t *e)
{
    bool v = (bool)(intptr_t)lv_event_get_user_data(e);
    if (v == s_major_only) return;
    s_major_only = v;
    sfx_tap();
    save_config();
    build_menu_at(0);
}

static void chip_pair(lv_obj_t *col, const char *label,
                      const char *a, const char *b, bool a_sel,
                      lv_event_cb_t cb, void *ud_a, void *ud_b)
{
    add_label(col, label, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);

    lv_obj_t *row = plain_box(col);
    lv_obj_set_size(row, T_CONTENT, 60);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);

    const char *txt[2] = { a, b };
    void *ud[2]        = { ud_a, ud_b };
    for (int i = 0; i < 2; i++) {
        bool sel = (i == 0) ? a_sel : !a_sel;
        lv_obj_t *chip = lv_obj_create(row);
        lv_obj_set_height(chip, 60);
        lv_obj_set_flex_grow(chip, 1);
        lv_obj_set_style_bg_color(chip,
            lv_color_hex(sel ? T_ACCENT : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_radius(chip, 16, 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, ud[i]);
        lv_obj_center(add_label(chip, txt[i],
            sel ? on_accent() : KIT_COLOR_TEXT, &kit_mono_20, 1));
    }
}

static void build_menu_tile_focus(lv_obj_t *tile)
{
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, focus_tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(tile);
    lv_obj_set_size(col, T_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 20, 0);
    lv_obj_center(col);

    /* A página principal é só a frase — sem as primitivas do KIT (isso é a
     * logo do sistema, não do Tarot). */
    add_wrap(col, "CONCENTRE-SE NA SUA PERGUNTA",
             KIT_COLOR_TEXT, &kit_sans_28, LV_TEXT_ALIGN_CENTER);
    add_wrap(col, "chacoalhe o KIT ou toque na tela",
             KIT_COLOR_TEXT_MUTED, &kit_sans_22, LV_TEXT_ALIGN_CENTER);
}

static void build_menu_tile_settings(lv_obj_t *tile)
{
    lv_obj_t *col = scroll_col(tile, 22, 28);

    add_label(col, "AJUSTES", KIT_COLOR_TEXT, &kit_mono_26, 3);

    chip_pair(col, "TIRAGEM", "UMA CARTA", "TR\xC3\x8AS CARTAS",
              s_pick_count == 1, pick_chip_cb,
              (void *)(intptr_t)1, (void *)(intptr_t)3);

    chip_pair(col, "BARALHO", "S\xC3\x93 MAIORES", "78 CARTAS",
              s_major_only, major_chip_cb,
              (void *)(intptr_t)1, (void *)(intptr_t)0);

    chip_pair(col, "CARTAS INVERTIDAS", "SIM", "N\xC3\x83O",
              s_reversed_on, rev_chip_cb,
              (void *)(intptr_t)1, (void *)(intptr_t)0);

    hairline(col);

    add_label(col, "v1.1.0", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
}

/* Mostra ou esconde o botão-pílula: só faz sentido na PRINCIPAL; nos Ajustes
 * ele não tem função e some. */
static void menu_sync_footer(void)
{
    if (!s_footer || s_state != ST_MENU || !s_tv) return;
    bool on_focus = (lv_tileview_get_tile_active(s_tv) == s_tiles[T_TILE_FOCUS]);
    if (on_focus) lv_obj_remove_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
}

static void build_menu_at(int start_tile)
{
    s_state = ST_MENU;
    lv_obj_t *scr = new_screen();

    build_titlebar(scr, "TAROT", 2);

    /* Ordem: [0] AJUSTES à esquerda · [1] PRINCIPAL à direita. Altura cheia:
     * o rodapé só aparece na PRINCIPAL e flutua sobre a base; os Ajustes usam
     * a tela toda e rolam. */
    build_tileview(scr, 2, T_PAGE_H_FULL);
    build_menu_tile_settings(s_tiles[0]);
    build_menu_tile_focus(s_tiles[1]);

    s_footer = build_footer(scr, tirar_label(), footer_draw_cb);

    lv_obj_update_layout(scr);
    lv_tileview_set_tile_by_index(s_tv, (uint32_t)start_tile, 0, LV_ANIM_OFF);
    sync_dots();
    menu_sync_footer();
    show_screen(scr);
}

static void build_menu(void)
{
    build_menu_at(T_TILE_FOCUS);
}

/* --------------------------------------------------------------------------
 * TELA: SORTEIO — embaralho místico
 * ========================================================================== */
static void shuffle_settle(void)
{
    kill_anim();
    int deck_n = s_major_only ? T_MAJOR_COUNT : tarot_deck_count;
    s_result_n = tarot_draw(s_picks, s_pick_count, s_reversed_on,
                            deck_n, rng_range);
    bool any_rev = false;
    for (int i = 0; i < s_result_n; i++) if (s_picks[i].reversed) any_rev = true;
    sfx_reveal(any_rev);
    build_result();
}

static void shuffle_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_anim_tick++;
    if (s_anim_tick < T_SHUFFLE_TICKS) {
        int lim = s_major_only ? T_MAJOR_COUNT : tarot_deck_count;
        int r = rng_range(0, lim - 1);
        if (s_shuf_lbl) lv_label_set_text(s_shuf_lbl, tr(tarot_deck[r].name));
        sfx_shuffle_note(s_anim_tick);
        uint32_t p = T_SHUFFLE_MS_MIN + (uint32_t)(T_SHUFFLE_MS_MAX - T_SHUFFLE_MS_MIN)
                     * s_anim_tick / (T_SHUFFLE_TICKS - 1);
        lv_timer_set_period(s_anim, p);
        return;
    }
    shuffle_settle();
}

static void build_shuffle(void)
{
    s_state = ST_SHUFFLE;
    lv_obj_t *scr = new_screen();

    lv_obj_t *col = plain_box(scr);
    lv_obj_set_size(col, T_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 18, 0);
    lv_obj_center(col);

    add_label(col, "EMBARALHANDO", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 4);
    s_shuf_lbl = add_wrap(col, "\xC2\xB7 \xC2\xB7 \xC2\xB7", KIT_COLOR_TEXT,
                          &kit_sans_28, LV_TEXT_ALIGN_CENTER);
    add_wrap(col, "concentre-se na sua pergunta",
             KIT_COLOR_TEXT_MUTED, &kit_sans_22, LV_TEXT_ALIGN_CENTER);

    s_anim_tick = 0;
    s_anim = lv_timer_create(shuffle_tick_cb, T_SHUFFLE_MS_MIN, NULL);

    show_screen(scr);
}

/* --------------------------------------------------------------------------
 * TELA: RESULTADO
 * ========================================================================== */
static void again_cb(lv_event_t *e)
{
    (void)e;
    sfx_tap();
    build_shuffle();
}

/* --- 1 carta: [0] A CARTA · [1] O QUE É · [2] NA LEITURA --- */
static void result_single_tile(lv_obj_t *tile, int idx)
{
    const tarot_pick_t *p = &s_picks[0];
    const tarot_card_t *c = &tarot_deck[p->index];

    if (idx == 0) {
        lv_obj_t *col = scroll_col(tile, 28, 20);
        card_head(col, p);
        swipe_hint(col, "deslize para a leitura \xE2\x86\x92");
        return;
    }

    lv_obj_t *col = scroll_col(tile, 22, 22);
    if (idx == 1) {
        add_label(col, "O QUE \xC3\x89", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 3);
        add_wrap(col, c->about, KIT_COLOR_TEXT, &kit_sans_22, LV_TEXT_ALIGN_LEFT);
    } else {
        add_label(col, p->reversed ? "NA LEITURA \xC2\xB7 INVERTIDA" : "NA LEITURA",
                  KIT_COLOR_TEXT_MUTED, &kit_mono_20, 3);
        add_wrap(col, p->reversed ? c->reversed : c->upright,
                 KIT_COLOR_TEXT, &kit_sans_22, LV_TEXT_ALIGN_LEFT);
    }
}

/* --- 3 cartas: [0..2] posições · [3] RESUMO --- */
static void result_triple_tile(lv_obj_t *tile, int idx)
{
    if (idx < 3) {
        const tarot_pick_t *p = &s_picks[idx];
        const tarot_card_t *c = &tarot_deck[p->index];
        const char *lens = (idx == 0) ? c->as_past
                         : (idx == 1) ? c->as_present : c->as_future;

        lv_obj_t *col = scroll_col(tile, 20, 24);

        char head[28];
        snprintf(head, sizeof head, "%s \xC2\xB7 %d/3", POS_TITLE[idx], idx + 1);
        add_label(col, head, T_ACCENT, &kit_mono_20, 3);

        card_head(col, p);
        hairline(col);
        add_wrap(col, c->about, KIT_COLOR_TEXT_MUTED, &kit_sans_22, LV_TEXT_ALIGN_LEFT);
        hairline(col);
        add_wrap(col, p->reversed ? c->reversed : c->upright,
                 KIT_COLOR_TEXT, &kit_sans_22, LV_TEXT_ALIGN_LEFT);
        hairline(col);
        add_wrap(col, lens, T_ACCENT, &kit_sans_22, LV_TEXT_ALIGN_LEFT);
        swipe_hint(col, idx < 2 ? "deslize \xE2\x86\x92" : "resumo \xE2\x86\x92");
        return;
    }

    lv_obj_t *col = scroll_col(tile, 24, 20);
    add_label(col, "RESUMO", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 3);
    for (int i = 0; i < 3; i++) {
        const tarot_card_t *c = &tarot_deck[s_picks[i].index];
        add_label(col, POS_TITLE[i], KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
        char line[64];
        snprintf(line, sizeof line, "%s%s", c->name,
                 s_picks[i].reversed ? "  (inv.)" : "");
        add_wrap(col, line, KIT_COLOR_TEXT, &kit_sans_28, LV_TEXT_ALIGN_CENTER);
    }
}

static void build_result(void)
{
    s_state = ST_RESULT;
    lv_obj_t *scr = new_screen();

    bool triple = (s_result_n == 3);
    int n = triple ? 4 : 3;

    build_titlebar(scr, triple ? "TIRAGEM" : "A CARTA", n);

    build_tileview(scr, n, T_PAGE_H);   /* telas de resultado têm rodapé fixo */
    for (int i = 0; i < n; i++) {
        if (triple) result_triple_tile(s_tiles[i], i);
        else        result_single_tile(s_tiles[i], i);
    }

    build_footer(scr, triple ? "NOVA TIRAGEM" : "TIRAR OUTRA", again_cb);

    lv_obj_update_layout(scr);
    sync_dots();
    show_screen(scr);
}

/* --------------------------------------------------------------------------
 * Ciclo de vida
 * -------------------------------------------------------------------------- */
kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    printf("[Tarot] tool_init (id=%s)\n", ctx->tool_id);

    load_config();

    if (s_api->imu)
        s_api->imu->register_shake_callback(on_shake, NULL);

    build_menu();
    return KIT_OK;
}

void tool_destroy(void)
{
    printf("[Tarot] tool_destroy\n");
    kill_anim();
    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);
    s_screen = NULL;
    s_tv = NULL;
    s_shuf_lbl = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo: só exercita a lógica de sorteio */

kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    tarot_pick_t p[TAROT_MAX_PICKS];
    int n = tarot_draw(p, 3, true, tarot_deck_count, NULL);
    printf("[Tarot stub] tarot_draw devolveu %d (esperado 0 sem rng)\n", n);
    return KIT_OK;
}

void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
