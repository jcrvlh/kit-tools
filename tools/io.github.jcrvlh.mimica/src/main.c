/**
 * @file main.c
 * @brief Mímica — mini-jogo de mesa "atue em silêncio, o time adivinha".
 *
 * Uma pessoa segura o KIT, vê a palavra/frase grande no centro (categoria
 * opcional acima) e a representa por gestos. Acertou: toque em ACERTOU;
 * travou: PULAR. Quando o TEMPO acaba, o KIT mostra quantas ela fez — quem
 * valida cada palpite é a mesa (filosofia do Bingo).
 *
 * Tela: titlebar fixa + lv_tileview de 3 páginas (AJUSTE / JOGO / COMO JOGA,
 * começa no JOGO) + barra de tempo + botões ACERTOU / PULAR no rodapé; overlay
 * TEMPO com o placar da vez.
 *
 * O chacoalhar NÃO é ligado nesta Tool (quem atua gesticula — viraria ACERTOU
 * acidental). Toda a UI atrás de #ifndef KIT_SDK_STUBS —
 * ver tools-sdk/docs/tool_lvgl_runtime.md.
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 */

#include "kit_tool_api.h"
#include "kit_theme.h"
#include "kit_fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KIT_SDK_STUBS

#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

// Mímica — linguagem "Brutalist Bauhaus" (ver docs/design/design-language.md).
// Mesma estrutura e ajustes da Veto (io.github.jcrvlh.veto), adaptada: aqui a
// pessoa ATUA por gestos (sem falar) e o time adivinha — não há palavras
// proibidas nem botão de falta, só ACERTOU e PULAR.
//
// Filosofia do Bingo: o KIT sorteia (as cartas) e cronometra; a mesa confere.
// O placar da vez (acertos/pulos) fica ESCONDIDO enquanto ela corre e é
// revelado no overlay de TEMPO — nada de placar acumulado nem times.
//
// 3 páginas, espelhando a Veto:
//   0 AJUSTE   — TEMPO (60S/90S/120S/OFF) · CATEGORIA (MOSTRA/ESCONDE) ·
//                BARALHO (FÁCIL/TUDO) · PULOS (1/2/3/LIVRES/OFF) · REINICIAR
//                BARALHO (dois toques). Tudo em Storage (mi_tempo/mi_categ/
//                mi_deck/mi_pulos).
//   1 JOGO     — a barra de tempo azul drenando, o kicker (categoria / ATUE),
//                a palavra grande (kit_display_44, ou kit_sans_28 se longa) e a
//                linha de ações PULAR + ACERTOU.
//   2 COMO JOGA — as regras, corpo rolável.
//
// Sem chacoalhar (kit_tool_manager desliga): quem atua gesticula muito e viraria
// acerto acidental. O palco NÃO é tocável.
//
// Animação zero: só troca de texto por carta. Nada de transform_scale/rotation
// nem opa intermediário em container — força layer buffer e estoura o render no
// CO5300/PSRAM. A barra de tempo é lv_obj_set_width numa caixa, a 5 Hz.


// ---------------------------------------------------------------------------
// Layout (métricas da Veto)
// ---------------------------------------------------------------------------
#define X_PAD        16
#define X_CONTENT    (KIT_DISPLAY_WIDTH - 2 * X_PAD)              // 336
#define X_TITLEBAR   88
#define X_PAGE_H     (KIT_DISPLAY_HEIGHT - X_TITLEBAR)            // 360
#define X_CHIP       56
#define PAGES        3

#define ACT_H        74
#define ACT_MARGIN   14
#define STAGE_H      (X_PAGE_H - (ACT_MARGIN + ACT_H + 12))       // 260

#define CLOCK_TICK_MS   200
#define TICK_LEAD_S     5      // tique nos últimos N s
#define WARN_LEAD_S     10     // barra pisca / relógio acende nos últimos N s
#define ALARM_REPEAT_MS 3400   // re-toca o alarme no overlay de TEMPO
#define RESET_ARM_MS    4000   // REINICIAR BARALHO: janela do 2º toque
#define PREP_SECS       3      // "PREPARE-SE 3 · 2 · 1" antes do relógio arrancar

#define K_TEMPO  "mi_tempo"
#define K_CATEG  "mi_categ"
#define K_DECK   "mi_deck"
#define K_PULOS  "mi_pulos"

// TEMPO da vez (segundos; 0 = OFF/livre).
static const int  TEMPO_S[4]          = { 60, 90, 120, 0 };
static const char *const TEMPO_LBL[4] = { "60S", "90S", "120S", "OFF" };
static const char *const CATEG_LBL[2] = { "MOSTRA", "ESCONDE" };
static const char *const DECK_LBL[2]  = { "F\xC3\x81""CIL", "TUDO" };
static const int  SKIP_LIM[5]         = { 1, 2, 3, -1, 0 };   // -1 = livres, 0 = off
static const char *const SKIP_LBL[5]  = { "1", "2", "3", "LIVRES", "OFF" };

#define DECK_FACIL 0
#define DECK_TUDO  1

// ---------------------------------------------------------------------------
// Baralho — { texto CAIXA ALTA, categoria }. As categorias a partir de
// CAT_PERSONAGEM só entram no baralho TUDO.
// ---------------------------------------------------------------------------
typedef enum {
    CAT_ACAO, CAT_OBJETO, CAT_ANIMAL, CAT_PROFISSAO, CAT_LUGAR, CAT_ESPORTE,
    CAT_PERSONAGEM, CAT_EXPRESSAO, CAT_EMOCAO,
} cat_t;
#define CAT_HARD_FROM CAT_PERSONAGEM

static const char *const CAT_NAME[] = {
    "A\xC3\x87\xC3\x83O", "OBJETO", "ANIMAL", "PROFISS\xC3\x83O", "LUGAR",
    "ESPORTE", "PERSONAGEM", "EXPRESS\xC3\x83O", "EMO\xC3\x87\xC3\x83O",
};

typedef struct { const char *txt; uint8_t cat; } card_t;

