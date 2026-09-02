/**
 * @file main.c
 * @brief FORA — Tool de dedução social para o KIT.
 *
 * Estrutura padrão do KIT (ver kit/docs/design/design-language.md, Tool DADOS):
 * uma tela só, titlebar (chip Voltar = sair) + lv_tileview AJUSTES ◄──► PALCO,
 * começa no PALCO. Todas as fases reconfiguram os mesmos widgets — nunca
 * destruir/recriar num callback.
 *
 * Restrições do runtime de Tool (.so) que este arquivo respeita:
 *  - `.text` cabe na RAM executável interna do S3 (~9 KB) → helpers densos.
 *  - **poucos objetos LVGL** (pool de 64 KB, compartilhado com o Launcher que
 *    segue montado durante o tool_init) → seletores `◄ valor ►` em vez de
 *    grades de chips. ~55 objetos no total.
 *  - nada de `arr[i-1]` em array global (o GCC materializa `&arr-elem` que cai
 *    no buraco entre seções e o elf_loader relocaliza pra 0 → deref de NULL).
 *
 * Lógica pura do jogo: fora_game.c/h (sem LVGL).
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "fora_game.h"
#include "fora_words.h"
#include <string.h>
#include <stdint.h>

#define F_SCR_W        368
#define F_SCR_H        448
#define F_PAD          16
#define F_CONTENT      (F_SCR_W - 2 * F_PAD)
#define F_TITLEBAR     88
#define F_BTN_H        76
#define F_BTN_MARGIN   18
#define F_PAGE_H       (F_SCR_H - F_TITLEBAR)
#define F_STAGE_H      (F_PAGE_H - (F_BTN_H + 2 * F_BTN_MARGIN))
#define F_STEP         56
#define F_ACCENT       KIT_COLOR_RED

#define F_VOTE_TICKS   13
#define F_VOTE_MS_MIN  90
#define F_VOTE_MS_MAX  240

#ifndef KIT_SDK_STUBS

/* ===================================================================== */

static const kit_api_table_t *s_api;
static fora_state_t s_game;

static lv_obj_t *s_screen, *s_tv, *s_tiles[3], *s_dots[3], *s_dots_box;

/* AJUSTES ◄─ PALCO ─► COMO JOGAR */
#define F_TILES 3

static const char RULES[] =
    "O FORA n\xC3\xA3o sabe a palavra secreta. Todos os outros sabem qual \xC3\xA9.\n\n"
    "1. Passe o KIT. Cada jogador v\xC3\xAA sua palavra, ou a tela FORA, e esconde.\n\n"
    "2. Em cada rodada, todos fazem UMA pergunta sobre a palavra a outro "
    "jogador. N\xC3\xA3o entregue demais: o FORA escuta pra descobrir.\n\n"
    "3. No fim, todos votam em quem acham que \xC3\xA9 o FORA.\n\n"
    "Acertaram o FORA? Ele ainda tenta adivinhar a palavra entre 4 op\xC3\xA7\xC3\xB5" "es: "
    "se acertar, vence mesmo assim.\n\n"
    "Erraram? O FORA vence na hora.";

static lv_obj_t *s_players_lbl;
static int       s_name_sel;
static lv_obj_t *s_name_lbl, *s_slot_lbl[3], *s_name_clear;
static lv_obj_t *s_rounds_lbl, *s_cat_lbl;

static lv_obj_t *s_meta, *s_big, *s_sub, *s_detail, *s_stage_col;
/* QUESTION: quem pergunta ▶ quem responde, os dois em kit_display_72 */
static lv_obj_t *s_qrow, *s_qfrom, *s_qto;
/* um seletor ◄ valor ► serve as duas telas de escolha: votação e chute final */
static lv_obj_t *s_sel_box, *s_sel_lbl;
static int       s_sel, s_sel_max;
static lv_obj_t *s_primary, *s_primary_lbl;

static lv_timer_t *s_timer;
static int         s_tick;
static fora_phase_t s_next_phase;

static void render_phase(void);
static void sync_ajustes(void);
static void sync_dots(void);
static void ident_text(lv_obj_t *o, int player);

/* --- áudio: assinatura de suspense e blefe (D menor / A menor) ------------ */

enum { SFX_TAP, SFX_PASS, SFX_WORD, SFX_TICK,
       SFX_CAUGHT, SFX_ESCAPE, SFX_WIN, SFX_GUESS_OK, SFX_GUESS_NO };

/* Assinatura sonora do FORA: suspense, mistério e blefe (D menor / A menor).
   Frequências contidas na faixa 294–698 Hz (D4–F5) e durações curtas (10–80 ms)
   para nunca saturar nem estourar o falante do KIT.
   Tiques de votação a 10 ms (<= 14 ms) tocam na amplitude suave do driver (34%).
   NB: distribuir a palavra e distribuir o FORA usam o MESMO som (SFX_WORD) para
   não denunciar quem recebeu o quê. */
