/**
 * @file main.c
 * @brief DUO — jogo de dupla: os dois respondem as mesmas perguntas, um de
 *        cada vez, passando o KIT de mão em mão, e no fim sai o quanto bateu.
 *
 * Dois modos:
 *  - ME CONHECE: um responde sobre si e o outro tenta adivinhar o que ele
 *    respondeu. No fim dá pra trocar os papéis.
 *  - COMBINA: os dois respondem sobre si.
 *
 * Fluxo da rodada (tudo dentro do tile do JOGO, com o shell travado):
 *   VEZ DO P1 (tela na cor da Tool, segurar pra começar) -> perguntas do P1
 *   -> VEZ DO P2 -> perguntas do P2 -> OS DOIS JUNTOS (segurar pra revelar)
 *   -> revelação pergunta a pergunta -> resultado (o % sobe de 0).
 *
 * Decisões que não são óbvias:
 *  - Passar a vez pinta a tela inteira na cor da Tool e só começa SEGURANDO
 *    o dedo (HOLD_MS): ninguém vê sem querer as respostas do outro, e um
 *    toque acidental no fim da vez anterior não pula a tela.
 *  - Revelação: bateu = tela verde e as duas respostas fundidas num bloco só;
 *    não bateu = dois blocos desencontrados (um sobe, o outro desce). A forma
 *    já diz o resultado antes de ler.
 *  - Inéditas primeiro: 1 bit por pergunta e por tema em NVS (3 x i32 por
 *    tema). Quando acabam, ciclo novo (duo_pick).
 *  - A rodada não sobrevive a fechar/reabrir (é de mão em mão, minutos);
 *    só os ajustes e as inéditas persistem.
 *  - Sem lv_anim no runtime: as animações são lv_timer + translate/largura.
 *  - Inteiro puro (o .so não resolve float). Lógica em duo_game.c, banco
 *    gerado em duo_bank.h (scripts/make_bank.py).
 */
#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "duo_game.h"

#include <stdio.h>
#include <string.h>

#ifndef KIT_SDK_STUBS

#include "duo_bank.h"

/* ----------------------------------------------------------------------- */

#define ACC         KIT_COLOR_BLUE
#define ON_ACC      KIT_COLOR_ON_COLOR
#define HIT_BG      KIT_COLOR_GREEN
#define ON_HIT      KIT_COLOR_BG            /* preto no verde: paper ficaria 2,7:1 */

#define W           KIT_UI_SCREEN_W         /* 368 */
#define PAGE_H      KIT_UI_PAGE_H           /* 360 */
#define PAD         KIT_UI_PAD
#define CW          KIT_UI_CONTENT          /* 336 */
#define RADIUS      18

#define HOLD_MS     900     /* segurar pra começar / revelar */
#define HOLD_TICK   30
#define PICK_MS     320     /* a opção escolhida fica acesa antes da próxima */
#define ANIM_TICK   30
#define ANIM_FRAMES 8
#define APART_PX    10      /* desencontro dos blocos do "não bateu" */
#define ARENA_H     124
#define BLOCK_H     (ARENA_H - 2 * APART_PX)
#define TALLY_PX    20
#define COUNT_MAX_STEPS 25
#define PCT_NUDGE   5       /* '%' do display_44 desce em relação à base do 120 */
#define RES_GAP_PHRASE 8
#define RES_GAP_BTNS   18
#define RES_BTN_GAP    10
#define TICK_MS     14      /* bipe <= 14 ms o firmware toca a 0,34x: tique suave */
#define HAND_DY     (-(KIT_UI_TITLEBAR / 2))   /* centro da TELA, não do tile: a cor cobre tudo */

#define K_MODE  "duo_modo"
#define K_NQ    "duo_nq"
#define K_THEME "duo_tema"

static const char *const MODE_LABELS[]  = { "ME CONHECE", "COMBINA" };
static const char *const NQ_LABELS[]    = { "3", "5", "7", "10" };
static const int         NQ_VALUES[]    = { 3, 5, 7, 10 };
static const char *const THEME_LABELS[] = { "GERAL", "AMIGOS", "CASAL", "POL\xC3\x8AMICA" };

static const char RULES[] =
    "Pra 2 pessoas, passando o KIT de m\xC3\xA3o em m\xC3\xA3o.\n\n"
    "1. No AJUSTE, escolha o modo, quantas perguntas e o tema.\n\n"
    "2. Me conhece: um responde sobre si e o outro tenta adivinhar o que ele "
    "respondeu. Combina: os dois respondem sobre si.\n\n"
    "3. Quando a tela ficar azul, passe o KIT. Quem pegar segura o dedo na "
    "tela pra come\xC3\xA7" "ar.\n\n"
    "4. No fim, vejam juntos as respostas lado a lado. Verde \xC3\xA9 que "
    "bateu. Toque na tela pra ir passando.\n\n"
    "5. O resultado \xC3\xA9 quanto bateu. No Me conhece d\xC3\xA1 pra trocar "
    "os pap\xC3\xA9is e jogar de novo.\n\n"
    "As perguntas n\xC3\xA3o se repetem at\xC3\xA9 o tema acabar.";

typedef enum { ST_IDLE = 0, ST_HAND, ST_Q, ST_REV, ST_COUNT, ST_RES } duo_state_t;
enum { HAND_FIRST = 0, HAND_SECOND = 1, HAND_REVEAL = 2 };

