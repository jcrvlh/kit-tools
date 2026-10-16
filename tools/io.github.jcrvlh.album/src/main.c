/**
 * @file main.c
 * @brief ÁLBUM — álbum de figurinhas de Wi-Fi para o KIT.
 *
 * Abrir a Tool é abrir o pacote: o KIT varre as redes em volta (api->wifi,
 * runtime 0.18.0) e cada rede nova perto o bastante vira figurinha. Inédita
 * (cola vaga nova) vem num envelope que a pessoa rasga arrastando o dedo;
 * repetida entra direto em "Todas as redes". Lógica pura em album.c, arte em
 * art.c.
 *
 * Telas: shell padrão AJUSTE ◄─ JOGO ─► COMO JOGA (kit_ui.h) + uma camada por
 * cima (s_over) com pilha de vistas: PACOTE, ÁLBUM, PÁGINA, FICHA, LUGARES e
 * a pergunta LUGAR NOVO.
 *
 * Regra do runtime que este arquivo respeita: nunca apagar objeto LVGL dentro
 * do callback de um objeto que vai junto (ver Fora). Toda troca de vista
 * marca a pilha e reconstrói num lv_timer de um disparo (defer_render).
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"
#include "album.h"
#include "art.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KIT_SDK_STUBS
#include "shapes.h"

#define ACCENT       KIT_COLOR_GREEN
#define SCAN_MS      6000          /* > KIT_WIFI_SCAN_MIN_GAP_MS + ~1 s de scan */
#define TODAS_STEP   20            /* figurinhas por lote em "Todas as redes" */
#define ENV_W        300
#define ENV_H        240
#define FLAP_H       70
#define ENV_TRAVEL   200           /* px de arraste pra rasgar (de qualquer ponto da tela) */
#define BTN_H        KIT_TOUCH_TARGET_COMFORTABLE   /* 80: piso de botão principal */
#define CELL_W       ((KIT_UI_CONTENT - 12) / 2)    /* grade de 2 colunas */
#define MAX_DEPTH    6

/* Primeira constante global alinhada (ver o README do hello_sd). */
static const char RULES[] __attribute__((aligned(4))) =
    "1. Abra o Álbum onde estiver. O KIT olha as redes Wi-Fi em volta.\n\n"
    "2. Cada rede nova vira uma figurinha. Inédita é a que cola uma vaga nova "
    "do álbum: ela vem num envelope. Arraste o dedo na linha pra abrir.\n\n"
    "3. Só pega rede perto. Se aparecer \"longe\", ande uns passos com o KIT na mão.\n\n"
    "4. As páginas pedem redes especiais: cada canal, cada tipo de senha, cada "
    "hora do dia. Brilhante é rara.\n\n"
    "5. Abrindo sempre no mesmo lugar, o KIT pergunta que lugar é.\n\n"
    "O KIT só olha as redes, nunca conecta em nenhuma.";

static const char *const SLOT_CAP[FIG_SLOTS] = {
    "CANAL 1", "CANAL 2", "CANAL 3", "CANAL 4", "CANAL 5", "CANAL 6", "CANAL 7",
    "CANAL 8", "CANAL 9", "CANAL 10", "CANAL 11", "CANAL 12", "CANAL 13",
    "ABERTA", "WEP", "WPA2", "WPA3",
    "MADRUGADA", "MANHÃ", "ALMOÇO", "TARDE", "NOITE",
    "OCULTA", "NÔMADE", "BANDO", "ENCOSTADO", "FIEL", "MULTIDÃO",
};
static const char *const PAGE_TITLE[FIG_PAGES + 1] = {
    "CANAIS", "SEGURANÇA", "HORÁRIOS", "ESPECIAIS", "TODAS"
};
static const char *const PAGE_ROW[FIG_PAGES] = { "Canais", "Segurança", "Horários", "Especiais" };
static const char *const PLACE_ROW[FIG_PLACE_NAMES + 1] = {
    "Lugar sem nome", "Casa", "Trabalho", "Escola", "Academia", "Família", "Outro"
};
static const char *const PLACE_CHIPS[FIG_PLACE_NAMES] = {
    "CASA", "TRABALHO", "ESCOLA", "ACADEMIA", "FAMÍLIA", "OUTRO"
};
static const char *const AUTH_NAME[7] = {
    "ABERTA", "WEP", "WPA", "WPA2", "WPA3", "ENTERPRISE", "OUTRA"
};
/* índice = mês (1..12): sem "MESES[m - 1]" em array global (ver Fora) */
static const char *const MESES[13] = {
    "", "JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"
};
static const char *const NAME_CHIPS[2] = { "MOSTRAR", "ESCONDER" };

enum { V_PACK = 1, V_ALBUM, V_PAGE, V_FICHA, V_LUGARES, V_LUGAR };
enum { PAGE_TODAS = FIG_PAGES };
enum { ANIM_NONE = 0, ANIM_OPEN, ANIM_RISE };

/* ===================================================================== */

static const kit_api_table_t *s_api;
static fig_album_t  *s_album;
static fig_session_t s_sess;
static bool          s_show_name;

static lv_obj_t      *s_screen;
static kit_ui_shell_t s_shell;
static kit_ui_chips_t s_name_chips;
static kit_ui_chips_t s_place_chips;

/* JOGO */
static lv_obj_t *s_led, *s_place_lbl, *s_right_lbl;
static lv_obj_t *s_num_lbl, *s_hero_lbl, *s_radar, *s_ring, *s_found, *s_msg_lbl;
#define BLIPS 6
static lv_obj_t *s_blip[BLIPS];
static int       s_pop;          /* quadros restantes do "pulo" ao achar inédita */
static lv_obj_t *s_pack_btn, *s_pack_lbl, *s_pack_sub, *s_album_lbl;
static bool      s_have_scan;
static kit_err_t s_scan_err;

/* AJUSTE */
static lv_obj_t *s_zero_lbl;
static uint64_t  s_zero_armed_ms;

/* camada de vistas */
static lv_obj_t *s_over, *s_over_body, *s_over_right;
static struct { uint8_t v; int16_t arg; } s_stack[MAX_DEPTH];
static int  s_depth;
static int  s_todas_limit;

/* pacote */
static lv_obj_t *s_env, *s_env_flap, *s_env_tear, *s_env_grip, *s_env_hint, *s_rise_obj;
static bool s_drag, s_env_open;
static int  s_drag_x0, s_drag_dx;
static int  s_rev_idx;            /* figurinha recém-revelada, -1 = mostra envelope */
static int  s_pack_done;          /* reveladas desde que o pacote abriu */
static int  s_anim, s_anim_step;

