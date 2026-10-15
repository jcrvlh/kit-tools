/**
 * @file art.c
 * @brief Álbum — arte da figurinha (ver art.h).
 *
 * Círculo e quadrado são lv_obj com raio; o triângulo é máscara A8 recolorida
 * (src/shapes.h, tamanhos fixos). As medidas são da figurinha pequena (96 px)
 * e multiplicadas por u = 1 ou 2 — por isso os triângulos existem em pares
 * 52/104 e 22/44.
 */

#include "art.h"

#ifndef KIT_SDK_STUBS
#include "kit_theme.h"
#include "kit_fonts.h"
#include "shapes.h"
#include <string.h>

/* Primeira constante global alinhada (ver o README do hello_sd). */
static const uint32_t PALETTE[5] __attribute__((aligned(4))) = {
    KIT_COLOR_TEXT, 0x000000, KIT_COLOR_RED, KIT_COLOR_YELLOW, KIT_COLOR_BLUE
};

enum { SH_CIRCLE = 0, SH_SQUARE, SH_TRIANGLE };

#define BIG_SIDE   52   /* forma grande, em px da figurinha pequena */
#define SMALL_SIDE 22   /* forma pequena / trio / espalhadas */

static uint32_t rng_next(uint32_t *s)
{
    *s += 0x6D2B79F5u;
    uint32_t t = *s;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}
static int rnd(uint32_t *s, int n) { return (int)(rng_next(s) % (uint32_t)n); }

static uint32_t channel_color(uint8_t ch)
{
    if (ch <= 4) return KIT_COLOR_RED;
    if (ch <= 8) return KIT_COLOR_YELLOW;
    return KIT_COLOR_BLUE;
}