static const uint16_t SFX[][6] = {
    [SFX_TAP]      = { 440, 10 },                       /* A4, pip seco/discreto */
    [SFX_PASS]     = { 294, 30, 349, 40 },              /* D4 -> F4: transição suave e discreta */
    [SFX_WORD]     = { 440, 35, 523, 45, 587, 55 },     /* A4 -> C5 -> D5: revelação confidencial (A-C-D) */
    [SFX_TICK]     = { 494, 10 },                       /* B4, tique seco de 10 ms (34% amp, sem estalo) */
    [SFX_CAUGHT]   = { 622, 45, 440, 80 },              /* Eb5 -> A4: trítono dramático de tensão ("pegou!") */
    [SFX_ESCAPE]   = { 659, 35, 622, 35, 587, 65 },     /* E5 -> Eb5 -> D5: fuga cromática sutil */
    [SFX_WIN]      = { 440, 45, 587, 50, 698, 80 },     /* A4 -> D5 -> F5: resolução final D menor */
    [SFX_GUESS_OK] = { 294, 35, 349, 35, 440, 75 },     /* D4 -> F4 -> A4: triunfo do infiltrado */
    [SFX_GUESS_NO] = { 415, 40, 349, 70 },              /* G#4 -> F4: queda/chute errado ("não era") */
};

static void sfx(int id)
{
    if (!s_api || !s_api->audio) return;
    if (id == SFX_TAP && s_api->audio->sfx) {
        s_api->audio->sfx(KIT_SFX_CLICK);
        return;
    }
    const uint16_t *n = SFX[id];
    for (int i = 0; i < 6 && n[i]; i += 2) s_api->audio->beep(n[i], n[i + 1]);
}

/* --- helpers de widget -------------------------------------------------- */

static const kit_random_api_t *rng(void) { return s_api ? s_api->random : NULL; }
static int rng_i(int lo, int hi)
{
    return (s_api && s_api->random) ? (int)s_api->random->range(lo, hi) : lo;
}

static void show(lv_obj_t *o, bool v)
{
    if (o) (v ? lv_obj_remove_flag : lv_obj_add_flag)(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *pane(lv_obj_t *par)
{
    lv_obj_t *o = lv_obj_create(par);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* caixa preenchida (superfície/alvo de toque): pane sem estilo + só o essencial */
static lv_obj_t *rect(lv_obj_t *par, int w, int h, uint32_t bg, int rad)
{
    lv_obj_t *o = pane(par);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, rad, 0);
    return o;
}

static lv_obj_t *tap(lv_obj_t *o, lv_event_cb_t cb, int code)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(o, 8);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    return o;
}

static lv_obj_t *lbl(lv_obj_t *par, const char *t, uint32_t col,
                     const lv_font_t *f, int ls, int wrapw)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_font(l, f, 0);
    if (ls) lv_obj_set_style_text_letter_space(l, ls, 0);
    if (wrapw) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, wrapw);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
    return l;
}

static void flex(lv_obj_t *o, lv_flex_flow_t flow, lv_flex_align_t main, int prow, int pcol)
{
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (prow) lv_obj_set_style_pad_row(o, prow, 0);
    if (pcol) lv_obj_set_style_pad_column(o, pcol, 0);
}

/* pane rolável de tela cheia, coluna flex alinhada ao topo (AJUSTES / COMO JOGAR) */
static lv_obj_t *scroll_col(lv_obj_t *tile, int prow)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = pane(tile);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, F_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    flex(p, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, prow, 0);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void kill_timer(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_tick = 0;
}

/* Linha "ROTULO   ◄ [valor] ►" — 1 label de campo + 2 setas + 1 valor.
   `codeneg`/`codepos` vão pro callback. Devolve o label do valor. */
static lv_obj_t *stepper(lv_obj_t *par, const char *field, const lv_font_t *vf,
                         lv_event_cb_t cb, int codeneg, int codepos)
{
    lv_obj_t *sec = pane(par);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    flex(sec, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, 10, 0);
    lv_obj_set_flex_align(sec, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lbl(sec, field, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2, 0);

    lv_obj_t *row = pane(sec);
    lv_obj_set_size(row, lv_pct(100), F_STEP);
    flex(row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_START, 0, 10);

    lv_obj_t *lft = tap(rect(row, F_STEP, F_STEP, KIT_COLOR_SURFACE, 16), cb, codeneg);
    lv_obj_center(lbl(lft, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0, 0));

    lv_obj_t *val = lbl(row, "", KIT_COLOR_TEXT, vf, 1, 0);
    lv_obj_set_flex_grow(val, 1);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);

    lv_obj_t *rgt = tap(rect(row, F_STEP, F_STEP, KIT_COLOR_SURFACE, 16), cb, codepos);
    lv_obj_center(lbl(rgt, KIT_ICON_CHEVRON, KIT_COLOR_TEXT, &kit_display_44, 0, 0));
    return val;
}

/* --- persistência ----------------------------------------------------- */

static void save_config(void)
{
    if (!s_api || !s_api->storage) return;
    s_api->storage->set_i32("fora_players", s_game.num_players);
    s_api->storage->set_i32("fora_cat", s_game.category_index);
    s_api->storage->set_i32("fora_rounds", s_game.num_rounds);
    char p[64];
    int w = 0;
    p[0] = 0;
    for (int i = 0; i < FORA_MAX_PLAYERS && w < (int)sizeof p - 1; i++)
        w += snprintf(p + w, sizeof p - w, "%s%s", i ? "/" : "", s_game.player_names[i]);
    s_api->storage->set_str("fora_names", p);
}

