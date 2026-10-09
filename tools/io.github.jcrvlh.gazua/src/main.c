/**
 * @file main.c
 * @brief GAZUA — jogo de dedução solo: o código, os verificadores e as regras secretas.
 *
 * Um código de 3 números (triângulo, quadrado, círculo; 1 a 5) e 4
 * verificadores, cada um com uma regra secreta. Monte um código, teste até 3
 * verificadores por rodada (SIM/NÃO) e arrisque quando souber. A lógica pura
 * (cartas, gerador, deduções) mora em gazua_game.c.
 *
 * Telas:
 *  - shell padrão do KIT: AJUSTE ◄ JOGO ► COMO JOGA (abre no JOGO);
 *  - por cima, um overlay de tela cheia com titlebar própria (‹ fecha):
 *    VERIFICADOR, NOTAS, ARRISCAR e RESULTADO;
 *  - o carimbo SIM/NÃO depois de cada teste e a abertura animada do
 *    resultado (ABERTO / TRAVOU) são camadas por cima de tudo.
 *
 * Decisões que não são óbvias:
 *  - Nada é apagado dentro do callback do objeto tocado (trava a placa, ver
 *    Fora). O que muda de estrutura (cartas de um puzzle novo, o conteúdo de
 *    um overlay) é reconstruído num lv_timer de um disparo (`defer`); o resto
 *    é atualizado no lugar.
 *  - Pisos de acessibilidade do protótipo: texto de jogo em 26–28 px, alvos
 *    de toque de 64 px ou mais. Rótulos opcionais em kit_mono_16/20.
 *  - A partida fica salva (semente + rodadas): o ‹ do shell sai direto, e ao
 *    abrir de novo o jogo volta de onde parou.
 *  - Mudar REGRAS/PUZZLE no meio de uma partida só vale no próximo puzzle.
 *  - ARRISCAR: a própria tela é a confirmação. O botão só acende 0,6 s depois
 *    de a tela abrir, então um toque duplo não arrisca sem querer.
 *  - Sem lv_anim na tabela de símbolos: as animações andam num lv_timer pelo
 *    relógio (time->get_millis).
 */
#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "gazua_game.h"
#include "shapes.h"

#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------- estado puro */

#define MAX_ROUNDS   40
#define TESTS_PER_ROUND 3

typedef struct {
    uint8_t code[GZ_SHAPES];
    uint8_t res[GZ_VERIFIERS];   /* 0 = não testado, 1 = SIM, 2 = NÃO */
} round_t;

static const kit_api_table_t *s_api;

static gz_puzzle_t s_pz;
static uint32_t s_seed;
static uint8_t  s_diff;            /* 0 = fácil, 1 = difícil (deste puzzle) */
static uint8_t  s_mode;            /* 0 = do dia, 1 = livre (deste puzzle) */
static int32_t  s_day;             /* número do dia, se do dia */
static round_t  s_rounds[MAX_ROUNDS];
static int      s_nr;              /* rodadas abertas (a última é a atual) */
static uint8_t  s_strike[GZ_VERIFIERS];   /* regras riscadas à mão (bits) */
static uint8_t  s_grid[GZ_SHAPES][GZ_DIGITS];   /* 0 nada, 1 riscado, 2 certo */
static bool     s_over;

/* ajustes (valem pro PRÓXIMO puzzle) e histórico */
static uint8_t  s_next_diff, s_next_mode, s_assist;
static uint8_t  s_paper;           /* 1 = modo COM FOLHA (deste puzzle) */
static uint8_t  s_paper_pref;      /* a última escolha, pré-selecionada no próximo */
static int32_t  s_played, s_won, s_best_r, s_best_t, s_daily_done;
static bool     s_dirty;

#ifndef KIT_SDK_STUBS

#define T_ACCENT   KIT_COLOR_YELLOW

/* folha web (jcrvlh/kit, web-installer/gazua.html). A URL leva só as cartas
 * (índices em GZ_CARDS — a ordem da tabela é contrato com a página), o dia e
 * a dificuldade; nunca a regra secreta nem o código. */
#define FOLHA_URL  "https://jcrvlh.github.io/kit/gazua.html?v="

/* alvos de toque: KIT_TOUCH_TARGET_COMFORTABLE (80 px) em tudo que se toca no jogo */
#define TOUCH_H       KIT_TOUCH_TARGET_COMFORTABLE
#define DISC_CODE_H   124
#define DISC_GUESS_H  112

static uint8_t s_guess[GZ_SHAPES];
static bool    s_record, s_win;
#define C_DARK_ON_GREEN 0x0B2412

static const uint32_t SHAPE_COLOR[GZ_SHAPES] = { KIT_COLOR_YELLOW, KIT_COLOR_RED, KIT_COLOR_BLUE };

static const char RULES[] =
    "O CÓDIGO\n"
    "Três números de 1 a 5, um em cada forma: triângulo, quadrado e círculo.\n\n"
    "OS VERIFICADORES\n"
    "Cada um tem UMA regra secreta, tirada da lista dele. Ele só responde SIM ou NÃO.\n\n"
    "A RODADA\n"
    "Na página CÓDIGO, toque nos discos pra montar um código. Deslize pra TESTES e teste até 3 verificadores. "
    "O código trava no 1º teste.\n\n"
    "ARRISCAR\n"
    "Um palpite só: errou, acabou. Menos rodadas vence; no empate, menos testes.\n\n"
    "A PISTA\n"
    "Nenhum verificador sobra e a solução é uma só. Se uma regra parece não servir pra nada, ela não é a regra certa.\n\n"
    "NOTAS\n"
    "A tabela de todos os testes e uma grade pra riscar números.\n\n"
    "COM FOLHA\n"
    "Ao começar um puzzle, escolha SÓ KIT ou COM FOLHA. Com folha, aponte a câmera pro QR: dá pra imprimir "
    "ou anotar no celular, e o KIT só testa e recebe o palpite. O botão FOLHA reabre o QR. "
    "Pra trocar no meio da partida: AJUSTE > MODO.\n\n"
    "AJUDA\n"
    "No AJUSTE. Risca sozinho as regras que algum teste já contradiz e confere o palpite contra os seus testes. "
    "Não olha a resposta.\n\n"
    "SAIR\n"
    "A seta no topo sai do jogo. A partida fica salva.";

static const char *const DIFF_LABELS[] = { "FÁCIL", "DIFÍCIL" };
static const char *const MODE_LABELS[] = { "DO DIA", "LIVRE" };
static const char *const ASSIST_LABELS[] = { "NÃO", "SIM" };
static const char *const PAPER_LABELS[] = { "SÓ KIT", "COM FOLHA" };

/* ------------------------------------------------------------- widgets */

static lv_obj_t *s_screen;
static kit_ui_shell_t s_shell;
static kit_ui_chips_t s_diff_chips, s_mode_chips, s_assist_chips, s_paper_chips;

/* JOGO */
static lv_obj_t *s_st_round, *s_st_tests;
static lv_obj_t *s_disc[GZ_SHAPES], *s_disc_lbl[GZ_SHAPES];
static lv_obj_t *s_cards_box;
static lv_obj_t *s_card_hole[GZ_VERIFIERS], *s_card_hole_lbl[GZ_VERIFIERS];
static lv_obj_t *s_btn_round, *s_btn_notes_lbl;

/* AJUSTE */
static lv_obj_t *s_pend, *s_pend_lbl;
static lv_obj_t *s_hist_val[3];

/* overlay */
enum { OV_NONE = 0, OV_VER, OV_NOTES, OV_GUESS, OV_RESULT, OV_MODE, OV_QR };
static kit_ui_qr_t s_qr;
static lv_obj_t *s_ov, *s_ov_title, *s_ov_body;
static int s_ov_kind;
static int s_ver;                  /* verificador aberto */
static int s_notes_tab;            /* 0 testes, 1 números */
static int s_num_shape;

/* VERIFICADOR: linhas de regra (pra riscar no lugar) */
static lv_obj_t *s_opt_row[GZ_MAX_OPTS], *s_opt_tok[GZ_MAX_OPTS], *s_opt_tag[GZ_MAX_OPTS];

/* NOTAS > NÚMEROS */
static lv_obj_t *s_num_tab[GZ_SHAPES];
static lv_obj_t *s_num_cell[GZ_DIGITS], *s_num_cell_lbl[GZ_DIGITS], *s_num_cell_bar[GZ_DIGITS];

/* ARRISCAR */
static lv_obj_t *s_gdisc[GZ_SHAPES], *s_gdisc_lbl[GZ_SHAPES];
static lv_obj_t *s_gcheck, *s_gconfirm;
static bool s_guess_ready;
static lv_timer_t *s_guard_timer;

/* carimbo SIM/NÃO */
static lv_obj_t *s_stamp, *s_stamp_pill, *s_stamp_icon, *s_stamp_word;

/* abertura do resultado */
static lv_obj_t *s_intro, *s_flood, *s_lock, *s_shackle, *s_lock_body, *s_key_c, *s_key_s;
static lv_obj_t *s_intro_word, *s_intro_sub;
static lv_timer_t *s_anim_timer;
static uint32_t s_anim_t0;
static int s_anim_frames;
static int s_anim_note;

/* reconstrução adiada */
enum { DEF_CARDS = 1, DEF_OV = 2 };
static lv_timer_t *s_defer_timer;
static int s_defer_bits;

static lv_timer_t *s_save_timer;

#endif /* !KIT_SDK_STUBS */

/* ======================================================== estado / regras */

static int used(void)
{
    int n = 0;
    for (int v = 0; v < GZ_VERIFIERS; v++) n += s_rounds[s_nr - 1].res[v] != 0;
    return n;
}

static int tests_total(void)
{
    int n = 0;
    for (int r = 0; r < s_nr; r++)
        for (int v = 0; v < GZ_VERIFIERS; v++) n += s_rounds[r].res[v] != 0;
    return n;
}

static int rounds_played(void)
{
    int n = 0;
    for (int r = 0; r < s_nr; r++) {
        bool any = false;
        for (int v = 0; v < GZ_VERIFIERS; v++) any |= s_rounds[r].res[v] != 0;
        n += any;
    }
    return n;
}

static bool in_progress(void) { return tests_total() > 0 && !s_over; }

