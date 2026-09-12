/**
 * @file main.c
 * @brief Soundbox — mesa de sons de mão para o KIT (Tool do catálogo).
 *
 * Grade 3x3 de pads; cada toque toca um .wav de um banco no cartão microSD.
 * Um som novo corta o anterior (retrigger, sem polifonia), no ritmo de uma
 * soundbox de zoeira.
 *
 *   BANCOS  [0]  volume (stepper) + lista dos conjuntos em /sdcard/soundbox/<x>/
 *   PADS    [1]  o palco: a grade 3x3 (abre aqui)
 *   ADICIONAR SONS [2]  passo a passo + QR do conversor + créditos dos exemplos
 *
 * Os bancos são varridos do cartão a cada abertura (a Tool nunca escreve no
 * cartão). O banco escolhido persiste em storage ("sb_bank"); o volume global
 * do KIT fica em "sb_vol".
 *
 * Nasceu built-in (componente kit_soundbox) e migrou para o catálogo quando o
 * runtime 0.8.0 passou a exportar opendir/readdir/stat/fopen para as Tools.
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* --------------------------------------------------------------------------
 * Layout (368 x 448)
 * -------------------------------------------------------------------------- */
#define SB_W          368
#define SB_H          448
#define X_PAD         16
#define X_CONTENT     (SB_W - 2 * X_PAD)          /* 336 */
#define X_CHIP        56
#define X_TITLEBAR    88
#define X_PAGE_H      (SB_H - X_TITLEBAR)         /* 360 */
#define PAGES         3

#define SB_ROOT       "/sdcard/soundbox"
#define SB_CONVERTER_URL "https://jcrvlh.github.io/kit/soundbox.html"

#define SB_MAX_BANKS   12
#define SB_MAX_PADS    9
#define SB_DIR_LEN     40
#define SB_BASE_LEN    96      /* caminho completo da pasta do banco */
#define SB_NAME_LEN    28
#define SB_FILE_LEN    64
#define SB_LABEL_LEN   18
#define SB_JSON_MAX    4096

#define SB_EXAMPLE_DIR "Exemplo"   /* rótulo do banco embutido no .kit */

#define K_BANK        "sb_bank"
#define K_VOL         "sb_vol"
#define VOL_DEFAULT   75

#define FLASH_MS      140     /* brilho do pad ao tocar */

/* --------------------------------------------------------------------------
 * Modelo (compilado nos dois alvos — não usa LVGL nem cartão)
 * -------------------------------------------------------------------------- */
typedef struct {
    char     file[SB_FILE_LEN];       /* "buzina.wav" */
    char     label[SB_LABEL_LEN];     /* "BUZINA" */
    uint32_t color;                   /* já resolvida (herda a do banco) */
} sb_pad_t;

typedef struct {
    char     dir[SB_DIR_LEN];         /* nome curto (subpasta ou "Exemplo") — chave de persistência */
    char     base[SB_BASE_LEN];       /* caminho absoluto da pasta com os .wav */
    char     name[SB_NAME_LEN];       /* nome de exibição */
    uint32_t color;                   /* cor do banco */
    bool     builtin;                 /* veio do .kit (assets), não do /soundbox do usuário */
    int      npads;
    sb_pad_t pads[SB_MAX_PADS];
} sb_bank_t;