static const card_t DECK[] = {
    // -- AÇÃO --
    { "ESCOVAR OS DENTES", CAT_ACAO }, { "TOMAR BANHO", CAT_ACAO },
    { "DIRIGIR", CAT_ACAO }, { "PESCAR", CAT_ACAO }, { "VARRER O CH\xC3\x83O", CAT_ACAO },
    { "LAVAR A LOU\xC3\x87""A", CAT_ACAO }, { "TIRAR FOTO", CAT_ACAO },
    { "COZINHAR", CAT_ACAO }, { "ESPIRRAR", CAT_ACAO }, { "DAN\xC3\x87""AR", CAT_ACAO },
    { "NADAR", CAT_ACAO }, { "AMARRAR O SAPATO", CAT_ACAO },
    { "TOCAR VIOL\xC3\x83O", CAT_ACAO }, { "APAGAR VELINHAS", CAT_ACAO },
    { "ASSISTIR TV", CAT_ACAO }, { "PENTEAR O CABELO", CAT_ACAO },
    // -- OBJETO --
    { "GUARDA-CHUVA", CAT_OBJETO }, { "ESCOVA DE DENTE", CAT_OBJETO },
    { "TELEFONE", CAT_OBJETO }, { "TESOURA", CAT_OBJETO }, { "MARTELO", CAT_OBJETO },
    { "TRAVESSEIRO", CAT_OBJETO }, { "VASSOURA", CAT_OBJETO }, { "GARRAFA", CAT_OBJETO },
    { "C\xC3\x82MERA", CAT_OBJETO }, { "MOCHILA", CAT_OBJETO }, { "REL\xC3\x93GIO", CAT_OBJETO },
    { "\xC3\x93""CULOS", CAT_OBJETO }, { "CHAVE", CAT_OBJETO },
    // -- ANIMAL --
    { "GIRAFA", CAT_ANIMAL }, { "PINGUIM", CAT_ANIMAL }, { "ELEFANTE", CAT_ANIMAL },
    { "MACACO", CAT_ANIMAL }, { "COBRA", CAT_ANIMAL }, { "CANGURU", CAT_ANIMAL },
    { "GALO", CAT_ANIMAL }, { "SAPO", CAT_ANIMAL }, { "BORBOLETA", CAT_ANIMAL },
    { "LE\xC3\x83O", CAT_ANIMAL }, { "CAVALO", CAT_ANIMAL }, { "ARANHA", CAT_ANIMAL },
    { "TARTARUGA", CAT_ANIMAL }, { "CARANGUEJO", CAT_ANIMAL },
    // -- PROFISSÃO --
    { "DENTISTA", CAT_PROFISSAO }, { "BOMBEIRO", CAT_PROFISSAO },
    { "GAR\xC3\x87OM", CAT_PROFISSAO }, { "PILOTO", CAT_PROFISSAO },
    { "PROFESSOR", CAT_PROFISSAO }, { "PALHA\xC3\x87O", CAT_PROFISSAO },
    { "GOLEIRO", CAT_PROFISSAO }, { "M\xC3\x89""DICO", CAT_PROFISSAO },
    { "PEDREIRO", CAT_PROFISSAO }, { "M\xC3\x81GICO", CAT_PROFISSAO },
    { "JARDINEIRO", CAT_PROFISSAO }, { "CABELEIREIRO", CAT_PROFISSAO },
    // -- LUGAR --
    { "PRAIA", CAT_LUGAR }, { "ACADEMIA", CAT_LUGAR }, { "HOSPITAL", CAT_LUGAR },
    { "CINEMA", CAT_LUGAR }, { "SUPERMERCADO", CAT_LUGAR }, { "ELEVADOR", CAT_LUGAR },
    { "IGREJA", CAT_LUGAR }, { "ESCOLA", CAT_LUGAR }, { "AVI\xC3\x83O", CAT_LUGAR },
    { "RESTAURANTE", CAT_LUGAR },
    // -- ESPORTE --
    { "NATA\xC3\x87\xC3\x83O", CAT_ESPORTE }, { "BOXE", CAT_ESPORTE },
    { "V\xC3\x94LEI", CAT_ESPORTE }, { "SKATE", CAT_ESPORTE }, { "T\xC3\x8ANIS", CAT_ESPORTE },
    { "BASQUETE", CAT_ESPORTE }, { "SURFE", CAT_ESPORTE }, { "CICLISMO", CAT_ESPORTE },
    { "GIN\xC3\x81STICA", CAT_ESPORTE }, { "REMO", CAT_ESPORTE },

    // ================= só no baralho TUDO =================
    // -- PERSONAGEM --
    { "CHAPEUZINHO VERMELHO", CAT_PERSONAGEM }, { "PIN\xC3\x93QUIO", CAT_PERSONAGEM },
    { "BRANCA DE NEVE", CAT_PERSONAGEM }, { "PAPAI NOEL", CAT_PERSONAGEM },
    { "FRANKENSTEIN", CAT_PERSONAGEM }, { "PIRATA", CAT_PERSONAGEM },
    { "SEREIA", CAT_PERSONAGEM }, { "VAMPIRO", CAT_PERSONAGEM },
    { "M\xC3\x9AMIA", CAT_PERSONAGEM }, { "BRUXA", CAT_PERSONAGEM },
    { "CAVERN\xC3\x8D" "COLA", CAT_PERSONAGEM }, { "ROB\xC3\x94", CAT_PERSONAGEM },
    // -- EXPRESSÃO --
    { "CHUTAR O BALDE", CAT_EXPRESSAO }, { "PAGAR MICO", CAT_EXPRESSAO },
    { "ENGOLIR SAPO", CAT_EXPRESSAO }, { "SEGURAR VELA", CAT_EXPRESSAO },
    { "PISAR EM OVOS", CAT_EXPRESSAO }, { "CHORAR AS PITANGAS", CAT_EXPRESSAO },
    { "COM A PULGA ATR\xC3\x81S DA ORELHA", CAT_EXPRESSAO },
    { "TIRAR O CAVALINHO DA CHUVA", CAT_EXPRESSAO },
    { "DAR COM A L\xC3\x8DNGUA NOS DENTES", CAT_EXPRESSAO },
    { "COM A M\xC3\x83O NA MASSA", CAT_EXPRESSAO },
    // -- EMOÇÃO --
    { "COM MEDO", CAT_EMOCAO }, { "APAIXONADO", CAT_EMOCAO }, { "COM RAIVA", CAT_EMOCAO },
    { "ENTEDIADO", CAT_EMOCAO }, { "ENVERGONHADO", CAT_EMOCAO }, { "COM CI\xC3\x9AMES", CAT_EMOCAO },
    { "ALIVIADO", CAT_EMOCAO }, { "ANIMADO", CAT_EMOCAO }, { "NERVOSO", CAT_EMOCAO },
    { "COM SONO", CAT_EMOCAO },
};
#define DECK_N ((int)(sizeof(DECK) / sizeof(DECK[0])))