/* ------------------------------------------------------------------ estado */

static const kit_api_table_t *s_api;
static lv_obj_t *s_screen;
static kit_ui_shell_t  s_shell;
static kit_ui_chips_t  s_mode_chips, s_nq_chips, s_theme_chips;
static kit_ui_action_t s_action;

static int s_mode, s_nq_idx, s_theme;     /* ajustes */
static int s_knower = 1;                   /* ME CONHECE: quem responde sobre si */
static uint32_t s_seen[DUO_THEME_COUNT][DUO_SEEN_WORDS];

static duo_state_t s_st;
static int s_hand;                         /* HAND_* */
static int s_qidx[DUO_MAX_Q], s_n, s_i, s_who;
static int8_t s_ans[2][DUO_MAX_Q];
static bool s_pick_lock;
static int s_hold_ms, s_anim_frame, s_count_k, s_count_n, s_pct;
static int s_res_dy;

/* JOGO parado */
static lv_obj_t *s_idle, *s_idle_n, *s_idle_sub, *s_idle_mode, *s_idle_who;

/* rodada */
static lv_obj_t *s_play;
static lv_obj_t *s_hand_p, *s_hand_top, *s_hand_big, *s_hand_txt, *s_hand_hint, *s_hand_fill;
static lv_obj_t *s_q_p, *s_q_who, *s_q_cnt, *s_q_txt, *s_q_row[2], *s_q_btn[4], *s_q_lbl[4];
static lv_obj_t *s_rev_p, *s_rev_txt, *s_rev_one, *s_rev_one_l[2], *s_rev_one_ans;
static lv_obj_t *s_rev_half[2], *s_rev_half_p[2], *s_rev_half_ans[2];
static lv_obj_t *s_rev_big, *s_rev_small, *s_rev_hint, *s_tally[DUO_MAX_Q];
static lv_obj_t *s_res_p, *s_res_row, *s_res_num, *s_res_pct, *s_res_phrase;
static lv_obj_t *s_res_again, *s_res_swap;

/* timers */
static lv_timer_t *s_hold_timer, *s_step_timer, *s_anim_timer;
static void (*s_step_fn)(void);

/* -------------------------------------------------------------------- util */

static int rng(int lo, int hi) { return kit_ui_rnd(lo, hi); }

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

static void seen_key(char *buf, int theme, int w) { snprintf(buf, 12, "duo_v%d%d", theme, w); }

static void load_seen(void)
{
    char k[12];
    for (int t = 0; t < DUO_THEME_COUNT; t++)
        for (int w = 0; w < DUO_SEEN_WORDS; w++) {
            seen_key(k, t, w);
            s_seen[t][w] = (uint32_t)get_i32(k, 0);
        }
}

