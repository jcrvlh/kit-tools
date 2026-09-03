/**
 * @file main.c
 * @brief Pavio — mini-jogo de mesa para o KIT.
 *
 * Mini-jogo de mesa: o KIT acende um pavio com tempo ESCONDIDO e mostra uma
 * sílaba grande no centro. Cada jogador fala em voz alta uma palavra que
 * contenha aquela sílaba e passa o aparelho adiante — antes que exploda!
 * Quando o pavio acaba, BUM: quem estiver com o KIT na mão perde a rodada.
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
// Layout (368 × 448 — espelha as métricas da Bingo)
// ---------------------------------------------------------------------------
#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define X_PAD        16
#define X_CONTENT    (KIT_DISPLAY_WIDTH - 2 * X_PAD)              // 336
#define X_TITLEBAR   88
#define X_PAGE_H     (KIT_DISPLAY_HEIGHT - X_TITLEBAR)            // 360
#define X_CHIP       56
#define X_GO_H       76
#define X_GO_MARGIN  18
#define PAGES        3         // AJUSTE · JOGO · COMO JOGA

#define PULSE_MS     100
#define LIGHT_MS     750        // "acendendo o pavio": passe travado

// Pulso visual: a banda (s_flash) fica SEMPRE opaca e é a COR que sobe de preto
// até PV_FLASH_HOT — opaco redesenha limpo no buffer parcial.
#define PV_FLASH_HOT       0xD8482F   // vermelho da batida no auge
#define PV_FLASH_DIP_PCT   66         // fase baixa do pulso (66% do brilho cheio)

// Explosão: um SFX só (KIT_SFX_PAVIO_BOOM). O timer s_boom só faz o strobe.
#define BOOM_STEP_MS 90
#define BOOM_STEPS   6

#define K_PAVIO   "pv_faixa"
#define K_SILABA  "pv_silaba"
#define K_DECK    "pv_deck"

// Faixas de pavio (ms) — o tempo real é sorteado dentro delas e nunca aparece.
static const uint32_t PAVIO_MIN[3] = { 12000, 22000, 40000 };
static const uint32_t PAVIO_MAX[3] = { 28000, 48000, 75000 };
static const char *const PAVIO_LBL[3] = { "CURTO", "M\xC3\x89""DIO", "LONGO" };

#define SIL_TROCA  0   // troca a cada passe (mais fácil)
#define SIL_FIXA   1   // fixa na rodada (fica difícil conforme as palavras acabam)

#define DECK_FACIL 0
#define DECK_TUDO  1

// Sílabas — só glifos de kit_display_72 (" - 0-9 A-Z Ã Ç Õ"), CAIXA ALTA.
// FÁCIL: começos de sílaba que puxam centenas de palavras.
static const char *const SIL_FACIL[] = {
    "BA","BE","BI","BO","CA","CO","DA","DE","DO","FA","FE","FO","GA","GO",
    "LA","LE","LI","LO","MA","ME","MI","MO","NA","NO","PA","PE","PI","PO",
    "RA","RE","RI","RO","SA","SE","SO","TA","TE","TI","TO","VA","VE","VI",
};
#define SIL_FACIL_N ((int)(sizeof(SIL_FACIL) / sizeof(SIL_FACIL[0])))

// TUDO: FÁCIL + encontros consonantais, dígrafos e sílabas nasais/travadas —
// bem mais difícil de achar palavra na hora.
static const char *const SIL_DIFICIL[] = {
    "BRA","BRO","CRA","CLA","CHA","CHE","CHO","DRA","FLA","FRA","GLO","GRA",
    "GRI","LHA","LHO","NHA","NHO","PLA","PRA","PRO","TRA","TRO","PRE","CRE",
    "\xC3\x87\xC3\x83O","S\xC3\x83O","T\xC3\x83O","M\xC3\x83O","P\xC3\x83O",
    "N\xC3\x83O","\xC3\x95""ES","V\xC3\x83O",
};
#define SIL_DIFICIL_N ((int)(sizeof(SIL_DIFICIL) / sizeof(SIL_DIFICIL[0])))

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
typedef enum { ST_IDLE, ST_RUN, ST_BOOM } pv_state_t;

static const kit_api_table_t *s_api = NULL;

static uint32_t   s_accent = KIT_COLOR_RED;
static pv_state_t s_state  = ST_IDLE;

static int s_pavio  = 1;            // 0/1/2
static int s_silaba = SIL_TROCA;
static int s_deck   = DECK_FACIL;

static uint64_t s_deadline         = 0;    // millis em que o pavio zera
static uint32_t s_total_ms         = 0;    // tempo total sorteado da rodada
static uint64_t s_lit_at           = 0;    // millis em que o pavio foi aceso
static int      s_intensity_1000   = 0;    // 0 (calmo) → 1000 (prestes a explodir)
static int      s_sil_idx          = -1;   // sílaba atual (índice no pool)
static int      s_pulse_ph         = 0;
static int      s_boom_step        = 0;
static int      s_ring_bw          = -1;   // degrau atual da moldura
static int      s_sil_lit          = -1;   // degrau atual do clareamento da sílaba

static lv_timer_t *s_burn  = NULL;  // período fixo: intensidade + tensão + pulso
static lv_timer_t *s_boom  = NULL;

// ---------------------------------------------------------------------------
// Objetos LVGL
// ---------------------------------------------------------------------------
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv     = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];

// Página 0 — AJUSTE
static lv_obj_t *s_pavio_pills[3];   static lv_obj_t *s_pavio_lbls[3];
static lv_obj_t *s_silaba_pills[2];  static lv_obj_t *s_silaba_lbls[2];
static lv_obj_t *s_deck_pills[2];    static lv_obj_t *s_deck_lbls[2];

// Página 1 — JOGO
static lv_obj_t *s_flash  = NULL;    // banda vermelha atrás da sílaba (pulso)
static lv_obj_t *s_ring   = NULL;    // moldura vermelha (pulso)
static lv_obj_t *s_kicker = NULL;    // "SÍLABA"
static lv_obj_t *s_sil    = NULL;    // a sílaba — kit_display_72
static lv_obj_t *s_go_btn = NULL;
static lv_obj_t *s_go_lbl = NULL;

// Overlay — EXPLODIU
static lv_obj_t *s_boom_ov  = NULL;
static lv_obj_t *s_boom_new = NULL;

// ---------------------------------------------------------------------------
// Helpers da API do KIT
// ---------------------------------------------------------------------------

static inline uint64_t millis(void)
{
    return (s_api && s_api->time) ? s_api->time->get_millis() : 0;
}

static inline void sfx(kit_sfx_t s)
{
    if (s_api && s_api->audio) s_api->audio->sfx(s);
}

static inline void fuse(int16_t tension)
{
    if (s_api && s_api->audio) s_api->audio->fuse(tension);
}

static inline void keep_awake(bool on)
{
    if (s_api && s_api->power) s_api->power->keep_awake(on);
}

static int rnd(int min, int max)
{
    if (min >= max) return min;
    if (s_api && s_api->random)
        return s_api->random->range(min, max);
    return min;
}

// Interpolação RGB 100% inteira em escala de ponto fixo (0..1000).
static uint32_t lerp_rgb(uint32_t a, uint32_t b, int t_1000)
{
    if (t_1000 <= 0) return a;
    if (t_1000 >= 1000) return b;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + ((br - ar) * t_1000) / 1000;
    int g = ag + ((bg - ag) * t_1000) / 1000;
    int bl = ab + ((bb - ab) * t_1000) / 1000;
    return ((uint32_t)(r & 0xFF) << 16) | ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(bl & 0xFF);
}

static inline uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, uint32_t color,
                           const lv_font_t *font, int tracking)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    if (tracking) lv_obj_set_style_text_letter_space(l, tracking, 0);
    return l;
}

// ---------------------------------------------------------------------------
// Baralho de sílabas
// ---------------------------------------------------------------------------

static void pick_syllable(void)
{
    int pool_n = (s_deck == DECK_TUDO) ? (SIL_FACIL_N + SIL_DIFICIL_N) : SIL_FACIL_N;
    if (pool_n <= 0) return;

    int next;
    int guard = 16;
    do {
        next = rnd(0, pool_n - 1);
    } while (next == s_sil_idx && pool_n > 1 && --guard > 0);
    s_sil_idx = next;

    const char *str;
    if (s_sil_idx < SIL_FACIL_N) {
        str = SIL_FACIL[s_sil_idx];
    } else {
        str = SIL_DIFICIL[s_sil_idx - SIL_FACIL_N];
    }
    if (s_sil) lv_label_set_text(s_sil, str);
}

// ---------------------------------------------------------------------------
// Persistência
// ---------------------------------------------------------------------------

static void save_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    s_api->storage->set_i32(K_PAVIO, s_pavio);
    s_api->storage->set_i32(K_SILABA, s_silaba);
    s_api->storage->set_i32(K_DECK, s_deck);
}

static void load_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    int32_t v;
    if (s_api->storage->get_i32(K_PAVIO, &v) == KIT_OK && v >= 0 && v <= 2) s_pavio = (int)v;
    if (s_api->storage->get_i32(K_SILABA, &v) == KIT_OK && (v == SIL_TROCA || v == SIL_FIXA)) s_silaba = (int)v;
    if (s_api->storage->get_i32(K_DECK, &v) == KIT_OK && (v == DECK_FACIL || v == DECK_TUDO)) s_deck = (int)v;
}

// ---------------------------------------------------------------------------
// Sincronização de UI
// ---------------------------------------------------------------------------

static void sync_dots(void)
{
    if (!s_tv) return;
    lv_obj_t *cur = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < PAGES; i++) {
        if (!s_dots[i]) continue;
        bool active = (s_tiles[i] == cur);
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(active ? KIT_COLOR_TEXT : KIT_COLOR_LINE), 0);
    }
}

static void set_pill_state(lv_obj_t *pill, lv_obj_t *lbl, bool sel)
{
    if (!pill || !lbl) return;
    lv_obj_set_style_bg_color(pill,
        lv_color_hex(sel ? s_accent : KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(lbl,
        lv_color_hex(sel ? on_accent() : KIT_COLOR_TEXT), 0);
}

static void sync_segs(void)
{
    for (int i = 0; i < 3; i++)
        set_pill_state(s_pavio_pills[i], s_pavio_lbls[i], s_pavio == i);
    for (int i = 0; i < 2; i++) {
        set_pill_state(s_silaba_pills[i], s_silaba_lbls[i], s_silaba == i);
        set_pill_state(s_deck_pills[i],   s_deck_lbls[i],   s_deck == i);
    }
}

static void reset_visuals(void)
{
    if (s_flash) {
        lv_obj_set_style_bg_color(s_flash, lv_color_hex(KIT_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(s_flash, LV_OPA_TRANSP, 0);
    }
    if (s_ring) {
        lv_obj_set_style_border_width(s_ring, 0, 0);
        lv_obj_set_style_border_opa(s_ring, LV_OPA_TRANSP, 0);
        s_ring_bw = -1;
    }
    if (s_sil) {
        lv_obj_set_style_text_color(s_sil, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_obj_set_style_translate_x(s_sil, 0, 0);
        lv_obj_set_style_translate_y(s_sil, 0, 0);
        s_sil_lit = -1;
    }
}

static void sync_stage(void)
{
    bool run = (s_state == ST_RUN);

    if (s_kicker) {
        lv_obj_set_style_text_color(s_kicker,
            lv_color_hex(run ? KIT_COLOR_TEXT : KIT_COLOR_TEXT_MUTED), 0);
    }
    if (s_sil && s_state == ST_IDLE) {
        lv_label_set_text(s_sil, "PRONTO");
        lv_obj_set_style_text_color(s_sil, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_obj_set_style_translate_x(s_sil, 0, 0);
        lv_obj_set_style_translate_y(s_sil, 0, 0);
    } else if (run && s_sil) {
        lv_obj_set_style_text_color(s_sil, lv_color_hex(KIT_COLOR_TEXT), 0);
    }

    if (s_go_lbl) {
        lv_label_set_text(s_go_lbl, run ? "PASSEI" : "ACENDER PAVIO");
        lv_obj_invalidate(s_go_btn);
    }
    if (s_go_btn) {
        if (s_state == ST_BOOM) lv_obj_add_flag(s_go_btn, LV_OBJ_FLAG_HIDDEN);
        else                    lv_obj_remove_flag(s_go_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_tv_locked(bool locked)
{
    if (!s_tv) return;
    if (locked) {
        lv_obj_remove_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_obj_add_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    }
}

// ---------------------------------------------------------------------------
// Pulso visual e contagem do pavio (período fixo PULSE_MS)
// ---------------------------------------------------------------------------

static void explode(void);

static void burn_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != ST_RUN) return;

    uint64_t now = millis();
    if (now >= s_deadline) { explode(); return; }

    int frac_1000;
    if (now - s_lit_at < LIGHT_MS) {
        frac_1000 = 1000;
    } else {
        uint32_t left = (uint32_t)(s_deadline - now);
        frac_1000 = s_total_ms ? (int)(((uint32_t)left * 1000) / s_total_ms) : 0;
        if (frac_1000 < 0) frac_1000 = 0;
        else if (frac_1000 > 1000) frac_1000 = 1000;
    }
    s_intensity_1000 = 1000 - frac_1000;

    // Tensão pro áudio: linear no tempo restante (0..255)
    fuse((int16_t)((s_intensity_1000 * 255) / 1000));

    // ---- Pulso visual ----
    s_pulse_ph ^= 1;
    int in = s_intensity_1000;

    int lvl = s_pulse_ph ? in : (in * PV_FLASH_DIP_PCT) / 100;
    if (s_flash) {
        lv_obj_set_style_bg_color(s_flash,
            lv_color_hex(lerp_rgb(KIT_COLOR_BG, PV_FLASH_HOT, lvl)), 0);
    }

    // Anel: engrossa em degraus de 2 px
    int bw = 2 + (((in * 12) / 1000) & ~1);
    if (bw != s_ring_bw) {
        s_ring_bw = bw;
        if (s_ring) lv_obj_set_style_border_width(s_ring, bw, 0);
    }

    // Sílaba: clareia até o branco, em degraus
    int lit = (in * 12) / 1000;
    if (lit != s_sil_lit) {
        s_sil_lit = lit;
        if (s_sil) {
            lv_obj_set_style_text_color(s_sil,
                lv_color_hex(lerp_rgb(KIT_COLOR_TEXT, 0xFFFFFF, (lit * 1000) / 12)), 0);
        }
    }

    // Tremor da sílaba
    if (s_sil) {
        if (in > 420) {
            int j = (in * 6) / 1000;
            lv_obj_set_style_translate_x(s_sil, rnd(-j, j), 0);
            lv_obj_set_style_translate_y(s_sil, rnd(-j, j), 0);
        } else {
            lv_obj_set_style_translate_x(s_sil, 0, 0);
            lv_obj_set_style_translate_y(s_sil, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Rodada
// ---------------------------------------------------------------------------

static void stop_timers(void)
{
    if (s_burn) { lv_timer_delete(s_burn); s_burn = NULL; }
    if (s_boom) { lv_timer_delete(s_boom); s_boom = NULL; }
    fuse(-1);   // apaga o pavio no motor de áudio
}

static void light_fuse(void)
{
    if (s_state == ST_RUN) return;

    stop_timers();

    s_state = ST_RUN;
    s_intensity_1000 = 0;
    s_pulse_ph = 0;

    uint32_t lo = PAVIO_MIN[s_pavio], hi = PAVIO_MAX[s_pavio];
    s_total_ms = (uint32_t)rnd((int)lo, (int)hi);
    s_lit_at   = millis();
    s_deadline = s_lit_at + s_total_ms;

    pick_syllable();
    reset_visuals();

    if (s_flash) {
        lv_obj_set_style_bg_color(s_flash, lv_color_hex(KIT_COLOR_BG), 0);
        lv_obj_set_style_bg_opa(s_flash, LV_OPA_COVER, 0);
    }
    if (s_ring) lv_obj_set_style_border_opa(s_ring, LV_OPA_COVER, 0);

    if (s_boom_ov) lv_obj_add_flag(s_boom_ov, LV_OBJ_FLAG_HIDDEN);
    if (s_tv) lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
    set_tv_locked(true);
    keep_awake(true);
    sync_stage();

    sfx(KIT_SFX_PAVIO_TICK);   // o pavio pega: um "tec"
    fuse(0);                   // ...e começa a queimar
    s_burn = lv_timer_create(burn_cb, PULSE_MS, NULL);
}

static void pass_kit(void)
{
    if (s_state != ST_RUN) return;
    if (millis() - s_lit_at < LIGHT_MS) return;   // pavio ainda acendendo

    if (s_silaba == SIL_TROCA) pick_syllable();

    sfx(KIT_SFX_CLICK);   // "toque" pronto
}

static void boom_burst_cb(lv_timer_t *t)
{
    (void)t;
    s_boom_step++;

    if (s_boom_ov)
        lv_obj_set_style_bg_color(s_boom_ov,
            lv_color_hex((s_boom_step & 1) ? 0xFF6A4D : KIT_COLOR_RED), 0);

    if (s_boom_step >= BOOM_STEPS) {
        if (s_boom_ov) lv_obj_set_style_bg_color(s_boom_ov, lv_color_hex(KIT_COLOR_RED), 0);
        if (s_boom) { lv_timer_delete(s_boom); s_boom = NULL; }
    }
}

static void explode(void)
{
    s_state = ST_BOOM;
    stop_timers();
    reset_visuals();
    set_tv_locked(true);

    if (s_boom_ov) lv_obj_remove_flag(s_boom_ov, LV_OBJ_FLAG_HIDDEN);
    sync_stage();
    keep_awake(true);

    sfx(KIT_SFX_PAVIO_BOOM);

    s_boom_step = 0;
    s_boom = lv_timer_create(boom_burst_cb, BOOM_STEP_MS, NULL);
}

static void new_round(void)
{
    s_state = ST_IDLE;
    stop_timers();
    reset_visuals();
    if (s_boom_ov) lv_obj_add_flag(s_boom_ov, LV_OBJ_FLAG_HIDDEN);
    set_tv_locked(false);
    keep_awake(false);
    sync_stage();
    sfx(KIT_SFX_BACK);
}

// ---------------------------------------------------------------------------
// Ação principal (toque no palco / botão / chacoalhar)
// ---------------------------------------------------------------------------

static void kit_pavio_action(void)
{
    switch (s_state) {
    case ST_IDLE: light_fuse(); break;
    case ST_RUN:  pass_kit();   break;
    case ST_BOOM: if (!s_boom) new_round(); break;
    }
}

static void on_shake(void *user_data)
{
    (void)user_data;
    kit_pavio_action();
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_api && s_api->system) s_api->system->exit();
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    if (s_state != ST_IDLE && s_tv) {
        lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
        return;
    }
    sync_dots();
}

static void stage_cb(lv_event_t *e)
{
    (void)e;
    kit_pavio_action();
}

static void go_cb(lv_event_t *e)
{
    (void)e;
    kit_pavio_action();
}

static void newround_cb(lv_event_t *e)
{
    (void)e;
    kit_pavio_action();
}

static void pavio_pill_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_pavio) return;
    sfx(KIT_SFX_CONFIRM);
    s_pavio = v; sync_segs(); save_prefs();
}

static void silaba_pill_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_silaba) return;
    sfx(KIT_SFX_CONFIRM);
    s_silaba = v; sync_segs(); save_prefs();
}

static void deck_pill_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_deck) return;
    sfx(KIT_SFX_CONFIRM);
    s_deck = v; sync_segs(); save_prefs();
}

// ---------------------------------------------------------------------------
// Construção da tela
// ---------------------------------------------------------------------------

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, X_CHIP, X_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, X_PAD, 16);

    lv_obj_t *g = add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(g);

    lv_obj_t *title = add_label(s_screen, "PAVIO", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, X_PAD + X_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -X_PAD, 40);
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

static lv_obj_t *make_pill(lv_obj_t *parent, const char *txt, lv_event_cb_t cb,
                           int code, lv_obj_t **out_lbl)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_height(c, 58);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_color(c, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 15, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(c, 4);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    lv_obj_t *l = add_label(c, txt, KIT_COLOR_TEXT, &kit_mono_20, 1);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return c;
}

static void build_page_adjust(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 28, 0);
    lv_obj_set_style_pad_row(p, 8, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "PAVIO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_t *r1 = plain_box(p);
    lv_obj_set_size(r1, X_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r1, 8, 0);
    for (int i = 0; i < 3; i++) {
        s_pavio_pills[i] = make_pill(r1, PAVIO_LBL[i], pavio_pill_cb, i, &s_pavio_lbls[i]);
    }

    lv_obj_t *sp1 = plain_box(p); lv_obj_set_size(sp1, 1, 6);

    add_label(p, "S\xC3\x8DLABA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_t *r2 = plain_box(p);
    lv_obj_set_size(r2, X_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r2, 8, 0);
    s_silaba_pills[0] = make_pill(r2, "TROCA", silaba_pill_cb, SIL_TROCA, &s_silaba_lbls[0]);
    s_silaba_pills[1] = make_pill(r2, "FIXA",  silaba_pill_cb, SIL_FIXA,  &s_silaba_lbls[1]);

    lv_obj_t *sp2 = plain_box(p); lv_obj_set_size(sp2, 1, 6);

    add_label(p, "BARALHO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_t *r3 = plain_box(p);
    lv_obj_set_size(r3, X_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r3, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r3, 8, 0);
    s_deck_pills[0] = make_pill(r3, "F\xC3\x81""CIL", deck_pill_cb, DECK_FACIL, &s_deck_lbls[0]);
    s_deck_pills[1] = make_pill(r3, "TUDO",          deck_pill_cb, DECK_TUDO,  &s_deck_lbls[1]);
}

static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *box = lv_obj_create(tile);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    int stage_h = X_PAGE_H - (X_GO_H + 2 * X_GO_MARGIN);
    lv_obj_t *stage = plain_box(box);
    lv_obj_set_size(stage, KIT_DISPLAY_WIDTH, stage_h);
    lv_obj_set_pos(stage, 0, 0);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(stage, stage_cb, LV_EVENT_CLICKED, NULL);

    // Flash vermelho — banda atrás da sílaba (não a tela toda: buffer parcial).
    s_flash = lv_obj_create(stage);
    lv_obj_remove_style_all(s_flash);
    lv_obj_set_size(s_flash, KIT_DISPLAY_WIDTH, 210);
    lv_obj_center(s_flash);
    lv_obj_set_style_bg_color(s_flash, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Coluna central (sílaba + rótulos).
    lv_obj_t *col = plain_box(stage);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 10, 0);
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(col);

    s_kicker = add_label(col, "S\xC3\x8DLABA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);
    s_sil    = add_label(col, "PRONTO", KIT_COLOR_TEXT_MUTED, &kit_display_72, 0);
    lv_obj_set_style_pad_top(s_sil, 4, 0);
    lv_obj_set_style_pad_bottom(s_sil, 4, 0);

    // Anel — moldura pulsante.
    s_ring = lv_obj_create(stage);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, KIT_DISPLAY_WIDTH - 8, stage_h - 8);
    lv_obj_center(s_ring);
    lv_obj_set_style_radius(s_ring, 24, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_set_style_border_width(s_ring, 0, 0);
    lv_obj_set_style_border_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Botão primário.
    s_go_btn = lv_obj_create(box);
    lv_obj_set_size(s_go_btn, X_CONTENT, X_GO_H);
    lv_obj_set_style_radius(s_go_btn, X_GO_H / 2, 0);
    lv_obj_set_style_border_width(s_go_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_go_btn, 0, 0);
    lv_obj_set_style_pad_all(s_go_btn, 0, 0);
    lv_obj_set_style_bg_color(s_go_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_go_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_go_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_go_btn, 8);
    lv_obj_align(s_go_btn, LV_ALIGN_BOTTOM_MID, 0, -X_GO_MARGIN);
    lv_obj_add_event_cb(s_go_btn, go_cb, LV_EVENT_CLICKED, NULL);
    s_go_lbl = add_label(s_go_btn, "ACENDER PAVIO", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_go_lbl);
}

static void build_boom_overlay(void)
{
    s_boom_ov = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_boom_ov);
    lv_obj_set_size(s_boom_ov, KIT_DISPLAY_WIDTH, X_PAGE_H);
    lv_obj_set_pos(s_boom_ov, 0, X_TITLEBAR);
    lv_obj_set_style_bg_color(s_boom_ov, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_set_style_bg_opa(s_boom_ov, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_boom_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_boom_ov, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *col = plain_box(s_boom_ov);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -30);

    add_label(col, "BUM", KIT_COLOR_ON_COLOR, &kit_display_72, 0);
    add_label(col, "PERDEU A RODADA", KIT_COLOR_ON_COLOR, &kit_mono_20, 2);

    s_boom_new = lv_obj_create(s_boom_ov);
    lv_obj_set_size(s_boom_new, X_CONTENT, X_GO_H);
    lv_obj_set_style_radius(s_boom_new, X_GO_H / 2, 0);
    lv_obj_set_style_border_width(s_boom_new, 2, 0);
    lv_obj_set_style_border_color(s_boom_new, lv_color_hex(KIT_COLOR_ON_COLOR), 0);
    lv_obj_set_style_bg_opa(s_boom_new, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_boom_new, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(s_boom_new, 0, 0);
    lv_obj_remove_flag(s_boom_new, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_boom_new, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_boom_new, 8);
    lv_obj_align(s_boom_new, LV_ALIGN_BOTTOM_MID, 0, -X_GO_MARGIN);
    lv_obj_add_event_cb(s_boom_new, newround_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = add_label(s_boom_new, "NOVA RODADA", KIT_COLOR_ON_COLOR, &kit_mono_26, 3);
    lv_obj_center(nl);
}

// Página 2 — COMO JOGA
static const char PV_RULES[] =
    "No PAVIO o tempo fica escondido: s\xC3\xB3 o BUM avisa.\n\n"
    "1. Toque em ACENDER PAVIO. Uma s\xC3\xADlaba aparece no centro.\n\n"
    "2. Fale em voz alta uma palavra que contenha aquela s\xC3\xADlaba e passe o "
    "KIT: toque na tela ou chacoalhe o aparelho.\n\n"
    "3. A s\xC3\xADlaba troca a cada passe, ou fica fixa na rodada conforme o "
    "Ajuste. O tique vai acelerando.\n\n"
    "Explodiu na sua m\xC3\xA3o? Voc\xC3\xAA perdeu a rodada.";

static void build_page_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, PV_RULES, KIT_COLOR_TEXT, &kit_sans_22, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, X_CONTENT);
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, KIT_DISPLAY_WIDTH, X_PAGE_H);
    lv_obj_set_pos(s_tv, 0, X_TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);
    build_page_adjust(s_tiles[0]);
    build_page_game(s_tiles[1]);
    build_page_help(s_tiles[2]);
}

// ---------------------------------------------------------------------------
// Ciclo de vida da Tool
// ---------------------------------------------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    printf("[Pavio] tool_init\n");
    s_api = ctx ? ctx->api : NULL;

    s_accent         = KIT_COLOR_RED;
    s_state          = ST_IDLE;
    s_intensity_1000 = 0;
    s_sil_idx        = -1;
    s_pavio          = 1;
    s_silaba         = SIL_TROCA;
    s_deck           = DECK_FACIL;
    load_prefs();

    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(on_shake, NULL);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();
    build_boom_overlay();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   // começa no JOGO
    lv_obj_update_layout(s_screen);

    sync_segs();
    sync_stage();
    sync_dots();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Pavio] tool_destroy\n");
    stop_timers();
    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);
    keep_awake(false);
    s_state = ST_IDLE;

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < 3; i++) { s_pavio_pills[i] = NULL; s_pavio_lbls[i] = NULL; }
    for (int i = 0; i < 2; i++) {
        s_silaba_pills[i] = s_deck_pills[i] = NULL;
        s_silaba_lbls[i] = s_deck_lbls[i] = NULL;
    }
    s_flash = s_ring = s_kicker = s_sil = NULL;
    s_go_btn = s_go_lbl = NULL;
    s_boom_ov = s_boom_new = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo para testes e CI */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Pavio stub] tool_init — compila nativo; UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
