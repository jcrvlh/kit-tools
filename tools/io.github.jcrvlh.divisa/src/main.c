/**
 * @file main.c
 * @brief DIVISA — duelo de toques pra 2: a tela dividida, quem toca mais
 *        conquista território. O placar só aparece na apuração.
 *
 * O KIT fica deitado na mesa entre os dois: a metade de baixo é do vermelho,
 * a de cima é do azul. Cada rodada é uma sequência de janelas curtas: a sua
 * metade acende, você toca o mais rápido que puder; apaga e acende a do outro
 * pelo MESMO tempo (sorteado por par). O número de pares também é sorteado:
 * ninguém sabe quando a rodada acaba. Ninguém vê o placar. No fim, a
 * apuração conta os toques rodada a rodada e a divisa anda de verdade pra
 * quem tocou mais. Ganha quem terminar com mais território.
 *
 * Decisões que não são óbvias:
 *  - Janelas alternadas, não toque simultâneo: o CST820 é single-touch (o
 *    firmware só lê 0 ou 1 ponto). Dois dedos ao mesmo tempo = um bloqueia o
 *    outro. Por isso também ENCOSTAR NA PRÓPRIA METADE FORA DA VEZ desconta
 *    (1 na hora + 1 a cada PEN_MS): dedo plantado pra travar o rival sai caro.
 *  - Folga de GAP_MS entre janelas sem contar nada: o dedo de quem acabou de
 *    tocar ainda está na tela quando a vez troca.
 *  - O jogador de cima lê de cabeça pra baixo e a rotação de objeto não está
 *    na tabela de símbolos: o percentual dele usa os glifos do display_72
 *    girados 180° (digits_rot.h, gerado por scripts/make_digits.py). Menus
 *    ficam pra quem está do lado de baixo.
 *  - Apuração honesta: o vai-e-vem da divisa vem dos dados (ticks
 *    intercalados por rodada), não de animação inventada.
 *  - Partida não sobrevive a fechar/reabrir: é um duelo de reflexo de ~1 min.
 *    Só os ajustes persistem.
 *  - Inteiro puro (o .so não resolve float). Lógica em divisa_game.c.
 */
#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "divisa_game.h"

#include <stdio.h>
#include <string.h>

#ifndef KIT_SDK_STUBS

#include "digits_rot.h"

/* ----------------------------------------------------------------------- */

#define D_ACCENT    KIT_COLOR_YELLOW          /* neutro entre os dois lados */
#define D_ON_ACCENT KIT_COLOR_ON_YELLOW

#define W           KIT_UI_SCREEN_W           /* 368 */
#define H           KIT_UI_SCREEN_H           /* 448 */
#define HALF        (H / 2)
#define DIV_LINE    6
#define CHIP        56

#define GAP_MS          300   /* entre janelas: ninguém pontua */
#define BEAT_MS         650   /* contagem de entrada: 3 bipes + VAI */
#define BREAK_MS        1800  /* entre rodadas */
#define PEN_MS          125   /* desconto contínuo por dedo plantado fora da vez */
#define COUNT_PAUSE_MS  1100  /* entre a apuração de uma rodada e a próxima */
#define FINAL_PAUSE_MS  1700  /* antes de apurar a última: tambor */
#define SHOW_MS         2400  /* percentuais na tela antes da conquista */
#define FLOOD_FRAMES    14
#define FLOOD_TICK_MS   40
#define ABORT_HOLD_MS   900

#define PCT_NUDGE   5     /* '%' do display_44 desce 5 px em relação à base do 72 */
#define JUMP_PX     90    /* salto entre 2 leituras do touch acima disto = 2º dedo, não deslize */

#define K_ROUNDS "div_rod"
#define K_PACE   "div_rit"

static const uint32_t SIDE_COLOR[2] = { KIT_COLOR_RED, KIT_COLOR_BLUE };
static const uint16_t SIDE_TONE[2]  = { 392, 659 };     /* abre a janela */
static const uint16_t SIDE_TAP[2]   = { 1047, 1397 };   /* cada toque */

static const char *const ROUNDS_LABELS[] = { "3 RODADAS", "5 RODADAS" };
static const int         ROUNDS_N[]      = { 3, 5 };
static const char *const PACE_LABELS[]   = { "NORMAL", "FREN\xC3\x89TICO" };

