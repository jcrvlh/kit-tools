/**
 * @file main.c
 * @brief Vira Certo — mini-jogo de pontaria giratória para o KIT.
 *
 * Cada rodada sorteia UM alvo (ângulo + eixo + direção), igual pra todo
 * mundo: "GIRE 130 À ESQUERDA" ou "INCLINE 40 PRA BAIXO". O jogador da vez
 * toca PRONTO, gira ou inclina o KIT tentando parar bem em cima do número, e
 * segura quieto por um instante. O giroscópio (kit_api -> imu -> gyro_*)
 * integra o ângulo percorrido e detecta "parou" pelo módulo da velocidade
 * angular caindo e ficando baixo. O erro (|medido - alvo|) vira uma faixa de
 * pontos: PERFEITO / ÓTIMO / QUASE / ERROU.
 *
 * A dificuldade sobe sozinha conforme a rodada avança: ângulo maior, menos
 * "redondo" (evita múltiplos de 45°), mais chance de virar inclinar em vez de
 * girar, tolerância mais apertada. A 1ª rodada é sempre um aquecimento de 90°
 * fácil; a última é sempre "mais de 360°" (uma volta inteira e pouco).
 *
 * Requer runtime >= 0.4.0 (`imu -> gyro_*`). Toda a aritmética é inteira e em
 * centigraus (grau × 100): o `.so` da Tool não resolve float no elf_loader do
 * KIT. A dica sonora de "quente/frio" reusa o motor `audio -> fuse` do Pavio.
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
// Layout (368 × 448 — espelha as métricas do Estouro/Pavio/Bingo)
// ---------------------------------------------------------------------------
#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define V_PAD        16
#define V_CONTENT    (KIT_DISPLAY_WIDTH - 2 * V_PAD)          // 336
#define V_TITLEBAR   88
#define V_CHIP       56
#define V_PAGE_H     (KIT_DISPLAY_HEIGHT - V_TITLEBAR)
#define V_GO_H       76
#define V_GO_MARGIN  18
#define V_BTN_BAND   (V_GO_H + V_GO_MARGIN * 2)
#define V_STAGE_H    (V_PAGE_H - V_BTN_BAND)
#define V_STEP       80

#define PAGES        4     // 0 ajuste · 1 jogo · 2 placar · 3 como joga

#define MAX_PLAYERS      8
#define ROUND_OPTIONS_N  3
static const int ROUND_OPTIONS[ROUND_OPTIONS_N] = { 3, 5, 7 };

#define GYRO_POLL_MS            40
#define VC_SETTLE_THRESHOLD_CDPS 1500   // 15 °/s — abaixo disso conta como "parado"
#define VC_MOVE_THRESHOLD_CDPS   2500   // 25 °/s — acima disso conta como "girando de vez"
#define VC_PRESETTLE_MS          250    // parado por isso antes de abrir a confirmação
#define VC_CONFIRM_MS            3000   // contagem regressiva pra consolidar o resultado
#define VC_ATTEMPT_TIMEOUT_MS    8000   // trava mesmo sem "parar"
#define VC_TENSION_RANGE_CDEG    9000   // erro (centigraus) em que a dica sonora zera

#define K_PLAYERS "vc_players"
#define K_ROUNDS  "vc_rounds"

typedef enum {
    VC_IDLE = 0,        // parado, esperando alguém começar o campeonato
    VC_WAITING_READY,   // mostrando o alvo, esperando o jogador da vez tocar PRONTO
    VC_CALIBRATING,     // ligando o giroscópio e tirando o zero
    VC_MEASURING,       // girando/inclinando; só dica sonora, sem número ao vivo
    VC_CONFIRMING,      // "parou" detectado — contagem de 3s pra consolidar
    VC_RESULT,          // mostrando o resultado da tentativa
    VC_DONE,            // campeonato acabou, mostrando o vencedor
} vc_phase_t;

typedef enum { TIER_PERFEITO, TIER_OTIMO, TIER_QUASE, TIER_ERROU } vc_tier_t;

static const char VC_RULES[] =
    "Passa o KIT de m\xC3\xA3o em m\xC3\xA3o. Cada rodada tem UM alvo s\xC3\xB3, "
    "igual pra todo mundo.\n\n"
    "1. No AJUSTE, escolha quantos jogadores v\xC3\xA3o jogar e quantas "
    "rodadas. Toque em COME\xC3\x87" "AR.\n\n"
    "2. A tela mostra o alvo (\"GIRE 130 \xC3\x80 ESQUERDA\" ou \"INCLINE 40 "
    "PRA BAIXO\"). O jogador da vez toca PRONTO, gira ou inclina o KIT "
    "tentando parar bem em cima do n\xC3\xBAmero, e fica quieto um instante "
    "pra confirmar que parou (ou toca PARAR pra travar na hora).\n\n"
    "3. Quanto mais perto do alvo, mais pontos: PERFEITO, \xC3\x93TIMO, QUASE "
    "ou ERROU. Os \xC3\xA2ngulos ficam mais esquisitos e a toler\xC3\xA2ncia "
    "mais apertada a cada rodada - a \xC3\xBAltima passa de uma volta "
    "inteira.\n\n"
    "4. No fim das rodadas, quem tiver mais pontos no PLACAR vence.";

// --- estado --------------------------------------------------------------

static const kit_api_table_t *s_api = NULL;
static uint32_t   s_accent = KIT_COLOR_BLUE;
static vc_phase_t s_phase  = VC_IDLE;

static int s_cfg_players     = 4;   // 2..8, vai pro Storage
static int s_cfg_rounds_idx  = 1;   // índice em ROUND_OPTIONS (padrão: 5 rodadas)

static int s_total_players;
static int s_total_rounds;
static int s_scores[MAX_PLAYERS];
static int s_round;        // 1-based
static int s_player_idx;   // 0-based

static int32_t s_target_cd;      // com sinal: + = direita/baixo, - = esquerda/cima
static bool    s_target_incline; // true = eixo de inclinar; false = eixo de girar
static int32_t s_tol_perfect_cd, s_tol_otimo_cd, s_tol_quase_cd;

static bool     s_has_moved;
static uint32_t s_quiet_ms;
static uint32_t s_measuring_ms;
static uint32_t s_confirm_ms;
static int32_t  s_last_live_cd;

static int32_t   s_result_error_cd;
static vc_tier_t s_result_tier;
static int       s_result_points;

static lv_timer_t *s_timer = NULL;

// --- objetos LVGL ------------------------------------------------------

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv     = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];
static lv_obj_t *s_lead_in  = NULL;
static lv_obj_t *s_phrase   = NULL;   // número protagonista (kit_display_72)
static lv_obj_t *s_lead_out = NULL;
static lv_obj_t *s_go_btn   = NULL;
static lv_obj_t *s_go_lbl   = NULL;

static lv_obj_t *s_players_lbl = NULL;
static lv_obj_t *s_players_minus = NULL;
static lv_obj_t *s_players_plus  = NULL;
static lv_obj_t *s_rounds_chips[ROUND_OPTIONS_N];
static lv_obj_t *s_rounds_chip_lbls[ROUND_OPTIONS_N];

static lv_obj_t *s_placar_rows   = NULL;
static lv_obj_t *s_placar_banner = NULL;

// --- helpers -----------------------------------------------------------

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

static uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

static int winner_index(void)
{
    int best = 0;
    for (int i = 1; i < s_total_players; i++)
        if (s_scores[i] > s_scores[best]) best = i;
    return best;
}

static const char *axis_word(void) { return s_target_incline ? "INCLINE" : "GIRE"; }

static const char *dir_word(bool positive)
{
    if (s_target_incline) return positive ? "PRA BAIXO" : "PRA CIMA";
    return positive ? "\xC3\x80 DIREITA" : "\xC3\x80 ESQUERDA";
}

static int rnd_range(int lo, int hi)
{
    if (s_api && s_api->random) return (int)s_api->random->range(lo, hi);
    return lo;
}

static void audio_fuse(int16_t t)  { if (s_api && s_api->audio) s_api->audio->fuse(t); }
static void audio_sfx(kit_sfx_t s) { if (s_api && s_api->audio) s_api->audio->sfx(s); }

// --- persistência (Storage API) --------------------------------------------

static void load_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    int32_t v;
    if (s_api->storage->get_i32(K_PLAYERS, &v) == KIT_OK && v >= 2 && v <= MAX_PLAYERS)
        s_cfg_players = (int)v;
    if (s_api->storage->get_i32(K_ROUNDS, &v) == KIT_OK && v >= 0 && v < ROUND_OPTIONS_N)
        s_cfg_rounds_idx = (int)v;
}

static void save_prefs(void)
{
    if (!s_api || !s_api->storage) return;
    s_api->storage->set_i32(K_PLAYERS, s_cfg_players);
    s_api->storage->set_i32(K_ROUNDS, s_cfg_rounds_idx);
}

// --- palco / sincronização de UI --------------------------------------

static void set_stage(const char *lead_in, const char *num, uint32_t num_color,
                      const char *lead_out)
{
    if (!s_phrase) return;

    if (lead_in && lead_in[0]) {
        lv_label_set_text(s_lead_in, lead_in);
        lv_obj_remove_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_phrase, num);
    lv_obj_set_style_text_color(s_phrase, lv_color_hex(num_color), 0);

    if (lead_out && lead_out[0]) {
        lv_label_set_text(s_lead_out, lead_out);
        lv_obj_remove_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_lead_in_font(const lv_font_t *f)
{
    if (s_lead_in) lv_obj_set_style_text_font(s_lead_in, f, 0);
}

static void sync_button(void)
{
    if (!s_go_lbl) return;
    switch (s_phase) {
    case VC_IDLE:          lv_label_set_text(s_go_lbl, "COME\xC3\x87" "AR"); break;
    case VC_WAITING_READY: lv_label_set_text(s_go_lbl, "PRONTO"); break;
    case VC_CALIBRATING:   lv_label_set_text(s_go_lbl, "CALIBRANDO..."); break;
    case VC_MEASURING:     lv_label_set_text(s_go_lbl, "PARAR"); break;
    case VC_CONFIRMING:    lv_label_set_text(s_go_lbl, "PARAR"); break;
    case VC_RESULT:        lv_label_set_text(s_go_lbl, "PR\xC3\x93XIMO"); break;
    case VC_DONE:          lv_label_set_text(s_go_lbl, "JOGAR DE NOVO"); break;
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

static void dim_step(lv_obj_t *btn, bool disabled)
{
    if (btn) lv_obj_set_style_opa(btn, disabled ? LV_OPA_30 : LV_OPA_COVER, 0);
}

static void sync_players_lbl(void)
{
    if (s_players_lbl) lv_label_set_text_fmt(s_players_lbl, "%d", s_cfg_players);
    bool locked = (s_phase != VC_IDLE);
    dim_step(s_players_minus, locked || s_cfg_players <= 2);
    dim_step(s_players_plus,  locked || s_cfg_players >= MAX_PLAYERS);
}

static void sync_rounds_chips(void)
{
    for (int i = 0; i < ROUND_OPTIONS_N; i++) {
        bool sel = (i == s_cfg_rounds_idx);
        lv_obj_set_style_bg_color(s_rounds_chips[i],
            lv_color_hex(sel ? s_accent : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(s_rounds_chip_lbls[i],
            lv_color_hex(sel ? on_accent() : KIT_COLOR_TEXT), 0);
    }
}

static void sync_placar(void)
{
    if (!s_placar_rows) return;
    lv_obj_clean(s_placar_rows);
    int lead = (s_total_players > 0) ? winner_index() : 0;

    for (int i = 0; i < s_total_players; i++) {
        lv_obj_t *row = plain_box(s_placar_rows);
        lv_obj_set_size(row, lv_pct(100), 52);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(KIT_COLOR_LINE), 0);

        bool is_lead = (i == lead) && s_scores[i] > 0;
        uint32_t c = is_lead ? s_accent : KIT_COLOR_TEXT;

        char name[16];
        snprintf(name, sizeof(name), "JOGADOR %d", i + 1);
        lv_obj_t *k = add_label(row, name, c, &kit_mono_16, 1);
        lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);

        char sc[8];
        snprintf(sc, sizeof(sc), "%d", s_scores[i]);
        lv_obj_t *v = add_label(row, sc, c, &kit_mono_26, 0);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    if (s_phase == VC_DONE && s_placar_banner) {
        char buf[32];
        snprintf(buf, sizeof(buf), "JOGADOR %d VENCEU!", lead + 1);
        lv_label_set_text(s_placar_banner, buf);
        lv_obj_remove_flag(s_placar_banner, LV_OBJ_FLAG_HIDDEN);
    } else if (s_placar_banner) {
        lv_obj_add_flag(s_placar_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

// Composição do palco por fase — número grande sempre em kit_display_72 (só
// dígitos/A-Z/hífen, por isso "GRAUS" vai por extenso: essa fonte não tem "°").
static void sync_stage(void)
{
    char lead_in[48], num[16], lead_out[56];

    set_lead_in_font(&kit_mono_20);   // padrão; VC_RESULT aumenta pra destacar o veredito
    if (s_lead_in) lv_obj_set_style_text_color(s_lead_in, lv_color_hex(KIT_COLOR_TEXT), 0);

    switch (s_phase) {
    case VC_IDLE:
        set_stage(NULL, "0", KIT_COLOR_TEXT_MUTED, "TOQUE EM COME\xC3\x87" "AR");
        break;

    case VC_WAITING_READY:
    case VC_MEASURING:
        snprintf(lead_in, sizeof(lead_in), "RODADA %d/%d \xC2\xB7 JOGADOR %d/%d",
                 s_round, s_total_rounds, s_player_idx + 1, s_total_players);
        snprintf(num, sizeof(num), "%d", (int)(iabs32(s_target_cd) / 100));
        snprintf(lead_out, sizeof(lead_out), "%s %s",
                 axis_word(), dir_word(s_target_cd >= 0));
        set_stage(lead_in, num, s_accent, lead_out);
        break;

    case VC_CALIBRATING:
        set_stage("CALIBRANDO...", "0", KIT_COLOR_TEXT_MUTED, NULL);
        break;

    case VC_CONFIRMING: {
        int sec = 3 - (int)(s_confirm_ms / 1000);
        if (sec < 1) sec = 1;
        snprintf(num, sizeof(num), "%d", sec);
        set_stage("PAROU! SEGURE FIRME...", num, KIT_COLOR_YELLOW, "N\xC3\x83O SE MEXA");
        break;
    }

    case VC_RESULT: {
        const char *tier_txt =
            s_result_tier == TIER_PERFEITO ? "PERFEITO!" :
            s_result_tier == TIER_OTIMO    ? "\xC3\x93TIMO!" :
            s_result_tier == TIER_QUASE    ? "QUASE!" : "ERROU!";
        uint32_t c =
            (s_result_tier == TIER_PERFEITO || s_result_tier == TIER_OTIMO) ? KIT_COLOR_GREEN :
            (s_result_tier == TIER_QUASE) ? KIT_COLOR_YELLOW : KIT_COLOR_RED;
        set_lead_in_font(&kit_mono_26);   // o veredito é o texto de maior destaque da tela
        if (s_lead_in) lv_obj_set_style_text_color(s_lead_in, lv_color_hex(c), 0);
        snprintf(num, sizeof(num), "%d", (int)((s_result_error_cd + 50) / 100));
        snprintf(lead_out, sizeof(lead_out), "GRAUS DE ERRO \xC2\xB7 +%d PONTOS", s_result_points);
        set_stage(tier_txt, num, c, lead_out);
        break;
    }

    case VC_DONE: {
        int w = winner_index();
        snprintf(num, sizeof(num), "%d", w + 1);
        set_stage("FIM DE JOGO", num, KIT_COLOR_GREEN, "JOGADOR VENCEDOR \xC2\xB7 VEJA O PLACAR");
        break;
    }
    }
    sync_button();
}

// --- rodada / dificuldade dinâmica -----------------------------------------

// prg = progresso 0..1000 (permilagem) conforme a rodada avança dentro do
// total escolhido — a mesma fórmula serve pra "melhor de 3", "5" ou "7".
static void generate_target(void)
{
    int prg = (s_total_rounds > 1)
        ? (s_round - 1) * 1000 / (s_total_rounds - 1) : 1000;
    bool is_first = (s_round == 1);
    bool is_last  = (s_round == s_total_rounds);

    int mag_deg;
    bool incline = false;

    if (is_first) {
        mag_deg = 90;                       // rodada de aquecimento: sempre fácil
    } else if (is_last) {
        mag_deg = 360 + 20 + rnd_range(0, 130);   // rodada final: mais de uma volta
    } else {
        int range_max = 90 + prg * 330 / 1000;
        if (range_max < 40) range_max = 40;
        do {
            mag_deg = 20 + rnd_range(0, range_max - 20);
        } while (mag_deg % 45 == 0);        // evita número "redondo" de contar de cabeça

        int incline_chance_pm = prg * 500 / 1000;   // 0..500 (‰)
        incline = (rnd_range(0, 999) < incline_chance_pm);
    }

    bool positive = (rnd_range(0, 1) == 1);
    s_target_cd      = (positive ? mag_deg : -mag_deg) * 100;
    s_target_incline = incline;

    s_tol_perfect_cd = 800  - prg * 500  / 1000;   // 8,00° -> 3,00°
    s_tol_otimo_cd   = 2000 - prg * 1200 / 1000;   // 20°  -> 8°
    s_tol_quase_cd   = 4000 - prg * 2200 / 1000;   // 40°  -> 18°
}

static void start_championship(void)
{
    s_total_players = s_cfg_players;
    s_total_rounds  = ROUND_OPTIONS[s_cfg_rounds_idx];
    for (int i = 0; i < MAX_PLAYERS; i++) s_scores[i] = 0;
    s_round = 1;
    s_player_idx = 0;

    // Liga o giroscópio uma vez pro campeonato inteiro — cada tentativa só
    // re-zera (gyro_rezero()). Religar o sensor a cada "PRONTO" puxa corrente
    // extra do PMIC e pisca o AMOLED.
    if (s_api && s_api->imu && s_api->imu->gyro_start) s_api->imu->gyro_start();

    generate_target();
    s_phase = VC_WAITING_READY;
    sync_players_lbl();
    sync_stage();
    sync_placar();
}

static void advance_after_result(void)
{
    s_player_idx++;
    if (s_player_idx >= s_total_players) {
        s_player_idx = 0;
        s_round++;
        if (s_round > s_total_rounds) {
            if (s_api && s_api->imu && s_api->imu->gyro_stop) s_api->imu->gyro_stop();
            s_phase = VC_DONE;
            sync_stage();
            sync_placar();
            if (s_tv) lv_tileview_set_tile_by_index(s_tv, 2, 0, LV_ANIM_ON);
            return;
        }
        generate_target();
    }
    s_phase = VC_WAITING_READY;
    sync_stage();
}

static void lock_result(int32_t measured_cd)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    audio_fuse(-1);   // apaga a dica sonora de quente/frio

    int32_t error_cd = iabs32(measured_cd - s_target_cd);
    vc_tier_t tier;
    int points;
    if (error_cd <= s_tol_perfect_cd)    { tier = TIER_PERFEITO; points = 100; }
    else if (error_cd <= s_tol_otimo_cd) { tier = TIER_OTIMO;    points = 70;  }
    else if (error_cd <= s_tol_quase_cd) { tier = TIER_QUASE;    points = 40;  }
    else                                 { tier = TIER_ERROU;    points = 10;  }

    s_result_error_cd = error_cd;
    s_result_tier     = tier;
    s_result_points   = points;
    s_scores[s_player_idx] += points;

    s_phase = VC_RESULT;
    audio_sfx(tier == TIER_ERROU ? KIT_SFX_VETO_FOUL : KIT_SFX_CONFIRM);

    sync_stage();
    sync_placar();
}

// Um só timer cobre MEASURING e CONFIRMING (troca de fase no meio do caminho,
// sem recriar o lv_timer). A cada tick: lê o giroscópio, toca a dica sonora de
// "quente/frio" (audio -> fuse) e decide se abre a confirmação, volta a medir
// ou consolida.
static void gyro_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_api || !s_api->imu || !s_api->imu->gyro_poll) return;

    int32_t yaw_cd, pitch_cd, rate_cd;
    if (!s_api->imu->gyro_poll(&yaw_cd, &pitch_cd, NULL, &rate_cd)) return;

    int32_t live_cd = s_target_incline ? pitch_cd : yaw_cd;
    s_last_live_cd = live_cd;

    if (s_phase == VC_MEASURING) {
        int32_t error_cd = iabs32(live_cd - s_target_cd);
        if (error_cd > VC_TENSION_RANGE_CDEG) error_cd = VC_TENSION_RANGE_CDEG;
        int tension = 255 - (int)(error_cd * 255 / VC_TENSION_RANGE_CDEG);
        if (tension < 0) tension = 0;
        if (tension > 255) tension = 255;
        audio_fuse((int16_t)tension);

        s_measuring_ms += GYRO_POLL_MS;
        if (!s_has_moved) {
            if (rate_cd > VC_MOVE_THRESHOLD_CDPS) s_has_moved = true;
        } else if (rate_cd < VC_SETTLE_THRESHOLD_CDPS) {
            s_quiet_ms += GYRO_POLL_MS;
        } else {
            s_quiet_ms = 0;
        }

        if (s_has_moved && s_quiet_ms >= VC_PRESETTLE_MS) {
            audio_fuse(-1);
            s_confirm_ms = 0;
            s_phase = VC_CONFIRMING;
            sync_stage();
            return;
        }
        if (s_measuring_ms >= VC_ATTEMPT_TIMEOUT_MS) lock_result(live_cd);
        return;
    }

    // VC_CONFIRMING: mexeu de novo antes dos 3s — desiste da confirmação e
    // volta a medir (a dica de quente/frio religa no próximo tick).
    if (rate_cd > VC_MOVE_THRESHOLD_CDPS) {
        s_quiet_ms = 0;
        s_phase = VC_MEASURING;
        sync_stage();
        return;
    }

    int prev_sec = 3 - (int)(s_confirm_ms / 1000);
    s_confirm_ms += GYRO_POLL_MS;
    int cur_sec = 3 - (int)(s_confirm_ms / 1000);
    if (cur_sec != prev_sec && cur_sec >= 1) {
        sync_stage();
        audio_sfx(KIT_SFX_TIMER_TICK);
    }
    if (s_confirm_ms >= VC_CONFIRM_MS) lock_result(live_cd);
}

static void calib_kickoff_cb(lv_timer_t *t)
{
    (void)t;
    s_timer = NULL;

    if (s_api && s_api->imu && s_api->imu->gyro_rezero) s_api->imu->gyro_rezero();

    s_has_moved = false;
    s_quiet_ms = 0;
    s_measuring_ms = 0;
    s_confirm_ms = 0;
    s_last_live_cd = 0;
    s_phase = VC_MEASURING;
    sync_stage();

    s_timer = lv_timer_create(gyro_tick_cb, GYRO_POLL_MS, NULL);
}

// Um tick vazio antes de calibrar de verdade — dá tempo do LVGL pintar
// "CALIBRANDO..." antes do bloqueio curto do gyro_rezero().
static void begin_calibration(void)
{
    s_phase = VC_CALIBRATING;
    sync_stage();
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_timer = lv_timer_create(calib_kickoff_cb, 30, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}

// --- ação principal (toque na tela / botão) -------------------------------

static void game_action(void)
{
    if (!s_screen) return;
    switch (s_phase) {
    case VC_IDLE:
    case VC_DONE:
        start_championship();
        break;
    case VC_WAITING_READY:
        begin_calibration();
        break;
    case VC_CALIBRATING:
        break;   // ignora — ainda calibrando
    case VC_MEASURING:
    case VC_CONFIRMING:
        lock_result(s_last_live_cd);   // "PARAR" manual — trava na hora
        break;
    case VC_RESULT:
        advance_after_result();
        break;
    }
}

// --- callbacks de UI -------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_api && s_api->system) s_api->system->exit();
}

static void action_cb(lv_event_t *e) { (void)e; game_action(); }
static void tv_changed_cb(lv_event_t *e) { (void)e; sync_dots(); }

static void players_step_cb(lv_event_t *e)
{
    if (s_phase != VC_IDLE) return;
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int v = s_cfg_players + delta;
    if (v < 2) v = 2;
    if (v > MAX_PLAYERS) v = MAX_PLAYERS;
    if (v == s_cfg_players) return;
    s_cfg_players = v;
    sync_players_lbl();
    save_prefs();
}

static void rounds_chip_cb(lv_event_t *e)
{
    if (s_phase != VC_IDLE) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= ROUND_OPTIONS_N) return;
    s_cfg_rounds_idx = i;
    sync_rounds_chips();
    save_prefs();
}

// --- construção da tela --------------------------------------------------

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, V_CHIP, V_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, V_PAD, 16);

    lv_obj_t *g = add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(g);

    lv_obj_t *title = add_label(s_screen, "VIRA CERTO", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, V_PAD + V_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -V_PAD, 40);
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

static lv_obj_t *make_step_btn(lv_obj_t *parent, const char *sym, int delta)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, V_STEP, V_STEP);
    lv_obj_set_style_bg_color(b, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 12);
    lv_obj_add_event_cb(b, players_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)delta);
    lv_obj_t *l = add_label(b, sym, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(l);
    return b;
}

// Grade de chips, no máximo 2 por linha e bem altos — padrão do Estouro.
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

        int in_row = (count - idx < 2) ? (count - idx) : 2;
        for (int k = 0; k < in_row; k++, idx++) {
            lv_obj_t *c = lv_obj_create(row);
            lv_obj_set_height(c, 84);
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

            lv_obj_t *l = add_label(c, labels[idx], KIT_COLOR_TEXT, &kit_mono_26, 1);
            lv_obj_center(l);

            out_chips[idx] = c;
            out_lbls[idx]  = l;
        }
    }
}

// Página AJUSTE: nº de jogadores (stepper) e nº de rodadas (chips).
static void build_page_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, V_PAD, 0);
    lv_obj_set_style_pad_right(p, V_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 22, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *sec_p = plain_box(p);
    lv_obj_set_size(sec_p, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec_p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec_p, 9, 0);
    field_label(sec_p, "JOGADORES");

    lv_obj_t *row = plain_box(sec_p);
    lv_obj_set_size(row, lv_pct(100), V_STEP);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    s_players_minus = make_step_btn(row, "-", -1);
    s_players_lbl = add_label(row, "4", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_set_width(s_players_lbl, 76);
    lv_obj_set_style_text_align(s_players_lbl, LV_TEXT_ALIGN_CENTER, 0);
    s_players_plus = make_step_btn(row, "+", 1);

    lv_obj_t *sec_r = plain_box(p);
    lv_obj_set_size(sec_r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec_r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec_r, 9, 0);
    field_label(sec_r, "RODADAS");
    static const char *ROUND_LABELS[ROUND_OPTIONS_N] = { "3", "5", "7" };
    build_chip_grid(sec_r, ROUND_LABELS, ROUND_OPTIONS_N, rounds_chip_cb,
                    s_rounds_chips, s_rounds_chip_lbls);
}

// Página JOGO: palco (toque em qualquer lugar = mesma ação do botão) + botão
// fixo no rodapé DESTE tile.
static void build_game_page(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *stage = lv_obj_create(tile);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, KIT_DISPLAY_WIDTH, V_STAGE_H);
    lv_obj_set_pos(stage, 0, 0);
    lv_obj_remove_flag(stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stage, action_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(stage);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 10, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_center(col);

    s_lead_in = add_label(col, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_set_width(s_lead_in, V_CONTENT);
    lv_obj_set_style_text_align(s_lead_in, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);

    s_phrase = add_label(col, "0", KIT_COLOR_TEXT_MUTED, &kit_display_72, 0);
    lv_obj_set_width(s_phrase, V_CONTENT);
    lv_obj_set_style_text_align(s_phrase, LV_TEXT_ALIGN_CENTER, 0);

    s_lead_out = add_label(col, "", KIT_COLOR_TEXT, &kit_mono_20, 1);
    lv_label_set_long_mode(s_lead_out, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lead_out, V_CONTENT);
    lv_obj_set_style_text_align(s_lead_out, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);

    s_go_btn = lv_obj_create(tile);
    lv_obj_set_size(s_go_btn, V_CONTENT, V_GO_H);
    lv_obj_set_style_radius(s_go_btn, V_GO_H / 2, 0);
    lv_obj_set_style_border_width(s_go_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_go_btn, 0, 0);
    lv_obj_set_style_pad_all(s_go_btn, 0, 0);
    lv_obj_set_style_bg_color(s_go_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_go_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_go_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_go_btn, 8);
    lv_obj_align(s_go_btn, LV_ALIGN_BOTTOM_MID, 0, -V_GO_MARGIN);
    lv_obj_add_event_cb(s_go_btn, action_cb, LV_EVENT_CLICKED, NULL);

    s_go_lbl = add_label(s_go_btn, "COME\xC3\x87" "AR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_go_lbl);
}

// Página PLACAR: uma linha por jogador, líder destacado; banner de vencedor
// no fim do campeonato.
static void build_page_placar(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, V_PAD, 0);
    lv_obj_set_style_pad_right(p, V_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 10, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    s_placar_banner = add_label(p, "", KIT_COLOR_GREEN, &kit_mono_20, 2);
    lv_obj_add_flag(s_placar_banner, LV_OBJ_FLAG_HIDDEN);

    field_label(p, "PLACAR");

    s_placar_rows = plain_box(p);
    lv_obj_set_size(s_placar_rows, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_placar_rows, LV_FLEX_FLOW_COLUMN);
}

// Página COMO JOGA: regras, corpo rolável (padrão da Mímica/Estouro).
static void build_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, V_PAD, 0);
    lv_obj_set_style_pad_right(p, V_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, VC_RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, V_CONTENT);
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, KIT_DISPLAY_WIDTH, V_PAGE_H);
    lv_obj_set_pos(s_tv, 0, V_TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);
    s_tiles[3] = lv_tileview_add_tile(s_tv, 3, 0, LV_DIR_HOR);

    build_page_ajuste(s_tiles[0]);
    build_game_page(s_tiles[1]);
    build_page_placar(s_tiles[2]);
    build_help(s_tiles[3]);
}

// ---------------------------------------------------------------------------
// Ciclo de vida da Tool
// ---------------------------------------------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    printf("[Vira Certo] tool_init\n");
    s_api = ctx ? ctx->api : NULL;

    s_accent = KIT_COLOR_BLUE;
    s_phase  = VC_IDLE;
    s_cfg_players = 4;
    s_cfg_rounds_idx = 1;
    for (int i = 0; i < MAX_PLAYERS; i++) s_scores[i] = 0;
    load_prefs();

    // O gesto de girar/inclinar É o próprio jogo — não registramos callback de
    // chacoalhar: pegar/passar o aparelho não pode disparar nada.

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();
    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   // sempre abre no JOGO

    sync_stage();
    sync_players_lbl();
    sync_rounds_chips();
    sync_placar();
    sync_dots();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Vira Certo] tool_destroy\n");
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_api && s_api->imu && s_api->imu->gyro_stop) s_api->imu->gyro_stop();
    audio_fuse(-1);
    s_phase = VC_IDLE;

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < ROUND_OPTIONS_N; i++) { s_rounds_chips[i] = NULL; s_rounds_chip_lbls[i] = NULL; }
    s_lead_in = s_phrase = s_lead_out = s_go_btn = s_go_lbl = NULL;
    s_players_lbl = s_players_minus = s_players_plus = NULL;
    s_placar_rows = s_placar_banner = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo para testes e CI */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Vira Certo stub] tool_init — compila nativo; UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
