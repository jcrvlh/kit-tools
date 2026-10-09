/**
 * @file main.c
 * @brief QUIQUE — ping-pong solo contra a parede, controlado inclinando o KIT.
 *
 * A raquete segue o dedo (arraste relativo, padrão) ou a inclinação lateral
 * (giroscópio, opcional no AJUSTE). A cada 10 rebatidas o
 * jogo pausa e oferece 3 cartas abertas, cada uma com um bônus E um ônus; os
 * efeitos acumulam. Três vidas; acabou, o placar vai (ou não) pro top-5.
 *
 * Decisões que não são óbvias:
 *  - Toque: o firmware manda um TOUCH_DOWN cru por leitura do sensor (~30 ms)
 *    enquanto o dedo está na tela e NADA ao soltar; a soltura é detectada
 *    pela falta de leitura (TOUCH_LOST_FRAMES). O arraste é RELATIVO: o dedo
 *    corre em qualquer ponto da mesa, sem cobrir a raquete nem a bola.
 *  - Não há leitura crua do acelerômetro na API das Tools: a inclinação vem
 *    do giroscópio integrado (`imu->gyro_poll`, centigraus), zerado ao
 *    começar. O giroscópio deriva um pouco com o tempo, então o "centro" vaza
 *    devagar em direção ao ângulo atual (quique_game.c, LEAK_DIV).
 *  - Inclinação lateral = yaw + roll. Inclinar pro lado (como volante) gira
 *    em torno da linha de visão; com o KIT a θ da vertical, isso cai em
 *    yaw·cos θ (eixo normal à tela) + roll·sin θ (eixo da altura da tela),
 *    os dois com o mesmo sinal. A soma acerta em qualquer pegada — em pé,
 *    deitado ou no meio — com ganho entre 1× e 1,41×. Só o roll (v1.0) não
 *    mexia a raquete com o KIT em pé.
 *  - O giroscópio é lido a cada quadro ENQUANTO a partida existe, inclusive
 *    na pausa e na escolha de carta: ele só integra quando é lido, e parar de
 *    ler perderia a rotação feita no meio tempo (o centro ficaria torto).
 *  - Ângulo -> POSIÇÃO da raquete (não velocidade): é o que dá precisão.
 *  - O sentido do eixo no aparelho não foi validado no hardware: o AJUSTE
 *    tem DIREÇÃO NORMAL/INVERTIDA pra corrigir sem recompilar.
 *  - Depois de escolher uma carta a bola volta parada na raquete: o toque na
 *    tela tira o KIT do lugar, ninguém perde vida no susto.
 *  - Inteiro puro (o .so não resolve float). Lógica em quique_game.c.
 */
#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "quique_game.h"

#include <stdio.h>
#include <string.h>

#ifndef KIT_SDK_STUBS

/* ----------------------------------------------------------------------- */

#define Q_ACCENT    KIT_COLOR_GREEN           /* mesa de ping-pong */
#define W           KIT_UI_SCREEN_W           /* 368 */
#define H           KIT_UI_SCREEN_H           /* 448 */

#define FRAME_MS        16
#define CALIB_MS        900    /* "SEGURE O KIT RETO" antes de zerar o giroscópio */
#define PICK_ARM_MS     700    /* cartas ignoram toque logo que abrem */
#define RESUME_MS       800
#define TOAST_MS        900
#define TOAST_ABOVE     72     /* aviso: px acima da raquete */
#define TOUCH_JUMP_PX   90     /* salto entre 2 leituras acima disto = dedo novo */
#define STRIP_GRAB      16     /* o toque que começa até aqui acima da faixa também pega */
#define TOUCH_LOST_FRAMES 6    /* ~100 ms sem leitura = dedo levantou */
#define CARD_GAP        10
#define CARD_TOP        78     /* título + montagem em cima das cartas */
#define PAUSE_BTN       40     /* visual; com a área estendida passa de 56 */
#define PAUSE_X         (W - KIT_UI_PAD - PAUSE_BTN)
#define PAUSE_Y         (QQ_WALL_Y + 10)
#define CORNER_Y        (PAUSE_Y + (PAUSE_BTN - LIFE_D) / 2)   /* vidas no canto de cima (inclinação) */
#define LIFE_D          10
#define MAX_PIPS        10     /* cargas na raquete: 6 cortadas + 4 freios */
#define PIP_D           6
#define LIST_ROWS       12     /* cartas listadas na pausa */
#define WM_COLOR        0x2A2925   /* placar-marca d'água: papel a ~15% sobre o preto */
#define WM_HALF_H       60     /* meia altura da linha do display_120 */

#define K_SENS  "qq_sens"
#define K_CTL   "qq_ctl"
#define K_DIR   "qq_dir"
#define K_HS    "qq_hs"        /* qq_hs0..qq_hs4 */

static const char *const SENS_LABELS[] = { "SUAVE", "NORMAL", "VIVA" };
static const int32_t     SENS_CDEG[]   = { 3500, 2500, 1500 };   /* inclinação até a borda */
static const int32_t     SENS_GAIN[]   = { 100, 115, 140 };      /* toque: quanto a raquete passa do dedo */
static const char *const CTL_LABELS[]  = { "TOQUE", "INCLINA\xC3\x87\xC3\x83O" };
static const char *const DIR_LABELS[]  = { "NORMAL", "INVERTIDA" };

static const char RULES[] =
    "Ping-pong sozinho contra a parede. Segure o KIT na m\xC3\xA3o, com a tela "
    "pra voc\xC3\xAA.\n\n"
    "1. Toque em COME\xC3\x87" "AR.\n\n"
    "2. Deslize o dedo na faixa embaixo da mesa: a raquete fica em cima "
    "dele. Se o dedo subir pra mesa sem soltar, ela continua seguindo.\n\n"
    "3. Cada rebatida vale ponto. N\xC3\xA3o deixe a bola passar: voc\xC3\xAA "
    "tem 3 vidas.\n\n"
    "4. A cada 10 rebatidas, escolha uma carta. Toda carta tem um lado bom "
    "(+) e um ruim (-), e os efeitos se acumulam at\xC3\xA9 o fim.\n\n"
    "5. Algumas cartas usam o chacoalhar: SACODE corta a bola que sobe, "
    "FREIO deixa em c\xC3\xA2mera lenta a que desce.\n\n"
    "6. O bot\xC3\xA3o no canto de cima pausa e mostra as suas cartas ativas. "
    "As vidas s\xC3\xA3o os pontinhos na faixa.\n\n"
    "No AJUSTE d\xC3\xA1 pra trocar o controle pra INCLINA\xC3\x87\xC3\x83O: "
    "segure o KIT reto ao come\xC3\xA7" "ar e incline pros lados. Se a raquete "
    "fugir pro lado errado, mude a DIRE\xC3\x87\xC3\x83O.";

