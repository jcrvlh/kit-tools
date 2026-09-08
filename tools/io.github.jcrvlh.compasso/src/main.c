/**
 * @file main.c
 * @brief Compasso — treino de percepção de tempo para o KIT.
 *
 * A tela mostra um alvo em segundos (sorteado a cada rodada, 5–60 s), você
 * memoriza, toca em APAGAR e a tela fica preta. Toque de novo quando achar
 * que o tempo passou. O Compasso revela o seu erro em segundos e em
 * porcentagem e, no fim de 3 rodadas, o viés do seu "relógio interno" —
 * quanto você corta ou estica o tempo, sempre.
 *
 * Dois modos (AJUSTE):
 *  - SOLO: 3 rodadas, veredito final + top-5 salvo no aparelho (menor erro
 *    médio %, sigla de 3 letras pela roleta de arraste — kit_ui_sigla).
 *  - DUPLA: melhor de 3, os dois estimam o mesmo alvo. Na tela preta cada um
 *    toca no seu lado; uma linha verde vertical divide os campos e NÃO apaga.
 *    Ponto pro mais perto; empate não pontua.
 *
 * UI montada com a galeria tools-sdk/include/kit_ui.h (shell + tileview,
 * grade de chips, página COMO JOGA, botão de ação, seletor de sigla).
 *
 * Runtime de Tool (.so): só inteiro de 32 bits — o tempo vem do RTC em
 * milissegundos (uint64), mas só entra em subtração; a diferença é truncada
 * pra uint32 antes de qualquer divisão. Ver tool_lvgl_runtime.md.
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 * Toda a UI fica atrás de #ifndef KIT_SDK_STUBS.
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"
#include "kit_ui.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KIT_SDK_STUBS

/* --- métricas (tela 368 × 448) --------------------------------------- */
#define C_SCR_W       368
#define C_SCR_H       448
#define C_PAD         16
#define C_CONTENT     (C_SCR_W - 2 * C_PAD)            /* 336 */
#define C_TITLEBAR    88
#define C_BTN_H       76
#define C_BTN_MARGIN  18
#define C_PAGE_H      (C_SCR_H - C_TITLEBAR)           /* 360 */
#define C_STAGE_H     (C_PAGE_H - (C_BTN_H + 2 * C_BTN_MARGIN))  /* 248 */
#define C_HALF_W      (C_SCR_W / 2)                    /* 184 */

#define PAGES         4   /* AJUSTE · JOGO · COMO JOGA · MELHORES */

#define MODE_SOLO     0
#define MODE_DUPLA    1

#define ROUNDS        3
#define TGT_MIN       5
#define TGT_MAX       60

#define HS_COUNT      5
#define HS_EMPTY      9999   /* sentinela de slot vazio (menor erro % = melhor) */

#define K_MODE        "cmp_mode"
#define K_LAST        "cmp_last"
#define K_HS_SCORE    "cmp_hs"   /* + índice 0..4 */
#define K_HS_INIT     "cmp_hi"   /* + índice 0..4 */

typedef enum { ST_IDLE, ST_SHOW, ST_DARK, ST_REVEAL, ST_SUMMARY } st_t;

static const char *const MODE_LABELS[2] = { "SOLO", "DUPLA" };

/* --- estado --------------------------------------------------------- */
static const kit_api_table_t *s_api = NULL;
static uint32_t s_accent = KIT_COLOR_GREEN;

static st_t s_state = ST_IDLE;
static int  s_mode  = MODE_SOLO;

static int      s_round      = 0;        /* 0..ROUNDS-1 */
static int      s_target_s   = 0;
static uint32_t s_target_ms  = 0;
static uint64_t s_dark_start = 0;

/* SOLO — erro relativo com sinal, por rodada (negativo = cedo, "pra menos") */
static int  s_serr[ROUNDS];

/* DUPLA — ms de cada lado nesta rodada (-1 = ainda não tocou) + placar */
static int32_t s_wait_l = -1, s_wait_r = -1;
static int     s_pts_l = 0, s_pts_r = 0;

static int  s_pending_rank = -1;

typedef struct { int32_t score; char initials[4]; } hs_t;
static hs_t s_hs[HS_COUNT];
static char s_last_initials[4] = "AAA";