static bool card_hard(int i) { return DECK[i].cat >= CAT_HARD_FROM; }

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
typedef enum { ST_IDLE, ST_PREP, ST_RUN, ST_TIMEUP } st_t;

static uint32_t s_accent = KIT_COLOR_BLUE;
static st_t     s_state  = ST_IDLE;

static int s_tempo_i = 0;
static int s_categ_i = 0;            // 0 MOSTRA / 1 ESCONDE
static int s_deck_i  = DECK_FACIL;
static int s_pulos_i = 3;            // índice em SKIP_LIM (3 -> LIVRES)

// Placar da vez (escondido até o overlay).
static int s_hits = 0, s_skips = 0;
static int s_prep_left = 0;

// Baralho: saco embaralhado sem reposição (uma sessão de festa = uma sentada).
static int s_bag[DECK_N];
static int s_bag_n = 0, s_bag_pos = 0;
static int s_last_card = -1;
static int s_cur = -1;

// Relógio da vez.
static uint64_t s_started_at = 0;
static uint64_t s_deadline   = 0;   // 0 = livre
static uint32_t s_total_ms   = 0;
static int      s_shown_sec  = -1;
static int      s_tick_sec   = -1;
static int      s_blink      = 0;
static bool     s_by_time    = true;

static lv_timer_t *s_clock = NULL;
static lv_timer_t *s_alarm = NULL;
static lv_timer_t *s_prep  = NULL;

// REINICIAR BARALHO — dois toques.
static bool        s_reset_armed = false;
static lv_timer_t *s_reset_timer = NULL;

// ---------------------------------------------------------------------------
// Objetos LVGL
// ---------------------------------------------------------------------------
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv     = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];
static lv_obj_t *s_dots_box = NULL;   // some no preparo/jogo (páginas travadas)

// Página 0 — AJUSTE
static lv_obj_t *s_tempo_pills[4]; static lv_obj_t *s_tempo_lbls[4];
static lv_obj_t *s_categ_pills[2]; static lv_obj_t *s_categ_lbls[2];
static lv_obj_t *s_deck_pills[2];  static lv_obj_t *s_deck_lbls[2];
static lv_obj_t *s_pulos_pills[5]; static lv_obj_t *s_pulos_lbls[5];
static lv_obj_t *s_reset_btn = NULL;
static lv_obj_t *s_reset_lbl = NULL;

// Página 1 — JOGO
static lv_obj_t *s_kicker    = NULL;
static lv_obj_t *s_bar_track = NULL;
static lv_obj_t *s_bar_fill  = NULL;
static lv_obj_t *s_clock_l   = NULL;
static lv_obj_t *s_endstrip  = NULL;
static lv_obj_t *s_stage_col = NULL;
static lv_obj_t *s_word      = NULL;
static lv_obj_t *s_idle_msg  = NULL;
static lv_obj_t *s_go_row    = NULL;
static lv_obj_t *s_skip_btn  = NULL;
static lv_obj_t *s_skip_lbl  = NULL;
static lv_obj_t *s_go_btn    = NULL;
static lv_obj_t *s_go_lbl    = NULL;

// Overlay — TEMPO
static lv_obj_t *s_ov       = NULL;
static lv_obj_t *s_ov_title = NULL;
static lv_obj_t *s_ov_tally = NULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const kit_api_table_t *s_api = NULL;
static void mimica_destroy(void);
static const kit_api_table_t *api(void) { return s_api; }

static uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static void beep(uint16_t freq, uint16_t ms)
{
    const kit_api_table_t *t = api();
    if (t && t->audio) t->audio->beep(freq, ms);
}

static void sfx(kit_sfx_t s)
{
    const kit_api_table_t *t = api();
    if (t && t->audio) t->audio->sfx(s);
}

static int rnd(int lo, int hi)
{
    const kit_api_table_t *t = api();
    if (hi < lo) hi = lo;
    if (t && t->random) return (int)t->random->range(lo, hi);
    return lo;
}

static uint64_t millis(void)
{
    const kit_api_table_t *t = api();
    return (t && t->time) ? t->time->get_millis() : 0;
}

static void keep_awake(bool on)
{
    const kit_api_table_t *t = api();
    if (t && t->power) t->power->keep_awake(on);
}

static int  tempo_secs(void) { return TEMPO_S[s_tempo_i]; }
static int  skip_limit(void) { return SKIP_LIM[s_pulos_i]; }
static bool categ_shown(void) { return s_categ_i == 0; }

static void vis(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *txt, uint32_t color,
                           const lv_font_t *font, int letter_space)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
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
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *field_label(lv_obj_t *parent, const char *txt)
{
    return add_label(parent, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_20, 2);
}

// ---------------------------------------------------------------------------
// Baralho
// ---------------------------------------------------------------------------

static int active_n(void)
{
    if (s_deck_i == DECK_TUDO) return DECK_N;
    int n = 0;
    for (int i = 0; i < DECK_N; i++) if (!card_hard(i)) n++;
    return n;
}

static void shuffle_bag(void)
{
    s_bag_n = 0;
    for (int i = 0; i < DECK_N; i++)
        if (s_deck_i == DECK_TUDO || !card_hard(i))
            s_bag[s_bag_n++] = i;

    for (int i = s_bag_n - 1; i > 0; i--) {
        int j = rnd(0, i);
        int t = s_bag[i]; s_bag[i] = s_bag[j]; s_bag[j] = t;
    }
    s_bag_pos = 0;
    if (s_bag_n > 1 && s_bag[0] == s_last_card) {
        int t = s_bag[0]; s_bag[0] = s_bag[1]; s_bag[1] = t;
    }
}

static void ensure_bag(void)
{
    if (s_bag_n != active_n()) shuffle_bag();
}

static int deck_next(void)
{
    if (s_bag_pos >= s_bag_n) shuffle_bag();
    int idx = s_bag[s_bag_pos++];
    s_last_card = idx;
    return idx;
}