typedef enum { A_OFF = 0, A_CALIB, A_PLAY, A_PICK, A_PAUSE, A_RESUME, A_OVER } arena_state_t;

/* ------------------------------------------------------------------ estado */

static const kit_api_table_t *s_api;
static lv_obj_t *s_screen;
static kit_ui_shell_t  s_shell;
static kit_ui_chips_t  s_sens_chips;
static kit_ui_chips_t  s_dir_chips;
static kit_ui_chips_t  s_ctl_chips;
static kit_ui_action_t s_action;

static int     s_sens_idx = 1;
static int     s_ctl_idx;          /* 0 = toque, 1 = inclinação */
static int     s_dir_idx;
static int32_t s_hs[QQ_HS_N];

static arena_state_t s_st;
static qq_game_t     s_g;
static int32_t s_lat, s_pitch;
/* toque cru: o dedo que COMEÇA na faixa (ou em qualquer lugar, com Prumo)
 * controla a raquete até levantar, mesmo se subir pra mesa */
static bool    s_touch_on, s_capture;
static int32_t s_tx, s_ty;
static uint32_t s_frame, s_touch_frame;
static bool    s_gyro_on;
static bool    s_pick_armed;
static int     s_rank = -1;

static char s_cards_txt[3072];
static char s_fx_buf[160];

/* JOGO (parado) */
static lv_obj_t *s_idle_best, *s_idle_top;

/* arena: a mesa é a tela. Em cima, só as vidas (pontinhos) e o botão de
 * pausa; o placar é uma marca d'água atrás da bola; as cargas de
 * SACODE/FREIO são pontinhos na raquete; as cartas ficam na pausa. */
static lv_obj_t *s_arena, *s_pause_btn, *s_lives, *s_life[QQ_MAX_LIVES], *s_watermark, *s_strip;
static lv_obj_t *s_wall, *s_fog, *s_portal[2], *s_shield;
static lv_obj_t *s_paddle[2], *s_ball[QQ_MAX_BALLS], *s_dot[QQ_PREVIEW_N], *s_pip[MAX_PIPS];
static lv_obj_t *s_toast;

/* escolha de carta */
static lv_obj_t *s_pick, *s_pick_title, *s_pick_build, *s_card[QQ_MAX_OFFER];
static lv_obj_t *s_card_name[QQ_MAX_OFFER], *s_card_bon[QQ_MAX_OFFER], *s_card_onu[QQ_MAX_OFFER];

/* pausa = a montagem: uma linha (nome, +, -) por carta ativa */
static lv_obj_t *s_pause, *s_pause_more, *s_pause_hint;
static lv_obj_t *s_row_name[LIST_ROWS], *s_row_bon[LIST_ROWS], *s_row_onu[LIST_ROWS];

/* fim */
static lv_obj_t *s_over, *s_over_score, *s_over_caption, *s_over_fx;

/* timers */
static lv_timer_t *s_frame_timer, *s_step_timer, *s_toast_timer;
static void (*s_step_fn)(void);

/* cache do que já está desenhado (evita invalidar a tela à toa) */
static int32_t s_drawn_score = -1, s_drawn_lives = -1, s_drawn_charges = -1;
static int32_t s_drawn_wy = -1, s_drawn_fy = -1, s_drawn_pw = -1, s_drawn_r = -1;

static void reset_drawn(void)
{
    s_drawn_score = s_drawn_lives = s_drawn_charges = -1;
    s_drawn_wy = s_drawn_fy = s_drawn_pw = s_drawn_r = -1;
}

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

/* só mexe no flag quando muda: add/remove HIDDEN invalida a área sempre */
static void show(lv_obj_t *o, bool on)
{
    if (!o) return;
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (on && hidden)       lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!on && !hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
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

static void decor(lv_obj_t *o) { lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE); }

/* --- giroscópio --------------------------------------------------------- */

static bool gyro_ok(void) { return s_api && s_api->imu && s_api->imu->gyro_poll; }

static void gyro_begin(void)
{
    s_lat = s_pitch = 0;
    if (!gyro_ok()) return;
    if (!s_gyro_on && s_api->imu->gyro_start) s_api->imu->gyro_start();
    else if (s_api->imu->gyro_rezero) s_api->imu->gyro_rezero();
    s_gyro_on = true;
}

static void gyro_end(void)
{
    if (s_gyro_on && s_api && s_api->imu && s_api->imu->gyro_stop) s_api->imu->gyro_stop();
    s_gyro_on = false;
}

static void gyro_read(void)
{
    if (!s_gyro_on || !gyro_ok()) return;
    int32_t yaw, pitch, roll;
    if (s_api->imu->gyro_poll(&yaw, &pitch, &roll, NULL)) { s_lat = yaw + roll; s_pitch = pitch; }
}

/* --- top-5 -------------------------------------------------------------- */

static void hs_load(void)
{
    char key[16];
    for (int i = 0; i < QQ_HS_N; i++) {
        snprintf(key, sizeof key, "%s%d", K_HS, i);
        int32_t v = get_i32(key, 0);
        s_hs[i] = v > 0 ? v : 0;
    }
}

static void hs_save(void)
{
    char key[16];
    for (int i = 0; i < QQ_HS_N; i++) {
        snprintf(key, sizeof key, "%s%d", K_HS, i);
        set_i32(key, s_hs[i]);
    }
}

/* ---------------------------------------------------------------- pintura */