static void load_config(void)
{
    if (!s_api || !s_api->storage) return;
    int32_t v;
    if (s_api->storage->get_i32("fora_players", &v) == KIT_OK &&
        v >= FORA_MIN_PLAYERS && v <= FORA_MAX_PLAYERS)
        s_game.num_players = v;
    if (s_api->storage->get_i32("fora_cat", &v) == KIT_OK &&
        v >= FORA_MIX_INDEX && v < FORA_CATEGORY_COUNT)
        s_game.category_index = v;
    if (s_api->storage->get_i32("fora_rounds", &v) == KIT_OK && (v == 1 || v == 2))
        s_game.num_rounds = v;
    char p[64];
    if (s_api->storage->get_str("fora_names", p, sizeof p) != KIT_OK) return;
    int pl = 0;
    const char *start = p;
    for (const char *c = p; pl < FORA_MAX_PLAYERS; c++) {
        if (*c == '/' || *c == 0) {
            int n = (int)(c - start);
            if (n > FORA_NAME_LEN - 1) n = FORA_NAME_LEN - 1;
            memcpy(s_game.player_names[pl], start, n);
            s_game.player_names[pl][n] = 0;
            pl++;
            start = c + 1;
            if (*c == 0) break;
        }
    }
}

/* --- navegação ------------------------------------------------------- */

static void sync_dots(void)
{
    lv_obj_t *act = s_tv ? lv_tileview_get_tile_active(s_tv) : NULL;
    for (int i = 0; i < F_TILES; i++) {
        bool on = (act == s_tiles[i]);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(on ? F_ACCENT : KIT_COLOR_LINE), 0);
        lv_obj_set_size(s_dots[i], on ? 20 : 8, 8);
    }
}

static void tv_changed_cb(lv_event_t *e) { (void)e; sync_dots(); }

static void set_swipe(bool on)
{
    if (!s_tv) return;
    if (on) {
        lv_obj_add_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
        lv_obj_remove_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    }
    show(s_dots_box, on);
    if (on) sync_dots();
}

static void back_cb(lv_event_t *e) { (void)e; if (s_api && s_api->system) s_api->system->exit(); }

/* --- AJUSTES: helpers de nome ---------------------------------------- */

static void name_slots(int pl, char o[3])
{
    const char *nm = s_game.player_names[pl];
    int L = (int)strlen(nm);
    for (int k = 0; k < 3; k++) o[k] = k < L ? nm[k] : ' ';
}

static void name_store(int pl, const char in[3])
{
    int a = 0, b = 2;
    while (a < 3 && in[a] == ' ') a++;
    while (b >= a && in[b] == ' ') b--;
    char *d = s_game.player_names[pl];
    int n = 0;
    for (int k = a; k <= b; k++) d[n++] = in[k];
    d[n] = 0;
}

/* rótulo do jogador atualmente selecionado no editor de nomes.
   Só "JOGADOR N" — a sigla aparece nas 3 caixas de letra abaixo; misturar as
   duas coisas na mesma linha estourava a largura e cortava com "...". */
static void name_sel_text(char *buf, size_t n)
{
    snprintf(buf, n, "JOGADOR %d", s_name_sel + 1);
}

static void sync_ajustes(void)
{
    char b[48];

    lv_label_set_text_fmt(s_players_lbl, "%d", s_game.num_players);
    lv_label_set_text(s_rounds_lbl, s_game.num_rounds == 1 ? "1" : "2");
    lv_label_set_text(s_cat_lbl, s_game.category_index < 0 ? "MIX"
                      : FORA_CATEGORIES[s_game.category_index].name);

    if (s_name_sel >= s_game.num_players) s_name_sel = 0;
    name_sel_text(b, sizeof b);
    lv_label_set_text(s_name_lbl, b);
    char sl[3];
    name_slots(s_name_sel, sl);
    for (int k = 0; k < 3; k++) {
        char t[2] = { sl[k] == ' ' ? '-' : sl[k], 0 };
        lv_label_set_text(s_slot_lbl[k], t);
        lv_obj_set_style_text_color(s_slot_lbl[k],
            lv_color_hex(sl[k] == ' ' ? KIT_COLOR_TEXT_MUTED : KIT_COLOR_TEXT), 0);
    }
    show(s_name_clear, fora_game_has_name(&s_game, s_name_sel));
}

/* --- AJUSTES: callbacks -------------------------------------------- */

static void players_cb(lv_event_t *e)
{
    int n = s_game.num_players + (int)(intptr_t)lv_event_get_user_data(e);
    if (n < FORA_MIN_PLAYERS) n = FORA_MAX_PLAYERS;
    if (n > FORA_MAX_PLAYERS) n = FORA_MIN_PLAYERS;
    s_game.num_players = n;
    sync_ajustes();
    save_config();
    sfx(SFX_TAP);
}

static void round_cb(lv_event_t *e)
{
    int n = s_game.num_rounds + (int)(intptr_t)lv_event_get_user_data(e);
    s_game.num_rounds = n < 1 ? FORA_MAX_ROUNDS : n > FORA_MAX_ROUNDS ? 1 : n;
    sync_ajustes();
    save_config();
    sfx(SFX_TAP);
}

static void cat_cb(lv_event_t *e)
{
    int c = s_game.category_index + (int)(intptr_t)lv_event_get_user_data(e);
    if (c < FORA_MIX_INDEX) c = FORA_CATEGORY_COUNT - 1;
    if (c >= FORA_CATEGORY_COUNT) c = FORA_MIX_INDEX;
    s_game.category_index = c;
    sync_ajustes();
    save_config();
    sfx(SFX_TAP);
}

static void name_sel_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_name_sel = (s_name_sel + d + s_game.num_players) % s_game.num_players;
    sync_ajustes();
    sfx(SFX_TAP);
}

