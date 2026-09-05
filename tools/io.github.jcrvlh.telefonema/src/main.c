/**
 * @file main.c
 * @brief Telefonema — mini-jogo de reflexo para o KIT.
 *
 * O KIT fica parado na mesa "tocando": alguns toques são trotes (falsos) e um
 * é o toque de verdade, em posição sorteada. Quem "atender" (chacoalhar,
 * apertar a tela, ou o botão PEGAR) durante a janela do toque certo ganha e
 * vê o tempo de reação; atender cedo demais, num trote, ou não atender a
 * tempo faz perder. Migrado do KIT Core (era com.kit.telefonema) para o
 * catálogo — mesma lógica, agora via kit_tool_api.h e chacoalhar por
 * register_shake_callback (Tools externas não têm `primary_action`).
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

// Sem kit_display.h no SDK de Tools — as dimensões do AMOLED são fixas.
#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define T_PAD        16
#define T_CONTENT    (KIT_DISPLAY_WIDTH - 2 * T_PAD)          // 336
#define T_TITLEBAR   88
#define T_CHIP       56
#define T_PAGE_H     (KIT_DISPLAY_HEIGHT - T_TITLEBAR)        // tileview cheio
#define T_GO_H       76
#define T_GO_MARGIN  18
#define T_BTN_BAND   (T_GO_H + T_GO_MARGIN * 2)   // faixa reservada pro botão DENTRO do tile do jogo
#define T_STAGE_H    (T_PAGE_H - T_BTN_BAND)

#define PAGES        3   // 0 ajuste, 1 jogo, 2 como joga

#define T_VARIANTS        3
#define T_PREVIEW_MS      1800
#define T_GAP_MIN_MS      700
#define T_GAP_MAX_MS      2200
#define T_RING_WINDOW_MS  2000

#define TROTE_COUNT  4   // SEM, POUCOS, PADRÃO, MUITOS

typedef enum {
    PHASE_IDLE = 0,   // parado, esperando alguém começar
    PHASE_PREVIEW,    // tocando a referência do toque certo
    PHASE_WAITING,    // rodada em curso (trotes e/ou toque de verdade), silenciosa
    PHASE_RESULT,     // mostrando ganhou/perdeu
} phase_t;

static const int TROTE_MIN[TROTE_COUNT] = { 0, 1, 1, 3 };
static const int TROTE_MAX[TROTE_COUNT] = { 0, 2, 4, 6 };

static const char TL_RULES[] =
    "O KIT fica parado na mesa, no meio da roda.\n\n"
    "1. Toque em COME\xC3\x87" "AR. O KIT toca uma vez o \"toque certo\" para "
    "todo mundo escutar e decorar.\n\n"
    "2. Depois v\xC3\xAAm v\xC3\xA1rios toques: alguns s\xC3\xA3o trotes (parecidos, mas "
    "errados) e um \xC3\xA9 o toque certo de novo, em posi\xC3\xA7\xC3\xA3o sorteada. A "
    "tela fica parada o tempo todo - quem entrega se \xC3\xA9 trote ou o certo "
    "\xC3\xA9 s\xC3\xB3 o ouvido.\n\n"
    "3. Quem pegar o KIT (chacoalhando ou tocando a tela) durante o toque "
    "CERTO ganha, e o tempo de rea\xC3\xA7\xC3\xA3o aparece na tela.\n\n"
    "4. Pegar num trote, pegar antes de qualquer toque, ou deixar passar o "
    "toque certo sem atender \xC3\xA9 derrota na hora (a n\xC3\xA3o ser que o Ajuste "
    "\"SE N\xC3\x83O ATENDER A TEMPO\" esteja em CONTINUA).\n\n"
    "Ajuste a quantidade de trotes e se d\xC3\xA1 pra perder a liga\xC3\xA7\xC3\xA3o na "
    "p\xC3\xA1gina AJUSTE (arraste pra esquerda). Toque em JOGAR DE NOVO (ou "
    "chacoalhe) para sortear outra rodada e passar o KIT adiante.";

// --- API do Runtime --------------------------------------------------------
static const kit_api_table_t *s_api = NULL;
static const kit_api_table_t *api(void) { return s_api; }

// --- estado ----------------------------------------------------------------
static uint32_t s_accent    = KIT_COLOR_RED;
static phase_t  s_phase     = PHASE_IDLE;
static int      s_ring_variant  = 0;   // 0..T_VARIANTS-1, sorteado no COMEÇAR
static bool     s_ring_open = false;   // janela do toque de verdade aberta
static int      s_decoys_before = 0;   // quantos trotes antes do toque real
static int      s_decoys_played = 0;
static uint64_t s_ring_onset_ms = 0;   // time->get_millis() de quando o toque real começou
static lv_timer_t *s_timer = NULL;

static int  s_trote_lvl    = 2;        // índice em TROTE_MIN/MAX — padrão = PADRÃO
static bool s_pode_perder  = true;     // não atender a tempo conta como derrota?

// --- objetos LVGL ---------------------------------------------------------
static lv_obj_t *s_screen   = NULL;
static lv_obj_t *s_tv       = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];
static lv_obj_t *s_lead_in  = NULL;
static lv_obj_t *s_phrase   = NULL;
static lv_obj_t *s_lead_out = NULL;
static lv_obj_t *s_go_btn   = NULL;
static lv_obj_t *s_go_lbl   = NULL;

static lv_obj_t *s_trote_chips[TROTE_COUNT];
static lv_obj_t *s_trote_chip_lbls[TROTE_COUNT];
static lv_obj_t *s_perder_chips[2];
static lv_obj_t *s_perder_chip_lbls[2];

// --- helpers ---------------------------------------------------------------

static uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static uint64_t now_ms(void)
{
    const kit_api_table_t *a = api();
    return (a && a->time) ? a->time->get_millis() : 0;
}

static kit_sfx_t ring_sfx_for_variant(int v)
{
    switch (v) {
    case 0:  return KIT_SFX_TELEFONEMA_RING_A;
    case 1:  return KIT_SFX_TELEFONEMA_RING_B;
    default: return KIT_SFX_TELEFONEMA_RING_C;
    }
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

static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *field_label(lv_obj_t *parent, const char *txt)
{
    return add_label(parent, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

// Atualiza as três linhas do palco. lead_in/lead_out em NULL ficam ocultas.
static void set_stage(const char *lead_in, const char *phrase, uint32_t phrase_color,
                      const char *lead_out)
{
    if (!s_phrase) return;

    if (lead_in && lead_in[0]) {
        lv_label_set_text(s_lead_in, lead_in);
        lv_obj_clear_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_phrase, phrase);
    lv_obj_set_style_text_color(s_phrase, lv_color_hex(phrase_color), 0);

    if (lead_out && lead_out[0]) {
        lv_label_set_text(s_lead_out, lead_out);
        lv_obj_clear_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_phrase_font(const lv_font_t *f)
{
    if (s_phrase) lv_obj_set_style_text_font(s_phrase, f, 0);
}

static void sync_button(void)
{
    if (!s_go_lbl) return;
    switch (s_phase) {
    case PHASE_IDLE:
        lv_label_set_text(s_go_lbl, "COME\xC3\x87" "AR");
        break;
    case PHASE_PREVIEW:
        lv_label_set_text(s_go_lbl, "OUVINDO...");
        break;
    case PHASE_WAITING:
        lv_label_set_text(s_go_lbl, "PEGAR!");
        break;
    case PHASE_RESULT:
        lv_label_set_text(s_go_lbl, "JOGAR DE NOVO");
        break;
    }
}

static void sync_dots(void)
{
    if (!s_tv) return;
    lv_obj_t *act = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < PAGES; i++) {
        bool on = (act == s_tiles[i]);
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(on ? s_accent : KIT_COLOR_LINE), 0);
        lv_obj_set_size(s_dots[i], on ? 20 : 8, 8);
    }
}

static void sync_trote_chips(void)
{
    uint32_t sel_txt = on_accent();
    for (int i = 0; i < TROTE_COUNT; i++) {
        bool sel = (i == s_trote_lvl);
        lv_obj_set_style_bg_color(s_trote_chips[i],
            lv_color_hex(sel ? s_accent : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(s_trote_chip_lbls[i],
            lv_color_hex(sel ? sel_txt : KIT_COLOR_TEXT), 0);
    }
}

static void sync_perder_chips(void)
{
    uint32_t sel_txt = on_accent();
    int sel_i = s_pode_perder ? 0 : 1;
    for (int i = 0; i < 2; i++) {
        bool sel = (i == sel_i);
        lv_obj_set_style_bg_color(s_perder_chips[i],
            lv_color_hex(sel ? s_accent : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(s_perder_chip_lbls[i],
            lv_color_hex(sel ? sel_txt : KIT_COLOR_TEXT), 0);
    }
}

// --- persistência (Storage API — isolada por Tool) -------------------------

static void load_prefs(void)
{
    const kit_api_table_t *a = api();
    if (!a || !a->storage) return;
    int32_t v;
    if (a->storage->get_i32("tel_trote", &v) == KIT_OK && v >= 0 && v < TROTE_COUNT)
        s_trote_lvl = (int)v;
    if (a->storage->get_i32("tel_perde", &v) == KIT_OK)
        s_pode_perder = (v != 0);
}

static void save_prefs(void)
{
    const kit_api_table_t *a = api();
    if (!a || !a->storage) return;
    a->storage->set_i32("tel_trote", s_trote_lvl);
    a->storage->set_i32("tel_perde", s_pode_perder ? 1 : 0);
}

// --- rodada ----------------------------------------------------------------

static void event_tick_cb(lv_timer_t *t);
static void ring_expire_cb(lv_timer_t *t);
static void preview_done_cb(lv_timer_t *t);

static void schedule_next_event(void)
{
    const kit_api_table_t *a = api();
    int gap = (a && a->random) ? (int)a->random->range(T_GAP_MIN_MS, T_GAP_MAX_MS)
                                : (T_GAP_MIN_MS + T_GAP_MAX_MS) / 2;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_timer = lv_timer_create(event_tick_cb, (uint32_t)gap, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}

// Referência antes da rodada: sorteia a variante do toque certo, toca ela
// sozinha e mostra "ESSE É O TOQUE CERTO" bem grande. Some depois de
// T_PREVIEW_MS e cai direto na espera de verdade (start_round).
static void begin_preview(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }

    const kit_api_table_t *a = api();
    s_ring_variant = (a && a->random) ? (int)a->random->range(0, T_VARIANTS - 1) : 0;
    s_phase = PHASE_PREVIEW;

    if (a && a->audio) a->audio->sfx(ring_sfx_for_variant(s_ring_variant));
    set_phrase_font(&kit_display_44);
    set_stage(NULL, "ESSE \xC3\x89 O TOQUE CERTO", s_accent, NULL);
    sync_button();

    s_timer = lv_timer_create(preview_done_cb, T_PREVIEW_MS, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}

static void start_round(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }

    const kit_api_table_t *a = api();
    int lo = TROTE_MIN[s_trote_lvl], hi = TROTE_MAX[s_trote_lvl];
    s_decoys_before = (a && a->random) ? (int)a->random->range(lo, hi) : lo;
    s_decoys_played = 0;
    s_ring_open      = false;
    s_phase          = PHASE_WAITING;

    // Sem feedback visual daqui pra frente: o palco fica parado até alguém
    // atender ou a janela do toque certo fechar — só o ouvido entrega.
    set_phrase_font(&kit_mono_26);
    set_stage(NULL, "FIQUE ATENTO...", KIT_COLOR_TEXT_MUTED, NULL);
    sync_button();
    schedule_next_event();
}

static void preview_done_cb(lv_timer_t *t)
{
    (void)t;
    s_timer = NULL;
    start_round();
}

// Fecha a rodada (vitória ou derrota) e mostra o resultado, bem grande.
// reason: 0 = venceu; 1 = cedo demais; 2 = toque falso; 3 = não atendeu a tempo.
static void finish_round(bool win, uint32_t reaction_ms, int reason)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_ring_open = false;
    s_phase     = PHASE_RESULT;

    set_phrase_font(&kit_display_44);
    const kit_api_table_t *a = api();
    if (win) {
        if (a && a->audio) a->audio->sfx(KIT_SFX_TELEFONEMA_PICKUP);
        char buf[16];
        snprintf(buf, sizeof(buf), "%u MS", (unsigned)reaction_ms);
        set_stage("VOC\xC3\x8A GANHOU", buf, KIT_COLOR_GREEN, NULL);
    } else {
        if (a && a->audio) a->audio->sfx(KIT_SFX_TELEFONEMA_MISS);
        const char *msg = (reason == 1) ? "CEDO DEMAIS!" :
                          (reason == 2) ? "TOQUE FALSO!" : "N\xC3\x83O ATENDEU!";
        set_stage("VOC\xC3\x8A PERDEU", msg, KIT_COLOR_RED, NULL);
    }
    sync_button();
}

// Um evento agendado chegou: ainda falta trote, ou é o toque de verdade.
// Nenhum dos dois mexe na tela (ver comentário em start_round) — só toca.
static void event_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_timer = NULL;
    const kit_api_table_t *a = api();

    if (s_decoys_played < s_decoys_before) {
        s_decoys_played++;
        if (a && a->audio) a->audio->sfx(KIT_SFX_TELEFONEMA_FAKE);
        schedule_next_event();
        return;
    }

    s_ring_open    = true;
    s_ring_onset_ms = now_ms();
    if (a && a->audio) a->audio->sfx(ring_sfx_for_variant(s_ring_variant));

    // Só arma o "não atendeu a tempo" se o Ajuste deixar perder a ligação;
    // em CONTINUA a janela fica aberta até alguém atender.
    if (s_pode_perder) {
        s_timer = lv_timer_create(ring_expire_cb, T_RING_WINDOW_MS, NULL);
        lv_timer_set_repeat_count(s_timer, 1);
    }
}

// Janela do toque de verdade fechou sem ninguém atender.
static void ring_expire_cb(lv_timer_t *t)
{
    (void)t;
    s_timer = NULL;
    finish_round(false, 0, 3);
}

// --- ação principal (chacoalhar / botão / toque na tela) -------------------
// Sem PWR (só built-ins têm primary_action): o chacoalhar é ligado por
// register_shake_callback e o botão/palco cobrem o toque.

static void do_grab(void)
{
    if (!s_screen) return;

    switch (s_phase) {
    case PHASE_IDLE:
    case PHASE_RESULT:
        begin_preview();
        break;

    case PHASE_PREVIEW:
        break;   // ainda ouvindo a referência — ignora

    case PHASE_WAITING:
        if (s_ring_open) {
            uint32_t reaction_ms = (uint32_t)(now_ms() - s_ring_onset_ms);
            finish_round(true, reaction_ms, 0);
        } else {
            int reason = (s_decoys_played == 0) ? 1 : 2;   // cedo demais x trote
            finish_round(false, 0, reason);
        }
        break;
    }
}

static void on_shake(void *user_data)
{
    (void)user_data;
    do_grab();
}

// --- callbacks de UI ---------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    const kit_api_table_t *t = api();
    if (t && t->system) t->system->exit();
}

static void grab_cb(lv_event_t *e)
{
    (void)e;
    do_grab();
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    sync_dots();
}

static void trote_chip_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= TROTE_COUNT) return;
    s_trote_lvl = i;
    sync_trote_chips();
    save_prefs();
}

static void perder_chip_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_pode_perder = (i == 0);   // 0 = "PERDE", 1 = "CONTINUA"
    sync_perder_chips();
    save_prefs();
}

// --- construção da tela -------------------------------------------------

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, T_CHIP, T_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, T_PAD, 16);

    lv_obj_t *g = add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(g);

    lv_obj_t *title = add_label(s_screen, "TELEFONEMA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, T_PAD + T_CHIP + 12, 30);

    // Indicador de página (3 pontos: AJUSTE / JOGO / COMO JOGA)
    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -T_PAD, 40);
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

// Grade de "chips" (pílulas), no máximo 2 por linha e bem altas — área de
// toque generosa.
#define CHIP_H        84
#define CHIP_PER_ROW  2

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
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
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

// Página AJUSTE: quantidade de trotes e se dá pra perder a ligação.
static void build_page_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, T_PAD, 0);
    lv_obj_set_style_pad_right(p, T_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 22, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *sec_trote = plain_box(p);
    lv_obj_set_size(sec_trote, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec_trote, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec_trote, 9, 0);
    field_label(sec_trote, "TROTES");
    static const char *TROTE_LABELS[TROTE_COUNT] = { "SEM", "POUCOS", "PADR\xC3\x83O", "MUITOS" };
    build_chip_grid(sec_trote, TROTE_LABELS, TROTE_COUNT, trote_chip_cb,
                    s_trote_chips, s_trote_chip_lbls);

    lv_obj_t *sec_perder = plain_box(p);
    lv_obj_set_size(sec_perder, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec_perder, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec_perder, 9, 0);
    field_label(sec_perder, "SE N\xC3\x83O ATENDER A TEMPO");
    static const char *PERDER_LABELS[2] = { "PERDE", "CONTINUA" };
    build_chip_grid(sec_perder, PERDER_LABELS, 2, perder_chip_cb,
                    s_perder_chips, s_perder_chip_lbls);
}

// Página JOGO: palco (toque em qualquer lugar conta como "atender", mesma
// lógica do botão/chacoalhar) + botão fixo no rodapé DESTE tile.
static void build_game_page(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *stage = lv_obj_create(tile);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, KIT_DISPLAY_WIDTH, T_STAGE_H);
    lv_obj_set_pos(stage, 0, 0);
    lv_obj_clear_flag(stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stage, grab_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(stage);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);   // toque cai no palco
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_center(col);

    s_lead_in = add_label(col, "", KIT_COLOR_TEXT, &kit_mono_20, 3);
    lv_obj_add_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);

    s_phrase = add_label(col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_26, 2);
    lv_label_set_long_mode(s_phrase, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_phrase, T_CONTENT);
    lv_obj_set_height(s_phrase, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_phrase, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_phrase, 6, 0);
    lv_obj_set_style_pad_bottom(s_phrase, 10, 0);

    s_lead_out = add_label(col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);
    lv_obj_add_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);

    // Botão fixo no rodapé DESTE tile (não da tela): some sozinho ao trocar
    // de página, sem deixar tarja preta na Ajuste/Como Joga.
    s_go_btn = lv_obj_create(tile);
    lv_obj_set_size(s_go_btn, T_CONTENT, T_GO_H);
    lv_obj_set_style_radius(s_go_btn, T_GO_H / 2, 0);
    lv_obj_set_style_border_width(s_go_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_go_btn, 0, 0);
    lv_obj_set_style_pad_all(s_go_btn, 0, 0);
    lv_obj_set_style_bg_color(s_go_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_clear_flag(s_go_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_go_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_go_btn, 8);
    lv_obj_align(s_go_btn, LV_ALIGN_BOTTOM_MID, 0, -T_GO_MARGIN);
    lv_obj_add_event_cb(s_go_btn, grab_cb, LV_EVENT_CLICKED, NULL);

    s_go_lbl = add_label(s_go_btn, "COME\xC3\x87" "AR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_go_lbl);
}

// Página COMO JOGA: regras, corpo rolável, tela inteira (sem botão fixo).
static void build_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, T_PAD, 0);
    lv_obj_set_style_pad_right(p, T_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, TL_RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, T_CONTENT);
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, KIT_DISPLAY_WIDTH, T_PAGE_H);
    lv_obj_set_pos(s_tv, 0, T_TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);

    build_page_ajuste(s_tiles[0]);
    build_game_page(s_tiles[1]);
    build_help(s_tiles[2]);
}

// --- ciclo de vida -------------------------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    printf("[Telefonema] tool_init\n");
    s_api = ctx ? ctx->api : NULL;

    s_accent         = KIT_COLOR_RED;
    s_phase          = PHASE_IDLE;
    s_ring_variant   = 0;
    s_ring_open      = false;
    s_decoys_before  = 0;
    s_decoys_played  = 0;
    s_trote_lvl      = 2;
    s_pode_perder    = true;
    load_prefs();

    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(on_shake, NULL);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();

    // Home da Tool é a página do JOGO (índice 1) — AJUSTE fica à esquerda,
    // COMO JOGA à direita.
    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);

    set_phrase_font(&kit_mono_26);
    set_stage(NULL, "DEIXE NA MESA. TOQUE EM COME\xC3\x87" "AR", KIT_COLOR_TEXT_MUTED, NULL);
    sync_button();
    sync_trote_chips();
    sync_perder_chips();
    sync_dots();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Telefonema] tool_destroy\n");
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);
    s_phase     = PHASE_IDLE;
    s_ring_open = false;

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < TROTE_COUNT; i++) { s_trote_chips[i] = NULL; s_trote_chip_lbls[i] = NULL; }
    for (int i = 0; i < 2; i++) { s_perder_chips[i] = NULL; s_perder_chip_lbls[i] = NULL; }
    s_lead_in = s_phrase = s_lead_out = s_go_btn = s_go_lbl = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo para testes e CI */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Telefonema stub] tool_init — compila nativo; UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Telefonema stub] tool_destroy\n");
}

#endif /* KIT_SDK_STUBS */
