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
 * Ao entrar no top-5, 3 caixas de letra pedem a inicial — arraste pra cima/
 * baixo em cada caixa pra rolar as letras (ou toque pra avançar uma),
 * pré-preenchidas com a última sigla usada, com botão pra redefinir. O
 * arraste usa o callback de toque bruto do input (s_api->input), já que o
 * SDK de Tools não expõe um widget de rolagem pronto.
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

// ---------------------------------------------------------------------------
// Layout (368 × 448 — espelha as métricas do Estouro/Telefonema)
// ---------------------------------------------------------------------------
#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define B_PAD        16
#define B_CONTENT    (KIT_DISPLAY_WIDTH - 2 * B_PAD)   // 336
#define B_TITLEBAR   88
#define B_CHIP       56
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
#define LETTER_DRAG_STEP_PX 24   // px de arraste por letra — sensação de roleta

static const int32_t DURATIONS[3] = { 15, 30, 60 };
static const char *const DUR_LABELS[3] = { "15S", "30S", "60S" };
static const char *const INV_LABELS[2] = { "DESLIGADO", "LIGADO" };

typedef enum { STATE_IDLE, STATE_PLAYING, STATE_RESULT } ui_state_t;

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
static char s_edit_letters[3]  = { 'A', 'A', 'A' };

// Arraste nas caixas de letra (roleta) — ver on_touch().
static int s_drag_box    = -1;
static int s_drag_last_y = 0;
static int s_drag_accum  = 0;

// --- objetos LVGL (todos zerados em tool_destroy) ---------------------
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];
static lv_timer_t *s_round_timer = NULL;

// AJUSTE
static lv_obj_t *s_dur_chips[3], *s_dur_lbls[3];
static lv_obj_t *s_inv_chips[2], *s_inv_lbls[2];

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

static lv_obj_t *s_result_group   = NULL;
static lv_obj_t *s_result_score   = NULL;
static lv_obj_t *s_result_caption = NULL;
static lv_obj_t *s_letters_row    = NULL;
static lv_obj_t *s_letter_box[3];
static lv_obj_t *s_letter_lbl[3];
static lv_obj_t *s_reset_btn      = NULL;

// Botão de ação — compartilhado entre IDLE ("COMEÇAR") e RESULTADO
// ("SALVAR"/"JOGAR DE NOVO"). Filho do tile, fixo no rodapé, escondido
// durante o estado "jogando".
static lv_obj_t *s_action_btn     = NULL;
static lv_obj_t *s_action_btn_lbl = NULL;

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

// Container invisível de layout — NÃO usar para algo que precisa rolar
// (remove a flag SCROLLABLE; ver build_page_setup/help/build_game_result,
// que criam o próprio container quando precisam de scroll).
static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
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

static void show_idle_state(void)
{
    s_state = STATE_IDLE;
    sync_idle_record();
    lv_obj_remove_flag(s_idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_play_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_action_btn_lbl, "COMEÇAR");
    lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
}