/* Cópia truncada e terminada. */
static void sb_copy(char *dst, const char *src, size_t n)
{
    if (n == 0) return;
    size_t i = 0;
    for (; src && src[i] && i < n - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* CAIXA ALTA, só ASCII a-z (rótulo de pad, fonte mono). */
static void sb_upcase(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
}

static uint32_t sb_color_from_name(const char *s, uint32_t fallback)
{
    if (!s) return fallback;
    if (strcasecmp(s, "vermelho") == 0 || strcasecmp(s, "red") == 0)    return KIT_COLOR_RED;
    if (strcasecmp(s, "azul") == 0     || strcasecmp(s, "blue") == 0)   return KIT_COLOR_BLUE;
    if (strcasecmp(s, "amarelo") == 0  || strcasecmp(s, "yellow") == 0) return KIT_COLOR_YELLOW;
    if (strcasecmp(s, "verde") == 0    || strcasecmp(s, "green") == 0)  return KIT_COLOR_GREEN;
    return fallback;
}

static bool sb_ends_wav(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && strcasecmp(n + l - 4, ".wav") == 0;
}

/* "buzina.wav" -> "BUZINA" */
static void sb_label_from_file(const char *file, char *out)
{
    size_t l = strlen(file);
    if (l > 4 && strcasecmp(file + l - 4, ".wav") == 0) l -= 4;
    size_t j = 0;
    for (size_t i = 0; i < l && j < SB_LABEL_LEN - 1; i++) {
        char c = file[i];
        if (c == '_' || c == '-') c = ' ';
        out[j++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    out[j] = '\0';
}

/* --------------------------------------------------------------------------
 * banco.json — parser mínimo (o .so não tem cJSON)
 *
 * Schema (tudo opcional):
 *   { "nome": "...", "cor": "verde",
 *     "pads": [ { "arquivo": "x.wav", "rotulo": "X", "cor": "vermelho" } ] }
 * -------------------------------------------------------------------------- */
static const char *j_skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

/* p aponta para '"'. Copia a string (des-escapa \" \\ \/ \n \t) para out
   (out/cap podem ser NULL/0 — aí só anda até depois das aspas de fecho). */
static const char *j_str(const char *p, const char *end, char *out, size_t cap)
{
    size_t o = 0;
    if (p >= end || *p != '"') { if (cap) out[0] = '\0'; return p; }
    p++;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char e = *p++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                default:  c = e;    break;   /* " \ / e o resto: literal */
            }
        }
        if (cap && o + 1 < cap) out[o++] = c;
    }
    if (p < end) p++;   /* fecha aspas */
    if (cap) out[o] = '\0';
    return p;
}

/* p aponta para o início de um valor. Retorna o ponteiro logo após o valor. */
static const char *j_skip_value(const char *p, const char *end)
{
    p = j_skip_ws(p, end);
    if (p >= end) return p;
    if (*p == '"') return j_str(p, end, NULL, 0);
    if (*p == '{' || *p == '[') {
        char open = *p, close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (p < end) {
            char c = *p;
            if (c == '"') { p = j_str(p, end, NULL, 0); continue; }
            if (c == open)  depth++;
            if (c == close) { depth--; p++; if (depth == 0) return p; continue; }
            p++;
        }
        return p;
    }
    /* número / true / false / null */
    while (p < end && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    return p;
}

/* [p,end) começa logo após '{'. Retorna ponteiro para o valor da chave, ou NULL. */
static const char *j_obj_find(const char *p, const char *end, const char *key)
{
    p = j_skip_ws(p, end);
    while (p < end && *p != '}') {
        char k[40];
        p = j_str(p, end, k, sizeof k);
        p = j_skip_ws(p, end);
        if (p < end && *p == ':') p++;
        p = j_skip_ws(p, end);
        const char *val = p;
        if (strcmp(k, key) == 0) return val;
        p = j_skip_value(p, end);
        p = j_skip_ws(p, end);
        if (p < end && *p == ',') p++;
        p = j_skip_ws(p, end);
    }
    return NULL;
}

#ifndef KIT_SDK_STUBS

#include <dirent.h>
#include <sys/stat.h>

/* --------------------------------------------------------------------------
 * Estado
 * -------------------------------------------------------------------------- */
static const kit_api_table_t *s_api = NULL;
static char       s_assets[SB_BASE_LEN] = "";   /* <data_path>/assets — banco embutido */

static uint32_t   s_accent   = KIT_COLOR_GREEN;
static sb_bank_t  s_banks[SB_MAX_BANKS];
static int        s_nbanks   = 0;
static int        s_cur      = -1;     /* banco na grade, -1 = nenhum */
static int        s_last_pad = -1;     /* último pad tocado (chacoalhar/PWR retoca) */
static int        s_vol      = VOL_DEFAULT;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];
static lv_obj_t *s_bank_lbl = NULL;
static lv_obj_t *s_grid = NULL;
static lv_obj_t *s_pad_btn[SB_MAX_PADS];
static lv_obj_t *s_vol_lbl = NULL;
static lv_timer_t *s_flash_timer = NULL;
static int         s_flash_pad = -1;

static void rebuild_grid(void);

/* --------------------------------------------------------------------------
 * Helpers de UI
 * -------------------------------------------------------------------------- */
static lv_obj_t *add_label(lv_obj_t *parent, const char *txt, uint32_t color,
                           const lv_font_t *font, int letter_space)
{
    lv_obj_t *l = lv_label_create(parent);
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

static lv_obj_t *scroll_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(o, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_AUTO);
    return o;
}

static uint32_t on_color(uint32_t bg)
{
    return (bg == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

/* --------------------------------------------------------------------------
 * Scan do cartão
 * -------------------------------------------------------------------------- */
static bool pad_has_file(const sb_bank_t *b, const char *file)
{
    for (int i = 0; i < b->npads; i++)
        if (strcasecmp(b->pads[i].file, file) == 0) return true;
    return false;
}

static bool file_exists(const sb_bank_t *b, const char *file)
{
    char p[SB_BASE_LEN + SB_FILE_LEN + 2];
    snprintf(p, sizeof(p), "%s/%s", b->base, file);
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* Lê o banco.json (se houver) e preenche name/color/pads na ordem dele. */
static void load_banco_json(sb_bank_t *b)
{
    char path[SB_BASE_LEN + 16];
    snprintf(path, sizeof(path), "%s/banco.json", b->base);
    FILE *f = fopen(path, "rb");
    if (!f) return;

    static char buf[SB_JSON_MAX];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return;
    buf[n] = '\0';
    const char *end = buf + n;

    const char *root = j_skip_ws(buf, end);
    if (root >= end || *root != '{') return;
    root++;

    char tmp[SB_NAME_LEN];
    const char *v = j_obj_find(root, end, "nome");
    if (v && *v == '"') {
        j_str(v, end, tmp, sizeof tmp);
        if (tmp[0]) sb_copy(b->name, tmp, sizeof b->name);
    }

    v = j_obj_find(root, end, "cor");
    if (v && *v == '"') {
        char c[16];
        j_str(v, end, c, sizeof c);
        b->color = sb_color_from_name(c, b->color);
    }

    v = j_obj_find(root, end, "pads");
    if (!v || *v != '[') return;
    const char *p = j_skip_ws(v + 1, end);
    while (p < end && *p != ']') {
        if (*p != '{') break;
        const char *obj_start = p + 1;
        const char *obj_end = j_skip_value(p, end);   /* logo após o '}' */
        p = obj_end;

        if (b->npads < SB_MAX_PADS) {
            char arq[SB_FILE_LEN] = "";
            const char *a = j_obj_find(obj_start, obj_end, "arquivo");
            if (a && *a == '"') j_str(a, obj_end, arq, sizeof arq);

            if (arq[0] && file_exists(b, arq) && !pad_has_file(b, arq)) {
                sb_pad_t *pad = &b->pads[b->npads++];
                sb_copy(pad->file, arq, sizeof pad->file);

                char rot[SB_LABEL_LEN] = "";
                const char *r = j_obj_find(obj_start, obj_end, "rotulo");
                if (r && *r == '"') j_str(r, obj_end, rot, sizeof rot);
                if (rot[0]) { sb_copy(pad->label, rot, sizeof pad->label); sb_upcase(pad->label); }
                else        sb_label_from_file(pad->file, pad->label);

                char pc[16] = "";
                const char *c = j_obj_find(obj_start, obj_end, "cor");
                if (c && *c == '"') j_str(c, obj_end, pc, sizeof pc);
                pad->color = pc[0] ? sb_color_from_name(pc, b->color) : b->color;
            }
        }

        p = j_skip_ws(p, end);
        if (p < end && *p == ',') p++;
        p = j_skip_ws(p, end);
    }
}

/* banco.json primeiro (ordem dele), depois todo *.wav restante em ordem
 * alfabética, teto de 9. */
static void load_bank(sb_bank_t *b)
{
    b->npads = 0;
    b->color = s_accent;
    sb_copy(b->name, b->dir, sizeof b->name);

    load_banco_json(b);

    DIR *d = opendir(b->base);
    if (!d) return;

    static char names[SB_MAX_PADS * 3][SB_FILE_LEN];
    int nn = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && nn < (int)(sizeof(names) / sizeof(names[0]))) {
        char nm[SB_FILE_LEN];
        sb_copy(nm, e->d_name, sizeof nm);
        if (nm[0] == '.') continue;                 /* ._resource forks do macOS */
        if (!sb_ends_wav(nm)) continue;
        if (pad_has_file(b, nm)) continue;
        bool dup = false;                           /* FatFS pode repetir entrada */
        for (int k = 0; k < nn; k++)
            if (strcasecmp(names[k], nm) == 0) { dup = true; break; }
        if (dup) continue;
        sb_copy(names[nn++], nm, SB_FILE_LEN);
    }
    closedir(d);

    for (int i = 0; i < nn - 1; i++)
        for (int j = 0; j < nn - 1 - i; j++)
            if (strcasecmp(names[j], names[j + 1]) > 0) {
                char t[SB_FILE_LEN];
                sb_copy(t, names[j], sizeof t);
                sb_copy(names[j], names[j + 1], SB_FILE_LEN);
                sb_copy(names[j + 1], t, SB_FILE_LEN);
            }

    for (int i = 0; i < nn && b->npads < SB_MAX_PADS; i++) {
        sb_pad_t *pad = &b->pads[b->npads++];
        sb_copy(pad->file, names[i], sizeof pad->file);
        sb_label_from_file(pad->file, pad->label);
        pad->color = b->color;
    }
}

/* Banco embutido no .kit: <data_path>/assets/, sempre o primeiro da lista. */
static void scan_builtin_bank(void)
{
    if (!s_assets[0]) return;
    sb_bank_t *b = &s_banks[s_nbanks];
    memset(b, 0, sizeof(*b));
    sb_copy(b->dir, SB_EXAMPLE_DIR, sizeof b->dir);
    sb_copy(b->base, s_assets, sizeof b->base);
    b->builtin = true;
    load_bank(b);
    if (b->npads > 0) s_nbanks++;
}

/* Bancos do usuário: cada subpasta de /sdcard/soundbox/. */
static void scan_user_banks(void)
{
    DIR *d = opendir(SB_ROOT);
    if (!d) {
        printf("[Soundbox] sem %s (sem cartao ou sem a pasta)\n", SB_ROOT);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && s_nbanks < SB_MAX_BANKS) {
        if (e->d_name[0] == '.') continue;
        char name[SB_DIR_LEN];
        sb_copy(name, e->d_name, sizeof name);
        char p[SB_BASE_LEN];
        snprintf(p, sizeof(p), SB_ROOT "/%s", name);
        struct stat st;
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        sb_bank_t *b = &s_banks[s_nbanks];
        memset(b, 0, sizeof(*b));
        sb_copy(b->dir, name, sizeof b->dir);
        sb_copy(b->base, p, sizeof b->base);
        load_bank(b);
        if (b->npads > 0) s_nbanks++;   /* banco vazio não entra na lista */
    }
    closedir(d);
}

static void scan_banks(void)
{
    s_nbanks = 0;
    scan_builtin_bank();
    scan_user_banks();
    printf("[Soundbox] %d banco(s) (%s embutido)\n",
           s_nbanks, (s_nbanks && s_banks[0].builtin) ? "com" : "sem");
}

/* --------------------------------------------------------------------------
 * Tocar
 * -------------------------------------------------------------------------- */
static void flash_restore(void)
{
    if (s_flash_pad >= 0 && s_flash_pad < SB_MAX_PADS && s_pad_btn[s_flash_pad] &&
        s_cur >= 0 && s_flash_pad < s_banks[s_cur].npads) {
        lv_obj_set_style_bg_color(s_pad_btn[s_flash_pad],
            lv_color_hex(s_banks[s_cur].pads[s_flash_pad].color), 0);
    }
    s_flash_pad = -1;
}

static void flash_end_cb(lv_timer_t *t)
{
    (void)t;
    flash_restore();
    s_flash_timer = NULL;   /* repeat_count esgotou — o LVGL deleta sozinho */
}

static void play_pad(int i)
{
    if (s_cur < 0 || i < 0 || i >= s_banks[s_cur].npads) return;
    const sb_bank_t *b = &s_banks[s_cur];

    char path[SB_BASE_LEN + SB_FILE_LEN + 2];
    snprintf(path, sizeof(path), "%s/%s", b->base, b->pads[i].file);
    if (s_api && s_api->audio && s_api->audio->play_sample) s_api->audio->play_sample(path);
    s_last_pad = i;

    if (i < SB_MAX_PADS && s_pad_btn[i]) {
        if (s_flash_timer) { lv_timer_delete(s_flash_timer); s_flash_timer = NULL; }
        flash_restore();
        s_flash_pad = i;
        lv_obj_set_style_bg_color(s_pad_btn[i], lv_color_hex(KIT_COLOR_TEXT), 0);
        s_flash_timer = lv_timer_create(flash_end_cb, FLASH_MS, NULL);
        lv_timer_set_repeat_count(s_flash_timer, 1);
    }
}

static void replay_last(void)
{
    if (s_last_pad >= 0) play_pad(s_last_pad);
}

static void on_shake(void *ud) { (void)ud; replay_last(); }

/* --------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------- */
static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_api && s_api->system) s_api->system->exit();
}

static void pad_cb(lv_event_t *e)
{
    play_pad((int)(intptr_t)lv_event_get_user_data(e));
}

/* Persiste o banco pelo nome curto (índice muda quando muda o nº de bancos). */
static void save_cur_bank(void)
{
    if (s_api && s_api->storage && s_cur >= 0 && s_cur < s_nbanks)
        s_api->storage->set_str(K_BANK, s_banks[s_cur].dir);
}

static int find_bank(const char *dir)
{
    for (int i = 0; i < s_nbanks; i++)
        if (strcasecmp(s_banks[i].dir, dir) == 0) return i;
    return -1;
}

static void bank_pick_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_nbanks) return;
    s_cur = idx;
    s_last_pad = -1;
    save_cur_bank();
    rebuild_grid();
    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_ON);
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    int act = 0;
    lv_obj_t *t = lv_tileview_get_tile_active(s_tv);
    for (int i = 0; i < PAGES; i++) if (s_tiles[i] == t) act = i;
    for (int i = 0; i < PAGES; i++)
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(i == act ? s_accent : KIT_COLOR_LINE), 0);
}