static void save_seen(int t)
{
    char k[12];
    for (int w = 0; w < DUO_SEEN_WORDS; w++) {
        seen_key(k, t, w);
        set_i32(k, (int32_t)s_seen[t][w]);
    }
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

/* agenda o próximo passo (um só por vez) */
static void step_after(uint32_t ms, void (*fn)(void))
{
    kill_timer(&s_step_timer);
    s_step_fn = fn;
    s_step_timer = lv_timer_create(step_cb, ms ? ms : 1, NULL);
}

/* container de layout que não pode "engolir" o toque do painel de baixo */
static lv_obj_t *passive(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* caracteres, não bytes (acento = 2 bytes em UTF-8) */
static int utf8_len(const char *s)
{
    int n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static void set_text_color(lv_obj_t *o, uint32_t c) { lv_obj_set_style_text_color(o, lv_color_hex(c), 0); }
static void set_font(lv_obj_t *o, const lv_font_t *f, int ls)
{
    lv_obj_set_style_text_font(o, f, 0);
    lv_obj_set_style_text_letter_space(o, ls, 0);
}

static void paint_screen(uint32_t bg)
{
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(bg), 0);
}

static const duo_q_t *cur_q(int i) { return &DUO_THEMES[s_theme].q[s_qidx[i]]; }

/* jogador que responde em 1º / 2º: ME CONHECE começa por quem é conhecido */
static int player(int order)
{
    if (s_mode == DUO_MODE_CONHECE) return order == 0 ? s_knower : 3 - s_knower;
    return order + 1;
}

static int hits_until(int last)
{
    int h = 0;
    for (int i = 0; i <= last && i < s_n; i++) if (s_ans[0][i] == s_ans[1][i]) h++;
    return h;
}

/* ---------------------------------------------------------------- JOGO parado */

static void paint_idle(void)
{
    lv_label_set_text_fmt(s_idle_n, "%d", NQ_VALUES[s_nq_idx]);
    lv_label_set_text_fmt(s_idle_sub, "PERGUNTAS \xC2\xB7 %s", THEME_LABELS[s_theme]);
    lv_label_set_text(s_idle_mode, MODE_LABELS[s_mode]);
    if (s_mode == DUO_MODE_CONHECE)
        lv_label_set_text_fmt(s_idle_who, "P%d ADIVINHA O P%d", 3 - s_knower, s_knower);
    else
        lv_label_set_text(s_idle_who, "OS DOIS RESPONDEM");
}

static void show_panel(lv_obj_t *p)
{
    show(s_hand_p, p == s_hand_p);
    show(s_q_p, p == s_q_p);
    show(s_rev_p, p == s_rev_p);
    show(s_res_p, p == s_res_p);
}

/* ------------------------------------------------------------- passar a vez */

static void show_hand(int kind)
{
    s_st = ST_HAND;
    s_hand = kind;
    s_hold_ms = 0;
    lv_obj_set_width(s_hand_fill, 0);
    paint_screen(ACC);
    show_panel(s_hand_p);

    if (kind == HAND_REVEAL) {
        lv_label_set_text(s_hand_top, "");
        lv_label_set_text(s_hand_big, "JUNTOS");
        lv_label_set_text(s_hand_txt, "Agora vejam as respostas lado a lado.");
        lv_label_set_text(s_hand_hint, "SEGURE PRA REVELAR");
        return;
    }
    int p = player(kind == HAND_FIRST ? 0 : 1);
    int other = player(kind == HAND_FIRST ? 1 : 0);
    lv_label_set_text(s_hand_top, kind == HAND_FIRST ? "PEGUE O KIT" : "PASSE O KIT");
    lv_label_set_text_fmt(s_hand_big, "P%d", p);
    if (kind == HAND_FIRST)
        lv_label_set_text_fmt(s_hand_txt, "Responda sobre voc\xC3\xAA. N\xC3\xA3o deixe o P%d ver.", other);
    else if (s_mode == DUO_MODE_CONHECE)
        lv_label_set_text_fmt(s_hand_txt, "Adivinhe o que o P%d respondeu.", other);
    else
        lv_label_set_text(s_hand_txt, "Agora responda sobre voc\xC3\xAA.");
    lv_label_set_text(s_hand_hint, "SEGURE A TELA");
}

/* ----------------------------------------------------------------- pergunta */

static void show_question(void)
{
    s_st = ST_Q;
    s_pick_lock = false;
    paint_screen(KIT_COLOR_BG);
    show_panel(s_q_p);

    const duo_q_t *q = cur_q(s_i);
    bool guess = (s_mode == DUO_MODE_CONHECE && s_who == 1);
    if (guess) lv_label_set_text_fmt(s_q_who, "P%d \xC2\xB7 CHUTE O P%d", player(1), player(0));
    else       lv_label_set_text_fmt(s_q_who, "P%d \xC2\xB7 SOBRE VOC\xC3\x8A", player(s_who));
    lv_label_set_text_fmt(s_q_cnt, "%d/%d", s_i + 1, s_n);
    lv_label_set_text(s_q_txt, q->q);

    /* 2 opções: metades empilhadas (botões 0 e 2). 4 opções: grade 2x2. */
    bool four = (q->n == 4);
    bool big = (!four && (q->fit72 & 3) == 3);
    for (int b = 0; b < 4; b++) {
        int o = four ? b : (b == 0 ? 0 : (b == 2 ? 1 : -1));
        show(s_q_btn[b], o >= 0);
        if (o < 0) continue;
        lv_obj_set_style_bg_color(s_q_btn[b], lv_color_hex(KIT_COLOR_SURFACE), 0);
        set_text_color(s_q_lbl[b], KIT_COLOR_TEXT);
        lv_label_set_text(s_q_lbl[b], q->opt[o]);
        lv_obj_set_width(s_q_lbl[b], four ? CW / 2 - 24 : CW - 24);
        lv_obj_center(s_q_lbl[b]);
        if (big)       set_font(s_q_lbl[b], &kit_display_72, 0);
        else if (four) set_font(s_q_lbl[b], &kit_mono_20, 2);
        else           set_font(s_q_lbl[b], &kit_mono_26, 3);
    }
}

static void begin_reveal(void);

static void after_answer(void)
{
    s_pick_lock = false;
    if (s_i + 1 < s_n) { s_i++; show_question(); return; }
    if (s_who == 0) { s_who = 1; s_i = 0; show_hand(HAND_SECOND); return; }
    s_i = 0;
    show_hand(HAND_REVEAL);
}

static void opt_cb(lv_event_t *e)
{
    int b = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_st != ST_Q || s_pick_lock) return;
    const duo_q_t *q = cur_q(s_i);
    int o = (q->n == 4) ? b : (b == 0 ? 0 : 1);
    s_ans[s_who][s_i] = (int8_t)o;
    s_pick_lock = true;
    lv_obj_set_style_bg_color(s_q_btn[b], lv_color_hex(ACC), 0);
    set_text_color(s_q_lbl[b], ON_ACC);
    kit_ui_beep(988, TICK_MS);
    step_after(PICK_MS, after_answer);
}

/* --------------------------------------------------------------- revelação */

static void paint_tally(bool on_green)
{
    for (int i = 0; i < DUO_MAX_Q; i++) {
        lv_obj_t *t = s_tally[i];
        show(t, i < s_n);
        if (i >= s_n) continue;
        bool done = (i <= s_i), hit = done && s_ans[0][i] == s_ans[1][i];
        uint32_t ink = on_green ? KIT_COLOR_BG : KIT_COLOR_TEXT;
        lv_obj_set_style_border_width(t, (i == s_i) ? 3 : (done ? 0 : 2), 0);
        lv_obj_set_style_border_color(t, lv_color_hex(i == s_i ? ink : (on_green ? KIT_COLOR_BG : KIT_COLOR_LINE)), 0);
        lv_obj_set_style_border_opa(t, (i == s_i || !on_green) ? LV_OPA_COVER : LV_OPA_30, 0);
        if (!done) {
            lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
        } else if (hit) {
            lv_obj_set_style_bg_color(t, lv_color_hex(on_green ? KIT_COLOR_BG : HIT_BG), 0);
            lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(t, lv_color_hex(on_green ? KIT_COLOR_BG : KIT_COLOR_SURFACE_ALT), 0);
            lv_obj_set_style_bg_opa(t, on_green ? LV_OPA_20 : LV_OPA_COVER, 0);
        }
    }
}

static const lv_font_t *half_font(const char *s, int *ls)
{
    /* blocos de 163 px: até 7 letras em mono 26, mais que isso mono 20 */
    if (utf8_len(s) <= 7) { *ls = 3; return &kit_mono_26; }
    *ls = 1;
    return &kit_mono_20;
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    s_anim_frame++;
    int f = s_anim_frame;
    if (s_st == ST_REV) {
        bool hit = s_ans[0][s_i] == s_ans[1][s_i];
        if (hit) {
            /* o bloco único "se junta": cresce de 80% a 100% da largura */
            int w = CW - ((CW / 5) * (ANIM_FRAMES - f)) / ANIM_FRAMES;
            lv_obj_set_width(s_rev_one, w);
            lv_obj_align(s_rev_one, LV_ALIGN_TOP_MID, 0, APART_PX);
            lv_obj_set_style_translate_y(s_rev_big, (16 * (ANIM_FRAMES - f)) / ANIM_FRAMES, 0);
        } else {
            /* os dois blocos se desencontram: um sobe, o outro desce */
            int d = (APART_PX * f) / ANIM_FRAMES;
            lv_obj_set_style_translate_y(s_rev_half[0], -d, 0);
            lv_obj_set_style_translate_y(s_rev_half[1], d, 0);
        }
    } else if (s_st == ST_RES) {
        lv_obj_set_style_translate_y(s_res_row, (s_res_dy * (ANIM_FRAMES - f)) / ANIM_FRAMES, 0);
        lv_obj_set_style_opa(s_res_phrase, (lv_opa_t)((255 * f) / ANIM_FRAMES), 0);
        lv_obj_set_style_opa(s_res_again, (lv_opa_t)((255 * f) / ANIM_FRAMES), 0);
        lv_obj_set_style_opa(s_res_swap, (lv_opa_t)((255 * f) / ANIM_FRAMES), 0);
    }
    if (f >= ANIM_FRAMES) kill_timer(&s_anim_timer);
}

static void anim_start(void)
{
    kill_timer(&s_anim_timer);
    s_anim_frame = 0;
    s_anim_timer = lv_timer_create(anim_cb, ANIM_TICK, NULL);
}

static void show_reveal(void)
{
    s_st = ST_REV;
    const duo_q_t *q = cur_q(s_i);
    int x = s_ans[0][s_i], y = s_ans[1][s_i];
    bool hit = (x == y);
    uint32_t ink = hit ? ON_HIT : KIT_COLOR_TEXT;

    paint_screen(hit ? HIT_BG : KIT_COLOR_BG);
    show_panel(s_rev_p);
    lv_label_set_text(s_rev_txt, q->q);
    set_text_color(s_rev_txt, ink);

    show(s_rev_one, hit);
    show(s_rev_half[0], !hit);
    show(s_rev_half[1], !hit);
    show(s_rev_big, hit);
    show(s_rev_small, !hit);
    if (hit) {
        lv_obj_set_width(s_rev_one, (CW * 4) / 5);
        lv_obj_align(s_rev_one, LV_ALIGN_TOP_MID, 0, APART_PX);
        lv_label_set_text_fmt(s_rev_one_l[0], "P%d", player(0));
        lv_label_set_text_fmt(s_rev_one_l[1], "P%d", player(1));
        lv_label_set_text(s_rev_one_ans, q->opt[x]);
        if (q->fit72 & (1u << x)) set_font(s_rev_one_ans, &kit_display_72, 0);
        else                      set_font(s_rev_one_ans, &kit_mono_26, 3);
        set_text_color(s_rev_big, ON_HIT);
        lv_obj_set_style_translate_y(s_rev_big, 16, 0);
        kit_ui_sfx(KIT_SFX_VETO_HIT);
    } else {
        for (int k = 0; k < 2; k++) {
            int v = k ? y : x;
            int ls;
            const lv_font_t *f = half_font(q->opt[v], &ls);
            lv_label_set_text_fmt(s_rev_half_p[k], "P%d", player(k));
            lv_label_set_text(s_rev_half_ans[k], q->opt[v]);
            set_font(s_rev_half_ans[k], f, ls);
            lv_obj_set_style_translate_y(s_rev_half[k], 0, 0);
        }
        kit_ui_beep(784, 50);   /* duas notas descendo, sem ir abaixo de 600 Hz */
        kit_ui_beep(659, 80);
    }
    set_text_color(s_rev_hint, ink);
    lv_obj_set_style_opa(s_rev_hint, hit ? LV_OPA_60 : LV_OPA_COVER, 0);
    if (!hit) set_text_color(s_rev_hint, KIT_COLOR_TEXT_MUTED);
    lv_label_set_text(s_rev_hint, s_i + 1 < s_n ? "TOQUE PRA SEGUIR" : "TOQUE PRO RESULTADO");
    paint_tally(hit);
    anim_start();
}

static void begin_count(void);

static void rev_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != ST_REV) return;
    if (s_i + 1 < s_n) { s_i++; show_reveal(); }
    else begin_count();
}

