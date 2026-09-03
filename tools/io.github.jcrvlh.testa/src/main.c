/**
 * @file main.c
 * @brief Testa — mini-jogo de mesa estilo "Heads Up!" para o KIT.
 *
 * Uma pessoa segura o KIT na testa, tela virada pra roda. A roda ajuda, ela
 * adivinha. Inclina pra baixo = acertou; pra cima = passou (gesto do kit_imu,
 * ver kit_tool_api.h → register_tilt_callback; exige min_runtime >= 0.2.0).
 *
 * Tela: titlebar fixa + lv_tileview de 3 páginas (AJUSTE / JOGO / COMO JOGA,
 * começa no JOGO) + barra de tempo; sem botões de acerto/passe — a mecânica é
 * o gesto. Flash verde ACERTOU / vermelho PASSOU entre as cartas; overlay
 * amarelo TEMPO com o placar da vez (1º toque cala o alarme, PASSAR A VEZ sai).
 *
 * Sem transform_scale/rotation e sem `opa` intermediário em container (força
 * layer buffer e estoura o render no CO5300/PSRAM). Barra de tempo = lv_timer
 * de 200 ms + lv_obj_set_width. Toda a UI atrás de #ifndef KIT_SDK_STUBS —
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

// ---------------------------------------------------------------------------
// Layout (espelha as métricas da Mímica)
// ---------------------------------------------------------------------------
#define T_PAD         16
#define T_CONTENT     (KIT_DISPLAY_WIDTH - 2 * T_PAD)             // 336
#define T_CHIP        56
#define T_TITLEBAR    88
#define T_PAGE_H      (KIT_DISPLAY_HEIGHT - T_TITLEBAR)           // 360
#define PAGES         3

#define T_BAR_W       (T_CONTENT - 62)   // sobra pro M:SS à direita
#define T_BAR_H       8
#define T_ACT_H       76
#define T_ACT_MARGIN  14

#define PREP_SECS     3       // "PREPARE-SE 3 · 2 · 1" antes do relógio arrancar
#define FEEDBACK_MS   900     // duração do flash ACERTOU / PASSOU
#define ALARM_MS      3400    // re-toca o alarme no overlay TEMPO

// Estados
#define ST_IDLE      0
#define ST_PREP      1
#define ST_PLAY      2
#define ST_FEEDBACK  3
#define ST_OVER      4

// Storage
#define K_TEMPO  "te_tempo"
#define K_DECK   "te_deck"

static const int32_t TEMPO_SECS[] = { 60, 90, 120, 0 };   // 0 = LIVRE
#define TEMPO_N ((int)(sizeof(TEMPO_SECS) / sizeof(TEMPO_SECS[0])))
static const char *const TEMPO_LBL[TEMPO_N] = { "60S", "90S", "120S", "OFF" };

// ---------------------------------------------------------------------------
// Baralhos temáticos — fixos no firmware. Curadoria pró-adivinhação-verbal
// (nomes próprios e cultura pop são bem-vindos, ao contrário da Mímica).
// Tudo em CAIXA ALTA. A fonte da palavra (word_font) cobre ASCII + maiúsculas
// acentuadas À–Ü em kit_display_44; o resto cai em kit_mono_26. Evitar em dash,
// reticências e aspas curvas (glifos ausentes).
// O baralho MIX embaralha as cartas de todos os temas juntas.
// ---------------------------------------------------------------------------
static const char *const DECK_FILMES[] = {
    "TITANIC", "O REI LEÃO", "HARRY POTTER", "VINGADORES", "TOY STORY",
    "PIRATAS DO CARIBE", "JURASSIC PARK", "FROZEN", "MATRIX", "HOMEM-ARANHA",
    "STAR WARS", "O SENHOR DOS ANÉIS", "PANTERA NEGRA", "SHREK", "BATMAN",
    "STRANGER THINGS", "LA CASA DE PAPEL", "THE OFFICE", "GAME OF THRONES",
    "BREAKING BAD", "OS SIMPSONS", "TROPA DE ELITE", "CIDADE DE DEUS",
    "DE VOLTA PARA O FUTURO", "CORINGA", "AVATAR", "MINIONS",
    "O AUTO DA COMPADECIDA", "MINHA MÃE É UMA PEÇA", "CENTRAL DO BRASIL",
    "ELITE", "CARROSSEL", "CHAVES", "A GRANDE FAMÍLIA", "SÍTIO DO PICA-PAU",
    "MALHAÇÃO", "VELHO CHICO", "AVENIDA BRASIL", "O CLONE",
    "MADAGASCAR", "PROCURANDO NEMO", "DIVERTIDA MENTE", "VIVA A VIDA É UMA FESTA",
    "CARROS", "MEU MALVADO FAVORITO", "PANIC ROOM", "A COISA", "COCO",
    "CREPÚSCULO", "JOGOS VORAZES", "REBELDE", "THE LAST OF US", "WEDNESDAY",
    "ROUND 6", "THE CROWN", "PEAKY BLINDERS", "FRIENDS", "GREYS ANATOMY",
    "O REI DO GADO", "SESSÃO DA TARDE", "SHERLOCK", "MISSÃO IMPOSSÍVEL", "VELOZES E FURIOSOS",
    "O EXORCISTA", "PODEROSO CHEFÃO", "SUPERMAN", "MULHER-MARAVILHA", "AQUAMAN",
};
static const char *const DECK_FAMOSOS[] = {
    "NEYMAR", "PELÉ", "ANITTA", "XUXA", "SILVIO SANTOS", "FAUSTÃO",
    "IVETE SANGALO", "GISELE BÜNDCHEN", "AYRTON SENNA", "RONALDINHO GAÚCHO",
    "GABRIEL MEDINA", "SANDY E JUNIOR", "ROBERTO CARLOS", "CHICO BUARQUE",
    "MACHADO DE ASSIS", "SANTOS DUMONT", "TARSILA DO AMARAL", "PABLLO VITTAR",
    "WHINDERSSON NUNES", "FELIPE NETO", "CASIMIRO", "MC DANIEL",
    "GKAY", "LUÍSA SONZA", "MARÍLIA MENDONÇA", "TIM MAIA", "SEU JORGE",
    "GIL DO VIGOR", "JULIETTE", "TATÁ WERNECK", "PAULO GUSTAVO", "HEBE CAMARGO",
    "GUGU LIBERATO", "ANA MARIA BRAGA", "WILLIAM BONNER", "GALVÃO BUENO",
    "SABRINA SATO", "GRETCHEN", "PAOLLA OLIVEIRA", "WAGNER MOURA", "LÁZARO RAMOS",
    "FERNANDA MONTENEGRO", "GRANDE OTELO", "CAZUZA", "RAUL SEIXAS", "CARTOLA",
    "GLÓRIA MARIA", "OSCAR SCHMIDT", "HORTÊNCIA", "DAIANE DOS SANTOS", "REBECA ANDRADE",
    "ZICO", "GARRINCHA", "ROMÁRIO", "CAFU", "MARTA", "RIVALDO",
    "LULA", "TIRIRICA", "DILMA", "MURILO BENÍCIO", "SÔNIA ABRÃO",
    "PORCHAT", "DÉBORA NASCIMENTO", "TAÍS ARAÚJO", "BRUNO GAGLIASSO",
    "PEDRO BIAL", "SERGINHO GROISMAN", "JÔ SOARES", "CHACRINHA", "RITA LEE",
};
static const char *const DECK_ANIMAIS[] = {
    "ORNITORRINCO", "PREGUIÇA", "TAMANDUÁ", "CAPIVARA", "TUCANO", "ARARA",
    "ONÇA-PINTADA", "TATU-BOLA", "LOBO-GUARÁ", "BOTO-COR-DE-ROSA", "QUATI",
    "JACARÉ", "PIRANHA", "MICO-LEÃO-DOURADO", "SERIEMA", "BICHO-PREGUIÇA",
    "CANGURU", "PANDA", "PINGUIM", "CAMALEÃO", "POLVO", "MORCEGO",
    "ESCORPIÃO", "LAGARTIXA", "JOANINHA", "BESOURO", "CAVALO-MARINHO",
    "GIRAFA", "ELEFANTE", "HIPOPÓTAMO", "RINOCERONTE", "LEÃO", "TIGRE",
    "URSO POLAR", "COALA", "PREÁ", "OURIÇO", "PACA", "GAMBÁ", "LONTRA",
    "FOCA", "BALEIA", "GOLFINHO", "TUBARÃO", "ÁGUIA", "CORUJA", "PAVÃO",
    "FLAMINGO", "AVESTRUZ", "EMA", "GALINHA DA ANGOLA", "PERU", "PATO",
    "MARRECO", "CISNE", "SAPO", "PERERECA", "TEIÚ", "IGUANA", "COBRA CORAL",
    "JIBOIA", "SUCURI", "ARANHA-CARANGUEJEIRA", "LOUVA-A-DEUS", "VAGA-LUME",
    "FORMIGA", "CUPIM", "ABELHA", "MARIMBONDO", "CARRAPATO", "MINHOCA",
    "LESMA", "CARACOL", "CENTOPEIA", "TATUZINHO DE JARDIM",
};
static const char *const DECK_GIRIAS[] = {
    "MITO", "CRUSH", "PLOT TWIST", "MICO", "PAGAR MICO", "ZOEIRA",
    "TRETA", "RESENHA", "PLANTÃO", "FURÃO", "FICAR", "DAR UM ROLÊ",
    "PISAR NA BOLA", "CHUTAR O BALDE", "ENGOLIR SAPO", "SEGURAR VELA",
    "VIAJAR NA MAIONESE", "ENCHER LINGUIÇA", "COM A PULGA ATRÁS DA ORELHA",
    "TIRAR O CAVALINHO DA CHUVA", "PÔR A MÃO NA MASSA", "ESTAR COM A CORDA TODA",
    "SANGUE DE BARATA", "CARA DE PAU", "GATO PINGADO", "DAR UM GELO",
    "PAGAR O PATO", "DESCASCAR O ABACAXI", "COLOCAR A BOCA NO TROMBONE",
    "ACERTAR NA MOSCA", "CHOVER NO MOLHADO", "DAR COM OS BURROS NA ÁGUA",
    "FICAR DE OLHO", "SANTINHA DO PAU OCO", "COBRA CRIADA", "MALA SEM ALÇA",
    "DAR UMA MÃOZINHA", "ESTAR NA PINDAÍBA", "ENCHER O SACO", "MANDAR VER",
    "SE LIGAR", "TÁ LIGADO", "DAR RUIM", "SHOW DE BOLA", "FIRMEZA",
    "MARAVILHA", "PARÇA", "TOP", "MASSA", "DE BOA", "DAR MOLE", "VACILÃO",
    "CARETA", "ZUAR", "SE GARANTIR", "MÃO NA RODA", "QUEBRAR O GALHO",
    "TIRAR ONDA", "PUXAR SACO", "FAZER VISTA GROSSA", "MATAR A COBRA",
    "TROCAR AS BOLAS", "FICAR A VER NAVIOS",
};
static const char *const DECK_OBJETOS[] = {
    "LIQUIDIFICADOR", "CATRACA", "GUARDA-CHUVA", "CHUVEIRO", "GELADEIRA",
    "VENTILADOR", "CONTROLE REMOTO", "CARREGADOR", "FONE DE OUVIDO",
    "ESCADA ROLANTE", "MÁQUINA DE LAVAR", "ASPIRADOR DE PÓ", "FERRO DE PASSAR",
    "ABRIDOR DE LATA", "CORTADOR DE UNHA", "PADARIA", "FARMÁCIA", "RODOVIÁRIA",
    "POSTO DE GASOLINA", "SALÃO DE BELEZA", "PARQUE DE DIVERSÕES", "AEROPORTO",
    "BANCA DE JORNAL", "CARTÓRIO", "LAVANDERIA", "PET SHOP", "SORVETERIA",
    "MICRO-ONDAS", "CAFETEIRA", "TORRADEIRA", "BATEDEIRA", "PANELA DE PRESSÃO",
    "TÁBUA DE PASSAR", "VARAL", "PRENDEDOR DE ROUPA", "RALADOR", "ESPREMEDOR",
    "PENEIRA", "CONCHA", "ESPÁTULA", "GARFO", "ABRIDOR DE VINHO", "SACA-ROLHAS",
    "GRAMPEADOR", "FURADEIRA", "CHAVE DE FENDA", "ALICATE", "TRENA", "NÍVEL",
    "REGADOR", "MANGUEIRA", "CARRINHO DE MÃO", "REDE DE DORMIR",
    "MÁQUINA FOTOGRÁFICA", "BÚSSOLA", "LANTERNA", "BINÓCULO", "AMPULHETA",
    "GLOBO TERRESTRE", "MOLINETE", "PIPA", "GANGORRA", "ESCORREGADOR",
    "BIBLIOTECA", "MUSEU", "ZOOLÓGICO", "PLANETÁRIO", "OBSERVATÓRIO",
    "MERCADO MUNICIPAL", "FEIRA LIVRE", "CIRCO", "TEATRO", "ESTÁDIO",
    "RODA-GIGANTE", "AQUÁRIO", "SHOPPING", "DELEGACIA", "CORREIOS",
};

typedef struct { const char *name; const char *const *cards; int n; } deck_t;
#define DECK_ENTRY(nm, arr) { nm, arr, (int)(sizeof(arr) / sizeof((arr)[0])) }

// Só os temas. MIX é virtual (índice THEME_COUNT) e junta todos.
static const deck_t THEMES[] = {
    DECK_ENTRY("FILMES & SÉRIES",   DECK_FILMES),
    DECK_ENTRY("CELEBRIDADES",      DECK_FAMOSOS),
    DECK_ENTRY("ANIMAIS",           DECK_ANIMAIS),
    DECK_ENTRY("GÍRIAS",            DECK_GIRIAS),
    DECK_ENTRY("OBJETOS & LUGARES", DECK_OBJETOS),
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))
#define MIX_IDX     THEME_COUNT
#define DECK_COUNT  (THEME_COUNT + 1)

#define MIX_N ((int)((sizeof(DECK_FILMES) + sizeof(DECK_FAMOSOS) + sizeof(DECK_ANIMAIS) + \
                      sizeof(DECK_GIRIAS) + sizeof(DECK_OBJETOS)) / sizeof(const char *)))
#define T_BAG_MAX (MIX_N + 96)   // MIX inteiro + folga (PASSAR devolve a carta ao monte)

static const char *deck_label(int i)
{
    return (i >= 0 && i < THEME_COUNT) ? THEMES[i].name : "MIX";
}

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
static uint32_t s_accent = KIT_COLOR_YELLOW;

static int  s_tempo_idx = 0;
static int  s_deck_idx  = 0;

static int  s_state = ST_IDLE;
static int  s_acertos = 0;
static int  s_passou = 0;
static int  s_prep_left = 0;

static uint32_t s_turn_ms  = 0;
static uint64_t s_deadline = 0;
static int      s_shown_secs = -1;

// Saco sem reposição (Fisher-Yates). Cresce ao devolver um PASSAR.
static uint16_t s_bag[T_BAG_MAX];
static int      s_bag_n = 0;
static int      s_bag_pos = 0;
static int      s_cur = -1;
static int      s_last_idx = -1;

// Anti-repetição entre rodadas: as N últimas cartas mostradas não voltam já na
// próxima (o rebuild do saco reembaralha tudo e podia repetir de cara).
#define RECENT_N 8
static int      s_recent[RECENT_N];
static int      s_recent_pos = 0;

static lv_timer_t *s_clock_timer = NULL;
static lv_timer_t *s_prep_timer  = NULL;
static lv_timer_t *s_fb_timer    = NULL;
static lv_timer_t *s_alarm_timer = NULL;

// ---------------------------------------------------------------------------
// Objetos LVGL
// ---------------------------------------------------------------------------
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tv     = NULL;
static lv_obj_t *s_tiles[PAGES];
static lv_obj_t *s_dots[PAGES];

// Página 0 — AJUSTE
static lv_obj_t *s_tempo_pills[TEMPO_N]; static lv_obj_t *s_tempo_lbls[TEMPO_N];
static lv_obj_t *s_deck_pills[DECK_COUNT]; static lv_obj_t *s_deck_lbls[DECK_COUNT];

// Página 1 — JOGO
static lv_obj_t *s_bar_track = NULL;
static lv_obj_t *s_bar_fill  = NULL;
static lv_obj_t *s_clock_lbl = NULL;
static lv_obj_t *s_end_strip = NULL;   // "ENCERRAR VEZ" (modo LIVRE)
static lv_obj_t *s_top_lbl   = NULL;   // nome do baralho / "PREPARE-SE N"
static lv_obj_t *s_word_lbl  = NULL;
static lv_obj_t *s_hint_lbl  = NULL;   // "INCLINE ↓ ACERTOU · ↑ PASSOU"
static lv_obj_t *s_act_btn   = NULL;   // COMEÇAR / JÁ! (só ocioso e prep)
static lv_obj_t *s_act_lbl   = NULL;

// Flash — ACERTOU / PASSOU
static lv_obj_t *s_flash     = NULL;
static lv_obj_t *s_flash_ttl = NULL;
static lv_obj_t *s_flash_sub = NULL;

// Overlay — TEMPO
static lv_obj_t *s_over     = NULL;
static lv_obj_t *s_over_ttl = NULL;
static lv_obj_t *s_over_sc  = NULL;
static lv_obj_t *s_over_btn = NULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const kit_api_table_t *s_api = NULL;
static void testa_destroy(void);
static const kit_api_table_t *api(void) { return s_api; }

static void beep(uint16_t f, uint16_t ms)
{
    const kit_api_table_t *t = api();
    if (t && t->audio) t->audio->beep(f, ms);
}

static void sfx(kit_sfx_t s)
{
    const kit_api_table_t *t = api();
    if (t && t->audio) t->audio->sfx(s);
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

static int rnd_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    const kit_api_table_t *t = api();
    if (t && t->random) return (int)t->random->range(lo, hi);
    return lo;
}

static uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static int tempo_secs(void)   { return (int)TEMPO_SECS[s_tempo_idx]; }
static bool livre_mode(void)  { return tempo_secs() == 0; }
static int cur_deck_n(void)
{
    if (s_deck_idx != MIX_IDX) return THEMES[s_deck_idx].n;
    int t = 0;
    for (int d = 0; d < THEME_COUNT; d++) t += THEMES[d].n;
    return t;
}

static const char *cur_card(int i)
{
    if (s_deck_idx != MIX_IDX) return THEMES[s_deck_idx].cards[i];
    for (int d = 0; d < THEME_COUNT; d++) {
        if (i < THEMES[d].n) return THEMES[d].cards[i];
        i -= THEMES[d].n;
    }
    return "";
}

static const char *cur_deck_name(void) { return deck_label(s_deck_idx); }

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
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *field_label(lv_obj_t *parent, const char *txt)
{
    return add_label(parent, txt, KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
}

// A fonte da palavra: kit_display_44 (Archivo Black) cobre ASCII + as maiúsculas
// acentuadas À–Ü (0xC0–0xDC). Frase longa quebra em várias linhas (LONG_WRAP),
// sem encolher a fonte — igual à Mímica. Só um acento minúsculo ou char fora do
// set derruba para kit_mono_26 (Latin-1 completo).
static const lv_font_t *word_font(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x80) continue;             // ASCII: sempre OK
        if (*p == 0xC3 && p[1]) {
            unsigned char n = *++p;          // 2º byte UTF-8: À–Ü = 0x80–0x9C
            if (n >= 0x80 && n <= 0x9C) continue;
        }
        return &kit_mono_26;                 // acento minúsculo / char fora do set
    }
    return &kit_display_44;
}

// ---------------------------------------------------------------------------
// Persistência
// ---------------------------------------------------------------------------

static void save_prefs(void)
{
    const kit_api_table_t *t = api();
    if (!t || !t->storage) return;
    t->storage->set_i32(K_TEMPO, s_tempo_idx);
    t->storage->set_i32(K_DECK, s_deck_idx);
}

static void load_prefs(void)
{
    const kit_api_table_t *t = api();
    if (!t || !t->storage) return;
    int32_t v;
    if (t->storage->get_i32(K_TEMPO, &v) == KIT_OK && v >= 0 && v < TEMPO_N) s_tempo_idx = (int)v;
    if (t->storage->get_i32(K_DECK, &v) == KIT_OK && v >= 0 && v < DECK_COUNT) s_deck_idx = (int)v;
}

// ---------------------------------------------------------------------------
// Saco de cartas
// ---------------------------------------------------------------------------

static void rebuild_bag(void)
{
    int n = cur_deck_n();
    if (n > T_BAG_MAX) n = T_BAG_MAX;
    s_bag_n = n;
    for (int i = 0; i < n; i++) s_bag[i] = (uint16_t)i;

    for (int i = s_bag_n - 1; i > 0; i--) {
        int j = rnd_range(0, i);
        uint16_t tmp = s_bag[i]; s_bag[i] = s_bag[j]; s_bag[j] = tmp;
    }
    s_bag_pos = 0;
}

static int bag_next(void)
{
    if (s_bag_pos >= s_bag_n) rebuild_bag();

    int idx = s_bag[s_bag_pos];
    if (idx == s_last_idx && s_bag_pos + 1 < s_bag_n) {
        s_bag[s_bag_pos] = s_bag[s_bag_pos + 1];
        s_bag[s_bag_pos + 1] = (uint16_t)idx;
        idx = s_bag[s_bag_pos];
    }
    s_bag_pos++;
    return idx;
}

// PASSAR: devolve a carta atual a uma posição aleatória à frente no saco.
static void bag_return_current(void)
{
    if (s_cur < 0 || s_bag_n >= T_BAG_MAX) return;
    int span = s_bag_n - s_bag_pos;
    int at = s_bag_pos + (span > 0 ? rnd_range(1, span) : 0);
    if (at > s_bag_n) at = s_bag_n;
    memmove(&s_bag[at + 1], &s_bag[at], (size_t)(s_bag_n - at) * sizeof(s_bag[0]));
    s_bag[at] = (uint16_t)s_cur;
    s_bag_n++;
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
    for (int i = 0; i < n; i++) {
        bool on = (i == sel);
        lv_obj_set_style_bg_color(pills[i], lv_color_hex(on ? s_accent : KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_text_color(lbls[i],
            lv_color_hex(on ? on_accent() : KIT_COLOR_TEXT), 0);
    }
}

static void sync_segs(void)
{
    sync_seg(s_tempo_pills, s_tempo_lbls, TEMPO_N, s_tempo_idx);
    sync_seg(s_deck_pills, s_deck_lbls, DECK_COUNT, s_deck_idx);
}

// Rótulo acima da palavra — nome do baralho no jogo, "PREPARE-SE N" na contagem.
static void sync_top(void)
{
    if (!s_top_lbl) return;
    lv_label_set_text(s_top_lbl, s_state == ST_PREP ? "PREPARE-SE" : cur_deck_name());
}

static void show_word(const char *txt, uint32_t color)
{
    lv_label_set_text(s_word_lbl, txt);
    lv_obj_set_style_text_font(s_word_lbl, word_font(txt), 0);
    lv_obj_set_style_text_color(s_word_lbl, lv_color_hex(color), 0);
}

static void sync_footer(void)
{
    bool prep_or_idle = (s_state == ST_IDLE || s_state == ST_PREP);

    // Botão de ação: só ocioso (COMEÇAR) e contagem (JÁ!).
    if (prep_or_idle) {
        lv_label_set_text(s_act_lbl, s_state == ST_PREP ? "JÁ!" : "COMEÇAR");
        lv_obj_remove_flag(s_act_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_act_btn, LV_OBJ_FLAG_HIDDEN);
    }

    // Dica do gesto: só em jogo.
    if (s_state == ST_PLAY || s_state == ST_FEEDBACK)
        lv_obj_remove_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);

    bool strip = (s_state == ST_PLAY && livre_mode());
    if (strip) lv_obj_remove_flag(s_end_strip, LV_OBJ_FLAG_HIDDEN);
    else       lv_obj_add_flag(s_end_strip, LV_OBJ_FLAG_HIDDEN);

    // Barra de tempo: escondida no ocioso e no modo LIVRE.
    bool bar = (s_state == ST_PREP || s_state == ST_PLAY || s_state == ST_FEEDBACK) && !livre_mode();
    if (bar) {
        lv_obj_remove_flag(s_bar_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_clock_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_bar_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_clock_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Trava do arraste entre páginas
// ---------------------------------------------------------------------------
static void tv_lock(bool locked)
{
    if (!s_tv) return;
    if (locked) lv_obj_remove_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    else        lv_obj_add_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------

static void stop_timer(lv_timer_t **t) { if (*t) { lv_timer_delete(*t); *t = NULL; } }

static void stop_all_timers(void)
{
    stop_timer(&s_clock_timer);
    stop_timer(&s_prep_timer);
    stop_timer(&s_fb_timer);
    stop_timer(&s_alarm_timer);
}

// ---------------------------------------------------------------------------
// Fluxo da vez
// ---------------------------------------------------------------------------

static void alarm_tick_cb(lv_timer_t *t) { (void)t; sfx(KIT_SFX_TIMER_DONE); }

static void recent_clear(void)
{
    for (int i = 0; i < RECENT_N; i++) s_recent[i] = -1;
    s_recent_pos = 0;
}

static bool in_recent(int idx)
{
    for (int i = 0; i < RECENT_N; i++) if (s_recent[i] == idx) return true;
    return false;
}

static void next_card(void)
{
    int idx = -1;
    int cap = (cur_deck_n() > RECENT_N + 2) ? RECENT_N + 4 : 1;
    for (int t = 0; t < cap; t++) {
        idx = bag_next();
        if (!in_recent(idx)) break;
    }
    s_cur = idx;
    s_last_idx = idx;
    s_recent[s_recent_pos] = idx;
    s_recent_pos = (s_recent_pos + 1) % RECENT_N;
    show_word(cur_card(s_cur), s_accent);   // palavra: acento sobre o preto
    sync_top();
}

// Número grande da contagem regressiva ("3" · "2" · "1"), no lugar da palavra.
static void show_count(int n)
{
    char b[8];
    snprintf(b, sizeof(b), "%d", n);
    show_word(b, KIT_COLOR_TEXT);
}

static void time_up(void)
{
    stop_timer(&s_clock_timer);
    stop_timer(&s_fb_timer);
    s_state = ST_OVER;

    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_over_ttl, livre_mode() ? "FIM" : "TEMPO");
    lv_label_set_text_fmt(s_over_sc, "ACERTOS %d    PASSOU %d", s_acertos, s_passou);
    lv_obj_remove_flag(s_over, LV_OBJ_FLAG_HIDDEN);

    sfx(KIT_SFX_TIMER_DONE);
    stop_timer(&s_alarm_timer);
    s_alarm_timer = lv_timer_create(alarm_tick_cb, ALARM_MS, NULL);
}

static void clock_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state != ST_PLAY && s_state != ST_FEEDBACK) return;

    uint64_t now = millis();
    if (now >= s_deadline) {
        lv_obj_set_width(s_bar_fill, 0);
        time_up();
        return;
    }

    uint32_t left = (uint32_t)(s_deadline - now);
    int w = (int)((left * (uint32_t)T_BAR_W) / s_turn_ms);
    lv_obj_set_width(s_bar_fill, w);

    int secs = (int)((left + 999) / 1000);
    if (secs != s_shown_secs) {
        s_shown_secs = secs;
        lv_label_set_text_fmt(s_clock_lbl, "%d:%02d", secs / 60, secs % 60);
        if (secs <= 10)
            lv_obj_set_style_bg_color(s_bar_fill, lv_color_hex(KIT_COLOR_RED), 0);
        if (secs <= 5)
            sfx(KIT_SFX_TIMER_TICK);
    }
}

static void fb_done_cb(lv_timer_t *t)
{
    (void)t;
    s_fb_timer = NULL;
    if (s_state != ST_FEEDBACK) return;   // o tempo pode ter acabado durante o flash
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    s_state = ST_PLAY;
    next_card();
    sync_footer();
}

static void show_feedback(bool hit)
{
    s_state = ST_FEEDBACK;
    lv_obj_set_style_bg_color(s_flash, lv_color_hex(hit ? KIT_COLOR_GREEN : KIT_COLOR_RED), 0);
    lv_label_set_text(s_flash_ttl, hit ? "ACERTOU" : "PASSOU");
    lv_label_set_text(s_flash_sub, s_cur >= 0 ? cur_card(s_cur) : "");
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    // s_flash é criado depois do tileview (build order) — já fica acima dele.
    // O único irmão acima é s_over, que está escondido durante o FEEDBACK.

    sfx(hit ? KIT_SFX_VETO_HIT : KIT_SFX_CLICK);

    stop_timer(&s_fb_timer);
    s_fb_timer = lv_timer_create(fb_done_cb, FEEDBACK_MS, NULL);
    lv_timer_set_repeat_count(s_fb_timer, 1);
}

static void got_it(void)
{
    if (s_state != ST_PLAY) return;
    s_acertos++;
    show_feedback(true);
}

static void pass_word(void)
{
    if (s_state != ST_PLAY) return;
    s_passou++;
    bag_return_current();
    show_feedback(false);
}

static void begin_turn(void)
{
    stop_timer(&s_prep_timer);
    s_state = ST_PLAY;
    s_shown_secs = -1;
    next_card();          // a primeira palavra só aparece agora, fim da contagem
    sync_top();
    sync_footer();

    if (!livre_mode()) {
        s_turn_ms  = (uint32_t)tempo_secs() * 1000u;
        s_deadline = millis() + s_turn_ms;
        lv_obj_set_width(s_bar_fill, T_BAR_W);
        lv_obj_set_style_bg_color(s_bar_fill, lv_color_hex(s_accent), 0);
        stop_timer(&s_clock_timer);
        s_clock_timer = lv_timer_create(clock_tick_cb, 200, NULL);
    }
    sfx(KIT_SFX_CONFIRM);
}

static void prep_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_prep_left--;
    if (s_prep_left > 0) {
        show_count(s_prep_left);
        beep(880, 12);
    } else {
        begin_turn();
    }
}

static void start_prep(void)
{
    s_state = ST_PREP;
    s_acertos = 0;
    s_passou = 0;
    s_prep_left = PREP_SECS;

    tv_lock(true);
    if (s_tv) lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
    keep_awake(true);

    s_cur = -1;
    show_count(s_prep_left);   // "3" · "2" · "1" — a palavra fica escondida até o fim
    sync_top();
    sync_footer();
    lv_obj_set_width(s_bar_fill, T_BAR_W);
    lv_obj_set_style_bg_color(s_bar_fill, lv_color_hex(s_accent), 0);

    stop_timer(&s_prep_timer);
    s_prep_timer = lv_timer_create(prep_tick_cb, 1000, NULL);
    sfx(KIT_SFX_CONFIRM);
}

static void pass_turn(void)
{
    stop_all_timers();
    lv_obj_add_flag(s_over, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    s_state = ST_IDLE;
    s_cur = -1;
    tv_lock(false);
    keep_awake(false);

    show_word("SEGURE NA TESTA", KIT_COLOR_TEXT_MUTED);
    sync_top();
    sync_footer();
}

// A ação principal, sensível ao estado (botão PWR).
static void testa_action(void)
{
    if (!s_screen) return;
    switch (s_state) {
    case ST_IDLE:     start_prep(); break;
    case ST_PREP:     begin_turn(); break;   // pressa: pula a contagem
    case ST_PLAY:     got_it();     break;   // rede de segurança do gesto
    case ST_FEEDBACK: break;
    case ST_OVER:     pass_turn();  break;
    }
}

// Callback do gesto de inclinar (kit_imu). Só age em jogo.
static void on_tilt(kit_tilt_t dir, void *ud)
{
    (void)ud;
    if (s_state != ST_PLAY) return;
    if (dir == KIT_TILT_DOWN)      got_it();
    else if (dir == KIT_TILT_UP)   pass_word();
}

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
}

static void act_cb(lv_event_t *e) { (void)e; testa_action(); }
static void end_cb(lv_event_t *e) { (void)e; if (s_state == ST_PLAY) time_up(); }

// Toque em qualquer lugar do overlay TEMPO: só cala o alarme. A vez só passa
// no botão PASSAR A VEZ (pass_turn).
static void over_tap_cb(lv_event_t *e)
{
    (void)e;
    if (s_state == ST_OVER) stop_timer(&s_alarm_timer);
}

static void tempo_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_tempo_idx) return;
    s_tempo_idx = v; sync_segs(); save_prefs();
}
static void deck_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_deck_idx) return;
    s_deck_idx = v;
    rebuild_bag(); s_last_idx = -1; recent_clear();
    sync_segs(); sync_top(); save_prefs();
}

// ---------------------------------------------------------------------------
// Construção da tela
// ---------------------------------------------------------------------------

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, T_CHIP, T_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, T_PAD, 16);
    lv_obj_center(add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0));

    lv_obj_t *title = add_label(s_screen, "TESTA", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, T_PAD + T_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_align(dots, LV_ALIGN_TOP_RIGHT, -T_PAD, 40);
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

// Pílula de opção — mesma métrica da Mímica (altura 66, kit_mono_26).
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
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
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

// Baralhos: coluna de pills largas (5 opções não cabem numa linha só).
static void deck_col(lv_obj_t *parent)
{
    lv_obj_t *sec = plain_box(parent);
    lv_obj_set_size(sec, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sec, 10, 0);
    field_label(sec, "BARALHO");
    for (int i = 0; i < DECK_COUNT; i++) {
        lv_obj_t *p = lv_obj_create(sec);
        lv_obj_set_size(p, lv_pct(100), 64);
        lv_obj_set_style_bg_color(p, lv_color_hex(KIT_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(p, 0, 0);
        lv_obj_set_style_radius(p, 16, 0);
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(p, 10);
        lv_obj_add_event_cb(p, deck_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = add_label(p, deck_label(i), KIT_COLOR_TEXT, &kit_mono_20, 1);
        lv_obj_center(l);
        s_deck_pills[i] = p;
        s_deck_lbls[i] = l;
    }
}

static void build_page_adjust(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_left(p, T_PAD, 0);
    lv_obj_set_style_pad_right(p, T_PAD, 0);
    lv_obj_set_style_pad_top(p, 22, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 26, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    seg_row(p, "TEMPO", TEMPO_LBL, TEMPO_N, tempo_cb, s_tempo_pills, s_tempo_lbls);
    deck_col(p);
}

static lv_obj_t *make_footer_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb,
                                 int w, int h, lv_obj_t **out_lbl)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = add_label(b, txt, on_accent(), &kit_mono_26, 3);
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

static void build_page_game(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *box = lv_obj_create(tile);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Barra de tempo + M:SS
    s_bar_track = plain_box(box);
    lv_obj_set_size(s_bar_track, T_BAR_W, T_BAR_H);
    lv_obj_set_style_radius(s_bar_track, T_BAR_H / 2, 0);
    lv_obj_set_style_bg_color(s_bar_track, lv_color_hex(KIT_COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(s_bar_track, LV_OPA_COVER, 0);
    lv_obj_align(s_bar_track, LV_ALIGN_TOP_LEFT, T_PAD, 12);

    s_bar_fill = plain_box(s_bar_track);
    lv_obj_set_size(s_bar_fill, T_BAR_W, T_BAR_H);
    lv_obj_set_style_radius(s_bar_fill, T_BAR_H / 2, 0);
    lv_obj_set_style_bg_color(s_bar_fill, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_bar_fill, LV_OPA_COVER, 0);
    lv_obj_align(s_bar_fill, LV_ALIGN_LEFT_MID, 0, 0);

    s_clock_lbl = add_label(box, "1:00", KIT_COLOR_TEXT, &kit_mono_16, 1);
    lv_obj_align(s_clock_lbl, LV_ALIGN_TOP_RIGHT, -T_PAD, 8);

    // Faixa "ENCERRAR VEZ" (modo LIVRE)
    s_end_strip = lv_obj_create(box);
    lv_obj_set_size(s_end_strip, T_CONTENT, 44);
    lv_obj_set_style_radius(s_end_strip, 12, 0);
    lv_obj_set_style_bg_opa(s_end_strip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_end_strip, 2, 0);
    lv_obj_set_style_border_color(s_end_strip, lv_color_hex(KIT_COLOR_TEXT), 0);
    lv_obj_set_style_pad_all(s_end_strip, 0, 0);
    lv_obj_remove_flag(s_end_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_end_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_end_strip, 6);
    lv_obj_add_event_cb(s_end_strip, end_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(s_end_strip, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_center(add_label(s_end_strip, "ENCERRAR VEZ", KIT_COLOR_TEXT, &kit_mono_20, 2));
    lv_obj_add_flag(s_end_strip, LV_OBJ_FLAG_HIDDEN);

    // Palco: nome do baralho + palavra
    lv_obj_t *col = plain_box(box);
    lv_obj_set_size(col, T_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -18);

    s_top_lbl = add_label(col, cur_deck_name(), KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);

    s_word_lbl = add_label(col, "SEGURE NA TESTA", KIT_COLOR_TEXT_MUTED, &kit_mono_26, 1);
    lv_label_set_long_mode(s_word_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_word_lbl, T_CONTENT);
    lv_obj_set_style_text_align(s_word_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_word_lbl, 6, 0);
    lv_obj_set_style_pad_bottom(s_word_lbl, 10, 0);

    // Rodapé: dica do gesto (em jogo) + botão COMEÇAR/JÁ! (ocioso/contagem)
    s_hint_lbl = add_label(box, "INCLINE PRA BAIXO = ACERTOU\nINCLINE PRA CIMA = PASSOU",
                           KIT_COLOR_TEXT_MUTED, &kit_mono_16, 1);
    lv_label_set_long_mode(s_hint_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint_lbl, T_CONTENT);
    lv_obj_set_style_text_align(s_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint_lbl, LV_ALIGN_BOTTOM_MID, 0, -T_ACT_MARGIN);
    lv_obj_add_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);

    s_act_btn = make_footer_btn(box, "COMEÇAR", act_cb, T_CONTENT, T_ACT_H, &s_act_lbl);
    lv_obj_align(s_act_btn, LV_ALIGN_BOTTOM_MID, 0, -T_ACT_MARGIN);
}

static const char TESTA_RULES[] =
    "Uma pessoa segura o KIT na testa, com a tela virada pra roda. A roda "
    "ajuda, ela adivinha.\n\n"
    "1. Escolha o baralho e o tempo no Ajuste. Toque em COMEÇAR e encoste o "
    "KIT na testa. Depois da contagem 3, 2, 1 a primeira palavra aparece.\n\n"
    "2. A roda dá dicas pra pessoa acertar a palavra. Combinem antes o que "
    "vale: só falar, cantar, fazer gestos ou som. Não vale dizer a palavra "
    "nem parte dela.\n\n"
    "3. Acertou: incline o KIT pra baixo, tela pro chão. Flash verde e cai a "
    "próxima. O botão PWR também conta como acerto.\n\n"
    "4. Travou: incline pra cima, tela pro teto. Flash vermelho, a carta volta "
    "pro monte e conta como passou.\n\n"
    "Quando o TEMPO acaba, o KIT mostra quantas a pessoa fez. Se cada palpite "
    "valeu, quem decide é a roda. Passe o KIT adiante.";

static void build_page_help(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(p, T_PAD, 0);
    lv_obj_set_style_pad_top(p, 16, 0);
    lv_obj_set_style_pad_bottom(p, 32, 0);
    lv_obj_set_style_pad_row(p, 14, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);

    add_label(p, "COMO JOGA", KIT_COLOR_TEXT, &kit_mono_26, 3);
    lv_obj_t *body = add_label(p, TESTA_RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, T_CONTENT);
}

static void build_flash(void)
{
    s_flash = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_flash);
    lv_obj_set_size(s_flash, KIT_DISPLAY_WIDTH, T_PAGE_H);
    lv_obj_set_pos(s_flash, 0, T_TITLEBAR);
    lv_obj_set_style_bg_color(s_flash, lv_color_hex(KIT_COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *col = plain_box(s_flash);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_center(col);

    s_flash_ttl = add_label(col, "ACERTOU", KIT_COLOR_ON_COLOR, &kit_display_44, 2);
    s_flash_sub = add_label(col, "", KIT_COLOR_ON_COLOR, &kit_mono_20, 2);
    lv_label_set_long_mode(s_flash_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_flash_sub, T_CONTENT);
    lv_obj_set_style_text_align(s_flash_sub, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_overlay(void)
{
    s_over = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_over);
    lv_obj_set_size(s_over, KIT_DISPLAY_WIDTH, T_PAGE_H);
    lv_obj_set_pos(s_over, 0, T_TITLEBAR);
    lv_obj_set_style_bg_color(s_over, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_over, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_over, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_over, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_over, LV_OBJ_FLAG_CLICKABLE);   // tocar aqui só cala o alarme
    lv_obj_add_event_cb(s_over, over_tap_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(s_over);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 10, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -26);

    s_over_ttl = add_label(col, "TEMPO", on_accent(), &kit_display_72, 0);
    s_over_sc  = add_label(col, "ACERTOS 0    PASSOU 0", on_accent(), &kit_mono_20, 2);

    s_over_btn = lv_obj_create(s_over);
    lv_obj_set_size(s_over_btn, T_CONTENT, 64);
    lv_obj_set_style_radius(s_over_btn, 32, 0);
    lv_obj_set_style_border_width(s_over_btn, 2, 0);
    lv_obj_set_style_border_color(s_over_btn, lv_color_hex(on_accent()), 0);
    lv_obj_set_style_bg_opa(s_over_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_over_btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(s_over_btn, 0, 0);
    lv_obj_remove_flag(s_over_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_over_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_over_btn, 8);
    lv_obj_align(s_over_btn, LV_ALIGN_BOTTOM_MID, 0, -T_ACT_MARGIN);
    lv_obj_add_event_cb(s_over_btn, act_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(add_label(s_over_btn, "PASSAR A VEZ", on_accent(), &kit_mono_20, 3));
}

static void build_tileview(void)
{
    s_tv = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tv, KIT_DISPLAY_WIDTH, T_PAGE_H);
    lv_obj_set_pos(s_tv, 0, T_TITLEBAR);
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

static kit_err_t testa_start(uint32_t accent)
{
    if (s_screen) testa_destroy();
    s_accent = accent ? accent : KIT_COLOR_YELLOW;
    s_tempo_idx = 0;
    s_deck_idx  = 0;
    s_state = ST_IDLE;
    s_acertos = 0;
    s_passou = 0;
    s_cur = -1;
    s_last_idx = -1;
    recent_clear();
    load_prefs();
    rebuild_bag();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);   // cala o alarme mesmo tocando fora do overlay
    lv_obj_add_event_cb(s_screen, over_tap_cb, LV_EVENT_CLICKED, NULL);

    build_titlebar();
    build_tileview();
    build_flash();
    build_overlay();

    lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);   // começa no JOGO
    lv_obj_update_layout(s_screen);

    sync_segs();
    sync_top();
    sync_footer();
    sync_dots();

    const kit_api_table_t *t = api();
    if (t && t->imu && t->imu->register_tilt_callback)
        t->imu->register_tilt_callback(on_tilt, NULL);

    lv_screen_load(s_screen);
    return KIT_OK;
}

static void testa_destroy(void)
{
    const kit_api_table_t *t = api();
    if (t && t->imu && t->imu->register_tilt_callback)
        t->imu->register_tilt_callback(NULL, NULL);

    stop_all_timers();
    keep_awake(false);

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < TEMPO_N; i++) { s_tempo_pills[i] = NULL; s_tempo_lbls[i] = NULL; }
    for (int i = 0; i < DECK_COUNT; i++) { s_deck_pills[i] = NULL; s_deck_lbls[i] = NULL; }
    s_bar_track = s_bar_fill = s_clock_lbl = s_end_strip = NULL;
    s_top_lbl = s_word_lbl = s_hint_lbl = NULL;
    s_act_btn = s_act_lbl = NULL;
    s_flash = s_flash_ttl = s_flash_sub = NULL;
    s_over = s_over_ttl = s_over_sc = s_over_btn = NULL;
    s_state = ST_IDLE;
}

// ---------------------------------------------------------------------------
// Ciclo de vida da Tool (catálogo)
// ---------------------------------------------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (!ctx || !ctx->api) return KIT_ERR_INVALID_ARG;
    s_api = ctx->api;
    printf("[Testa] tool_init (id=%s)\n", ctx->tool_id ? ctx->tool_id : "?");
    return testa_start(KIT_COLOR_YELLOW);
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Testa] tool_destroy\n");
    testa_destroy();
    s_api = NULL;
}

#else  /* KIT_SDK_STUBS — build nativo: só compila, sem UI */

#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Testa stub] tool_init — UI atrás de KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