/* --- objetos LVGL (todos zerados em tool_destroy) ------------------- */
static lv_obj_t      *s_screen = NULL;
static kit_ui_shell_t s_shell;
static kit_ui_action_t s_action;
static kit_ui_chips_t  s_mode_chips;

/* JOGO — grupo "palco" (IDLE / SHOW / DARK / REVEAL) */
static lv_obj_t *s_play  = NULL;
static lv_obj_t *s_stage = NULL;
static lv_obj_t *s_col   = NULL;
static lv_obj_t *s_meta  = NULL;
static lv_obj_t *s_hero  = NULL;
static lv_obj_t *s_sub   = NULL;
static lv_obj_t *s_extra = NULL;
static lv_obj_t *s_divider = NULL;
static lv_obj_t *s_tap_full = NULL;
static lv_obj_t *s_tap_l = NULL;
static lv_obj_t *s_tap_r = NULL;
static lv_obj_t *s_j1 = NULL;   /* "J1" no lado esquerdo, na tela preta (DUPLA) */
static lv_obj_t *s_j2 = NULL;   /* "J2" no lado direito */

/* JOGO — grupo "resultado" (SUMMARY), rola na vertical */
static lv_obj_t *s_sum   = NULL;
static lv_obj_t *s_sum_meta = NULL;
static lv_obj_t *s_sum_hero = NULL;
static lv_obj_t *s_sum_sub  = NULL;
static lv_obj_t *s_sum_extra = NULL;
static lv_obj_t *s_sum_cap  = NULL;
static kit_ui_sigla_t s_sigla;

/* MELHORES */
static lv_obj_t *s_hs_left[HS_COUNT], *s_hs_right[HS_COUNT];

/* --- helpers ------------------------------------------------------- */
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

static void show(lv_obj_t *o, bool v)
{
    if (o) (v ? lv_obj_remove_flag : lv_obj_add_flag)(o, LV_OBJ_FLAG_HIDDEN);
}

static void sfx(kit_sfx_t s) { if (s_api && s_api->audio) s_api->audio->sfx(s); }
static void beep(uint16_t hz, uint16_t ms) { if (s_api && s_api->audio) s_api->audio->beep(hz, ms); }

static uint64_t now_ms(void)
{
    return (s_api && s_api->time) ? s_api->time->get_millis() : 0;
}