static int collect_obs(gz_obs_t *obs)
{
    int n = 0;
    for (int r = 0; r < s_nr; r++)
        for (int v = 0; v < GZ_VERIFIERS; v++)
            if (s_rounds[r].res[v]) {
                memcpy(obs[n].code, s_rounds[r].code, GZ_SHAPES);
                obs[n].v = (uint8_t)v;
                obs[n].yes = s_rounds[r].res[v] == 1;
                n++;
            }
    return n;
}

static bool opt_contradicted(int v, int o)
{
    static gz_obs_t obs[MAX_ROUNDS * GZ_VERIFIERS];
    int n = collect_obs(obs);
    return gz_contradicted(&s_pz, v, o, obs, n);
}

static bool opt_struck(int v, int o)
{
    if (s_paper) return false;
    return ((s_strike[v] >> o) & 1) || (s_assist && opt_contradicted(v, o));
}

/* ======================================================== persistência */

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

static int32_t pack_grid(void)
{
    uint32_t g = 0;
    for (int i = 0; i < GZ_SHAPES; i++)
        for (int d = 0; d < GZ_DIGITS; d++) g |= (uint32_t)(s_grid[i][d] & 3) << (2 * (i * GZ_DIGITS + d));
    return (int32_t)g;
}

static void unpack_grid(int32_t v)
{
    uint32_t g = (uint32_t)v;
    for (int i = 0; i < GZ_SHAPES; i++)
        for (int d = 0; d < GZ_DIGITS; d++) {
            uint8_t c = (uint8_t)((g >> (2 * (i * GZ_DIGITS + d))) & 3);
            s_grid[i][d] = c > 2 ? 0 : c;
        }
}

/* "2;semente;dif;modo;dia;fim;folha;" + 7 dígitos por rodada (código + 4
 * resultados). O formato 1 (sem o campo folha) ainda é lido. */
static void save_game(void)
{
    if (!s_api || !s_api->storage) return;
    char buf[400];
    int n = snprintf(buf, sizeof buf, "2;%u;%d;%d;%d;%d;%d;", (unsigned)s_seed, s_diff, s_mode,
                     (int)s_day, s_over ? 1 : 0, s_paper ? 1 : 0);
    for (int r = 0; r < s_nr && n + 8 < (int)sizeof buf; r++) {
        round_t *R = &s_rounds[r];
        buf[n++] = (char)('0' + R->code[0]);
        buf[n++] = (char)('0' + R->code[1]);
        buf[n++] = (char)('0' + R->code[2]);
        for (int v = 0; v < GZ_VERIFIERS; v++) buf[n++] = (char)('0' + R->res[v]);
    }
    buf[n] = '\0';
    s_api->storage->set_str("gz_game", buf);
    uint32_t st = 0;
    for (int v = 0; v < GZ_VERIFIERS; v++) st |= (uint32_t)(s_strike[v] & 15) << (4 * v);
    set_i32("gz_strike", (int32_t)st);
    set_i32("gz_grid", pack_grid());
    s_dirty = false;
}

/* número sem sinal até ';' — o loader não tem sscanf/strtol */
static bool parse_num(const char **p, int32_t *out)
{
    const char *s = *p;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    if (*s < '0' || *s > '9') return false;
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10u + (uint32_t)(*s - '0'); s++; }
    if (*s != ';') return false;
    *p = s + 1;
    *out = neg ? -(int32_t)v : (int32_t)v;
    return true;
}

/* devolve true se havia uma partida em andamento e ela foi restaurada */
static bool load_game(void)
{
    if (!s_api || !s_api->storage) return false;
    char buf[400];
    buf[0] = '\0';
    if (s_api->storage->get_str("gz_game", buf, sizeof buf) != KIT_OK ||
        (buf[0] != '1' && buf[0] != '2') || buf[1] != ';')
        return false;
    const char *p = buf + 2;
    int32_t seed, diff, mode, day, over, paper = 0;
    if (!parse_num(&p, &seed) || !parse_num(&p, &diff) || !parse_num(&p, &mode) ||
        !parse_num(&p, &day) || !parse_num(&p, &over)) return false;
    if (buf[0] == '2' && !parse_num(&p, &paper)) return false;
    if (over || diff < 0 || diff > 1 || mode < 0 || mode > 1) return false;

    int nr = 0;
    while (p[0] && nr < MAX_ROUNDS) {
        for (int k = 0; k < 7; k++) if (!p[k]) return false;
        round_t *R = &s_rounds[nr];
        for (int i = 0; i < GZ_SHAPES; i++) {
            int d = p[i] - '0';
            if (d < 1 || d > 5) return false;
            R->code[i] = (uint8_t)d;
        }
        for (int v = 0; v < GZ_VERIFIERS; v++) {
            int x = p[3 + v] - '0';
            if (x < 0 || x > 2) return false;
            R->res[v] = (uint8_t)x;
        }
        nr++;
        p += 7;
    }
    if (nr == 0) return false;
    if (!gz_generate(&s_pz, diff == 0, (uint32_t)seed)) return false;

    s_seed = (uint32_t)seed;
    s_diff = (uint8_t)diff;
    s_mode = (uint8_t)mode;
    s_day = day;
    s_nr = nr;
    s_over = false;
    s_paper = paper == 1;
    uint32_t st = (uint32_t)get_i32("gz_strike", 0);
    for (int v = 0; v < GZ_VERIFIERS; v++) s_strike[v] = (uint8_t)((st >> (4 * v)) & 15);
    unpack_grid(get_i32("gz_grid", 0));
    return true;
}

static void load_settings(void)
{
    s_next_diff = get_i32("gz_diff", 0) == 1 ? 1 : 0;
    s_next_mode = get_i32("gz_mode", 0) == 1 ? 1 : 0;
    s_assist    = get_i32("gz_assist", 0) == 1 ? 1 : 0;
    s_played = get_i32("gz_played", 0);
    s_won    = get_i32("gz_won", 0);
    s_best_r = get_i32("gz_bestr", 0);
    s_best_t = get_i32("gz_bestt", 0);
    s_daily_done = get_i32("gz_dday", 0);
    s_paper_pref = get_i32("gz_paper", 0) == 1 ? 1 : 0;
}

/* hoje, como número do dia (0 se o relógio não sabe) */
static int32_t today(int *y, int *m, int *d)
{
    kit_datetime_t dt;
    if (!s_api || !s_api->time || s_api->time->get_datetime(&dt) != KIT_OK || dt.year < 2026) return 0;
    if (y) { *y = dt.year; *m = dt.month; *d = dt.day; }
    return gz_day_number(dt.year, dt.month, dt.day);
}

static void new_puzzle_state(void)
{
    int y = 0, m = 0, d = 0;
    s_diff = s_next_diff;
    s_mode = s_next_mode;
    s_day = 0;
    if (s_mode == 0) {
        int32_t t = today(&y, &m, &d);
        if (t <= 0 || t == s_daily_done) s_mode = 1;   /* sem relógio, ou o do dia já foi */
        else s_day = t;
    }
    if (s_mode == 0)
        s_seed = (uint32_t)((y * 10000 + m * 100 + d) * 10 + s_diff);
    else
        s_seed = (s_api && s_api->random) ? s_api->random->u32() : 0x12345678u;
    gz_generate(&s_pz, s_diff == 0, s_seed);

    memset(s_rounds, 0, sizeof s_rounds);
    s_nr = 1;
    s_rounds[0].code[0] = s_rounds[0].code[1] = s_rounds[0].code[2] = 1;
    memset(s_strike, 0, sizeof s_strike);
    memset(s_grid, 0, sizeof s_grid);
    s_over = false;
    s_paper = s_paper_pref;
    s_dirty = true;
}

/* ===================================================================== */
#ifndef KIT_SDK_STUBS

/* --------------------------------------------------------------- util */

static void mark_dirty(void) { s_dirty = true; }

static void save_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_dirty) save_game();
}