static void paint_idle(void)
{
    if (s_hs[0] > 0) lv_label_set_text_fmt(s_idle_best, "RECORDE  %d", (int)s_hs[0]);
    else             lv_label_set_text(s_idle_best, "SEM RECORDE AINDA");

    char buf[96];
    int len = 0;
    buf[0] = 0;
    for (int i = 0; i < QQ_HS_N && s_hs[i] > 0; i++)
        len += snprintf(buf + len, sizeof buf - (size_t)len, "%s%d. %d",
                        i ? "   " : "", i + 1, (int)s_hs[i]);
    lv_label_set_text(s_idle_top, buf);
    show(s_idle_top, s_hs[1] > 0);
}

static void toast_hide_cb(lv_timer_t *t)
{
    (void)t;
    kill_timer(&s_toast_timer);
    show(s_toast, false);
}

/* aviso curto no meio da mesa ("VAI!", "FÊNIX!", nome da carta pega);
 * ms = 0 fica até trocar. `muted` = cinza, pra não competir com a bola. */
static void toast_col(const char *txt, uint32_t ms, bool muted)
{
    lv_label_set_text(s_toast, txt);
    lv_obj_set_style_text_color(s_toast, lv_color_hex(muted ? KIT_COLOR_TEXT_MUTED : KIT_COLOR_TEXT), 0);
    /* logo acima da raquete: é onde o olho está, e não briga com o placar */
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, s_g.paddle_y - TOAST_ABOVE);
    show(s_toast, true);
    kill_timer(&s_toast_timer);
    if (ms) s_toast_timer = lv_timer_create(toast_hide_cb, ms, NULL);
}

static void toast(const char *txt, uint32_t ms) { toast_col(txt, ms, false); }

static void paint_hud(void)
{
    const qq_game_t *g = &s_g;
    bool dark = g->blackout_t > 0;
    show(s_watermark, !dark);
    if (g->score != s_drawn_score) {
        lv_label_set_text_fmt(s_watermark, "%d", (int)g->score);
        s_drawn_score = g->score;
    }
    if (g->lives != s_drawn_lives) {
        for (int i = 0; i < QQ_MAX_LIVES; i++) show(s_life[i], i < g->lives);
        s_drawn_lives = g->lives;
    }
}

/* cargas de SACODE (escuras) e FREIO (claras) como furinhos na raquete */
static void paint_pips(const int32_t *lp, int32_t pw, bool dark)
{
    const qq_game_t *g = &s_g;
    int n = g->smash_charges + g->slow_charges;
    if (n > MAX_PIPS) n = MAX_PIPS;
    int fit = (pw - 8) / (PIP_D + 4);
    if (n > fit) n = fit;
    if (dark) n = 0;
    int32_t charges = g->smash_charges * 16 + g->slow_charges;
    if (charges != s_drawn_charges) {
        for (int i = 0; i < MAX_PIPS; i++)
            lv_obj_set_style_bg_color(s_pip[i], lv_color_hex(i < g->smash_charges ? KIT_COLOR_BG : KIT_COLOR_TEXT), 0);
        s_drawn_charges = charges;
    }
    int32_t x0 = lp[0] + (pw - (n * (PIP_D + 4) - 4)) / 2;
    for (int i = 0; i < MAX_PIPS; i++) {
        show(s_pip[i], i < n);
        if (i < n) lv_obj_set_pos(s_pip[i], x0 + i * (PIP_D + 4), g->paddle_y + (QQ_PADDLE_H - PIP_D) / 2);
    }
}

static void set_rect(lv_obj_t *o, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
}

static void paint_field(void)
{
    const qq_game_t *g = &s_g;
    bool dark = g->blackout_t > 0;
    int32_t wy = qq_wall_y(g);
    int32_t fy = qq_fog_y(g);

    /* geometria que só muda quando entra carta: redesenha só se mudou */
    if (wy != s_drawn_wy || fy != s_drawn_fy) {
        set_rect(s_wall, 0, wy - 4, W, 4);
        if (fy > wy) set_rect(s_fog, 0, wy, W, fy - wy);
        set_rect(s_portal[0], 0, wy, 4, g->paddle_y - wy);
        set_rect(s_portal[1], W - 4, wy, 4, g->paddle_y - wy);
        lv_obj_align(s_watermark, LV_ALIGN_TOP_MID, 0, (wy + g->paddle_y) / 2 - WM_HALF_H);
        s_drawn_wy = wy;
        s_drawn_fy = fy;
    }
    show(s_fog, fy > wy);
    show(s_portal[0], qq_portals(g));
    show(s_portal[1], qq_portals(g));
    show(s_shield, g->shield > 0);

    int32_t lp[2];
    int n = qq_paddles(g, lp);
    int32_t pw = qq_paddle_w(g);
    if (pw != s_drawn_pw) {
        for (int k = 0; k < 2; k++) lv_obj_set_size(s_paddle[k], pw, QQ_PADDLE_H);
        s_drawn_pw = pw;
    }
    for (int k = 0; k < 2; k++) {
        bool on = k < n && !dark;
        show(s_paddle[k], on);
        if (on) lv_obj_set_pos(s_paddle[k], lp[k], g->paddle_y);
    }
    paint_pips(lp, pw, dark);

    int32_t r = qq_ball_r(g);
    if (r != s_drawn_r) {
        for (int i = 0; i < QQ_MAX_BALLS; i++) {
            lv_obj_set_size(s_ball[i], 2 * r, 2 * r);
            lv_obj_set_style_radius(s_ball[i], r, 0);
        }
        s_drawn_r = r;
    }
    for (int i = 0; i < QQ_MAX_BALLS; i++) {
        bool vis = qq_ball_visible(g, i);
        show(s_ball[i], vis);
        if (vis) lv_obj_set_pos(s_ball[i], g->ball[i].x / QQ_FP - r, g->ball[i].y / QQ_FP - r);
    }

    int16_t xs[QQ_PREVIEW_N], ys[QQ_PREVIEW_N];
    int np = dark ? 0 : qq_preview(g, xs, ys);
    for (int i = 0; i < QQ_PREVIEW_N; i++) {
        show(s_dot[i], i < np);
        if (i < np) lv_obj_set_pos(s_dot[i], xs[i] - 3, ys[i] - 3);
    }
    paint_hud();
}

/* --- som dos eventos ------------------------------------------------------ */

