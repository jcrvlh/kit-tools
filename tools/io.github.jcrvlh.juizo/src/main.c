/**
 * @file main.c
 * @brief Juízo — a Bola 8 do KIT: toque, pergunte e receba um veredito seco.
 *
 * Sem chatbot, sem interpretar a pergunta — a resposta é sorteada de um
 * banco fixo de 23 frases brasileiras, curtas e levemente debochadas. A
 * mecânica é objeto → ritual → veredito: um orbe no centro da tela balança
 * enquanto o Juízo "pensa", e a resposta trava grande, dominando a tela.
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 * Toda a UI fica atrás de #ifndef KIT_SDK_STUBS — ver tool_lvgl_runtime.md.
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KIT_SDK_STUBS

#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define J_PAD        16
#define J_CONTENT    (KIT_DISPLAY_WIDTH - 2 * J_PAD)          // 336
#define J_TITLEBAR   88
#define J_FOOT       112
#define J_CHIP       56
#define J_GO_H       76
#define J_GO_MARGIN  18
#define J_PAGE_H     (KIT_DISPLAY_HEIGHT - J_TITLEBAR - J_FOOT)

#define J_ORB_D      84   // diâmetro do orbe

// Sorteio: flicker desacelerando (ease-out), o "pensar" antes do veredito.
#define J_DRAW_TICKS   16
#define J_DRAW_MS_MIN  40
#define J_DRAW_MS_MAX  130

// O orbe balança de leve durante o "pensar" — sem float, só inteiro.
static const int8_t ORB_WOBBLE[] = { 0, -7, 7, -6, 6, -5, 5, -4, 4, -3, 3, -2, 2, -1, 1, 0 };
#define ORB_WOBBLE_N ((int)(sizeof(ORB_WOBBLE) / sizeof(ORB_WOBBLE[0])))

// Banco de respostas — brasileiro, seco, sem "nordestinês" forçado.
static const char *const ANSWERS[] = {
    "Sim",
    "Não",
    "Pode deixar",
    "Pois é",
    "Sei não",
    "Aí complica",
    "Não se anime",
    "Vá na fé",
    "Deixe disso",
    "Juízo",
    "Vish",
    "Talvez",
    "Com certeza",
    "Nem pensar",
    "Pode ser",
    "Vai saber",
    "Boa sorte",
    "Não conte com isso",
    "Agora é tarde",
    "Rapaz...",
    "É melhor não",
    "Vai dar certo",
    "Vai dar ruim",
};
#define ANSWERS_N ((int)(sizeof(ANSWERS) / sizeof(ANSWERS[0])))

// --- estado --------------------------------------------------------------
static const kit_api_table_t *s_api = NULL;

static uint32_t s_accent  = KIT_COLOR_RED;
static bool     s_asking  = false;
static bool     s_asked   = false;    // já houve ao menos uma pergunta
static int      s_last    = -1;
static int      s_target  = -1;
static int      s_tick    = 0;
static lv_timer_t *s_timer = NULL;

// --- objetos LVGL ---------------------------------------------------------
static lv_obj_t *s_screen    = NULL;
static lv_obj_t *s_orb_wrap  = NULL;
static lv_obj_t *s_orb       = NULL;
static lv_obj_t *s_answer    = NULL;
static lv_obj_t *s_status    = NULL;
static lv_obj_t *s_go_btn    = NULL;

// --- helpers ---------------------------------------------------------

static inline uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static int rnd_index(void)
{
    int i = 0;
    int guard = 16;
    do {
        if (s_api && s_api->random)
            i = (int)s_api->random->range(0, ANSWERS_N - 1);
    } while (i == s_last && ANSWERS_N > 1 && --guard > 0);
    return i;
}

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

// --- pergunta / veredito ------------------------------------------------

static void draw_tick_cb(lv_timer_t *t);

static void do_ask(void)
{
    if (s_asking || !s_answer) return;
    s_asking = true;
    s_target = rnd_index();

    if (!s_asked) {
        s_asked = true;
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(s_answer, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_opa(s_go_btn, LV_OPA_60, 0);   // "ocupado"

    s_tick = 0;
    if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_ROULETTE);
    s_timer = lv_timer_create(draw_tick_cb, J_DRAW_MS_MIN, NULL);
}

static void draw_tick_cb(lv_timer_t *t)
{
    (void)t;

    int wobble_i = s_tick % ORB_WOBBLE_N;
    lv_obj_set_style_translate_x(s_orb_wrap, ORB_WOBBLE[wobble_i], 0);

    s_tick++;

    if (s_tick < J_DRAW_TICKS) {
        int i = 0;
        if (s_api && s_api->random)
            i = (int)s_api->random->range(0, ANSWERS_N - 1);
        lv_label_set_text(s_answer, ANSWERS[i]);

        uint32_t p = J_DRAW_MS_MIN +
            (uint32_t)(J_DRAW_MS_MAX - J_DRAW_MS_MIN) * s_tick / (J_DRAW_TICKS - 1);
        lv_timer_set_period(s_timer, p);
        return;
    }

    // trava na resposta sorteada
    s_last = s_target;
    lv_label_set_text(s_answer, ANSWERS[s_target]);
    lv_obj_set_style_text_color(s_answer, lv_color_hex(s_accent), 0);
    lv_label_set_text(s_status, "TOQUE PRA PERGUNTAR DE NOVO");
    lv_obj_set_style_translate_x(s_orb_wrap, 0, 0);

    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_asking = false;
    lv_obj_set_style_opa(s_go_btn, LV_OPA_COVER, 0);
    if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_REVEAL);
}

static void on_shake(void *user_data)
{
    (void)user_data;
    do_ask();
}

// --- callbacks ------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_api && s_api->system) s_api->system->exit();
}

static void ask_cb(lv_event_t *e)
{
    (void)e;
    do_ask();
}

// --- construção da tela -------------------------------------------

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, J_CHIP, J_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, J_PAD, 16);

    lv_obj_t *g = add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(g);

    lv_obj_t *title = add_label(s_screen, "JUÍZO", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, J_PAD + J_CHIP + 12, 30);
}

static void build_stage(void)
{
    lv_obj_t *stage = lv_obj_create(s_screen);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, KIT_DISPLAY_WIDTH, J_PAGE_H);
    lv_obj_set_pos(stage, 0, J_TITLEBAR);
    lv_obj_remove_flag(stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stage, ask_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(stage);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 18, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_center(col);

    // Orbe: o "objeto" do Juízo. Balança (translate_x) enquanto pensa.
    s_orb_wrap = plain_box(col);
    lv_obj_set_size(s_orb_wrap, J_ORB_D, J_ORB_D);
    lv_obj_add_flag(s_orb_wrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    s_orb = lv_obj_create(s_orb_wrap);
    lv_obj_remove_style_all(s_orb);
    lv_obj_set_size(s_orb, J_ORB_D, J_ORB_D);
    lv_obj_set_style_radius(s_orb, J_ORB_D / 2, 0);
    lv_obj_set_style_bg_color(s_orb, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_orb, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_orb, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mark = add_label(s_orb, "?", on_accent(), &kit_display_44, 0);
    lv_obj_center(mark);

    s_answer = add_label(col, "Pergunte.", KIT_COLOR_TEXT_MUTED, &kit_sans_28, 0);
    lv_label_set_long_mode(s_answer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_answer, J_CONTENT);
    lv_obj_set_style_text_align(s_answer, LV_TEXT_ALIGN_CENTER, 0);

    s_status = add_label(col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
}

static void build_footer(void)
{
    s_go_btn = lv_obj_create(s_screen);
    lv_obj_set_size(s_go_btn, J_CONTENT, J_GO_H);
    lv_obj_set_style_radius(s_go_btn, J_GO_H / 2, 0);
    lv_obj_set_style_border_width(s_go_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_go_btn, 0, 0);
    lv_obj_set_style_pad_all(s_go_btn, 0, 0);
    lv_obj_set_style_bg_color(s_go_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_go_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_go_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_go_btn, 8);
    lv_obj_align(s_go_btn, LV_ALIGN_BOTTOM_MID, 0, -J_GO_MARGIN);
    lv_obj_add_event_cb(s_go_btn, ask_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l = add_label(s_go_btn, "PERGUNTAR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(l);
}

// --- ciclo de vida da Tool ---------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    printf("[Juízo] tool_init\n");
    s_api = ctx->api;

    s_accent = KIT_COLOR_RED;
    s_asking = false;
    s_asked  = false;
    s_last   = -1;
    s_target = -1;
    s_tick   = 0;

    if (s_api->imu)
        s_api->imu->register_shake_callback(on_shake, NULL);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_stage();
    build_footer();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Juízo] tool_destroy\n");
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);
    s_asking = false;

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_orb_wrap = s_orb = s_answer = s_status = s_go_btn = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo (CI / teste de lógica, sem UI) */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Juízo stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