static int rnd(int lo, int hi)
{
    return (s_api && s_api->random) ? (int)s_api->random->range(lo, hi) : lo;
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* "24.3" — segundos com uma casa, a partir de ms (divisão de 32 bits) */
static void fmt_secs(char *b, size_t n, uint32_t ms)
{
    snprintf(b, n, "%u.%u", (unsigned)(ms / 1000), (unsigned)((ms % 1000) / 100));
}

/* --- persistência ------------------------------------------------- */
static void hs_defaults(void)
{
    for (int i = 0; i < HS_COUNT; i++) {
        s_hs[i].score = HS_EMPTY;
        s_hs[i].initials[0] = s_hs[i].initials[1] = s_hs[i].initials[2] = '-';
        s_hs[i].initials[3] = 0;
    }
}

static void load_prefs(void)
{
    hs_defaults();
    if (!s_api || !s_api->storage) return;
    const kit_storage_api_t *st = s_api->storage;
    char key[16];

    int32_t v;
    if (st->get_i32(K_MODE, &v) == KIT_OK && (v == MODE_SOLO || v == MODE_DUPLA))
        s_mode = (int)v;

    for (int i = 0; i < HS_COUNT; i++) {
        snprintf(key, sizeof key, "%s%d", K_HS_SCORE, i);
        if (st->get_i32(key, &v) == KIT_OK && v >= 0 && v < HS_EMPTY) s_hs[i].score = v;
        snprintf(key, sizeof key, "%s%d", K_HS_INIT, i);
        char buf[8];
        if (st->get_str(key, buf, sizeof buf) == KIT_OK && buf[0]) {
            int k = 0;
            for (; k < 3 && buf[k]; k++) s_hs[i].initials[k] = buf[k];
            for (; k < 3; k++) s_hs[i].initials[k] = '-';
            s_hs[i].initials[3] = 0;
        }
    }

    char lb[8];
    if (st->get_str(K_LAST, lb, sizeof lb) == KIT_OK) {
        bool ok = lb[0] && lb[1] && lb[2];
        for (int k = 0; ok && k < 3; k++) if (lb[k] < 'A' || lb[k] > 'Z') ok = false;
        if (ok) { s_last_initials[0] = lb[0]; s_last_initials[1] = lb[1];
                  s_last_initials[2] = lb[2]; s_last_initials[3] = 0; }
    }
}

static void save_mode(void)
{
    if (s_api && s_api->storage) s_api->storage->set_i32(K_MODE, s_mode);
}

static void hs_save_slot(int i)
{
    if (!s_api || !s_api->storage) return;
    char key[16];
    snprintf(key, sizeof key, "%s%d", K_HS_SCORE, i);
    s_api->storage->set_i32(key, s_hs[i].score);
    snprintf(key, sizeof key, "%s%d", K_HS_INIT, i);
    s_api->storage->set_str(key, s_hs[i].initials);
}

/* posição em que `score` entraria no top-5 (menor = melhor), ou -1 */
static int hs_rank_of(int score)
{
    for (int i = 0; i < HS_COUNT; i++) if (score < s_hs[i].score) return i;
    return -1;
}

static void hs_insert(int score, const char in[3])
{
    int idx = hs_rank_of(score);
    if (idx < 0) return;
    for (int i = HS_COUNT - 1; i > idx; i--) s_hs[i] = s_hs[i - 1];
    s_hs[idx].score = score;
    s_hs[idx].initials[0] = in[0];
    s_hs[idx].initials[1] = in[1];
    s_hs[idx].initials[2] = in[2];
    s_hs[idx].initials[3] = 0;
    for (int i = idx; i < HS_COUNT; i++) hs_save_slot(i);
}

/* --- MELHORES ---------------------------------------------------- */
static void sync_melhores(void)
{
    for (int i = 0; i < HS_COUNT; i++) {
        bool set = s_hs[i].score < HS_EMPTY;
        uint32_t col = set ? KIT_COLOR_TEXT : KIT_COLOR_TEXT_MUTED;
        uint32_t rcol = (i == 0 && set) ? s_accent : col;
        lv_label_set_text_fmt(s_hs_left[i], "%d  %s", i + 1, s_hs[i].initials);
        lv_obj_set_style_text_color(s_hs_left[i], lv_color_hex(col), 0);
        if (set) lv_label_set_text_fmt(s_hs_right[i], "%d%%", (int)s_hs[i].score);
        else     lv_label_set_text(s_hs_right[i], "--");
        lv_obj_set_style_text_color(s_hs_right[i], lv_color_hex(rcol), 0);
    }
}

/* --- palco: helpers de render --------------------------------------- */
static void set_hero_font(const lv_font_t *f) { if (s_hero) lv_obj_set_style_text_font(s_hero, f, 0); }

static void hide_dark_layers(void)
{
    show(s_divider, false);
    show(s_tap_full, false);
    show(s_tap_l, false);
    show(s_tap_r, false);
}

/* o palco volta ao normal: coluna centralizada, protagonista visível */
static void stage_center(void)
{
    show(s_col, true);
    show(s_hero, true);
    lv_obj_center(s_col);
}

/* --- transições de estado --------------------------------------- */
static void go_idle(void)
{
    s_state = ST_IDLE;
    show(s_play, true);
    show(s_sum, false);
    hide_dark_layers();
    stage_center();

    set_hero_font(&kit_display_72);
    lv_label_set_text(s_meta, "MODO");
    lv_label_set_text(s_hero, MODE_LABELS[s_mode]);
    lv_obj_set_style_text_color(s_hero, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_label_set_text(s_sub, "3 RODADAS / TOQUE EM COME\xC3\x87""AR");
    lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT), 0);

    if (s_hs[0].score < HS_EMPTY)
        lv_label_set_text_fmt(s_extra, "MELHOR: %s %d%%", s_hs[0].initials, (int)s_hs[0].score);
    else
        lv_label_set_text(s_extra, "SEM RECORDE AINDA");

    kit_ui_action_set(&s_action, "COME\xC3\x87""AR");
    kit_ui_action_show(&s_action, true);
    kit_ui_shell_lock(&s_shell, false);
}