static void play_events(uint32_t ev)
{
    bool quiet = qq_silent(&s_g);
    if (ev & QE_MISS) kit_ui_miss();
    else if (ev & QE_SMASH_PT) kit_ui_beep(1319, 70);
    else if (ev & QE_HIT) { if (!quiet) kit_ui_beep((ev & QE_CENTER) ? 988 : 659, 25); }
    else if (ev & QE_WALL) { if (!quiet) kit_ui_beep(440, 20); }
    else if (ev & QE_SIDE) { if (!quiet) kit_ui_beep(392, 15); }
    else if (ev & QE_PORTAL) { if (!quiet) kit_ui_beep(1047, 25); }
    if (ev & QE_SHIELD) { kit_ui_beep(880, 40); kit_ui_beep(1175, 60); }
    if (ev & QE_NEAR) kit_ui_beep(1568, 15);
    if (ev & QE_SLIP) kit_ui_beep(247, 40);
    if (ev & QE_BLACKOUT) kit_ui_beep(196, 40);
    if (ev & QE_SMASH) kit_ui_sfx(KIT_SFX_VETO_HIT);
    if (ev & QE_SLOWMO) kit_ui_beep(294, 120);
}

/* --------------------------------------------------------------- partida */

static void open_pick(void);
static void game_over(void);

static bool touch_mode(void) { return s_ctl_idx == 0; }

static bool on_pause_btn(int32_t x, int32_t y)
{
    return x >= PAUSE_X - 16 && y <= PAUSE_Y + PAUSE_BTN + 16;
}

/* leitura crua do toque (uma por amostra do sensor, só enquanto encostado) */
static void on_touch(const kit_input_event_t *ev, void *user)
{
    (void)user;
    if (!ev || ev->type != KIT_INPUT_TOUCH_DOWN) return;
    if (s_st != A_PLAY || !touch_mode()) { s_touch_on = s_capture = false; return; }
    int32_t dx = ev->x - s_tx, dy = ev->y - s_ty;
    bool jump = dx > TOUCH_JUMP_PX || dx < -TOUCH_JUMP_PX || dy > TOUCH_JUMP_PX || dy < -TOUCH_JUMP_PX;
    if (!s_touch_on || jump) {
        /* dedo novo: decide se ele é o controle */
        s_capture = s_g.onu[QC_PRUMO] ? !on_pause_btn(ev->x, ev->y)
                                      : ev->y >= QQ_STRIP_Y - STRIP_GRAB;
    }
    s_tx = ev->x;
    s_ty = ev->y;
    s_touch_on = true;
    s_touch_frame = s_frame;
}

static void frame_cb(lv_timer_t *t)
{
    (void)t;
    s_frame++;
    if (s_touch_on && s_frame - s_touch_frame > TOUCH_LOST_FRAMES) s_touch_on = s_capture = false;
    gyro_read();
    if (s_st != A_PLAY) return;
    uint32_t ev;
    if (touch_mode()) ev = qq_step(&s_g, s_capture ? s_tx : -1, s_ty);
    else              ev = qq_step(&s_g, s_lat, s_pitch);
    play_events(ev);
    if (ev & QE_PHOENIX) { kit_ui_confirm(); toast("F\xC3\x8ANIX!", TOAST_MS); }
    if (ev & QE_SHIELD) toast("ESCUDO!", 600);
    paint_field();
    if (ev & QE_OVER) { game_over(); return; }
    if (ev & QE_PICK) open_pick();
}

static void begin_play(void)
{
    s_st = A_PLAY;
    show(s_toast, false);
    paint_field();
}

static void new_game(void)
{
    qq_start(&s_g, SENS_CDEG[s_sens_idx], s_dir_idx ? -1 : 1, rng);
    if (touch_mode()) qq_use_touch(&s_g, SENS_GAIN[s_sens_idx]);
    /* toque: faixa do dedo embaixo, vidas dentro dela; inclinação: mesa
     * cheia, vidas no canto de cima (espelhando a pausa) */
    show(s_strip, touch_mode());
    if (touch_mode()) lv_obj_set_pos(s_lives, KIT_UI_PAD + 4, QQ_STRIP_Y + (QQ_STRIP_H - LIFE_D) / 2);
    else              lv_obj_set_pos(s_lives, KIT_UI_PAD + 4, CORNER_Y);
    lv_obj_set_pos(s_shield, 0, s_g.shield_y);
}

static void calib_done(void)
{
    if (!touch_mode()) gyro_begin();   /* bloqueia ~80 ms: KIT parado */
    new_game();
    reset_drawn();
    s_touch_on = s_capture = false;
    if (touch_mode()) toast_col("DEDO NA FAIXA", 1500, true);
    else              toast("VAI!", 600);
    paint_field();
    s_st = A_PLAY;
    if (!s_frame_timer) s_frame_timer = lv_timer_create(frame_cb, FRAME_MS, NULL);
}

static void begin_match(void)
{
    s_st = A_CALIB;
    s_rank = -1;
    show(s_over, false);
    show(s_pick, false);
    show(s_pause, false);
    /* mesa já montada (bola na raquete) enquanto calibra; calib_done recomeça */
    new_game();
    reset_drawn();
    paint_field();
    kit_ui_keep_awake(true);
    lv_label_set_text(s_pause_hint, touch_mode() ? "TOQUE EM CONTINUAR QUANDO QUISER"
                                                 : "CENTRALIZE O KIT ANTES DE VOLTAR");
    if (touch_mode()) { calib_done(); return; }   /* toque não calibra nada */
    toast("SEGURE O KIT RETO", 0);
    step_after(CALIB_MS, calib_done);
}

/* --- escolha de carta ------------------------------------------------------ */

static void pick_arm(void) { s_pick_armed = true; }

