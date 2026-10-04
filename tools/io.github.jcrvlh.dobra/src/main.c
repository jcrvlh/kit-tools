/**
 * @file main.c
 * @brief DOBRA — junte peças iguais até a meta.
 *
 * Arraste no tabuleiro 4x4: as peças escorrem, duas iguais viram uma com o
 * dobro do valor. AJUSTE escolhe a meta (256/512/1024/Infinito) e quantos
 * "desfazer" (chacoalhar) a partida dá. A lógica pura mora em dobra_game.c.
 *
 * Decisões que não são óbvias:
 *  - Arraste: o SDK não expõe gesto do LVGL, só o stream bruto de toque
 *    (TOUCH_DOWN contínuo, sem TOUCH_UP). PRESSED/RELEASED de uma área
 *    armam/desarmam o arraste e a direção dispara assim que o dedo anda
 *    SWIPE_PX, sem esperar soltar.
 *  - Troca de página SÓ na faixa do título: o tileview não rola com o dedo
 *    (senão disputava o arraste com o tabuleiro e às vezes ganhava). Uma
 *    área invisível sobre a titlebar lê o arraste e troca de página por
 *    código; a página JOGO inteira, abaixo da titlebar, é área de jogada.
 *  - Animação: sem lv_anim na tabela de símbolos. Um lv_timer move as peças
 *    com translate_x/y em ANIM_FRAMES quadros; no fim, repinta tudo do estado
 *    novo (que já foi aplicado antes de começar).
 *  - Contraste: texto paper sobre o verde dá 2,7:1. No DOBRA, tudo que é
 *    cheio de verde (peça maior, chip ativo, botão) leva texto PRETO (6,4:1).
 */
#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "dobra_game.h"

#include <stdio.h>
#include <string.h>

#ifndef KIT_SDK_STUBS

/* ----------------------------------------------------------------------- */

#define D_ACCENT     KIT_COLOR_GREEN
#define D_ON_ACCENT  0x000000          /* preto sobre verde: 6,4:1 (paper dá 2,7:1) */

#define CELL         74
#define GAP          8
#define STEP         (CELL + GAP)
#define BOARD        (DOBRA_N * CELL + (DOBRA_N - 1) * GAP)   /* 320 */
#define BOARD_X      ((KIT_UI_SCREEN_W - BOARD) / 2)          /* 24 */
#define BOARD_Y      (KIT_UI_PAGE_H - BOARD)                  /* 40 */
#define TILE_RADIUS  12
#define TILE_BORDER  3

#define SWIPE_PX     36     /* deslocamento que vira jogada com o dedo na tela */
#define SWIPE_RELEASE_PX 20 /* no soltar basta menos (deslize rápido = poucas amostras) */
#define JUMP_MAX     200    /* salto de 1 amostra acima disto = lixo; deslize
                               rápido anda 100+ px entre leituras de ~33 ms */
#define ANIM_FRAMES  5
#define ANIM_MS      16
#define SAVE_MS      1200   /* grava a partida em lote, não a cada jogada */

typedef enum { ST_PLAY = 0, ST_WON, ST_OVER } dobra_state_t;

static const char *const META_LABELS[] = { "256", "512", "1024", "INFINITO" };
static const uint8_t     META_EXP[]    = { 8, 9, 10, 0 };
static const char *const UNDO_LABELS[] = { "DESLIGADO", "1X", "2X", "ILIMITADO" };
static const int8_t      UNDO_COUNT[]  = { 0, 1, 2, -1 };

/* nota da junção, por expoente criado — pentatônica subindo com a peça */
static const uint16_t MERGE_HZ[] = {
    0, 0, 523, 587, 659, 784, 880, 1047, 1175, 1319, 1568, 1760,
    2093, 2349, 2637, 3136, 3520, 4186,
};