/* --------------------------------------------------------------------------
 * Grade de pads (página 1)
 * -------------------------------------------------------------------------- */
static void rebuild_grid(void)
{
    if (!s_grid) return;
    lv_obj_clean(s_grid);
    for (int i = 0; i < SB_MAX_PADS; i++) s_pad_btn[i] = NULL;

    if (s_cur < 0) {
        lv_label_set_text(s_bank_lbl, "NENHUM BANCO");
        lv_obj_set_style_text_color(s_bank_lbl, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
        lv_obj_t *msg = add_label(s_grid,
            "Crie um banco no conversor e copie a pasta\n"
            "pra /soundbox na raiz do cartao:\n\n"
            "jcrvlh.github.io/kit/soundbox.html",
            KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(msg, X_CONTENT);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(msg);
        return;
    }

    const sb_bank_t *b = &s_banks[s_cur];
    lv_label_set_text(s_bank_lbl, b->name);
    lv_obj_set_style_text_color(s_bank_lbl, lv_color_hex(b->color), 0);

    const int gap = 8;
    const int cols = 3;
    const int cell_w = (X_CONTENT - gap * (cols - 1)) / cols;   /* 106 */
    const int rows = 3;
    int gh = lv_obj_get_height(s_grid);
    if (gh < 120) gh = X_PAGE_H - 72;      /* layout ainda não resolvido */
    const int cell_h = (gh - gap * (rows - 1)) / rows;

    for (int i = 0; i < b->npads && i < SB_MAX_PADS; i++) {
        lv_obj_t *pad = lv_obj_create(s_grid);
        lv_obj_remove_style_all(pad);
        lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(pad, cell_w, cell_h);
        lv_obj_set_pos(pad, (i % cols) * (cell_w + gap), (i / cols) * (cell_h + gap));
        lv_obj_set_style_bg_color(pad, lv_color_hex(b->pads[i].color), 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pad, 16, 0);
        lv_obj_set_style_pad_all(pad, 6, 0);
        lv_obj_set_ext_click_area(pad, 4);
        /* SHORT_CLICKED: dispara na soltura só se NÃO houve arraste — deslizar
           pra trocar de página não toca o som sem querer. */
        lv_obj_add_event_cb(pad, pad_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *l = add_label(pad, b->pads[i].label, on_color(b->pads[i].color),
                                &kit_mono_20, 1);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, cell_w - 12);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);

        s_pad_btn[i] = pad;
    }
}

static void build_page_pads(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = plain_box(tile);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, X_PAD, 0);
    lv_obj_set_style_pad_right(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 12, 0);
    lv_obj_set_style_pad_bottom(p, 16, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(p, 10, 0);

    s_bank_lbl = add_label(p, "", s_accent, &kit_mono_20, 2);
    lv_label_set_long_mode(s_bank_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_bank_lbl, X_CONTENT);
    lv_obj_set_style_text_align(s_bank_lbl, LV_TEXT_ALIGN_CENTER, 0);

    s_grid = plain_box(p);
    lv_obj_set_width(s_grid, X_CONTENT);
    lv_obj_set_flex_grow(s_grid, 1);
}

/* --------------------------------------------------------------------------
 * Volume — mexe no volume do KIT ao vivo e persiste em storage. Fica no topo
 * da página BANCOS.
 * -------------------------------------------------------------------------- */
static void apply_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_vol = v;
    if (s_api && s_api->audio && s_api->audio->set_volume) s_api->audio->set_volume((uint8_t)v);
    if (s_api && s_api->storage) s_api->storage->set_i32(K_VOL, v);
    if (s_vol_lbl) lv_label_set_text_fmt(s_vol_lbl, "%d%%", v);
}