static void open_pick(void)
{
    s_st = A_PICK;
    s_pick_armed = false;
    kit_ui_sfx(KIT_SFX_REVEAL);

    int n = s_g.noffer;
    int top = CARD_TOP;
    int avail = H - top - 12;
    int ch = (avail - (n - 1) * CARD_GAP) / (n > 0 ? n : 1);
    lv_label_set_text_fmt(s_pick_title, "ESCOLHA UMA CARTA \xC2\xB7 %d", (int)s_g.hits);
    /* a montagem aparece aqui, onde ela decide a escolha */
    qq_fx_text(&s_g, s_fx_buf, sizeof s_fx_buf);
    lv_label_set_text(s_pick_build, s_fx_buf[0] ? s_fx_buf : "SEM CARTAS AINDA");
    for (int i = 0; i < QQ_MAX_OFFER; i++) {
        bool on = i < n;
        show(s_card[i], on);
        if (!on) continue;
        const qq_card_info_t *c = &QQ_CARDS[s_g.offer[i]];
        set_rect(s_card[i], KIT_UI_PAD, top + i * (ch + CARD_GAP), KIT_UI_CONTENT, ch);
        lv_label_set_text(s_card_name[i], c->name);
        lv_label_set_text_fmt(s_card_bon[i], "+ %s", c->bonus);
        lv_label_set_text_fmt(s_card_onu[i], "- %s", c->onus);
        int pad = n > 3 ? 6 : 12;
        lv_obj_set_style_pad_top(s_card[i], pad, 0);
        lv_obj_set_style_pad_bottom(s_card[i], pad, 0);
    }
    show(s_pick, true);
    step_after(PICK_ARM_MS, pick_arm);
}

static void card_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_st != A_PICK || !s_pick_armed || i >= s_g.noffer) return;
    kit_ui_confirm();
    const char *name = QQ_CARDS[s_g.offer[i]].name;
    qq_take_offer(&s_g, i);
    show(s_pick, false);
    s_st = A_PLAY;
    /* o nome fica apagado na mesa enquanto a bola espera na raquete: é o
     * lembrete das cartas que não se veem (Espelho, Mola, Prumo...) */
    toast_col(name, TOAST_MS, true);
    paint_field();
}

/* --- pausa ----------------------------------------------------------------- */

/* lista da montagem: uma linha por carta ativa, só os lados que valem */
static void paint_pause_list(void)
{
    const qq_game_t *g = &s_g;
    int row = 0, extra = 0;
    for (int c = 0; c < QC_COUNT; c++) {
        int nb = g->bon[c], no = g->onu[c];
        if (!nb && !no) continue;
        if (row >= LIST_ROWS) { extra++; continue; }
        int n = nb > no ? nb : no;
        if (n > 1) lv_label_set_text_fmt(s_row_name[row], "%s x%d", QQ_CARDS[c].name, n);
        else       lv_label_set_text(s_row_name[row], QQ_CARDS[c].name);
        lv_label_set_text_fmt(s_row_bon[row], "+ %s", QQ_CARDS[c].bonus);
        lv_label_set_text_fmt(s_row_onu[row], "- %s", QQ_CARDS[c].onus);
        show(s_row_name[row], true);
        show(s_row_bon[row], nb > 0);
        show(s_row_onu[row], no > 0);
        row++;
    }
    if (row == 0) {
        lv_label_set_text(s_row_name[0], "NENHUMA CARTA AINDA");
        show(s_row_name[0], true);
        show(s_row_bon[0], false);
        show(s_row_onu[0], false);
        row = 1;
    }
    for (int r = row; r < LIST_ROWS; r++) {
        show(s_row_name[r], false);
        show(s_row_bon[r], false);
        show(s_row_onu[r], false);
    }
    if (extra) lv_label_set_text_fmt(s_pause_more, "+ %d CARTA%s", extra, extra > 1 ? "S" : "");
    show(s_pause_more, extra > 0);
}

static void pause_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != A_PLAY) return;
    kit_ui_click();
    s_st = A_PAUSE;
    paint_pause_list();
    show(s_pause, true);
}

static void resume_go(void)
{
    if (s_st != A_RESUME) return;
    begin_play();
}

static void pause_continue_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != A_PAUSE) return;
    kit_ui_click();
    show(s_pause, false);
    s_st = A_RESUME;
    toast("VAI!", RESUME_MS);
    step_after(RESUME_MS, resume_go);
}

static void pause_end_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != A_PAUSE) return;
    kit_ui_click();
    show(s_pause, false);
    game_over();
}

/* --- fim ------------------------------------------------------------------- */

static void game_over(void)
{
    s_st = A_OVER;
    kill_timer(&s_frame_timer);
    kill_timer(&s_step_timer);
    gyro_end();
    kit_ui_keep_awake(false);
    show(s_toast, false);
    show(s_pick, false);

    int32_t fin = qq_final_score(&s_g);
    s_rank = qq_hs_insert(s_hs, fin);
    if (s_rank >= 0) hs_save();

    lv_label_set_text_fmt(s_over_score, "%d", (int)fin);
    if (s_rank == 0)
        lv_label_set_text(s_over_caption, "NOVO RECORDE!");
    else if (s_rank > 0)
        lv_label_set_text_fmt(s_over_caption, "%d\xC2\xBA LUGAR \xC2\xB7 RECORDE %d", s_rank + 1, (int)s_hs[0]);
    else
        lv_label_set_text_fmt(s_over_caption, "RECORDE %d", (int)s_hs[0]);
    if (s_g.onu[QC_FENIX] && fin != s_g.score)
        lv_label_set_text_fmt(s_over_fx, "%d PONTOS - 25%% DA F\xC3\x8ANIX", (int)s_g.score);
    else {
        qq_fx_text(&s_g, s_fx_buf, sizeof s_fx_buf);
        lv_label_set_text(s_over_fx, s_fx_buf[0] ? s_fx_buf : "SEM CARTAS");
    }
    show(s_over, true);
    kit_ui_sfx(s_rank == 0 ? KIT_SFX_ONBOARD_DONE : KIT_SFX_ADEDONHA_STOP);
}

/* --- abrir / fechar a arena -------------------------------------------- */

static void arena_open(void)
{
    show(s_arena, true);
    begin_match();
}

static void arena_close(void)
{
    kill_timer(&s_frame_timer);
    kill_timer(&s_step_timer);
    kill_timer(&s_toast_timer);
    s_step_fn = NULL;
    gyro_end();
    s_st = A_OFF;
    kit_ui_keep_awake(false);
    show(s_arena, false);
    paint_idle();
    kit_ui_shell_open(&s_shell, 1);
}

/* ------------------------------------------------------------- callbacks */

static void on_shake(void *user)
{
    (void)user;
    if (s_st != A_PLAY) return;
    uint32_t ev = qq_shake(&s_g);
    play_events(ev);
    if (ev & QE_SMASH) toast("CORTADA!", 600);
    if (ev & QE_SLOWMO) toast("FREIO", 600);
    if (ev) paint_hud();
}

