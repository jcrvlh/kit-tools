/**
 * @file main.c
 * @brief Estouro — mini-jogo de mesa para o KIT.
 *
 * "Passa o KIT e reza": cada jogador escolhe livremente quanto chacoalha
 * antes de repassar o aparelho adiante. Um limiar de chacoalhadas (sorteado
 * numa faixa, nunca mostrado) decide quando estoura — ninguém sabe quando.
 * Quem estiver segurando o KIT na hora sai da roda (eliminação social, o
 * firmware só conta jogadores restantes). O limiar encolhe a cada eliminação
 * — final de jogo mais tenso, com estouros mais rápidos.
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 * Toda a UI fica atrás de #ifndef KIT_SDK_STUBS — ver tool_lvgl_runtime.md.
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KIT_SDK_STUBS

// ---------------------------------------------------------------------------
// Layout (368 × 448 — espelha as métricas do Pavio/Bingo)
// ---------------------------------------------------------------------------
#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define E_PAD        16
#define E_CONTENT    (KIT_DISPLAY_WIDTH - 2 * E_PAD)          // 336
#define E_TITLEBAR   88
#define E_CHIP       56
#define E_STEP       80
#define E_BTN_H      76
#define E_BTN_MARGIN 18
#define E_PAGE_H     (KIT_DISPLAY_HEIGHT - E_TITLEBAR)
// Offset vertical do grupo de texto da página JOGO: centraliza no espaço
// livre ACIMA do botão. Usado com 0 (centro real da página) quando o botão
// está escondido — ver update_game_view().
#define E_GROUP_OFFSET_Y  (-(E_BTN_H + E_BTN_MARGIN) / 2)
#define PAGES        3   // AJUSTE · JOGO · COMO JOGA

#define PLAYERS_MIN     2
#define PLAYERS_MAX     16
#define PLAYERS_DEFAULT 4

// Faixa do limiar (em chacoalhadas) no início do jogo, e o quanto encolhe a
// cada eliminação — final de jogo com o balão mais curto.
#define THRESH_MAX_START  22
#define THRESH_MIN_START   9
#define THRESH_FLOOR_MAX    8
#define THRESH_FLOOR_MIN    4
#define THRESH_SHRINK_STEP  2

// Piscar do overlay de estouro/fim.
#define FLASH_TICK_MS 420

// Pulso ambiente de tensão (fundo da página JOGO, ver mais abaixo).
#define PULSE_PERIOD_CALM_MS 620   // batimento com tensão 0
#define PULSE_PERIOD_SPAN_MS 480   // quanto encurta até a tensão máxima
#define PULSE_OPA_AMBIENT     LV_OPA_20

// Flash forte no instante de cada chacoalhada registrada.
#define SHAKE_FLASH_MS 90

#define K_PLAYERS "estouro_np"

typedef enum {
    GSTATE_IDLE = 0,   // ainda não começou / entre configurações
    GSTATE_PLAYING,    // rodada em andamento, contando chacoalhadas
    GSTATE_BURST,      // acabou de estourar — overlay piscando
    GSTATE_OVER        // só sobrou 1 jogador — overlay de fim (não pisca)
} game_state_t;

// --- estado ---------------------------------------------------------------
static const kit_api_table_t *s_api = NULL;

static int          s_players_cfg  = PLAYERS_DEFAULT;   // ajuste (persistido)
static int          s_players_init = 0;                 // jogadores no início da partida atual
static int          s_players_left = 0;
static int          s_shake_count  = 0;
static int          s_threshold    = 0;
static game_state_t s_state        = GSTATE_IDLE;
static uint32_t     s_accent       = KIT_COLOR_YELLOW;

// Proporções em que o aviso muda de cor (calmo->alerta->perigo), em
// permilagem (0..1000), sorteadas de novo a cada rodada em roll_threshold()
// — ver comentário lá. Ponto fixo inteiro de propósito: um .so de Tool não
// resolve __divsf3 (divisão de float) no elf_loader do KIT — nada de float
// nesta Tool, só inteiro de 32 bits.
static int s_yellow_permille = 450;
static int s_red_permille    = 800;

// --- objetos LVGL --------------------------------------------------------
static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_tv            = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];

static lv_obj_t *s_players_lbl   = NULL;
static lv_obj_t *s_players_minus = NULL;
static lv_obj_t *s_players_plus  = NULL;

static lv_obj_t *s_game_box      = NULL;   // fundo da página JOGO (recebe o pulso/flash)
static lv_obj_t *s_game_group    = NULL;   // grupo (rótulo/número/aviso), realinhado por estado
static lv_obj_t *s_above_lbl     = NULL;   // "JOGADORES" / "NA RODA"
static lv_obj_t *s_count_lbl     = NULL;   // número grande
static lv_obj_t *s_status_lbl    = NULL;   // dica / aviso de tensão
static lv_obj_t *s_action_btn    = NULL;
static lv_obj_t *s_action_lbl    = NULL;

static lv_obj_t   *s_overlay   = NULL;
static lv_obj_t   *s_ov_disc   = NULL;
static lv_obj_t   *s_ov_icon   = NULL;   // "!" — estourou
static lv_obj_t   *s_ov_check  = NULL;   // check — fim de jogo
static lv_obj_t   *s_ov_word   = NULL;
static lv_obj_t   *s_ov_hint   = NULL;
static lv_timer_t *s_flash_timer = NULL;
static bool         s_flash_on    = false;

static lv_timer_t *s_pulse_timer = NULL;
static bool         s_pulse_on    = false;

// --- helpers ---------------------------------------------------------------

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

static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *shape(lv_obj_t *parent, int w, int h, int radius, int border)
{
    lv_obj_t *o = plain_box(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    if (border) lv_obj_set_style_border_width(o, border, 0);
    else        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *field_label(lv_obj_t *parent, const char *txt)
{
    return add_label(parent, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

static uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

// --- persistência (Storage API) --------------------------------------------

static void load_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    int32_t v;
    if (s_api->storage->get_i32(K_PLAYERS, &v) == KIT_OK && v >= PLAYERS_MIN && v <= PLAYERS_MAX)
        s_players_cfg = (int)v;
}

static void save_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    s_api->storage->set_i32(K_PLAYERS, s_players_cfg);
}

// --- sincronização de UI -------------------------------------------------

static void sync_dots(void)
{
    lv_obj_t *act = s_tv ? lv_tileview_get_tile_active(s_tv) : NULL;
    for (int i = 0; i < PAGES; i++) {
        bool on = (act == s_tiles[i]);
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(on ? s_accent : KIT_COLOR_LINE), 0);
        lv_obj_set_size(s_dots[i], on ? 20 : 8, 8);
    }
}

static void dim_step(lv_obj_t *btn, bool disabled)
{
    lv_obj_set_style_opa(btn, disabled ? LV_OPA_30 : LV_OPA_COVER, 0);
}

static void sync_players_stepper(void)
{
    lv_label_set_text_fmt(s_players_lbl, "%d", s_players_cfg);
    bool locked = (s_state != GSTATE_IDLE);
    dim_step(s_players_minus, locked || s_players_cfg <= PLAYERS_MIN);
    dim_step(s_players_plus,  locked || s_players_cfg >= PLAYERS_MAX);
}

// Cor do aviso conforme a proporção de chacoalhadas já dadas na rodada —
// nunca mostramos o número, só o "clima" (calmo -> alerta -> perigo).
static uint32_t tension_color(void)
{
    // Nunca apagado: mesmo calmo, o texto precisa ler bem no AMOLED — só sobe
    // pra cor de alerta/perigo conforme a proporção chacoalhada/limiar.
    if (s_threshold <= 0) return KIT_COLOR_TEXT;
    int ratio_pm = (s_shake_count * 1000) / s_threshold;   // permilagem, só inteiro
    if (ratio_pm < s_yellow_permille) return KIT_COLOR_TEXT;
    if (ratio_pm < s_red_permille)    return s_accent;
    return KIT_COLOR_RED;
}

static void update_game_view(void)
{
    switch (s_state) {
    case GSTATE_IDLE:
        lv_obj_remove_flag(s_above_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_count_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_above_lbl, "JOGADORES");
        lv_obj_set_style_text_color(s_above_lbl, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_label_set_text_fmt(s_count_lbl, "%d", s_players_cfg);
        lv_obj_set_style_text_color(s_count_lbl, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_obj_set_style_text_font(s_status_lbl, &kit_mono_20, 0);
        lv_label_set_text(s_status_lbl, "TOQUE EM COME\xC3\x87" "AR");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_game_group, LV_ALIGN_CENTER, 0, E_GROUP_OFFSET_Y);   // reserva espaço pro botão
        break;
    case GSTATE_PLAYING:
        // Sem número de jogadores na tela do chacoalho — só o aviso, grande,
        // que é o que importa enquanto se está jogando.
        lv_obj_add_flag(s_above_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_count_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(s_status_lbl, &kit_sans_28, 0);
        lv_label_set_text(s_status_lbl, "CHACOALHE E\nPASSE ADIANTE");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(tension_color()), 0);
        lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);   // só chacoalhar durante o jogo
        lv_obj_align(s_game_group, LV_ALIGN_CENTER, 0, 0);   // sem botão, centraliza na página inteira
        break;
    case GSTATE_BURST:
    case GSTATE_OVER:
        // conteúdo da página fica escondido atrás do overlay cheio.
        break;
    }
}

// --- limiar da rodada -------------------------------------------------

static void roll_threshold(void)
{
    int eliminated = s_players_init - s_players_left;
    int rmax = THRESH_MAX_START - eliminated * THRESH_SHRINK_STEP;
    int rmin = THRESH_MIN_START - eliminated * THRESH_SHRINK_STEP;
    if (rmax < THRESH_FLOOR_MAX) rmax = THRESH_FLOOR_MAX;
    if (rmin < THRESH_FLOOR_MIN) rmin = THRESH_FLOOR_MIN;
    if (rmin >= rmax) rmin = rmax - 1;
    if (rmin < 1) rmin = 1;

    s_threshold   = (s_api && s_api->random) ? (int)s_api->random->range(rmin, rmax) : rmin;
    s_shake_count = 0;

    // Faixas com folga entre si (e entre 0/1) pra sempre dar pra perceber a
    // escalada calmo -> alerta -> perigo, mas sorteadas de novo por rodada —
    // se fossem sempre as mesmas 45%/80%, dava pra "aprender" contando
    // chacoalhadas em quantas geralmente muda de cor e estimar o limiar.
    s_yellow_permille = (s_api && s_api->random) ? s_api->random->range(250, 550) : 400;   // 25%-55%
    s_red_permille    = (s_api && s_api->random) ? s_api->random->range(650, 900) : 800;   // 65%-90%
}

// --- overlay de estouro / fim -------------------------------------------

static void ov_paint(bool bright)
{
    uint32_t bg = bright ? KIT_COLOR_RED : KIT_COLOR_BG;
    uint32_t fg = bright ? KIT_COLOR_BG  : KIT_COLOR_RED;
    if (s_state == GSTATE_OVER) { bg = s_accent; fg = KIT_COLOR_BG; }

    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(s_ov_disc, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(s_ov_word, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(s_ov_hint, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(s_ov_check, lv_color_hex(bg), 0);

    // s_ov_icon tem exatamente 2 filhos, criados nesta ordem em build_overlay
    // — acesso direto em vez de contar filhos (símbolo indisponível ao .so).
    lv_obj_t *c0 = lv_obj_get_child(s_ov_icon, 0);
    lv_obj_t *c1 = lv_obj_get_child(s_ov_icon, 1);
    if (c0) { lv_obj_set_style_bg_color(c0, lv_color_hex(bg), 0); lv_obj_set_style_border_color(c0, lv_color_hex(bg), 0); }
    if (c1) { lv_obj_set_style_bg_color(c1, lv_color_hex(bg), 0); lv_obj_set_style_border_color(c1, lv_color_hex(bg), 0); }
}

static void flash_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_flash_on = !s_flash_on;
    ov_paint(s_flash_on);
}

static void hide_overlay(void)
{
    if (s_flash_timer) { lv_timer_delete(s_flash_timer); s_flash_timer = NULL; }
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void show_burst_overlay(void)
{
    lv_label_set_text(s_ov_word, "ESTOUROU");
    lv_label_set_text(s_ov_hint, "QUEM SEGURAVA SAI \xC2\xB7 TOQUE PARA CONTINUAR");
    lv_obj_remove_flag(s_ov_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ov_check, LV_OBJ_FLAG_HIDDEN);
    s_flash_on = true;
    ov_paint(true);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    if (s_flash_timer) lv_timer_delete(s_flash_timer);
    s_flash_timer = lv_timer_create(flash_tick_cb, FLASH_TICK_MS, NULL);
}

static void show_over_overlay(void)
{
    lv_label_set_text(s_ov_word, "ACABOU");
    lv_label_set_text(s_ov_hint, "TOQUE PARA JOGAR DE NOVO");
    lv_obj_add_flag(s_ov_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ov_check, LV_OBJ_FLAG_HIDDEN);
    if (s_flash_timer) { lv_timer_delete(s_flash_timer); s_flash_timer = NULL; }
    ov_paint(false);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

// --- pulso ambiente de tensão ---------------------------------------------
// Um "batimento" visual contínuo (bg_opa indo e voltando) no fundo da página
// JOGO enquanto se joga: fica mais rápido conforme a proporção chacoalhada/
// limiar sobe, dando a sensação de "mais agitado perto do estouro" mesmo
// parado entre uma chacoalhada e outra. Mesma curva de suspense-depois-
// pânico do `audio->fuse`.

// Curva "medo -> pânico" em ponto fixo (permilagem, 0..1000) — mistura
// linear + quadrática, igual à do motor de áudio do Pavio, só que sem float.
static uint32_t pulse_period_ms(void)
{
    int t_pm = (s_threshold > 0) ? (s_shake_count * 1000) / s_threshold : 0;
    if (t_pm > 1000) t_pm = 1000;
    int t2_pm = (t_pm * t_pm) / 1000;
    int shape_pm = (300 * t_pm + 700 * t2_pm) / 1000;
    return (uint32_t)(PULSE_PERIOD_CALM_MS - (PULSE_PERIOD_SPAN_MS * shape_pm) / 1000);
}

static void stop_pulse(void)
{
    if (s_pulse_timer) { lv_timer_delete(s_pulse_timer); s_pulse_timer = NULL; }
    if (s_game_box) lv_obj_set_style_bg_opa(s_game_box, LV_OPA_TRANSP, 0);
}

static void pulse_tick_cb(lv_timer_t *t)
{
    s_pulse_on = !s_pulse_on;
    if (s_game_box) {
        if (s_pulse_on) {
            lv_obj_set_style_bg_color(s_game_box, lv_color_hex(tension_color()), 0);
            lv_obj_set_style_bg_opa(s_game_box, PULSE_OPA_AMBIENT, 0);
        } else {
            lv_obj_set_style_bg_opa(s_game_box, LV_OPA_TRANSP, 0);
        }
    }
    lv_timer_set_period(t, pulse_period_ms());
}

static void start_pulse(void)
{
    stop_pulse();
    s_pulse_on = false;
    s_pulse_timer = lv_timer_create(pulse_tick_cb, (uint32_t)PULSE_PERIOD_CALM_MS, NULL);
}

// --- feedback de chacoalhada ---------------------------------------------
// Além do pulso ambiente, um flash mais forte e curto no instante exato de
// cada chacoalhada registrada — confirma na hora que ela valeu.

static void shake_flash_off_cb(lv_timer_t *t)
{
    if (s_game_box) lv_obj_set_style_bg_opa(s_game_box, LV_OPA_TRANSP, 0);
    lv_timer_delete(t);
}

static void flash_shake_feedback(void)
{
    if (!s_game_box) return;
    lv_obj_set_style_bg_color(s_game_box, lv_color_hex(tension_color()), 0);
    lv_obj_set_style_bg_opa(s_game_box, LV_OPA_50, 0);
    lv_timer_create(shake_flash_off_cb, SHAKE_FLASH_MS, NULL);
}

// --- jogo ---------------------------------------------------------------

static void start_game(void)
{
    s_players_init = s_players_cfg;
    s_players_left = s_players_cfg;
    s_state        = GSTATE_PLAYING;
    roll_threshold();
    sync_players_stepper();
    update_game_view();
    start_pulse();
}

static void trigger_burst(void)
{
    if (s_api && s_api->audio) {
        s_api->audio->fuse(-1);
        s_api->audio->sfx(KIT_SFX_ESTOURO_POP);
    }
    stop_pulse();
    s_state = GSTATE_BURST;
    show_burst_overlay();
}

static void register_shake(void)
{
    s_shake_count++;

    if (s_shake_count >= s_threshold) {
        trigger_burst();
        return;
    }

    int tension = (s_shake_count * 255) / s_threshold;
    if (tension < 0) tension = 0;
    if (tension > 255) tension = 255;
    if (s_api && s_api->audio) {
        s_api->audio->fuse((int16_t)tension);
        s_api->audio->sfx(KIT_SFX_ESTOURO_SHAKE);   // confirma a chacoalhada, alto e curto
    }

    update_game_view();
    flash_shake_feedback();
}

// Toque/chacoalhar no overlay: avança pra próxima rodada ou reinicia.
static void overlay_advance(void)
{
    if (s_state == GSTATE_BURST) {
        hide_overlay();
        s_players_left--;
        if (s_players_left <= 1) {
            s_state = GSTATE_OVER;
            update_game_view();
            show_over_overlay();
        } else {
            s_state = GSTATE_PLAYING;
            roll_threshold();
            update_game_view();
            start_pulse();
        }
    } else if (s_state == GSTATE_OVER) {
        hide_overlay();
        s_state = GSTATE_IDLE;
        s_players_left = s_players_init = 0;
        update_game_view();
    }
}

// Ação única do jogo — chamada pelo toque (botão/tela) e pelo chacoalhar.
// O efeito depende do estado: parado = começa; jogando = registra a
// chacoalhada; overlay = confirma e segue.
static void game_action(void)
{
    if (!s_screen) return;
    switch (s_state) {
    case GSTATE_IDLE:    start_game();      break;
    case GSTATE_PLAYING: register_shake();  break;
    case GSTATE_BURST:
    case GSTATE_OVER:    overlay_advance(); break;
    }
}

static void on_shake(void *user_data)
{
    (void)user_data;
    game_action();
}

// --- callbacks -------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_api && s_api->system) s_api->system->exit();
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    sync_dots();
}

static void players_step_cb(lv_event_t *e)
{
    if (s_state != GSTATE_IDLE) return;
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int v = s_players_cfg + delta;
    if (v < PLAYERS_MIN) v = PLAYERS_MIN;
    if (v > PLAYERS_MAX) v = PLAYERS_MAX;
    if (v == s_players_cfg) return;
    s_players_cfg = v;
    sync_players_stepper();
    update_game_view();
    save_prefs();
}

static void action_cb(lv_event_t *e) { (void)e; game_action(); }
static void overlay_cb(lv_event_t *e) { (void)e; game_action(); }

// --- construção da tela ---------------------------------------------

static lv_obj_t *make_step_btn(lv_obj_t *parent, const char *sym, int delta)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, E_STEP, E_STEP);
    lv_obj_set_style_bg_color(b, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_add_event_cb(b, players_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)delta);
    lv_obj_t *l = add_label(b, sym, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(l);
    return b;
}

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, E_CHIP, E_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, E_PAD, 16);

    lv_obj_t *g = add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(g);

    lv_obj_t *title = add_label(s_screen, "ESTOURO", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, E_PAD + E_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -E_PAD, 40);
    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(KIT_COLOR_LINE), 0);
        s_dots[i] = d;
    }
}

// Página 0 — AJUSTE: só o número de jogadores.
static void build_page_setup(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = plain_box(tile);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, E_PAD, 0);
    lv_obj_set_style_pad_right(p, E_PAD, 0);
    lv_obj_set_style_pad_top(p, 10, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 14, 0);

    field_label(p, "JOGADORES");

    lv_obj_t *row = plain_box(p);
    lv_obj_set_size(row, lv_pct(100), E_STEP);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);

    s_players_minus = make_step_btn(row, "-", -1);
    s_players_lbl = add_label(row, "4", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_set_width(s_players_lbl, 76);
    lv_obj_set_style_text_align(s_players_lbl, LV_TEXT_ALIGN_CENTER, 0);
    s_players_plus = make_step_btn(row, "+", 1);
}

// Página 1 — JOGO. O grupo (rótulo/número/aviso) fica centralizado no espaço
// livre ACIMA do botão — ver E_GROUP_OFFSET_Y (topo do arquivo), reutilizado
// por update_game_view() pra realinhar quando o botão some.
static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    s_game_box = lv_obj_create(tile);
    lv_obj_remove_style_all(s_game_box);
    lv_obj_set_size(s_game_box, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_game_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_game_box, LV_OPA_TRANSP, 0);   // fica opaco só no pulso/flash
    lv_obj_set_style_radius(s_game_box, 24, 0);

    s_game_group = plain_box(s_game_box);
    lv_obj_t *group = s_game_group;
    lv_obj_set_size(group, E_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 10, 0);
    lv_obj_align(group, LV_ALIGN_CENTER, 0, E_GROUP_OFFSET_Y);

    s_above_lbl = add_label(group, "JOGADORES", KIT_COLOR_TEXT, &kit_mono_20, 2);

    s_count_lbl = add_label(group, "4", KIT_COLOR_TEXT, &kit_display_120, 0);
    lv_obj_set_width(s_count_lbl, E_CONTENT);
    lv_obj_set_style_text_align(s_count_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_status_lbl = add_label(group, "TOQUE EM COME\xC3\x87" "AR", KIT_COLOR_TEXT, &kit_mono_20, 1);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_lbl, E_CONTENT);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_action_btn = lv_obj_create(s_game_box);
    lv_obj_set_size(s_action_btn, E_CONTENT, E_BTN_H);
    lv_obj_set_style_radius(s_action_btn, E_BTN_H / 2, 0);
    lv_obj_set_style_border_width(s_action_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_action_btn, 0, 0);
    lv_obj_set_style_pad_all(s_action_btn, 0, 0);
    lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_action_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_action_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_action_btn, 8);
    lv_obj_align(s_action_btn, LV_ALIGN_BOTTOM_MID, 0, -E_BTN_MARGIN);
    lv_obj_add_event_cb(s_action_btn, action_cb, LV_EVENT_CLICKED, NULL);

    s_action_lbl = add_label(s_action_btn, "COME\xC3\x87" "AR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_action_lbl);
}

static const char ESTOURO_RULES[] =
    "Todo mundo em roda, passando o KIT de m\xC3\xA3o em m\xC3\xA3o.\n\n"
    "1. No AJUSTE, escolha quantos jogadores v\xC3\xA3o entrar e toque em "
    "COME\xC3\x87" "AR.\n\n"
    "2. Chacoalhe o KIT quantas vezes quiser (pelo menos uma) e passe pro "
    "pr\xC3\xB3ximo. Ningu\xC3\xA9m sabe quantas faltam pra estourar - "
    "s\xC3\xB3 d\xC3\xA1 pra sentir o aviso ficando mais vermelho e a tela mais "
    "agitada.\n\n"
    "3. Estourou: quem estava segurando o KIT sai da roda. Toque na tela pra "
    "confirmar e a pr\xC3\xB3xima rodada come\xC3\xA7" "a.\n\n"
    "4. A cada jogador eliminado o bal\xC3\xA3o fica mais curto - o fim do "
    "jogo fica mais tenso. S\xC3\xB3 sobra um: esse ganhou.";

static void build_page_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, E_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, ESTOURO_RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, E_CONTENT);
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, KIT_DISPLAY_WIDTH, E_PAGE_H);
    lv_obj_set_pos(s_tv, 0, E_TITLEBAR);
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

// Overlay cheio-de-tela de estouro/fim: disco + ícone geométrico + palavra,
// piscando (estouro) ou estático (fim). Um "!" simples faz as vezes de
// estilhaço/alerta; no fim de jogo um check substitui o "!".
static void build_overlay(void)
{
    s_overlay = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, KIT_DISPLAY_WIDTH, KIT_DISPLAY_HEIGHT);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, overlay_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *col = plain_box(s_overlay);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 24, 0);
    lv_obj_center(col);

    s_ov_disc = lv_obj_create(col);
    lv_obj_set_size(s_ov_disc, 132, 132);
    lv_obj_set_style_bg_color(s_ov_disc, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_border_width(s_ov_disc, 0, 0);
    lv_obj_set_style_radius(s_ov_disc, 66, 0);
    lv_obj_set_style_pad_all(s_ov_disc, 0, 0);
    lv_obj_remove_flag(s_ov_disc, LV_OBJ_FLAG_SCROLLABLE);

    s_ov_icon = plain_box(s_ov_disc);
    lv_obj_set_size(s_ov_icon, 64, 64);
    lv_obj_add_flag(s_ov_icon, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_center(s_ov_icon);
    lv_obj_align(shape(s_ov_icon, 16, 40, 6, 0), LV_ALIGN_CENTER, 0, -8);
    lv_obj_align(shape(s_ov_icon, 16, 16, 8, 0), LV_ALIGN_CENTER, 0, 24);

    // Fim de jogo: check em vez do "!" — mesmo disco, glifo diferente.
    s_ov_check = add_label(s_ov_disc, KIT_ICON_CHECK, KIT_COLOR_BG, &kit_display_44, 0);
    lv_obj_center(s_ov_check);
    lv_obj_add_flag(s_ov_check, LV_OBJ_FLAG_HIDDEN);

    s_ov_word = add_label(col, "ESTOUROU", KIT_COLOR_BG, &kit_mono_26, 5);

    s_ov_hint = add_label(s_overlay, "", KIT_COLOR_BG, &kit_mono_20, 2);
    lv_label_set_long_mode(s_ov_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ov_hint, E_CONTENT);
    lv_obj_set_style_text_align(s_ov_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ov_hint, LV_ALIGN_BOTTOM_MID, 0, -36);
}

// ---------------------------------------------------------------------------
// Ciclo de vida da Tool
// ---------------------------------------------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    printf("[Estouro] tool_init\n");
    s_api = ctx ? ctx->api : NULL;

    s_accent       = KIT_COLOR_YELLOW;
    s_state        = GSTATE_IDLE;
    s_players_init = s_players_left = s_shake_count = s_threshold = 0;
    load_prefs();

    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(on_shake, NULL);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();
    build_overlay();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   // começa no JOGO

    sync_players_stepper();
    update_game_view();
    sync_dots();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Estouro] tool_destroy\n");
    if (s_api && s_api->audio) s_api->audio->fuse(-1);   // nunca deixa o batimento tocando sozinho
    stop_pulse();
    if (s_flash_timer) { lv_timer_delete(s_flash_timer); s_flash_timer = NULL; }

    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    s_players_lbl = s_players_minus = s_players_plus = NULL;
    s_game_box = s_game_group = s_above_lbl = s_count_lbl = s_status_lbl = NULL;
    s_action_btn = s_action_lbl = NULL;
    s_overlay = s_ov_disc = s_ov_icon = s_ov_check = s_ov_word = s_ov_hint = NULL;
    s_state = GSTATE_IDLE;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo para testes e CI */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Estouro stub] tool_init — compila nativo; UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