static void vol_step_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    apply_volume(s_vol + delta);
    if (s_api && s_api->audio && s_api->audio->beep) s_api->audio->beep(1320, 45);  /* prévia */
}

static void make_vol_btn(lv_obj_t *parent, const char *sym, int delta)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 66, 66);
    lv_obj_set_style_bg_color(b, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 12);
    lv_obj_add_event_cb(b, vol_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)delta);
    lv_obj_center(add_label(b, sym, KIT_COLOR_TEXT, &kit_display_44, 0));
}

static void make_volume_block(lv_obj_t *p)
{
    lv_obj_t *vttl = add_label(p, "VOLUME", s_accent, &kit_mono_16, 2);
    lv_obj_set_width(vttl, X_CONTENT);

    lv_obj_t *vr = plain_box(p);
    lv_obj_set_size(vr, X_CONTENT, 84);
    lv_obj_set_style_pad_bottom(vr, 10, 0);
    lv_obj_set_flex_flow(vr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(vr, 22, 0);
    make_vol_btn(vr, "-", -10);
    s_vol_lbl = add_label(vr, "", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_set_width(s_vol_lbl, 150);
    lv_label_set_long_mode(s_vol_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_vol_lbl, LV_TEXT_ALIGN_CENTER, 0);
    make_vol_btn(vr, "+", 10);
    lv_label_set_text_fmt(s_vol_lbl, "%d%%", s_vol);
}

/* --------------------------------------------------------------------------
 * Página 0 (esquerda) — volume + seletor de bancos
 * -------------------------------------------------------------------------- */
static void build_page_banks(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = scroll_box(tile);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, X_PAD, 0);
    lv_obj_set_style_pad_right(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 18, 0);
    lv_obj_set_style_pad_bottom(p, 44, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(p, 14, 0);

    make_volume_block(p);

    lv_obj_t *hdr = add_label(p, "BANCOS", s_accent, &kit_mono_16, 2);
    lv_obj_set_width(hdr, X_CONTENT);
    lv_obj_set_style_pad_top(hdr, 12, 0);

    if (s_nbanks == 0) {
        lv_obj_t *m = add_label(p,
            "Nenhum banco ainda.\n\n"
            "Deslize pro lado (ADICIONAR SONS) pra ver como por sons no KIT.",
            KIT_COLOR_TEXT, &kit_sans_22, 0);
        lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(m, X_CONTENT);
        return;
    }

    for (int i = 0; i < s_nbanks; i++) {
        lv_obj_t *chip = lv_obj_create(p);
        lv_obj_remove_style_all(chip);
        lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(chip, X_CONTENT, 80);
        lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 16, 0);
        lv_obj_set_style_pad_left(chip, 16, 0);
        lv_obj_set_style_pad_right(chip, 16, 0);
        lv_obj_set_style_border_side(chip, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(chip, 5, 0);
        lv_obj_set_style_border_color(chip, lv_color_hex(s_banks[i].color), 0);
        lv_obj_set_ext_click_area(chip, 6);
        lv_obj_add_event_cb(chip, bank_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *nm = add_label(chip, s_banks[i].name, KIT_COLOR_TEXT, &kit_sans_22, 0);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(nm, X_CONTENT - 40);

        char sub[40];
        snprintf(sub, sizeof(sub), "%d SOM%s%s", s_banks[i].npads,
                 s_banks[i].npads == 1 ? "" : "S",
                 s_banks[i].builtin ? " - DO APP" : "");
        add_label(chip, sub, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    }
}

/* --------------------------------------------------------------------------
 * Página 2 (direita) — como adicionar sons + créditos dos exemplos
 * -------------------------------------------------------------------------- */
static const char ADD_STEPS[] =
    "A Soundbox ja vem com o banco EXEMPLO. Pra por os seus:\n\n"
    "1. Aponte a camera no codigo abaixo pra abrir o conversor.\n\n"
    "2. Solte seus audios la - MP3, WAV, o que tiver. De um nome e "
    "uma cor pra cada pad.\n\n"
    "3. O conversor deixa cada audio no formato que o KIT toca e "
    "todos no mesmo volume, e avisa se algum nao serve.\n\n"
    "4. Baixe a pasta pronta - ou grave direto no cartao com o KIT "
    "em Ajustes > Modo pen drive.\n\n"
    "5. A pasta vai pra /soundbox na raiz do cartao. Cada pasta "
    "dentro de /soundbox e um banco aqui na Soundbox.";

/* Sons do banco EXEMPLO — todos do Pixabay (pixabay.com). */
static const char CREDITS[] =
    "Todos do Pixabay (pixabay.com):\n\n"
    "Coins, Magic - Game Studio\n"
    "Goblin, WOW - freesound_community\n"
    "Crickets - Alex\n"
    "Horn - Universfield\n"
    "Fah! - JohnnyBacon156";

static kit_ui_qr_t s_conv_qr;

static void build_page_add(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = scroll_box(tile);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, X_PAD, 0);
    lv_obj_set_style_pad_right(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 18, 0);
    lv_obj_set_style_pad_bottom(p, 44, 0);
    lv_obj_set_style_pad_row(p, 18, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *ttl = add_label(p, "ADICIONAR SONS", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_set_width(ttl, X_CONTENT);

    lv_obj_t *steps = add_label(p, ADD_STEPS, KIT_COLOR_TEXT, &kit_sans_22, 0);
    lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(steps, X_CONTENT);

    lv_obj_t *qwrap = plain_box(p);
    lv_obj_set_size(qwrap, X_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(qwrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qwrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(qwrap, 6, 0);

    // QR do conversor no padrão do KIT: toque expande em tela cheia com o
    // brilho no máximo (kit_ui_qr, tools-sdk/include/kit_ui.h).
    kit_ui_qr(&s_conv_qr, qwrap, SB_CONVERTER_URL);

    lv_obj_t *sep = add_label(p, "SONS DE EXEMPLO", s_accent, &kit_mono_16, 2);
    lv_obj_set_width(sep, X_CONTENT);
    lv_obj_set_style_pad_top(sep, 12, 0);

    lv_obj_t *cr = add_label(p, CREDITS, KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);
    lv_label_set_long_mode(cr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cr, X_CONTENT);
}

/* --------------------------------------------------------------------------
 * Titlebar + tileview
 * -------------------------------------------------------------------------- */
static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, X_CHIP, X_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, X_PAD, 16);
    lv_obj_center(add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    lv_obj_t *title = add_label(s_screen, "SOUNDBOX", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, X_PAD + X_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -X_PAD, 40);
    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        s_dots[i] = d;
    }
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, SB_W, X_PAGE_H);
    lv_obj_set_pos(s_tv, 0, X_TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);
    build_page_banks(s_tiles[0]);
    build_page_pads(s_tiles[1]);
    build_page_add(s_tiles[2]);
}

/* --------------------------------------------------------------------------
 * Ciclo de vida
 * -------------------------------------------------------------------------- */
KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);
    printf("[Soundbox] tool_init (id=%s)\n", ctx->tool_id);

    s_accent = KIT_COLOR_GREEN;
    s_last_pad = -1;

    /* banco EXEMPLO embutido: os .wav vêm do .kit, extraídos em
       <data_path>/assets/ pelo instalador. Sem data_path -> só bancos do cartão. */
    s_assets[0] = '\0';
    if (ctx->data_path && ctx->data_path[0])
        snprintf(s_assets, sizeof s_assets, "%s/assets", ctx->data_path);

    s_vol = VOL_DEFAULT;
    if (s_api->storage) {
        int32_t v;
        if (s_api->storage->get_i32(K_VOL, &v) == KIT_OK && v >= 0 && v <= 100) s_vol = (int)v;
    }
    if (s_api->audio && s_api->audio->set_volume) s_api->audio->set_volume((uint8_t)s_vol);

    scan_banks();

    s_cur = (s_nbanks > 0) ? 0 : -1;
    if (s_api->storage) {
        char saved[SB_DIR_LEN];
        if (s_api->storage->get_str(K_BANK, saved, sizeof saved) == KIT_OK && saved[0]) {
            int idx = find_bank(saved);
            if (idx >= 0) s_cur = idx;
        }
    }

    if (s_api->imu) s_api->imu->register_shake_callback(on_shake, NULL);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_tileview();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   /* abre nos PADS */
    tv_changed_cb(NULL);

    lv_obj_update_layout(s_screen);
    rebuild_grid();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Soundbox] tool_destroy\n");
    if (s_flash_timer) { lv_timer_delete(s_flash_timer); s_flash_timer = NULL; }
    s_flash_pad = -1;
    if (s_api && s_api->audio && s_api->audio->stop_sample) s_api->audio->stop_sample();
    if (s_api && s_api->imu) s_api->imu->register_shake_callback(NULL, NULL);

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }
    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < SB_MAX_PADS; i++) s_pad_btn[i] = NULL;
    s_bank_lbl = s_grid = s_vol_lbl = NULL;
    s_nbanks = 0;
    s_cur = -1;
    s_last_pad = -1;
    s_assets[0] = '\0';
    kit_ui_qr_reset(&s_conv_qr);
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo: exercita o parser de banco.json */