static const char RULES[] =
    "1. Arraste o dedo no tabuleiro pra cima, pra baixo ou pros lados: todas "
    "as pe\xC3\xA7" "as escorrem pra esse lado.\n\n"
    "2. Duas pe\xC3\xA7" "as iguais que se encostam viram uma s\xC3\xB3, com o "
    "dobro do valor. Cada jun\xC3\xA7\xC3\xA3o soma pontos.\n\n"
    "3. A cada jogada nasce uma pe\xC3\xA7" "a nova, um 2 ou um 4.\n\n"
    "4. Chegue \xC3\xA0 meta antes de o tabuleiro encher. No Infinito, vale o "
    "recorde.\n\n"
    "5. A pe\xC3\xA7" "a verde \xC3\xA9 a maior do tabuleiro.\n\n"
    "6. Errou? Chacoalhe o KIT pra desfazer a \xC3\xBAltima jogada. Quantas "
    "vezes pode, voc\xC3\xAA escolhe no AJUSTE.\n\n"
    "7. Pra recome\xC3\xA7" "ar, toque em NOVA PARTIDA no AJUSTE.\n\n"
    "8. Pra trocar de p\xC3\xA1gina, deslize na faixa do t\xC3\xADtulo, l\xC3\xA1 em cima.";

/* ----------------------------------------------------------------------- */

static const kit_api_table_t *s_api;
static lv_obj_t *s_screen;
static kit_ui_shell_t s_shell;
static kit_ui_chips_t s_meta_chips;
static kit_ui_chips_t s_undo_chips;

static dobra_game_t  s_game;
static dobra_state_t s_state;
static int           s_meta_idx = 1;      /* 512 */
static int           s_undo_idx = 1;      /* 1x  */
static int32_t       s_record[4];

static lv_obj_t *s_board;
static lv_obj_t *s_tile[DOBRA_CELLS];
static lv_obj_t *s_tile_lbl[DOBRA_CELLS];
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_rec_lbl;
static lv_obj_t *s_undo_lbl;

static lv_obj_t *s_overlay;
static lv_obj_t *s_ov_title;
static lv_obj_t *s_ov_sub;
static lv_obj_t *s_ov_primary_lbl;
static lv_obj_t *s_ov_secondary;
static lv_obj_t *s_ov_hint;

static lv_obj_t *s_new_btn;
static lv_obj_t *s_new_lbl;
static bool      s_new_armed;             /* NOVA PARTIDA pede um 2º toque */

static lv_timer_t *s_anim_timer;
static lv_timer_t *s_save_timer;
static int         s_anim_frame;
static int16_t     s_anim_tx[DOBRA_CELLS];
static int16_t     s_anim_ty[DOBRA_CELLS];
static bool        s_dirty;

typedef enum { DRAG_NONE = 0, DRAG_BOARD, DRAG_TITLE } drag_t;

static drag_t s_drag;
static bool s_drag_fired;
static int  s_drag_x0, s_drag_y0;
static int  s_raw_x, s_raw_y;            /* última amostra de toque válida */
static bool s_raw_ok;

/* ---------------------------------------------------------------- util */

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

/* ------------------------------------------------------- persistência */

static const char *const REC_KEYS[] = { "dobra_r0", "dobra_r1", "dobra_r2", "dobra_r3" };

static void save_game(void)
{
    int32_t w[3];
    dobra_pack(&s_game, w);
    set_i32("dobra_b0", w[0]);
    set_i32("dobra_b1", w[1]);
    set_i32("dobra_b2", w[2]);
    set_i32("dobra_pts", (int32_t)s_game.score);
    set_i32("dobra_und", s_game.undo_left);
    set_i32("dobra_won", s_game.won ? 1 : 0);
    /* NVS não regrava valor igual — gravar os 4 recordes sai barato */
    for (int i = 0; i < 4; i++) set_i32(REC_KEYS[i], s_record[i]);
    s_dirty = false;
}

static void load_all(void)
{
    int32_t v = get_i32("dobra_meta", s_meta_idx);
    if (v >= 0 && v < 4) s_meta_idx = (int)v;
    v = get_i32("dobra_dsf", s_undo_idx);
    if (v >= 0 && v < 4) s_undo_idx = (int)v;
    for (int i = 0; i < 4; i++) {
        s_record[i] = get_i32(REC_KEYS[i], 0);
        if (s_record[i] < 0) s_record[i] = 0;
    }

    memset(&s_game, 0, sizeof s_game);
    s_game.goal_exp = META_EXP[s_meta_idx];

    int32_t w[3] = { get_i32("dobra_b0", 0), get_i32("dobra_b1", 0), get_i32("dobra_b2", 0) };
    if (dobra_unpack(&s_game, w)) {
        int32_t pts = get_i32("dobra_pts", 0);
        s_game.score = pts > 0 ? (uint32_t)pts : 0;
        int32_t und = get_i32("dobra_und", UNDO_COUNT[s_undo_idx]);
        s_game.undo_left = (und >= -1 && und <= 2) ? (int8_t)und : UNDO_COUNT[s_undo_idx];
        s_game.won = get_i32("dobra_won", 0) != 0;
    } else {
        dobra_new_game(&s_game, UNDO_COUNT[s_undo_idx], rng);
        s_dirty = true;
    }
}

