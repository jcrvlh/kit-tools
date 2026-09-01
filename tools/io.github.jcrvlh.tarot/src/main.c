/**
 * @file main.c
 * @brief Tarot — Tool de tiragem e interpretação de tarot para o KIT.
 *
 * Consulta enxuta: sorteia, mostra nome + significado em texto, e sai da
 * frente. Sem arte de carta. Duas tiragens: uma carta e três cartas
 * (Passado / Presente / Futuro). Cartas invertidas opcionais.
 *
 * Linguagem visual "Brutalist Bauhaus" do KIT (kit_theme.h / kit_fonts.h),
 * mesmos padrões da Tool Fora. Accent: KIT_COLOR_BLUE.
 *
 * A lógica de sorteio é pura e vive em tarot_draw.c; os dados das cartas em
 * tarot_deck.c. Este arquivo cuida da UI e do ciclo de vida.
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
#define T_SCREEN_W   368
#define T_SCREEN_H   448
#define T_PAD        16
#define T_CONTENT    (T_SCREEN_W - 2 * T_PAD)   /* 336 */
#define T_BTN_H      76
#define T_BTN_MARGIN 18
#define T_ACCENT     KIT_COLOR_BLUE

/* Embaralho */
#define T_SHUFFLE_TICKS   14
#define T_SHUFFLE_MS_MIN  45
#define T_SHUFFLE_MS_MAX  150

#ifndef KIT_SDK_STUBS

/* --------------------------------------------------------------------------
 * Estado
 * -------------------------------------------------------------------------- */
static const kit_api_table_t *s_api = NULL;
static lv_obj_t *s_screen = NULL;

static lv_timer_t *s_anim_timer = NULL;
static int         s_anim_tick = 0;

static bool s_reversed_on = true;     /* config: cartas invertidas */

/* Tiragem corrente */
static tarot_pick_t s_picks[TAROT_MAX_PICKS];
static int          s_pick_count = 0;   /* 1 = uma carta, 3 = três cartas */

/* Gatilho de shake só vale na tela de concentração */
static bool s_shake_armed = false;

static const char *const POS_TITLE[3] = { "PASSADO", "PRESENTE", "FUTURO" };

/* Forward decls */
static void build_menu(void);
static void build_focus(void);
static void build_shuffle(void);
static void build_result_single(void);
static void build_position(int pos);
static void build_summary(void);
static void build_settings(void);

/* --------------------------------------------------------------------------
 * RNG: adapta ctx->api->random->range à assinatura tarot_rng_fn
 * -------------------------------------------------------------------------- */
static int rng_range(int lo, int hi)
{
    if (s_api && s_api->random) return (int)s_api->random->range(lo, hi);
    if (hi <= lo) return lo;
    return lo + (int)(rand() % (unsigned)(hi - lo + 1));
}

/* --------------------------------------------------------------------------
 * Áudio
 * -------------------------------------------------------------------------- */
static void sfx_tap(void)     { if (s_api && s_api->audio) s_api->audio->beep(1800, 25); }
static void sfx_tick(void)    { if (s_api && s_api->audio) s_api->audio->beep(1200, 18); }
static void sfx_reveal(void)  { if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_REVEAL); }

/* --------------------------------------------------------------------------
 * Helpers LVGL (espelho do estilo das Tools internas)
 * -------------------------------------------------------------------------- */
static uint32_t on_accent(void)
{
    return (T_ACCENT == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *txt, uint32_t color,
                           const lv_font_t *font, int letter_space)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    if (letter_space) lv_obj_set_style_text_letter_space(l, letter_space, 0);
    return l;
}

static lv_obj_t *add_wrap(lv_obj_t *parent, const char *txt, uint32_t color,
                          const lv_font_t *font, int letter_space, lv_text_align_t align)
{
    lv_obj_t *l = add_label(parent, txt, color, font, letter_space);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, T_CONTENT);
    lv_obj_set_style_text_align(l, align, 0);
    return l;
}

static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/** Coluna rolável que ocupa a tela toda (para telas de resultado). */
static lv_obj_t *scroll_body(lv_obj_t *scr, int pad_top, int pad_bottom)
{
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, T_SCREEN_W, T_SCREEN_H);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_pad_left(cont, T_PAD, 0);
    lv_obj_set_style_pad_right(cont, T_PAD, 0);
    lv_obj_set_style_pad_top(cont, pad_top, 0);
    lv_obj_set_style_pad_bottom(cont, pad_bottom, 0);
    lv_obj_set_style_pad_row(cont, 10, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
    return cont;
}