/* timers */
static lv_timer_t *s_scan_timer, *s_fx_timer, *s_anim_timer, *s_defer_timer, *s_toast_timer;
static lv_obj_t   *s_toast;
static int         s_fx_tick;

static void render_over(void);
static void refresh_game(void);
static void game_cb(lv_event_t *e);

/* --- utilidades -------------------------------------------------------- */

static FILE *open_data(const char *name, const char *mode)
{
    return (s_api && s_api->storage && s_api->storage->open_file)
               ? s_api->storage->open_file(name, mode) : NULL;
}

static void save(void)
{
    if (s_album) fig_album_save(s_album, open_data);
}

static uint64_t now_ms(void)
{
    return (s_api && s_api->time) ? s_api->time->get_millis() : 0;
}

static fig_clock_t read_clock(void)
{
    fig_clock_t c = { false, 0, FIG_NO_HOUR };
    kit_datetime_t dt;
    if (s_api && s_api->time && s_api->time->get_datetime &&
        s_api->time->get_datetime(&dt) == KIT_OK) {
        c.day = fig_day_number(dt.year, dt.month, dt.day);
        c.hour = dt.hour;
        c.valid = c.day != 0;
    }
    return c;
}

static void net_name(int idx, char *buf, size_t len)
{
    const fig_net_t *n = &s_album->nets[idx];
    if (!s_show_name)            snprintf(buf, len, "Rede #%03d", idx + 1);
    else if (fig_is_hidden(n))   snprintf(buf, len, "(rede oculta)");
    else                         snprintf(buf, len, "%s", n->ssid);
}

static const char *place_caps(void)
{
    if (s_sess.place < 0) return "PASSEIO";
    int nm = s_album->places[s_sess.place].name;
    return nm ? FIG_PLACE_NAME[nm] : "PASSEIO";
}

static lv_obj_t *col_box(lv_obj_t *parent, int w)
{
    lv_obj_t *b = kit_ui_box(parent);
    lv_obj_set_size(b, w, LV_SIZE_CONTENT);
    kit_ui_flex(b, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, 6, 0);
    return b;
}