static void slot_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    char sl[3];
    name_slots(s_name_sel, sl);
    char c = sl[k];
    sl[k] = c == ' ' ? 'A' : c == 'Z' ? ' ' : c + 1;
    name_store(s_name_sel, sl);
    sync_ajustes();
    save_config();
    sfx(SFX_TAP);
}

/* apaga a sigla do jogador selecionado → volta pro "JOGADOR N" padrão */
static void name_clear_cb(lv_event_t *e)
{
    (void)e;
    s_game.player_names[s_name_sel][0] = 0;
    sync_ajustes();
    save_config();
    sfx(SFX_PASS);
}

/* --- PALCO: fluxo -------------------------------------------------- */

static void new_game(void)
{
    fora_game_reset(&s_game);
    fora_game_select_word(&s_game, rng());
    s_game.current_player = 0;
    s_game.phase = FORA_PHASE_DISTRIBUTE;
}

static void primary_cb(lv_event_t *e)
{
    (void)e;
    kill_timer();
    fora_state_t *g = &s_game;

    switch (g->phase) {
    case FORA_PHASE_CONFIG:
        save_config();
        new_game();
        sfx(SFX_PASS);
        break;
    case FORA_PHASE_DISTRIBUTE:
        g->phase = FORA_PHASE_REVEAL;
        sfx(SFX_WORD);   /* mesmo som pro FORA e pra quem sabe a palavra */
        break;
    case FORA_PHASE_REVEAL:
        g->current_player++;
        g->phase = g->current_player < g->num_players ? FORA_PHASE_DISTRIBUTE : FORA_PHASE_ALL_READY;
        sfx(SFX_PASS);
        break;
    case FORA_PHASE_ALL_READY:
        g->current_round = 0;
        fora_game_generate_pairs(g, rng());
        g->current_pair = 0;
        g->phase = FORA_PHASE_QUESTION;
        sfx(SFX_TAP);
        break;
    case FORA_PHASE_QUESTION:
        g->current_pair++;
        if (g->current_pair >= g->num_pairs) g->phase = FORA_PHASE_ROUND_END;
        sfx(SFX_TAP);
        break;
    case FORA_PHASE_ROUND_END:
        fora_game_save_prev_pairs(g);
        g->current_round++;
        if (g->current_round < g->num_rounds) {
            fora_game_generate_pairs(g, rng());
            g->current_pair = 0;
            g->phase = FORA_PHASE_QUESTION;
        } else {
            s_sel = 0;
            g->phase = FORA_PHASE_VOTE;
        }
        sfx(SFX_TAP);
        break;
    case FORA_PHASE_VOTE:
        g->voted_player = s_sel;
        g->phase = FORA_PHASE_VOTE_REVEAL;   /* render_phase → vote_start() cria o timer */
        break;
    case FORA_PHASE_FINAL_GUESS:
        fora_game_check_guess(g, s_sel);
        g->phase = FORA_PHASE_GUESS_RESULT;
        sfx(g->fora_won ? SFX_GUESS_OK : SFX_GUESS_NO);
        break;
    case FORA_PHASE_FORA_ESCAPED:
        g->phase = FORA_PHASE_RESULT;
        sfx(SFX_TAP);
        break;
    case FORA_PHASE_GUESS_RESULT:
        g->phase = FORA_PHASE_RESULT;
        sfx(g->fora_won ? SFX_ESCAPE : SFX_WIN);
        break;
    case FORA_PHASE_RESULT:
        new_game();
        sfx(SFX_PASS);
        break;
    default:
        break;
    }
    render_phase();
}

static void post_vote_cb(lv_timer_t *t)
{
    (void)t;
    s_timer = NULL;
    s_game.phase = s_next_phase;
    render_phase();
}

static void vote_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_tick++;
    if (s_tick < F_VOTE_TICKS) {
        ident_text(s_big, rng_i(0, s_game.num_players - 1));   /* gira o número */
        sfx(SFX_TICK);
        lv_timer_set_period(s_timer, F_VOTE_MS_MIN +
            (uint32_t)(F_VOTE_MS_MAX - F_VOTE_MS_MIN) * s_tick / (F_VOTE_TICKS - 1));
        return;
    }
    kill_timer();
    bool caught = fora_game_vote_correct(&s_game);
    ident_text(s_big, s_game.voted_player);
    lv_obj_set_style_text_color(s_big, lv_color_hex(caught ? F_ACCENT : KIT_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_sub, &kit_mono_26, 0);
    if (caught) {
        lv_label_set_text(s_sub, "ERA O FORA");
        lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT), 0);
        sfx(SFX_CAUGHT);
        fora_game_generate_guess(&s_game, rng());
        s_next_phase = FORA_PHASE_FINAL_GUESS;
    } else {
        lv_label_set_text(s_sub, "N\xC3\x83O ERA O FORA");
        lv_obj_set_style_text_color(s_sub, lv_color_hex(F_ACCENT), 0);
        sfx(SFX_ESCAPE);
        s_game.fora_won = true;
        s_next_phase = FORA_PHASE_FORA_ESCAPED;
    }
    s_timer = lv_timer_create(post_vote_cb, 1700, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}

static void vote_start(void)
{
    s_tick = 0;
    s_timer = lv_timer_create(vote_tick_cb, F_VOTE_MS_MIN, NULL);
}