static void hairline(lv_obj_t *parent)
{
    lv_obj_t *sep = plain_box(parent);
    lv_obj_set_size(sep, T_CONTENT, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
}

/** Botão pill de ação, ancorado no rodapé da tela. */
static lv_obj_t *pill_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, T_CONTENT, T_BTN_H);
    lv_obj_set_style_radius(b, T_BTN_H / 2, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(T_ACCENT), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -T_BTN_MARGIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = add_label(b, text, on_accent(), &kit_mono_26, 3);
    lv_obj_center(l);
    return b;
}

/** Linha/chip de superfície (menu, ajustes, botões secundários). */
static lv_obj_t *surface_row(lv_obj_t *parent, int h, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, T_CONTENT, h);
    lv_obj_set_style_bg_color(r, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 16, 0);
    lv_obj_set_style_pad_all(r, 14, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(r, 6);
    if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
    return r;
}

/* --------------------------------------------------------------------------
 * Ciclo de telas
 * -------------------------------------------------------------------------- */
static void kill_anim(void)
{
    if (s_anim_timer) { lv_timer_delete(s_anim_timer); s_anim_timer = NULL; }
    s_anim_tick = 0;
}

static lv_obj_t *new_screen(void)
{
    kill_anim();
    s_shake_armed = false;
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
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
 * Persistência
 * -------------------------------------------------------------------------- */
static void load_config(void)
{
    if (!s_api || !s_api->storage) return;
    int32_t v;
    if (s_api->storage->get_i32("reversed_on", &v) == KIT_OK)
        s_reversed_on = (v != 0);
}

static void save_config(void)
{
    if (!s_api || !s_api->storage) return;
    s_api->storage->set_i32("reversed_on", s_reversed_on ? 1 : 0);
    s_api->storage->set_str("deck", "kit");
}

/* --------------------------------------------------------------------------
 * Render de uma carta (bloco reutilizado por 1-carta e por posição)
 * -------------------------------------------------------------------------- */
static void render_card(lv_obj_t *cont, const tarot_pick_t *pick, const char *lens)
{
    const tarot_card_t *c = &tarot_deck[pick->index];

    lv_obj_t *name = add_wrap(cont, c->name, KIT_COLOR_TEXT,
                              &kit_display_44, 1, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);

    add_wrap(cont, c->arcana, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1, LV_TEXT_ALIGN_CENTER);

    if (pick->reversed)
        add_wrap(cont, "INVERTIDA", T_ACCENT, &kit_mono_20, 2, LV_TEXT_ALIGN_CENTER);

    add_wrap(cont, c->about, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0, LV_TEXT_ALIGN_CENTER);

    hairline(cont);

    add_wrap(cont, c->keywords, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1, LV_TEXT_ALIGN_CENTER);
    add_wrap(cont, pick->reversed ? c->reversed : c->upright,
             KIT_COLOR_TEXT, &kit_mono_16, 0, LV_TEXT_ALIGN_CENTER);

    if (lens) {
        hairline(cont);
        add_wrap(cont, lens, KIT_COLOR_TEXT, &kit_mono_16, 0, LV_TEXT_ALIGN_CENTER);
    }
}

/* ==========================================================================
 * TELA: MENU
 * ========================================================================== */
static void menu_single_cb(lv_event_t *e) { (void)e; sfx_tap(); s_pick_count = 1; build_focus(); }
static void menu_triple_cb(lv_event_t *e) { (void)e; sfx_tap(); s_pick_count = 3; build_focus(); }
static void menu_settings_cb(lv_event_t *e){ (void)e; sfx_tap(); build_settings(); }
static void menu_exit_cb(lv_event_t *e)   { (void)e; if (s_api && s_api->system) s_api->system->exit(); }

static void add_menu_option(lv_obj_t *parent, const char *title, const char *sub,
                            lv_event_cb_t cb)
{
    lv_obj_t *r = surface_row(parent, 88, cb, NULL);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(r, 4, 0);
    add_label(r, title, KIT_COLOR_TEXT, &kit_mono_26, 2);
    add_label(r, sub, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0);
}

static void build_menu(void)
{
    lv_obj_t *scr = new_screen();

    lv_obj_t *cont = scroll_body(scr, 26, 20);

    lv_obj_t *title = add_label(cont, "TAROT", T_ACCENT, &kit_display_44, 4);
    lv_obj_set_width(title, T_CONTENT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *sub = add_label(cont, "LEITURA & REFLEX\xC3\x83O", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_set_width(sub, T_CONTENT);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_pad_row(cont, 14, 0);

    add_menu_option(cont, "UMA CARTA", "uma pergunta, uma resposta", menu_single_cb);
    add_menu_option(cont, "TR\xC3\x8AS CARTAS", "passado \xC2\xB7 presente \xC2\xB7 futuro", menu_triple_cb);

    hairline(cont);

    lv_obj_t *row = plain_box(cont);
    lv_obj_set_size(row, T_CONTENT, 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cfg = surface_row(row, 56, menu_settings_cb, NULL);
    lv_obj_set_width(cfg, (T_CONTENT - 10) / 2);
    lv_obj_set_style_pad_all(cfg, 0, 0);
    lv_obj_center(add_label(cfg, "AJUSTES", KIT_COLOR_TEXT, &kit_mono_16, 1));

    lv_obj_t *ex = surface_row(row, 56, menu_exit_cb, NULL);
    lv_obj_set_width(ex, (T_CONTENT - 10) / 2);
    lv_obj_set_style_pad_all(ex, 0, 0);
    lv_obj_center(add_label(ex, "SAIR", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1));

    show_screen(scr);
}

/* ==========================================================================
 * TELA: CONCENTRA\xC3\x87\xC3\x83O  (shake OU toque para tirar)
 * ========================================================================== */
static void focus_draw_cb(lv_event_t *e)
{
    (void)e;
    if (!s_shake_armed) return;      /* evita duplo-disparo */
    s_shake_armed = false;
    sfx_tap();
    build_shuffle();
}

static void on_shake(void *ud)
{
    (void)ud;
    if (s_shake_armed) focus_draw_cb(NULL);
}

static void build_focus(void)
{
    lv_obj_t *scr = new_screen();

    lv_obj_t *col = plain_box(scr);
    lv_obj_set_size(col, T_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_center(col);

    add_label(col, "CONCENTRE-SE", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    lv_obj_t *q = add_wrap(col, "NA SUA PERGUNTA", KIT_COLOR_TEXT, &kit_mono_26, 2,
                           LV_TEXT_ALIGN_CENTER);
    (void)q;

    add_label(col, KIT_ICON_TRIANGLE "  " KIT_ICON_CIRCLE "  " KIT_ICON_SQUARE,
              KIT_COLOR_TEXT_MUTED, &kit_mono_26, 4);

    add_wrap(col, s_pick_count == 3
                 ? "chacoalhe o KIT ou toque\npara tirar as tr\xC3\xAas"
                 : "chacoalhe o KIT ou toque\npara tirar a carta",
             KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0, LV_TEXT_ALIGN_CENTER);

    pill_btn(scr, s_pick_count == 3 ? "TIRAR AS TR\xC3\x8AS" : "TIRAR CARTA",
             focus_draw_cb, NULL);

    /* toque em qualquer lugar da tela também tira */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, focus_draw_cb, LV_EVENT_CLICKED, NULL);

    s_shake_armed = true;
    show_screen(scr);
}

/* ==========================================================================
 * TELA: EMBARALHO  (flicker desacelerando, depois assenta)
 * ========================================================================== */
static lv_obj_t *s_shuffle_lbl = NULL;

static void shuffle_settle(void)
{
    kill_anim();
    /* sorteia de fato */
    s_pick_count = tarot_draw(s_picks, s_pick_count, s_reversed_on,
                              tarot_deck_count, rng_range);
    sfx_reveal();
    if (s_pick_count == 1) build_result_single();
    else                   build_position(0);
}

static void shuffle_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_anim_tick++;
    if (s_anim_tick < T_SHUFFLE_TICKS) {
        int r = rng_range(0, tarot_deck_count - 1);
        lv_label_set_text(s_shuffle_lbl, tarot_deck[r].name);
        sfx_tick();
        uint32_t p = T_SHUFFLE_MS_MIN +
            (uint32_t)(T_SHUFFLE_MS_MAX - T_SHUFFLE_MS_MIN) * s_anim_tick / (T_SHUFFLE_TICKS - 1);
        lv_timer_set_period(s_anim_timer, p);
        return;
    }
    shuffle_settle();
}

static void build_shuffle(void)
{
    lv_obj_t *scr = new_screen();

    lv_obj_t *col = plain_box(scr);
    lv_obj_set_size(col, T_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 16, 0);
    lv_obj_center(col);

    add_label(col, "EMBARALHANDO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);
    s_shuffle_lbl = add_wrap(col, "\xC2\xB7\xC2\xB7\xC2\xB7", KIT_COLOR_TEXT,
                             &kit_mono_26, 1, LV_TEXT_ALIGN_CENTER);

    s_anim_tick = 0;
    s_anim_timer = lv_timer_create(shuffle_tick_cb, T_SHUFFLE_MS_MIN, NULL);

    show_screen(scr);
}

/* ==========================================================================
 * TELA: RESULTADO — UMA CARTA
 * ========================================================================== */
static void again_cb(lv_event_t *e) { (void)e; sfx_tap(); build_focus(); }
static void back_menu_cb(lv_event_t *e) { (void)e; sfx_tap(); build_menu(); }

static void build_result_single(void)
{
    lv_obj_t *scr = new_screen();
    lv_obj_t *cont = scroll_body(scr, 24, 24);

    add_label(cont, "A CARTA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);
    render_card(cont, &s_picks[0], NULL);

    hairline(cont);

    lv_obj_t *again = surface_row(cont, T_BTN_H, again_cb, NULL);
    lv_obj_set_style_bg_color(again, lv_color_hex(T_ACCENT), 0);
    lv_obj_set_style_radius(again, T_BTN_H / 2, 0);
    lv_obj_set_style_pad_all(again, 0, 0);
    lv_obj_center(add_label(again, "TIRAR OUTRA", on_accent(), &kit_mono_20, 2));

    lv_obj_t *back = surface_row(cont, 56, back_menu_cb, NULL);
    lv_obj_set_style_radius(back, 28, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_center(add_label(back, "VOLTAR", KIT_COLOR_TEXT, &kit_mono_16, 2));

    show_screen(scr);
}

/* ==========================================================================
 * TELA: RESULTADO — TR\xC3\x8AS CARTAS (posi\xC3\xA7\xC3\xA3o a posi\xC3\xA7\xC3\xA3o)
 * ========================================================================== */
static void position_next_cb(lv_event_t *e)
{
    int pos = (int)(intptr_t)lv_event_get_user_data(e);
    sfx_tap();
    if (pos + 1 < s_pick_count) build_position(pos + 1);
    else                        build_summary();
}

static void build_position(int pos)
{
    lv_obj_t *scr = new_screen();
    lv_obj_t *cont = scroll_body(scr, 24, 24);

    char head[24];
    snprintf(head, sizeof head, "%s  \xC2\xB7  %d/%d", POS_TITLE[pos], pos + 1, s_pick_count);
    add_label(cont, head, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);

    const tarot_card_t *c = &tarot_deck[s_picks[pos].index];
    const char *lens = (pos == 0) ? c->as_past : (pos == 1) ? c->as_present : c->as_future;
    render_card(cont, &s_picks[pos], lens);

    hairline(cont);

    lv_obj_t *next = surface_row(cont, T_BTN_H, position_next_cb, (void *)(intptr_t)pos);
    lv_obj_set_style_bg_color(next, lv_color_hex(T_ACCENT), 0);
    lv_obj_set_style_radius(next, T_BTN_H / 2, 0);
    lv_obj_set_style_pad_all(next, 0, 0);
    const char *label = (pos + 1 < s_pick_count)
                        ? (pos == 0 ? "PRESENTE \xE2\x86\x92" : "FUTURO \xE2\x86\x92")
                        : "RESULTADO \xE2\x86\x92";
    lv_obj_center(add_label(next, label, on_accent(), &kit_mono_20, 2));

    show_screen(scr);
}

/* ==========================================================================
 * TELA: RESUMO DA TIRAGEM DE TR\xC3\x8AS
 * ========================================================================== */
static void new_spread_cb(lv_event_t *e) { (void)e; sfx_tap(); build_focus(); }

static void build_summary(void)
{
    lv_obj_t *scr = new_screen();
    lv_obj_t *cont = scroll_body(scr, 28, 24);

    add_label(cont, "RESULTADO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);

    for (int i = 0; i < s_pick_count; i++) {
        const tarot_card_t *c = &tarot_deck[s_picks[i].index];
        add_label(cont, POS_TITLE[i], KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
        char line[48];
        snprintf(line, sizeof line, "%s%s", c->name, s_picks[i].reversed ? " (inv.)" : "");
        add_wrap(cont, line, KIT_COLOR_TEXT, &kit_mono_20, 1, LV_TEXT_ALIGN_CENTER);
    }

    hairline(cont);

    lv_obj_t *ns = surface_row(cont, T_BTN_H, new_spread_cb, NULL);
    lv_obj_set_style_bg_color(ns, lv_color_hex(T_ACCENT), 0);
    lv_obj_set_style_radius(ns, T_BTN_H / 2, 0);
    lv_obj_set_style_pad_all(ns, 0, 0);
    lv_obj_center(add_label(ns, "NOVA TIRAGEM", on_accent(), &kit_mono_20, 2));

    lv_obj_t *back = surface_row(cont, 56, back_menu_cb, NULL);
    lv_obj_set_style_radius(back, 28, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_center(add_label(back, "VOLTAR", KIT_COLOR_TEXT, &kit_mono_16, 2));

    show_screen(scr);
}

/* ==========================================================================
 * TELA: AJUSTES
 * ========================================================================== */
static void toggle_reversed_cb(lv_event_t *e)
{
    s_reversed_on = (bool)(intptr_t)lv_event_get_user_data(e);
    sfx_tap();
    save_config();
    build_settings();
}

static void settings_back_cb(lv_event_t *e) { (void)e; sfx_tap(); save_config(); build_menu(); }

static void build_settings(void)
{
    lv_obj_t *scr = new_screen();
    lv_obj_t *cont = scroll_body(scr, 28, 24);

    add_label(cont, "AJUSTES", KIT_COLOR_TEXT, &kit_display_44, 2);

    /* --- Cartas invertidas --- */
    add_label(cont, "CARTAS INVERTIDAS", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);

    lv_obj_t *rowr = plain_box(cont);
    lv_obj_set_size(rowr, T_CONTENT, 54);
    lv_obj_set_flex_flow(rowr, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(rowr, 8, 0);

    static const char *OPT[2] = { "ATIVADO", "DESATIVADO" };
    for (int i = 0; i < 2; i++) {
        bool sel = (s_reversed_on == (i == 0));
        lv_obj_t *chip = lv_obj_create(rowr);
        lv_obj_set_height(chip, 54);
        lv_obj_set_flex_grow(chip, 1);
        lv_obj_set_style_bg_color(chip, lv_color_hex(sel ? T_ACCENT : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_radius(chip, 15, 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chip, toggle_reversed_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(i == 0));
        lv_obj_center(add_label(chip, OPT[i],
                                sel ? on_accent() : KIT_COLOR_TEXT, &kit_mono_16, 1));
    }

    /* --- Baralho (1 item na V1) --- */
    add_label(cont, "BARALHO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_t *deck = surface_row(cont, 54, NULL, NULL);
    lv_obj_set_flex_flow(deck, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(deck, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    add_label(deck, "KIT Tarot", KIT_COLOR_TEXT, &kit_mono_16, 0);
    add_label(deck, KIT_ICON_CHECK, T_ACCENT, &kit_mono_16, 0);

    hairline(cont);

    add_wrap(cont, "Tarot \xC3\xA9 entretenimento e reflex\xC3\xA3o \xE2\x80\x94 n\xC3\xA3o previs\xC3\xA3o.",
             KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0, LV_TEXT_ALIGN_CENTER);
    add_label(cont, "v1.0.0", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);

    hairline(cont);

    lv_obj_t *back = surface_row(cont, 56, settings_back_cb, NULL);
    lv_obj_set_style_radius(back, 28, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_center(add_label(back, "VOLTAR", KIT_COLOR_TEXT, &kit_mono_16, 2));

    show_screen(scr);
}

/* ==========================================================================
 * Ciclo de vida
 * ========================================================================== */
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
    s_screen = NULL;
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