static void begin_reveal(void)
{
    s_i = 0;
    show_reveal();
}

/* -------------------------------------------------------------- resultado */

/* o % sobe de 0 até o valor, centralizado; um bipe por passo com o tom
 * subindo, passos cada vez mais lentos. Depois sobe pro lugar dele. */
static void count_settle(void);

static void count_tick(void)
{
    s_count_k++;
    int v = (s_pct * s_count_k) / s_count_n;
    lv_label_set_text_fmt(s_res_num, "%d", v);
    kit_ui_beep((uint16_t)(700 + v * 6), TICK_MS);   /* tique curto (sai a 1/3 do volume) */
    if (s_count_k < s_count_n) {
        int r = (s_count_k * 100) / s_count_n;          /* 0..100 */
        step_after((uint32_t)(35 + (90 * r * r) / 10000), count_tick);
        return;
    }
    step_after(180, count_settle);
}

static void count_settle(void)
{
    s_st = ST_RES;
    kit_ui_shell_lock(&s_shell, false);   /* dá pra ir no AJUSTE antes de jogar de novo */
    bool swap = (s_mode == DUO_MODE_CONHECE);
    lv_label_set_text(s_res_phrase, duo_phrase(s_mode, s_pct));
    show(s_res_phrase, true);
    show(s_res_again, true);
    show(s_res_swap, swap);
    lv_obj_update_layout(s_res_p);

    /* número + frase + botões centralizados juntos na página */
    int h = lv_obj_get_height(s_res_row);
    int hp = lv_obj_get_height(s_res_phrase);
    int hb = swap ? 2 * KIT_UI_BTN_H + RES_BTN_GAP : KIT_UI_BTN_H;
    int top = (PAGE_H - (h + RES_GAP_PHRASE + hp + RES_GAP_BTNS + hb)) / 2;
    if (top < 0) top = 0;
    int y_phrase = top + h + RES_GAP_PHRASE;
    int y_btns = y_phrase + hp + RES_GAP_BTNS;

    s_res_dy = (PAGE_H - h) / 2 - top;     /* sai do centro (contagem) e sobe pro lugar */
    lv_obj_align(s_res_row, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_style_translate_y(s_res_row, s_res_dy, 0);
    lv_obj_align(s_res_phrase, LV_ALIGN_TOP_MID, 0, y_phrase);
    lv_obj_align(s_res_again, LV_ALIGN_TOP_MID, 0, y_btns);
    lv_obj_align(s_res_swap, LV_ALIGN_TOP_MID, 0, y_btns + KIT_UI_BTN_H + RES_BTN_GAP);
    lv_obj_set_style_opa(s_res_phrase, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(s_res_again, LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(s_res_swap, LV_OPA_TRANSP, 0);
    anim_start();
    /* fecho em notas médias e curtas: grave e longo estoura o alto-falante */
    if (s_pct >= 50) { kit_ui_beep(784, 60); kit_ui_beep(1047, 90); }
    else             { kit_ui_beep(784, 60); kit_ui_beep(659, 90); }
}

static void begin_count(void)
{
    s_st = ST_COUNT;
    s_pct = duo_percent(hits_until(s_n - 1), s_n);
    paint_screen(KIT_COLOR_BG);
    show_panel(s_res_p);
    show(s_res_phrase, false);
    show(s_res_again, false);
    show(s_res_swap, false);
    lv_obj_set_style_translate_y(s_res_row, 0, 0);
    lv_obj_align(s_res_row, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_res_num, "0");
    s_count_k = 0;
    s_count_n = s_pct < 1 ? 1 : (s_pct < COUNT_MAX_STEPS ? s_pct : COUNT_MAX_STEPS);
    step_after(300, count_tick);
}

/* ------------------------------------------------------------------ rodada */

static void round_start(void)
{
    kill_timer(&s_step_timer);
    kill_timer(&s_anim_timer);
    kill_timer(&s_hold_timer);
    const duo_theme_t *t = &DUO_THEMES[s_theme];
    s_n = duo_pick(s_seen[s_theme], t, s_mode, NQ_VALUES[s_nq_idx], s_qidx, rng);
    save_seen(s_theme);
    memset(s_ans, -1, sizeof s_ans);
    s_who = 0;
    s_i = 0;
    show(s_idle, false);
    kit_ui_action_show(&s_action, false);
    show(s_play, true);
    kit_ui_shell_lock(&s_shell, true);
    kit_ui_keep_awake(true);
    show_hand(HAND_FIRST);
}

/* ------------------------------------------------------------- callbacks */

static void hold_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_st != ST_HAND) { kill_timer(&s_hold_timer); return; }
    s_hold_ms += HOLD_TICK;
    int w = (CW * (s_hold_ms > HOLD_MS ? HOLD_MS : s_hold_ms)) / HOLD_MS;
    lv_obj_set_width(s_hand_fill, w);
    if (s_hold_ms < HOLD_MS) return;
    kill_timer(&s_hold_timer);
    kit_ui_confirm();
    if (s_hand == HAND_REVEAL) begin_reveal();
    else                       show_question();
}

static void hold_press_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != ST_HAND || s_hold_timer) return;
    s_hold_ms = 0;
    s_hold_timer = lv_timer_create(hold_tick_cb, HOLD_TICK, NULL);
}