static void over_again_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != A_OVER) return;
    kit_ui_confirm();
    begin_match();
}

static void over_exit_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != A_OVER) return;
    kit_ui_click();
    arena_close();
}

static void action_cb(lv_event_t *e)
{
    (void)e;
    if (s_st == A_OFF) { kit_ui_confirm(); arena_open(); }
}

static void sens_cb(int idx, void *user)
{
    (void)user;
    s_sens_idx = idx;
    set_i32(K_SENS, idx);
}

static void dir_cb(int idx, void *user)
{
    (void)user;
    s_dir_idx = idx;
    set_i32(K_DIR, idx);
}

/* ---------------------------------------------------------------- AJUSTE */

static void section_label(lv_obj_t *p, const char *txt)
{
    kit_ui_label(p, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

static void ctl_cb(int idx, void *user)
{
    (void)user;
    s_ctl_idx = idx;
    set_i32(K_CTL, idx);
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

    section_label(p, "CONTROLE");
    kit_ui_chips(&s_ctl_chips, p, CTL_LABELS, 2, s_ctl_idx, Q_ACCENT, ctl_cb, NULL);
    section_label(p, "SENSIBILIDADE");
    kit_ui_chips(&s_sens_chips, p, SENS_LABELS, 3, s_sens_idx, Q_ACCENT, sens_cb, NULL);
    lv_obj_t *hint = kit_ui_label(p, "TOQUE: A RAQUETE ANDA 1x, 1,15x OU 1,4x O DEDO, A PARTIR DO MEIO. "
                                     "INCLINA\xC3\x87\xC3\x83O: 35\xC2\xB0, 25\xC2\xB0 OU 15\xC2\xB0 AT\xC3\x89 A BORDA.",
                                  KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, KIT_UI_CONTENT);
    section_label(p, "DIRE\xC3\x87\xC3\x83O (S\xC3\x93 INCLINA\xC3\x87\xC3\x83O)");
    kit_ui_chips(&s_dir_chips, p, DIR_LABELS, 2, s_dir_idx, Q_ACCENT, dir_cb, NULL);
}

/* ------------------------------------------------------------------ JOGO */

static void build_jogo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *g = kit_ui_box(tile);
    lv_obj_set_size(g, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(g, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 14, 0);
    lv_obj_align(g, LV_ALIGN_CENTER, 0, -(KIT_UI_BTN_H + KIT_UI_BTN_MARGIN) / 2);

    /* protagonista: a mesa — parede em cima, bola no ar, raquete embaixo */
    lv_obj_t *sq = kit_ui_box(g);
    lv_obj_set_size(sq, 132, 132);
    kit_ui_rect(sq, 132, 6, KIT_COLOR_TEXT, 0);
    lv_obj_t *ball = kit_ui_rect(sq, 22, 22, KIT_COLOR_TEXT, 11);
    lv_obj_set_pos(ball, 78, 46);
    lv_obj_t *pad = kit_ui_rect(sq, 60, 14, Q_ACCENT, 7);
    lv_obj_set_pos(pad, 30, 118);

    s_idle_best = kit_ui_label(g, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    s_idle_top = kit_ui_label(g, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0);
    kit_ui_label(g, "INCLINE O KIT PRA JOGAR", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);

    kit_ui_action_button(&s_action, tile, Q_ACCENT, action_cb);
    kit_ui_action_set(&s_action, "COME\xC3\x87" "AR");
}

/* --------------------------------------------------------------- CARTAS */

static void build_cards_text(void)
{
    int len = 0;
    s_cards_txt[0] = 0;
    len += snprintf(s_cards_txt + len, sizeof s_cards_txt - (size_t)len,
                    "Toda carta traz um b\xC3\xB4nus (+) e um \xC3\xB4nus (-). "
                    "Os efeitos se acumulam; pegar a mesma carta de novo refor\xC3\xA7" "a.\n\n");
    for (int c = 0; c < QC_COUNT && len < (int)sizeof s_cards_txt - 1; c++) {
        const qq_card_info_t *k = &QQ_CARDS[c];
        len += snprintf(s_cards_txt + len, sizeof s_cards_txt - (size_t)len,
                        "%s\n+ %s\n- %s\n\n", k->name, k->bonus, k->onus);
    }
}

/* ---------------------------------------------------------------- arena */

static lv_obj_t *pill(lv_obj_t *parent, int w, uint32_t bg, uint32_t fg, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *b = kit_ui_rect(parent, w, KIT_UI_BTN_H, bg, KIT_UI_BTN_H / 2);
    if (bg == KIT_COLOR_BG) {
        lv_obj_set_style_border_width(b, 3, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(KIT_COLOR_TEXT), 0);
    }
    kit_ui_tap(b, cb, 0);
    lv_obj_center(kit_ui_label(b, txt, fg, &kit_mono_20, 2));
    return b;
}

static void build_pick(void)
{
    s_pick = kit_ui_rect(s_arena, W, H, KIT_COLOR_BG, 0);
    lv_obj_set_pos(s_pick, 0, 0);
    s_pick_title = kit_ui_label(s_pick, "", KIT_COLOR_TEXT, &kit_mono_16, 2);
    lv_obj_align(s_pick_title, LV_ALIGN_TOP_MID, 0, 20);
    /* a montagem atual, numa linha: é a hora em que ela importa */
    s_pick_build = kit_ui_label(s_pick, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0);
    lv_label_set_long_mode(s_pick_build, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_pick_build, LV_TEXT_ALIGN_CENTER, 0);
    set_rect(s_pick_build, KIT_UI_PAD, 46, KIT_UI_CONTENT, 22);
    for (int i = 0; i < QQ_MAX_OFFER; i++) {
        lv_obj_t *c = kit_ui_rect(s_pick, KIT_UI_CONTENT, 100, KIT_COLOR_SURFACE, 18);
        lv_obj_set_style_pad_left(c, 16, 0);
        lv_obj_set_style_pad_right(c, 16, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(c, 2, 0);
        kit_ui_tap(c, card_cb, i);
        lv_obj_set_style_bg_color(c, lv_color_hex(KIT_COLOR_SURFACE_ALT), LV_STATE_PRESSED);
        s_card_name[i] = kit_ui_label(c, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
        s_card_bon[i] = kit_ui_label(c, "", KIT_COLOR_GREEN, &kit_sans_22, 0);
        s_card_onu[i] = kit_ui_label(c, "", KIT_COLOR_RED, &kit_sans_22, 0);
        lv_label_set_long_mode(s_card_bon[i], LV_LABEL_LONG_DOT);
        lv_label_set_long_mode(s_card_onu[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_card_bon[i], KIT_UI_CONTENT - 32);
        lv_obj_set_width(s_card_onu[i], KIT_UI_CONTENT - 32);
        s_card[i] = c;
    }
    show(s_pick, false);
}

static void build_pause(void)
{
    s_pause = kit_ui_rect(s_arena, W, H, KIT_COLOR_BG, 0);
    lv_obj_set_pos(s_pause, 0, 0);
    lv_obj_t *t = kit_ui_label(s_pause, "PAUSA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);
    s_pause_hint = kit_ui_label(s_pause, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 0);
    lv_obj_align(s_pause_hint, LV_ALIGN_TOP_MID, 0, 52);

    /* a montagem: rola se não couber */
    int top = 82, bottom = H - KIT_UI_BTN_H - 2 * KIT_UI_BTN_MARGIN;
    lv_obj_t *list = lv_obj_create(s_pause);
    lv_obj_remove_style_all(list);
    set_rect(list, KIT_UI_PAD, top, KIT_UI_CONTENT, bottom - top);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_style_pad_bottom(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    for (int r = 0; r < LIST_ROWS; r++) {
        s_row_name[r] = kit_ui_label(list, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
        if (r) lv_obj_set_style_pad_top(s_row_name[r], 10, 0);
        s_row_bon[r] = kit_ui_label(list, "", KIT_COLOR_GREEN, &kit_sans_22, 0);
        s_row_onu[r] = kit_ui_label(list, "", KIT_COLOR_RED, &kit_sans_22, 0);
    }
    s_pause_more = kit_ui_label(list, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_set_style_pad_top(s_pause_more, 10, 0);

    lv_obj_t *row = kit_ui_box(s_pause);
    lv_obj_set_size(row, KIT_UI_CONTENT, KIT_UI_BTN_H);
    kit_ui_flex(row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 12);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -KIT_UI_BTN_MARGIN);
    pill(row, (KIT_UI_CONTENT - 12) / 2, Q_ACCENT, kit_ui_on(Q_ACCENT), "CONTINUAR", pause_continue_cb);
    pill(row, (KIT_UI_CONTENT - 12) / 2, KIT_COLOR_BG, KIT_COLOR_TEXT, "ENCERRAR", pause_end_cb);
    show(s_pause, false);
}

static void build_over(void)
{
    s_over = kit_ui_rect(s_arena, W, H, KIT_COLOR_BG, 0);
    lv_obj_set_pos(s_over, 0, 0);
    lv_obj_t *col = kit_ui_box(s_over);
    lv_obj_set_size(col, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(col, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 10, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -(KIT_UI_BTN_H + KIT_UI_BTN_MARGIN) / 2);
    kit_ui_label(col, "FIM DE JOGO", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 3);
    s_over_score = kit_ui_label(col, "", KIT_COLOR_TEXT, &kit_display_120, 0);
    s_over_caption = kit_ui_label(col, "", Q_ACCENT, &kit_mono_20, 2);
    s_over_fx = kit_ui_text(col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, KIT_UI_CONTENT);

    lv_obj_t *row = kit_ui_box(s_over);
    lv_obj_set_size(row, KIT_UI_CONTENT, KIT_UI_BTN_H);
    kit_ui_flex(row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 12);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -KIT_UI_BTN_MARGIN);
    pill(row, (KIT_UI_CONTENT - 12) / 2, Q_ACCENT, kit_ui_on(Q_ACCENT), "DE NOVO", over_again_cb);
    pill(row, (KIT_UI_CONTENT - 12) / 2, KIT_COLOR_BG, KIT_COLOR_TEXT, "SAIR", over_exit_cb);
    show(s_over, false);
}

static void build_arena(void)
{
    /* tela cheia por cima da titlebar: o KIT vira a mesa */
    s_arena = kit_ui_rect(s_screen, W, H, KIT_COLOR_BG, 0);
    lv_obj_set_pos(s_arena, 0, 0);
    lv_obj_add_flag(s_arena, LV_OBJ_FLAG_CLICKABLE);

    /* placar: marca d'água atrás de tudo da mesa */
    s_watermark = kit_ui_label(s_arena, "0", WM_COLOR, &kit_display_120, 0);
    decor(s_watermark);

    s_wall = kit_ui_rect(s_arena, W, 4, KIT_COLOR_TEXT, 0);
    decor(s_wall);
    for (int k = 0; k < 2; k++) {
        s_portal[k] = kit_ui_rect(s_arena, 4, 10, KIT_COLOR_BLUE, 0);
        decor(s_portal[k]);
    }
    s_shield = kit_ui_rect(s_arena, W, 3, KIT_COLOR_BLUE, 0);
    decor(s_shield);

    for (int i = 0; i < QQ_PREVIEW_N; i++) {
        s_dot[i] = kit_ui_rect(s_arena, 6, 6, KIT_COLOR_TEXT_MUTED, 3);
        decor(s_dot[i]);
    }
    for (int k = 0; k < 2; k++) {
        s_paddle[k] = kit_ui_rect(s_arena, QQ_PADDLE_W, QQ_PADDLE_H, Q_ACCENT, QQ_PADDLE_H / 2);
        decor(s_paddle[k]);
    }
    for (int i = 0; i < MAX_PIPS; i++) {
        s_pip[i] = kit_ui_rect(s_arena, PIP_D, PIP_D, KIT_COLOR_BG, PIP_D / 2);
        decor(s_pip[i]);
        show(s_pip[i], false);
    }
    for (int i = 0; i < QQ_MAX_BALLS; i++) {
        s_ball[i] = kit_ui_rect(s_arena, 2 * QQ_BALL_R, 2 * QQ_BALL_R,
                                i ? KIT_COLOR_YELLOW : KIT_COLOR_TEXT, QQ_BALL_R);
        decor(s_ball[i]);
    }
    /* neblina por cima das bolas */
    s_fog = kit_ui_rect(s_arena, W, 10, KIT_COLOR_SURFACE_ALT, 0);
    decor(s_fog);

    /* toque: a faixa do dedo, por cima das bolas (a que passa da raquete
     * some nela). Uma pegada no centro diz "o dedo vai aqui". */
    s_strip = kit_ui_rect(s_arena, W, QQ_STRIP_H, KIT_COLOR_SURFACE, 0);
    lv_obj_set_pos(s_strip, 0, QQ_STRIP_Y);
    decor(s_strip);
    for (int k = 0; k < 3; k++) {
        lv_obj_t *grip = kit_ui_rect(s_strip, 44, 3, KIT_COLOR_LINE, 1);
        lv_obj_align(grip, LV_ALIGN_CENTER, 0, (k - 1) * 8);
        decor(grip);
    }

    /* vidas e pausa por cima de tudo: sempre visíveis, mesmo com Neblina */
    s_lives = kit_ui_box(s_arena);
    lv_obj_set_size(s_lives, LV_SIZE_CONTENT, LIFE_D);
    kit_ui_flex(s_lives, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_START, 0, 6);
    decor(s_lives);
    for (int i = 0; i < QQ_MAX_LIVES; i++) {
        s_life[i] = kit_ui_rect(s_lives, LIFE_D, LIFE_D, KIT_COLOR_TEXT, LIFE_D / 2);
        decor(s_life[i]);
    }

    /* pausa: só contorno, a bola aparece através dele */
    s_pause_btn = kit_ui_rect(s_arena, PAUSE_BTN, PAUSE_BTN, KIT_COLOR_BG, PAUSE_BTN / 2);
    lv_obj_set_style_bg_opa(s_pause_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pause_btn, 2, 0);
    lv_obj_set_style_border_color(s_pause_btn, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    lv_obj_set_pos(s_pause_btn, PAUSE_X, PAUSE_Y);
    kit_ui_tap(s_pause_btn, pause_btn_cb, 0);
    lv_obj_set_ext_click_area(s_pause_btn, 10);
    lv_obj_set_style_bg_color(s_pause_btn, lv_color_hex(KIT_COLOR_SURFACE_ALT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(s_pause_btn, LV_OPA_COVER, LV_STATE_PRESSED);
    for (int k = 0; k < 2; k++) {   /* ícone ‖ */
        lv_obj_t *bar = kit_ui_rect(s_pause_btn, 4, 14, KIT_COLOR_TEXT_MUTED, 1);
        lv_obj_align(bar, LV_ALIGN_CENTER, k ? 4 : -4, 0);
        decor(bar);
    }

    s_toast = kit_ui_label(s_arena, "", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_set_style_text_align(s_toast, LV_TEXT_ALIGN_CENTER, 0);
    decor(s_toast);
    show(s_toast, false);

    build_pick();
    build_pause();
    build_over();
    show(s_arena, false);
}

#endif /* !KIT_SDK_STUBS */

/* ===================================================================== */

#ifdef KIT_SDK_STUBS

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Quique stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#else

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    int32_t v = get_i32(K_SENS, 1);
    s_sens_idx = (v >= 0 && v <= 2) ? (int)v : 1;
    v = get_i32(K_CTL, 0);
    s_ctl_idx = (v == 1) ? 1 : 0;
    v = get_i32(K_DIR, 0);
    s_dir_idx = (v == 1) ? 1 : 0;
    hs_load();
    s_st = A_OFF;
    s_step_fn = NULL;
    s_gyro_on = false;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_cards_text();
    kit_ui_shell_begin(&s_shell, s_screen, "QUIQUE", Q_ACCENT, 4);
    kit_ui_shell_tiles(&s_shell, NULL, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_jogo(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);
    kit_ui_help_page(s_shell.tiles[3], "CARTAS", s_cards_txt);
    build_arena();
    paint_idle();
    if (s_api->imu && s_api->imu->register_shake_callback)
        s_api->imu->register_shake_callback(on_shake, NULL);
    if (s_api->input) s_api->input->register_callback(on_touch, NULL);

    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);   /* abre no JOGO */
    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    kill_timer(&s_frame_timer);
    kill_timer(&s_step_timer);
    kill_timer(&s_toast_timer);
    gyro_end();
    kit_ui_keep_awake(false);
    if (s_api && s_api->imu && s_api->imu->register_shake_callback)
        s_api->imu->register_shake_callback(NULL, NULL);
    if (s_api && s_api->input) s_api->input->register_callback(NULL, NULL);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell = (kit_ui_shell_t){0};
    s_sens_chips = (kit_ui_chips_t){0};
    s_dir_chips = (kit_ui_chips_t){0};
    s_ctl_chips = (kit_ui_chips_t){0};
    s_touch_on = false;
    s_action = (kit_ui_action_t){0};
    s_idle_best = s_idle_top = NULL;
    s_arena = s_pause_btn = s_lives = s_watermark = s_strip = NULL;
    memset(s_life, 0, sizeof s_life);
    memset(s_pip, 0, sizeof s_pip);
    s_wall = s_fog = s_shield = s_toast = NULL;
    s_portal[0] = s_portal[1] = s_paddle[0] = s_paddle[1] = NULL;
    memset(s_ball, 0, sizeof s_ball);
    memset(s_dot, 0, sizeof s_dot);
    s_pick = s_pick_title = s_pick_build = s_pause = s_pause_more = s_pause_hint = NULL;
    memset(s_row_name, 0, sizeof s_row_name);
    memset(s_row_bon, 0, sizeof s_row_bon);
    memset(s_row_onu, 0, sizeof s_row_onu);
    memset(s_card, 0, sizeof s_card);
    memset(s_card_name, 0, sizeof s_card_name);
    memset(s_card_bon, 0, sizeof s_card_bon);
    memset(s_card_onu, 0, sizeof s_card_onu);
    s_over = s_over_score = s_over_caption = s_over_fx = NULL;
    s_step_fn = NULL;
    s_st = A_OFF;
    kit_ui_bind(NULL);
    s_api = NULL;
}

#endif