static void show_play_state(void)
{
    s_state = STATE_PLAYING;
    lv_obj_add_flag(s_idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_play_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
}

static void show_result_state(void)
{
    s_state = STATE_RESULT;
    lv_obj_add_flag(s_idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_play_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_result_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
}

static void show_letters(bool on)
{
    if (on) {
        lv_obj_remove_flag(s_letters_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_reset_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_letters_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_reset_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sync_letter_boxes(void)
{
    for (int k = 0; k < 3; k++) {
        char t[2] = { s_edit_letters[k], 0 };
        lv_label_set_text(s_letter_lbl[k], t);
    }
}

// Avança (dir>0) ou volta (dir<0) uma letra na caixa k — usado tanto pelo
// toque simples quanto pelo arraste (roleta).
static void step_letter(int k, int dir)
{
    char c = s_edit_letters[k];
    if (dir > 0) c = (c == 'Z') ? 'A' : (char)(c + 1);
    else         c = (c == 'A') ? 'Z' : (char)(c - 1);
    s_edit_letters[k] = c;
    sync_letter_boxes();
    sfx_click();
}

// Pré-preenche o editor com a última sigla usada (se for válida); senão AAA.
static void prefill_edit_letters(void)
{
    bool valid = true;
    for (int k = 0; k < 3; k++) if (s_last_initials[k] < 'A' || s_last_initials[k] > 'Z') valid = false;
    for (int k = 0; k < 3; k++) s_edit_letters[k] = valid ? s_last_initials[k] : 'A';
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
    show_letters(qualifies);

    if (qualifies) {
        lv_label_set_text(s_result_caption, s_pending_rank == 0 ? "RECORDE NOVO!" : "ENTROU NO TOP 5!");
        prefill_edit_letters();
        sync_letter_boxes();
        lv_label_set_text(s_action_btn_lbl, "SALVAR");
    } else {
        char buf[40];
        snprintf(buf, sizeof buf, "RECORDE: %s %d", t[0].initials, t[0].score);
        lv_label_set_text(s_result_caption, buf);
        lv_label_set_text(s_action_btn_lbl, "JOGAR DE NOVO");
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
        enter_result();
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
    enter_result();
}

// --- callbacks -----------------------------------------------------------
static void back_cb(lv_event_t *e) { (void)e; if (s_api && s_api->system) s_api->system->exit(); }

static void zone_cb(lv_event_t *e)
{
    int side = (int)(intptr_t)lv_event_get_user_data(e);
    int square_side = s_square_on_left ? 0 : 1;
    bool side_has_square = (side == square_side);
    bool hit = (side_has_square == s_target_is_square);
    if (hit) on_hit(); else on_miss();
}

static void letter_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    step_letter(k, +1);
}

// Roleta: o dedo pousa numa caixa (PRESSED) — LVGL já sabe em qual, sem eu
// precisar calcular coordenada nenhuma. Enquanto durar o toque, o stream de
// posição do input bruto (on_touch) acumula o deslocamento vertical e vai
// avançando/voltando a letra a cada LETTER_DRAG_STEP_PX arrastados.
//
// A flag SCROLLABLE na própria caixa (ver build_game_result) já devia
// bastar pra ela "absorver" o arraste, mas trava-se o scroll do
// result_group aqui também, de propósito: sem isso, se a rolagem da tela
// vazar durante o arraste da letra, a rolagem em si vira outro desafio pro
// jogador — e o ponto aqui é escolher a sigla, não lutar com o scroll.
static void letter_pressed_cb(lv_event_t *e)
{
    s_drag_box = (int)(intptr_t)lv_event_get_user_data(e);
    s_drag_accum = 0;
    s_drag_last_y = -1;   // -1 = ainda sem amostra; a próxima vira a referência
    lv_obj_set_scroll_dir(s_result_group, LV_DIR_NONE);
}

static void letter_released_cb(lv_event_t *e)
{
    (void)e;
    s_drag_box = -1;
    lv_obj_set_scroll_dir(s_result_group, LV_DIR_VER);
}

static void on_touch(const kit_input_event_t *ev, void *user_data)
{
    (void)user_data;
    if (ev->type != KIT_INPUT_TOUCH_DOWN || s_drag_box < 0) return;
    if (s_drag_last_y < 0) { s_drag_last_y = ev->y; return; }

    s_drag_accum += (ev->y - s_drag_last_y);
    s_drag_last_y = ev->y;

    // Arrastar pra CIMA (y diminuindo) avança a letra; pra BAIXO, volta —
    // como girar uma roleta com o dedo.
    while (s_drag_accum <= -LETTER_DRAG_STEP_PX) { step_letter(s_drag_box, +1); s_drag_accum += LETTER_DRAG_STEP_PX; }
    while (s_drag_accum >=  LETTER_DRAG_STEP_PX) { step_letter(s_drag_box, -1); s_drag_accum -= LETTER_DRAG_STEP_PX; }
}

static void reset_letters_cb(lv_event_t *e)
{
    (void)e;
    s_edit_letters[0] = s_edit_letters[1] = s_edit_letters[2] = 'A';
    sync_letter_boxes();
    sfx_click();
}

static void action_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_state == STATE_IDLE) { start_round(); return; }

    // STATE_RESULT:
    if (s_pending_rank >= 0) {
        hs_insert(hs_table(s_round_inverte), s_score, s_edit_letters,
                  hs_score_prefix(s_round_inverte), hs_init_prefix(s_round_inverte));
        s_last_initials[0] = s_edit_letters[0];
        s_last_initials[1] = s_edit_letters[1];
        s_last_initials[2] = s_edit_letters[2];
        s_last_initials[3] = 0;
        save_last_initials();
        sync_highscores_view();
        sfx_confirm();
    } else {
        sfx_click();
    }
    show_idle_state();
}

static void sync_chip_selection(lv_obj_t **chips, lv_obj_t **lbls, int count, int sel)
{
    uint32_t sel_txt = on_accent();
    for (int i = 0; i < count; i++) {
        bool s = (i == sel);
        lv_obj_set_style_bg_color(chips[i], lv_color_hex(s ? s_accent : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(lbls[i], lv_color_hex(s ? sel_txt : KIT_COLOR_TEXT), 0);
    }
}

static void duration_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_dur_idx = i;
    sync_chip_selection(s_dur_chips, s_dur_lbls, 3, s_dur_idx);
    save_duration();
    sfx_click();
}

static void inverte_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_inverte_on = (i == 1);
    sync_chip_selection(s_inv_chips, s_inv_lbls, 2, i);
    save_inverte();
    if (s_state == STATE_IDLE) sync_idle_record();
    sfx_click();
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    int act = 0;
    lv_obj_t *t = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < PAGES; i++) if (s_tiles[i] == t) act = i;
    for (int i = 0; i < PAGES; i++)
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(i == act ? s_accent : KIT_COLOR_LINE), 0);
    if (act == 3) sync_highscores_view();
}

// --- construção da tela ---------------------------------------------------
static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, B_CHIP, B_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, B_PAD, 16);
    lv_obj_center(add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    lv_obj_t *title = add_label(s_screen, "QUADRADO", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, B_PAD + B_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -B_PAD, 40);
    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        s_dots[i] = d;
    }
}