static void hold_release_cb(lv_event_t *e)
{
    (void)e;
    kill_timer(&s_hold_timer);
    s_hold_ms = 0;
    if (s_hand_fill) lv_obj_set_width(s_hand_fill, 0);
}

static void action_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != ST_IDLE) return;
    kit_ui_confirm();
    round_start();
}

static void again_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != ST_RES) return;
    kit_ui_confirm();
    round_start();
}

static void swap_cb(lv_event_t *e)
{
    (void)e;
    if (s_st != ST_RES) return;
    kit_ui_confirm();
    s_knower = 3 - s_knower;
    paint_idle();
    round_start();
}

static void mode_cb(int idx, void *user)  { (void)user; s_mode = idx;   set_i32(K_MODE, idx);  paint_idle(); }
static void nq_cb(int idx, void *user)    { (void)user; s_nq_idx = idx; set_i32(K_NQ, idx);    paint_idle(); }
static void theme_cb(int idx, void *user) { (void)user; s_theme = idx;  set_i32(K_THEME, idx); paint_idle(); }

/* --------------------------------------------------------------- AJUSTE */

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
    lv_obj_set_style_pad_left(p, PAD, 0);
    lv_obj_set_style_pad_right(p, PAD, 0);
    lv_obj_set_style_pad_top(p, 8, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    section_label(p, "MODO");
    kit_ui_chips(&s_mode_chips, p, MODE_LABELS, 2, s_mode, ACC, mode_cb, NULL);
    section_label(p, "PERGUNTAS");
    kit_ui_chips(&s_nq_chips, p, NQ_LABELS, 4, s_nq_idx, ACC, nq_cb, NULL);
    section_label(p, "TEMA");
    kit_ui_chips(&s_theme_chips, p, THEME_LABELS, 4, s_theme, ACC, theme_cb, NULL);
}