// PULAR: devolve a carta atual ao monte, numa posição aleatória à frente.
static void deck_requeue(int idx)
{
    if (s_bag_pos > 0 && s_bag_pos <= s_bag_n) {
        s_bag_pos--;
        s_bag[s_bag_pos] = idx;
        int j = rnd(s_bag_pos, s_bag_n - 1);
        int t = s_bag[s_bag_pos]; s_bag[s_bag_pos] = s_bag[j]; s_bag[j] = t;
    }
}

// ---------------------------------------------------------------------------
// Persistência
// ---------------------------------------------------------------------------

static void save_prefs(void)
{
    const kit_api_table_t *t = api();
    if (!t || !t->storage) return;
    t->storage->set_i32(K_TEMPO, s_tempo_i);
    t->storage->set_i32(K_CATEG, s_categ_i);
    t->storage->set_i32(K_DECK, s_deck_i);
    t->storage->set_i32(K_PULOS, s_pulos_i);
}

static void load_prefs(void)
{
    const kit_api_table_t *t = api();
    if (!t || !t->storage) return;
    int32_t v;
    if (t->storage->get_i32(K_TEMPO, &v) == KIT_OK && v >= 0 && v <= 3) s_tempo_i = (int)v;
    if (t->storage->get_i32(K_CATEG, &v) == KIT_OK && (v == 0 || v == 1)) s_categ_i = (int)v;
    if (t->storage->get_i32(K_DECK, &v) == KIT_OK && (v == DECK_FACIL || v == DECK_TUDO)) s_deck_i = (int)v;
    if (t->storage->get_i32(K_PULOS, &v) == KIT_OK && v >= 0 && v <= 4) s_pulos_i = (int)v;
}

// ---------------------------------------------------------------------------
// Sincronização de UI
// ---------------------------------------------------------------------------

static void sync_dots(void)
{
    lv_obj_t *act = s_tv ? lv_tileview_get_tile_active(s_tv) : NULL;
    for (int i = 0; i < PAGES; i++) {
        bool on = (act == s_tiles[i]);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(on ? s_accent : KIT_COLOR_LINE), 0);
        lv_obj_set_size(s_dots[i], on ? 20 : 8, 8);
    }
}

static void sync_seg(lv_obj_t **pills, lv_obj_t **lbls, int n, int sel)
{
    uint32_t sel_txt = on_accent();
    for (int i = 0; i < n; i++) {
        bool on = (i == sel);
        lv_obj_set_style_bg_color(pills[i], lv_color_hex(on ? s_accent : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(lbls[i], lv_color_hex(on ? sel_txt : KIT_COLOR_TEXT), 0);
    }
}

static void sync_segs(void)
{
    sync_seg(s_tempo_pills, s_tempo_lbls, 4, s_tempo_i);
    sync_seg(s_categ_pills, s_categ_lbls, 2, s_categ_i);
    sync_seg(s_deck_pills, s_deck_lbls, 2, s_deck_i);
    sync_seg(s_pulos_pills, s_pulos_lbls, 5, s_pulos_i);
}

// PULAR só aparece EM JOGO/PREPARO, com PULOS != 0 e ainda sobrando pulo. Some
// quando esgota — sem opa intermediária (que forçaria layer buffer no CO5300).
static void update_skip_btn(void)
{
    if (!s_skip_btn) return;
    bool active = (s_state == ST_RUN || s_state == ST_PREP);
    int lim = skip_limit();
    bool spent = (lim > 0 && s_skips >= lim);
    bool show = (active && lim != 0 && !spent);
    vis(s_skip_btn, show);
    if (lim > 0)
        lv_label_set_text_fmt(s_skip_lbl, "PULAR %d", lim - s_skips);
    else
        lv_label_set_text(s_skip_lbl, "PULAR");
    if (active) {
        lv_obj_set_flex_grow(s_skip_btn, show ? 1 : 0);
        lv_obj_set_flex_grow(s_go_btn, show ? 2 : 1);
    }
}

static void render_card(void)
{
    if (!s_word || s_cur < 0) return;

    // O kicker é conteúdo POR CARTA (categoria da carta atual, ou ATUE se
    // CATEGORIA=ESCONDE) — atualiza aqui, não no sync_stage, senão fica preso na
    // categoria da 1ª carta.
    if (s_kicker)
        lv_label_set_text(s_kicker, categ_shown() ? CAT_NAME[DECK[s_cur].cat] : "ATUE");

    lv_label_set_text(s_word, DECK[s_cur].txt);
    // Paper (não a cor da Tool): azul sobre preto é escuro demais pra ler. O
    // azul fica no botão e na barra; a tipografia carrega a hierarquia.
    lv_obj_set_style_text_color(s_word, lv_color_hex(KIT_COLOR_TEXT), 0);
    // Palavra curta: kit_display_44 (Archivo Black, cobre A-Z + maiúsculas
    // acentuadas). Frase longa: kit_sans_28, que quebra em linhas.
    const lv_font_t *f = (strlen(DECK[s_cur].txt) >= 10) ? &kit_sans_28 : &kit_display_44;
    lv_obj_set_style_text_font(s_word, f, 0);
    // A carta pode ter sido renderizada com o palco recém-revelado (ou com a
    // fonte trocando de altura): força o recálculo do label que quebra linha.
    if (s_stage_col && !lv_obj_has_flag(s_stage_col, LV_OBJ_FLAG_HIDDEN))
        lv_obj_update_layout(s_stage_col);
}

static void sync_stage(void)
{
    if (!s_word) return;
    bool run   = (s_state == ST_RUN);
    bool prep  = (s_state == ST_PREP);
    bool over  = (s_state == ST_TIMEUP);
    bool active = run || prep;
    bool timed = active && tempo_secs() > 0;
    bool livre = run && tempo_secs() == 0;

    // Só no ocioso dá pra deslizar entre as páginas — some com os pontos no
    // preparo / vez / TEMPO.
    vis(s_dots_box, s_state == ST_IDLE);

    vis(s_ov, over);
    vis(s_stage_col, active);
    vis(s_idle_msg, !active && !over);

    vis(s_bar_track, timed);
    vis(s_clock_l, timed);
    vis(s_endstrip, livre);
    vis(s_go_row, !over);

    if (prep) {
        // Contagem regressiva SEM revelar a 1ª palavra — só o número grande.
        lv_label_set_text(s_kicker, "PREPARE-SE");
        lv_label_set_text_fmt(s_word, "%d", s_prep_left);
        lv_obj_set_style_text_font(s_word, &kit_display_72, 0);
        lv_obj_set_style_text_color(s_word, lv_color_hex(KIT_COLOR_TEXT), 0);
    } else if (!run) {
        lv_label_set_text(s_kicker, "M\xC3\x8DMICA");
    }
    // No estado RUN o kicker (categoria da carta) é do render_card.

    lv_label_set_text(s_go_lbl, active ? "ACERTOU" : "COME\xC3\x87""AR");
    if (!active) lv_obj_set_flex_grow(s_go_btn, 1);

    update_skip_btn();
    if (s_go_btn) lv_obj_invalidate(s_go_btn);
}

static void set_tv_locked(bool locked)
{
    if (!s_tv) return;
    if (locked) lv_obj_clear_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    else        lv_obj_add_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
}

// ---------------------------------------------------------------------------
// Relógio da vez
// ---------------------------------------------------------------------------

static void stop_clock(void) { if (s_clock) { lv_timer_delete(s_clock); s_clock = NULL; } }
static void stop_alarm(void) { if (s_alarm) { lv_timer_delete(s_alarm); s_alarm = NULL; } }
static void stop_prep(void)  { if (s_prep)  { lv_timer_delete(s_prep);  s_prep  = NULL; } }

static void fmt_clock(char *buf, size_t n, int secs)
{
    if (secs < 0) secs = 0;
    snprintf(buf, n, "%d:%02d", secs / 60, secs % 60);
}

static void time_up(bool by_time);

static void clock_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != ST_RUN) return;
    uint64_t now = millis();
    if (now >= s_deadline) { time_up(true); return; }

    uint32_t left_ms = (uint32_t)(s_deadline - now);
    int left = (int)((left_ms + 999) / 1000);

    int w = (s_total_ms > 0) ? (int)(((uint32_t)X_CONTENT * left_ms) / s_total_ms) : 0;
    lv_obj_set_width(s_bar_fill, w < 2 ? 2 : w);

    bool warn = left <= WARN_LEAD_S;
    if (left != s_shown_sec) {
        s_shown_sec = left;
        char b[8];
        fmt_clock(b, sizeof(b), left);
        lv_label_set_text(s_clock_l, b);
        lv_obj_set_style_text_color(s_clock_l,
            lv_color_hex(warn ? KIT_COLOR_TEXT : KIT_COLOR_TEXT_MUTED), 0);
        if (left <= TICK_LEAD_S && left != s_tick_sec) {
            s_tick_sec = left;
            sfx(KIT_SFX_TIMER_TICK);
        }
    }
    if (warn) {
        s_blink ^= 1;
        lv_obj_set_style_bg_opa(s_bar_fill, s_blink ? LV_OPA_COVER : LV_OPA_40, 0);
    } else {
        lv_obj_set_style_bg_opa(s_bar_fill, LV_OPA_COVER, 0);
    }
}