static const char RULES[] =
    "Pra 2 pessoas, com o KIT deitado na mesa entre as duas. A metade de "
    "baixo \xC3\xA9 do vermelho, a de cima \xC3\xA9 do azul.\n\n"
    "1. Toque em COME\xC3\x87" "AR. Depois de 3 bipes, valendo.\n\n"
    "2. Quando a sua metade acender, toque nela o mais r\xC3\xA1pido que "
    "puder. Ela apaga e acende a do outro, v\xC3\xA1rias vezes, sempre com o "
    "mesmo tempo pros dois.\n\n"
    "3. Fora da sua vez, n\xC3\xA3o encoste na sua metade: cada instante "
    "encostado desconta toques seus.\n\n"
    "4. Ningu\xC3\xA9m v\xC3\xAA o placar durante o jogo, e a dura\xC3\xA7\xC3\xA3o "
    "de cada rodada \xC3\xA9 sorteada.\n\n"
    "5. No fim vem a apura\xC3\xA7\xC3\xA3o: rodada por rodada, a divisa anda "
    "pra quem tocou mais. Ganha quem terminar com mais territ\xC3\xB3rio.\n\n"
    "6. Empatou? Desempate: uma rodada a mais decide.\n\n"
    "Pra sair no meio, segure o X na divisa entre as rodadas.";

typedef enum {
    A_OFF = 0, A_INTRO, A_GAP, A_WINDOW, A_BREAK, A_COUNT, A_SHOW, A_FLOOD, A_DONE, A_TIE
} arena_state_t;

/* ------------------------------------------------------------------ estado */

static const kit_api_table_t *s_api;
static lv_obj_t *s_screen;
static kit_ui_shell_t  s_shell;
static kit_ui_chips_t  s_rounds_chips;
static kit_ui_chips_t  s_pace_chips;
static kit_ui_action_t s_action;

static int  s_rounds_idx;          /* 0 = 3 rodadas, 1 = 5 */
static int  s_pace_idx;            /* 0 = normal, 1 = frenético */

static arena_state_t s_st;
static div_plan_t    s_plan;
static div_score_t   s_score[DIV_MAX_ROUNDS];
static int  s_nrounds, s_round, s_win, s_active, s_coin;
static int  s_held = -1;           /* metade com dedo encostado agora */
static int  s_raw_y = -1, s_raw_prev_y = -1;   /* 2 últimas leituras cruas do touch */
static int  s_beat;
static int  s_cround, s_k, s_red, s_blue, s_top_px;
static int  s_flood_from, s_flood_to, s_flood_frame, s_winner;

/* JOGO (parado) */
static lv_obj_t *s_idle_info;

/* arena */
static lv_obj_t *s_arena, *s_half[2], *s_divline, *s_dots_box, *s_dot[DIV_MAX_ROUNDS];
static lv_obj_t *s_abort, *s_abort_lbl;
static lv_obj_t *s_up_row, *s_up_num, *s_up_pct;
static lv_obj_t *s_rot_row, *s_rot_img[4];
static lv_obj_t *s_btn_row, *s_btn_a, *s_btn_a_lbl, *s_btn_b, *s_btn_b_lbl;

/* timers */
static lv_timer_t *s_step_timer, *s_pen_timer, *s_abort_timer;
static void (*s_step_fn)(void);

/* -------------------------------------------------------------------- util */

static int32_t rng(int32_t lo, int32_t hi) { return kit_ui_rnd(lo, hi); }

static int32_t get_i32(const char *key, int32_t def)
{
    int32_t v;
    if (s_api && s_api->storage && s_api->storage->get_i32(key, &v) == KIT_OK) return v;
    return def;
}

static void set_i32(const char *key, int32_t v)
{
    if (s_api && s_api->storage) s_api->storage->set_i32(key, v);
}