/* Cor da paleta diferente de `a` e de `b`. */
static uint32_t pick_color(uint32_t *s, uint32_t a, uint32_t b)
{
    uint32_t opts[5];
    int n = 0;
    for (int i = 0; i < 5; i++)
        if (PALETTE[i] != a && PALETTE[i] != b) opts[n++] = PALETTE[i];
    return opts[rnd(s, n)];
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static int tri_index(int side)
{
    for (int i = 0; i < TRI_SIZES; i++) if (TRI_SIDE[i] == side) return i;
    return 0;
}

/* Forma centrada em (cx, cy) com lado `side` (já multiplicado por u). */
static void shape(lv_obj_t *parent, int type, int cx, int cy, int side, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o;
    if (type == SH_TRIANGLE) {
        int ti = tri_index(side);
        o = lv_image_create(parent);
        lv_image_set_src(o, TRI_IMG[ti]);
        lv_obj_set_style_image_recolor(o, lv_color_hex(color), 0);
        lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_pos(o, cx - side / 2, cy - TRI_H[ti] / 2);
    } else {
        o = plain(parent);
        lv_obj_set_size(o, side, side);
        lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        if (type == SH_CIRCLE) lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(o, cx - side / 2, cy - side / 2);
    }
    if (opa != LV_OPA_COVER) lv_obj_set_style_opa(o, opa, 0);
}

static void frame(lv_obj_t *parent, int size, int inset, int width, uint32_t color)
{
    lv_obj_t *f = plain(parent);
    lv_obj_set_size(f, size - 2 * inset, size - 2 * inset);
    lv_obj_set_pos(f, inset, inset);
    lv_obj_set_style_border_width(f, width, 0);
    lv_obj_set_style_border_color(f, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(f, LV_OPA_COVER, 0);
}

static void rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *d = plain(parent);
    lv_obj_set_size(d, w, h);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    if (opa != LV_OPA_COVER) lv_obj_set_style_opa(d, opa, 0);
}

lv_obj_t *art_sticker(lv_obj_t *parent, const fig_album_t *a, int idx, int size)
{
    const fig_net_t *n = &a->nets[idx];
    const int u = size / ART_SMALL;
    uint32_t seed = fig_bssid_hash(n->bssid);
    const bool hidden = fig_is_hidden(n);
    const uint32_t chc = channel_color(n->ch);
    const uint32_t bg = hidden ? 0x000000 : chc;
    /* oculta = fantasma: formas translúcidas na cor do canal, sobre preto */
    const lv_opa_t opa = hidden ? LV_OPA_50 : LV_OPA_COVER;

    lv_obj_t *st = plain(parent);
    lv_obj_set_size(st, size, size);
    lv_obj_set_style_bg_color(st, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(st, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(st, 6 * u, 0);
    /* rara na grade: fio "paper" na borda (o brilho que corre fica no pacote e
     * na ficha). Oculta sem isso: fio apagado, senão some no preto. */
    if (fig_is_rare(n) && size == ART_SMALL) {
        lv_obj_set_style_border_width(st, 2, 0);
        lv_obj_set_style_border_color(st, lv_color_hex(KIT_COLOR_TEXT), 0);
    } else if (hidden) {
        lv_obj_set_style_border_width(st, 2 * u, 0);
        lv_obj_set_style_border_color(st, lv_color_hex(KIT_COLOR_LINE), 0);
    }

    const int big = BIG_SIDE * u, small = SMALL_SIDE * u;

    if (fig_is_hotspot(n)) {
        /* celular: formas pequenas espalhadas */
        for (int i = 0; i < 6; i++) {
            int cx = (18 + rnd(&seed, 61)) * u, cy = (18 + rnd(&seed, 61)) * u;
            shape(st, rnd(&seed, 3), cx, cy, small, pick_color(&seed, bg, bg), opa);
        }
    } else if (fig_mesh_count(a, n->ssid) >= 2) {
        /* mesh: trio da mesma forma + barra */
        int t = rnd(&seed, 3);
        uint32_t col = hidden ? chc : pick_color(&seed, bg, bg);
        for (int i = 0; i < 3; i++) shape(st, t, (23 + 25 * i) * u, 46 * u, small, col, opa);
        rect(st, 12 * u, 70 * u, 72 * u, 4 * u, hidden ? chc : pick_color(&seed, bg, col), opa);
    } else {
        if (rnd(&seed, 100) < 65) {
            /* barra fina de ponta a ponta, preta ou "paper" */
            uint32_t bc = hidden ? chc : (bg == KIT_COLOR_YELLOW ? 0x000000 : KIT_COLOR_TEXT);
            int pos = (14 + rnd(&seed, 68)) * u;
            if (rnd(&seed, 2)) rect(st, 0, pos, size, 4 * u, bc, opa);
            else               rect(st, pos, 0, 4 * u, size, bc, opa);
        }
        int t1 = rnd(&seed, 3);
        uint32_t c1 = hidden ? chc : pick_color(&seed, bg, bg);
        int cx = (34 + rnd(&seed, 28)) * u, cy = (34 + rnd(&seed, 28)) * u;
        shape(st, t1, cx, cy, big, c1, opa);
        int t2 = (t1 + 1 + rnd(&seed, 2)) % 3;
        uint32_t c2 = hidden ? KIT_COLOR_TEXT : pick_color(&seed, bg, c1);
        shape(st, t2, cx < 48 * u ? 73 * u : 23 * u, cy < 48 * u ? 73 * u : 23 * u, small, c2, opa);
    }

    /* moldura = segurança */
    const uint32_t fc = (bg == KIT_COLOR_YELLOW) ? 0x000000 : KIT_COLOR_TEXT;
    switch (n->auth) {
    case KIT_WIFI_AUTH_WPA:
    case KIT_WIFI_AUTH_WPA2:
        frame(st, size, 6 * u, 3 * u, fc);
        break;
    case KIT_WIFI_AUTH_WPA3:
        frame(st, size, 6 * u, 3 * u, fc);
        frame(st, size, 12 * u, 3 * u, fc);
        break;
    case KIT_WIFI_AUTH_ENTERPRISE:
        frame(st, size, 6 * u, 7 * u, fc);
        break;
    case KIT_WIFI_AUTH_WEP:
        /* picotada, como selo antigo */
        for (int i = 0; i < 6; i++) {
            int p = (10 + 15 * i) * u, d = 5 * u;
            rect(st, p, 4 * u, d, d, fc, LV_OPA_COVER);
            rect(st, p, size - 9 * u, d, d, fc, LV_OPA_COVER);
            rect(st, 4 * u, p, d, d, fc, LV_OPA_COVER);
            rect(st, size - 9 * u, p, d, d, fc, LV_OPA_COVER);
        }
        break;
    default:
        break;   /* aberta: sem moldura */
    }
    return st;
}

lv_obj_t *art_empty(lv_obj_t *parent, const char *mark)
{
    lv_obj_t *e = plain(parent);
    lv_obj_set_size(e, ART_SMALL, ART_SMALL);
    lv_obj_set_style_radius(e, 6, 0);
    lv_obj_set_style_border_width(e, 3, 0);
    lv_obj_set_style_border_color(e, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_set_style_border_opa(e, LV_OPA_COVER, 0);
    lv_obj_t *l = lv_label_create(e);
    lv_label_set_text(l, mark);
    lv_obj_set_style_text_font(l, &kit_display_44, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    lv_obj_center(l);
    return e;
}

/* --- brilho da rara -------------------------------------------------------- */

static lv_timer_t *s_sheen_timer;
static lv_obj_t   *s_sheen_bar;
static int         s_sheen_x, s_sheen_size;

static void sheen_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_sheen_bar) return;
    s_sheen_x += 8;
    if (s_sheen_x > s_sheen_size * 3) s_sheen_x = -48;   /* passa e descansa */
    lv_obj_set_style_translate_x(s_sheen_bar, s_sheen_x, 0);
}

void art_sheen_start(lv_obj_t *sticker, int size)
{
    art_sheen_stop();
    s_sheen_size = size;
    s_sheen_x = -48;
    s_sheen_bar = plain(sticker);
    lv_obj_set_size(s_sheen_bar, 28, size);
    lv_obj_set_pos(s_sheen_bar, 0, 0);
    lv_obj_set_style_bg_color(s_sheen_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_sheen_bar, LV_OPA_30, 0);
    lv_obj_set_style_translate_x(s_sheen_bar, s_sheen_x, 0);
    s_sheen_timer = lv_timer_create(sheen_tick, 30, NULL);
}

void art_sheen_stop(void)
{
    if (s_sheen_timer) lv_timer_delete(s_sheen_timer);
    s_sheen_timer = NULL;
    s_sheen_bar = NULL;   /* o objeto morre com a figurinha */
}
#endif