static void go_show(void)
{
    s_state = ST_SHOW;
    show(s_play, true);
    show(s_sum, false);
    hide_dark_layers();
    stage_center();

    /* 1ª rodada sempre curta — um número que dá pra "sentir" de cara */
    s_target_s  = (s_round == 0) ? rnd(TGT_MIN, 20) : rnd(TGT_MIN, TGT_MAX);
    s_target_ms = (uint32_t)s_target_s * 1000u;
    s_wait_l = s_wait_r = -1;

    set_hero_font(&kit_display_120);
    lv_label_set_text_fmt(s_meta, "RODADA %d/%d", s_round + 1, ROUNDS);
    lv_label_set_text_fmt(s_hero, "%d", s_target_s);
    lv_obj_set_style_text_color(s_hero, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_label_set_text(s_sub, "SEGUNDOS / TOQUE EM APAGAR");
    lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_label_set_text(s_extra, "");

    kit_ui_action_set(&s_action, "APAGAR");
    kit_ui_action_show(&s_action, true);
    kit_ui_shell_lock(&s_shell, true);
}

static void go_dark(void)
{
    s_state = ST_DARK;
    show(s_play, true);
    show(s_sum, false);
    kit_ui_action_show(&s_action, false);
    kit_ui_shell_lock(&s_shell, true);

    s_wait_l = s_wait_r = -1;
    s_dark_start = now_ms();
    sfx(KIT_SFX_LOCK);

    /* a tela não fica vazia: a meta continua à vista (não é sobre decorar) e
       um lembrete discreto do que fazer — tudo em cinza, no topo */
    show(s_col, true);
    show(s_hero, false);
    lv_obj_align(s_col, LV_ALIGN_TOP_MID, 0, 18);
    lv_label_set_text_fmt(s_meta, "ALVO %d S", s_target_s);
    lv_label_set_text(s_sub, s_mode == MODE_SOLO ? "TOQUE PARA PARAR" : "TOQUE NO SEU LADO");
    lv_obj_set_style_text_color(s_sub, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(s_extra, "");

    if (s_j1) lv_obj_set_style_text_color(s_j1, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    if (s_j2) lv_obj_set_style_text_color(s_j2, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);

    if (s_mode == MODE_SOLO) {
        show(s_tap_full, true);
        show(s_tap_l, false);
        show(s_tap_r, false);
        show(s_divider, false);
    } else {
        show(s_tap_full, false);
        show(s_tap_l, true);
        show(s_tap_r, true);
        show(s_divider, true);   /* a linha verde NÃO apaga */
    }
}

static void go_reveal(void)
{
    s_state = ST_REVEAL;
    show(s_play, true);
    show(s_sum, false);
    hide_dark_layers();
    stage_center();
    sfx(KIT_SFX_UNLOCK);

    char a[32], b[48];
    lv_label_set_text_fmt(s_meta, "RODADA %d/%d", s_round + 1, ROUNDS);
    bool last = (s_round + 1 >= ROUNDS);

    if (s_mode == MODE_SOLO) {
        uint32_t w = (uint32_t)s_wait_l;                           /* ms */
        int dif  = (int)w - (int)s_target_ms;
        int pct  = dif * 100 / (int)s_target_ms;
        s_serr[s_round] = pct;

        /* protagonista: número puro (a fonte display não tem '.' nem '%') */
        set_hero_font(&kit_display_72);
        lv_label_set_text_fmt(s_hero, "%u S", (unsigned)((w + 500) / 1000));
        lv_obj_set_style_text_color(s_hero, lv_color_hex(KIT_COLOR_TEXT), 0);

        snprintf(b, sizeof b, "ALVO %d S  /  ERRO %+d%%", s_target_s, pct);
        lv_label_set_text(s_sub, b);
        lv_obj_set_style_text_color(s_sub,
            lv_color_hex(iabs(pct) <= 5 ? s_accent : KIT_COLOR_TEXT), 0);

        fmt_secs(a, sizeof a, w);
        lv_label_set_text_fmt(s_extra, "VOC\xC3\x8A: %s S  /  %d%% %s",
                              a, iabs(pct), pct < 0 ? "CEDO" : "TARDE");
    } else {
        int difl = s_wait_l - (int)s_target_ms;
        int difr = s_wait_r - (int)s_target_ms;
        int pctl = difl * 100 / (int)s_target_ms;
        int pctr = difr * 100 / (int)s_target_ms;
        int al = iabs(pctl), ar = iabs(pctr);

        const char *pt;
        if (al < ar)      { s_pts_l++; pt = "PONTO: J1"; }
        else if (ar < al) { s_pts_r++; pt = "PONTO: J2"; }
        else              { pt = "EMPATE"; }

        set_hero_font(&kit_display_72);
        lv_label_set_text_fmt(s_hero, "%d - %d", s_pts_l, s_pts_r);
        lv_obj_set_style_text_color(s_hero, lv_color_hex(KIT_COLOR_TEXT), 0);

        lv_label_set_text(s_sub, pt);
        lv_obj_set_style_text_color(s_sub,
            lv_color_hex(al == ar ? KIT_COLOR_TEXT : s_accent), 0);

        lv_label_set_text_fmt(s_extra, "J1 %+d%%   J2 %+d%%", pctl, pctr);
    }

    kit_ui_action_set(&s_action, last ? "RESULTADO" : "PR\xC3\x93XIMA");
    kit_ui_action_show(&s_action, true);
}

static void go_summary(void)
{
    s_state = ST_SUMMARY;
    show(s_play, false);
    show(s_sum, true);
    hide_dark_layers();

    if (s_mode == MODE_SOLO) {
        int sa = 0, ss = 0;
        for (int i = 0; i < ROUNDS; i++) { sa += iabs(s_serr[i]); ss += s_serr[i]; }
        int avg_abs = sa / ROUNDS;
        int avg_sig = ss / ROUNDS;
        bool certeiro = iabs(avg_sig) <= 3;

        lv_label_set_text(s_sum_meta, "SEU REL\xC3\x93GIO");
        /* display_120 não tem '%'; a fonte 44 tem tudo e ainda lê como número-herói */
        lv_obj_set_style_text_font(s_sum_hero, &kit_display_44, 0);
        lv_label_set_text_fmt(s_sum_hero, "%+d%%", avg_sig);
        lv_label_set_text(s_sum_sub, certeiro ? "REL\xC3\x93GIO CERTEIRO"
            : avg_sig < 0 ? "VOC\xC3\x8A CORTA O TEMPO"
                          : "VOC\xC3\x8A ESTICA O TEMPO");

        lv_label_set_text_fmt(s_sum_extra, "ERRO M\xC3\x89""DIO %d%%   /   %+d  %+d  %+d",
                              avg_abs, s_serr[0], s_serr[1], s_serr[2]);

        s_pending_rank = hs_rank_of(avg_abs);
        bool qual = s_pending_rank >= 0;
        kit_ui_sigla_show(&s_sigla, qual);
        if (qual) {
            lv_label_set_text(s_sum_cap, s_pending_rank == 0 ? "MELHOR REL\xC3\x93GIO!" : "ENTROU NO TOP 5!");
            kit_ui_sigla_set(&s_sigla, s_last_initials);
            kit_ui_action_set(&s_action, "SALVAR");
        } else if (s_hs[0].score < HS_EMPTY) {
            lv_label_set_text_fmt(s_sum_cap, "MELHOR: %s %d%%", s_hs[0].initials, (int)s_hs[0].score);
            kit_ui_action_set(&s_action, "JOGAR DE NOVO");
        } else {
            lv_label_set_text(s_sum_cap, "");
            kit_ui_action_set(&s_action, "JOGAR DE NOVO");
        }
    } else {
        kit_ui_sigla_show(&s_sigla, false);
        s_pending_rank = -1;

        lv_label_set_text(s_sum_meta, "RESULTADO");
        lv_obj_set_style_text_font(s_sum_hero, &kit_display_72, 0);
        lv_label_set_text_fmt(s_sum_hero, "%d - %d", s_pts_l, s_pts_r);
        if (s_pts_l > s_pts_r)      lv_label_set_text(s_sum_sub, "J1 VENCEU");
        else if (s_pts_r > s_pts_l) lv_label_set_text(s_sum_sub, "J2 VENCEU");
        else                        lv_label_set_text(s_sum_sub, "EMPATE");
        lv_label_set_text(s_sum_extra, "MELHOR DE 3");
        lv_label_set_text(s_sum_cap, "");
        kit_ui_action_set(&s_action, "JOGAR DE NOVO");
    }

    kit_ui_action_show(&s_action, true);
    kit_ui_shell_lock(&s_shell, true);
}

/* --- fluxo -------------------------------------------------------- */
static void start_match(void)
{
    s_round = 0;
    s_pts_l = s_pts_r = 0;
    for (int i = 0; i < ROUNDS; i++) s_serr[i] = 0;
    go_show();
}

static void dark_tap(bool left)
{
    if (s_state != ST_DARK) return;
    uint32_t el = (uint32_t)(now_ms() - s_dark_start);

    if (s_mode == MODE_SOLO) {
        s_wait_l = (int32_t)el;
        go_reveal();
        return;
    }

    if (left) {
        if (s_wait_l >= 0) return;
        s_wait_l = (int32_t)el;
    } else {
        if (s_wait_r >= 0) return;
        s_wait_r = (int32_t)el;
    }
    beep(1500, 25);
    if (s_wait_l >= 0 && s_wait_r >= 0) go_reveal();
}

/* --- callbacks --------------------------------------------------- */
static void action_cb(lv_event_t *e)
{
    (void)e;
    switch (s_state) {
    case ST_IDLE:
        sfx(KIT_SFX_CLICK);
        start_match();
        break;
    case ST_SHOW:
        go_dark();
        break;
    case ST_REVEAL:
        sfx(KIT_SFX_CLICK);
        s_round++;
        if (s_round < ROUNDS) go_show();
        else                  go_summary();
        break;
    case ST_SUMMARY:
        if (s_mode == MODE_SOLO && s_pending_rank >= 0) {
            const char *sig = kit_ui_sigla_get(&s_sigla);
            int sa = 0;
            for (int i = 0; i < ROUNDS; i++) sa += iabs(s_serr[i]);
            char in[3] = { sig[0], sig[1], sig[2] };
            hs_insert(sa / ROUNDS, in);
            s_last_initials[0] = sig[0]; s_last_initials[1] = sig[1];
            s_last_initials[2] = sig[2]; s_last_initials[3] = 0;
            if (s_api && s_api->storage) s_api->storage->set_str(K_LAST, s_last_initials);
            sync_melhores();
            sfx(KIT_SFX_CONFIRM);
        } else {
            sfx(KIT_SFX_CLICK);
        }
        go_idle();
        break;
    default:
        break;
    }
}

static void tap_full_cb(lv_event_t *e) { (void)e; dark_tap(true); }
static void tap_l_cb(lv_event_t *e)    { (void)e; dark_tap(true); }
static void tap_r_cb(lv_event_t *e)    { (void)e; dark_tap(false); }

static void on_touch(const kit_input_event_t *ev, void *u)
{
    (void)u;
    kit_ui_sigla_feed_touch(&s_sigla, ev);
}

static void on_mode(int i, void *u)
{
    (void)u;
    s_mode = (i == MODE_DUPLA) ? MODE_DUPLA : MODE_SOLO;
    save_mode();
    if (s_state == ST_IDLE) go_idle();
}

static void on_page(int page, void *u)
{
    (void)u;
    if (page == 3) sync_melhores();
}

/* --- construção da tela ---------------------------------------- */
static void build_ajuste(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, C_PAD, 0);
    lv_obj_set_style_pad_right(p, C_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 24, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(p, 10, 0);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "MODO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    kit_ui_chips(&s_mode_chips, p, MODE_LABELS, 2, s_mode, s_accent, on_mode, NULL);

    lv_obj_t *hint = add_label(p,
        "SOLO: 3 rodadas, veredito do seu rel\xC3\xB3gio interno e top-5 salvo no "
        "aparelho.\n\n"
        "DUPLA: melhor de 3, cada um toca no seu lado da tela preta. A linha "
        "verde do meio n\xC3\xA3o apaga.",
        KIT_COLOR_TEXT_MUTED, &kit_sans_22, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, C_CONTENT);
}

static lv_obj_t *make_tap(lv_obj_t *parent, int w, int x, lv_event_cb_t cb)
{
    lv_obj_t *o = plain_box(parent);
    lv_obj_set_size(o, w, C_STAGE_H);
    lv_obj_set_pos(o, x, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static void build_play(lv_obj_t *tile)
{
    s_play = plain_box(tile);
    lv_obj_set_size(s_play, lv_pct(100), lv_pct(100));

    s_stage = plain_box(s_play);
    lv_obj_set_size(s_stage, C_SCR_W, C_STAGE_H);
    lv_obj_set_pos(s_stage, 0, 0);

    s_col = plain_box(s_stage);
    lv_obj_set_size(s_col, C_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_col, 10, 0);
    lv_obj_center(s_col);

    s_meta = add_label(s_col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
    s_hero = add_label(s_col, "", KIT_COLOR_TEXT, &kit_display_72, 0);
    lv_obj_set_width(s_hero, C_CONTENT);
    lv_obj_set_style_text_align(s_hero, LV_TEXT_ALIGN_CENTER, 0);

    s_sub = add_label(s_col, "", KIT_COLOR_TEXT, &kit_mono_20, 1);
    lv_label_set_long_mode(s_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_sub, C_CONTENT);
    lv_obj_set_style_text_align(s_sub, LV_TEXT_ALIGN_CENTER, 0);

    s_extra = add_label(s_col, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_set_width(s_extra, C_CONTENT);
    lv_obj_set_style_text_align(s_extra, LV_TEXT_ALIGN_CENTER, 0);

    /* linha divisória do modo DUPLA — verde, no centro, NÃO apaga */
    s_divider = plain_box(s_stage);
    lv_obj_set_size(s_divider, 2, C_STAGE_H);
    lv_obj_set_pos(s_divider, C_HALF_W - 1, 0);
    lv_obj_set_style_bg_color(s_divider, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_divider, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_divider, LV_OBJ_FLAG_HIDDEN);

    /* camadas de toque da tela preta (criadas por último = ficam por cima) */
    s_tap_full = make_tap(s_stage, C_SCR_W, 0, tap_full_cb);
    s_tap_l    = make_tap(s_stage, C_HALF_W, 0, tap_l_cb);
    s_tap_r    = make_tap(s_stage, C_HALF_W, C_HALF_W, tap_r_cb);

    /* rótulo do lado de cada jogador, na metade inferior (não colide com a
       meta, que fica no topo) — só aparecem com o tap panel do modo DUPLA */
    s_j1 = add_label(s_tap_l, "J1", KIT_COLOR_TEXT_MUTED, &kit_display_44, 0);
    lv_obj_align(s_j1, LV_ALIGN_BOTTOM_MID, 0, -40);
    s_j2 = add_label(s_tap_r, "J2", KIT_COLOR_TEXT_MUTED, &kit_display_44, 0);
    lv_obj_align(s_j2, LV_ALIGN_BOTTOM_MID, 0, -40);
}

static void build_summary(lv_obj_t *tile)
{
    /* rola a partir do topo (align START) — com CENTER, conteúdo mais alto
       que a área útil fica com o topo cortado e sem como rolar até ele */
    s_sum = lv_obj_create(tile);
    lv_obj_remove_style_all(s_sum);
    lv_obj_set_size(s_sum, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_top(s_sum, 14, 0);
    lv_obj_set_style_pad_bottom(s_sum, C_BTN_H + C_BTN_MARGIN + 16, 0);
    lv_obj_set_style_pad_row(s_sum, 8, 0);
    lv_obj_set_flex_flow(s_sum, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sum, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_sum, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_sum, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_sum, LV_OBJ_FLAG_HIDDEN);

    s_sum_meta = add_label(s_sum, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 3);

    s_sum_hero = add_label(s_sum, "", KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_set_width(s_sum_hero, C_CONTENT);
    lv_obj_set_style_text_align(s_sum_hero, LV_TEXT_ALIGN_CENTER, 0);

    s_sum_sub = add_label(s_sum, "", s_accent, &kit_sans_28, 0);
    lv_label_set_long_mode(s_sum_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_sum_sub, C_CONTENT);
    lv_obj_set_style_text_align(s_sum_sub, LV_TEXT_ALIGN_CENTER, 0);

    s_sum_extra = add_label(s_sum, "", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    lv_obj_set_width(s_sum_extra, C_CONTENT);
    lv_obj_set_style_text_align(s_sum_extra, LV_TEXT_ALIGN_CENTER, 0);

    s_sum_cap = add_label(s_sum, "", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_set_width(s_sum_cap, C_CONTENT);
    lv_obj_set_style_text_align(s_sum_cap, LV_TEXT_ALIGN_CENTER, 0);

    kit_ui_sigla(&s_sigla, s_sum, s_accent, NULL, NULL);
    kit_ui_sigla_scroll_lock(&s_sigla, s_sum, LV_DIR_VER);
    kit_ui_sigla_scroll_lock(&s_sigla, s_shell.tv, LV_DIR_HOR);
}

static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    build_play(tile);
    build_summary(tile);
    kit_ui_action_button(&s_action, tile, s_accent, action_cb);
}

static const char RULES[] =
    "O Compasso mede o qu\xC3\xA3o certo voc\xC3\xAA sente o tempo passar.\n\n"
    "1. A cada rodada aparece um alvo em segundos (a 1\xC2\xAA sempre curta, at\xC3\xA9 "
    "20 s; as outras v\xC3\xA3o at\xC3\xA9 60).\n\n"
    "2. Toque em APAGAR. A tela escurece — mas a meta continua \xC3\xA0 vista, "
    "n\xC3\xA3o \xC3\xA9 sobre decorar.\n\n"
    "3. Toque de novo quando achar que o tempo passou. O Compasso mostra o "
    "seu tempo real e o erro em porcentagem.\n\n"
    "4. Depois de 3 rodadas, o veredito: quanto voc\xC3\xAA corta ou estica o "
    "tempo, em m\xC3\xA9""dia. Consist\xC3\xAAncia importa mais que acerto — quem "
    "erra sempre o mesmo tanto s\xC3\xB3 compensa.\n\n"
    "DUPLA (AJUSTE): melhor de 3, mesmo alvo pros dois. Na tela escura cada um "
    "toca no seu lado (J1 \xC3\xA0 esquerda, J2 \xC3\xA0 direita); a linha verde do "
    "meio n\xC3\xA3o apaga. Ponto pro mais perto, empate n\xC3\xA3o pontua.\n\n"
    "SOLO: entrou no top-5 (menor erro m\xC3\xA9""dio)? Arraste pra cima ou pra "
    "baixo em cada caixa pra girar a letra, ou toque pra avan\xC3\xA7""ar. "
    "Toque em SALVAR.";

static void build_melhores(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, C_PAD, 0);
    lv_obj_set_style_pad_right(p, C_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 24, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(p, 10, 0);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "MELHORES REL\xC3\x93GIOS", KIT_COLOR_TEXT, &kit_mono_26, 3);
    add_label(p, "SOLO / MENOR ERRO M\xC3\x89""DIO", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);

    for (int i = 0; i < HS_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(p);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_set_style_pad_right(row, 16, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        s_hs_left[i]  = add_label(row, "1  ---", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
        s_hs_right[i] = add_label(row, "--", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    }
}

/* --- ciclo de vida --------------------------------------------- */
KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    kit_ui_bind(s_api);

    s_state = ST_IDLE;
    s_mode  = MODE_SOLO;
    s_round = 0;
    s_pts_l = s_pts_r = 0;
    s_wait_l = s_wait_r = -1;
    s_pending_rank = -1;
    load_prefs();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    kit_ui_shell_begin(&s_shell, s_screen, "COMPASSO", s_accent, PAGES);
    kit_ui_shell_tiles(&s_shell, on_page, NULL);
    build_ajuste(s_shell.tiles[0]);
    build_page_game(s_shell.tiles[1]);
    kit_ui_help_page(s_shell.tiles[2], "COMO JOGA", RULES);
    build_melhores(s_shell.tiles[3]);

    sync_melhores();
    if (s_api->input) s_api->input->register_callback(on_touch, NULL);

    lv_obj_update_layout(s_screen);
    kit_ui_shell_open(&s_shell, 1);   /* abre no JOGO */
    go_idle();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    if (s_api && s_api->input) s_api->input->register_callback(NULL, NULL);
    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_shell      = (kit_ui_shell_t){0};
    s_action     = (kit_ui_action_t){0};
    s_mode_chips = (kit_ui_chips_t){0};
    s_sigla      = (kit_ui_sigla_t){0};

    s_play = s_stage = s_col = NULL;
    s_meta = s_hero = s_sub = s_extra = NULL;
    s_divider = s_tap_full = s_tap_l = s_tap_r = s_j1 = s_j2 = NULL;
    s_sum = s_sum_meta = s_sum_hero = s_sum_sub = s_sum_extra = s_sum_cap = NULL;
    for (int i = 0; i < HS_COUNT; i++) { s_hs_left[i] = NULL; s_hs_right[i] = NULL; }
    s_api = NULL;
}

#else /* KIT_SDK_STUBS — build nativo (CI / teste de lógica, sem UI) */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Compasso stub] tool_init — UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}
KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