static void show(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void kill_timer(lv_timer_t **t)
{
    if (*t) { lv_timer_delete(*t); *t = NULL; }
}

static void step_cb(lv_timer_t *t)
{
    (void)t;
    kill_timer(&s_step_timer);
    void (*fn)(void) = s_step_fn;
    s_step_fn = NULL;
    if (fn) fn();
}

/* agenda o próximo passo da arena (um só por vez) */
static void step_after(uint32_t ms, void (*fn)(void))
{
    kill_timer(&s_step_timer);
    s_step_fn = fn;
    s_step_timer = lv_timer_create(step_cb, ms ? ms : 1, NULL);
}

static void fuse(int16_t tension)
{
    if (s_api && s_api->audio && s_api->audio->fuse) s_api->audio->fuse(tension);
}

/* ---------------------------------------------------------------- pintura */

static void paint_idle(void)
{
    lv_label_set_text_fmt(s_idle_info, "%s \xC2\xB7 %s",
                          ROUNDS_LABELS[s_rounds_idx], PACE_LABELS[s_pace_idx]);
}

/* território: altura do azul (cima); o vermelho fica com o resto */
static void set_territory(int top_px)
{
    if (top_px < 0) top_px = 0;
    if (top_px > H) top_px = H;
    s_top_px = top_px;
    lv_obj_set_pos(s_half[DIV_BLUE], 0, 0);
    lv_obj_set_size(s_half[DIV_BLUE], W, top_px);
    lv_obj_set_pos(s_half[DIV_RED], 0, top_px);
    lv_obj_set_size(s_half[DIV_RED], W, H - top_px);

    int ly = top_px - DIV_LINE / 2;
    if (ly < 0) ly = 0;
    if (ly > H - DIV_LINE) ly = H - DIV_LINE;
    lv_obj_set_pos(s_divline, 0, ly);
    lv_obj_align(s_dots_box, LV_ALIGN_TOP_MID, 0, ly + DIV_LINE / 2 - 14);
    lv_obj_set_pos(s_abort, W - KIT_UI_PAD - CHIP, ly + DIV_LINE / 2 - CHIP / 2);
}

/* opacidade de cada metade conforme o estado */
static void paint_halves(void)
{
    for (int s = 0; s < 2; s++) {
        lv_opa_t opa;
        switch (s_st) {
        case A_WINDOW: opa = (s == s_active) ? LV_OPA_COVER : LV_OPA_10; break;
        case A_INTRO: case A_GAP: case A_BREAK: opa = LV_OPA_20; break;
        default: opa = LV_OPA_COVER; break;
        }
        lv_obj_set_style_bg_color(s_half[s], lv_color_hex(SIDE_COLOR[s]), 0);
        lv_obj_set_style_bg_opa(s_half[s], opa, 0);
    }
}

/* bolinhas das rodadas: feitas = paper, atual = amarelo, futuras = linha */
static void paint_dots(int current)
{
    for (int i = 0; i < DIV_MAX_ROUNDS; i++) {
        show(s_dot[i], i < s_nrounds);
        uint32_t c = (i < current) ? KIT_COLOR_TEXT : (i == current ? D_ACCENT : KIT_COLOR_LINE);
        lv_obj_set_style_bg_color(s_dot[i], lv_color_hex(c), 0);
    }
}

static void show_chrome(bool dots, bool abort)
{
    show(s_divline, dots);
    show(s_dots_box, dots);
    show(s_abort, abort);
}

/* percentual "de cabeça pra baixo" com os glifos girados: % + dígitos ao contrário */
static void set_rot_value(int v)
{
    char buf[4];
    int n = 0;
    if (v >= 100) { buf[n++] = '1'; buf[n++] = '0'; buf[n++] = '0'; }
    else if (v >= 10) { buf[n++] = (char)('0' + v / 10); buf[n++] = (char)('0' + v % 10); }
    else buf[n++] = (char)('0' + v);
    for (int i = 0; i < 3; i++) {
        bool on = i < n;
        show(s_rot_img[1 + i], on);
        if (on) lv_image_set_src(s_rot_img[1 + i], DIGIT_ROT[buf[n - 1 - i] - '0']);
    }
}

static void place_upright(int yc)
{
    lv_obj_align(s_up_row, LV_ALIGN_TOP_MID, 0, yc - 40);
}

static void place_rot(int yc)
{
    lv_obj_align(s_rot_row, LV_ALIGN_TOP_MID, 0, yc - 40);
}

/* percentuais dentro de cada território (vermelho em pé, azul invertido) */
static void show_numbers(void)
{
    int pr = div_percent(s_red, s_blue, DIV_RED);
    int pb = div_percent(s_red, s_blue, DIV_BLUE);
    lv_label_set_text_fmt(s_up_num, "%d", pr);
    set_rot_value(pb);
    int yb = (s_top_px + H) / 2, yt = s_top_px / 2;
    if (yb > H - 44) yb = H - 44;
    if (yt < 44) yt = 44;
    place_upright(yb);
    place_rot(yt);
    show(s_up_row, true);
    show(s_rot_row, true);
}

static void hide_numbers(void)
{
    show(s_up_row, false);
    show(s_rot_row, false);
}

static void show_buttons(const char *a, const char *b)
{
    lv_label_set_text(s_btn_a_lbl, a);
    lv_label_set_text(s_btn_b_lbl, b);
    show(s_btn_row, true);
}

/* --------------------------------------------------------------- partida */

static void arena_close(void);
static void start_round(void);
static void open_window(void);
static void count_round_begin(void);

static void pen_stop(void) { kill_timer(&s_pen_timer); }

static void pen_tick_cb(lv_timer_t *t)
{
    (void)t;
    int s = s_held;
    if (s_st != A_WINDOW || s < 0 || s == s_active) { pen_stop(); return; }
    s_score[s_round].pen[s]++;
    kit_ui_beep(196, 40);
}

static void pen_start(void)
{
    if (!s_pen_timer) s_pen_timer = lv_timer_create(pen_tick_cb, PEN_MS, NULL);
}

static void enter_gap(void)
{
    s_st = A_GAP;
    pen_stop();
    paint_halves();
    step_after(GAP_MS, open_window);
}

static void end_round(void)
{
    s_st = A_BREAK;
    pen_stop();
    paint_halves();
    s_round++;
    paint_dots(s_round);
    show_chrome(true, true);
    kit_ui_beep(1568, 80);
    kit_ui_beep(1175, 140);
    if (s_round < s_nrounds) step_after(BREAK_MS, start_round);
    else                     step_after(BREAK_MS, count_round_begin);
}

static void close_window(void)
{
    s_win++;
    if (s_win < div_windows(&s_plan)) enter_gap();
    else                               end_round();
}

static void open_window(void)
{
    s_active = div_window_side(&s_plan, s_win);
    s_st = A_WINDOW;
    paint_halves();
    kit_ui_beep(SIDE_TONE[s_active], 70);
    /* quem ficou com o dedo na própria metade depois da troca já começa pagando */
    if (s_held >= 0 && s_held != s_active) pen_start();
    step_after((uint32_t)div_window_ms(&s_plan, s_win), close_window);
}

static void start_round(void)
{
    div_plan_round(&s_plan, s_round + s_coin, s_pace_idx ? &DIV_PACE_FRENETIC : &DIV_PACE_NORMAL, rng);
    s_win = 0;
    paint_dots(s_round);
    show_chrome(true, false);   /* sem X durante a rodada: ninguém trava ninguém */
    enter_gap();
}

static void intro_beat(void)
{
    if (s_beat < 3) {
        kit_ui_beep(880, 80);
        s_beat++;
        step_after(BEAT_MS, intro_beat);
    } else {
        kit_ui_beep(1320, 200);
        start_round();
    }
}

static void begin_match(int rounds)
{
    memset(s_score, 0, sizeof s_score);
    s_nrounds = rounds;
    s_round = 0;
    s_red = s_blue = 0;
    s_coin = rng(0, 1);
    s_st = A_INTRO;
    s_beat = 0;
    hide_numbers();
    show(s_btn_row, false);
    set_territory(HALF);
    paint_halves();
    paint_dots(0);
    show_chrome(true, true);
    kit_ui_keep_awake(true);
    step_after(BEAT_MS, intro_beat);
}

/* --- apuração ----------------------------------------------------------- */

static void show_result(void);

static void count_tick(void)
{
    int a = div_net(&s_score[s_cround], DIV_RED);
    int b = div_net(&s_score[s_cround], DIV_BLUE);
    int n = div_count_ticks(a, b);
    if (s_k < a) s_red++;
    if (s_k < b) s_blue++;
    s_k++;
    set_territory(div_top_px(s_red, s_blue, H));
    fuse((int16_t)(40 + (215 * s_k) / (n > 0 ? n : 1)));   /* tambor acelera */

    if (s_k < n) {
        step_after((uint32_t)div_tick_ms(s_k, a, b), count_tick);
        return;
    }
    /* fim da rodada apurada: pancada na cor de quem levou a rodada */
    fuse(-1);
    int w = div_winner(a, b);
    kit_ui_beep(w == DIV_TIE ? 523 : SIDE_TONE[w], 160);
    s_cround++;
    paint_dots(s_cround);
    if (s_cround < s_nrounds)
        step_after(s_cround == s_nrounds - 1 ? FINAL_PAUSE_MS : COUNT_PAUSE_MS, count_round_begin);
    else
        step_after(900, show_result);
}

static void count_round_begin(void)
{
    if (s_st != A_COUNT) {
        /* primeira rodada a apurar: territórios acesos, divisa no meio */
        s_st = A_COUNT;
        s_cround = 0;
        s_red = s_blue = 0;
        set_territory(HALF);
        paint_halves();
        paint_dots(0);
        show_chrome(true, true);
        step_after(COUNT_PAUSE_MS, count_round_begin);
        return;
    }
    int a = div_net(&s_score[s_cround], DIV_RED);
    int b = div_net(&s_score[s_cround], DIV_BLUE);
    s_k = 0;
    paint_dots(s_cround);
    if (div_count_ticks(a, b) == 0) {           /* ninguém tocou nessa rodada */
        s_cround++;
        if (s_cround < s_nrounds) step_after(COUNT_PAUSE_MS, count_round_begin);
        else                      step_after(900, show_result);
        return;
    }
    step_after((uint32_t)div_tick_ms(0, a, b), count_tick);
}

static void flood_tick(void)
{
    s_flood_frame++;
    int y = s_flood_from + ((s_flood_to - s_flood_from) * s_flood_frame) / FLOOD_FRAMES;
    set_territory(y);
    if (s_flood_frame < FLOOD_FRAMES) { step_after(FLOOD_TICK_MS, flood_tick); return; }

    /* conquistou tudo: só o número de quem venceu, virado pra ele */
    s_st = A_DONE;
    show_chrome(false, false);
    if (s_winner == DIV_RED) { show(s_rot_row, false); place_upright(HALF - 20); }
    else                     { show(s_up_row, false);  place_rot(HALF - 20); }
    kit_ui_sfx(KIT_SFX_ONBOARD_DONE);
    show_buttons("DE NOVO", "SAIR");
    kit_ui_keep_awake(false);
}

static void flood_start(void)
{
    s_st = A_FLOOD;
    show_chrome(true, false);
    s_flood_from = s_top_px;
    s_flood_to = (s_winner == DIV_BLUE) ? H : 0;
    s_flood_frame = 0;
    step_after(FLOOD_TICK_MS, flood_tick);
}

static void show_result(void)
{
    s_st = A_SHOW;
    paint_dots(s_nrounds);
    show_numbers();
    kit_ui_sfx(KIT_SFX_REVEAL);
    s_winner = div_winner(s_red, s_blue);
    if (s_winner == DIV_TIE) {
        s_st = A_TIE;
        show_chrome(true, false);
        show_buttons("DESEMPATE", "SAIR");
        kit_ui_keep_awake(false);
        return;
    }
    step_after(SHOW_MS, flood_start);
}

/* --- abrir / fechar a arena -------------------------------------------- */

static void arena_open(void)
{
    show(s_arena, true);
    begin_match(ROUNDS_N[s_rounds_idx]);
}

static void arena_close(void)
{
    kill_timer(&s_step_timer);
    kill_timer(&s_abort_timer);
    pen_stop();
    fuse(-1);
    s_step_fn = NULL;
    s_st = A_OFF;
    s_held = -1;
    kit_ui_keep_awake(false);
    show(s_arena, false);
    kit_ui_shell_open(&s_shell, 1);
}

/* ------------------------------------------------------------- callbacks */

/* dedo chegou numa metade: toque (na sua vez) ou desconto (fora dela) */
static void half_down(int s)
{
    s_held = s;
    if (s_st != A_WINDOW) return;
    if (s == s_active) {
        s_score[s_round].taps[s]++;
        lv_obj_set_style_bg_opa(s_half[s], LV_OPA_60, 0);   /* pulso do toque */
        kit_ui_beep(SIDE_TAP[s], 20);
    } else {
        s_score[s_round].pen[s]++;                            /* encostou fora da vez */
        lv_obj_set_style_bg_opa(s_half[s], LV_OPA_40, 0);
        kit_ui_beep(196, 60);
        pen_start();
    }
}

static void half_press_cb(lv_event_t *e)
{
    half_down((int)(intptr_t)lv_event_get_user_data(e));
}

/* Leitura crua do touch: chega ANTES do evento do LVGL da mesma amostra. */
static void on_touch(const kit_input_event_t *ev, void *user)
{
    (void)user;
    if (!ev || ev->type != KIT_INPUT_TOUCH_DOWN) return;
    s_raw_prev_y = s_raw_y;
    s_raw_y = ev->y;
}

/* A última amostra pulou longe? Sem leitura crua, assume pulo (pune). */
static bool touch_jumped(void)
{
    if (s_raw_y < 0 || s_raw_prev_y < 0) return true;
    int d = s_raw_y - s_raw_prev_y;
    return (d < 0 ? -d : d) >= JUMP_PX;
}

/* Dedo "chegou arrastado" numa metade. O LVGL 9.5 manda PRESS_LOST pra
 * metade antiga mas NÃO manda PRESSED pra nova nesse caso — só PRESSING.
 * Duas causas, separadas pelo tamanho do salto entre leituras:
 *  - 2º dedo (o CST820 é single-touch e só PULA a coordenada): salto grande
 *    -> conta como chegada (desconto pra quem encostou fora da vez);
 *  - dedo escorregando pela divisa: salto pequeno -> não conta nada (nem
 *    toque pra quem esfrega, nem desconto injusto pro outro). */
static void half_pressing_cb(lv_event_t *e)
{
    int s = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_held == s) return;
    if (touch_jumped()) half_down(s);
    else                s_held = s;
}