#include <assert.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    const char *j =
        "{ \"nome\": \"Meus Sons\", \"cor\": \"verde\","
        "  \"pads\": [ { \"arquivo\": \"buzina.wav\", \"rotulo\": \"BUZINA\", \"cor\": \"vermelho\" },"
        "             { \"arquivo\": \"tada.wav\" } ] }";
    const char *end = j + strlen(j);
    const char *root = j_skip_ws(j, end) + 1;

    char nome[SB_NAME_LEN];
    const char *v = j_obj_find(root, end, "nome");
    j_str(v, end, nome, sizeof nome);
    assert(strcmp(nome, "Meus Sons") == 0);

    v = j_obj_find(root, end, "cor");
    char cor[16];
    j_str(v, end, cor, sizeof cor);
    assert(sb_color_from_name(cor, 0) == KIT_COLOR_GREEN);

    v = j_obj_find(root, end, "pads");
    assert(v && *v == '[');
    const char *p = j_skip_ws(v + 1, end);
    const char *o0 = p + 1;
    const char *o0end = j_skip_value(p, end);
    char arq[SB_FILE_LEN];
    j_str(j_obj_find(o0, o0end, "arquivo"), o0end, arq, sizeof arq);
    assert(strcmp(arq, "buzina.wav") == 0);
    assert(j_obj_find(o0, o0end, "rotulo") != NULL);

    printf("[Soundbox stub] parser de banco.json OK\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