/* Contagem de caracteres visuais UTF-8 (em vez de bytes). */
static int utf8_len(const char *s)
{
    int len = 0;
    while (*s) {
        if ((*s & 0xC0) != 0x80) len++;
        s++;
    }
    return len;
}

/* Verifica se todos os caracteres da string estão presentes em kit_display_72:
   ASCII A-Z, 0-9, '-', ' ' e Latin-1: Ã (0xC3 0x83), Ç (0xC3 0x87), Õ (0xC3 0x95). */
static bool display72_supported(const char *w)
{
    const unsigned char *c = (const unsigned char *)w;
    while (*c) {
        if (*c == ' ' || *c == '-' || (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z')) {
            c++;
        } else if (*c == 0xC3 && *(c + 1)) {
            unsigned char c2 = *(c + 1);
            if (c2 == 0x83 || c2 == 0x87 || c2 == 0x95) {
                c += 2;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

/* A palavra secreta seleciona a fonte dinamicamente para NUNCA quebrar linha:
   - kit_display_72: palavras muito curtas (<= 5 caracteres) com glifos suportados.
   - kit_display_44: palavras médias (6 a 9 caracteres, ex: "ESFIRRA", "FUTEBOL", "BASQUETE").
   - kit_mono_26: palavras longas (10 a 16 caracteres, ex: "COMPUTADOR").
   - kit_mono_20: expressões muito longas (>= 17 caracteres). */
static const lv_font_t *word_font(const char *w, int *out_ls)
{
    int n = utf8_len(w);
    if (n <= 5 && display72_supported(w)) {
        if (out_ls) *out_ls = 0;
        return &kit_display_72;
    }
    if (n <= 9 && display72_supported(w)) {
        if (out_ls) *out_ls = 0;
        return &kit_display_44;
    }
    if (n <= 16) {
        if (out_ls) *out_ls = 1;
        return &kit_mono_26;
    }
    if (out_ls) *out_ls = 1;
    return &kit_mono_20;
}

/* rótulo do seletor: nome de jogador (votação) ou palavra candidata (chute) */
static void render_sel(void)
{
    char a[16];
    if (s_game.phase == FORA_PHASE_FINAL_GUESS) {
        show(s_sub, false);
        const char *gw = fora_game_get_guess_word(&s_game, s_sel);
        int ls = 1;
        const lv_font_t *f = word_font(gw, &ls);
        if (f == &kit_display_72 || f == &kit_display_44) f = &kit_mono_26;
        lv_obj_set_style_text_font(s_sel_lbl, f, 0);
        lv_obj_set_style_text_letter_space(s_sel_lbl, ls, 0);
        lv_label_set_text(s_sel_lbl, gw);
    } else {
        bool has_name = fora_game_has_name(&s_game, s_sel);
        if (has_name) {
            snprintf(a, sizeof a, "%s", s_game.player_names[s_sel]);
            lv_label_set_text_fmt(s_sub, "JOGADOR %d", s_sel + 1);
        } else {
            snprintf(a, sizeof a, "%d", s_sel + 1);
            lv_label_set_text(s_sub, "JOGADOR");
        }
        show(s_sub, true);
        lv_obj_set_style_text_font(s_sub, &kit_mono_20, 0);
        lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_obj_set_style_text_letter_space(s_sub, 2, 0);

        /* Sigla ou número em destaque gigante */
        const lv_font_t *f = has_name ? &kit_display_44 : &kit_display_72;
        lv_obj_set_style_text_font(s_sel_lbl, f, 0);
        lv_obj_set_style_text_letter_space(s_sel_lbl, 0, 0);
        lv_label_set_text(s_sel_lbl, a);
    }
}

static void sel_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel = (s_sel + d + s_sel_max) % s_sel_max;
    render_sel();
    sfx(SFX_TAP);
}

/* --- PALCO: render ------------------------------------------------- */

static void P(const char *m, const char *g, const char *s, const char *btn,
              const lv_font_t *gf, uint32_t gc)
{
    if (s_stage_col) {
        lv_obj_set_flex_align(s_stage_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(s_stage_col, 0, 0);
        lv_obj_set_style_pad_bottom(s_stage_col, 0, 0);
    }
    show(s_meta, m != NULL);
    show(s_big, g != NULL);
    show(s_sub, s != NULL);
    show(s_detail, false);
    show(s_qrow, false);
    show(s_sel_box, false);
    show(s_primary, btn != NULL);
    /* volta meta/sub/big ao padrão; fases sobrescrevem logo depois */
    lv_obj_set_style_text_color(s_meta, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_meta, &kit_mono_26, 0);
    lv_obj_set_style_text_letter_space(s_meta, 2, 0);
    lv_label_set_long_mode(s_big, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_sub, &kit_mono_20, 0);
    lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_obj_set_style_text_letter_space(s_sub, 2, 0);
    if (m) lv_label_set_text(s_meta, m);
    if (g) {
        lv_label_set_text(s_big, g);
        lv_obj_set_style_text_font(s_big, gf, 0);
        lv_obj_set_style_text_color(s_big, lv_color_hex(gc), 0);
    }
    if (s) lv_label_set_text(s_sub, s);
    if (btn) lv_label_set_text(s_primary_lbl, btn);
}

/* Identidade "grande" de um jogador: sigla (A-Z, sem kerning) ou o número
   solto em kit_display_72 — "JOGADOR N" inteiro não cabe nessa fonte.
   s_big = protagonista; s_sub = rótulo de apoio ("JOGADOR" / "JOGADOR N"). */
/* escreve a sigla (A-Z) ou o número do jogador num label */
static void ident_text(lv_obj_t *o, int player)
{
    if (fora_game_has_name(&s_game, player))
        lv_label_set_text(o, s_game.player_names[player]);
    else
        lv_label_set_text_fmt(o, "%d", player + 1);
}

static void big_identity(int player, uint32_t color)
{
    ident_text(s_big, player);
    if (fora_game_has_name(&s_game, player))
        lv_label_set_text_fmt(s_sub, "JOGADOR %d", player + 1);
    else
        lv_label_set_text(s_sub, "JOGADOR");
    lv_obj_set_style_text_font(s_big, &kit_display_72, 0);
    lv_obj_set_style_text_color(s_big, lv_color_hex(color), 0);
}

static void render_phase(void)
{
    char a[16], b[72];
    fora_state_t *g = &s_game;
    const lv_font_t *M = &kit_mono_26, *D = &kit_display_72;
    uint32_t TX = KIT_COLOR_TEXT, AC = F_ACCENT;

    switch (g->phase) {
    case FORA_PHASE_CONFIG:
        P("QUEM EST\xC3\x81 FORA?", "FORA", "ARRASTE PARA AJUSTAR", "COME\xC3\x87" "AR", D, AC);
        lv_obj_set_style_text_font(s_meta, &kit_mono_20, 0);
        lv_obj_set_style_text_color(s_meta, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        break;
    case FORA_PHASE_DISTRIBUTE:
        P(g->current_player == 0 ? "PEGUE O KIT" : "PASSE O KIT", " ", " ", "REVELAR", D, TX);
        big_identity(g->current_player, TX);
        lv_obj_set_style_text_font(s_meta, &kit_mono_20, 0);
        lv_obj_set_style_text_color(s_meta, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        break;
    case FORA_PHASE_REVEAL: {
        /* FORA e não-FORA têm exatamente o mesmo peso/cor — um olhar de canto
           não distingue quem recebeu o quê. */
        const char *w = g->current_player == g->fora_player ? "FORA" : fora_game_get_word(g);
        int ls = 1;
        const lv_font_t *wf = word_font(w, &ls);
        P("SUA PALAVRA", w, NULL, "OCULTAR", wf, TX);
        lv_obj_set_style_text_letter_space(s_big, ls, 0);
        lv_obj_set_style_text_font(s_meta, &kit_mono_16, 0);
        lv_obj_set_style_text_color(s_meta, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        break;
    }
    case FORA_PHASE_ALL_READY:
        P(NULL, "TODOS\nPRONTOS?", NULL, "COME\xC3\x87" "AR", &kit_sans_28, TX);
        lv_obj_set_style_text_letter_space(s_big, 2, 0);
        break;
    case FORA_PHASE_QUESTION: {
        snprintf(b, sizeof b, "RODADA %d  \xC2\xB7  %d/%d",
                 g->current_round + 1, g->current_pair + 1, g->num_pairs);
        P(b, NULL, "FA\xC3\x87" "A UMA PERGUNTA", "CONTINUAR", M, TX);
        show(s_qrow, true);
        int pf = g->pairs_from[g->current_pair];
        int pt = g->pairs_to[g->current_pair];
        const lv_font_t *qf = (fora_game_has_name(g, pf) || fora_game_has_name(g, pt))
                              ? &kit_display_44 : &kit_display_72;
        lv_obj_set_style_text_font(s_qfrom, qf, 0);
        lv_obj_set_style_text_font(s_qto, qf, 0);
        ident_text(s_qfrom, pf);   /* quem pergunta */
        ident_text(s_qto,   pt);   /* quem responde */
        break;
    }
    case FORA_PHASE_ROUND_END:
        snprintf(b, sizeof b, "RODADA %d\nCONCLU\xC3\x8D" "DA", g->current_round + 1);
        P(NULL, b, NULL,
          g->current_round + 1 < g->num_rounds ? "PR\xC3\x93XIMA RODADA" : "HORA DE VOTAR", &kit_sans_28, TX);
        lv_obj_set_style_text_letter_space(s_big, 2, 0);
        break;
    case FORA_PHASE_VOTE:
        P("QUEM \xC3\x89 O FORA?", NULL, NULL, "CONFIRMAR", M, TX);
        s_sel_max = g->num_players;
        show(s_sel_box, true);
        render_sel();
        break;
    case FORA_PHASE_VOTE_REVEAL:
        P("A MAIORIA ESCOLHEU", " ", "", NULL, D, KIT_COLOR_TEXT_MUTED);
        lv_obj_set_style_text_font(s_meta, &kit_mono_16, 0);
        lv_obj_set_style_text_color(s_meta, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        vote_start();
        break;
    case FORA_PHASE_FORA_ESCAPED:
        P(NULL, "O FORA\nFUGIU", NULL, "CONTINUAR", D, AC);
        lv_obj_set_style_text_letter_space(s_big, 0, 0);
        break;
    case FORA_PHASE_FINAL_GUESS:
        P("\xC3\x9ALTIMA CHANCE\nQUAL ERA A PALAVRA", NULL, NULL, "CHUTAR", M, TX);
        s_sel = 0;
        s_sel_max = FORA_GUESS_OPTIONS;
        show(s_sel_box, true);
        render_sel();
        break;
    case FORA_PHASE_GUESS_RESULT: {
        const char *w = fora_game_get_word(g);
        snprintf(b, sizeof b, "PALAVRA: %s", w);
        int w_len = utf8_len(b);
        const lv_font_t *pf = (w_len <= 16) ? &kit_mono_20 : &kit_mono_16;

        P("CHUTE FINAL",
          g->fora_won ? "ACERTOU!" : "N\xC3\x83O ERA",
          b,
          "CONTINUAR",
          M,
          g->fora_won ? TX : AC);

        /* Eyebrow muted mono_16 */
        lv_obj_set_style_text_font(s_meta, &kit_mono_16, 0);
        lv_obj_set_style_text_color(s_meta, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);

        /* Hero com letter-space 3 */
        lv_obj_set_style_text_letter_space(s_big, 3, 0);

        /* Linha "PALAVRA: ..." nunca quebra linha */
        lv_label_set_long_mode(s_sub, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(s_sub, pf, 0);
        lv_obj_set_style_text_letter_space(s_sub, 1, 0);
        lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT), 0);

        /* Vencedor da partida em destaque */
        show(s_detail, true);
        lv_label_set_text(s_detail, g->fora_won ? "O FORA VENCEU" : "OS JOGADORES VENCERAM");
        lv_obj_set_style_text_font(s_detail, &kit_mono_20, 0);
        lv_obj_set_style_text_letter_space(s_detail, 2, 0);
        lv_obj_set_style_text_color(s_detail, lv_color_hex(g->fora_won ? AC : TX), 0);
        break;
    }
    case FORA_PHASE_RESULT: {
        fora_game_player_label(g, g->fora_player, a, sizeof a);
        snprintf(b, sizeof b, "%s ERA O FORA", a);

        P("RESULTADO",
          g->fora_won ? "FORA\nVENCEU" : "JOGADORES\nVENCERAM",
          b,
          "JOGAR DE NOVO",
          &kit_display_44,
          g->fora_won ? AC : TX);

        /* Permite rolagem vertical suave com fontes grandes e alinhamento do topo */
        lv_obj_set_flex_align(s_stage_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(s_stage_col, 8, 0);
        lv_obj_set_style_pad_bottom(s_stage_col, 16, 0);

        /* Eyebrow em mono_20 com tracking 3 */
        lv_obj_set_style_text_font(s_meta, &kit_mono_20, 0);
        lv_obj_set_style_text_letter_space(s_meta, 3, 0);
        lv_obj_set_style_text_color(s_meta, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);

        /* Hero com display_44 e letter-space 1 */
        lv_obj_set_style_text_letter_space(s_big, 1, 0);

        /* Identidade do FORA em mono_26 off-white */
        lv_obj_set_style_text_font(s_sub, &kit_mono_26, 0);
        lv_obj_set_style_text_letter_space(s_sub, 1, 0);
        lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT), 0);

        /* Palavra e Categoria em linhas separadas em mono_20 branco */
        show(s_detail, true);
        snprintf(b, sizeof b, "PALAVRA: %s\nASSUNTO: %s",
                 fora_game_get_word(g), fora_game_get_category_name(g));
        lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_detail, F_CONTENT);
        lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_detail, b);
        lv_obj_set_style_text_font(s_detail, &kit_mono_20, 0);
        lv_obj_set_style_text_letter_space(s_detail, 1, 0);
        lv_obj_set_style_text_color(s_detail, lv_color_hex(KIT_COLOR_TEXT), 0);
        break;
    }
    }

    set_swipe(g->phase == FORA_PHASE_CONFIG || g->phase == FORA_PHASE_RESULT);
}

/* --- construção -------------------------------------------------- */

static void build_titlebar(void)
{
    lv_obj_t *c = tap(rect(s_screen, F_STEP, F_STEP, KIT_COLOR_SURFACE, 18), back_cb, 0);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, F_PAD, 16);
    lv_obj_center(lbl(c, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0, 0));
    lv_obj_t *tt = lbl(s_screen, "FORA", KIT_COLOR_TEXT, &kit_mono_26, 3, 0);
    lv_obj_align(tt, LV_ALIGN_TOP_LEFT, F_PAD + F_STEP + 12, 30);

    /* indicador de paginação AJUSTES ◄─► PALCO (igual DADOS) */
    s_dots_box = pane(s_screen);
    lv_obj_set_size(s_dots_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_dots_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_dots_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_dots_box, 6, 0);
    lv_obj_align(s_dots_box, LV_ALIGN_TOP_RIGHT, -F_PAD, 40);
    for (int i = 0; i < F_TILES; i++)
        s_dots[i] = rect(s_dots_box, 8, 8, KIT_COLOR_LINE, 4);
}

static void build_rules(lv_obj_t *tile)
{
    lv_obj_t *p = scroll_col(tile, 14);
    lbl(p, "COMO JOGAR", KIT_COLOR_TEXT, &kit_mono_26, 3, 0);
    lv_obj_t *body = lbl(p, RULES, KIT_COLOR_TEXT, &kit_sans_22, 0, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, F_CONTENT);
}

static void build_adjust(lv_obj_t *tile)
{
    lv_obj_t *p = scroll_col(tile, 22);

    s_players_lbl = stepper(p, "JOGADORES", &kit_display_44, players_cb, -1, 1);

    /* NOMES: seletor de jogador + 3 caixas de letra (toca = próxima letra) */
    s_name_lbl = stepper(p, "NOMES (OPCIONAL)", &kit_mono_20, name_sel_cb, -1, 1);
    lv_obj_t *sec = pane(p);
    lv_obj_set_size(sec, lv_pct(100), 100);
    flex(sec, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 12);
    for (int k = 0; k < 3; k++) {
        lv_obj_t *box = tap(rect(sec, 92, 96, KIT_COLOR_SURFACE, 16), slot_cb, k);
        s_slot_lbl[k] = lbl(box, "-", KIT_COLOR_TEXT, &kit_display_72, 0, 0);
        lv_obj_center(s_slot_lbl[k]);
    }
    s_name_clear = tap(rect(p, lv_pct(100), F_STEP, KIT_COLOR_SURFACE, 15), name_clear_cb, 0);
    lv_obj_center(lbl(s_name_clear, "APAGAR NOME", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2, 0));

    s_rounds_lbl = stepper(p, "RODADAS", &kit_display_44, round_cb, -1, 1);

    /* ASSUNTOS — seletor. NB: FORA_CATEGORIES[c] com c >= 0, nunca [c-1]. */
    s_cat_lbl = stepper(p, "ASSUNTOS", &kit_mono_26, cat_cb, -1, 1);
}

static void build_palco(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *box = pane(tile);
    lv_obj_set_size(box, lv_pct(100), lv_pct(100));

    s_stage_col = pane(box);
    lv_obj_set_size(s_stage_col, F_SCR_W, F_STAGE_H);
    lv_obj_set_pos(s_stage_col, 0, 0);
    lv_obj_set_style_pad_left(s_stage_col, F_PAD, 0);
    lv_obj_set_style_pad_right(s_stage_col, F_PAD, 0);
    lv_obj_add_flag(s_stage_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_stage_col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_stage_col, LV_SCROLLBAR_MODE_AUTO);
    flex(s_stage_col, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 14, 0);

    s_meta = lbl(s_stage_col, "", KIT_COLOR_TEXT, &kit_mono_26, 2, F_CONTENT);
    s_big  = lbl(s_stage_col, "", KIT_COLOR_TEXT, &kit_mono_26, 2, F_CONTENT);

    /* QUESTION: [ quem pergunta ] ▶ [ quem responde ] — os dois grandes */
    s_qrow = pane(s_stage_col);
    lv_obj_set_size(s_qrow, F_CONTENT, LV_SIZE_CONTENT);
    flex(s_qrow, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 10);
    s_qfrom = lbl(s_qrow, "", F_ACCENT, &kit_display_72, 0, 0);
    lbl(s_qrow, KIT_ICON_CHEVRON, KIT_COLOR_TEXT_MUTED, &kit_display_44, 0, 0);
    s_qto = lbl(s_qrow, "", KIT_COLOR_TEXT, &kit_display_72, 0, 0);
    show(s_qrow, false);

    s_sub  = lbl(s_stage_col, "", KIT_COLOR_TEXT, &kit_mono_20, 2, F_CONTENT);
    s_detail = lbl(s_stage_col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2, F_CONTENT);
    show(s_detail, false);

    /* seletor ◄ valor ► — votação (sigla/número grande) e chute final (palavra) */
    s_sel_box = pane(s_stage_col);
    lv_obj_set_size(s_sel_box, F_CONTENT, 72);
    flex(s_sel_box, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 8);
    lv_obj_t *b_prev = tap(rect(s_sel_box, 52, 72, KIT_COLOR_SURFACE, 16), sel_cb, -1);
    lv_obj_center(lbl(b_prev, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0, 0));
    s_sel_lbl = lbl(s_sel_box, "", KIT_COLOR_TEXT, &kit_display_72, 0, 0);
    lv_obj_set_flex_grow(s_sel_lbl, 1);
    lv_obj_set_style_text_align(s_sel_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *b_next = tap(rect(s_sel_box, 52, 72, KIT_COLOR_SURFACE, 16), sel_cb, 1);
    lv_obj_center(lbl(b_next, KIT_ICON_CHEVRON, KIT_COLOR_TEXT, &kit_display_44, 0, 0));
    show(s_sel_box, false);

    s_primary = tap(rect(box, F_CONTENT, F_BTN_H, F_ACCENT, F_BTN_H / 2), primary_cb, 0);
    lv_obj_align(s_primary, LV_ALIGN_BOTTOM_MID, 0, -F_BTN_MARGIN);
    s_primary_lbl = lbl(s_primary, "COME\xC3\x87" "AR", KIT_COLOR_ON_COLOR, &kit_mono_26, 3, 0);
    lv_obj_center(s_primary_lbl);
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, F_SCR_W, F_PAGE_H);
    lv_obj_set_pos(s_tv, 0, F_TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);
    build_adjust(s_tiles[0]);
    build_palco(s_tiles[1]);
    build_rules(s_tiles[2]);
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

    memset(&s_game, 0, sizeof s_game);
    s_game.num_players = 5;
    s_game.category_index = FORA_MIX_INDEX;
    s_game.num_rounds = 2;
    fora_game_reset(&s_game);
    load_config();
    s_game.phase = FORA_PHASE_CONFIG;
    s_name_sel = 0;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
    lv_obj_update_layout(s_screen);

    sync_ajustes();
    render_phase();

    lv_screen_load(s_screen);
    return KIT_OK;
}

void tool_destroy(void)
{
    kill_timer();
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }
    s_tv = NULL;
    s_api = NULL;
}

#endif
