/**
 * @file art.c
 * @brief Álbum — arte da figurinha (ver art.h).
 *
 * A composição é desenhada num espaço de 96 px e escalada por SC(v) =
 * v * size / 96. Círculo e quadrado são lv_obj com raio; o triângulo é
 * máscara A8 recolorida (src/shapes.h), que existe nos lados exatos que a
 * escala produz: 52/78/104 (forma grande) e 22/33/44 (pequena).
 *
 * As formas ficam numa camada própria (s_layer) e as molduras por cima, na
 * figurinha: inclinar o KIT desloca só a camada — efeito de profundidade.
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

#define BIG_SIDE   52   /* forma grande, no espaço de 96 px */
#define SMALL_SIDE 22   /* forma pequena / trio / espalhadas */
#define SC(v)      ((v) * size / ART_SMALL)

static const kit_api_table_t *s_api;
static lv_obj_t *s_last_layer;

void art_bind(const kit_api_table_t *api) { s_api = api; }

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

/* Forma centrada em (cx, cy) com lado `side` (já escalado). */
static void shape(lv_obj_t *parent, int type, int cx, int cy, int side, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o;
    if (type == SH_TRIANGLE) {
        int ti = tri_index(side);
        o = lv_image_create(parent);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
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
    lv_obj_set_style_radius(st, SC(6), 0);

    /* camada das formas: é ela que se mexe com a inclinação */
    lv_obj_t *ly = plain(st);
    lv_obj_set_size(ly, size, size);
    s_last_layer = ly;

    const int big = SC(BIG_SIDE), small = SC(SMALL_SIDE);

    if (fig_is_hotspot(n)) {
        /* celular: formas pequenas espalhadas */
        for (int i = 0; i < 6; i++) {
            int cx = SC(18 + rnd(&seed, 61)), cy = SC(18 + rnd(&seed, 61));
            shape(ly, rnd(&seed, 3), cx, cy, small, pick_color(&seed, bg, bg), opa);
        }
    } else if (fig_mesh_count(a, n->ssid) >= 2) {
        /* mesh: trio da mesma forma + barra */
        int t = rnd(&seed, 3);
        uint32_t col = hidden ? chc : pick_color(&seed, bg, bg);
        for (int i = 0; i < 3; i++) shape(ly, t, SC(23 + 25 * i), SC(46), small, col, opa);
        rect(ly, SC(12), SC(70), SC(72), SC(4), hidden ? chc : pick_color(&seed, bg, col), opa);
    } else {
        if (rnd(&seed, 100) < 65) {
            /* barra fina de ponta a ponta, preta ou "paper" */
            uint32_t bc = hidden ? chc : (bg == KIT_COLOR_YELLOW ? 0x000000 : KIT_COLOR_TEXT);
            int pos = SC(14 + rnd(&seed, 68));
            if (rnd(&seed, 2)) rect(ly, -SC(12), pos, size + SC(24), SC(4), bc, opa);
            else               rect(ly, pos, -SC(12), SC(4), size + SC(24), bc, opa);
        }
        int t1 = rnd(&seed, 3);
        uint32_t c1 = hidden ? chc : pick_color(&seed, bg, bg);
        int cx = SC(34 + rnd(&seed, 28)), cy = SC(34 + rnd(&seed, 28));
        shape(ly, t1, cx, cy, big, c1, opa);
        int t2 = (t1 + 1 + rnd(&seed, 2)) % 3;
        uint32_t c2 = hidden ? KIT_COLOR_TEXT : pick_color(&seed, bg, c1);
        shape(ly, t2, cx < SC(48) ? SC(73) : SC(23), cy < SC(48) ? SC(73) : SC(23), small, c2, opa);
    }

    /* moldura = segurança (fica parada, por cima da camada) */
    const uint32_t fc = (bg == KIT_COLOR_YELLOW) ? 0x000000 : KIT_COLOR_TEXT;
    switch (n->auth) {
    case KIT_WIFI_AUTH_WPA:
    case KIT_WIFI_AUTH_WPA2:
        frame(st, size, SC(6), SC(3), fc);
        break;
    case KIT_WIFI_AUTH_WPA3:
        frame(st, size, SC(6), SC(3), fc);
        frame(st, size, SC(12), SC(3), fc);
        break;
    case KIT_WIFI_AUTH_ENTERPRISE:
        frame(st, size, SC(6), SC(7), fc);
        break;
    case KIT_WIFI_AUTH_WEP:
        /* picotada, como selo antigo */
        for (int i = 0; i < 6; i++) {
            int p = SC(10 + 15 * i), d = SC(5);
            rect(st, p, SC(4), d, d, fc, LV_OPA_COVER);
            rect(st, p, size - SC(9), d, d, fc, LV_OPA_COVER);
            rect(st, SC(4), p, d, d, fc, LV_OPA_COVER);
            rect(st, size - SC(9), p, d, d, fc, LV_OPA_COVER);
        }
        break;
    default:
        break;   /* aberta: sem moldura */
    }

    /* contorno: rara na grade ganha fio "paper"; oculta, fio apagado (senão
     * some no preto). Por último, pra ficar por cima da camada. */
    lv_obj_t *edge = plain(st);
    lv_obj_set_size(edge, size, size);
    lv_obj_set_style_radius(edge, SC(6), 0);
    if (fig_is_rare(n) && size < ART_BIG) {
        lv_obj_set_style_border_width(edge, 3, 0);
        lv_obj_set_style_border_color(edge, lv_color_hex(KIT_COLOR_TEXT), 0);
    } else if (hidden) {
        lv_obj_set_style_border_width(edge, SC(2), 0);
        lv_obj_set_style_border_color(edge, lv_color_hex(KIT_COLOR_LINE), 0);
    }
    return st;
}

lv_obj_t *art_empty(lv_obj_t *parent, const char *mark, int size)
{
    lv_obj_t *e = plain(parent);
    lv_obj_set_size(e, size, size);
    lv_obj_set_style_radius(e, SC(6), 0);
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

/* --- movimento: paralaxe + reflexo pela inclinação --------------------- */

#define TILT_FULL 3000   /* 30° de inclinação = deslocamento máximo */

static lv_timer_t *s_mo_timer;
static lv_obj_t   *s_mo_layer, *s_mo_gloss;
static int         s_mo_size, s_mo_x, s_mo_y, s_mo_g, s_mo_auto;

static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void motion_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_mo_layer) return;
    const int size = s_mo_size;
    int32_t tx = 0, ty = 0;
    bool ok = s_api && s_api->imu && s_api->imu->accel_tilt && s_api->imu->accel_tilt(&tx, &ty);

    if (!ok) {
        /* sem acelerômetro: a rara ganha o brilho correndo sozinho */
        if (!s_mo_gloss) return;
        s_mo_auto += 8;
        if (s_mo_auto > size * 3) s_mo_auto = -SC(40);
        lv_obj_set_style_translate_x(s_mo_gloss, s_mo_auto, 0);
        return;
    }
    tx = clamp(tx, -TILT_FULL, TILT_FULL);
    ty = clamp(ty, -TILT_FULL, TILT_FULL);

    /* a borda mais alta "empurra" as formas pro lado mais baixo */
    int gx = -tx * SC(9) / TILT_FULL, gy = -ty * SC(9) / TILT_FULL;
    s_mo_x += (gx - s_mo_x) / 3;
    s_mo_y += (gy - s_mo_y) / 3;
    lv_obj_set_style_translate_x(s_mo_layer, s_mo_x, 0);
    lv_obj_set_style_translate_y(s_mo_layer, s_mo_y, 0);

    if (s_mo_gloss) {
        /* o reflexo atravessa a figurinha conforme inclina pros lados */
        int gpos = size / 2 - SC(14) + tx * size / TILT_FULL;
        s_mo_g += (gpos - s_mo_g) / 3;
        lv_obj_set_style_translate_x(s_mo_gloss, s_mo_g, 0);
    }
}

void art_motion_start(lv_obj_t *sticker, int size, bool rare)
{
    art_motion_stop();
    s_mo_layer = s_last_layer;
    s_mo_size = size;
    s_mo_x = s_mo_y = 0;
    s_mo_auto = -SC(40);
    s_mo_g = size / 2 - SC(14);

    bool has_imu = s_api && s_api->imu && s_api->imu->accel_tilt;
    if (rare || has_imu) {
        s_mo_gloss = plain(sticker);
        lv_obj_set_size(s_mo_gloss, SC(28), size);
        lv_obj_set_pos(s_mo_gloss, 0, 0);
        lv_obj_set_style_bg_color(s_mo_gloss, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_mo_gloss, rare ? LV_OPA_40 : LV_OPA_10, 0);
        lv_obj_set_style_translate_x(s_mo_gloss, has_imu ? s_mo_g : s_mo_auto, 0);
    }
    s_mo_timer = lv_timer_create(motion_tick, 33, NULL);
}

void art_motion_stop(void)
{
    if (s_mo_timer) lv_timer_delete(s_mo_timer);
    s_mo_timer = NULL;
    s_mo_layer = NULL;   /* os objetos morrem com a figurinha */
    s_mo_gloss = NULL;
}
#endif