/* tira CLICKABLE de um decorativo: o toque passa pro alvo de baixo */
static lv_obj_t *deco(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *box(lv_obj_t *parent) { return deco(kit_ui_box(parent)); }

static lv_obj_t *shape_img(lv_obj_t *parent, const lv_image_dsc_t *const *set, int i, uint32_t color)
{
    lv_obj_t *im = lv_image_create(parent);
    lv_image_set_src(im, set[i]);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(im, lv_color_hex(color), 0);
    return deco(im);
}

/* Texto com marcas "#0/#1/#2" virando as formas. Cada palavra é um item de
 * um flex que quebra linha — a capa longa vira duas linhas. */
static lv_obj_t *tokens(lv_obj_t *parent, const char *s, const lv_font_t *font,
                        uint32_t color, int width)
{
    lv_obj_t *row = box(parent);
    lv_obj_set_flex_flow(row, width ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    if (width) lv_obj_set_size(row, width, LV_SIZE_CONTENT);
    else       lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    char word[32];
    int wn = 0;
    for (const char *c = s;; c++) {
        bool end = (*c == '\0' || *c == ' ');
        bool shape = (*c == GZ_SHAPE_MARK && c[1] >= '0' && c[1] <= '2');
        if ((end || shape) && wn) {
            word[wn] = '\0';
            kit_ui_label(row, word, color, font, 0);
            wn = 0;
        }
        if (shape) { int i = c[1] - '0'; shape_img(row, SHAPE_IMG_SM, i, SHAPE_COLOR[i]); c++; continue; }
        if (*c == '\0') break;
        if (*c != ' ' && wn < (int)sizeof word - 1) word[wn++] = *c;
    }
    return row;
}

static void tokens_color(lv_obj_t *row, uint32_t color)
{
    for (int i = 0;; i++) {
        lv_obj_t *ch = lv_obj_get_child(row, i);
        if (!ch) break;
        lv_obj_set_style_text_color(ch, lv_color_hex(color), 0);
    }
}

/* botão retangular: superfície/cor cheia, alvo de toque com feedback */
static lv_obj_t *button(lv_obj_t *parent, int w, int h, uint32_t bg, lv_event_cb_t cb, int code)
{
    lv_obj_t *b = kit_ui_rect(parent, w, h, bg, 16);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    kit_ui_tap(b, cb, code);
    return b;
}

static void soft(uint16_t hz) { kit_ui_beep(hz, 12); }

/* cadeado desenhado com retângulos (arco = borda sem o lado de baixo) */
static lv_obj_t *mini_lock(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *l = box(parent);
    lv_obj_set_size(l, 24, 32);
    lv_obj_t *sh = box(l);              /* arco alto: a curva de baixo some atrás do corpo */
    lv_obj_set_size(sh, 16, 24);
    lv_obj_set_pos(sh, 4, 0);           /* subido 4 px: aberto */
    lv_obj_set_style_border_width(sh, 4, 0);
    lv_obj_set_style_border_color(sh, lv_color_hex(color), 0);
    lv_obj_set_style_border_side(sh, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(sh, 8, 0);
    lv_obj_t *b = deco(kit_ui_rect(l, 24, 16, color, 3));
    lv_obj_set_pos(b, 0, 16);
    return l;
}

/* SIM / NÃO num rótulo de furo */
static void set_mark(lv_obj_t *hole, lv_obj_t *lbl, int res)
{
    lv_obj_set_style_bg_color(hole, lv_color_hex(res == 1 ? KIT_COLOR_GREEN :
                                                 res == 2 ? KIT_COLOR_RED : KIT_COLOR_BG), 0);
    lv_label_set_text(lbl, res == 1 ? KIT_ICON_CHECK : res == 2 ? "X" : "");
}

/* ----------------------------------------------------- reconstrução adiada */

static void build_cards(void);
static void build_overlay(void);

static void defer_cb(lv_timer_t *t)
{
    (void)t;
    s_defer_timer = NULL;      /* repeat_count 1: o LVGL apaga o timer */
    int bits = s_defer_bits;
    s_defer_bits = 0;
    if (bits & DEF_CARDS) build_cards();
    if (bits & DEF_OV) build_overlay();
}

static void defer(int bits)
{
    s_defer_bits |= bits;
    if (!s_defer_timer) {
        s_defer_timer = lv_timer_create(defer_cb, 1, NULL);
        lv_timer_set_repeat_count(s_defer_timer, 1);
    }
}

/* ------------------------------------------------------------ discos */

static void paint_disc(lv_obj_t *disc, lv_obj_t *lbl, int i, int v, bool locked)
{
    lv_label_set_text_fmt(lbl, "%d", v);
    lv_obj_set_style_bg_opa(disc, locked ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, locked ? 3 : 0, 0);
    (void)i;
}

static lv_obj_t *make_disc(lv_obj_t *parent, int i, int h, lv_event_cb_t cb, lv_obj_t **lbl_out)
{
    lv_obj_t *d = button(parent, 104, h, KIT_COLOR_SURFACE, cb, i);
    lv_obj_set_style_radius(d, 18, 0);
    lv_obj_set_style_border_color(d, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_t *im = shape_img(d, SHAPE_IMG_LG, i, SHAPE_COLOR[i]);
    lv_obj_center(im);
    lv_obj_t *l = kit_ui_label(d, "1", i == 0 ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR, &kit_display_44, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, i == 0 ? 10 : 0);   /* triângulo: centro de massa mais baixo */
    *lbl_out = l;
    return d;
}

/* forma média com o dígito dentro (faixa do código, comparação) */
static lv_obj_t *code_shape(lv_obj_t *parent, int i, int v, const lv_image_dsc_t *const *set, const lv_font_t *font)
{
    lv_obj_t *w = box(parent);
    int sz = (set == SHAPE_IMG_LG) ? SHAPE_LG : SHAPE_MD;
    lv_obj_set_size(w, sz, sz);
    lv_obj_center(shape_img(w, set, i, SHAPE_COLOR[i]));
    char t[4];
    snprintf(t, sizeof t, "%d", v);
    lv_obj_t *l = kit_ui_label(w, t, i == 0 ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR, font, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, i == 0 ? sz / 8 : 0);
    return w;
}

/* ------------------------------------------------------------- JOGO */

static void refresh_jogo(void)
{
    if (!s_st_round) return;
    int u = used();
    lv_label_set_text_fmt(s_st_round, "RODADA %d", s_nr);
    lv_label_set_text_fmt(s_st_tests, "· %d/3", u);
    for (int i = 0; i < GZ_SHAPES; i++)
        paint_disc(s_disc[i], s_disc_lbl[i], i, s_rounds[s_nr - 1].code[i], u > 0);
    for (int v = 0; v < GZ_VERIFIERS; v++)
        if (s_card_hole[v]) set_mark(s_card_hole[v], s_card_hole_lbl[v], s_rounds[s_nr - 1].res[v]);
    lv_obj_set_style_opa(s_btn_round, (u > 0 && s_nr < MAX_ROUNDS) ? LV_OPA_COVER : LV_OPA_30, 0);
    if (s_btn_notes_lbl) lv_label_set_text(s_btn_notes_lbl, s_paper ? "FOLHA" : "NOTAS");
}

static void refresh_ajuste(void);

static void disc_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_over) return;
    if (used() > 0) { kit_ui_miss(); return; }   /* travado nesta rodada */
    uint8_t *c = &s_rounds[s_nr - 1].code[i];
    *c = (uint8_t)(*c % 5 + 1);
    kit_ui_click();
    mark_dirty();
    refresh_jogo();
}

static void open_ov(int kind);

static void card_cb(lv_event_t *e)
{
    s_ver = (int)(intptr_t)lv_event_get_user_data(e);
    kit_ui_click();
    open_ov(OV_VER);
}

static void notes_cb(lv_event_t *e) { (void)e; kit_ui_click(); open_ov(s_paper ? OV_QR : OV_NOTES); }

static void round_cb(lv_event_t *e)
{
    (void)e;
    if (s_over || used() == 0 || s_nr >= MAX_ROUNDS) { kit_ui_miss(); return; }
    memcpy(s_rounds[s_nr].code, s_rounds[s_nr - 1].code, GZ_SHAPES);
    memset(s_rounds[s_nr].res, 0, GZ_VERIFIERS);
    s_nr++;
    kit_ui_confirm();
    mark_dirty();
    refresh_jogo();
}

static void guess_open_cb(lv_event_t *e)
{
    (void)e;
    if (s_over) return;
    /* abre com o que foi marcado CERTO nas notas; o resto vem do código da rodada */
    for (int i = 0; i < GZ_SHAPES; i++) {
        s_guess[i] = s_rounds[s_nr - 1].code[i];
        for (int d = 0; d < GZ_DIGITS; d++) if (s_grid[i][d] == 2) s_guess[i] = (uint8_t)(d + 1);
    }
    kit_ui_click();
    open_ov(OV_GUESS);
}

/* página TESTES: os 4 verificadores em linhas de 336 × 80 */
static void build_cards(void)
{
    lv_obj_clean(s_cards_box);
    char buf[48];
    for (int v = 0; v < GZ_VERIFIERS; v++) {
        lv_obj_t *c = button(s_cards_box, KIT_UI_CONTENT, TOUCH_H, KIT_COLOR_SURFACE, card_cb, v);
        lv_obj_set_style_radius(c, 18, 0);
        lv_obj_set_pos(c, 0, v * (TOUCH_H + 8));
        gz_face(s_pz.card[v], buf, sizeof buf);
        lv_obj_t *t = tokens(c, buf, &kit_sans_28, KIT_COLOR_TEXT, 0);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_t *h = deco(kit_ui_rect(c, 40, 40, KIT_COLOR_BG, LV_RADIUS_CIRCLE));
        lv_obj_align(h, LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_t *hl = kit_ui_label(h, "", KIT_COLOR_ON_COLOR, &kit_mono_26, 0);
        lv_obj_center(hl);
        s_card_hole[v] = h;
        s_card_hole_lbl[v] = hl;
    }
    refresh_jogo();
}

/* rótulo centrado num botão */
static lv_obj_t *btn_label(lv_obj_t *b, const char *t, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *l = kit_ui_label(b, t, color, font, 1);
    lv_obj_center(l);
    return l;
}

/* página CÓDIGO: o palpite da rodada, com alvos grandes */
static void build_codigo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *st = box(tile);
    lv_obj_set_size(st, KIT_UI_CONTENT, 26);
    lv_obj_set_pos(st, KIT_UI_PAD, 4);
    kit_ui_flex(st, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_START, 0, 8);
    s_st_round = kit_ui_label(st, "", KIT_COLOR_TEXT, &kit_mono_20, 1);
    s_st_tests = kit_ui_label(st, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    lv_obj_t *hint = kit_ui_label(tile, "TESTES " KIT_ICON_CHEVRON, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -KIT_UI_PAD, 8);

    for (int i = 0; i < GZ_SHAPES; i++) {
        s_disc[i] = make_disc(tile, i, DISC_CODE_H, disc_cb, &s_disc_lbl[i]);
        lv_obj_set_pos(s_disc[i], KIT_UI_PAD + i * 116, 40);
    }

    int half = (KIT_UI_CONTENT - 12) / 2;   /* 162 */
    lv_obj_t *nb = button(tile, half, TOUCH_H, KIT_COLOR_SURFACE, notes_cb, 0);
    lv_obj_set_pos(nb, KIT_UI_PAD, 176);
    s_btn_notes_lbl = btn_label(nb, "NOTAS", KIT_COLOR_TEXT, &kit_mono_20);

    s_btn_round = button(tile, half, TOUCH_H, KIT_COLOR_SURFACE, round_cb, 0);
    lv_obj_set_pos(s_btn_round, KIT_UI_PAD + half + 12, 176);
    btn_label(s_btn_round, "NOVA RODADA", KIT_COLOR_TEXT, &kit_mono_20);

    lv_obj_t *gb = button(tile, KIT_UI_CONTENT, TOUCH_H, T_ACCENT, guess_open_cb, 0);
    lv_obj_set_pos(gb, KIT_UI_PAD, 268);
    lv_obj_t *row = box(gb);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 12);
    mini_lock(row, KIT_COLOR_ON_YELLOW);
    kit_ui_label(row, "ARRISCAR", KIT_COLOR_ON_YELLOW, &kit_mono_26, 1);
    lv_obj_center(row);
}

static void build_testes(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    s_cards_box = box(tile);
    lv_obj_set_size(s_cards_box, KIT_UI_CONTENT, 4 * TOUCH_H + 3 * 8);
    lv_obj_set_pos(s_cards_box, KIT_UI_PAD, 4);
}

/* ----------------------------------------------------------- AJUSTE */

static void start_new_puzzle(void)
{
    new_puzzle_state();
    save_game();
    defer(DEF_CARDS);
    refresh_ajuste();
}

static void diff_cb(int idx, void *u)
{
    (void)u;
    s_next_diff = (uint8_t)idx;
    set_i32("gz_diff", idx);
    if (!in_progress()) start_new_puzzle(); else refresh_ajuste();
}

static void mode_cb(int idx, void *u)
{
    (void)u;
    s_next_mode = (uint8_t)idx;
    set_i32("gz_mode", idx);
    if (!in_progress()) start_new_puzzle(); else refresh_ajuste();
}

/* o modo só muda a interface (NOTAS/FOLHA, regras riscáveis, Ajuda): vale na
 * hora, inclusive no meio da partida. Testes e notas do KIT ficam como estão. */
static void paper_cb(int idx, void *u)
{
    (void)u;
    s_paper = s_paper_pref = (uint8_t)idx;
    set_i32("gz_paper", idx);
    save_game();
    refresh_jogo();
    refresh_ajuste();
    if (s_paper) open_ov(OV_QR);   /* quem passa pra folha precisa do QR */
}

static void assist_cb(int idx, void *u)
{
    (void)u;
    s_assist = (uint8_t)idx;
    set_i32("gz_assist", idx);
}

/* DO DIA só existe se o relógio sabe a data e o puzzle de hoje ainda não foi jogado */
static bool daily_available(void)
{
    int32_t t = today(NULL, NULL, NULL);
    return t > 0 && t != s_daily_done;
}

/* sem puzzle do dia: o chip DO DIA apaga e não aceita toque, e o LIVRE aparece
 * marcado. A preferência salva (s_next_mode) não muda: amanhã o DO DIA volta. */
static void paint_mode_chips(void)
{
    if (!s_mode_chips.chip[0]) return;
    bool avail = daily_available();
    lv_obj_t *d = s_mode_chips.chip[0];
    if (avail) {
        lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(d, LV_OPA_COVER, 0);
    } else {
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(d, LV_OPA_30, 0);
    }
    kit_ui_chips_select(&s_mode_chips, avail ? s_next_mode : 1);
}

static void refresh_ajuste(void)
{
    if (!s_pend) return;
    paint_mode_chips();
    kit_ui_chips_select(&s_paper_chips, s_paper);
    bool show = true;
    if (in_progress()) {
        bool changed = s_next_diff != s_diff || s_next_mode != s_mode;
        lv_label_set_text(s_pend_lbl, changed ? "MUDANÇA SALVA. VALE NO PRÓXIMO PUZZLE."
                                              : "PARTIDA EM ANDAMENTO. O MODO MUDA NA HORA; REGRAS E PUZZLE, NO PRÓXIMO.");
        uint32_t c = changed ? T_ACCENT : KIT_COLOR_TEXT;
        lv_obj_set_style_text_color(s_pend_lbl, lv_color_hex(c), 0);
        lv_obj_set_style_border_color(s_pend, lv_color_hex(changed ? T_ACCENT : KIT_COLOR_LINE), 0);
    } else if (s_next_mode == 0 && today(NULL, NULL, NULL) > 0 && today(NULL, NULL, NULL) == s_daily_done) {
        lv_label_set_text(s_pend_lbl, "PUZZLE DO DIA RESOLVIDO. AMANHÃ TEM OUTRO; ATÉ LÁ, SÓ LIVRE.");
        lv_obj_set_style_text_color(s_pend_lbl, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_obj_set_style_border_color(s_pend, lv_color_hex(KIT_COLOR_LINE), 0);
    } else if (s_next_mode == 0 && today(NULL, NULL, NULL) <= 0) {
        lv_label_set_text(s_pend_lbl, "SEM DATA NO RELÓGIO: SÓ PUZZLE LIVRE.");
        lv_obj_set_style_text_color(s_pend_lbl, lv_color_hex(KIT_COLOR_TEXT), 0);
        lv_obj_set_style_border_color(s_pend, lv_color_hex(KIT_COLOR_LINE), 0);
    } else {
        show = false;
    }
    if (show) lv_obj_remove_flag(s_pend, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_pend, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text_fmt(s_hist_val[0], "%d", (int)s_played);
    lv_label_set_text_fmt(s_hist_val[1], "%d", (int)s_won);
    if (s_best_r > 0) lv_label_set_text_fmt(s_hist_val[2], "%dr %dt", (int)s_best_r, (int)s_best_t);
    else              lv_label_set_text(s_hist_val[2], "-");
}

/* rótulo de seção: kit_mono_20 na cor de texto — o cinza em 16 px não se lia na placa */
static void section(lv_obj_t *p, const char *t)
{
    lv_obj_t *l = kit_ui_label(p, t, KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_set_style_pad_top(l, 6, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
}

static void build_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_right(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_top(p, 4, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 12, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    s_pend = box(p);
    lv_obj_set_size(s_pend, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(s_pend, 2, 0);
    lv_obj_set_style_radius(s_pend, 14, 0);
    lv_obj_set_style_pad_all(s_pend, 12, 0);
    s_pend_lbl = kit_ui_label(s_pend, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 0);
    lv_label_set_long_mode(s_pend_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_pend_lbl, lv_pct(100));

    section(p, "MODO");
    kit_ui_chips(&s_paper_chips, p, PAPER_LABELS, 2, s_paper, T_ACCENT, paper_cb, NULL);
    section(p, "REGRAS");
    kit_ui_chips(&s_diff_chips, p, DIFF_LABELS, 2, s_next_diff, T_ACCENT, diff_cb, NULL);
    section(p, "PUZZLE");
    kit_ui_chips(&s_mode_chips, p, MODE_LABELS, 2, s_next_mode, T_ACCENT, mode_cb, NULL);
    section(p, "AJUDA");
    kit_ui_chips(&s_assist_chips, p, ASSIST_LABELS, 2, s_assist, T_ACCENT, assist_cb, NULL);

    section(p, "HISTÓRICO");
    lv_obj_t *card = kit_ui_rect(p, lv_pct(100), LV_SIZE_CONTENT, KIT_COLOR_SURFACE, 18);
    deco(card);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    static const char *const NAMES[3] = { "Jogados", "Abertos", "Melhor" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *r = box(card);
        lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
        kit_ui_flex(r, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 0);
        kit_ui_label(r, NAMES[i], KIT_COLOR_TEXT, &kit_sans_28, 0);
        s_hist_val[i] = kit_ui_label(r, "", KIT_COLOR_TEXT, &kit_mono_26, 0);
    }
}

/* ----------------------------------------------------------- overlay */

static void close_ov(void)
{
    s_ov_kind = OV_NONE;
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
    refresh_jogo();
}

static void ov_back_cb(lv_event_t *e)
{
    (void)e;
    kit_ui_sfx(KIT_SFX_BACK);
    if (s_ov_kind == OV_RESULT) { kit_ui_exit(); return; }
    if (s_ov_kind == OV_MODE && s_paper_pref) { s_paper = 1; save_game(); refresh_ajuste(); open_ov(OV_QR); return; }
    close_ov();
}

static void open_ov(int kind)
{
    s_ov_kind = kind;
    lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
    defer(DEF_OV);
}

static void ov_title_text(const char *t)
{
    lv_obj_t *l = kit_ui_label(s_ov_title, t, KIT_COLOR_TEXT, &kit_mono_26, 3);
    (void)l;
}

/* corpo em coluna: área que rola (cresce) + rodapé fixo opcional */
static lv_obj_t *body_scroll(void)
{
    lv_obj_t *sc = lv_obj_create(s_ov_body);
    lv_obj_remove_style_all(sc);
    lv_obj_set_width(sc, lv_pct(100));
    lv_obj_set_flex_grow(sc, 1);
    lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sc, 8, 0);
    lv_obj_set_style_pad_bottom(sc, 8, 0);
    lv_obj_add_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
    /* fica CLICKABLE: o LVGL só acha quem rola a partir de um alvo clicável */
    return sc;
}

/* .......................................................... VERIFICADOR */

static void paint_opt(int o)
{
    bool x = opt_struck(s_ver, o);
    bool manual = (s_strike[s_ver] >> o) & 1;
    lv_obj_set_style_bg_opa(s_opt_row[o], x ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_opt_row[o], x ? 2 : 0, 0);
    tokens_color(s_opt_tok[o], x ? KIT_COLOR_TEXT_MUTED : KIT_COLOR_TEXT);
    lv_label_set_text(s_opt_tag[o], !x ? "" : manual ? "RISCADA" : "AUTO");
}

static void opt_cb(lv_event_t *e)
{
    int o = (int)(intptr_t)lv_event_get_user_data(e);
    s_strike[s_ver] ^= (uint8_t)(1u << o);
    kit_ui_click();
    mark_dirty();
    paint_opt(o);
}

static void stamp_show(bool yes);

static void test_cb(lv_event_t *e)
{
    (void)e;
    round_t *R = &s_rounds[s_nr - 1];
    if (s_over || R->res[s_ver] || used() >= TESTS_PER_ROUND) return;
    bool yes = gz_answer(&s_pz, s_ver, R->code);
    R->res[s_ver] = yes ? 1 : 2;
    mark_dirty();
    stamp_show(yes);
    refresh_jogo();
    defer(DEF_OV);   /* histórico e rodapé mudaram */
}

static void build_ver(void)
{
    char buf[48];
    int card = s_pz.card[s_ver];
    gz_face(card, buf, sizeof buf);
    tokens(s_ov_title, buf, &kit_sans_28, KIT_COLOR_TEXT, 0);

    round_t *R = &s_rounds[s_nr - 1];
    bool done = R->res[s_ver] != 0;

    lv_obj_t *sc = body_scroll();
    lv_obj_set_style_pad_row(sc, 8, 0);
    lv_obj_set_style_pad_bottom(sc, 0, 0);
    for (int o = 0; o < GZ_MAX_OPTS; o++) s_opt_row[o] = NULL;
    for (int o = 0; o < GZ_CARDS[card].nopt; o++) {
        /* no modo folha as regras são só referência: quem risca é o papel */
        lv_obj_t *row = s_paper ? deco(kit_ui_rect(sc, lv_pct(100), TOUCH_H, KIT_COLOR_SURFACE, 18))
                                : button(sc, lv_pct(100), TOUCH_H, KIT_COLOR_SURFACE, opt_cb, o);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(KIT_COLOR_LINE), 0);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 14, 0);
        gz_opt_label(card, o, buf, sizeof buf);
        s_opt_tok[o] = tokens(row, buf, &kit_sans_28, KIT_COLOR_TEXT, 0);
        lv_obj_align(s_opt_tok[o], LV_ALIGN_LEFT_MID, 0, 0);
        s_opt_tag[o] = kit_ui_label(row, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
        lv_obj_align(s_opt_tag[o], LV_ALIGN_RIGHT_MID, 0, 0);
        s_opt_row[o] = row;
        paint_opt(o);
    }

    /* rodapé: TESTAR com o código numa pastilha escura (o triângulo amarelo
     * sumiria no fundo amarelo) — ou o motivo de não dar pra testar */
    if (done || used() >= TESTS_PER_ROUND || s_over) {
        lv_obj_t *w = box(s_ov_body);
        lv_obj_set_size(w, lv_pct(100), TOUCH_H);
        lv_obj_t *l = kit_ui_label(w, done ? (s_paper ? "JÁ TESTADO NESTA RODADA." : "JÁ TESTADO NESTA RODADA. VEJA EM NOTAS.")
                                           : "3 TESTES POR RODADA. ABRA OUTRA.",
                                   KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_center(l);
    } else {
        lv_obj_t *b = button(s_ov_body, lv_pct(100), TOUCH_H, T_ACCENT, test_cb, 0);
        lv_obj_set_style_radius(b, 18, 0);
        lv_obj_t *row = box(b);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        kit_ui_flex(row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 14);
        lv_obj_center(row);
        kit_ui_label(row, "TESTAR", KIT_COLOR_ON_YELLOW, &kit_mono_26, 2);
        lv_obj_t *pill = deco(kit_ui_rect(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT, KIT_COLOR_BG, 12));
        lv_obj_set_style_pad_left(pill, 12, 0);
        lv_obj_set_style_pad_right(pill, 12, 0);
        lv_obj_set_style_pad_top(pill, 8, 0);
        lv_obj_set_style_pad_bottom(pill, 8, 0);
        snprintf(buf, sizeof buf, "#0%d #1%d #2%d", R->code[0], R->code[1], R->code[2]);
        tokens(pill, buf, &kit_mono_26, KIT_COLOR_TEXT, 0);
    }
}

/* .............................................................. NOTAS */

static void paint_num_cells(void)
{
    for (int j = 0; j < GZ_SHAPES; j++) {
        bool on = j == s_num_shape;
        lv_obj_set_style_bg_color(s_num_tab[j], lv_color_hex(on ? KIT_COLOR_TEXT : KIT_COLOR_SURFACE), 0);
    }
    for (int d = 0; d < GZ_DIGITS; d++) {
        int st = s_grid[s_num_shape][d];
        lv_obj_set_style_bg_opa(s_num_cell[d], st == 1 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_num_cell[d], st == 0 ? 0 : (st == 1 ? 3 : 6), 0);
        lv_obj_set_style_border_color(s_num_cell[d], lv_color_hex(st == 2 ? KIT_COLOR_GREEN : KIT_COLOR_LINE), 0);
        lv_obj_set_style_text_color(s_num_cell_lbl[d], lv_color_hex(st == 1 ? KIT_COLOR_TEXT_MUTED : KIT_COLOR_TEXT), 0);
        if (st == 1) lv_obj_remove_flag(s_num_cell_bar[d], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(s_num_cell_bar[d], LV_OBJ_FLAG_HIDDEN);
    }
}

static void num_cell_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_grid[s_num_shape][d] = (uint8_t)((s_grid[s_num_shape][d] + 1) % 3);
    kit_ui_click();
    mark_dirty();
    paint_num_cells();
}

static void num_tab_cb(lv_event_t *e)
{
    s_num_shape = (int)(intptr_t)lv_event_get_user_data(e);
    kit_ui_click();
    paint_num_cells();
}

static void notes_tab_cb(lv_event_t *e)
{
    int t = (int)(intptr_t)lv_event_get_user_data(e);
    if (t == s_notes_tab) return;
    s_notes_tab = t;
    kit_ui_click();
    defer(DEF_OV);
}

static void build_notes(void)
{
    ov_title_text("NOTAS");

    lv_obj_t *tabs = box(s_ov_body);
    lv_obj_set_size(tabs, lv_pct(100), TOUCH_H);
    kit_ui_flex(tabs, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 0);
    static const char *const TN[2] = { "TESTES", "NÚMEROS" };
    for (int t = 0; t < 2; t++) {
        bool on = t == s_notes_tab;
        lv_obj_t *b = button(tabs, 162, TOUCH_H, on ? KIT_COLOR_TEXT : KIT_COLOR_SURFACE, notes_tab_cb, t);
        lv_obj_center(kit_ui_label(b, TN[t], on ? KIT_COLOR_BG : KIT_COLOR_TEXT, &kit_mono_20, 1));
    }

    if (s_notes_tab == 0) {
        /* uma linha por verificador, uma coluna por rodada; a mais recente primeiro
         * (o SDK não tem scroll_to — o que importa já abre à vista) */
        int cols[MAX_ROUNDS], nc = 0;
        for (int r = s_nr - 1; r >= 0; r--) {
            bool any = false;
            for (int v = 0; v < GZ_VERIFIERS; v++) any |= s_rounds[r].res[v] != 0;
            if (any) cols[nc++] = r;
        }
        if (!nc) {
            kit_ui_label(s_ov_body, "Nenhum teste ainda.", KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);
            return;
        }
        lv_obj_t *sc = lv_obj_create(s_ov_body);
        lv_obj_remove_style_all(sc);
        lv_obj_set_width(sc, lv_pct(100));
        lv_obj_set_flex_grow(sc, 1);
        lv_obj_add_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(sc, LV_DIR_ALL);
        lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
        /* fica CLICKABLE: o LVGL só acha quem rola a partir de um alvo clicável */

        enum { FACE_W = 124, COL_W = 56, HEAD_H = 104, ROW_H = 60 };
        char buf[48];
        /* cabeçalho: número da rodada + código empilhado */
        for (int c = 0; c < nc; c++) {
            round_t *R = &s_rounds[cols[c]];
            lv_obj_t *h = box(sc);
            lv_obj_set_size(h, COL_W, HEAD_H);
            lv_obj_set_pos(h, FACE_W + c * COL_W, 0);
            kit_ui_flex(h, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, 0, 0);
            for (int i = 0; i < GZ_SHAPES; i++) {
                snprintf(buf, sizeof buf, "#%d%d", i, R->code[i]);
                tokens(h, buf, &kit_mono_26, KIT_COLOR_TEXT, 0);
            }
        }
        lv_obj_t *rl = kit_ui_label(sc, "RODADA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
        lv_obj_set_pos(rl, 0, 40);
        for (int v = 0; v < GZ_VERIFIERS; v++) {
            int y = HEAD_H + v * ROW_H;
            lv_obj_t *line = deco(kit_ui_rect(sc, FACE_W + nc * COL_W, 2, KIT_COLOR_SURFACE_ALT, 0));
            lv_obj_set_pos(line, 0, y);
            gz_face(s_pz.card[v], buf, sizeof buf);
            lv_obj_t *t = tokens(sc, buf, &kit_sans_22, KIT_COLOR_TEXT, FACE_W - 8);
            lv_obj_set_pos(t, 0, y + 8);
            for (int c = 0; c < nc; c++) {
                int res = s_rounds[cols[c]].res[v];
                lv_obj_t *m = kit_ui_label(sc, res == 1 ? KIT_ICON_CHECK : res == 2 ? "X" : "·",
                                           res == 1 ? KIT_COLOR_GREEN : res == 2 ? KIT_COLOR_RED : KIT_COLOR_TEXT_MUTED,
                                           &kit_mono_26, 0);
                lv_obj_set_pos(m, FACE_W + c * COL_W + 16, y + 14);
            }
        }
        return;
    }

    /* NÚMEROS: uma forma por vez, botões grandes */
    lv_obj_t *st = box(s_ov_body);
    lv_obj_set_size(st, lv_pct(100), TOUCH_H);
    kit_ui_flex(st, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 0);
    for (int j = 0; j < GZ_SHAPES; j++) {
        lv_obj_t *b = button(st, 104, TOUCH_H, KIT_COLOR_SURFACE, num_tab_cb, j);
        lv_obj_center(shape_img(b, SHAPE_IMG_SM, j, SHAPE_COLOR[j]));
        s_num_tab[j] = b;
    }
    lv_obj_t *g = box(s_ov_body);
    lv_obj_set_size(g, lv_pct(100), 2 * TOUCH_H + 8);
    for (int d = 0; d < GZ_DIGITS + 1; d++) {
        int x = (d % 3) * 116, y = (d / 3) * (TOUCH_H + 8);
        if (d == GZ_DIGITS) {
            lv_obj_t *lg = box(g);
            lv_obj_set_size(lg, 104, TOUCH_H);
            lv_obj_set_pos(lg, x, y);
            kit_ui_flex(lg, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 0, 0);
            kit_ui_label(lg, "1 RISCA", KIT_COLOR_TEXT, &kit_mono_16, 1);
            kit_ui_label(lg, "2 CERTO", KIT_COLOR_TEXT, &kit_mono_16, 1);
            kit_ui_label(lg, "3 LIMPA", KIT_COLOR_TEXT, &kit_mono_16, 1);
            break;
        }
        lv_obj_t *b = button(g, 104, TOUCH_H, KIT_COLOR_SURFACE, num_cell_cb, d);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_style_radius(b, 18, 0);
        char t[4];
        snprintf(t, sizeof t, "%d", d + 1);
        s_num_cell_lbl[d] = kit_ui_label(b, t, KIT_COLOR_TEXT, &kit_display_44, 0);
        lv_obj_center(s_num_cell_lbl[d]);
        lv_obj_t *bar = deco(kit_ui_rect(b, 64, 5, KIT_COLOR_RED, 2));
        lv_obj_center(bar);
        s_num_cell_bar[d] = bar;
        s_num_cell[d] = b;
    }
    paint_num_cells();
}

/* ........................................................... ARRISCAR */

static void paint_guess(void)
{
    for (int i = 0; i < GZ_SHAPES; i++) paint_disc(s_gdisc[i], s_gdisc_lbl[i], i, s_guess[i], false);
    lv_obj_set_style_opa(s_gconfirm, s_guess_ready ? LV_OPA_COVER : LV_OPA_40, 0);

    if (!s_gcheck) return;
    lv_obj_clean(s_gcheck);
    if (!s_assist || s_paper) { lv_obj_add_flag(s_gcheck, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(s_gcheck, LV_OBJ_FLAG_HIDDEN);
    static gz_obs_t obs[MAX_ROUNDS * GZ_VERIFIERS];
    int n = collect_obs(obs);
    uint8_t bad = gz_guess_conflicts(&s_pz, s_guess, obs, n);
    lv_obj_set_style_border_color(s_gcheck, lv_color_hex(bad ? KIT_COLOR_RED : KIT_COLOR_GREEN), 0);
    if (!bad) {
        kit_ui_label(s_gcheck, KIT_ICON_CHECK " BATE COM SEUS TESTES", KIT_COLOR_GREEN, &kit_mono_20, 0);
        return;
    }
    kit_ui_label(s_gcheck, "NÃO BATE COM", KIT_COLOR_TEXT, &kit_mono_20, 0);
    char buf[48];
    for (int v = 0; v < GZ_VERIFIERS; v++)
        if ((bad >> v) & 1) {
            gz_face(s_pz.card[v], buf, sizeof buf);
            tokens(s_gcheck, buf, &kit_sans_22, KIT_COLOR_TEXT, 0);
        }
}

static void gdisc_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_guess[i] = (uint8_t)(s_guess[i] % 5 + 1);
    kit_ui_click();
    paint_guess();
}

static void guard_cb(lv_timer_t *t)
{
    (void)t;
    s_guard_timer = NULL;
    s_guess_ready = true;
    if (s_ov_kind == OV_GUESS && s_gconfirm) lv_obj_set_style_opa(s_gconfirm, LV_OPA_COVER, 0);
}

static void intro_start(void);

static void confirm_cb(lv_event_t *e)
{
    (void)e;
    if (s_ov_kind != OV_GUESS || !s_guess_ready || s_over) return;
    s_win = s_guess[0] == s_pz.code[0] && s_guess[1] == s_pz.code[1] && s_guess[2] == s_pz.code[2];
    s_over = true;
    s_record = false;
    s_played++;
    if (s_win) {
        s_won++;
        int32_t r = rounds_played(), t = tests_total();
        s_record = s_best_r > 0 && (r < s_best_r || (r == s_best_r && t < s_best_t));
        if (s_best_r == 0 || s_record) { s_best_r = r; s_best_t = t; }
    }
    if (s_mode == 0) { s_daily_done = s_day; set_i32("gz_dday", s_day); }
    set_i32("gz_played", s_played);
    set_i32("gz_won", s_won);
    set_i32("gz_bestr", s_best_r);
    set_i32("gz_bestt", s_best_t);
    save_game();
    refresh_ajuste();
    open_ov(OV_RESULT);
    intro_start();
}

static void cancel_cb(lv_event_t *e) { (void)e; kit_ui_sfx(KIT_SFX_BACK); close_ov(); }

static void build_guess(void)
{
    ov_title_text("ARRISCAR");
    kit_ui_label(s_ov_body, "Um palpite só. Errou, acabou.", KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);

    lv_obj_t *dr = box(s_ov_body);
    lv_obj_set_size(dr, lv_pct(100), DISC_GUESS_H);
    for (int i = 0; i < GZ_SHAPES; i++) {
        s_gdisc[i] = make_disc(dr, i, DISC_GUESS_H, gdisc_cb, &s_gdisc_lbl[i]);
        lv_obj_set_pos(s_gdisc[i], i * 116, 0);
    }

    s_gcheck = box(s_ov_body);
    lv_obj_set_size(s_gcheck, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(s_gcheck, 2, 0);
    lv_obj_set_style_radius(s_gcheck, 14, 0);
    lv_obj_set_style_pad_all(s_gcheck, 10, 0);
    lv_obj_set_flex_flow(s_gcheck, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_gcheck, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_gcheck, 10, 0);
    lv_obj_set_style_pad_row(s_gcheck, 4, 0);

    int rp = rounds_played(), tt = tests_total();
    char buf[48];
    snprintf(buf, sizeof buf, "Se abrir: %d rodada%s, %d teste%s", rp, rp == 1 ? "" : "s", tt, tt == 1 ? "" : "s");
    kit_ui_label(s_ov_body, buf, KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);

    lv_obj_t *sp = box(s_ov_body);
    lv_obj_set_width(sp, lv_pct(100));
    lv_obj_set_flex_grow(sp, 1);

    lv_obj_t *ft = box(s_ov_body);
    lv_obj_set_size(ft, lv_pct(100), TOUCH_H);
    s_gconfirm = button(ft, 200, TOUCH_H, T_ACCENT, confirm_cb, 0);
    lv_obj_set_pos(s_gconfirm, 0, 0);
    lv_obj_t *row = box(s_gconfirm);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(row, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 10);
    mini_lock(row, KIT_COLOR_ON_YELLOW);
    kit_ui_label(row, "ARRISCAR", KIT_COLOR_ON_YELLOW, &kit_mono_26, 1);
    lv_obj_center(row);
    lv_obj_t *cb = button(ft, KIT_UI_CONTENT - 212, TOUCH_H, KIT_COLOR_SURFACE, cancel_cb, 0);
    lv_obj_set_pos(cb, 212, 0);
    lv_obj_center(kit_ui_label(cb, "CANCELAR", KIT_COLOR_TEXT, &kit_mono_20, 1));

    /* trava de 0,6 s: o toque que abriu a tela (ou um toque duplo) não confirma */
    s_guess_ready = false;
    if (s_guard_timer) lv_timer_delete(s_guard_timer);
    s_guard_timer = lv_timer_create(guard_cb, 600, NULL);
    lv_timer_set_repeat_count(s_guard_timer, 1);
    paint_guess();
}

/* ........................................................... RESULTADO */

static void again_cb(lv_event_t *e)
{
    (void)e;
    kit_ui_confirm();
    start_new_puzzle();
    open_ov(OV_MODE);
}

/* ......................................................... MODO / FOLHA */

static void mode_pick_cb(lv_event_t *e)
{
    int paper = (int)(intptr_t)lv_event_get_user_data(e);
    s_paper = s_paper_pref = (uint8_t)paper;
    set_i32("gz_paper", paper);
    save_game();
    refresh_ajuste();
    kit_ui_click();
    if (paper) open_ov(OV_QR);
    else close_ov();
}

static void qr_done_cb(lv_event_t *e) { (void)e; kit_ui_click(); close_ov(); }

static void build_mode(void)
{
    ov_title_text("NOVO PUZZLE");
    char buf[48];
    if (s_mode == 0) snprintf(buf, sizeof buf, "PUZZLE DO DIA #%d · %s", (int)s_day, DIFF_LABELS[s_diff]);
    else             snprintf(buf, sizeof buf, "PUZZLE LIVRE · %s", DIFF_LABELS[s_diff]);
    section(s_ov_body, buf);

    static const char *const T[2] = { "SÓ KIT", "COM FOLHA" };
    static const char *const SUB[2] = { "anote tudo no KIT", "imprima ou anote no celular" };
    for (int i = 0; i < 2; i++) {
        bool on = i == s_paper_pref;   /* a última escolha vem marcada */
        lv_obj_t *b = button(s_ov_body, lv_pct(100), 118, on ? T_ACCENT : KIT_COLOR_SURFACE, mode_pick_cb, i);
        lv_obj_set_style_radius(b, 18, 0);
        lv_obj_t *col = box(b);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        kit_ui_flex(col, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 6, 0);
        lv_obj_center(col);
        kit_ui_label(col, T[i], on ? KIT_COLOR_ON_YELLOW : KIT_COLOR_TEXT, &kit_mono_26, 2);
        kit_ui_label(col, SUB[i], on ? KIT_COLOR_ON_YELLOW : KIT_COLOR_TEXT, &kit_sans_22, 0);
    }
}

static void build_qr(void)
{
    ov_title_text("FOLHA");
    char url[KIT_UI_QR_MAX];
    snprintf(url, sizeof url, FOLHA_URL "%d,%d,%d,%d&d=%d&n=%d",
             s_pz.card[0], s_pz.card[1], s_pz.card[2], s_pz.card[3],
             s_mode == 0 ? (int)s_day : 0, s_diff);
    lv_obj_t *c = box(s_ov_body);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_flex_grow(c, 1);
    kit_ui_flex(c, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 8, 0);
    kit_ui_qr(&s_qr, c, url);   /* 216 px + "Toque para expandir" */
    lv_obj_t *b = button(s_ov_body, lv_pct(100), TOUCH_H, T_ACCENT, qr_done_cb, 0);
    lv_obj_set_style_radius(b, 18, 0);
    btn_label(b, "CONTINUAR", KIT_COLOR_ON_YELLOW, &kit_mono_26);
}

static void build_result(void)
{
    ov_title_text("GAZUA");
    lv_obj_t *sc = body_scroll();
    lv_obj_set_style_pad_row(sc, 10, 0);
    char buf[64];

    lv_obj_t *band = deco(kit_ui_rect(sc, lv_pct(100), 72, s_win ? KIT_COLOR_GREEN : KIT_COLOR_RED, 16));
    lv_obj_set_style_pad_left(band, 16, 0);
    lv_obj_set_style_pad_right(band, 14, 0);
    kit_ui_flex(band, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 0);
    uint32_t on = s_win ? C_DARK_ON_GREEN : KIT_COLOR_ON_COLOR;
    kit_ui_label(band, s_win ? "ABERTO" : "TRAVOU", on, &kit_display_44, 0);
    if (s_record) kit_ui_label(band, "NOVO\nRECORDE", on, &kit_mono_16, 1);

    if (s_win) {
        lv_obj_t *rv = box(sc);
        lv_obj_set_size(rv, lv_pct(100), LV_SIZE_CONTENT);
        kit_ui_flex(rv, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, 0, 16);
        for (int i = 0; i < GZ_SHAPES; i++) code_shape(rv, i, s_pz.code[i], SHAPE_IMG_LG, &kit_display_44);
        int rp = rounds_played(), tt = tests_total();
        snprintf(buf, sizeof buf, "%d rodada%s · %d teste%s", rp, rp == 1 ? "" : "s", tt, tt == 1 ? "" : "s");
        lv_obj_t *s = kit_ui_label(sc, buf, KIT_COLOR_TEXT, &kit_sans_28, 0);
        lv_obj_set_width(s, lv_pct(100));
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        /* ERA × VOCÊ em colunas, com selo SIM/NÃO em cada forma do palpite */
        for (int row = 0; row < 2; row++) {
            lv_obj_t *r = box(sc);
            lv_obj_set_size(r, lv_pct(100), row == 0 ? SHAPE_LG : SHAPE_MD + 8);
            lv_obj_t *l = kit_ui_label(r, row == 0 ? "ERA" : "VOCÊ", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
            for (int i = 0; i < GZ_SHAPES; i++) {
                int cx = 70 + i * 90;
                if (row == 0) {
                    lv_obj_t *s = code_shape(r, i, s_pz.code[i], SHAPE_IMG_LG, &kit_display_44);
                    lv_obj_set_pos(s, cx + (88 - SHAPE_LG) / 2, 0);
                } else {
                    lv_obj_t *s = code_shape(r, i, s_guess[i], SHAPE_IMG_MD, &kit_mono_26);
                    lv_obj_set_pos(s, cx + (88 - SHAPE_MD) / 2, 8);
                    bool ok = s_guess[i] == s_pz.code[i];
                    lv_obj_t *bd = deco(kit_ui_rect(r, 30, 30, ok ? KIT_COLOR_GREEN : KIT_COLOR_RED, LV_RADIUS_CIRCLE));
                    lv_obj_set_style_border_width(bd, 3, 0);
                    lv_obj_set_style_border_color(bd, lv_color_hex(KIT_COLOR_BG), 0);
                    lv_obj_set_pos(bd, cx + (88 - SHAPE_MD) / 2 + SHAPE_MD - 18, 0);
                    lv_obj_center(kit_ui_label(bd, ok ? KIT_ICON_CHECK : "X", KIT_COLOR_ON_COLOR, &kit_mono_16, 0));
                }
            }
        }
    }

    section(sc, "AS REGRAS ERAM");
    for (int v = 0; v < GZ_VERIFIERS; v++) {
        lv_obj_t *r = box(sc);
        lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_bottom(r, 8, 0);
        lv_obj_set_style_border_width(r, 2, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(r, lv_color_hex(KIT_COLOR_SURFACE_ALT), 0);
        kit_ui_flex(r, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 8);
        gz_face(s_pz.card[v], buf, sizeof buf);
        tokens(r, buf, &kit_sans_22, KIT_COLOR_TEXT_MUTED, 0);
        gz_opt_label(s_pz.card[v], s_pz.secret[v], buf, sizeof buf);
        tokens(r, buf, &kit_sans_22, KIT_COLOR_TEXT, 0);
    }
    if (s_mode == 0) snprintf(buf, sizeof buf, "PUZZLE DO DIA #%d · %s", (int)s_day, DIFF_LABELS[s_diff]);
    else             snprintf(buf, sizeof buf, "PUZZLE LIVRE · %s", DIFF_LABELS[s_diff]);
    section(sc, buf);

    lv_obj_t *b = button(sc, lv_pct(100), TOUCH_H, KIT_COLOR_SURFACE, again_cb, 0);
    lv_obj_center(kit_ui_label(b, "NOVO PUZZLE", KIT_COLOR_TEXT, &kit_mono_26, 1));
}

static void build_overlay(void)
{
    if (s_ov_kind == OV_NONE) return;
    kit_ui_qr_close(&s_qr);          /* o expandido (se aberto) some antes de a página ser refeita */
    s_qr.qr = NULL;
    s_qr.hint = NULL;
    lv_obj_clean(s_ov_title);
    lv_obj_clean(s_ov_body);
    s_gcheck = NULL;
    s_gconfirm = NULL;
    switch (s_ov_kind) {
    case OV_VER:    build_ver(); break;
    case OV_NOTES:  build_notes(); break;
    case OV_GUESS:  build_guess(); break;
    case OV_RESULT: build_result(); break;
    case OV_MODE:   build_mode(); break;
    case OV_QR:     build_qr(); break;
    }
}

static void create_overlay(void)
{
    s_ov = kit_ui_rect(s_screen, KIT_UI_SCREEN_W, KIT_UI_SCREEN_H, KIT_COLOR_BG, 0);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);   /* absorve o toque: nada vaza pro JOGO */

    lv_obj_t *chip = button(s_ov, KIT_UI_CHIP, KIT_UI_CHIP, KIT_COLOR_SURFACE, ov_back_cb, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_set_pos(chip, KIT_UI_PAD, 16);
    lv_obj_center(kit_ui_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    s_ov_title = box(s_ov);
    lv_obj_set_size(s_ov_title, KIT_UI_SCREEN_W - (KIT_UI_PAD + KIT_UI_CHIP + 12) - KIT_UI_PAD, KIT_UI_CHIP);
    lv_obj_set_pos(s_ov_title, KIT_UI_PAD + KIT_UI_CHIP + 12, 16);
    kit_ui_flex(s_ov_title, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_START, 0, 6);

    s_ov_body = box(s_ov);
    lv_obj_set_size(s_ov_body, KIT_UI_SCREEN_W, KIT_UI_PAGE_H);
    lv_obj_set_pos(s_ov_body, 0, KIT_UI_TITLEBAR);
    lv_obj_set_style_pad_left(s_ov_body, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_right(s_ov_body, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_top(s_ov_body, 0, 0);
    lv_obj_set_style_pad_bottom(s_ov_body, 10, 0);
    lv_obj_set_style_pad_row(s_ov_body, 8, 0);
    lv_obj_set_flex_flow(s_ov_body, LV_FLEX_FLOW_COLUMN);

    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------- carimbo SIM/NÃO */

static void stamp_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_stamp, LV_OBJ_FLAG_HIDDEN);
}

static void stamp_show(bool yes)
{
    lv_obj_set_style_bg_color(s_stamp_pill, lv_color_hex(yes ? KIT_COLOR_GREEN : KIT_COLOR_RED), 0);
    lv_label_set_text(s_stamp_icon, yes ? KIT_ICON_CHECK : "X");
    lv_label_set_text(s_stamp_word, yes ? "SIM" : "NÃO");
    lv_obj_remove_flag(s_stamp, LV_OBJ_FLAG_HIDDEN);
    if (yes) { soft(784); soft(1047); }
    else kit_ui_miss();
}

static void create_stamp(void)
{
    s_stamp = kit_ui_rect(s_screen, KIT_UI_SCREEN_W, KIT_UI_SCREEN_H, KIT_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_stamp, 242, 0);
    lv_obj_set_pos(s_stamp, 0, 0);
    kit_ui_tap(s_stamp, stamp_close_cb, 0);
    lv_obj_set_style_bg_opa(s_stamp, 242, LV_STATE_PRESSED);
    kit_ui_flex(s_stamp, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 22, 0);

    s_stamp_pill = deco(kit_ui_rect(s_stamp, 250, 170, KIT_COLOR_GREEN, 36));
    lv_obj_set_style_border_width(s_stamp_pill, 5, 0);
    lv_obj_set_style_border_color(s_stamp_pill, lv_color_hex(KIT_COLOR_TEXT), 0);
    kit_ui_flex(s_stamp_pill, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 0, 0);
    s_stamp_icon = kit_ui_label(s_stamp_pill, "", KIT_COLOR_ON_COLOR, &kit_display_44, 0);
    s_stamp_word = kit_ui_label(s_stamp_pill, "", KIT_COLOR_ON_COLOR, &kit_display_72, 0);
    kit_ui_label(s_stamp, "TOQUE PRA SEGUIR", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_add_flag(s_stamp, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------- abertura (ABERTO / TRAVOU) */

#define INTRO_MS  1800
#define LOCK_CX   (KIT_UI_SCREEN_W / 2)
#define LOCK_CY   170
#define LOCK_HEADROOM 40   /* folga acima do arco: ele sobe 26 px (até ~28 na passada) */

/* notas do arpejo da vitória (ms desde o início, Hz) */
static const uint16_t WIN_NOTES[][2] = {
    { 520, 523 }, { 590, 659 }, { 660, 784 }, { 730, 1047 }, { 800, 1319 }, { 870, 1568 }, { 960, 2093 },
};
#define WIN_NOTES_N ((int)(sizeof WIN_NOTES / sizeof WIN_NOTES[0]))

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* progresso 0..1000 de [a, b] */
static int prog(int t, int a, int b) { return clampi((t - a) * 1000 / (b - a), 0, 1000); }

/* ease-out com leve passada (back), inteiro: 0..1000 -> ~0..1080..1000 */
static int ease_back(int p)
{
    int q = 1000 - p;                       /* 1000..0 */
    int cub = q * q / 1000 * q / 1000;      /* (1-p)^3 */
    int base = 1000 - cub;                  /* ease-out cúbico */
    int bump = p * q / 1000 * 4 / 10;       /* sobe e volta: passa um pouco */
    return base + bump;
}

static void intro_stop(void)
{
    if (s_anim_timer) { lv_timer_delete(s_anim_timer); s_anim_timer = NULL; }
    lv_obj_add_flag(s_intro, LV_OBJ_FLAG_HIDDEN);
}

static uint32_t now_ms(void)
{
    if (s_api && s_api->time) return (uint32_t)s_api->time->get_millis();
    return (uint32_t)s_anim_frames * 20u;
}

static void intro_frame(int t)
{
    if (s_win) {
        int s = 640 * prog(t, 0, 550) / 1000;
        lv_obj_set_size(s_flood, s, s);
        lv_obj_set_style_radius(s_flood, s / 2, 0);
        lv_obj_set_pos(s_flood, LOCK_CX - s / 2, LOCK_CY + 20 - s / 2);
        lv_obj_set_style_translate_y(s_shackle, -26 * ease_back(prog(t, 450, 950)) / 1000, 0);
    } else {
        lv_obj_set_style_bg_opa(s_flood, (lv_opa_t)(255 * prog(t, 0, 160) / 1000), 0);
        int p = prog(t, 350, 850);
        static const int8_t SHAKE[] = { 0, -16, 14, -10, 6, -3, 0 };
        int k = p * 6 / 1000;
        int x = (k >= 6) ? 0 : SHAKE[k] + (SHAKE[k + 1] - SHAKE[k]) * (p * 6 % 1000) / 1000;
        lv_obj_set_style_translate_x(s_lock, x, 0);
    }
    /* o cadeado não esmaece: a opacidade do LVGL vale peça por peça, e o arco
     * apareceria através do corpo semitransparente */
    lv_obj_set_style_opa(s_intro_word, (lv_opa_t)(255 * prog(t, 600, 950) / 1000), 0);
    lv_obj_set_style_opa(s_intro_sub, (lv_opa_t)(255 * prog(t, 800, 1150) / 1000), 0);
}

static void anim_cb(lv_timer_t *tm)
{
    (void)tm;
    s_anim_frames++;
    int t = (int)(now_ms() - s_anim_t0);
    intro_frame(t);
    if (s_win) {
        while (s_anim_note < WIN_NOTES_N && t >= WIN_NOTES[s_anim_note][0]) {
            soft(WIN_NOTES[s_anim_note][1]);
            soft(WIN_NOTES[s_anim_note][1]);
            s_anim_note++;
        }
    } else if (s_anim_note == 0 && t >= 350) {
        kit_ui_miss();
        s_anim_note = 1;
    }
    if (t >= INTRO_MS) intro_stop();
}

static void intro_skip_cb(lv_event_t *e) { (void)e; intro_stop(); }

static void intro_start(void)
{
    uint32_t flood = s_win ? KIT_COLOR_GREEN : KIT_COLOR_RED;
    uint32_t ink = s_win ? C_DARK_ON_GREEN : KIT_COLOR_ON_COLOR;
    lv_obj_set_style_bg_color(s_flood, lv_color_hex(flood), 0);
    lv_obj_set_style_bg_opa(s_flood, LV_OPA_COVER, 0);
    if (s_win) {
        lv_obj_set_size(s_flood, 0, 0);
    } else {
        lv_obj_set_size(s_flood, KIT_UI_SCREEN_W, KIT_UI_SCREEN_H);
        lv_obj_set_pos(s_flood, 0, 0);
        lv_obj_set_style_radius(s_flood, 0, 0);
    }
    lv_obj_set_style_border_color(s_shackle, lv_color_hex(ink), 0);
    lv_obj_set_style_bg_color(s_lock_body, lv_color_hex(ink), 0);
    lv_obj_set_style_bg_color(s_key_c, lv_color_hex(flood), 0);
    lv_obj_set_style_bg_color(s_key_s, lv_color_hex(flood), 0);
    lv_obj_set_style_translate_y(s_shackle, 0, 0);
    lv_obj_set_style_translate_x(s_lock, 0, 0);
    lv_obj_set_style_text_color(s_intro_word, lv_color_hex(ink), 0);
    lv_obj_set_style_text_color(s_intro_sub, lv_color_hex(ink), 0);
    lv_label_set_text(s_intro_word, s_win ? "ABERTO" : "TRAVOU");
    if (s_win) {
        int rp = rounds_played(), tt = tests_total();
        lv_label_set_text_fmt(s_intro_sub, "%d RODADA%s · %d TESTE%s", rp, rp == 1 ? "" : "S", tt, tt == 1 ? "" : "S");
    } else {
        lv_label_set_text(s_intro_sub, "TOQUE PRA VER O CÓDIGO");
    }
    s_anim_frames = 0;
    s_anim_note = 0;
    s_anim_t0 = now_ms();
    intro_frame(0);
    lv_obj_remove_flag(s_intro, LV_OBJ_FLAG_HIDDEN);
    kit_ui_sfx(s_win ? KIT_SFX_UNLOCK : KIT_SFX_LOCK);
    if (s_anim_timer) lv_timer_delete(s_anim_timer);
    s_anim_timer = lv_timer_create(anim_cb, 20, NULL);
}

static void create_intro(void)
{
    s_intro = kit_ui_rect(s_screen, KIT_UI_SCREEN_W, KIT_UI_SCREEN_H, KIT_COLOR_BG, 0);
    lv_obj_set_pos(s_intro, 0, 0);
    kit_ui_tap(s_intro, intro_skip_cb, 0);
    lv_obj_set_style_bg_opa(s_intro, LV_OPA_COVER, LV_STATE_PRESSED);

    s_flood = deco(kit_ui_rect(s_intro, 0, 0, KIT_COLOR_GREEN, 0));

    /* a caixa tem LOCK_HEADROOM livres em cima: o arco sobe até ~28 px (com a
     * passada do ease_back) e o LVGL recorta o filho que sai da caixa do pai */
    s_lock = box(s_intro);
    lv_obj_set_size(s_lock, 150, 170 + LOCK_HEADROOM);
    lv_obj_set_pos(s_lock, LOCK_CX - 75, LOCK_CY - 100 - LOCK_HEADROOM);
    s_shackle = box(s_lock);
    lv_obj_set_size(s_shackle, 88, 150);
    lv_obj_set_pos(s_shackle, 31, LOCK_HEADROOM);
    lv_obj_set_style_border_width(s_shackle, 16, 0);
    lv_obj_set_style_border_side(s_shackle, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_radius(s_shackle, 44, 0);
    s_lock_body = deco(kit_ui_rect(s_lock, 130, 96, KIT_COLOR_TEXT, 16));
    lv_obj_set_pos(s_lock_body, 10, 74 + LOCK_HEADROOM);
    s_key_c = deco(kit_ui_rect(s_lock_body, 24, 24, KIT_COLOR_GREEN, LV_RADIUS_CIRCLE));
    lv_obj_align(s_key_c, LV_ALIGN_CENTER, 0, -10);
    s_key_s = deco(kit_ui_rect(s_lock_body, 12, 30, KIT_COLOR_GREEN, 4));
    lv_obj_align(s_key_s, LV_ALIGN_CENTER, 0, 8);

    s_intro_word = kit_ui_label(s_intro, "", KIT_COLOR_TEXT, &kit_display_72, 0);
    lv_obj_align(s_intro_word, LV_ALIGN_TOP_MID, 0, 270);
    s_intro_sub = kit_ui_label(s_intro, "", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_obj_align(s_intro_sub, LV_ALIGN_TOP_MID, 0, 360);
    lv_obj_add_flag(s_intro, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------- páginas */

static void on_page(int page, void *user)
{
    (void)user;
    if (page == 0) refresh_ajuste();
}

#endif /* !KIT_SDK_STUBS */

/* ===================================================================== */

#ifdef KIT_SDK_STUBS

kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    s_api = ctx ? ctx->api : NULL;
    gz_init();
    load_settings();
    if (!load_game()) new_puzzle_state();
    (void)in_progress;
    (void)opt_struck;
    (void)used;
    (void)rounds_played;
    return KIT_OK;
}

void tool_destroy(void)
{
    if (s_api && s_dirty) save_game();
    s_api = NULL;
}

#else

kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    gz_init();
    load_settings();
    bool fresh = !load_game();
    if (fresh) new_puzzle_state();
    s_ov_kind = OV_NONE;
    s_notes_tab = 0;
    s_num_shape = 0;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "GAZUA", T_ACCENT, 4);
    kit_ui_shell_tiles(&s_shell, on_page, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_codigo(s_shell.tiles[1]);
    build_testes(s_shell.tiles[2]);
    kit_ui_help_page(s_shell.tiles[3], "COMO JOGA", RULES);

    create_overlay();
    create_stamp();
    create_intro();

    build_cards();
    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);
    refresh_jogo();
    refresh_ajuste();

    s_save_timer = lv_timer_create(save_timer_cb, 1500, NULL);
    lv_screen_load(s_screen);
    if (fresh) open_ov(OV_MODE);   /* puzzle novo: SÓ KIT ou COM FOLHA */
    return KIT_OK;
}

void tool_destroy(void)
{
    if (s_anim_timer)  { lv_timer_delete(s_anim_timer);  s_anim_timer = NULL; }
    if (s_guard_timer) { lv_timer_delete(s_guard_timer); s_guard_timer = NULL; }
    if (s_defer_timer) { lv_timer_delete(s_defer_timer); s_defer_timer = NULL; }
    if (s_save_timer)  { lv_timer_delete(s_save_timer);  s_save_timer = NULL; }
    if (s_api && s_dirty) save_game();
    kit_ui_qr_reset(&s_qr);          /* devolve o brilho se o QR estava expandido */
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell = (kit_ui_shell_t){0};
    s_diff_chips = s_mode_chips = s_assist_chips = s_paper_chips = (kit_ui_chips_t){0};
    s_st_round = s_st_tests = s_cards_box = s_btn_round = s_btn_notes_lbl = NULL;
    memset(s_disc, 0, sizeof s_disc);
    memset(s_disc_lbl, 0, sizeof s_disc_lbl);
    memset(s_card_hole, 0, sizeof s_card_hole);
    memset(s_card_hole_lbl, 0, sizeof s_card_hole_lbl);
    s_pend = s_pend_lbl = NULL;
    memset(s_hist_val, 0, sizeof s_hist_val);
    s_ov = s_ov_title = s_ov_body = NULL;
    s_gcheck = s_gconfirm = NULL;
    s_stamp = s_stamp_pill = s_stamp_icon = s_stamp_word = NULL;
    s_intro = s_flood = s_lock = s_shackle = s_lock_body = s_key_c = s_key_s = NULL;
    s_intro_word = s_intro_sub = NULL;
    s_defer_bits = 0;
    s_ov_kind = OV_NONE;
    s_api = NULL;
}

#endif