static void save_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_dirty && !s_anim_timer) save_game();
}

/* -------------------------------------------------------------- pintura */

static const lv_font_t *font_for(uint8_t e)
{
    if (e <= 6)  return &kit_display_44;  /* 2..64: até 2 dígitos (59 px) */
    if (e <= 13) return &kit_sans_28;     /* 128..8192: até 4 dígitos (67 px) */
    return &kit_mono_20;                  /* 16384+: só no Infinito */
}

static void paint_board(void)
{
    uint8_t top = dobra_max_exp(&s_game);
    char buf[12];
    for (int i = 0; i < DOBRA_CELLS; i++) {
        lv_obj_t *t = s_tile[i];
        uint8_t e = s_game.cell[i];
        lv_obj_set_style_translate_x(t, 0, 0);
        lv_obj_set_style_translate_y(t, 0, 0);
        if (!e) { lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(t, LV_OBJ_FLAG_HIDDEN);

        bool hi = (e == top);
        lv_obj_set_style_bg_color(t, lv_color_hex(hi ? D_ACCENT : KIT_COLOR_BG), 0);
        lv_obj_set_style_border_width(t, hi ? 0 : TILE_BORDER, 0);
        snprintf(buf, sizeof buf, "%u", (unsigned)(1u << e));
        lv_label_set_text(s_tile_lbl[i], buf);
        lv_obj_set_style_text_font(s_tile_lbl[i], font_for(e), 0);
        lv_obj_set_style_text_color(s_tile_lbl[i],
            lv_color_hex(hi ? D_ON_ACCENT : KIT_COLOR_TEXT), 0);
        lv_obj_center(s_tile_lbl[i]);
    }
}

static void paint_status(void)
{
    lv_label_set_text_fmt(s_score_lbl, "%u PTS", (unsigned)s_game.score);
    lv_label_set_text_fmt(s_rec_lbl, "RECORDE %u", (unsigned)s_record[s_meta_idx]);

    if (UNDO_COUNT[s_undo_idx] == 0) {
        lv_obj_add_flag(s_undo_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_undo_lbl, LV_OBJ_FLAG_HIDDEN);
        if (s_game.undo_left < 0) lv_label_set_text(s_undo_lbl, "DESFAZER LIVRE");
        else lv_label_set_text_fmt(s_undo_lbl, "DESFAZER %d", (int)s_game.undo_left);
    }
}

static bool can_undo(void)
{
    return UNDO_COUNT[s_undo_idx] != 0 && s_game.undo_left != 0 && s_game.hist_len > 0;
}

static void show_overlay(dobra_state_t st)
{
    s_state = st;
    if (st == ST_PLAY) { lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN); return; }

    if (st == ST_WON) {
        lv_label_set_text(s_ov_title, "DOBROU");
        lv_label_set_text_fmt(s_ov_sub, "VOC\xC3\x8A CHEGOU A %u",
                              (unsigned)(1u << s_game.goal_exp));
        lv_label_set_text(s_ov_primary_lbl, "CONTINUAR");
        lv_obj_remove_flag(s_ov_secondary, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ov_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_ov_title, "FIM");
        lv_label_set_text(s_ov_sub, "SEM JOGADAS");
        lv_label_set_text(s_ov_primary_lbl, "NOVA PARTIDA");
        lv_obj_add_flag(s_ov_secondary, LV_OBJ_FLAG_HIDDEN);
        if (can_undo()) lv_obj_remove_flag(s_ov_hint, LV_OBJ_FLAG_HIDDEN);
        else            lv_obj_add_flag(s_ov_hint, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------- fluxo do jogo */

/* Depois que a jogada assentou (fim da animação): meta, fim de jogo, recorde. */
static void settle(void)
{
    paint_board();
    if ((int32_t)s_game.score > s_record[s_meta_idx]) s_record[s_meta_idx] = (int32_t)s_game.score;
    paint_status();
    s_dirty = true;

    if (dobra_goal_reached(&s_game)) {
        s_game.won = true;
        kit_ui_sfx(KIT_SFX_REVEAL);
        show_overlay(ST_WON);
    } else if (!dobra_can_move(&s_game)) {
        kit_ui_miss();
        show_overlay(ST_OVER);
    }
}

static void anim_stop(void)
{
    if (s_anim_timer) { lv_timer_delete(s_anim_timer); s_anim_timer = NULL; }
}

static void anim_finish(void)
{
    if (!s_anim_timer) return;
    anim_stop();
    settle();
}

static void anim_cb(lv_timer_t *t)
{
    (void)t;
    s_anim_frame++;
    if (s_anim_frame >= ANIM_FRAMES) { anim_finish(); return; }
    for (int i = 0; i < DOBRA_CELLS; i++) {
        if (!s_anim_tx[i] && !s_anim_ty[i]) continue;
        lv_obj_set_style_translate_x(s_tile[i], s_anim_tx[i] * s_anim_frame / ANIM_FRAMES, 0);
        lv_obj_set_style_translate_y(s_tile[i], s_anim_ty[i] * s_anim_frame / ANIM_FRAMES, 0);
    }
}

static void do_move(dobra_dir_t dir)
{
    if (s_state != ST_PLAY) return;
    anim_finish();   /* arraste rápido: a jogada anterior assenta na hora */
    if (s_state != ST_PLAY) return;

    dobra_move_t mv = dobra_move(&s_game, dir, rng);
    if (!mv.moved) return;

    if (mv.top_merge && mv.top_merge < sizeof MERGE_HZ / sizeof MERGE_HZ[0])
        kit_ui_beep(MERGE_HZ[mv.top_merge], 40);

    /* as peças ainda mostram o tabuleiro antigo; desliza cada uma até o destino */
    for (int i = 0; i < DOBRA_CELLS; i++) {
        s_anim_tx[i] = s_anim_ty[i] = 0;
        int d = mv.dest[i];
        if (d < 0 || d == i) continue;
        s_anim_tx[i] = (int16_t)((d % DOBRA_N - i % DOBRA_N) * STEP);
        s_anim_ty[i] = (int16_t)((d / DOBRA_N - i / DOBRA_N) * STEP);
    }
    s_anim_frame = 0;
    s_anim_timer = lv_timer_create(anim_cb, ANIM_MS, NULL);
}

static void new_game(void)
{
    anim_stop();
    dobra_new_game(&s_game, UNDO_COUNT[s_undo_idx], rng);
    s_game.goal_exp = META_EXP[s_meta_idx];
    show_overlay(ST_PLAY);
    paint_board();
    paint_status();
    save_game();
}

/* ----------------------------------------------------------- entradas */

static void on_page(int page, void *user);

static void goto_page(int delta)
{
    int cur = kit_ui_shell_active(&s_shell);
    int np = cur + delta;
    if (np < 0 || np >= s_shell.page_count) return;
    lv_tileview_set_tile_by_index(s_shell.tv, (uint32_t)np, 0, LV_ANIM_ON);
    /* com o dedo ainda na tela o tileview não manda VALUE_CHANGED no fim da
     * animação — sincroniza os dots já */
    kit_ui__dots_sync(&s_shell, np);
    on_page(np, NULL);
}

/* Decide o arraste com o deslocamento desde o toque até a última amostra. */
static void drag_eval(int threshold)
{
    int dx = s_raw_x - s_drag_x0, dy = s_raw_y - s_drag_y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax < threshold && ay < threshold) return;

    if (s_drag == DRAG_TITLE) {
        if (ax <= ay) return;   /* na titlebar só vale o eixo horizontal */
        s_drag_fired = true;
        goto_page(dx < 0 ? +1 : -1);   /* arrastar pra esquerda avança */
        return;
    }
    s_drag_fired = true;   /* uma jogada por arraste — rearma ao soltar */
    if (ax > ay) do_move(dx > 0 ? DOBRA_RIGHT : DOBRA_LEFT);
    else         do_move(dy > 0 ? DOBRA_DOWN : DOBRA_UP);
}

static void on_touch(const kit_input_event_t *ev, void *user)
{
    (void)user;
    if (!ev || ev->type != KIT_INPUT_TOUCH_DOWN) return;

    /* a amostra do instante do toque chega ANTES do PRESSED (o callback roda
     * dentro da leitura do indev) — guardada aqui, vira a origem do arraste */
    int sx = ev->x - s_raw_x, sy = ev->y - s_raw_y;
    bool sane = !s_raw_ok || (sx <= JUMP_MAX && sx >= -JUMP_MAX && sy <= JUMP_MAX && sy >= -JUMP_MAX);
    if (s_drag == DRAG_NONE || sane) {
        s_raw_x = ev->x;
        s_raw_y = ev->y;
        s_raw_ok = true;
    }
    if (s_drag == DRAG_NONE || !sane || s_drag_fired) return;
    drag_eval(SWIPE_PX);
}

static void drag_press_cb(lv_event_t *e)
{
    s_drag = (drag_t)(intptr_t)lv_event_get_user_data(e);
    s_drag_fired = false;
    s_drag_x0 = s_raw_x;
    s_drag_y0 = s_raw_y;
}

static void drag_release_cb(lv_event_t *e)
{
    (void)e;
    /* deslize rápido: o dedo sai antes de somar SWIPE_PX nas poucas amostras
     * (o indev lê a cada ~33 ms) — no soltar, aceita um limiar menor */
    if (s_drag != DRAG_NONE && !s_drag_fired) drag_eval(SWIPE_RELEASE_PX);
    s_drag = DRAG_NONE;
    s_raw_ok = false;
}

/* Área de arraste invisível: `kind` diz se o arraste é jogada ou página. */
static lv_obj_t *drag_area(lv_obj_t *parent, int x, int y, int w, int h, drag_t kind)
{
    lv_obj_t *a = kit_ui_box(parent);
    lv_obj_set_size(a, w, h);
    lv_obj_set_pos(a, x, y);
    lv_obj_add_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(a, drag_press_cb,   LV_EVENT_PRESSED,    (void *)(intptr_t)kind);
    lv_obj_add_event_cb(a, drag_release_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(a, drag_release_cb, LV_EVENT_PRESS_LOST, NULL);
    return a;
}

static void on_shake(void *user)
{
    (void)user;
    if (UNDO_COUNT[s_undo_idx] == 0) return;
    anim_stop();
    if (!dobra_undo(&s_game)) { paint_board(); kit_ui_miss(); return; }
    kit_ui_sfx(KIT_SFX_BACK);
    show_overlay(ST_PLAY);
    paint_board();
    paint_status();
    s_dirty = true;
}

static void ov_primary_cb(lv_event_t *e)
{
    (void)e;
    kit_ui_confirm();
    if (s_state == ST_WON) show_overlay(ST_PLAY);
    else                   new_game();
}

static void ov_secondary_cb(lv_event_t *e)
{
    (void)e;
    kit_ui_confirm();
    new_game();
}

/* --------------------------------------------------------------- AJUSTE */

/* kit_ui_chips pinta o ativo com paper sobre a cor da Tool; no verde isso
 * reprova em contraste — repinta o rótulo ativo de preto. */
static void chips_contrast(kit_ui_chips_t *c)
{
    lv_obj_set_style_text_color(c->lbl[c->selected], lv_color_hex(D_ON_ACCENT), 0);
}

static void paint_new_btn(void)
{
    lv_obj_set_style_bg_color(s_new_btn,
        lv_color_hex(s_new_armed ? D_ACCENT : KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(s_new_lbl,
        lv_color_hex(s_new_armed ? D_ON_ACCENT : KIT_COLOR_TEXT), 0);
    lv_label_set_text(s_new_lbl, s_new_armed ? "TOQUE DE NOVO" : "NOVA PARTIDA");
}

static void meta_cb(int idx, void *user)
{
    (void)user;
    chips_contrast(&s_meta_chips);
    s_meta_idx = idx;
    s_game.goal_exp = META_EXP[idx];
    /* meta nova vale na partida em curso; se já passou dela, não celebra */
    s_game.won = s_game.goal_exp && dobra_max_exp(&s_game) >= s_game.goal_exp;
    set_i32("dobra_meta", idx);
    paint_status();
    s_dirty = true;
}

static void undo_cb(int idx, void *user)
{
    (void)user;
    chips_contrast(&s_undo_chips);
    s_undo_idx = idx;
    s_game.undo_left = UNDO_COUNT[idx];
    set_i32("dobra_dsf", idx);
    paint_status();
    s_dirty = true;
}

static void new_btn_cb(lv_event_t *e)
{
    (void)e;
    /* partida sem nada a perder recomeça direto; senão pede confirmação */
    if (!s_new_armed && s_game.score > 0) {
        s_new_armed = true;
        kit_ui_click();
        paint_new_btn();
        return;
    }
    s_new_armed = false;
    paint_new_btn();
    kit_ui_confirm();
    new_game();
    kit_ui_shell_open(&s_shell, 1);
}

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
    lv_obj_set_style_pad_hor(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_top(p, 8, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    section_label(p, "META");
    kit_ui_chips(&s_meta_chips, p, META_LABELS, 4, s_meta_idx, D_ACCENT, meta_cb, NULL);
    chips_contrast(&s_meta_chips);

    section_label(p, "DESFAZER (CHACOALHAR)");
    kit_ui_chips(&s_undo_chips, p, UNDO_LABELS, 4, s_undo_idx, D_ACCENT, undo_cb, NULL);
    chips_contrast(&s_undo_chips);

    s_new_btn = lv_obj_create(p);
    lv_obj_set_size(s_new_btn, lv_pct(100), 84);
    lv_obj_set_style_radius(s_new_btn, 42, 0);
    lv_obj_set_style_border_width(s_new_btn, 0, 0);
    lv_obj_set_style_pad_all(s_new_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_new_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_new_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_new_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_new_btn, 8);
    lv_obj_add_event_cb(s_new_btn, new_btn_cb, LV_EVENT_CLICKED, NULL);
    s_new_lbl = kit_ui_label(s_new_btn, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_center(s_new_lbl);
    paint_new_btn();
}

/* ----------------------------------------------------------------- JOGO */

static lv_obj_t *ov_button(lv_obj_t *parent, int h, uint32_t bg, uint32_t fg,
                           const lv_font_t *font, lv_event_cb_t cb, lv_obj_t **lbl)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, BOARD, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = kit_ui_label(b, "", fg, font, 3);
    lv_obj_center(l);
    if (lbl) *lbl = l;
    return b;
}

static void build_overlay(lv_obj_t *tile)
{
    /* tela cheia por cima do tabuleiro (container só porque é tela cheia):
     * clicável pra engolir o toque no tabuleiro, mas não rola — o swipe
     * entre páginas continua passando pro tileview */
    s_overlay = kit_ui_box(tile);
    lv_obj_set_size(s_overlay, KIT_UI_SCREEN_W, KIT_UI_PAGE_H);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_80, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    kit_ui_flex(s_overlay, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 14, 0);

    s_ov_title = kit_ui_label(s_overlay, "", KIT_COLOR_TEXT, &kit_display_72, 0);
    s_ov_sub   = kit_ui_label(s_overlay, "", KIT_COLOR_TEXT, &kit_mono_20, 2);

    lv_obj_t *gap = kit_ui_box(s_overlay);
    lv_obj_set_size(gap, 1, 6);

    ov_button(s_overlay, KIT_UI_BTN_H, D_ACCENT, D_ON_ACCENT, &kit_mono_26,
              ov_primary_cb, &s_ov_primary_lbl);
    lv_obj_t *sec_lbl;
    s_ov_secondary = ov_button(s_overlay, 64, KIT_COLOR_SURFACE, KIT_COLOR_TEXT,
                               &kit_mono_20, ov_secondary_cb, &sec_lbl);
    lv_label_set_text(sec_lbl, "NOVA PARTIDA");

    s_ov_hint = kit_ui_label(s_overlay, "CHACOALHE PARA DESFAZER",
                             KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void build_jogo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    /* a página inteira é área de jogada; o resto por cima não recebe toque */
    drag_area(tile, 0, 0, KIT_UI_SCREEN_W, KIT_UI_PAGE_H, DRAG_BOARD);

    /* placar: pontos à esquerda; recorde e desfazer empilhados à direita */
    s_score_lbl = kit_ui_label(tile, "0 PTS", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_LEFT, BOARD_X, 2);

    lv_obj_t *right = kit_ui_box(tile);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(right, 2, 0);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -BOARD_X, 0);
    s_rec_lbl  = kit_ui_label(right, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    s_undo_lbl = kit_ui_label(right, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);

    s_board = kit_ui_box(tile);
    lv_obj_set_size(s_board, BOARD, BOARD);
    lv_obj_set_pos(s_board, BOARD_X, BOARD_Y);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_CLICKABLE);

    /* casas vazias (fundo) e peças (por cima) — peças fixas por casa; a
     * animação só as desloca com translate e a pintura volta tudo a zero */
    for (int i = 0; i < DOBRA_CELLS; i++) {
        lv_obj_t *slot = kit_ui_rect(s_board, CELL, CELL, KIT_COLOR_SURFACE, TILE_RADIUS);
        lv_obj_set_pos(slot, (i % DOBRA_N) * STEP, (i / DOBRA_N) * STEP);
        lv_obj_remove_flag(slot, LV_OBJ_FLAG_CLICKABLE);
    }
    for (int i = 0; i < DOBRA_CELLS; i++) {
        lv_obj_t *t = kit_ui_rect(s_board, CELL, CELL, KIT_COLOR_BG, TILE_RADIUS);
        lv_obj_set_pos(t, (i % DOBRA_N) * STEP, (i / DOBRA_N) * STEP);
        lv_obj_set_style_border_color(t, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_obj_set_style_border_width(t, TILE_BORDER, 0);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
        s_tile[i] = t;
        s_tile_lbl[i] = kit_ui_label(t, "", KIT_COLOR_TEXT, &kit_display_44, 0);
        lv_obj_center(s_tile_lbl[i]);
    }

    build_overlay(tile);
}

static void on_page(int page, void *user)
{
    (void)user;
    (void)page;
    if (s_new_armed) { s_new_armed = false; paint_new_btn(); }
}

#endif /* !KIT_SDK_STUBS */

/* ===================================================================== */

#ifdef KIT_SDK_STUBS

kit_err_t tool_init(kit_tool_ctx_t *ctx) { (void)ctx; return KIT_OK; }
void tool_destroy(void) {}

#else

kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    s_state = ST_PLAY;
    s_dirty = false;
    s_new_armed = false;
    s_drag = DRAG_NONE;
    load_all();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "DOBRA", D_ACCENT, 3);
    kit_ui_shell_tiles(&s_shell, on_page, NULL);
    lv_obj_remove_flag(s_shell.tv, LV_OBJ_FLAG_SCROLLABLE);   /* página só pela titlebar */
    /* faixa da titlebar à direita do chip de voltar (o chip segue tocável) */
    drag_area(s_screen, KIT_UI_PAD + KIT_UI_CHIP + 4, 0,
              KIT_UI_SCREEN_W - (KIT_UI_PAD + KIT_UI_CHIP + 4), KIT_UI_TITLEBAR, DRAG_TITLE);
    build_ajuste(s_shell.tiles[0]);
    build_jogo(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);

    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);

    paint_board();
    paint_status();
    if (!dobra_can_move(&s_game)) show_overlay(ST_OVER);

    if (s_api->input) s_api->input->register_callback(on_touch, NULL);
    if (s_api->imu)   s_api->imu->register_shake_callback(on_shake, NULL);
    s_save_timer = lv_timer_create(save_timer_cb, SAVE_MS, NULL);

    lv_screen_load(s_screen);
    return KIT_OK;
}

void tool_destroy(void)
{
    anim_stop();
    if (s_save_timer) { lv_timer_delete(s_save_timer); s_save_timer = NULL; }
    if (s_api && s_dirty) save_game();
    if (s_api && s_api->input) s_api->input->register_callback(NULL, NULL);
    if (s_api && s_api->imu)   s_api->imu->register_shake_callback(NULL, NULL);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell = (kit_ui_shell_t){0};
    s_meta_chips = (kit_ui_chips_t){0};
    s_undo_chips = (kit_ui_chips_t){0};
    s_board = s_score_lbl = s_rec_lbl = s_undo_lbl = NULL;
    s_overlay = s_ov_title = s_ov_sub = s_ov_primary_lbl = s_ov_secondary = s_ov_hint = NULL;
    s_new_btn = s_new_lbl = NULL;
    memset(s_tile, 0, sizeof s_tile);
    memset(s_tile_lbl, 0, sizeof s_tile_lbl);
    s_drag = DRAG_NONE;
    s_api = NULL;
}

#endif