static void alarm_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != ST_TIMEUP) { stop_alarm(); return; }
    s_blink ^= 1;
    if (s_ov)
        lv_obj_set_style_bg_color(s_ov,
            lv_color_hex(s_blink ? 0x4551E0 : s_accent), 0);
    sfx(KIT_SFX_TIMER_DONE);
}

// ---------------------------------------------------------------------------
// Vez
// ---------------------------------------------------------------------------

static void fill_tally(void)
{
    if (!s_ov_tally) return;
    char b[40];
    snprintf(b, sizeof(b), "ACERTOS %d\nPULOS %d", s_hits, s_skips);
    lv_label_set_text(s_ov_tally, b);
    if (s_ov_title) lv_label_set_text(s_ov_title, s_by_time ? "TEMPO" : "FIM");
}

static void time_up(bool by_time)
{
    stop_clock();
    stop_prep();
    s_state   = ST_TIMEUP;
    s_by_time = by_time;
    s_shown_sec = s_tick_sec = -1;
    keep_awake(true);
    set_tv_locked(true);
    fill_tally();
    sync_stage();
    sfx(KIT_SFX_TIMER_DONE);
    stop_alarm();
    s_alarm = lv_timer_create(alarm_cb, ALARM_REPEAT_MS, NULL);
}

static void begin_turn(void)
{
    stop_prep();
    s_state = ST_RUN;

    int secs = tempo_secs();
    s_started_at = millis();
    s_total_ms = secs ? (uint32_t)secs * 1000u : 0u;
    s_deadline = secs ? s_started_at + s_total_ms : 0;
    s_shown_sec = s_tick_sec = -1;
    s_blink = 0;

    if (s_bar_fill) {
        lv_obj_set_width(s_bar_fill, X_CONTENT);
        lv_obj_set_style_bg_opa(s_bar_fill, LV_OPA_COVER, 0);
    }
    if (s_clock_l && secs) { char b[8]; fmt_clock(b, sizeof(b), secs); lv_label_set_text(s_clock_l, b); }

    stop_clock();
    if (secs) s_clock = lv_timer_create(clock_cb, CLOCK_TICK_MS, NULL);

    render_card();     // agora sim revela a 1ª palavra
    sync_stage();
    sfx(KIT_SFX_CONFIRM);
}

static void prep_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != ST_PREP) { stop_prep(); return; }
    s_prep_left--;
    if (s_prep_left > 0) {
        sync_stage();
        beep(880, 12);
    } else {
        begin_turn();
    }
}

static void start_prep(void)
{
    s_hits = s_skips = 0;
    ensure_bag();
    s_cur = deck_next();

    s_state = ST_PREP;
    s_prep_left = PREP_SECS;

    if (s_bar_fill) {
        lv_obj_set_width(s_bar_fill, X_CONTENT);
        lv_obj_set_style_bg_opa(s_bar_fill, LV_OPA_COVER, 0);
    }
    if (s_clock_l && tempo_secs()) {
        char b[8]; fmt_clock(b, sizeof(b), tempo_secs());
        lv_label_set_text(s_clock_l, b);
    }

    keep_awake(true);
    set_tv_locked(true);
    if (s_tv) lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
    if (s_ov) lv_obj_set_style_bg_color(s_ov, lv_color_hex(s_accent), 0);

    sync_stage();      // mostra só a contagem regressiva (a palavra fica pra
                       // begin_turn — a contagem não revela a 1ª carta)
    sfx(KIT_SFX_CONFIRM);

    stop_prep();
    s_prep = lv_timer_create(prep_cb, 1000, NULL);
}