/* ------------------------------------------------------------------ JOGO */

static lv_obj_t *panel(lv_obj_t *parent)
{
    lv_obj_t *p = kit_ui_box(parent);
    lv_obj_set_size(p, W, PAGE_H);
    lv_obj_set_pos(p, 0, 0);
    return p;
}

static lv_obj_t *pill(lv_obj_t *parent, uint32_t bg, uint32_t fg, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *b = kit_ui_rect(parent, CW, KIT_UI_BTN_H, bg, KIT_UI_BTN_H / 2);
    kit_ui_tap(b, cb, 0);
    lv_obj_center(kit_ui_label(b, txt, fg, &kit_mono_26, 3));
    return b;
}

static void build_idle(lv_obj_t *tile)
{
    s_idle = kit_ui_box(tile);
    lv_obj_set_size(s_idle, W, PAGE_H - KIT_UI_BTN_H - KIT_UI_BTN_MARGIN);
    lv_obj_set_pos(s_idle, 0, 0);
    kit_ui_flex(s_idle, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_SPACE_EVENLY, 0, 0);

    lv_obj_t *g1 = kit_ui_box(s_idle);
    lv_obj_set_size(g1, CW, LV_SIZE_CONTENT);
    kit_ui_flex(g1, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 6, 0);
    s_idle_n   = kit_ui_label(g1, "", KIT_COLOR_TEXT, &kit_display_120, 0);
    s_idle_sub = kit_ui_label(g1, "", KIT_COLOR_TEXT, &kit_mono_20, 2);

    lv_obj_t *g2 = kit_ui_box(s_idle);
    lv_obj_set_size(g2, CW, LV_SIZE_CONTENT);
    kit_ui_flex(g2, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 6, 0);
    s_idle_mode = kit_ui_label(g2, "", KIT_COLOR_TEXT, &kit_mono_26, 3);
    s_idle_who  = kit_ui_label(g2, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);

    kit_ui_action_button(&s_action, tile, ACC, action_cb);
    kit_ui_action_set(&s_action, "COME\xC3\x87" "AR");
}

