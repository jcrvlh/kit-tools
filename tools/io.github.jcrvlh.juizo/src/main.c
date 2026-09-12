/**
 * @file main.c
 * @brief Juízo — o veredito do KIT: toque, pergunte e receba uma resposta seca.
 *
 * Sem chatbot, sem interpretar a pergunta — a resposta é sorteada de um
 * banco fixo de 23 frases brasileiras, curtas e levemente debochadas. O
 * objeto é uma pedra quadrada e escura (não uma bola — de propósito, pra não
 * copiar a Bola 8), anel vermelho, com uma janela circular no centro — o
 * contraste quadrado/círculo é a própria gramática Bauhaus. Ao tocar, a
 * pedra dá 3 saltos decrescentes (thud a cada pouso), segura um instante de
 * suspense e a tela inteira pisca antes do veredito travar na janela.
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 * Toda a UI fica atrás de #ifndef KIT_SDK_STUBS — ver tool_lvgl_runtime.md.
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"

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

#define J_STONE_D    210   // pedra: quadrado arredondado, não círculo — de
#define J_STONE_R    36    // propósito, pra não ler como a Bola 8
#define J_RING_W     10
#define J_WINDOW_D   148   // janela circular central onde o veredito aparece
#define J_ANSWER_W   112   // largura do texto dentro da janela (< diâmetro)

// Salto: 3 pousos decrescentes (translate_y), thud só no pouso (valor 0).
// Sem float — só inteiro. Cada tick tem o mesmo período (J_BOUNCE_MS).
static const int8_t BOUNCE_Y[] = { -20, -8, 0, -13, -5, 0, -7, -2, 0 };
#define BOUNCE_N ((int)(sizeof(BOUNCE_Y) / sizeof(BOUNCE_Y[0])))
#define J_BOUNCE_MS    70
#define J_SUSPENSE_MS  260

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

typedef enum {
    PHASE_IDLE = 0,
    PHASE_BOUNCE,
    PHASE_SUSPENSE,
    PHASE_FLASH,
} phase_t;

// Flash de tela cheia no instante do veredito: cobre tudo opaco e desbota em
// poucos passos — a resposta já está por baixo, revelada aos poucos. Só
// inteiro (degraus de LV_OPA), sem lv_anim (fora da whitelist do runtime).
static const uint8_t FLASH_OPA[] = { 255, 190, 120, 60, 0 };
#define FLASH_N ((int)(sizeof(FLASH_OPA) / sizeof(FLASH_OPA[0])))
#define J_FLASH_MS  45

// --- estado ----------------------------------------------------------------
static const kit_api_table_t *s_api = NULL;

static uint32_t s_accent = KIT_COLOR_RED;
static phase_t  s_phase  = PHASE_IDLE;
static int      s_last   = -1;
static int      s_target = -1;
static int      s_tick   = 0;
static lv_timer_t *s_timer = NULL;

// --- objetos LVGL ------------------------------------------------------
static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_stone_wrap = NULL;
static lv_obj_t *s_stone     = NULL;
static lv_obj_t *s_window     = NULL;
static lv_obj_t *s_answer     = NULL;
static lv_obj_t *s_go_btn     = NULL;
static lv_obj_t *s_flash      = NULL;

// --- helpers -------------------------------------------------------------

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

// --- pergunta / veredito ---------------------------------------------------
//
// Toda a sequência usa APENAS lv_timer + beep() de duração explícita — nunca
// um kit_sfx_t de duração fixa e desconhecida, que continuaria tocando depois
// da animação terminar se o timing não bater exatamente (foi o defeito da
// primeira versão: KIT_SFX_ROULETTE não parava quando o sorteio travava).

static void bounce_tick_cb(lv_timer_t *t);
static void suspense_done_cb(lv_timer_t *t);
static void flash_tick_cb(lv_timer_t *t);

static void do_ask(void)
{
    if (s_phase != PHASE_IDLE || !s_answer) return;
    s_target = rnd_index();
    s_tick = 0;
    s_phase = PHASE_BOUNCE;

    lv_label_set_text(s_answer, "");
    lv_obj_set_style_opa(s_go_btn, LV_OPA_60, 0);   // "ocupado"

    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_timer = lv_timer_create(bounce_tick_cb, J_BOUNCE_MS, NULL);
    lv_timer_set_repeat_count(s_timer, BOUNCE_N);
}

static void bounce_tick_cb(lv_timer_t *t)
{
    (void)t;
    int y = BOUNCE_Y[s_tick];
    lv_obj_set_style_translate_y(s_stone_wrap, y, 0);
    if (y == 0 && s_api && s_api->audio)
        s_api->audio->beep(160, 18);   // thud curto e seco a cada pouso

    s_tick++;
    if (s_tick < BOUNCE_N) return;

    s_timer = NULL;
    s_phase = PHASE_SUSPENSE;
    s_timer = lv_timer_create(suspense_done_cb, J_SUSPENSE_MS, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}

static void suspense_done_cb(lv_timer_t *t)
{
    (void)t;
    s_timer = NULL;
    s_phase = PHASE_FLASH;
    s_tick  = 0;

    // A resposta já é trocada AQUI, encoberta pelo flash opaco — o desbotar
    // seguinte revela o veredito, em vez de só aparecer.
    s_last = s_target;
    lv_label_set_text(s_answer, ANSWERS[s_target]);
    lv_obj_set_style_text_color(s_answer, lv_color_hex(s_accent), 0);

    if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_REVEAL);

    lv_obj_set_style_bg_opa(s_flash, FLASH_OPA[0], 0);
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    s_timer = lv_timer_create(flash_tick_cb, J_FLASH_MS, NULL);
    lv_timer_set_repeat_count(s_timer, FLASH_N - 1);
}

static void flash_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_tick++;
    lv_obj_set_style_bg_opa(s_flash, FLASH_OPA[s_tick], 0);

    if (s_tick < FLASH_N - 1) return;

    s_timer = NULL;
    s_phase = PHASE_IDLE;
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_go_btn, LV_OPA_COVER, 0);
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
    lv_obj_add_flag(stage, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(stage, ask_cb, LV_EVENT_CLICKED, NULL);

    // Pedra: quadrado arredondado (J_STONE_R, não J_STONE_D/2 — de propósito
    // pra não virar círculo/bola), anel vermelho sobre base escura. Balança
    // (translate_y) ao pousar; o contraste quadrado/círculo com a janela
    // dentro é a própria gramática Bauhaus, não uma Bola 8 achatada.
    s_stone_wrap = plain_box(stage);
    lv_obj_set_size(s_stone_wrap, J_STONE_D, J_STONE_D);
    lv_obj_add_flag(s_stone_wrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_center(s_stone_wrap);

    s_stone = lv_obj_create(s_stone_wrap);
    lv_obj_remove_style_all(s_stone);
    lv_obj_set_size(s_stone, J_STONE_D, J_STONE_D);
    lv_obj_set_style_radius(s_stone, J_STONE_R, 0);
    lv_obj_set_style_bg_color(s_stone, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_stone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_stone, J_RING_W, 0);
    lv_obj_set_style_border_color(s_stone, lv_color_hex(s_accent), 0);
    lv_obj_set_style_border_opa(s_stone, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_stone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_stone);

    // Janela central circular: onde o veredito aparece.
    s_window = lv_obj_create(s_stone);
    lv_obj_remove_style_all(s_window);
    lv_obj_set_size(s_window, J_WINDOW_D, J_WINDOW_D);
    lv_obj_set_style_radius(s_window, J_WINDOW_D / 2, 0);
    lv_obj_set_style_bg_color(s_window, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_window, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_window, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_window);

    s_answer = add_label(s_window, "Pergunte.", KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);
    lv_label_set_long_mode(s_answer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_answer, J_ANSWER_W);
    lv_obj_set_style_text_align(s_answer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_answer);
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

// Camada de flash: tela cheia, por cima de tudo, oculta até o veredito.
static void build_flash(void)
{
    s_flash = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_flash);
    lv_obj_set_size(s_flash, KIT_DISPLAY_WIDTH, KIT_DISPLAY_HEIGHT);
    lv_obj_set_pos(s_flash, 0, 0);
    lv_obj_set_style_bg_color(s_flash, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
}

// --- ciclo de vida da Tool ---------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;

    s_accent = KIT_COLOR_RED;
    s_phase  = PHASE_IDLE;
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
    build_flash();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);
    s_phase = PHASE_IDLE;

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_stone_wrap = s_stone = s_window = s_answer = s_go_btn = s_flash = NULL;
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