static void mark_correct(void)
{
    if (s_state != ST_RUN) return;
    s_hits++;
    sfx(KIT_SFX_VETO_HIT);
    s_cur = deck_next();
    render_card();
}

static void do_skip(void)
{
    if (s_state != ST_RUN && s_state != ST_PREP) return;
    int lim = skip_limit();
    if (lim == 0) return;
    if (lim > 0 && s_skips >= lim) return;

    s_skips++;
    int prev = s_cur;
    deck_requeue(s_cur);
    s_cur = deck_next();
    if (s_cur == prev && s_bag_n > 1) s_cur = deck_next();
    render_card();
    sfx(KIT_SFX_CLICK);
    update_skip_btn();
}

static void next_turn(void)
{
    stop_alarm();
    s_state = ST_IDLE;
    keep_awake(false);
    set_tv_locked(false);
    sync_stage();
    beep(660, 26);
}

// ---------------------------------------------------------------------------
// Ação principal (PWR / botão ACERTOU / COMEÇAR)
// ---------------------------------------------------------------------------

static void mimica_action(void)
{
    if (!s_screen) return;
    switch (s_state) {
    case ST_IDLE:   start_prep();   break;
    case ST_PREP:   begin_turn();   break;   // pressa: pula a contagem
    case ST_RUN:    mark_correct(); break;
    case ST_TIMEUP: next_turn();    break;
    }
}

// ---------------------------------------------------------------------------
// REINICIAR BARALHO — dois toques
// ---------------------------------------------------------------------------

static void reset_disarm(void)
{
    s_reset_armed = false;
    if (s_reset_timer) { lv_timer_delete(s_reset_timer); s_reset_timer = NULL; }
    if (s_reset_lbl) {
        lv_label_set_text(s_reset_lbl, "REINICIAR BARALHO");
        lv_obj_set_style_text_color(s_reset_lbl, lv_color_hex(KIT_COLOR_RED), 0);
    }
    if (s_reset_btn) lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_TRANSP, 0);
}

static void reset_disarm_cb(lv_timer_t *t) { (void)t; reset_disarm(); }

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    const kit_api_table_t *t = api();
    if (t && t->system) t->system->exit();
}

static void tv_changed_cb(lv_event_t *e)
{
    (void)e;
    if (s_state != ST_IDLE && s_tv && lv_tileview_get_tile_active(s_tv) != s_tiles[1])
        lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
    sync_dots();
    reset_disarm();
}

static void go_cb(lv_event_t *e)      { (void)e; mimica_action(); }
static void skip_cb(lv_event_t *e)    { (void)e; do_skip(); }
static void end_cb(lv_event_t *e)     { (void)e; if (s_state == ST_RUN) time_up(false); }
static void ov_pass_cb(lv_event_t *e) { (void)e; next_turn(); }
static void ov_tap_cb(lv_event_t *e)
{
    (void)e;
    if (s_state != ST_TIMEUP) return;
    stop_alarm();
    if (s_ov) lv_obj_set_style_bg_color(s_ov, lv_color_hex(s_accent), 0);
}

static void tempo_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_tempo_i) return;
    s_tempo_i = v; sync_segs(); save_prefs();
}

static void categ_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_categ_i) return;
    s_categ_i = v; sync_segs(); save_prefs();
}

static void deck_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_deck_i) return;
    s_deck_i = v; sync_segs(); save_prefs();
    shuffle_bag();   // conjunto mudou — monte novo
}

static void pulos_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_pulos_i) return;
    s_pulos_i = v; sync_segs(); save_prefs();
    update_skip_btn();
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    if (!s_reset_armed) {
        s_reset_armed = true;
        lv_label_set_text(s_reset_lbl, "TOCAR DE NOVO PARA EMBARALHAR");
        lv_obj_set_style_text_color(s_reset_lbl, lv_color_hex(KIT_COLOR_ON_COLOR), 0);
        lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_COVER, 0);
        s_reset_timer = lv_timer_create(reset_disarm_cb, RESET_ARM_MS, NULL);
        return;
    }
    s_last_card = -1;
    shuffle_bag();
    reset_disarm();
    beep(660, 40);
}

// ---------------------------------------------------------------------------
// Construção da tela
// ---------------------------------------------------------------------------

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, X_CHIP, X_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, X_PAD, 16);

    lv_obj_center(add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    lv_obj_t *title = add_label(s_screen, "M\xC3\x8DMICA", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, X_PAD + X_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    s_dots_box = dots;
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -X_PAD, 40);
    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(KIT_COLOR_LINE), 0);
        s_dots[i] = d;
    }
}

static lv_obj_t *make_pill(lv_obj_t *parent, const char *txt, lv_event_cb_t cb,
                           int code, lv_obj_t **out_lbl)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_height(c, 66);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_color(c, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(c, 4);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    lv_obj_t *l = add_label(c, txt, KIT_COLOR_TEXT, &kit_mono_26, 0);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return c;
}

static void seg_row(lv_obj_t *parent, const char *title, const char *const *opts,
                    int n, lv_event_cb_t cb, lv_obj_t **pills, lv_obj_t **lbls)
{
    lv_obj_t *sec = plain_box(parent);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 12, 0);
    field_label(sec, title);

    lv_obj_t *row = plain_box(sec);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    for (int i = 0; i < n; i++)
        pills[i] = make_pill(row, opts[i], cb, i, &lbls[i]);
}