/* PRESS_LOST: o ponto saiu desta metade (dedo ainda na tela) */
static void half_lost_cb(lv_event_t *e)
{
    int s = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_held == s) s_held = -1;
    pen_stop();
    if (s_st != A_OFF) paint_halves();
}

/* RELEASED: tela livre — a próxima leitura crua começa do zero */
static void half_release_cb(lv_event_t *e)
{
    int s = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_held == s) s_held = -1;
    s_raw_y = s_raw_prev_y = -1;
    pen_stop();
    if (s_st != A_OFF) paint_halves();
}

static void abort_fire_cb(lv_timer_t *t)
{
    (void)t;
    kill_timer(&s_abort_timer);
    kit_ui_sfx(KIT_SFX_BACK);
    arena_close();
}

static void abort_press_cb(lv_event_t *e)
{
    (void)e;
    if (s_st == A_WINDOW || s_st == A_GAP) return;
    lv_obj_set_style_bg_color(s_abort, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_abort_lbl, lv_color_hex(KIT_COLOR_BG), 0);
    kill_timer(&s_abort_timer);
    s_abort_timer = lv_timer_create(abort_fire_cb, ABORT_HOLD_MS, NULL);
}

static void abort_release_cb(lv_event_t *e)
{
    (void)e;
    kill_timer(&s_abort_timer);
    if (!s_abort) return;
    lv_obj_set_style_bg_color(s_abort, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_text_color(s_abort_lbl, lv_color_hex(KIT_COLOR_TEXT), 0);
}

static void btn_a_cb(lv_event_t *e)
{
    (void)e;
    if (s_st == A_DONE)      { kit_ui_confirm(); begin_match(ROUNDS_N[s_rounds_idx]); }
    else if (s_st == A_TIE)  { kit_ui_confirm(); begin_match(1); }   /* desempate: 1 rodada */
}

static void btn_b_cb(lv_event_t *e)
{
    (void)e;
    if (s_st == A_DONE || s_st == A_TIE) { kit_ui_click(); arena_close(); }
}

static void action_cb(lv_event_t *e)
{
    (void)e;
    if (s_st == A_OFF) { kit_ui_confirm(); arena_open(); }
}

static void rounds_cb(int idx, void *user)
{
    (void)user;
    s_rounds_idx = idx;
    set_i32(K_ROUNDS, idx);
    paint_idle();
}

static void pace_cb(int idx, void *user)
{
    (void)user;
    s_pace_idx = idx;
    set_i32(K_PACE, idx);
    paint_idle();
}

/* ---------------------------------------------------------------- AJUSTE */

static void section_label(lv_obj_t *p, const char *txt)
{
    kit_ui_label(p, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

static void build_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_right(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_top(p, 8, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    section_label(p, "RODADAS");
    kit_ui_chips(&s_rounds_chips, p, ROUNDS_LABELS, 2, s_rounds_idx, D_ACCENT, rounds_cb, NULL);
    section_label(p, "RITMO DAS JANELAS");
    kit_ui_chips(&s_pace_chips, p, PACE_LABELS, 2, s_pace_idx, D_ACCENT, pace_cb, NULL);
}

/* ------------------------------------------------------------------ JOGO */

static void build_jogo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *g = kit_ui_box(tile);
    lv_obj_set_size(g, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(g, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 14, 0);
    lv_obj_align(g, LV_ALIGN_CENTER, 0, -(KIT_UI_BTN_H + KIT_UI_BTN_MARGIN) / 2);

    /* protagonista: a tela dividida — azul em cima, vermelho embaixo */
    lv_obj_t *sq = kit_ui_box(g);
    lv_obj_set_size(sq, 132, 132);
    lv_obj_t *top = kit_ui_rect(sq, 132, 63, KIT_COLOR_BLUE, 0);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_t *bot = kit_ui_rect(sq, 132, 63, KIT_COLOR_RED, 0);
    lv_obj_set_pos(bot, 0, 69);
    lv_obj_t *ln = kit_ui_rect(sq, 132, 6, KIT_COLOR_TEXT, 0);
    lv_obj_set_pos(ln, 0, 63);

    kit_ui_label(g, "2 JOGADORES", KIT_COLOR_TEXT, &kit_mono_20, 2);
    s_idle_info = kit_ui_label(g, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    kit_ui_label(g, "DEITE O KIT ENTRE OS DOIS", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);

    kit_ui_action_button(&s_action, tile, D_ACCENT, action_cb);
    kit_ui_action_set(&s_action, "COME\xC3\x87" "AR");
}

/* ---------------------------------------------------------------- arena */

static lv_obj_t *arena_btn(lv_obj_t *parent, uint32_t bg, uint32_t fg, lv_event_cb_t cb, lv_obj_t **lbl)
{
    lv_obj_t *b = kit_ui_rect(parent, (KIT_UI_CONTENT - 12) / 2, KIT_UI_BTN_H, bg, KIT_UI_BTN_H / 2);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    *lbl = kit_ui_label(b, "", fg, &kit_mono_20, 2);
    lv_obj_center(*lbl);
    return b;
}

static void build_arena(void)
{
    /* tela cheia por cima da titlebar: o KIT vira o tabuleiro */
    s_arena = kit_ui_rect(s_screen, W, H, KIT_COLOR_BG, 0);
    lv_obj_set_pos(s_arena, 0, 0);
    lv_obj_add_flag(s_arena, LV_OBJ_FLAG_CLICKABLE);

    for (int s = 0; s < 2; s++) {
        lv_obj_t *h = kit_ui_box(s_arena);
        lv_obj_set_style_bg_color(h, lv_color_hex(SIDE_COLOR[s]), 0);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
        /* Sem press-lock: com dois dedos o CST820 não solta, só PULA a
         * coordenada pro dedo novo. Com o lock (padrão do LVGL) o pulo não
         * gera evento e quem planta o dedo trava o rival de graça; sem ele, o
         * pulo vira PRESS_LOST aqui + PRESSING na outra (= desconto). */
        lv_obj_remove_flag(h, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(h, half_press_cb,   LV_EVENT_PRESSED,    (void *)(intptr_t)s);
        lv_obj_add_event_cb(h, half_pressing_cb, LV_EVENT_PRESSING,  (void *)(intptr_t)s);
        lv_obj_add_event_cb(h, half_release_cb, LV_EVENT_RELEASED,   (void *)(intptr_t)s);
        lv_obj_add_event_cb(h, half_lost_cb,    LV_EVENT_PRESS_LOST, (void *)(intptr_t)s);
        s_half[s] = h;
    }

    s_divline = kit_ui_rect(s_arena, W, DIV_LINE, KIT_COLOR_TEXT, 0);
    lv_obj_remove_flag(s_divline, LV_OBJ_FLAG_CLICKABLE);

    /* rodadas: bolinhas numa pílula preta sobre a divisa (simétrico, sem texto) */
    s_dots_box = kit_ui_rect(s_arena, LV_SIZE_CONTENT, 28, KIT_COLOR_BG, 14);
    lv_obj_remove_flag(s_dots_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_left(s_dots_box, 12, 0);
    lv_obj_set_style_pad_right(s_dots_box, 12, 0);
    kit_ui_flex(s_dots_box, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 10);
    for (int i = 0; i < DIV_MAX_ROUNDS; i++) {
        s_dot[i] = kit_ui_rect(s_dots_box, 12, 12, KIT_COLOR_LINE, 6);
        lv_obj_remove_flag(s_dot[i], LV_OBJ_FLAG_CLICKABLE);
    }

    /* sair no meio: segurar o X (só fora das janelas) */
    s_abort = kit_ui_rect(s_arena, CHIP, CHIP, KIT_COLOR_BG, CHIP / 2);
    lv_obj_set_style_border_width(s_abort, 3, 0);
    lv_obj_set_style_border_color(s_abort, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_obj_add_flag(s_abort, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_abort, 8);
    lv_obj_add_event_cb(s_abort, abort_press_cb,   LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(s_abort, abort_release_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(s_abort, abort_release_cb, LV_EVENT_PRESS_LOST, NULL);
    s_abort_lbl = kit_ui_label(s_abort, "X", KIT_COLOR_TEXT, &kit_mono_20, 0);
    lv_obj_center(s_abort_lbl);

    /* percentual do vermelho (em pé) */
    s_up_row = kit_ui_box(s_arena);
    lv_obj_set_size(s_up_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(s_up_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(s_up_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_up_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(s_up_row, 4, 0);
    s_up_num = kit_ui_label(s_up_row, "", KIT_COLOR_ON_COLOR, &kit_display_72, 0);
    s_up_pct = kit_ui_label(s_up_row, "%", KIT_COLOR_ON_COLOR, &kit_display_44, 0);
    lv_obj_set_style_translate_y(s_up_pct, -PCT_NUDGE, 0);

    /* percentual do azul (girado 180°): '%' + dígitos ao contrário, alinhados pelo topo */
    s_rot_row = kit_ui_box(s_arena);
    lv_obj_set_size(s_rot_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(s_rot_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(s_rot_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_rot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s_rot_row, 4, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *im = lv_image_create(s_rot_row);
        lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(im, lv_color_hex(KIT_COLOR_ON_COLOR), 0);
        s_rot_img[i] = im;
    }
    lv_image_set_src(s_rot_img[0], &digit_rot_pct);
    lv_obj_set_style_translate_y(s_rot_img[0], PCT_NUDGE, 0);

    /* botões do fim (pra quem está do lado de baixo) */
    s_btn_row = kit_ui_box(s_arena);
    lv_obj_set_size(s_btn_row, KIT_UI_CONTENT, KIT_UI_BTN_H);
    lv_obj_remove_flag(s_btn_row, LV_OBJ_FLAG_CLICKABLE);
    kit_ui_flex(s_btn_row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 12);
    lv_obj_align(s_btn_row, LV_ALIGN_BOTTOM_MID, 0, -KIT_UI_BTN_MARGIN);
    s_btn_a = arena_btn(s_btn_row, D_ACCENT, D_ON_ACCENT, btn_a_cb, &s_btn_a_lbl);
    s_btn_b = arena_btn(s_btn_row, KIT_COLOR_BG, KIT_COLOR_TEXT, btn_b_cb, &s_btn_b_lbl);

    hide_numbers();
    show(s_btn_row, false);
    set_territory(HALF);
    show(s_arena, false);
}

#endif /* !KIT_SDK_STUBS */

/* ===================================================================== */

#ifdef KIT_SDK_STUBS

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Divisa stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#else

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    int32_t v = get_i32(K_ROUNDS, 0);
    s_rounds_idx = (v == 1) ? 1 : 0;
    v = get_i32(K_PACE, 0);
    s_pace_idx = (v == 1) ? 1 : 0;
    s_st = A_OFF;
    s_held = -1;
    s_step_fn = NULL;
    s_nrounds = ROUNDS_N[s_rounds_idx];

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "DIVISA", D_ACCENT, 3);
    kit_ui_shell_tiles(&s_shell, NULL, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_jogo(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);
    build_arena();
    paint_idle();
    if (s_api->input) s_api->input->register_callback(on_touch, NULL);

    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);   /* abre no JOGO */
    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    kill_timer(&s_step_timer);
    kill_timer(&s_pen_timer);
    kill_timer(&s_abort_timer);
    fuse(-1);
    kit_ui_keep_awake(false);
    if (s_api && s_api->input) s_api->input->register_callback(NULL, NULL);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell = (kit_ui_shell_t){0};
    s_rounds_chips = (kit_ui_chips_t){0};
    s_pace_chips = (kit_ui_chips_t){0};
    s_action = (kit_ui_action_t){0};
    s_idle_info = NULL;
    s_arena = s_divline = s_dots_box = s_abort = s_abort_lbl = NULL;
    s_half[0] = s_half[1] = NULL;
    memset(s_dot, 0, sizeof s_dot);
    memset(s_rot_img, 0, sizeof s_rot_img);
    s_up_row = s_up_num = s_up_pct = s_rot_row = NULL;
    s_btn_row = s_btn_a = s_btn_a_lbl = s_btn_b = s_btn_b_lbl = NULL;
    s_step_fn = NULL;
    s_st = A_OFF;
    s_held = -1;
    s_raw_y = s_raw_prev_y = -1;
    kit_ui_bind(NULL);
    s_api = NULL;
}

#endif