// Chip grid genérico (2 por linha) — mesmo padrão do io.github.jcrvlh.telefonema.
#define CHIP_H       84
#define CHIP_PER_ROW 2

static void build_chip_grid(lv_obj_t *parent, const char *const *labels, int count,
                            lv_event_cb_t cb, lv_obj_t **out_chips, lv_obj_t **out_lbls)
{
    lv_obj_t *wrap = plain_box(parent);
    lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wrap, 10, 0);

    int idx = 0;
    while (idx < count) {
        lv_obj_t *row = plain_box(wrap);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 10, 0);

        int in_row = (count - idx < CHIP_PER_ROW) ? (count - idx) : CHIP_PER_ROW;
        for (int k = 0; k < in_row; k++, idx++) {
            lv_obj_t *c = lv_obj_create(row);
            lv_obj_set_height(c, CHIP_H);
            lv_obj_set_flex_grow(c, 1);
            lv_obj_set_style_bg_color(c, lv_color_hex(KIT_COLOR_SURFACE), 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(c, 0, 0);
            lv_obj_set_style_radius(c, 18, 0);
            lv_obj_set_style_pad_all(c, 0, 0);
            lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(c, 6);
            lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

            lv_obj_t *l = add_label(c, labels[idx], KIT_COLOR_TEXT, &kit_mono_20, 1);
            lv_obj_center(l);

            out_chips[idx] = c;
            out_lbls[idx]  = l;
        }
    }
}

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
    build_chip_grid(p, DUR_LABELS, 3, duration_cb, s_dur_chips, s_dur_lbls);

    add_label(p, "MODO INVERTE", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    build_chip_grid(p, INV_LABELS, 2, inverte_cb, s_inv_chips, s_inv_lbls);

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
    // Precisa rolar: número grande + legenda + caixas de letra + redefinir
    // podem passar da altura livre acima do botão fixo em telas pequenas.
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

    s_letters_row = plain_box(s_result_group);
    lv_obj_set_size(s_letters_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_letters_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_letters_row, 12, 0);
    lv_obj_set_style_pad_top(s_letters_row, 4, 0);
    for (int k = 0; k < 3; k++) {
        lv_obj_t *box = lv_obj_create(s_letters_row);
        lv_obj_set_size(box, 92, 96);
        lv_obj_set_style_bg_color(box, lv_color_hex(KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_radius(box, 16, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        // SCROLLABLE (mesmo sem conteúdo pra rolar) faz a caixa absorver o
        // arraste em vez de repassar pro result_group (que rola vertical) —
        // sem isso, arrastar numa caixa rolaria a tela inteira junto.
        lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(box, letter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        lv_obj_add_event_cb(box, letter_pressed_cb, LV_EVENT_PRESSED, (void *)(intptr_t)k);
        lv_obj_add_event_cb(box, letter_released_cb, LV_EVENT_RELEASED, (void *)(intptr_t)k);
        lv_obj_t *l = add_label(box, "A", KIT_COLOR_TEXT, &kit_display_72, 0);
        lv_obj_center(l);
        s_letter_box[k] = box;
        s_letter_lbl[k] = l;
    }

    s_reset_btn = lv_obj_create(s_result_group);
    lv_obj_set_size(s_reset_btn, LV_SIZE_CONTENT, 56);
    lv_obj_set_style_pad_hor(s_reset_btn, 24, 0);
    lv_obj_set_style_bg_color(s_reset_btn, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(s_reset_btn, 0, 0);
    lv_obj_set_style_radius(s_reset_btn, 28, 0);
    lv_obj_remove_flag(s_reset_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_reset_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_reset_btn, 10);
    lv_obj_add_event_cb(s_reset_btn, reset_letters_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(add_label(s_reset_btn, "REDEFINIR", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2));
}

// Botão de ação — filho do tile (não de nenhum dos três grupos), fixo no
// rodapé, fora do scroll. Compartilhado entre IDLE e RESULTADO (troca de
// rótulo e ação conforme s_state); escondido durante o jogo.
static void build_game_action_btn(lv_obj_t *tile)
{
    s_action_btn = lv_obj_create(tile);
    lv_obj_set_size(s_action_btn, B_CONTENT, B_BTN_H);
    lv_obj_set_style_radius(s_action_btn, B_BTN_H / 2, 0);
    lv_obj_set_style_border_width(s_action_btn, 0, 0);
    lv_obj_set_style_pad_all(s_action_btn, 0, 0);
    lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_action_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_action_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_action_btn, 8);
    lv_obj_align(s_action_btn, LV_ALIGN_BOTTOM_MID, 0, -B_BTN_MARGIN);
    lv_obj_add_event_cb(s_action_btn, action_btn_cb, LV_EVENT_CLICKED, NULL);
    s_action_btn_lbl = add_label(s_action_btn, "COMEÇAR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_action_btn_lbl);
}

static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    build_game_idle(tile);
    build_game_playing(tile);
    build_game_result(tile);
    build_game_action_btn(tile);
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

static void build_page_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, B_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, B_CONTENT);
}

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

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, KIT_DISPLAY_WIDTH, B_PAGE_H);
    lv_obj_set_pos(s_tv, 0, B_TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);
    s_tiles[3] = lv_tileview_add_tile(s_tv, 3, 0, LV_DIR_HOR);
    build_page_setup(s_tiles[0]);
    build_page_game(s_tiles[1]);
    build_page_help(s_tiles[2]);
    build_page_highscores(s_tiles[3]);
}

// --- ciclo de vida ---------------------------------------------------------
KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    load_prefs();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();

    sync_chip_selection(s_dur_chips, s_dur_lbls, 3, s_dur_idx);
    sync_chip_selection(s_inv_chips, s_inv_lbls, 2, s_inverte_on ? 1 : 0);
    sync_highscores_view();

    // Callback de toque bruto — só usado pelo arraste nas caixas de letra
    // (on_touch ignora tudo enquanto s_drag_box < 0, ou seja, fora do
    // editor de sigla). Ver kit_input_api_t: um único callback por Tool.
    if (s_api->input) s_api->input->register_callback(on_touch, NULL);

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   // abre no JOGO
    show_idle_state();
    tv_changed_cb(NULL);

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    stop_round_timer();
    if (s_api && s_api->input) s_api->input->register_callback(NULL, NULL);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }
    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    s_dur_chips[0] = s_dur_chips[1] = s_dur_chips[2] = NULL;
    s_dur_lbls[0]  = s_dur_lbls[1]  = s_dur_lbls[2]  = NULL;
    s_inv_chips[0] = s_inv_chips[1] = NULL;
    s_inv_lbls[0]  = s_inv_lbls[1]  = NULL;
    s_idle_group = s_idle_record_lbl = NULL;
    s_play_group = s_score_lbl = s_time_lbl = s_record_lbl = s_stage = s_target_badge = NULL;
    s_zone[0] = s_zone[1] = s_shape[0] = s_shape[1] = NULL;
    s_result_group = s_result_score = s_result_caption = NULL;
    s_letters_row = s_reset_btn = s_action_btn = s_action_btn_lbl = NULL;
    for (int k = 0; k < 3; k++) { s_letter_box[k] = NULL; s_letter_lbl[k] = NULL; }
    for (int i = 0; i < HS_COUNT; i++) {
        s_hsn_left[i] = s_hsn_right[i] = NULL;
        s_hsi_left[i] = s_hsi_right[i] = NULL;
    }
    s_drag_box = -1;
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
