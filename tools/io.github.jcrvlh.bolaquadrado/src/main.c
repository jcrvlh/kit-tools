/**
 * @file main.c
 * @brief Quadrado — mini-jogo de mesa para o KIT.
 *
 * Homenagem ao primeiro app Android do autor (2017, "Bola/Quadrado"): a tela
 * mostra um quadrado e uma bola, um de cada lado. Toque em COMEÇAR, depois
 * toque sempre no quadrado — cada acerto vale 5 pontos e sorteia se o lado
 * troca ou não (50/50, ritmo constante). Tocar na bola encerra a rodada na
 * hora. A partida também acaba quando o tempo escolhido no AJUSTE (15/30/60 s)
 * chega a zero.
 *
 * Modo Inverte (opcional, AJUSTE): o alvo também pode trocar entre quadrado
 * e bola a cada acerto. Um ícone acima das formas mostra qual vale agora —
 * sem texto, reconhecimento puro. Tem highscore próprio, separado do modo
 * normal.
 *
 * Highscores: top-5 por modo (pontuação + 3 iniciais) persistidos no
 * aparelho (storage->set_i32/get_i32, set_str/get_str), numa página própria.
 * Ao entrar no top-5, o seletor de sigla (kit_ui_sigla) pede as 3 iniciais —
 * toque avança uma letra, arraste gira como roleta, pré-preenchido com a
 * última sigla usada.
 *
 * UI montada com a galeria de componentes tools-sdk/include/kit_ui.h — shell
 * (titlebar + tileview), grade de chips do AJUSTE, página COMO JOGA, botão de
 * ação e o seletor de sigla (que nasceu nesta Tool). Só a tabela de
 * highscores continua feita à mão (ainda não está na galeria).
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 * Toda a UI fica atrás de #ifndef KIT_SDK_STUBS — ver tool_lvgl_runtime.md.
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KIT_SDK_STUBS

// ---------------------------------------------------------------------------
// Layout (368 × 448)
// ---------------------------------------------------------------------------
#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define B_PAD        16
#define B_CONTENT    (KIT_DISPLAY_WIDTH - 2 * B_PAD)   // 336
#define B_TITLEBAR   88
#define B_PAGE_H     (KIT_DISPLAY_HEIGHT - B_TITLEBAR) // 360
#define B_BTN_H      76
#define B_BTN_MARGIN 18
#define PAGES        4   // AJUSTE · JOGO · COMO JOGA · HIGHSCORES

#define B_STATUS_H   44                          // faixa de placar/tempo/recorde
#define B_STAGE_H    (B_PAGE_H - B_STATUS_H)      // 316 — as duas metades
#define B_HALF_W     (KIT_DISPLAY_WIDTH / 2)      // 184
#define B_SHAPE      140                          // lado/diâmetro da forma

#define K_DURATION    "bq_dur"
#define K_INVERTE     "bq_inv"
#define K_LAST_INIT   "bq_lini"

#define POINTS_PER_HIT      5
#define HS_COUNT            5

static const int32_t DURATIONS[3] = { 15, 30, 60 };
static const char *const DUR_LABELS[3] = { "15S", "30S", "60S" };
static const char *const INV_LABELS[2] = { "DESLIGADO", "LIGADO" };

// ALERT: aviso de fim de rodada (TEMPO / PERDEU) que cobre a tela ANTES do
// resultado — evita apertar SALVAR no susto e trava a navegação junto.
typedef enum { STATE_IDLE, STATE_PLAYING, STATE_ALERT, STATE_RESULT } ui_state_t;

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
static const kit_api_table_t *s_api = NULL;
static uint32_t s_accent = KIT_COLOR_BLUE;

static ui_state_t s_state = STATE_IDLE;

static bool s_inverte_on = false;   // ajuste persistido (o que vale pro PRÓXIMO início)
static int  s_dur_idx    = 1;       // índice em DURATIONS — default 30 s

static int  s_score        = 0;
static int  s_time_left    = 30;
static bool s_round_inverte = false;   // modo capturado no início desta rodada

// Um top-5 por modo — o Modo Inverte é um jogo bem diferente, mistura-los
// no mesmo ranking não fazia sentido.
typedef struct { int32_t score; char initials[4]; } hs_entry_t;
static hs_entry_t s_hs_normal[HS_COUNT];
static hs_entry_t s_hs_inv[HS_COUNT];
static char s_last_initials[4] = "AAA";   // última sigla digitada — pré-preenche o editor

static bool s_square_on_left   = true;
static bool s_target_is_square = true;    // com Modo Inverte desligado, é sempre true
static int  s_pending_rank     = -1;      // posição no top-5 se a rodada acabar agora (-1 = não entra)

// --- objetos LVGL (todos zerados em tool_destroy) ---------------------
static lv_obj_t   *s_screen = NULL;
static kit_ui_shell_t  s_shell;          // titlebar + tileview + dots
static lv_timer_t *s_round_timer = NULL;

// AJUSTE
static kit_ui_chips_t s_dur;
static kit_ui_chips_t s_inv;

// JOGO — três estados no mesmo tile: parado, jogando, resultado.
static lv_obj_t *s_idle_group     = NULL;
static lv_obj_t *s_idle_record_lbl = NULL;

static lv_obj_t *s_play_group  = NULL;
static lv_obj_t *s_score_lbl   = NULL;
static lv_obj_t *s_time_lbl    = NULL;
static lv_obj_t *s_record_lbl  = NULL;
static lv_obj_t *s_stage       = NULL;
static lv_obj_t *s_target_badge = NULL;    // ícone do alvo atual (só com Modo Inverte ligado)
static lv_obj_t *s_zone[2]     = { NULL, NULL };   // alvo de toque = metade inteira
static lv_obj_t *s_shape[2]    = { NULL, NULL };   // forma dentro de cada metade

static lv_obj_t *s_alert_group = NULL;      // overlay TEMPO / PERDEU
static lv_obj_t *s_alert_lbl   = NULL;

static lv_obj_t *s_result_group   = NULL;
static lv_obj_t *s_result_score   = NULL;
static lv_obj_t *s_result_caption = NULL;
static kit_ui_sigla_t s_sigla;             // entrada das 3 iniciais no highscore

// Botão de ação — IDLE ("COMEÇAR") / RESULTADO ("SALVAR"/"JOGAR DE NOVO").
static kit_ui_action_t s_action;

// HIGHSCORES — uma seção por modo.
static lv_obj_t *s_hsn_left[HS_COUNT], *s_hsn_right[HS_COUNT];
static lv_obj_t *s_hsi_left[HS_COUNT], *s_hsi_right[HS_COUNT];

// --- helpers ------------------------------------------------------------
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

// Container invisível de layout — não rola (ver build_game_result, que cria
// o próprio container quando precisa de scroll).
static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// --- áudio: presets prontos + notas curtas na faixa confortável ---------
// (nada de bipe único agudo — é o que soava estridente na v1.)
static void sfx_click(void)   { if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_CLICK); }
static void sfx_confirm(void) { if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_CONFIRM); }
static void sfx_timeup(void)  { if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_TIMER_DONE); }

// Erro: duas notas curtas DESCENDO (A4 -> D4) — resolução suave, não um buzz.
static void sfx_miss(void)
{
    if (!s_api || !s_api->audio) return;
    s_api->audio->beep(440, 40);
    s_api->audio->beep(294, 90);
}

// --- persistência: highscores --------------------------------------------
static void hs_defaults(hs_entry_t *t)
{
    for (int i = 0; i < HS_COUNT; i++) {
        t[i].score = 0;
        t[i].initials[0] = t[i].initials[1] = t[i].initials[2] = '-';
        t[i].initials[3] = 0;
    }
}

static void hs_load(hs_entry_t *t, const char *score_prefix, const char *init_prefix)
{
    hs_defaults(t);
    if (!s_api || !s_api->storage) return;
    char key[16];
    for (int i = 0; i < HS_COUNT; i++) {
        int32_t v;
        snprintf(key, sizeof key, "%s%d", score_prefix, i);
        if (s_api->storage->get_i32(key, &v) == KIT_OK && v >= 0) t[i].score = v;
        snprintf(key, sizeof key, "%s%d", init_prefix, i);
        char buf[8];
        if (s_api->storage->get_str(key, buf, sizeof buf) == KIT_OK && buf[0]) {
            int n = 0;
            for (; n < 3 && buf[n]; n++) t[i].initials[n] = buf[n];
            for (; n < 3; n++) t[i].initials[n] = '-';
            t[i].initials[3] = 0;
        }
    }
}

static void hs_save_slot(hs_entry_t *t, int i, const char *score_prefix, const char *init_prefix)
{
    if (!s_api || !s_api->storage) return;
    char key[16];
    snprintf(key, sizeof key, "%s%d", score_prefix, i);
    s_api->storage->set_i32(key, t[i].score);
    snprintf(key, sizeof key, "%s%d", init_prefix, i);
    s_api->storage->set_str(key, t[i].initials);
}

// Posição em que `score` entraria no top-5 (0..4), ou -1 se não qualifica.
static int hs_rank_of(const hs_entry_t *t, int score)
{
    for (int i = 0; i < HS_COUNT; i++) if (score > t[i].score) return i;
    return -1;
}

static void hs_insert(hs_entry_t *t, int score, const char initials[3],
                       const char *score_prefix, const char *init_prefix)
{
    int idx = hs_rank_of(t, score);
    if (idx < 0) return;
    for (int i = HS_COUNT - 1; i > idx; i--) t[i] = t[i - 1];
    t[idx].score = score;
    t[idx].initials[0] = initials[0];
    t[idx].initials[1] = initials[1];
    t[idx].initials[2] = initials[2];
    t[idx].initials[3] = 0;
    for (int i = idx; i < HS_COUNT; i++) hs_save_slot(t, i, score_prefix, init_prefix);
}

static hs_entry_t *hs_table(bool inverte) { return inverte ? s_hs_inv : s_hs_normal; }
static const char *hs_score_prefix(bool inverte) { return inverte ? "bq_ihs" : "bq_hs"; }
static const char *hs_init_prefix(bool inverte)  { return inverte ? "bq_ihi" : "bq_hi"; }

static void load_prefs(void)
{
    hs_load(s_hs_normal, hs_score_prefix(false), hs_init_prefix(false));
    hs_load(s_hs_inv,    hs_score_prefix(true),  hs_init_prefix(true));
    if (!s_api || !s_api->storage) return;

    int32_t v;
    if (s_api->storage->get_i32(K_INVERTE, &v) == KIT_OK) s_inverte_on = (v != 0);

    if (s_api->storage->get_i32(K_DURATION, &v) == KIT_OK)
        for (int i = 0; i < 3; i++) if (DURATIONS[i] == v) s_dur_idx = i;

    char buf[8];
    if (s_api->storage->get_str(K_LAST_INIT, buf, sizeof buf) == KIT_OK) {
        bool valid = buf[0] && buf[1] && buf[2];
        for (int k = 0; valid && k < 3; k++) if (buf[k] < 'A' || buf[k] > 'Z') valid = false;
        if (valid) { s_last_initials[0] = buf[0]; s_last_initials[1] = buf[1];
                     s_last_initials[2] = buf[2]; s_last_initials[3] = 0; }
    }
}

static void save_inverte(void) { if (s_api && s_api->storage) s_api->storage->set_i32(K_INVERTE, s_inverte_on ? 1 : 0); }
static void save_duration(void) { if (s_api && s_api->storage) s_api->storage->set_i32(K_DURATION, DURATIONS[s_dur_idx]); }
static void save_last_initials(void) { if (s_api && s_api->storage) s_api->storage->set_str(K_LAST_INIT, s_last_initials); }

// --- lógica (só inteiro — nada de float) --------------------------------
static bool coin(void)
{
    if (!s_api || !s_api->random) return false;
    return s_api->random->range(0, 1) == 1;
}

static void sync_idle_record(void)
{
    hs_entry_t *t = hs_table(s_inverte_on);
    lv_label_set_text_fmt(s_idle_record_lbl, "RECORDE: %s %d", t[0].initials, t[0].score);
}

static void sync_score(void)
{
    hs_entry_t *t = hs_table(s_round_inverte);
    lv_label_set_text_fmt(s_score_lbl, "%d", s_score);
    lv_label_set_text_fmt(s_time_lbl, "%dS", s_time_left);
    lv_label_set_text_fmt(s_record_lbl, "%s %d", t[0].initials, t[0].score);
}

// Recoloca as duas formas conforme s_square_on_left. As duas são sempre
// azuis (cor base da Tool) — só a forma (quina viva x círculo) identifica
// qual é qual, nunca a cor.
static void place_shapes(void)
{
    int square_side = s_square_on_left ? 0 : 1;
    for (int side = 0; side < 2; side++) {
        bool is_square = (side == square_side);
        lv_obj_set_style_radius(s_shape[side], is_square ? 0 : LV_RADIUS_CIRCLE, 0);
    }
}

// Ícone do alvo atual, acima das formas — só aparece com Modo Inverte
// ligado (desligado, o alvo é sempre o quadrado; não precisa de aviso).
static void sync_target_badge(void)
{
    if (!s_inverte_on) { lv_obj_add_flag(s_target_badge, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(s_target_badge, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_target_badge, s_target_is_square ? KIT_ICON_SQUARE : KIT_ICON_CIRCLE);
}

// Enquanto a rodada está em curso (jogando, aviso de fim, ou digitando a
// sigla), o swipe entre as páginas fica travado — igual ao Fora.
static void show_idle_state(void)
{
    s_state = STATE_IDLE;
    sync_idle_record();
    lv_obj_remove_flag(s_idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_play_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_alert_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);
    kit_ui_action_set(&s_action, "COMEÇAR");
    kit_ui_action_show(&s_action, true);
    kit_ui_shell_lock(&s_shell, false);
}

static void show_play_state(void)
{
    s_state = STATE_PLAYING;
    lv_obj_add_flag(s_idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_play_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_alert_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);
    kit_ui_action_show(&s_action, false);
    kit_ui_shell_lock(&s_shell, true);
}

// Overlay de fim de rodada: cobre a tela com TEMPO / PERDEU e um "toque pra
// continuar". Só depois do toque é que aparece o resultado (e a sigla). É
// candidato a virar kit_ui_overlay (v2 da galeria).
static void enter_alert(const char *big)
{
    s_state = STATE_ALERT;
    lv_label_set_text(s_alert_lbl, big);
    lv_obj_add_flag(s_idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_play_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_alert_group, LV_OBJ_FLAG_HIDDEN);
    kit_ui_action_show(&s_action, false);   // toca no overlay pra seguir, não num botão
}

static void show_result_state(void)
{
    s_state = STATE_RESULT;
    lv_obj_add_flag(s_idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_play_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_alert_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);
    kit_ui_action_show(&s_action, true);
}

static void sync_hs_rows(const hs_entry_t *t, lv_obj_t **left, lv_obj_t **right)
{
    for (int i = 0; i < HS_COUNT; i++) {
        bool set = t[i].score > 0;
        uint32_t color = set ? KIT_COLOR_TEXT : KIT_COLOR_TEXT_MUTED;
        uint32_t score_color = (i == 0 && set) ? s_accent : color;
        char buf[16];
        snprintf(buf, sizeof buf, "%d  %s", i + 1, t[i].initials);
        lv_label_set_text(left[i], buf);
        lv_obj_set_style_text_color(left[i], lv_color_hex(color), 0);
        lv_label_set_text_fmt(right[i], "%d", t[i].score);
        lv_obj_set_style_text_color(right[i], lv_color_hex(score_color), 0);
    }
}

static void sync_highscores_view(void)
{
    sync_hs_rows(s_hs_normal, s_hsn_left, s_hsn_right);
    sync_hs_rows(s_hs_inv,    s_hsi_left, s_hsi_right);
}

static void stop_round_timer(void)
{
    if (s_round_timer) { lv_timer_delete(s_round_timer); s_round_timer = NULL; }
}

static void enter_result(void)
{
    lv_label_set_text_fmt(s_result_score, "%d", s_score);

    hs_entry_t *t = hs_table(s_round_inverte);
    s_pending_rank = hs_rank_of(t, s_score);
    bool qualifies = s_pending_rank >= 0;
    kit_ui_sigla_show(&s_sigla, qualifies);

    if (qualifies) {
        lv_label_set_text(s_result_caption, s_pending_rank == 0 ? "RECORDE NOVO!" : "ENTROU NO TOP 5!");
        kit_ui_sigla_set(&s_sigla, s_last_initials);   // pré-preenche com a última usada
        kit_ui_action_set(&s_action, "SALVAR");
    } else {
        char buf[40];
        snprintf(buf, sizeof buf, "RECORDE: %s %d", t[0].initials, t[0].score);
        lv_label_set_text(s_result_caption, buf);
        kit_ui_action_set(&s_action, "JOGAR DE NOVO");
    }
    show_result_state();
}

static void round_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_time_left--;
    if (s_time_left <= 0) {
        s_time_left = 0;
        lv_label_set_text_fmt(s_time_lbl, "%dS", s_time_left);
        stop_round_timer();
        sfx_timeup();
        enter_alert("TEMPO");
        return;
    }
    lv_label_set_text_fmt(s_time_lbl, "%dS", s_time_left);
}

static void start_round(void)
{
    s_round_inverte = s_inverte_on;   // a rodada joga com a regra de quando começou
    s_score = 0;
    s_time_left = DURATIONS[s_dur_idx];
    s_square_on_left = coin();
    s_target_is_square = true;   // toda rodada nova começa mirando o quadrado
    place_shapes();
    sync_target_badge();
    sync_score();
    show_play_state();

    stop_round_timer();
    s_round_timer = lv_timer_create(round_tick_cb, 1000, NULL);
}

static void on_hit(void)
{
    s_score += POINTS_PER_HIT;
    sfx_click();   // cliquezinho sutil — nada de "prêmio" chamativo repetido a cada toque
    if (coin()) s_square_on_left = !s_square_on_left;
    if (s_round_inverte && coin()) s_target_is_square = !s_target_is_square;
    place_shapes();
    sync_target_badge();
    sync_score();
}

static void on_miss(void)
{
    stop_round_timer();
    sfx_miss();
    enter_alert("PERDEU");
}

// --- callbacks -----------------------------------------------------------
static void zone_cb(lv_event_t *e)
{
    int side = (int)(intptr_t)lv_event_get_user_data(e);
    int square_side = s_square_on_left ? 0 : 1;
    bool side_has_square = (side == square_side);
    bool hit = (side_has_square == s_target_is_square);
    if (hit) on_hit(); else on_miss();
}

// Toque bruto — só serve pra roleta de arraste do seletor de sigla
// (kit_ui_sigla_feed_touch ignora tudo enquanto nenhuma caixa é arrastada).
static void on_touch(const kit_input_event_t *ev, void *user_data)
{
    (void)user_data;
    kit_ui_sigla_feed_touch(&s_sigla, ev);
}

// Toque em qualquer lugar do overlay de fim de rodada → vai pro resultado.
static void alert_tap_cb(lv_event_t *e)
{
    (void)e;
    if (s_state != STATE_ALERT) return;
    sfx_click();
    enter_result();
}

static void action_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_state == STATE_IDLE) { start_round(); return; }

    // STATE_RESULT:
    if (s_pending_rank >= 0) {
        const char *sig = kit_ui_sigla_get(&s_sigla);
        hs_insert(hs_table(s_round_inverte), s_score, sig,
                  hs_score_prefix(s_round_inverte), hs_init_prefix(s_round_inverte));
        s_last_initials[0] = sig[0];
        s_last_initials[1] = sig[1];
        s_last_initials[2] = sig[2];
        s_last_initials[3] = 0;
        save_last_initials();
        sync_highscores_view();
        sfx_confirm();
    } else {
        sfx_click();
    }
    show_idle_state();
}

static void on_dur(int i, void *u)
{
    (void)u;
    s_dur_idx = i;
    save_duration();
}

static void on_inv(int i, void *u)
{
    (void)u;
    s_inverte_on = (i == 1);
    save_inverte();
    if (s_state == STATE_IDLE) sync_idle_record();
}

static void on_page(int page, void *u)
{
    (void)u;
    if (page == 3) sync_highscores_view();
}

// --- construção da tela ---------------------------------------------------

// Página 0 — AJUSTE: tempo da partida + Modo Inverte. Rola se não couber.
static void build_page_setup(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, B_PAD, 0);
    lv_obj_set_style_pad_right(p, B_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 24, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(p, 10, 0);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "TEMPO DA PARTIDA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    kit_ui_chips(&s_dur, p, DUR_LABELS, 3, s_dur_idx, s_accent, on_dur, NULL);

    add_label(p, "MODO INVERTE", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    kit_ui_chips(&s_inv, p, INV_LABELS, 2, s_inverte_on ? 1 : 0, s_accent, on_inv, NULL);

    lv_obj_t *hint = add_label(p,
        "Com o Modo Inverte ligado, o alvo tambem pode virar a bola durante\n"
        "a rodada -- um icone acima das formas mostra qual vale. Tem\n"
        "highscore proprio, separado do modo normal.",
        KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, B_CONTENT);
}

// Página 1 — JOGO. Três estados no mesmo tile: parado, jogando, resultado.
static void build_game_idle(lv_obj_t *tile)
{
    s_idle_group = plain_box(tile);
    lv_obj_set_size(s_idle_group, lv_pct(100), lv_pct(100));

    lv_obj_t *group = plain_box(s_idle_group);
    lv_obj_set_size(group, B_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, 12, 0);
    lv_obj_align(group, LV_ALIGN_CENTER, 0, -(B_BTN_H + B_BTN_MARGIN) / 2);

    add_label(group, KIT_ICON_SQUARE, KIT_COLOR_TEXT, &kit_display_44, 0);

    s_idle_record_lbl = add_label(group, "RECORDE: --- 0", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    lv_obj_set_width(s_idle_record_lbl, B_CONTENT);
    lv_obj_set_style_text_align(s_idle_record_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *status = add_label(group, "TOQUE EM COMEÇAR", KIT_COLOR_TEXT, &kit_mono_20, 1);
    lv_obj_set_width(status, B_CONTENT);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_game_playing(lv_obj_t *tile)
{
    s_play_group = plain_box(tile);
    lv_obj_set_size(s_play_group, lv_pct(100), lv_pct(100));

    lv_obj_t *status = plain_box(s_play_group);
    lv_obj_set_size(status, B_CONTENT, B_STATUS_H);
    lv_obj_set_pos(status, B_PAD, 8);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_score_lbl  = add_label(status, "0", KIT_COLOR_TEXT, &kit_mono_26, 1);
    s_time_lbl   = add_label(status, "30S", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    s_record_lbl = add_label(status, "--- 0", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);

    s_stage = plain_box(s_play_group);
    lv_obj_set_size(s_stage, KIT_DISPLAY_WIDTH, B_STAGE_H);
    lv_obj_set_pos(s_stage, 0, B_STATUS_H);

    for (int side = 0; side < 2; side++) {
        lv_obj_t *zone = plain_box(s_stage);
        lv_obj_set_size(zone, B_HALF_W, B_STAGE_H);
        lv_obj_set_pos(zone, side * B_HALF_W, 0);
        lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(zone, zone_cb, LV_EVENT_CLICKED, (void *)(intptr_t)side);
        s_zone[side] = zone;

        lv_obj_t *shape = lv_obj_create(zone);
        lv_obj_set_size(shape, B_SHAPE, B_SHAPE);
        lv_obj_set_style_bg_color(shape, lv_color_hex(KIT_COLOR_BLUE), 0);
        lv_obj_set_style_border_width(shape, 0, 0);
        lv_obj_set_style_pad_all(shape, 0, 0);
        lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(shape);
        s_shape[side] = shape;
    }

    // Linha fina só pra marcar a divisa entre os dois alvos.
    lv_obj_t *divider = plain_box(s_stage);
    lv_obj_set_size(divider, 2, B_STAGE_H);
    lv_obj_set_style_bg_color(divider, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_pos(divider, B_HALF_W - 1, 0);

    s_target_badge = add_label(s_stage, KIT_ICON_SQUARE, KIT_COLOR_TEXT, &kit_mono_26, 0);
    lv_obj_align(s_target_badge, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_add_flag(s_target_badge, LV_OBJ_FLAG_HIDDEN);
}

static void build_game_result(lv_obj_t *tile)
{
    // Precisa rolar: número grande + legenda + seletor de sigla podem passar
    // da altura livre acima do botão fixo em telas pequenas.
    s_result_group = lv_obj_create(tile);
    lv_obj_remove_style_all(s_result_group);
    lv_obj_set_size(s_result_group, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_top(s_result_group, 24, 0);
    lv_obj_set_style_pad_bottom(s_result_group, B_BTN_H + B_BTN_MARGIN + 16, 0);
    lv_obj_set_style_pad_row(s_result_group, 8, 0);
    lv_obj_set_flex_flow(s_result_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_result_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_result_group, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_result_group, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);

    s_result_score = add_label(s_result_group, "0", KIT_COLOR_TEXT, &kit_display_120, 0);
    lv_obj_set_width(s_result_score, B_CONTENT);
    lv_obj_set_style_text_align(s_result_score, LV_TEXT_ALIGN_CENTER, 0);

    s_result_caption = add_label(s_result_group, "RECORDE: --- 0", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    lv_obj_set_width(s_result_caption, B_CONTENT);
    lv_obj_set_style_text_align(s_result_caption, LV_TEXT_ALIGN_CENTER, 0);

    // Seletor de sigla (kit_ui_sigla) — a roleta que nasceu aqui, agora no
    // componente compartilhado. Congela o scroll nos dois eixos durante o
    // arraste (este grupo na vertical, o tileview na horizontal).
    kit_ui_sigla(&s_sigla, s_result_group, s_accent, NULL, NULL);
    kit_ui_sigla_scroll_lock(&s_sigla, s_result_group, LV_DIR_VER);
    kit_ui_sigla_scroll_lock(&s_sigla, s_shell.tv, LV_DIR_HOR);
}

// Overlay de fim de rodada — superfície vermelha cheia (a primitiva de
// "erro/fim"), palavra grande + "TOQUE PARA CONTINUAR". Tocar em qualquer
// ponto segue pro resultado.
static void build_game_alert(lv_obj_t *tile)
{
    s_alert_group = lv_obj_create(tile);
    lv_obj_remove_style_all(s_alert_group);
    lv_obj_set_size(s_alert_group, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_alert_group, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_set_style_bg_opa(s_alert_group, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_alert_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_alert_group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_alert_group, 0);
    lv_obj_add_event_cb(s_alert_group, alert_tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_alert_group, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *col = plain_box(s_alert_group);
    lv_obj_set_size(col, B_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);

    s_alert_lbl = add_label(col, "TEMPO", KIT_COLOR_ON_COLOR, &kit_display_72, 0);
    lv_obj_t *hint = add_label(col, "TOQUE PARA CONTINUAR", KIT_COLOR_ON_COLOR, &kit_mono_20, 2);
    lv_obj_set_width(hint, B_CONTENT);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    build_game_idle(tile);
    build_game_playing(tile);
    build_game_result(tile);
    build_game_alert(tile);
    kit_ui_action_button(&s_action, tile, s_accent, action_btn_cb);
}

// Página 2 — COMO JOGA: regra resumida + a homenagem.
static const char RULES[] =
    "1. Toque em COMECAR e va sempre no QUADRADO -- vale 5 pontos e pode\n"
    "trocar de lado.\n\n"
    "2. Tocar na BOLA ou o tempo zerar acaba a rodada.\n\n"
    "3. Modo Inverte (AJUSTE): as vezes o alvo vira a bola -- um icone acima\n"
    "das formas mostra qual vale.\n\n"
    "4. Entrou no top-5 (um pra cada modo)? Arraste pra cima ou pra baixo em\n"
    "cada caixa pra rolar as letras (ou toque pra avancar uma) e toque em\n"
    "SALVAR.\n\n"
    "Homenagem a Bola/Quadrado, 2017.";

// Página 3 — HIGHSCORES: top-5 do modo normal + top-5 do Modo Inverte.
static void build_hs_section(lv_obj_t *parent, const char *title,
                             lv_obj_t **left_out, lv_obj_t **right_out)
{
    add_label(parent, title, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    for (int i = 0; i < HS_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 16, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        left_out[i]  = add_label(row, "1  ---", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
        right_out[i] = add_label(row, "0", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    }
}

static void build_page_highscores(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, B_PAD, 0);
    lv_obj_set_style_pad_right(p, B_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 24, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(p, 10, 0);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "HIGHSCORES", KIT_COLOR_TEXT, &kit_mono_26, 3);
    build_hs_section(p, "NORMAL", s_hsn_left, s_hsn_right);
    build_hs_section(p, "MODO INVERTE", s_hsi_left, s_hsi_right);
}

// --- ciclo de vida ---------------------------------------------------------
KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);
    load_prefs();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "QUADRADO", s_accent, PAGES);
    kit_ui_shell_tiles(&s_shell, on_page, NULL);
    build_page_setup(s_shell.tiles[0]);
    build_page_game(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);
    build_page_highscores(s_shell.tiles[3]);

    sync_highscores_view();

    // Callback de toque bruto — só usado pela roleta do seletor de sigla.
    // Ver kit_input_api_t: um único callback por Tool.
    if (s_api->input) s_api->input->register_callback(on_touch, NULL);

    kit_ui_shell_open(&s_shell, 1);   // abre no JOGO
    show_idle_state();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    stop_round_timer();
    if (s_api && s_api->input) s_api->input->register_callback(NULL, NULL);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell  = (kit_ui_shell_t){0};
    s_dur    = (kit_ui_chips_t){0};
    s_inv    = (kit_ui_chips_t){0};
    s_action = (kit_ui_action_t){0};
    s_sigla  = (kit_ui_sigla_t){0};

    s_idle_group = s_idle_record_lbl = NULL;
    s_play_group = s_score_lbl = s_time_lbl = s_record_lbl = s_stage = s_target_badge = NULL;
    s_zone[0] = s_zone[1] = s_shape[0] = s_shape[1] = NULL;
    s_alert_group = s_alert_lbl = NULL;
    s_result_group = s_result_score = s_result_caption = NULL;
    for (int i = 0; i < HS_COUNT; i++) {
        s_hsn_left[i] = s_hsn_right[i] = NULL;
        s_hsi_left[i] = s_hsi_right[i] = NULL;
    }
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo (CI / teste de lógica, sem UI) */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Quadrado stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