static void build_hand(void)
{
    s_hand_p = panel(s_play);
    lv_obj_add_flag(s_hand_p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_hand_p, hold_press_cb,   LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(s_hand_p, hold_release_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(s_hand_p, hold_release_cb, LV_EVENT_PRESS_LOST, NULL);

    s_hand_top = kit_ui_label(s_hand_p, "", ON_ACC, &kit_mono_20, 2);
    lv_obj_align(s_hand_top, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *g = passive(kit_ui_box(s_hand_p));
    lv_obj_set_size(g, CW, LV_SIZE_CONTENT);
    kit_ui_flex(g, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 10, 0);
    lv_obj_align(g, LV_ALIGN_CENTER, 0, HAND_DY);
    s_hand_big = kit_ui_label(g, "", ON_ACC, &kit_display_72, 0);
    s_hand_txt = kit_ui_text(g, "", ON_ACC, &kit_sans_28, 300);

    s_hand_hint = kit_ui_label(s_hand_p, "", ON_ACC, &kit_mono_20, 2);
    lv_obj_align(s_hand_hint, LV_ALIGN_BOTTOM_MID, 0, -46);
    lv_obj_t *track = passive(kit_ui_rect(s_hand_p, CW, 10, KIT_COLOR_BG, 5));
    lv_obj_set_style_bg_opa(track, LV_OPA_30, 0);
    lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -22);
    s_hand_fill = passive(kit_ui_rect(track, 0, 10, ON_ACC, 5));
    lv_obj_set_pos(s_hand_fill, 0, 0);
}

static void build_question(void)
{
    s_q_p = panel(s_play);
    lv_obj_set_style_pad_left(s_q_p, PAD, 0);
    lv_obj_set_style_pad_right(s_q_p, PAD, 0);
    lv_obj_set_style_pad_top(s_q_p, 4, 0);
    lv_obj_set_style_pad_bottom(s_q_p, KIT_UI_BTN_MARGIN, 0);
    lv_obj_set_flex_flow(s_q_p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_q_p, 10, 0);

    lv_obj_t *top = kit_ui_box(s_q_p);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_q_who = kit_ui_label(top, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    s_q_cnt = kit_ui_label(top, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);

    s_q_txt = kit_ui_label(s_q_p, "", KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(s_q_txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_q_txt, lv_pct(100));

    lv_obj_t *opts = kit_ui_box(s_q_p);
    lv_obj_set_width(opts, lv_pct(100));
    lv_obj_set_flex_grow(opts, 1);
    lv_obj_set_flex_flow(opts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(opts, 10, 0);
    for (int r = 0; r < 2; r++) {
        lv_obj_t *row = kit_ui_box(opts);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_flex_grow(row, 1);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 10, 0);
        s_q_row[r] = row;
        for (int c = 0; c < 2; c++) {
            int b = r * 2 + c;
            lv_obj_t *btn = kit_ui_rect(row, 10, lv_pct(100), KIT_COLOR_SURFACE, RADIUS);
            lv_obj_set_flex_grow(btn, 1);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(btn, 5);
            lv_obj_add_event_cb(btn, opt_cb, LV_EVENT_CLICKED, (void *)(intptr_t)b);
            s_q_btn[b] = btn;
            s_q_lbl[b] = kit_ui_text(btn, "", KIT_COLOR_TEXT, &kit_mono_26, CW / 2 - 20);
            lv_obj_center(s_q_lbl[b]);
        }
    }
}

static void build_reveal(void)
{
    s_rev_p = panel(s_play);
    lv_obj_add_flag(s_rev_p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_rev_p, rev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_left(s_rev_p, PAD, 0);
    lv_obj_set_style_pad_right(s_rev_p, PAD, 0);
    lv_obj_set_style_pad_top(s_rev_p, 4, 0);
    lv_obj_set_style_pad_bottom(s_rev_p, 14, 0);
    lv_obj_set_flex_flow(s_rev_p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_rev_p, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_rev_txt = kit_ui_label(s_rev_p, "", KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(s_rev_txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_rev_txt, lv_pct(100));

    lv_obj_t *arena = passive(kit_ui_box(s_rev_p));
    lv_obj_set_size(arena, CW, ARENA_H);

    /* bateu: um bloco preto só, P1 e P2 nos cantos */
    s_rev_one = passive(kit_ui_rect(arena, CW, BLOCK_H, KIT_COLOR_BG, RADIUS));
    lv_obj_align(s_rev_one, LV_ALIGN_TOP_MID, 0, APART_PX);
    s_rev_one_l[0] = kit_ui_label(s_rev_one, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    lv_obj_align(s_rev_one_l[0], LV_ALIGN_TOP_LEFT, 14, 10);
    s_rev_one_l[1] = kit_ui_label(s_rev_one, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    lv_obj_align(s_rev_one_l[1], LV_ALIGN_TOP_RIGHT, -14, 10);
    s_rev_one_ans = kit_ui_label(s_rev_one, "", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(s_rev_one_ans, LV_ALIGN_CENTER, 0, 12);

    /* não bateu: dois blocos, cada um com a sua resposta */
    for (int k = 0; k < 2; k++) {
        lv_obj_t *h = passive(kit_ui_rect(arena, (CW - 10) / 2, BLOCK_H, KIT_COLOR_SURFACE, RADIUS));
        lv_obj_align(h, k ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, APART_PX);
        s_rev_half[k] = h;
        s_rev_half_p[k] = kit_ui_label(h, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
        lv_obj_align(s_rev_half_p[k], LV_ALIGN_TOP_LEFT, 12, 10);
        s_rev_half_ans[k] = kit_ui_text(h, "", KIT_COLOR_TEXT, &kit_mono_26, (CW - 10) / 2 - 16);
        lv_obj_align(s_rev_half_ans[k], LV_ALIGN_CENTER, 0, 12);
    }

    lv_obj_t *verdict = passive(kit_ui_box(s_rev_p));
    lv_obj_set_size(verdict, CW, 66);
    lv_obj_add_flag(verdict, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    s_rev_big = kit_ui_label(verdict, "BATEU", ON_HIT, &kit_display_72, 0);
    lv_obj_center(s_rev_big);
    s_rev_small = kit_ui_label(verdict, "N\xC3\x83O BATEU", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_center(s_rev_small);

    lv_obj_t *foot = passive(kit_ui_box(s_rev_p));
    lv_obj_set_size(foot, CW, LV_SIZE_CONTENT);
    kit_ui_flex(foot, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 8, 0);
    lv_obj_t *tally = passive(kit_ui_box(foot));
    lv_obj_set_size(tally, LV_SIZE_CONTENT, TALLY_PX + 6);
    kit_ui_flex(tally, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 8);
    for (int i = 0; i < DUO_MAX_Q; i++) {
        s_tally[i] = passive(kit_ui_rect(tally, TALLY_PX, TALLY_PX, KIT_COLOR_SURFACE, 3));
        lv_obj_set_style_border_side(s_tally[i], LV_BORDER_SIDE_FULL, 0);
    }
    s_rev_hint = kit_ui_label(foot, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
}

static void build_result(void)
{
    s_res_p = panel(s_play);

    s_res_row = kit_ui_box(s_res_p);
    lv_obj_set_size(s_res_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_res_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_res_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s_res_row, 6, 0);
    s_res_num = kit_ui_label(s_res_row, "0", KIT_COLOR_TEXT, &kit_display_120, 0);
    s_res_pct = kit_ui_label(s_res_row, "%", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_set_style_translate_y(s_res_pct, 18 + PCT_NUDGE, 0);

    s_res_phrase = kit_ui_text(s_res_p, "", KIT_COLOR_TEXT, &kit_sans_28, CW);

    s_res_swap = pill(s_res_p, KIT_COLOR_SURFACE, KIT_COLOR_TEXT, "TROCAR PAP\xC3\x89IS", swap_cb);
    lv_obj_align(s_res_swap, LV_ALIGN_BOTTOM_MID, 0, -KIT_UI_BTN_MARGIN);
    s_res_again = pill(s_res_p, ACC, ON_ACC, "JOGAR DE NOVO", again_cb);
    lv_obj_align(s_res_again, LV_ALIGN_BOTTOM_MID, 0, -(KIT_UI_BTN_MARGIN + KIT_UI_BTN_H + 10));
}

static void build_jogo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);   /* a cor da vez/do acerto vem da screen */
    build_idle(tile);

    s_play = kit_ui_box(tile);
    lv_obj_set_size(s_play, W, PAGE_H);
    lv_obj_set_pos(s_play, 0, 0);
    build_hand();
    build_question();
    build_reveal();
    build_result();
    show_panel(NULL);
    show(s_play, false);
}

#endif /* !KIT_SDK_STUBS */

/* ===================================================================== */

#ifdef KIT_SDK_STUBS

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Duo stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#else

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    int32_t v = get_i32(K_MODE, 0);
    s_mode = (v == 1) ? 1 : 0;
    v = get_i32(K_NQ, 1);                       /* padrão: 5 perguntas */
    s_nq_idx = (v >= 0 && v < 4) ? (int)v : 1;
    v = get_i32(K_THEME, 0);
    s_theme = (v >= 0 && v < DUO_THEME_COUNT) ? (int)v : 0;
    s_knower = 1;
    s_st = ST_IDLE;
    s_step_fn = NULL;
    load_seen();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "DUO", ACC, 3);
    kit_ui_shell_tiles(&s_shell, NULL, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_jogo(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);
    paint_idle();

    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);   /* abre no JOGO */
    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    kill_timer(&s_hold_timer);
    kill_timer(&s_step_timer);
    kill_timer(&s_anim_timer);
    kit_ui_keep_awake(false);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell = (kit_ui_shell_t){0};
    s_mode_chips = (kit_ui_chips_t){0};
    s_nq_chips = (kit_ui_chips_t){0};
    s_theme_chips = (kit_ui_chips_t){0};
    s_action = (kit_ui_action_t){0};
    s_idle = s_idle_n = s_idle_sub = s_idle_mode = s_idle_who = NULL;
    s_play = NULL;
    s_hand_p = s_hand_top = s_hand_big = s_hand_txt = s_hand_hint = s_hand_fill = NULL;
    s_q_p = s_q_who = s_q_cnt = s_q_txt = NULL;
    memset(s_q_row, 0, sizeof s_q_row);
    memset(s_q_btn, 0, sizeof s_q_btn);
    memset(s_q_lbl, 0, sizeof s_q_lbl);
    s_rev_p = s_rev_txt = s_rev_one = s_rev_one_ans = NULL;
    memset(s_rev_one_l, 0, sizeof s_rev_one_l);
    memset(s_rev_half, 0, sizeof s_rev_half);
    memset(s_rev_half_p, 0, sizeof s_rev_half_p);
    memset(s_rev_half_ans, 0, sizeof s_rev_half_ans);
    s_rev_big = s_rev_small = s_rev_hint = NULL;
    memset(s_tally, 0, sizeof s_tally);
    s_res_p = s_res_row = s_res_num = s_res_pct = s_res_phrase = NULL;
    s_res_again = s_res_swap = NULL;
    s_step_fn = NULL;
    s_st = ST_IDLE;
    s_pick_lock = false;
    kit_ui_bind(NULL);
    s_api = NULL;
}

#endif
