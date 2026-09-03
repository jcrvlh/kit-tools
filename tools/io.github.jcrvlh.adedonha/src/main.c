/**
 * @file main.c
 * @brief Adedonha — mini-jogo de mesa "nome, lugar, objeto" para o KIT.
 *
 * Filosofia do Bingo: o KIT sorteia (a cartela e a letra), conta o tempo e
 * toca o alarme — a mesa confere no papel. Nada de pontuação no aparelho.
 *
 * Fluxo:
 *   1. Sorteia a cartela uma vez  → todos copiam as categorias como colunas.
 *   2. Sorteia uma letra (botão / toque no palco / chacoalhar) → o tempo começa.
 *   3. Todos preenchem até o TEMPO acabar (alarme) ou alguém apertar STOP.
 *   4. Conferem no papel. Sorteia a próxima letra na mesma cartela.
 *
 * Tela: titlebar fixa + lv_tileview de 3 páginas (AJUSTE / JOGO / CARTELA,
 * começa no JOGO) + botão primário fixo no rodapé.
 *   - AJUSTE: CATEGORIAS (4/6/8) · TEMPO (30S/1MIN/2MIN/OFF) · LETRAS
 *     (FÁCEIS/TODAS) · NOVA CARTELA · REINICIAR LETRAS. Só os ajustes vão pro
 *     Storage; a cartela e o saco de letras começam do zero a cada abertura.
 *   - JOGO: a letra sorteada em kit_display_72, o relógio MM:SS em
 *     kit_display_44 e o botão sensível ao estado. Tempo esgotado = overlay
 *     azul "TEMPO" com o alarme (1º toque cala, 2º vai pra próxima letra).
 *   - CARTELA: a lista das categorias sorteadas (o que se copia pro papel).
 *
 * Sorteio de letras SEM reposição (saco de 16 ou 26) via Random API/TRNG.
 * Saco vazio → reembaralha sozinho. Trocar o conjunto de letras / as categorias
 * / tocar REINICIAR também zera o saco.
 *
 * Animações = um único lv_timer curto que só troca texto / avança a barra por
 * tick (padrão validado da Dice/Bingo). O pulso do anel usa border/opa.
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
#define PAGES        3

// Animação de sorteio (só troca texto / avança a barra por tick).
#define SHUF_TICK_MS   50
#define LETTER_TICKS   11      // roleta A–Z (~550 ms) — kit_display_72 é pesada de repintar
#define CARD_TICKS     18      // barra "sorteando a cartela" (~900 ms)
#define LOAD_BAR_W     240     // largura do trilho da barra de carregamento

// Relógio da rodada.
#define CLOCK_TICK_MS  250
#define TICK_LEAD_S    5        // tique nos últimos N segundos
#define PULSE_LEAD_S   10       // anel pulsa nos últimos N segundos
#define OFF_CAP_S      900      // teto do modo OFF (evita keep_awake eterno)

#define ALARM_REPEAT_MS 3400    // re-toca o alarme no overlay de TEMPO
#define RESET_ARM_MS    4000    // REINICIAR LETRAS: janela do 2º toque

// Só os AJUSTES persistem. A cartela sorteada e o saco de letras começam do
// zero a cada abertura da Tool.
#define K_CATS   "ad_cats"
#define K_TEMPO  "ad_tempo"
#define K_LETRAS "ad_letras"

// CATEGORIAS por cartela.
static const int  CATS_OPT[3]  = { 4, 6, 8 };
static const char *const CATS_LBL[3] = { "4", "6", "8" };

// TEMPO da rodada (segundos; 0 = OFF, sem contagem regressiva).
static const int  TEMPO_S[4]   = { 30, 60, 120, 0 };
static const char *const TEMPO_LBL[4] = { "30S", "1MIN", "2MIN", "OFF" };

// LETRAS: FÁCEIS (dá pra preencher a cartela inteira) ou TODAS (A–Z).
#define LET_FACIL 0
#define LET_TODAS 1
static const char *const POOL_FACIL = "ABCDEFGILMNOPRST";           // 16
static const char *const POOL_TODAS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; // 26

// Pool fixo de categorias — nomes em caixa normal (kit_sans_28, cobre Latin-1).
static const char *const CATS[] = {
    "Nome", "Sobrenome", "Animal", "Fruta", "Cor",
    "Objeto", "Comida", "Pa\xC3\xADs", "Cidade", "Profiss\xC3\xA3o",
    "Parte do corpo", "Marca", "Filme", "Desenho animado", "Cantor(a)",
    "Banda", "Instrumento musical", "Esporte", "Time de futebol", "Flor",
    "\xC3\x81rvore", "Verdura ou legume", "Bebida", "Doce", "Roupa",
    "Cal\xC3\xA7""ado", "M\xC3\xB3vel", "Eletrodom\xC3\xA9stico", "Meio de transporte", "Personagem",
    "Super-her\xC3\xB3i", "Vil\xC3\xA3o de filme", "Jogo", "Aplicativo", "Rede social",
    "Signo", "Palavra em ingl\xC3\xAAs", "Nome de beb\xC3\xAA", "Tem na cozinha",
    "Coisa gelada", "Item de praia", "Desculpa pra faltar", "M\xC3\xBAsica",
};
#define CATS_N     ((int)(sizeof(CATS) / sizeof(CATS[0])))
#define CART_MAX   8

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
typedef enum { ST_IDLE, ST_READY, ST_RUN, ST_DONE } ad_state_t;
typedef enum { ANIM_NONE, ANIM_CARD, ANIM_LETTER } ad_anim_t;

static const kit_api_table_t *s_api = NULL;

static uint32_t   s_accent = KIT_COLOR_BLUE;
static ad_state_t s_state  = ST_IDLE;

static int s_cats_i  = 1;            // índice em CATS_OPT (0/1/2)
static int s_tempo_i = 1;            // índice em TEMPO_S
static int s_letras  = LET_FACIL;

static uint32_t s_used = 0;          // bitmask das letras já sorteadas (bit = c-'A')
static char     s_letter = 0;        // letra da rodada atual ('A'..'Z' ou 0)

static int  s_cart[CART_MAX];        // índices em CATS da cartela atual
static int  s_ncart = 0;

static bool s_timeup = false;        // ST_DONE por tempo esgotado (overlay no ar)

// Relógio da rodada.
static uint64_t s_started_at = 0;
static uint64_t s_deadline   = 0;    // 0 = modo OFF (sem contagem regressiva)
static int      s_shown_sec  = -1;
static int      s_tick_sec   = -1;
static int      s_ring_ph    = 0;

// Animação.
static ad_anim_t   s_anim_kind = ANIM_NONE;
static int         s_anim_tick = 0;
static int         s_anim_pick = 0;
static lv_timer_t *s_anim  = NULL;
static lv_timer_t *s_clock = NULL;
static lv_timer_t *s_alarm = NULL;

// REINICIAR LETRAS — dois toques.
static bool        s_reset_armed = false;
static lv_timer_t *s_reset_timer = NULL;

// ---------------------------------------------------------------------------
// Objetos LVGL
// ---------------------------------------------------------------------------
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv     = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];

// Página 0 — AJUSTE
static lv_obj_t *s_cats_pills[3];   static lv_obj_t *s_cats_lbls[3];
static lv_obj_t *s_tempo_pills[4];  static lv_obj_t *s_tempo_lbls[4];
static lv_obj_t *s_letras_pills[2]; static lv_obj_t *s_letras_lbls[2];
static lv_obj_t *s_newcart_btn = NULL;
static lv_obj_t *s_reset_btn   = NULL;
static lv_obj_t *s_reset_lbl   = NULL;

// Página 1 — JOGO
static lv_obj_t *s_kicker  = NULL;
static lv_obj_t *s_big     = NULL;   // a letra — kit_display_72
static lv_obj_t *s_status  = NULL;   // "SORTEIE A CARTELA" / "CARTELA PRONTA"
static lv_obj_t *s_load     = NULL;  // coluna "SORTEANDO A CARTELA" + barra
static lv_obj_t *s_load_bar = NULL;
static lv_obj_t *s_clock_l = NULL;   // relógio MM:SS
static lv_obj_t *s_hint    = NULL;
static lv_obj_t *s_ring    = NULL;
static lv_obj_t *s_go_btn  = NULL;
static lv_obj_t *s_go_lbl  = NULL;

// Página 2 — CARTELA
static lv_obj_t *s_cart_hdr  = NULL;
static lv_obj_t *s_cart_list = NULL;

// Overlay — TEMPO
static lv_obj_t *s_ov      = NULL;
static lv_obj_t *s_ov_sub  = NULL;   // linha de apoio (alarme ↔ confiram)
static lv_obj_t *s_ov_btn  = NULL;   // "PRÓXIMA LETRA" (só depois de calar o alarme)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static void beep(uint16_t freq, uint16_t ms)
{
    if (s_api && s_api->audio) s_api->audio->beep(freq, ms);
}

static void sfx(kit_sfx_t s)
{
    if (s_api && s_api->audio) s_api->audio->sfx(s);
}

static int rnd(int min, int max)
{
    if (s_api && s_api->random) return (int)s_api->random->range(min, max);
    return min;
}

static uint64_t millis(void)
{
    return (s_api && s_api->time) ? s_api->time->get_millis() : 0;
}

static void keep_awake(bool on)
{
    if (s_api && s_api->power) s_api->power->keep_awake(on);
}

static int cats_count(void) { return CATS_OPT[s_cats_i]; }
static int round_secs(void) { return TEMPO_S[s_tempo_i]; }
static const char *pool_str(void) { return s_letras == LET_TODAS ? POOL_TODAS : POOL_FACIL; }

static void fmt_mmss(char *buf, size_t n, int secs)
{
    if (secs < 0) secs = 0;
    snprintf(buf, n, "%02d:%02d", secs / 60, secs % 60);
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
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *field_label(lv_obj_t *parent, const char *txt)
{
    return add_label(parent, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

static void show_only(lv_obj_t *keep)
{
    lv_obj_t *all[3] = { s_big, s_status, s_load };
    for (int i = 0; i < 3; i++) {
        if (!all[i]) continue;
        if (all[i] == keep) lv_obj_remove_flag(all[i], LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(all[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Persistência (Storage API) — só os AJUSTES.
// ---------------------------------------------------------------------------

static void save_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    s_api->storage->set_i32(K_CATS, s_cats_i);
    s_api->storage->set_i32(K_TEMPO, s_tempo_i);
    s_api->storage->set_i32(K_LETRAS, s_letras);
}

static void load_prefs(void)
{
    if (!s_api || !s_api->storage) return;

    int32_t v;
    if (s_api->storage->get_i32(K_CATS, &v) == KIT_OK && v >= 0 && v <= 2) s_cats_i = (int)v;
    if (s_api->storage->get_i32(K_TEMPO, &v) == KIT_OK && v >= 0 && v <= 3) s_tempo_i = (int)v;
    if (s_api->storage->get_i32(K_LETRAS, &v) == KIT_OK && (v == LET_FACIL || v == LET_TODAS))
        s_letras = (int)v;
}

// ---------------------------------------------------------------------------
// Sincronização de UI
// ---------------------------------------------------------------------------

static void sync_dots(void)
{
    lv_obj_t *act = s_tv ? lv_tileview_get_tile_active(s_tv) : NULL;
    for (int i = 0; i < PAGES; i++) {
        bool on = (act == s_tiles[i]);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(on ? s_accent : KIT_COLOR_LINE), 0);
        lv_obj_set_size(s_dots[i], on ? 20 : 8, 8);
    }
}

static void sync_seg(lv_obj_t **pills, lv_obj_t **lbls, int n, int sel)
{
    uint32_t sel_txt = on_accent();
    for (int i = 0; i < n; i++) {
        bool on = (i == sel);
        lv_obj_set_style_bg_color(pills[i], lv_color_hex(on ? s_accent : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(lbls[i], lv_color_hex(on ? sel_txt : KIT_COLOR_TEXT), 0);
    }
}

static void sync_segs(void)
{
    sync_seg(s_cats_pills, s_cats_lbls, 3, s_cats_i);
    sync_seg(s_tempo_pills, s_tempo_lbls, 4, s_tempo_i);
    sync_seg(s_letras_pills, s_letras_lbls, 2, s_letras);
}

// Reconstrói a página CARTELA: cabeçalho + lista numerada (ou o "como joga"
// quando ainda não há cartela).
static void build_cart_page(void)
{
    if (!s_cart_list) return;
    lv_obj_clean(s_cart_list);

    if (s_ncart == 0) {
        lv_label_set_text(s_cart_hdr, "COMO JOGA");

        static const char *const steps[4] = {
            "1 \xC2\xB7 O KIT SORTEIA A CARTELA. TODOS COPIAM AS CATEGORIAS COMO COLUNAS NO PAPEL.",
            "2 \xC2\xB7 O KIT SORTEIA UMA LETRA E O TEMPO COME\xC3\x87""A A CORRER.",
            "3 \xC2\xB7 CADA UM PREENCHE UMA PALAVRA POR COLUNA COM AQUELA LETRA - AT\xC3\x89 O TEMPO ACABAR OU ALGU\xC3\x89M GRITAR STOP.",
            "4 \xC2\xB7 CONFEREM E PONTUAM NO PAPEL. O KIT SORTEIA A PR\xC3\x93XIMA LETRA NA MESMA CARTELA.",
        };
        for (int i = 0; i < 4; i++) {
            lv_obj_t *b = add_label(s_cart_list, steps[i], KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
            lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(b, X_CONTENT);
        }
        return;
    }

    if (s_letter)
        lv_label_set_text_fmt(s_cart_hdr, "LETRA \xC2\xB7 %c", s_letter);
    else
        lv_label_set_text(s_cart_hdr, "TOQUE PARA SORTEAR A LETRA");

    for (int i = 0; i < s_ncart; i++) {
        lv_obj_t *row = plain_box(s_cart_list);
        lv_obj_set_size(row, X_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(row, 14, 0);

        lv_obj_t *num = add_label(row, "", s_accent, &kit_mono_26, 1);
        lv_label_set_text_fmt(num, "%d", i + 1);
        lv_obj_set_width(num, 32);

        lv_obj_t *name = add_label(row, CATS[s_cart[i]], KIT_COLOR_TEXT, &kit_sans_28, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_set_flex_grow(name, 1);
    }
}

static void sync_stage(void)
{
    if (!s_kicker) return;

    if (s_ov) {
        if (s_state == ST_DONE && s_timeup) lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
        else                                lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_ring && s_state != ST_RUN) {
        lv_obj_set_style_border_width(s_ring, 0, 0);
        lv_obj_set_style_border_opa(s_ring, LV_OPA_TRANSP, 0);
    }

    char clk[8];

    switch (s_state) {
    case ST_IDLE:
        lv_label_set_text(s_kicker, "ADEDONHA");
        show_only(s_status);
        lv_label_set_text(s_status, "SORTEIE\nA CARTELA");
        lv_obj_set_style_text_color(s_status, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_obj_add_flag(s_clock_l, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "SORTEAR CARTELA \xC2\xB7 CHACOALHAR");
        lv_label_set_text(s_go_lbl, "SORTEAR CARTELA");
        break;

    case ST_READY:
        lv_label_set_text_fmt(s_kicker, "%d CATEGORIAS", s_ncart);
        show_only(s_status);
        lv_label_set_text(s_status, "CARTELA\nPRONTA");
        lv_obj_set_style_text_color(s_status, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_obj_remove_flag(s_clock_l, LV_OBJ_FLAG_HIDDEN);
        if (round_secs() == 0) lv_label_set_text(s_clock_l, "OFF");
        else { fmt_mmss(clk, sizeof(clk), round_secs()); lv_label_set_text(s_clock_l, clk); }
        lv_obj_set_style_text_color(s_clock_l, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_label_set_text(s_hint, "SORTEAR LETRA \xC2\xB7 CHACOALHAR");
        lv_label_set_text(s_go_lbl, "SORTEAR LETRA");
        break;

    case ST_RUN:
        lv_label_set_text(s_kicker, "LETRA");
        show_only(s_big);
        { char t[2] = { s_letter, 0 }; lv_label_set_text(s_big, t); }
        lv_obj_set_style_text_color(s_big, lv_color_hex(s_accent), 0);
        lv_obj_remove_flag(s_clock_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_clock_l, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_label_set_text(s_hint, "STOP \xC2\xB7 CHACOALHAR");
        lv_label_set_text(s_go_lbl, "STOP");
        break;

    case ST_DONE:
        lv_label_set_text(s_kicker, s_timeup ? "TEMPO" : "STOP");
        show_only(s_big);
        { char t[2] = { s_letter, 0 }; lv_label_set_text(s_big, t); }
        lv_obj_set_style_text_color(s_big, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_obj_remove_flag(s_clock_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_clock_l, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_label_set_text(s_hint, "");
        lv_label_set_text(s_go_lbl, "SORTEAR LETRA");
        break;
    }
}

// ---------------------------------------------------------------------------
// Relógio da rodada
// ---------------------------------------------------------------------------

static void stop_clock(void)
{
    if (s_clock) { lv_timer_delete(s_clock); s_clock = NULL; }
}

static void stop_alarm(void)
{
    if (s_alarm) { lv_timer_delete(s_alarm); s_alarm = NULL; }
}

static void draw_letter(void);

// Dois estágios do overlay de TEMPO: com o alarme tocando, o toque só o cala
// (fundo sólido, "TOQUE PARA SILENCIAR", sem botão). Depois de calado, aparece
// "CONFIRAM NO PAPEL" + o botão PRÓXIMA LETRA.
static void overlay_set_stage(bool alarming)
{
    if (s_ov)
        lv_obj_set_style_bg_color(s_ov, lv_color_hex(s_accent), 0);   // encerra o strobe
    if (s_ov_sub)
        lv_label_set_text(s_ov_sub, alarming ? "TOQUE PARA SILENCIAR" : "CONFIRAM NO PAPEL");
    if (s_ov_btn) {
        if (alarming) lv_obj_add_flag(s_ov_btn, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_remove_flag(s_ov_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

// Toque no overlay de TEMPO (fundo, botão, chacoalhar): 1º cala o alarme,
// 2º vai pra próxima letra.
static void timeup_tap(void)
{
    if (s_alarm) { stop_alarm(); overlay_set_stage(false); return; }
    draw_letter();
}

static void ring_pulse(int secs_left)
{
    if (!s_ring) return;
    if (secs_left > PULSE_LEAD_S) {
        lv_obj_set_style_border_width(s_ring, 0, 0);
        lv_obj_set_style_border_opa(s_ring, LV_OPA_TRANSP, 0);
        return;
    }
    s_ring_ph ^= 1;
    // Aritmética inteira de propósito: o elf_loader não exporta __divsf3 (float).
    if (secs_left < 0) secs_left = 0;
    int bw = 3 + (PULSE_LEAD_S - secs_left) * 11 / PULSE_LEAD_S;   // 3..14 conforme aperta
    lv_obj_set_style_border_color(s_ring, lv_color_hex(s_accent), 0);
    lv_obj_set_style_border_width(s_ring, bw, 0);
    lv_obj_set_style_border_opa(s_ring, s_ring_ph ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void time_up(void);

static void clock_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != ST_RUN) return;

    uint64_t now = millis();
    char clk[8];

    if (s_deadline == 0) {
        // OFF: cronômetro subindo (sem contagem regressiva), com teto de segurança.
        // Casta a diferença pra 32 bits ANTES de dividir — o elf_loader não
        // exporta __udivdi3 (divisão de 64 bits).
        int el = (int)((uint32_t)(now - s_started_at) / 1000u);
        if (el >= OFF_CAP_S) { time_up(); return; }
        if (el != s_shown_sec) {
            s_shown_sec = el;
            fmt_mmss(clk, sizeof(clk), el);
            lv_label_set_text(s_clock_l, clk);
        }
        return;
    }

    if (now >= s_deadline) { time_up(); return; }

    int left = (int)(((uint32_t)(s_deadline - now) + 999u) / 1000u);
    if (left != s_shown_sec) {
        s_shown_sec = left;
        fmt_mmss(clk, sizeof(clk), left);
        lv_label_set_text(s_clock_l, clk);
        lv_obj_set_style_text_color(s_clock_l,
            lv_color_hex(left <= PULSE_LEAD_S ? s_accent : KIT_COLOR_TEXT), 0);
        if (left <= TICK_LEAD_S && left != s_tick_sec) {
            s_tick_sec = left;
            sfx(KIT_SFX_TIMER_TICK);
        }
    }
    ring_pulse(left);
}

static void alarm_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != ST_DONE || !s_timeup) { stop_alarm(); return; }
    s_ring_ph ^= 1;
    if (s_ov)
        lv_obj_set_style_bg_color(s_ov,
            lv_color_hex(s_ring_ph ? 0x3B4BD6 : s_accent), 0);
    sfx(KIT_SFX_ADEDONHA_TIMEUP);
}

static void time_up(void)
{
    stop_clock();
    s_state   = ST_DONE;
    s_timeup  = true;
    s_shown_sec = s_tick_sec = -1;
    lv_label_set_text(s_clock_l, "00:00");
    keep_awake(true);
    sfx(KIT_SFX_ADEDONHA_TIMEUP);
    stop_alarm();
    s_alarm = lv_timer_create(alarm_cb, ALARM_REPEAT_MS, NULL);
    sync_stage();
    overlay_set_stage(true);   // 1º toque cala o alarme, não pula de letra
}

static void start_round(void)
{
    s_started_at = millis();
    int secs = round_secs();
    s_deadline = secs ? s_started_at + (uint64_t)secs * 1000 : 0;
    s_shown_sec = s_tick_sec = -1;
    s_state = ST_RUN;
    keep_awake(true);
    stop_clock();
    s_clock = lv_timer_create(clock_cb, CLOCK_TICK_MS, NULL);
    sync_stage();
}

static void stop_round(void)
{
    if (s_state != ST_RUN) return;
    stop_clock();
    s_state  = ST_DONE;
    s_timeup = false;
    keep_awake(false);
    sfx(KIT_SFX_ADEDONHA_STOP);
    sync_stage();
}

// ---------------------------------------------------------------------------
// Sorteios
// ---------------------------------------------------------------------------

static int bag_letters(char *out)
{
    const char *p = pool_str();
    int n = 0;
    for (; *p; p++)
        if (!(s_used & (1u << (*p - 'A')))) out[n++] = *p;
    return n;
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    s_anim_tick++;
    int total = (s_anim_kind == ANIM_LETTER) ? LETTER_TICKS : CARD_TICKS;

    if (s_anim_tick < total) {
        if (s_anim_kind == ANIM_LETTER) {
            char b[2] = { (char)('A' + rnd(0, 25)), 0 };
            lv_label_set_text(s_big, b);
        } else if (s_load_bar) {
            lv_obj_set_width(s_load_bar, LOAD_BAR_W * s_anim_tick / total);
        }
        return;
    }

    if (s_anim) { lv_timer_delete(s_anim); s_anim = NULL; }

    if (s_anim_kind == ANIM_LETTER) {
        s_letter = (char)('A' + s_anim_pick);
        s_used  |= (1u << s_anim_pick);
        s_anim_kind = ANIM_NONE;
        build_cart_page();
        start_round();
    } else {
        // A cartela (s_cart / s_ncart) já foi sorteada em draw_cartela.
        if (s_load_bar) lv_obj_set_width(s_load_bar, LOAD_BAR_W);
        s_anim_kind = ANIM_NONE;
        s_letter = 0;
        s_state = ST_READY;
        s_timeup = false;
        build_cart_page();
        sync_stage();
        // "Já mostra a cartela": pula pra página CARTELA.
        if (s_tv) lv_tileview_set_tile_by_index(s_tv, 2, 0, LV_ANIM_OFF);
        sync_dots();
        sfx(KIT_SFX_CONFIRM);
    }
}

static void draw_cartela(void)
{
    if (s_anim) return;
    stop_clock();
    stop_alarm();
    keep_awake(false);

    int idx[CATS_N];
    for (int i = 0; i < CATS_N; i++) idx[i] = i;
    int want = cats_count();
    for (int i = 0; i < want; i++) {
        int j = rnd(i, CATS_N - 1);
        int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
        s_cart[i] = idx[i];
    }
    s_ncart = want;

    s_anim_kind = ANIM_CARD;
    s_anim_tick = 0;
    s_timeup = false;
    if (s_ov) lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
    if (s_tv) lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);

    lv_label_set_text(s_kicker, "CARTELA");
    show_only(s_load);
    if (s_load_bar) lv_obj_set_width(s_load_bar, 0);
    lv_obj_add_flag(s_clock_l, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_hint, "");

    sfx(KIT_SFX_ADEDONHA_CARD);
    s_anim = lv_timer_create(anim_cb, SHUF_TICK_MS, NULL);
}

static void draw_letter(void)
{
    if (s_anim) return;
    if (s_ncart == 0) { draw_cartela(); return; }

    stop_clock();
    stop_alarm();
    s_timeup = false;
    if (s_ov) lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);

    char bag[26];
    int nb = bag_letters(bag);
    if (nb == 0) {
        s_used = 0;
        nb = bag_letters(bag);
        beep(440, 45);
        beep(660, 60);
    }
    char pick = bag[rnd(0, nb - 1)];
    s_anim_pick = pick - 'A';

    s_anim_kind = ANIM_LETTER;
    s_anim_tick = 0;
    if (s_tv) lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);

    lv_label_set_text(s_kicker, "LETRA");
    show_only(s_big);
    lv_obj_set_style_text_color(s_big, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_obj_add_flag(s_clock_l, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_hint, "");

    sfx(KIT_SFX_ADEDONHA_LETTER);
    s_anim = lv_timer_create(anim_cb, SHUF_TICK_MS, NULL);
}

// ---------------------------------------------------------------------------
// Ação principal (chacoalhar / toque no palco / botão)
// ---------------------------------------------------------------------------

static void adedonha_action(void)
{
    if (!s_screen || !s_kicker || s_anim) return;
    switch (s_state) {
    case ST_IDLE:  draw_cartela(); break;
    case ST_READY: draw_letter();  break;
    case ST_RUN:   stop_round();   break;
    case ST_DONE:  if (s_timeup) timeup_tap(); else draw_letter(); break;
    }
}

static void on_shake(void *ud) { (void)ud; adedonha_action(); }

// ---------------------------------------------------------------------------
// REINICIAR LETRAS — dois toques
// ---------------------------------------------------------------------------

static void reset_disarm(void)
{
    s_reset_armed = false;
    if (s_reset_timer) { lv_timer_delete(s_reset_timer); s_reset_timer = NULL; }
    if (s_reset_lbl) {
        lv_label_set_text(s_reset_lbl, "REINICIAR LETRAS");
        lv_obj_set_style_text_color(s_reset_lbl, lv_color_hex(KIT_COLOR_RED), 0);
    }
    if (s_reset_btn) lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_TRANSP, 0);
}

static void reset_disarm_cb(lv_timer_t *t) { (void)t; reset_disarm(); }

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
    if (s_anim && s_tv && lv_tileview_get_tile_active(s_tv) != s_tiles[1])
        lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
    sync_dots();
    reset_disarm();
}

static void stage_cb(lv_event_t *e) { (void)e; adedonha_action(); }
static void go_cb(lv_event_t *e)    { (void)e; adedonha_action(); }
static void ov_next_cb(lv_event_t *e) { (void)e; timeup_tap(); }

static void cats_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_cats_i) return;
    s_cats_i = v;
    stop_clock();
    stop_alarm();
    keep_awake(false);
    s_ncart = 0;
    s_letter = 0;
    s_timeup = false;
    s_state = ST_IDLE;
    sync_segs();
    save_prefs();
    build_cart_page();
    sync_stage();
}

static void tempo_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_tempo_i) return;
    s_tempo_i = v;
    sync_segs();
    save_prefs();
    if (s_state == ST_READY) sync_stage();
}

static void letras_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_letras) return;
    s_letras = v;
    s_used = 0;
    sync_segs();
    save_prefs();
}

static void newcart_cb(lv_event_t *e) { (void)e; if (!s_anim) draw_cartela(); }

static void reset_cb(lv_event_t *e)
{
    (void)e;
    if (!s_reset_armed) {
        if (s_used == 0) return;
        s_reset_armed = true;
        lv_label_set_text(s_reset_lbl, "TOCAR DE NOVO");
        lv_obj_set_style_text_color(s_reset_lbl, lv_color_hex(KIT_COLOR_ON_COLOR), 0);
        lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_COVER, 0);
        s_reset_timer = lv_timer_create(reset_disarm_cb, RESET_ARM_MS, NULL);
        return;
    }
    s_used = 0;
    reset_disarm();
    beep(660, 40);
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

    lv_obj_t *title = add_label(s_screen, "ADEDONHA", KIT_COLOR_TEXT, &kit_mono_26, 2);
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
    lv_obj_set_height(c, 64);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_color(c, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(c, 4);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    lv_obj_t *l = add_label(c, txt, KIT_COLOR_TEXT, &kit_mono_26, 1);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return c;
}

static void seg_row(lv_obj_t *parent, const char *title, const char *const *opts,
                    int n, lv_event_cb_t cb, lv_obj_t **pills, lv_obj_t **lbls)
{
    lv_obj_t *sec = plain_box(parent);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 12, 0);
    field_label(sec, title);

    lv_obj_t *row = plain_box(sec);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    for (int i = 0; i < n; i++)
        pills[i] = make_pill(row, opts[i], cb, i, &lbls[i]);
}

static lv_obj_t *make_outline_btn(lv_obj_t *parent, const char *txt, uint32_t color,
                                  lv_event_cb_t cb, lv_obj_t **out_lbl)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, lv_pct(100), 72);
    lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = add_label(b, txt, color, &kit_mono_26, 1);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

static void build_page_adjust(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, X_PAD, 0);
    lv_obj_set_style_pad_right(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 20, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 26, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    static const char *const LETRAS_OPT[2] = { "F\xC3\x81""CEIS", "TODAS" };

    seg_row(p, "CATEGORIAS", CATS_LBL, 3, cats_cb, s_cats_pills, s_cats_lbls);
    seg_row(p, "TEMPO", TEMPO_LBL, 4, tempo_cb, s_tempo_pills, s_tempo_lbls);
    seg_row(p, "LETRAS", LETRAS_OPT, 2, letras_cb, s_letras_pills, s_letras_lbls);

    lv_obj_t *sec = plain_box(p);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 12, 0);
    field_label(sec, "RODADA");
    s_newcart_btn = make_outline_btn(sec, "NOVA CARTELA", s_accent, newcart_cb, NULL);
    s_reset_btn   = make_outline_btn(sec, "REINICIAR LETRAS", KIT_COLOR_RED, reset_cb, &s_reset_lbl);
    lv_obj_t *note = add_label(sec,
        "TROCAR AS CATEGORIAS PEDE UMA CARTELA NOVA. REINICIAR LETRAS DEVOLVE TODAS AO SORTEIO.",
        KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, X_CONTENT);
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

    lv_obj_t *col = plain_box(stage);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(col);

    s_kicker = add_label(col, "ADEDONHA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);

    s_big = add_label(col, "A", KIT_COLOR_TEXT, &kit_display_72, 0);
    lv_obj_set_style_pad_top(s_big, 2, 0);
    lv_obj_set_style_pad_bottom(s_big, 2, 0);

    s_status = add_label(col, "SORTEIE\nA CARTELA", KIT_COLOR_TEXT_MUTED, &kit_mono_26, 2);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);

    // Carregando a cartela — rótulo + trilho/preenchimento (duas caixas simples).
    s_load = plain_box(col);
    lv_obj_set_size(s_load, X_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_load, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_load, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_load, 20, 0);
    lv_obj_t *ll = add_label(s_load, "SORTEANDO\nA CARTELA", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_set_style_text_align(ll, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *track = plain_box(s_load);
    lv_obj_set_size(track, LOAD_BAR_W, 12);
    lv_obj_set_style_radius(track, 6, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

    s_load_bar = plain_box(track);
    lv_obj_set_size(s_load_bar, 0, 12);
    lv_obj_align(s_load_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(s_load_bar, 6, 0);
    lv_obj_set_style_bg_color(s_load_bar, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_load_bar, LV_OPA_COVER, 0);

    s_clock_l = add_label(col, "01:00", KIT_COLOR_TEXT_MUTED, &kit_display_44, 0);
    lv_obj_set_style_pad_top(s_clock_l, 6, 0);

    s_hint = add_label(col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint, X_CONTENT);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    // Anel — moldura pulsante (por cima, sem roubar toque).
    s_ring = lv_obj_create(stage);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, KIT_DISPLAY_WIDTH - 8, stage_h - 8);
    lv_obj_center(s_ring);
    lv_obj_set_style_radius(s_ring, 24, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(s_accent), 0);
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
    s_go_lbl = add_label(s_go_btn, "SORTEAR CARTELA", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_go_lbl);
}

static void build_page_cart(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *wrap = lv_obj_create(tile);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(wrap, X_PAD, 0);
    lv_obj_set_style_pad_right(wrap, X_PAD, 0);
    lv_obj_set_style_pad_top(wrap, 18, 0);
    lv_obj_set_style_pad_bottom(wrap, 16, 0);
    lv_obj_set_style_pad_row(wrap, 14, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    s_cart_hdr = add_label(wrap, "COMO JOGA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);

    s_cart_list = plain_box(wrap);
    lv_obj_add_flag(s_cart_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(s_cart_list, X_CONTENT);
    lv_obj_set_flex_grow(s_cart_list, 1);
    lv_obj_set_flex_flow(s_cart_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_cart_list, 14, 0);
    lv_obj_set_style_pad_bottom(s_cart_list, 8, 0);
    lv_obj_set_scroll_dir(s_cart_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_cart_list, LV_SCROLLBAR_MODE_AUTO);
}

static void build_overlay(void)
{
    s_ov = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_ov);
    lv_obj_set_size(s_ov, KIT_DISPLAY_WIDTH, X_PAGE_H);
    lv_obj_set_pos(s_ov, 0, X_TITLEBAR);
    lv_obj_set_style_bg_color(s_ov, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ov, ov_next_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(s_ov);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -30);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);

    add_label(col, "TEMPO", KIT_COLOR_ON_COLOR, &kit_display_72, 0);
    s_ov_sub = add_label(col, "TOQUE PARA SILENCIAR", KIT_COLOR_ON_COLOR, &kit_mono_20, 2);

    lv_obj_t *nb = lv_obj_create(s_ov);
    s_ov_btn = nb;
    lv_obj_set_size(nb, X_CONTENT, X_GO_H);
    lv_obj_set_style_radius(nb, X_GO_H / 2, 0);
    lv_obj_set_style_border_width(nb, 2, 0);
    lv_obj_set_style_border_color(nb, lv_color_hex(KIT_COLOR_ON_COLOR), 0);
    lv_obj_set_style_bg_opa(nb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(nb, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(nb, 0, 0);
    lv_obj_remove_flag(nb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(nb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(nb, 8);
    lv_obj_align(nb, LV_ALIGN_BOTTOM_MID, 0, -X_GO_MARGIN);
    lv_obj_add_event_cb(nb, ov_next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = add_label(nb, "PR\xC3\x93XIMA LETRA", KIT_COLOR_ON_COLOR, &kit_mono_26, 3);
    lv_obj_center(nl);
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
    build_page_cart(s_tiles[2]);
}

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    printf("[Adedonha] tool_init (id=%s)\n", ctx->tool_id ? ctx->tool_id : "?");

    s_accent  = KIT_COLOR_BLUE;
    s_state   = ST_IDLE;
    s_cats_i  = 1;
    s_tempo_i = 1;
    s_letras  = LET_FACIL;
    s_used    = 0;
    s_letter  = 0;
    s_ncart   = 0;
    s_timeup  = false;
    s_anim_kind = ANIM_NONE;
    s_reset_armed = false;
    load_prefs();   // só os ajustes; sempre começa em ST_IDLE (sem cartela)

    if (s_api->imu)
        s_api->imu->register_shake_callback(on_shake, NULL);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();
    build_overlay();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   // começa no JOGO
    lv_obj_update_layout(s_screen);

    sync_segs();
    build_cart_page();
    sync_stage();
    sync_dots();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Adedonha] tool_destroy\n");
    if (s_anim)        { lv_timer_delete(s_anim);        s_anim = NULL; }
    if (s_clock)       { lv_timer_delete(s_clock);       s_clock = NULL; }
    if (s_alarm)       { lv_timer_delete(s_alarm);       s_alarm = NULL; }
    if (s_reset_timer) { lv_timer_delete(s_reset_timer); s_reset_timer = NULL; }
    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);
    keep_awake(false);
    s_state = ST_IDLE;
    s_anim_kind = ANIM_NONE;
    s_reset_armed = false;

    // Libera a própria tela: o Runtime já voltou pro Launcher, então ela não é
    // mais a tela ativa. Deixá-la viva depois do dlclose faria os callbacks
    // dela apontarem pra código desmapeado.
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < 3; i++) { s_cats_pills[i] = NULL; s_cats_lbls[i] = NULL; }
    for (int i = 0; i < 4; i++) { s_tempo_pills[i] = NULL; s_tempo_lbls[i] = NULL; }
    for (int i = 0; i < 2; i++) { s_letras_pills[i] = NULL; s_letras_lbls[i] = NULL; }
    s_newcart_btn = s_reset_btn = s_reset_lbl = NULL;
    s_kicker = s_big = s_status = s_load = s_load_bar = s_clock_l = s_hint = s_ring = NULL;
    s_go_btn = s_go_lbl = NULL;
    s_cart_hdr = s_cart_list = NULL;
    s_ov = s_ov_sub = s_ov_btn = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo: só exercita a lógica de sorteio */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Adedonha stub] tool_init — lógica de sorteio compila; UI atrás de KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