static void build_page_adjust(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, X_PAD, 0);
    lv_obj_set_style_pad_right(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 20, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 24, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    seg_row(p, "TEMPO", TEMPO_LBL, 4, tempo_cb, s_tempo_pills, s_tempo_lbls);
    seg_row(p, "CATEGORIA", CATEG_LBL, 2, categ_cb, s_categ_pills, s_categ_lbls);
    seg_row(p, "BARALHO", DECK_LBL, 2, deck_cb, s_deck_pills, s_deck_lbls);

    // PULOS em 2 linhas: 1, 2, 3 · LIVRES, OFF
    lv_obj_t *sec_pulos = plain_box(p);
    lv_obj_set_size(sec_pulos, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec_pulos, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec_pulos, 8, 0);
    field_label(sec_pulos, "PULOS");

    lv_obj_t *row_p1 = plain_box(sec_pulos);
    lv_obj_set_size(row_p1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_p1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row_p1, 6, 0);
    for (int i = 0; i < 3; i++)
        s_pulos_pills[i] = make_pill(row_p1, SKIP_LBL[i], pulos_cb, i, &s_pulos_lbls[i]);

    lv_obj_t *row_p2 = plain_box(sec_pulos);
    lv_obj_set_size(row_p2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_p2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row_p2, 6, 0);
    for (int i = 3; i < 5; i++)
        s_pulos_pills[i] = make_pill(row_p2, SKIP_LBL[i], pulos_cb, i, &s_pulos_lbls[i]);

    lv_obj_t *sec = plain_box(p);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 12, 0);
    field_label(sec, "BARALHO");

    s_reset_btn = lv_obj_create(sec);
    lv_obj_set_size(s_reset_btn, lv_pct(100), 66);
    lv_obj_set_style_radius(s_reset_btn, 16, 0);
    lv_obj_set_style_bg_color(s_reset_btn, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_set_style_bg_opa(s_reset_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_reset_btn, 2, 0);
    lv_obj_set_style_border_color(s_reset_btn, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_set_style_pad_all(s_reset_btn, 0, 0);
    lv_obj_clear_flag(s_reset_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_reset_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_reset_btn, 6);
    lv_obj_add_event_cb(s_reset_btn, reset_cb, LV_EVENT_CLICKED, NULL);
    s_reset_lbl = add_label(s_reset_btn, "REINICIAR BARALHO", KIT_COLOR_RED, &kit_mono_20, 1);
    lv_obj_center(s_reset_lbl);

    lv_obj_t *note = add_label(sec,
        "EMBARALHA O MONTE. AS CARTAS N\xC3\x83O S\xC3\x83O SALVAS: AO REABRIR, PODEM REPETIR.",
        KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, X_CONTENT);
}

static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *box = lv_obj_create(tile);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Palco — NÃO tocável.
    lv_obj_t *stage = plain_box(box);
    lv_obj_set_size(stage, KIT_DISPLAY_WIDTH, STAGE_H);
    lv_obj_set_pos(stage, 0, 0);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // Barra de tempo.
    s_bar_track = lv_obj_create(stage);
    lv_obj_remove_style_all(s_bar_track);
    lv_obj_set_size(s_bar_track, X_CONTENT, 8);
    lv_obj_align(s_bar_track, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(s_bar_track, 4, 0);
    lv_obj_set_style_bg_color(s_bar_track, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_bar_track, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_bar_track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_bar_fill = lv_obj_create(s_bar_track);
    lv_obj_remove_style_all(s_bar_fill);
    lv_obj_set_size(s_bar_fill, X_CONTENT, 8);
    lv_obj_align(s_bar_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(s_bar_fill, 4, 0);
    lv_obj_set_style_bg_color(s_bar_fill, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_bar_fill, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_bar_fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_clock_l = add_label(stage, "1:00", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
    lv_obj_align(s_clock_l, LV_ALIGN_TOP_RIGHT, -X_PAD, 26);

    // Faixa "ENCERRAR VEZ" (modo OFF/livre).
    s_endstrip = lv_obj_create(stage);
    lv_obj_set_size(s_endstrip, X_CONTENT, 36);
    lv_obj_align(s_endstrip, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(s_endstrip, 12, 0);
    lv_obj_set_style_bg_opa(s_endstrip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_endstrip, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_endstrip, 2, 0);
    lv_obj_set_style_border_color(s_endstrip, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_set_style_pad_all(s_endstrip, 0, 0);
    lv_obj_clear_flag(s_endstrip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_endstrip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_endstrip, 6);
    lv_obj_add_event_cb(s_endstrip, end_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(add_label(s_endstrip, "ENCERRAR VEZ", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2));

    // Coluna central — kicker + palavra.
    s_stage_col = plain_box(stage);
    lv_obj_set_size(s_stage_col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_stage_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_stage_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_stage_col, 8, 0);
    lv_obj_add_flag(s_stage_col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(s_stage_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_stage_col, LV_ALIGN_CENTER, 0, 10);

    s_kicker = add_label(s_stage_col, "M\xC3\x8DMICA", KIT_COLOR_TEXT, &kit_mono_20, 3);

    s_word = add_label(s_stage_col, "M\xC3\x8DMICA", KIT_COLOR_TEXT, &kit_display_44, 1);
    lv_label_set_long_mode(s_word, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_word, X_CONTENT);
    lv_obj_set_style_text_align(s_word, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_word, 2, 0);
    lv_obj_set_style_pad_bottom(s_word, 4, 0);

    // Mensagem de ocioso (irmã da coluna, centralizada).
    s_idle_msg = add_label(stage, "TOQUE EM\nCOME\xC3\x87""AR", KIT_COLOR_TEXT_MUTED, &kit_mono_26, 2);
    lv_obj_set_style_text_align(s_idle_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_idle_msg);

    // Linha de Ações: PULAR + ACERTOU (ou COMEÇAR).
    s_go_row = plain_box(box);
    lv_obj_set_size(s_go_row, X_CONTENT, ACT_H);
    lv_obj_set_flex_flow(s_go_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_go_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_go_row, 8, 0);
    lv_obj_align(s_go_row, LV_ALIGN_BOTTOM_MID, 0, -ACT_MARGIN);

    // PULAR — contornado
    s_skip_btn = lv_obj_create(s_go_row);
    lv_obj_set_height(s_skip_btn, ACT_H);
    lv_obj_set_flex_grow(s_skip_btn, 1);
    lv_obj_set_style_radius(s_skip_btn, ACT_H / 2, 0);
    lv_obj_set_style_bg_color(s_skip_btn, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(s_skip_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_skip_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_skip_btn, 2, 0);
    lv_obj_set_style_border_color(s_skip_btn, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_set_style_pad_all(s_skip_btn, 0, 0);
    lv_obj_clear_flag(s_skip_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_skip_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_skip_btn, 6);
    lv_obj_add_event_cb(s_skip_btn, skip_cb, LV_EVENT_CLICKED, NULL);
    s_skip_lbl = add_label(s_skip_btn, "PULAR", KIT_COLOR_TEXT_MUTED, &kit_mono_20, 1);
    lv_obj_center(s_skip_lbl);

    // ACERTOU / COMEÇAR — pílula azul cheia
    s_go_btn = lv_obj_create(s_go_row);
    lv_obj_set_height(s_go_btn, ACT_H);
    lv_obj_set_flex_grow(s_go_btn, 2);
    lv_obj_set_style_radius(s_go_btn, ACT_H / 2, 0);
    lv_obj_set_style_border_width(s_go_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_go_btn, 0, 0);
    lv_obj_set_style_pad_all(s_go_btn, 0, 0);
    lv_obj_set_style_bg_color(s_go_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_clear_flag(s_go_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_go_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_go_btn, 6);
    lv_obj_add_event_cb(s_go_btn, go_cb, LV_EVENT_CLICKED, NULL);
    s_go_lbl = add_label(s_go_btn, "COME\xC3\x87""AR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_go_lbl);
}

static const char MI_RULES[] =
    "Uma pessoa segura o KIT e vê a tela. O time dela adivinha.\n\n"
    "1. Toque em COMEÇAR. Depois da contagem 3, 2, 1 a palavra aparece só pra "
    "você e o tempo começa.\n\n"
    "2. Represente a palavra só com gestos e expressões. Sem falar, sem fazer "
    "som, sem mexer a boca formando a palavra, sem apontar objeto da sala e sem "
    "soletrar no ar.\n\n"
    "3. Pode mostrar quantas palavras e sílabas com os dedos, e pode dizer a "
    "categoria.\n\n"
    "4. Acertou: toque em ACERTOU ou aperte o PWR e cai a próxima carta. "
    "Travou: toque em PULAR e a carta volta pro monte, se o Ajuste deixar.\n\n"
    "Quando o TEMPO acaba, o KIT mostra quantas você fez. Se cada palpite valeu, "
    "quem decide é a mesa. Passe o KIT adiante.";

static void build_page_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, X_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, MI_RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, X_CONTENT);
}

static void build_overlay(void)
{
    s_ov = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_ov);
    lv_obj_set_size(s_ov, KIT_DISPLAY_WIDTH, X_PAGE_H);
    lv_obj_set_pos(s_ov, 0, X_TITLEBAR);
    lv_obj_set_style_bg_color(s_ov, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ov, ov_tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(s_ov);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -24);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    s_ov_title = add_label(col, "TEMPO", on_accent(), &kit_display_72, 0);
    s_ov_tally = add_label(col, "ACERTOS 0\nPULOS 0", on_accent(), &kit_mono_26, 2);
    lv_obj_set_style_text_align(s_ov_tally, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_ov_tally, 8, 0);
    lv_obj_clear_flag(s_ov_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ov_tally, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *nb = lv_obj_create(s_ov);
    lv_obj_set_size(nb, X_CONTENT, ACT_H);
    lv_obj_set_style_radius(nb, ACT_H / 2, 0);
    lv_obj_set_style_border_width(nb, 2, 0);
    lv_obj_set_style_border_color(nb, lv_color_hex(on_accent()), 0);
    lv_obj_set_style_bg_opa(nb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(nb, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(nb, 0, 0);
    lv_obj_clear_flag(nb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(nb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(nb, 8);
    lv_obj_align(nb, LV_ALIGN_BOTTOM_MID, 0, -ACT_MARGIN);
    lv_obj_add_event_cb(nb, ov_pass_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = add_label(nb, "PASSAR A VEZ", on_accent(), &kit_mono_26, 3);
    lv_obj_center(nl);
    lv_obj_clear_flag(nl, LV_OBJ_FLAG_CLICKABLE);
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, KIT_DISPLAY_WIDTH, X_PAGE_H);
    lv_obj_set_pos(s_tv, 0, X_TITLEBAR);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, tv_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_HOR);
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_HOR);
    build_page_adjust(s_tiles[0]);
    build_page_game(s_tiles[1]);
    build_page_help(s_tiles[2]);
}

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------

static kit_err_t mimica_start(uint32_t accent)
{
    if (s_screen) mimica_destroy();

    s_accent  = accent ? accent : KIT_COLOR_BLUE;
    s_state   = ST_IDLE;
    s_tempo_i = 0;
    s_categ_i = 0;
    s_deck_i  = DECK_FACIL;
    s_pulos_i = 3;   // LIVRES
    s_hits = s_skips = 0;
    s_cur = -1;
    s_last_card = -1;
    s_reset_armed = false;
    load_prefs();

    // Baralho reiniciado a cada abertura da Tool (sem memória entre sessões).
    s_bag_n = 0;
    s_bag_pos = 0;
    shuffle_bag();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, ov_tap_cb, LV_EVENT_CLICKED, NULL);

    build_titlebar();
    build_tileview();
    build_overlay();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   // começa no JOGO
    lv_obj_update_layout(s_screen);

    sync_segs();
    sync_stage();
    sync_dots();

    lv_screen_load(s_screen);
    return KIT_OK;
}

static void mimica_destroy(void)
{
    stop_clock();
    stop_alarm();
    stop_prep();
    if (s_reset_timer) { lv_timer_delete(s_reset_timer); s_reset_timer = NULL; }
    keep_awake(false);
    s_state = ST_IDLE;
    s_reset_armed = false;

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_tv = NULL;
    s_dots_box = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < 4; i++) { s_tempo_pills[i] = NULL; s_tempo_lbls[i] = NULL; }
    for (int i = 0; i < 2; i++) {
        s_categ_pills[i] = s_deck_pills[i] = NULL;
        s_categ_lbls[i] = s_deck_lbls[i] = NULL;
    }
    for (int i = 0; i < 5; i++) { s_pulos_pills[i] = s_pulos_lbls[i] = NULL; }
    s_reset_btn = s_reset_lbl = NULL;
    s_kicker = s_bar_track = s_bar_fill = s_clock_l = s_endstrip = NULL;
    s_stage_col = s_word = s_idle_msg = NULL;
    s_skip_btn = s_skip_lbl = s_go_row = s_go_btn = s_go_lbl = NULL;
    s_ov = s_ov_title = s_ov_tally = NULL;
}

// ---------------------------------------------------------------------------
// Ciclo de vida da Tool (catálogo)
// ---------------------------------------------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    printf("[Mimica] tool_init (id=%s)\n", ctx->tool_id ? ctx->tool_id : "?");
    return mimica_start(KIT_COLOR_BLUE);
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Mimica] tool_destroy\n");
    mimica_destroy();
    s_api = NULL;
}

#else  /* KIT_SDK_STUBS — build nativo: só compila, sem UI */

#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Mimica stub] tool_init — UI atrás de KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