static lv_obj_t *no_click(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* Botão de BTN_H: superfície `bg`, rótulo em `fg`. `pill` = botão de ação da
 * JOGO (pílula, mono 26, como kit_ui_action_button); senão raio de chip. */
static lv_obj_t *button(lv_obj_t *parent, int w, uint32_t bg, uint32_t fg, bool pill,
                        const char *txt, lv_event_cb_t cb, int code, lv_obj_t **lbl)
{
    lv_obj_t *b = kit_ui_rect(parent, w, BTN_H, bg, pill ? BTN_H / 2 : 18);
    kit_ui_tap(b, cb, code);
    lv_obj_t *l = kit_ui_label(b, txt, fg, pill ? &kit_mono_26 : &kit_mono_20, 3);
    lv_obj_center(l);
    if (lbl) *lbl = l;
    return b;
}

static void toast_cb(lv_timer_t *t)
{
    (void)t;
    s_toast_timer = NULL;   /* repeat_count 1: o LVGL apaga o timer */
    if (s_toast) lv_obj_delete(s_toast);
    s_toast = NULL;
}

static void toast(const char *msg)
{
    if (s_toast) lv_obj_delete(s_toast);
    if (s_toast_timer) lv_timer_delete(s_toast_timer);
    s_toast = kit_ui_rect(s_screen, KIT_UI_CONTENT, LV_SIZE_CONTENT, KIT_COLOR_SURFACE_ALT, 18);
    lv_obj_set_style_pad_all(s_toast, 16, 0);
    no_click(s_toast);
    lv_obj_t *l = kit_ui_label(s_toast, msg, KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, KIT_UI_CONTENT - 32);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -16);
    s_toast_timer = lv_timer_create(toast_cb, 2200, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

/* --- pilha de vistas (reconstrói fora do callback) -------------------- */

static void defer_cb(lv_timer_t *t)
{
    (void)t;
    s_defer_timer = NULL;   /* repeat_count 1: o LVGL apaga o timer */
    render_over();
}

static void defer_render(void)
{
    if (s_defer_timer) return;
    s_defer_timer = lv_timer_create(defer_cb, 1, NULL);
    lv_timer_set_repeat_count(s_defer_timer, 1);
}

static void push(int v, int arg)
{
    if (s_depth >= MAX_DEPTH) return;
    s_stack[s_depth].v = (uint8_t)v;
    s_stack[s_depth].arg = (int16_t)arg;
    s_depth++;
    if (v == V_PACK) { s_rev_idx = -1; s_pack_done = 0; }
    if (v == V_PAGE && arg == PAGE_TODAS) s_todas_limit = TODAS_STEP;
    defer_render();
}

static void pop(void)
{
    if (!s_depth) return;
    if (s_stack[s_depth - 1].v == V_LUGAR) {
        int p = s_stack[s_depth - 1].arg;
        s_album->places[p].asked = 1;   /* "agora não" / voltar: não pergunta de novo */
        save();
    }
    s_depth--;
    defer_render();
}

static void back_cb(lv_event_t *e) { (void)e; kit_ui_sfx(KIT_SFX_BACK); pop(); }

/* --- JOGO -------------------------------------------------------------- */

/* Radar: 3 anéis fixos, o pulso verde saindo do centro e as redes LONGE como
 * pontinhos amarelos no anel de fora (acendem quando o pulso passa). */
#define RADAR      168
#define BLIP_R     70
static const int8_t BLIP_XY[BLIPS][2] = {
    { 66, -24 }, { -24, -66 }, { -66, 24 }, { 24, 66 }, { 30, -63 }, { -30, 63 }
};

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_game(void)
{
    if (!s_num_lbl) return;
    int sealed = s_album ? fig_sealed_count(s_album) : 0;
    bool wifi = s_api->wifi != NULL;
    bool fail = !s_have_scan && s_scan_err == KIT_ERR_NO_MEM;
    bool found = wifi && s_have_scan && sealed > 0;
    bool radar = wifi && !fail && !found;

    if (found && lv_obj_has_flag(s_found, LV_OBJ_FLAG_HIDDEN)) s_pop = 8;   /* achou: pula */
    show(s_found, found);
    show(s_radar, radar);
    show(s_msg_lbl, !wifi || fail);
    if (!wifi)     lv_label_set_text(s_msg_lbl, "Precisa do firmware 0.18 ou mais novo.");
    else if (fail) lv_label_set_text(s_msg_lbl, "Sem memória pro Wi-Fi agora. Saia e abra de novo.");

    lv_label_set_text_fmt(s_num_lbl, "%d", sealed);
    lv_label_set_text(s_hero_lbl, sealed == 1 ? "INÉDITA" : "INÉDITAS");
    int blips = s_have_scan ? (s_sess.far < BLIPS ? s_sess.far : BLIPS) : 0;
    for (int i = 0; i < BLIPS; i++) show(s_blip[i], i < blips);

    lv_label_set_text(s_place_lbl, s_have_scan ? place_caps() : "");
    if (s_have_scan && s_sess.far) {
        lv_label_set_text_fmt(s_right_lbl, "%d LONGE", s_sess.far);
        lv_obj_set_style_text_color(s_right_lbl, lv_color_hex(KIT_COLOR_YELLOW), 0);
    } else {
        if (s_have_scan) lv_label_set_text_fmt(s_right_lbl, "%d REDES", s_sess.env);
        else             lv_label_set_text(s_right_lbl, "");
        lv_obj_set_style_text_color(s_right_lbl, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    }

    lv_obj_set_style_bg_color(s_pack_btn, lv_color_hex(sealed ? KIT_COLOR_YELLOW : KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(s_pack_lbl, lv_color_hex(sealed ? KIT_COLOR_ON_YELLOW : KIT_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_color(s_pack_sub, lv_color_hex(sealed ? KIT_COLOR_ON_YELLOW : KIT_COLOR_TEXT_MUTED), 0);
    lv_label_set_text_fmt(s_album_lbl, "%d/%d", s_album ? fig_filled(s_album) : 0, FIG_SLOTS);
}

static void on_scan(kit_err_t result, const kit_wifi_ap_t *aps, size_t count, void *ud)
{
    (void)ud;
    if (!s_album) return;
    if (result != KIT_OK) {
        if (result != KIT_ERR_NOT_SUPPORTED) s_scan_err = result;
        refresh_game();
        return;
    }
    fig_clock_t clk = read_clock();
    fig_scan_out_t out;
    fig_process_scan(s_album, &s_sess, &clk, aps, count, &out);
    s_have_scan = true;
    s_scan_err = KIT_OK;
    if (out.changed) save();

    char msg[64], nm[KIT_WIFI_SSID_MAX + 12];
    if (out.new_ineditas) {
        kit_ui_sfx(KIT_SFX_CONFIRM);
    } else if (out.fiel >= 0) {
        net_name(out.fiel, nm, sizeof(nm));
        snprintf(msg, sizeof(msg), "FIEL: %s COLOU", nm);
        toast(msg);
    } else if (out.multidao >= 0) {
        toast("MULTIDÃO COLOU");
    } else if (out.full) {
        toast("ÁLBUM CHEIO");
    } else if (out.new_reps) {
        snprintf(msg, sizeof(msg), "+%d EM TODAS AS REDES", out.new_reps);
        toast(msg);
    }

    if (out.ask_place >= 0 && !s_depth) push(V_LUGAR, out.ask_place);
    if (s_depth && s_stack[s_depth - 1].v == V_PACK && s_over_right && s_rev_idx < 0)
        lv_label_set_text_fmt(s_over_right, "%d/%d", s_pack_done + 1,
                              s_pack_done + fig_sealed_count(s_album));
    refresh_game();
}

static void scan_tick(lv_timer_t *t)
{
    (void)t;
    if (s_api->wifi) s_api->wifi->scan(on_scan, NULL);   /* KIT_FAIL = ocupado: próximo tick */
}

static void fx_tick(lv_timer_t *t)
{
    (void)t;
    s_fx_tick++;
    if (s_led) lv_obj_set_style_opa(s_led, (s_fx_tick / 12) % 2 ? LV_OPA_40 : LV_OPA_COVER, 0);
    if (s_ring && !lv_obj_has_flag(s_radar, LV_OBJ_FLAG_HIDDEN)) {
        int f = s_fx_tick % 32;
        int d = 16 + f * 5;                  /* 16 -> 171 px */
        lv_obj_set_size(s_ring, d, d);
        lv_obj_set_style_opa(s_ring, (lv_opa_t)(255 - f * 7), 0);
        int near = d / 2 - BLIP_R;
        lv_opa_t bo = (near > -14 && near < 14) ? LV_OPA_COVER : LV_OPA_40;
        for (int i = 0; i < BLIPS; i++) lv_obj_set_style_opa(s_blip[i], bo, 0);
    }
    if (s_pop > 0 && s_found) {
        s_pop--;
        lv_obj_set_style_translate_y(s_found, s_pop * 5, 0);
        lv_obj_set_style_opa(s_found, (lv_opa_t)(255 - s_pop * 24), 0);
    }
}

static void game_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    if (code == 0) {
        if (!fig_sealed_count(s_album)) { kit_ui_miss(); return; }
        kit_ui_sfx(KIT_SFX_TAP);
        push(V_PACK, 0);
    } else {
        kit_ui_sfx(KIT_SFX_TAP);
        push(V_ALBUM, 0);
    }
}

/* Botão de ação em pílula, duas linhas: rótulo + detalhe. */
static lv_obj_t *pill2(lv_obj_t *parent, int w, const char *main_txt, int code,
                       lv_obj_t **main_lbl, lv_obj_t **sub_lbl, const char *sub_txt)
{
    lv_obj_t *b = kit_ui_rect(parent, w, BTN_H, KIT_COLOR_SURFACE, BTN_H / 2);
    kit_ui_tap(b, game_cb, code);
    kit_ui_flex(b, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 0, 0);
    lv_obj_t *m = kit_ui_label(b, main_txt, KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *d = kit_ui_label(b, sub_txt, KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    if (main_lbl) *main_lbl = m;
    if (sub_lbl) *sub_lbl = d;
    return b;
}

static lv_obj_t *ring(lv_obj_t *parent, int d, int width, uint32_t color)
{
    lv_obj_t *r = kit_ui_box(parent);
    lv_obj_set_size(r, d, d);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r, width, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(r, LV_OPA_COVER, 0);
    lv_obj_align(r, LV_ALIGN_CENTER, 0, 0);
    return r;
}

static void build_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *col = kit_ui_box(tile);
    lv_obj_set_size(col, KIT_UI_SCREEN_W, KIT_UI_PAGE_H);
    lv_obj_set_style_pad_left(col, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_right(col, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_bottom(col, KIT_UI_BTN_MARGIN, 0);
    kit_ui_flex(col, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, 12, 0);

    /* status: pulso verde + lugar | redes em volta / longe */
    lv_obj_t *st = kit_ui_box(col);
    lv_obj_set_size(st, KIT_UI_CONTENT, 32);
    kit_ui_flex(st, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 0);
    lv_obj_t *left = kit_ui_box(st);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(left, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_START, 0, 10);
    s_led = kit_ui_rect(left, 12, 12, ACCENT, LV_RADIUS_CIRCLE);
    s_place_lbl = kit_ui_label(left, "", KIT_COLOR_TEXT, &kit_mono_20, 3);
    s_right_lbl = kit_ui_label(st, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);

    /* centro: ocupa o que sobra; radar OU número de inéditas OU aviso */
    lv_obj_t *hero = kit_ui_box(col);
    lv_obj_set_width(hero, KIT_UI_CONTENT);
    lv_obj_set_flex_grow(hero, 1);
    kit_ui_flex(hero, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(hero, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    s_radar = kit_ui_box(hero);
    lv_obj_set_size(s_radar, RADAR, RADAR);
    ring(s_radar, RADAR / 3, 2, KIT_COLOR_LINE);
    ring(s_radar, RADAR * 2 / 3, 2, KIT_COLOR_LINE);
    ring(s_radar, RADAR, 2, KIT_COLOR_LINE);
    s_ring = ring(s_radar, 16, 3, ACCENT);
    lv_obj_center(kit_ui_rect(s_radar, 16, 16, ACCENT, LV_RADIUS_CIRCLE));
    for (int i = 0; i < BLIPS; i++) {
        s_blip[i] = kit_ui_rect(s_radar, 14, 14, KIT_COLOR_YELLOW, LV_RADIUS_CIRCLE);
        lv_obj_align(s_blip[i], LV_ALIGN_CENTER, BLIP_XY[i][0], BLIP_XY[i][1]);
        lv_obj_add_flag(s_blip[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_found = kit_ui_box(hero);
    lv_obj_set_size(s_found, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(s_found, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, 10, 0);
    lv_obj_add_flag(s_found, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    s_num_lbl = kit_ui_label(s_found, "0", KIT_COLOR_TEXT, &kit_display_120, 0);
    s_hero_lbl = kit_ui_label(s_found, "INÉDITAS", KIT_COLOR_YELLOW, &kit_mono_26, 3);
    lv_obj_add_flag(s_found, LV_OBJ_FLAG_HIDDEN);

    s_msg_lbl = kit_ui_text(hero, "", KIT_COLOR_TEXT, &kit_sans_28, KIT_UI_CONTENT);
    lv_obj_add_flag(s_msg_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ação: lado a lado, 80 px */
    lv_obj_t *btns = kit_ui_box(col);
    lv_obj_set_size(btns, KIT_UI_CONTENT, BTN_H);
    kit_ui_flex(btns, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 0);
    lv_obj_t *dummy;
    s_pack_btn = pill2(btns, CELL_W, "ABRIR", 0, &s_pack_lbl, &dummy, "PACOTE");
    lv_obj_set_style_text_color(dummy, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    s_pack_sub = dummy;
    pill2(btns, CELL_W, "ÁLBUM", 1, NULL, &s_album_lbl, "0/28");
}

/* --- AJUSTE ------------------------------------------------------------ */

static void on_name(int idx, void *user)
{
    (void)user;
    s_show_name = (idx == 0);
    if (s_api->storage) s_api->storage->set_i32("album_nome", s_show_name ? 1 : 0);
}

static void zero_cb(lv_event_t *e)
{
    (void)e;
    uint64_t t = now_ms();
    if (!s_zero_armed_ms || t - s_zero_armed_ms > 4000) {
        s_zero_armed_ms = t ? t : 1;
        kit_ui_sfx(KIT_SFX_TAP);
        lv_label_set_text(s_zero_lbl, "TOQUE DE NOVO");
        return;
    }
    s_zero_armed_ms = 0;
    lv_label_set_text(s_zero_lbl, "ZERAR ÁLBUM");
    fig_album_reset(s_album);
    memset(&s_sess, 0, sizeof(s_sess));
    s_sess.place = -1;
    save();
    kit_ui_miss();
    toast("ÁLBUM ZERADO");
    refresh_game();
}

static void build_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    kit_ui_flex(p, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, 12, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);

    kit_ui_label(p, "NOME DA REDE", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    kit_ui_chips(&s_name_chips, p, NAME_CHIPS, 2, s_show_name ? 0 : 1, ACCENT, on_name, NULL);
    lv_obj_t *gap = kit_ui_box(p);
    lv_obj_set_size(gap, 1, 12);
    kit_ui_label(p, "COMEÇAR DE NOVO", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    button(p, KIT_UI_CONTENT, KIT_COLOR_SURFACE, KIT_COLOR_RED, false, "ZERAR ÁLBUM", zero_cb, 0, &s_zero_lbl);
}

/* --- camada: cabeçalho e corpo ----------------------------------------- */

static lv_obj_t *over_begin(const char *title, const char *right)
{
    lv_obj_t *chip = kit_ui_rect(s_over, KIT_UI_CHIP, KIT_UI_CHIP, KIT_COLOR_SURFACE, 18);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, KIT_UI_PAD, 16);
    lv_obj_center(kit_ui_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    lv_obj_t *ttl = kit_ui_label(s_over, title, KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(ttl, LV_ALIGN_TOP_LEFT, KIT_UI_PAD + KIT_UI_CHIP + 12, 30);
    s_over_right = kit_ui_label(s_over, right ? right : "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    lv_obj_align(s_over_right, LV_ALIGN_TOP_RIGHT, -KIT_UI_PAD, 34);

    lv_obj_t *b = lv_obj_create(s_over);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, KIT_UI_SCREEN_W, KIT_UI_PAGE_H);
    lv_obj_set_pos(b, 0, KIT_UI_TITLEBAR);
    lv_obj_set_style_pad_left(b, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_right(b, KIT_UI_PAD, 0);
    lv_obj_set_style_pad_bottom(b, 24, 0);
    kit_ui_flex(b, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, 12, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(b, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(b, LV_SCROLLBAR_MODE_OFF);
    s_over_body = b;
    return b;
}

/* --- PACOTE ------------------------------------------------------------ */

static void env_paint(void)
{
    if (!s_env_grip) return;
    lv_obj_set_style_translate_x(s_env_grip, s_drag_dx, 0);
    lv_obj_set_width(s_env_tear, s_drag_dx + 24);
}

static void anim_stop(void)
{
    if (s_anim_timer) lv_timer_delete(s_anim_timer);
    s_anim_timer = NULL;
    s_anim = ANIM_NONE;
}

static void anim_tick(lv_timer_t *t)
{
    (void)t;
    s_anim_step++;
    if (s_anim == ANIM_OPEN) {
        if (s_env_flap) lv_obj_set_style_translate_y(s_env_flap, -s_anim_step * 8, 0);
        if (s_env_flap) lv_obj_set_style_opa(s_env_flap, (lv_opa_t)(255 - s_anim_step * 40), 0);
        if (s_anim_step < 6) return;
        anim_stop();
        int idx = fig_sealed_first(s_album);
        if (idx < 0) { pop(); return; }
        fig_reveal(s_album, idx);
        save();
        s_rev_idx = idx;
        kit_ui_sfx(fig_is_rare(&s_album->nets[idx]) ? KIT_SFX_CATALOG_DONE : KIT_SFX_REVEAL);
        defer_render();
    } else if (s_anim == ANIM_RISE) {
        int y = 64 - s_anim_step * 9;
        if (y < 0) y = 0;
        if (s_rise_obj) {
            lv_obj_set_style_translate_y(s_rise_obj, y, 0);
            lv_obj_set_style_opa(s_rise_obj, (lv_opa_t)(y ? 255 - y * 3 : 255), 0);
        }
        if (!y) anim_stop();
    }
}

static void anim_start(int kind)
{
    anim_stop();
    s_anim = kind;
    s_anim_step = 0;
    s_anim_timer = lv_timer_create(anim_tick, 30, NULL);
}

static void env_open(void)
{
    if (s_env_open) return;
    s_env_open = true;
    s_drag = false;
    lv_obj_add_flag(s_env_grip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_env_tear, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_env_hint, "");
    kit_ui_sfx(KIT_SFX_TAP);
    anim_start(ANIM_OPEN);
}

static void env_press_cb(lv_event_t *e)
{
    (void)e;
    if (s_env_open || !s_env) return;
    s_drag = true;
    s_drag_x0 = -1;
    s_drag_dx = 0;
}

static void env_release_cb(lv_event_t *e)
{
    (void)e;
    if (s_env_open) return;
    if (!s_env) return;
    bool moved = s_drag_dx > 12;
    s_drag = false;
    s_drag_dx = 0;
    env_paint();
    if (!moved) {
        lv_label_set_text(s_env_hint, "ARRASTE NA LINHA");
        lv_obj_set_style_text_color(s_env_hint, lv_color_hex(KIT_COLOR_YELLOW), 0);
    }
}

static void on_input(const kit_input_event_t *ev, void *ud)
{
    (void)ud;
    if (!s_drag || !s_env || s_env_open || ev->type != KIT_INPUT_TOUCH_DOWN) return;
    if (s_drag_x0 < 0) { s_drag_x0 = ev->x; return; }
    int dx = (int)ev->x - s_drag_x0;
    if (dx < 0) dx = 0;
    if (dx > ENV_TRAVEL) dx = ENV_TRAVEL;
    if (dx - s_drag_dx > 120) return;   /* salto de uma amostra: lixo de coordenada */
    s_drag_dx = dx;
    env_paint();
    if (dx >= ENV_TRAVEL * 9 / 10) env_open();
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    if (s_anim) return;
    s_pack_done++;
    s_rev_idx = -1;
    if (!fig_sealed_count(s_album)) {
        pop();
        toast(s_pack_done == 1 ? "FIGURINHA COLADA" : "PACOTE COLADO");
        return;
    }
    kit_ui_sfx(KIT_SFX_TAP);
    defer_render();
}

static void build_pack(void)
{
    int sealed = fig_sealed_count(s_album);
    char pos[16];
    if (s_rev_idx < 0) snprintf(pos, sizeof(pos), "%d/%d", s_pack_done + 1, s_pack_done + sealed);
    else               snprintf(pos, sizeof(pos), "%d/%d", s_pack_done + 1, s_pack_done + 1 + sealed);
    lv_obj_t *b = over_begin("PACOTE", pos);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (s_rev_idx < 0) {
        if (!sealed) { pop(); return; }
        s_env_open = false;
        s_drag = false;
        s_drag_dx = 0;
        /* A área de baixo INTEIRA é a zona de arraste (o dedo não precisa
         * achar a alça): o corpo não rola e absorve o gesto, sem chain. */
        lv_obj_set_scroll_dir(b, LV_DIR_NONE);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, env_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(b, env_release_cb, LV_EVENT_RELEASED, NULL);
        s_env = no_click(kit_ui_box(b));
        lv_obj_set_size(s_env, ENV_W, ENV_H);

        lv_obj_t *body = no_click(kit_ui_rect(s_env, ENV_W, ENV_H - FLAP_H, KIT_COLOR_SURFACE, 18));
        lv_obj_set_pos(body, 0, FLAP_H);
        lv_obj_center(kit_ui_label(body, "?", KIT_COLOR_TEXT_MUTED, &kit_display_44, 0));
        s_env_flap = no_click(kit_ui_rect(s_env, ENV_W, FLAP_H + 18, KIT_COLOR_SURFACE, 18));
        lv_obj_set_pos(s_env_flap, 0, 0);
        lv_obj_t *cut = no_click(kit_ui_rect(s_env, ENV_W, 3, KIT_COLOR_TEXT_MUTED, 0));
        lv_obj_set_pos(cut, 0, FLAP_H - 1);
        s_env_tear = no_click(kit_ui_rect(s_env, 24, 6, KIT_COLOR_YELLOW, 0));
        lv_obj_set_pos(s_env_tear, 0, FLAP_H - 2);
        s_env_grip = lv_image_create(s_env);
        no_click(s_env_grip);
        lv_image_set_src(s_env_grip, &grip_img);
        lv_obj_set_style_image_recolor(s_env_grip, lv_color_hex(KIT_COLOR_YELLOW), 0);
        lv_obj_set_style_image_recolor_opa(s_env_grip, LV_OPA_COVER, 0);
        lv_obj_set_pos(s_env_grip, 8, FLAP_H - GRIP_H / 2);

        lv_obj_t *meta = col_box(b, KIT_UI_CONTENT);
        lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        no_click(meta);
        kit_ui_label(meta, "ARRASTE PRA ABRIR", KIT_COLOR_TEXT, &kit_mono_26, 3);
        s_env_hint = kit_ui_label(meta, "DEDO PRA DIREITA", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
        return;
    }

    /* revelada: a figurinha sobe do envelope; toque em qualquer lugar segue */
    const fig_net_t *n = &s_album->nets[s_rev_idx];
    kit_ui_tap(b, next_cb, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_STATE_PRESSED);
    s_rise_obj = col_box(b, KIT_UI_CONTENT);
    lv_obj_set_flex_align(s_rise_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_rise_obj, 10, 0);
    no_click(s_rise_obj);
    lv_obj_t *st = art_sticker(s_rise_obj, s_album, s_rev_idx, ART_BIG);
    art_motion_start(st, ART_BIG, fig_is_rare(n));

    char line[48], nm[KIT_WIFI_SSID_MAX + 12];
    snprintf(line, sizeof(line), fig_is_rare(n) ? "#%03d  BRILHANTE" : "#%03d", s_rev_idx + 1);
    kit_ui_label(s_rise_obj, line, fig_is_rare(n) ? KIT_COLOR_YELLOW : KIT_COLOR_TEXT_MUTED, &kit_mono_26, 3);
    net_name(s_rev_idx, nm, sizeof(nm));
    lv_obj_t *nl = kit_ui_text(s_rise_obj, nm, KIT_COLOR_TEXT, &kit_sans_28, KIT_UI_CONTENT);
    (void)nl;

    uint8_t sl[FIG_SLOTS];
    int k = fig_slots_of(s_album, s_rev_idx, sl, FIG_SLOTS);
    char got[160];
    int o = snprintf(got, sizeof(got), "COLOU EM ");
    for (int i = 0; i < k && o < (int)sizeof(got) - 16; i++)
        o += snprintf(got + o, sizeof(got) - o, i ? ", %s" : "%s", SLOT_CAP[sl[i]]);
    lv_obj_t *gl = kit_ui_text(s_rise_obj, got, ACCENT, &kit_mono_20, KIT_UI_CONTENT);
    lv_obj_set_style_text_letter_space(gl, 2, 0);
    kit_ui_label(s_rise_obj, fig_sealed_count(s_album) ? "TOQUE PRA PRÓXIMA" : "TOQUE PRA FECHAR",
                 KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);

    lv_obj_set_style_translate_y(s_rise_obj, 64, 0);
    lv_obj_set_style_opa(s_rise_obj, LV_OPA_TRANSP, 0);
    anim_start(ANIM_RISE);
}

/* --- ÁLBUM ------------------------------------------------------------- */

static void album_row_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    kit_ui_sfx(KIT_SFX_TAP);
    if (code <= PAGE_TODAS) push(V_PAGE, code);
    else                    push(V_LUGARES, 0);
}

static lv_obj_t *row(lv_obj_t *parent, const char *label, const char *value, bool muted_label)
{
    lv_obj_t *r = kit_ui_rect(parent, KIT_UI_CONTENT, 80, KIT_COLOR_SURFACE, 18);
    lv_obj_set_style_pad_left(r, 20, 0);
    lv_obj_set_style_pad_right(r, 16, 0);
    kit_ui_flex(r, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_START, 0, 12);
    lv_obj_t *l = kit_ui_label(r, label, muted_label ? KIT_COLOR_TEXT_MUTED : KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_obj_set_flex_grow(l, 1);
    kit_ui_label(r, value, KIT_COLOR_TEXT, &kit_mono_20, 2);
    return r;
}

static void build_album(void)
{
    lv_obj_t *b = over_begin("ÁLBUM", NULL);

    lv_obj_t *top = kit_ui_box(b);
    lv_obj_set_size(top, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(top, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 0);
    lv_obj_set_style_pad_bottom(top, 8, 0);
    lv_obj_t *l = col_box(top, LV_SIZE_CONTENT);
    lv_obj_t *n1 = kit_ui_label(l, "", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_label_set_text_fmt(n1, "%d/%d", fig_filled(s_album), FIG_SLOTS);
    kit_ui_label(l, "VAGAS", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    lv_obj_t *r = col_box(top, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t *n2 = kit_ui_label(r, "", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_label_set_text_fmt(n2, "%d", s_album->nnets - fig_sealed_count(s_album));
    kit_ui_label(r, "REDES", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);

    char v[24];
    for (int p = 0; p < FIG_PAGES; p++) {
        int got = fig_page_filled(s_album, p);
        snprintf(v, sizeof(v), "%d/%d", got, FIG_PAGE_COUNT[p]);
        lv_obj_t *rw = row(b, PAGE_ROW[p], v, false);
        kit_ui_tap(rw, album_row_cb, p);
        if (got) {
            /* progresso: fio na base da linha, recuado do raio dos cantos */
            lv_obj_t *bar = no_click(kit_ui_rect(rw, 1, 4, ACCENT, 0));
            lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_width(bar, (KIT_UI_CONTENT - 36) * got / FIG_PAGE_COUNT[p]);
            lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, -2, 0);
        }
    }
    snprintf(v, sizeof(v), "%d", s_album->nnets - fig_sealed_count(s_album));
    kit_ui_tap(row(b, "Todas as redes", v, false), album_row_cb, PAGE_TODAS);
    int named = 0;
    for (int i = 0; i < FIG_MAX_PLACES; i++) if (s_album->places[i].used && s_album->places[i].name) named++;
    snprintf(v, sizeof(v), "%d", named);
    kit_ui_tap(row(b, "Lugares", v, false), album_row_cb, PAGE_TODAS + 1);
}

/* --- PÁGINA ------------------------------------------------------------ */

static void sticker_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    kit_ui_sfx(KIT_SFX_TAP);
    push(V_FICHA, idx);
}

static void more_cb(lv_event_t *e)
{
    (void)e;
    s_todas_limit += TODAS_STEP;
    kit_ui_sfx(KIT_SFX_TAP);
    defer_render();
}

/* Célula da grade: figurinha da rede `idx`, ou vaga vazia com `mark`. */
static void cell(lv_obj_t *grid, int idx, const char *cap, const char *mark)
{
    lv_obj_t *c = col_box(grid, CELL_W);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (idx >= 0) {
        art_sticker(c, s_album, idx, ART_MID);
        kit_ui_tap(c, sticker_cb, idx);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_opa(c, LV_OPA_70, LV_STATE_PRESSED);
    } else {
        art_empty(c, mark, ART_MID);
    }
    kit_ui_label(c, cap, idx >= 0 ? KIT_COLOR_TEXT : KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
}

static void build_page(int page)
{
    char right[16];
    if (page == PAGE_TODAS) snprintf(right, sizeof(right), "%d", s_album->nnets - fig_sealed_count(s_album));
    else snprintf(right, sizeof(right), "%d/%d", fig_page_filled(s_album, page), FIG_PAGE_COUNT[page]);
    lv_obj_t *b = over_begin(PAGE_TITLE[page], right);

    lv_obj_t *grid = kit_ui_box(b);
    lv_obj_set_size(grid, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 16, 0);

    if (page == PAGE_TODAS) {
        int shown = 0;
        char cap[8];
        for (int i = s_album->nnets - 1; i >= 0 && shown < s_todas_limit; i--) {
            if (s_album->nets[i].flags & FIG_F_SEALED) continue;
            snprintf(cap, sizeof(cap), "#%03d", i + 1);
            cell(grid, i, cap, NULL);
            shown++;
        }
        int total = s_album->nnets - fig_sealed_count(s_album);
        if (!total) kit_ui_text(b, "Nenhuma figurinha ainda. Abra o Álbum em lugares diferentes.",
                                KIT_COLOR_TEXT_MUTED, &kit_sans_28, KIT_UI_CONTENT);
        if (shown < total)
            button(b, KIT_UI_CONTENT, KIT_COLOR_SURFACE, KIT_COLOR_TEXT, false, "MOSTRAR MAIS", more_cb, 0, NULL);
        return;
    }

    char mark[4];
    for (int i = 0; i < FIG_PAGE_COUNT[page]; i++) {
        int slot = FIG_PAGE_FIRST[page] + i;
        int idx = fig_slot_net(s_album, slot);
        if (page == 0) snprintf(mark, sizeof(mark), "%d", i + 1);
        else           snprintf(mark, sizeof(mark), "?");
        cell(grid, idx, SLOT_CAP[slot], mark);
    }
}

/* --- FICHA ------------------------------------------------------------- */

static void spec(lv_obj_t *parent, const char *k, const char *v)
{
    lv_obj_t *r = kit_ui_box(parent);
    lv_obj_set_size(r, KIT_UI_CONTENT, LV_SIZE_CONTENT);
    kit_ui_flex(r, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, 0, 12);
    kit_ui_label(r, k, KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    kit_ui_label(r, v, KIT_COLOR_TEXT, &kit_mono_20, 2);
}

static void build_ficha(int idx)
{
    const fig_net_t *n = &s_album->nets[idx];
    char t[16];
    snprintf(t, sizeof(t), "#%03d", idx + 1);
    lv_obj_t *b = over_begin(t, NULL);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *st = art_sticker(b, s_album, idx, ART_BIG);
    art_motion_start(st, ART_BIG, fig_is_rare(n));
    char nm[KIT_WIFI_SSID_MAX + 12];
    net_name(idx, nm, sizeof(nm));
    kit_ui_text(b, nm, KIT_COLOR_TEXT, &kit_sans_28, KIT_UI_CONTENT);
    if (fig_is_rare(n)) kit_ui_label(b, "BRILHANTE", KIT_COLOR_YELLOW, &kit_mono_20, 3);

    char v[40];
    snprintf(v, sizeof(v), "%d", n->ch);
    spec(b, "CANAL", v);
    spec(b, "SEGURANÇA", AUTH_NAME[n->auth < 7 ? n->auth : 6]);
    snprintf(v, sizeof(v), "%d DBM", n->rssi);
    spec(b, "SINAL", v);
    if (n->first_day) {
        int y, m, d;
        fig_day_civil(n->first_day, &y, &m, &d);
        if (n->hour != FIG_NO_HOUR) snprintf(v, sizeof(v), "%02d %s %dH", d, MESES[m >= 1 && m <= 12 ? m : 0], n->hour);
        else                        snprintf(v, sizeof(v), "%02d %s", d, MESES[m >= 1 && m <= 12 ? m : 0]);
        spec(b, "1ª VEZ", v);
    }
    snprintf(v, sizeof(v), n->days == 1 ? "%d DIA" : "%d DIAS", n->days);
    spec(b, "VISTA EM", v);
    int nm_i = n->place ? s_album->places[n->place - 1].name : 0;
    spec(b, "LUGAR", nm_i ? FIG_PLACE_NAME[nm_i] : "PASSEIO");
    if (fig_is_hidden(n))       spec(b, "TIPO", "OCULTA");
    else if (fig_is_hotspot(n)) spec(b, "TIPO", "CELULAR");
    else if (fig_mesh_count(s_album, n->ssid) >= 2) spec(b, "TIPO", "MESH");

    uint8_t sl[FIG_SLOTS];
    int k = fig_slots_of(s_album, idx, sl, FIG_SLOTS);
    char got[160];
    int o = 0;
    got[0] = '\0';
    for (int i = 0; i < k && o < (int)sizeof(got) - 16; i++)
        o += snprintf(got + o, sizeof(got) - o, i ? ", %s" : "%s", SLOT_CAP[sl[i]]);
    lv_obj_t *cb = col_box(b, KIT_UI_CONTENT);
    lv_obj_set_style_pad_top(cb, 8, 0);
    kit_ui_label(cb, "COLADA EM", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    lv_obj_t *gl = kit_ui_label(cb, k ? got : "TODAS AS REDES", KIT_COLOR_TEXT, &kit_mono_20, 2);
    lv_label_set_long_mode(gl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(gl, KIT_UI_CONTENT);
}

/* --- LUGARES / LUGAR NOVO ---------------------------------------------- */

static void build_lugares(void)
{
    lv_obj_t *b = over_begin("LUGARES", NULL);
    char v[32];
    for (int p = 0; p < FIG_MAX_PLACES; p++) {
        const fig_place_t *pl = &s_album->places[p];
        if (!pl->used || (!pl->name && pl->days < 2)) continue;
        int c = 0;
        for (int i = 0; i < s_album->nnets; i++) if (s_album->nets[i].place == p + 1) c++;
        snprintf(v, sizeof(v), "%d · %dD", c, pl->days);
        no_click(row(b, PLACE_ROW[pl->name], v, !pl->name));
    }
    int c = 0;
    for (int i = 0; i < s_album->nnets; i++) {
        int pl = s_album->nets[i].place;
        if (!pl || !s_album->places[pl - 1].name) c++;
    }
    snprintf(v, sizeof(v), "%d", c);
    no_click(row(b, "Passeio", v, false));
    kit_ui_text(b, "Abrindo no mesmo lugar em 3 dias diferentes, o KIT pergunta o nome dele.",
                KIT_COLOR_TEXT_MUTED, &kit_sans_28, KIT_UI_CONTENT);
}

static void on_place(int idx, void *user)
{
    int p = (int)(intptr_t)user;
    s_album->places[p].name = (uint8_t)(idx + 1);
    s_album->places[p].asked = 1;
    save();
    char msg[40];
    snprintf(msg, sizeof(msg), "LUGAR SALVO: %s", FIG_PLACE_NAME[idx + 1]);
    toast(msg);
    if (s_depth) s_depth--;
    defer_render();
    refresh_game();
}

static void later_cb(lv_event_t *e) { (void)e; kit_ui_sfx(KIT_SFX_TAP); pop(); }

static void build_lugar(int p)
{
    lv_obj_t *b = over_begin("LUGAR NOVO", NULL);
    lv_obj_t *q = kit_ui_label(b, "Você sempre abre aqui. Que lugar é esse?",
                               KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(q, KIT_UI_CONTENT);
    fig_clock_t clk = read_clock();
    int sug = fig_suggest_place(s_album, &clk);
    kit_ui_chips(&s_place_chips, b, PLACE_CHIPS, FIG_PLACE_NAMES, sug ? sug - 1 : -1,
                 ACCENT, on_place, (void *)(intptr_t)p);
    button(b, KIT_UI_CONTENT, KIT_COLOR_SURFACE, KIT_COLOR_TEXT, false, "AGORA NÃO", later_cb, 0, NULL);
}

/* --- render da camada -------------------------------------------------- */

static void render_over(void)
{
    art_motion_stop();
    anim_stop();
    s_env = s_env_flap = s_env_tear = s_env_grip = s_env_hint = s_rise_obj = NULL;
    s_drag = false;
    s_over_body = s_over_right = NULL;

    if (!s_depth) {
        if (s_over) lv_obj_delete(s_over);
        s_over = NULL;
        refresh_game();
        return;
    }
    if (!s_over) {
        s_over = kit_ui_rect(s_screen, KIT_UI_SCREEN_W, KIT_UI_SCREEN_H, KIT_COLOR_BG, 0);
        lv_obj_set_pos(s_over, 0, 0);
        lv_obj_add_flag(s_over, LV_OBJ_FLAG_CLICKABLE);   /* não deixa o toque vazar pro tileview */
    } else {
        lv_obj_clean(s_over);
    }

    int v = s_stack[s_depth - 1].v, arg = s_stack[s_depth - 1].arg;
    switch (v) {
    case V_PACK:    build_pack(); break;
    case V_ALBUM:   build_album(); break;
    case V_PAGE:    build_page(arg); break;
    case V_FICHA:   build_ficha(arg); break;
    case V_LUGARES: build_lugares(); break;
    case V_LUGAR:   build_lugar(arg); break;
    default: break;
    }
}

/* --- ciclo de vida ----------------------------------------------------- */

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);
    art_bind(s_api);

    s_album = malloc(sizeof(*s_album));
    if (!s_album) return KIT_ERR_NO_MEM;
    fig_album_load(s_album, open_data);
    memset(&s_sess, 0, sizeof(s_sess));
    s_sess.place = -1;
    int32_t nome = 1;
    if (s_api->storage) s_api->storage->get_i32("album_nome", &nome);
    s_show_name = nome != 0;
    s_have_scan = false;
    s_scan_err = KIT_OK;
    s_depth = 0;
    s_rev_idx = -1;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "ÁLBUM", ACCENT, 3);
    kit_ui_shell_tiles(&s_shell, NULL, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_game(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);
    kit_ui_shell_open(&s_shell, 1);
    lv_screen_load(s_screen);

    if (s_api->input) s_api->input->register_callback(on_input, NULL);
    s_fx_timer = lv_timer_create(fx_tick, 50, NULL);
    if (s_api->wifi) {
        s_api->wifi->scan(on_scan, NULL);
        s_scan_timer = lv_timer_create(scan_tick, SCAN_MS, NULL);
    }
    refresh_game();
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    if (s_api && s_api->wifi) s_api->wifi->cancel();
    if (s_api && s_api->input) s_api->input->register_callback(NULL, NULL);
    art_motion_stop();
    anim_stop();
    if (s_scan_timer)  lv_timer_delete(s_scan_timer);
    if (s_fx_timer)    lv_timer_delete(s_fx_timer);
    if (s_defer_timer) lv_timer_delete(s_defer_timer);
    if (s_toast_timer) lv_timer_delete(s_toast_timer);
    s_scan_timer = s_fx_timer = s_defer_timer = s_toast_timer = NULL;

    if (s_screen) lv_obj_delete(s_screen);
    s_screen = s_over = s_over_body = s_over_right = s_toast = NULL;
    s_led = s_place_lbl = s_right_lbl = s_num_lbl = s_hero_lbl = s_radar = s_ring = NULL;
    s_pack_btn = s_pack_lbl = s_pack_sub = s_album_lbl = s_zero_lbl = NULL;
    s_found = s_msg_lbl = NULL;
    for (int i = 0; i < BLIPS; i++) s_blip[i] = NULL;
    s_pop = 0;
    s_env = s_env_flap = s_env_tear = s_env_grip = s_env_hint = s_rise_obj = NULL;
    memset(&s_shell, 0, sizeof(s_shell));
    memset(&s_name_chips, 0, sizeof(s_name_chips));
    memset(&s_place_chips, 0, sizeof(s_place_chips));
    s_depth = 0;
    s_drag = s_env_open = false;
    s_zero_armed_ms = 0;

    free(s_album);
    s_album = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo: a lógica vive em album.c (test_album) */

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif
