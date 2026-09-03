/**
 * @file main.c
 * @brief Quebra-Gelo — mini-jogo de mesa / quebra-gelo para o KIT.
 *
 * Linguagem visual "Brutalist Bauhaus" (kit_theme.h / kit_fonts.h).
 * Toda a UI fica atrás de #ifndef KIT_SDK_STUBS — ver tool_lvgl_runtime.md.
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

#define Q_PAD        16
#define Q_CONTENT    (KIT_DISPLAY_WIDTH - 2 * Q_PAD)            // 336
#define Q_TITLEBAR   88
#define Q_FOOT       104
#define Q_CHIP       56
#define Q_GO_H       76
#define Q_GO_MARGIN  18
#define Q_PAGE_H     (KIT_DISPLAY_HEIGHT - Q_TITLEBAR - Q_FOOT) // 256

// Flicker do sorteio: desacelera em ease-out (o intervalo cresce a cada troca)
// pra casar com a catraca da Garrafa (KIT_SFX_BOTTLE_SPIN, ~1,9 s).
#define Q_DRAW_TICKS   20      // 19 trocas + a trava
#define Q_DRAW_MS_MIN  34
#define Q_DRAW_MS_MAX  165

// Baralho fixo de perguntas quebra-gelo — leves e criativas, todas em CAIXA
// ALTA (regra da tipografia mono).
static const char *const QUESTIONS[] = {
    "SE VOCÊ FOSSE UM ELETRODOMÉSTICO, QUAL SERIA?",
    "QUAL SUPERPODER INÚTIL VOCÊ GOSTARIA DE TER?",
    "QUAL MÚSICA GRUDA NA SUA CABEÇA COM MAIS FACILIDADE?",
    "QUAL COMIDA VOCÊ COMERIA TODO DIA SEM ENJOAR?",
    "SE OS ANIMAIS FALASSEM, QUAL SERIA O MAIS CHATO?",
    "QUAL NOME VOCÊ DARIA PARA UM BARCO?",
    "QUAL TALENTO ESTRANHO VOCÊ TEM?",
    "QUAL FILME VOCÊ JÁ ASSISTIU MAIS VEZES?",
    "SE VOCÊ PUDESSE JANTAR COM UM FAMOSO, QUEM SERIA?",
    "QUAL O MELHOR CHEIRO DO MUNDO?",
    "QUAL APLICATIVO VOCÊ MAIS USA SEM PERCEBER?",
    "QUAL FOI A ÚLTIMA COISA QUE TE FEZ RIR ALTO?",
    "QUAL LUGAR VOCÊ SONHA EM VISITAR?",
    "SE VOCÊ ABRISSE UM RESTAURANTE, QUAL SERIA O PRATO?",
    "QUAL DESENHO ANIMADO MARCOU SUA INFÂNCIA?",
    "QUAL A PIOR MODA QUE VOCÊ JÁ SEGUIU?",
    "QUAL OBJETO INÚTIL VOCÊ NUNCA CONSEGUE JOGAR FORA?",
    "SE VOCÊ VIRASSE UMA ESTAÇÃO DO ANO, QUAL SERIA?",
    "QUAL PALAVRA VOCÊ ACHA ENGRAÇADA DE FALAR?",
    "QUAL FOI SUA MAIOR GAFE RECENTE?",
    "SE PUDESSE APAGAR UMA MÚSICA DA HISTÓRIA, QUAL SERIA?",
    "QUAL HABILIDADE VOCÊ QUERIA APRENDER DO NADA?",
    "QUAL O SEU PEDIDO SECRETO NO CARDÁPIO?",
    "QUAL PERSONAGEM DE FICÇÃO VOCÊ LEVARIA PRA MORAR COM VOCÊ?",
    "QUAL O MELHOR PRESENTE QUE VOCÊ JÁ GANHOU?",
    "SE SEU DIA TIVESSE TRILHA SONORA, O QUE TOCARIA AGORA?",
    "QUAL COMIDA ESTRANGEIRA VOCÊ QUER PROVAR?",
    "QUAL FOI A MELHOR SESTA DA SUA VIDA?",
    "QUAL APELIDO VOCÊ TEVE NA ESCOLA?",
    "SE VOCÊ FOSSE UM SORVETE, QUAL SABOR SERIA?",
    "QUAL TAREFA DOMÉSTICA VOCÊ ODEIA DE VERDADE?",
    "QUAL A COISA MAIS CARA QUE VOCÊ JÁ QUEBROU?",
    "QUAL SÉRIE VOCÊ ABANDONOU E NÃO SE ARREPENDE?",
    "QUAL O MELHOR CONSELHO INÚTIL QUE VOCÊ JÁ RECEBEU?",
    "SE VOCÊ FALASSE UMA LÍNGUA NOVA AGORA, QUAL SERIA?",
    "QUAL COMIDA DE FESTA VOCÊ ATACA PRIMEIRO?",
    "QUAL FOI O SEU PRIMEIRO SHOW OU EVENTO?",
    "QUAL LOJA VOCÊ ENTRA SÓ PRA OLHAR E SAI COMPRANDO?",
    "SE GANHASSE UM ANIMAL EXÓTICO, QUAL VOCÊ TOPARIA?",
    "QUAL O SOM QUE MAIS TE IRRITA?",
    "QUAL A MELHOR VIAGEM MAL PLANEJADA QUE VOCÊ FEZ?",
    "QUAL COISA DE ADULTO VOCÊ AINDA NÃO SABE FAZER?",
    "QUAL EMOJI TE REPRESENTA HOJE?",
    "SE VOCÊ TIVESSE UM PROGRAMA DE TV, QUAL SERIA O TEMA?",
    "QUAL MÚSICA VOCÊ CANTA ALTO SÓ QUANDO ESTÁ SOZINHA?",
    "QUAL O MELHOR LANCHE DA MADRUGADA DA SUA VIDA?",
    "QUAL LUGAR DA SUA CIDADE VOCÊ MOSTRARIA A UM TURISTA?",
    "QUAL BRINQUEDO VOCÊ QUERIA E NUNCA GANHOU?",
    "SE VOCÊ VIRASSE UMA PLANTA, QUAL SERIA?",
    "QUAL A DECISÃO MAIS ALEATÓRIA QUE DEU CERTO PRA VOCÊ?",
    "QUAL PERSONAGEM MERECIA UM FINAL MELHOR?",
    "QUAL CHEIRO TE LEVA DIRETO PRA INFÂNCIA?",
    "SE PUDESSE SER UM FENÔMENO DA NATUREZA, QUAL SERIA?",
    "QUAL JOGO DE TABULEIRO JÁ DESTRUIU UMA AMIZADE SUA?",
    "QUAL COMBINAÇÃO ESTRANHA DE COMIDA VOCÊ ADORA?",
    "QUAL SOBREMESA DEVERIA SER PRATO PRINCIPAL?",
    "SE O SEU NOME TIVESSE UM SIGNIFICADO SECRETO, QUAL SERIA?",
    "QUAL COISA SIMPLES TE DEIXA MUITO FELIZ?",
    "QUAL O PIOR FILME QUE VOCÊ ADOROU ASSISTIR?",
    "SE VOCÊ FOSSE UM BRINQUEDO DE PARQUE, QUAL SERIA?",
    "QUAL MÚSICA É SEU 'GUILTY PLEASURE' ABSOLUTO?",
    "QUAL A COISA MAIS ANTIGA QUE VOCÊ TEM NO ARMÁRIO?",
    "SE VOCÊ CRIASSE UM FERIADO NACIONAL, QUAL SERIA?",
    "QUAL HÁBITO ESTRANHO VOCÊ TEM QUANDO NINGUÉM VÊ?",
    "QUAL SOBRENOME VOCÊ ESCOLHERIA SE PUDESSE MUDAR?",
    "QUAL SUPERMERCADO VOCÊ MORARIA DENTRO?",
    "SE SUA VIDA FOSSE UM LIVRO, QUAL SERIA O TÍTULO?",
    "QUAL PIADA BOBA SEMPRE TE FAZ RIR?",
    "QUAL TRABALHO ESTRANHO VOCÊ GOSTARIA DE EXPERIMENTAR?",
    "SE VOCÊ PUDESSE TER QUALQUER TRANSPORTE MÁGICO, QUAL SERIA?",
    "QUAL CIDADE TE FEZ SENTIR EM OUTRO PLANETA?",
    "QUAL PEÇA DE ROUPA VOCÊ NUNCA USARIA DE JEITO NENHUM?",
    "QUAL A MAIOR VERDADE QUE DEMOROU PRA VOCÊ APRENDER?",
    "SE VOCÊ FOSSE UM FANTASMA, QUAL LUGAR ASSOMBRARIA?",
    "QUAL O PIOR PRESENTE QUE VOCÊ JÁ RECEBEU COM UM SORRISO?",
    "QUAL MENSAGEM VOCÊ MANDARIA PRO SEU 'EU' DE 10 ANOS ATRÁS?",
    "QUAL RESTAURANTE QUE FECHOU VOCÊ QUERIA DE VOLTA?",
    "SE VOCÊ TIVESSE UM CANAL DE CULINÁRIA, QUAL SERIA O FOCO?",
    "QUAL LIVRO VOCÊ GOSTARIA DE ESQUECER PRA LER DE NOVO?",
    "QUAL A COISA MAIS CORAJOSA QUE VOCÊ JÁ FEZ SEM PENSAR?",
    "QUAL OBJETO DO FUTURO VOCÊ MAIS QUER VER EXISTIR?",
    "SE VOCÊ PUDESSE CONVERSAR COM QUALQUER PLANTA, QUAL SERIA?",
    "QUAL FOI A COINCIDÊNCIA MAIS BIZARRA DA SUA VIDA?",
    "QUAL CANÇÃO NÃO PODE FALTAR NO CHURRASCO?",
    "QUAL A REGRA MAIS INÚTIL DA VIDA SOCIAL?",
    "QUAL COMIDA DE CRIANÇA VOCÊ COME ATÉ HOJE?",
    "SE VOCÊ CRIASSE UMA REDE SOCIAL, QUAL SERIA A REGRA Nº 1?",
    "QUAL HORÁRIO DO DIA VOCÊ RENDE MAIS?",
    "QUAL SERIA O SEU PODER NUM VIDEOGAME DA VIDA REAL?",
    "QUAL COISA VOCÊ FAZ DIFERENTE DE TODO MUNDO?",
    "QUAL FOI O MELHOR OI QUE VOCÊ JÁ RECEBEU?",
    "QUAL É A SUA TEORIA MALUCA FAVORITA?",
    "QUAL CENA DE FILME VOCÊ QUERIA TER VIVIDO?",
    "QUAL O SEU MAIOR ORGULHO BOBO?",
    "SE O SEU HUMOR DE HOJE FOSSE UM CLIMA, QUAL SERIA?",
    "QUAL FOI A GENTILEZA ALEATÓRIA MAIS LEGAL QUE TE FIZERAM?",
};
#define QUESTIONS_N ((int)(sizeof(QUESTIONS) / sizeof(QUESTIONS[0])))

// --- estado --------------------------------------------------------------
static const kit_api_table_t *s_api = NULL;

static uint32_t s_accent  = KIT_COLOR_BLUE;
static bool     s_drawing = false;
static bool     s_drawn   = false;    // já houve ao menos um sorteio
static int      s_last    = -1;       // índice sorteado anterior
static int      s_target  = -1;       // índice sorteado da rodada atual
static int      s_tick    = 0;
static lv_timer_t *s_timer = NULL;

// --- objetos LVGL -------------------------------------------------------
static lv_obj_t *s_screen   = NULL;
static lv_obj_t *s_lead_in  = NULL;
static lv_obj_t *s_phrase   = NULL;
static lv_obj_t *s_lead_out = NULL;
static lv_obj_t *s_go_btn   = NULL;

// --- helpers ----------------------------------------------------------

static inline uint32_t on_accent(void)
{
    return (s_accent == KIT_COLOR_YELLOW) ? KIT_COLOR_ON_YELLOW : KIT_COLOR_ON_COLOR;
}

static int rnd_index(void)
{
    int i = 0;
    int guard = 16;
    do {
        if (s_api && s_api->random)
            i = (int)s_api->random->range(0, QUESTIONS_N - 1);
    } while (i == s_last && QUESTIONS_N > 1 && --guard > 0);
    return i;
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

// --- sorteio -----------------------------------------------------------

static void draw_tick_cb(lv_timer_t *t);

static void do_draw(void)
{
    if (s_drawing || !s_phrase) return;
    s_drawing = true;
    s_target = rnd_index();

    if (!s_drawn) {
        s_drawn = true;
        lv_obj_remove_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(s_phrase, lv_color_hex(KIT_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_opa(s_go_btn, LV_OPA_60, 0);   // "ocupado"

    s_tick = 0;
    if (s_api && s_api->audio) s_api->audio->sfx(KIT_SFX_BOTTLE_SPIN);
    s_timer = lv_timer_create(draw_tick_cb, Q_DRAW_MS_MIN, NULL);
}

static void draw_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_tick++;

    if (s_tick < Q_DRAW_TICKS) {
        int i = 0;
        if (s_api && s_api->random)
            i = (int)s_api->random->range(0, QUESTIONS_N - 1);
        lv_label_set_text(s_phrase, QUESTIONS[i]);

        // desacelera junto com a catraca (intervalo cresce até Q_DRAW_MS_MAX)
        uint32_t p = Q_DRAW_MS_MIN +
            (uint32_t)(Q_DRAW_MS_MAX - Q_DRAW_MS_MIN) * s_tick / (Q_DRAW_TICKS - 1);
        lv_timer_set_period(s_timer, p);
        return;
    }

    // trava na pergunta sorteada
    s_last = s_target;
    lv_label_set_text(s_phrase, QUESTIONS[s_target]);
    lv_obj_set_style_text_color(s_phrase, lv_color_hex(s_accent), 0);

    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_drawing = false;
    lv_obj_set_style_opa(s_go_btn, LV_OPA_COVER, 0);
}

static void on_shake(void *user_data)
{
    (void)user_data;
    do_draw();
}

// --- callbacks ------------------------------------------------------

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_api && s_api->system) s_api->system->exit();
}

static void draw_cb(lv_event_t *e)
{
    (void)e;
    do_draw();
}

// --- construção da tela -------------------------------------------

static void build_titlebar(void)
{
    lv_obj_t *chip = lv_obj_create(s_screen);
    lv_obj_set_size(chip, Q_CHIP, Q_CHIP);
    lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 18, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(chip, 12);
    lv_obj_add_event_cb(chip, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_TOP_LEFT, Q_PAD, 16);

    lv_obj_t *g = add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(g);

    lv_obj_t *title = add_label(s_screen, "QUEBRA-GELO", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, Q_PAD + Q_CHIP + 12, 30);
}

static void build_stage(void)
{
    lv_obj_t *stage = lv_obj_create(s_screen);
    lv_obj_remove_style_all(stage);
    lv_obj_set_size(stage, KIT_DISPLAY_WIDTH, Q_PAGE_H);
    lv_obj_set_pos(stage, 0, Q_TITLEBAR);
    lv_obj_remove_flag(stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stage, draw_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *col = plain_box(stage);
    lv_obj_set_size(col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_center(col);

    s_lead_in = add_label(col, "PERGUNTA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);
    lv_obj_add_flag(s_lead_in, LV_OBJ_FLAG_HIDDEN);

    s_phrase = add_label(col, "TOQUE EM SORTEAR", KIT_COLOR_TEXT_MUTED, &kit_mono_26, 2);
    lv_label_set_long_mode(s_phrase, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_phrase, Q_CONTENT);
    lv_obj_set_height(s_phrase, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(s_phrase, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_phrase, 6, 0);
    lv_obj_set_style_pad_bottom(s_phrase, 10, 0);

    s_lead_out = add_label(col, "PASSE ADIANTE", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);
    lv_obj_add_flag(s_lead_out, LV_OBJ_FLAG_HIDDEN);
}

static void build_footer(void)
{
    s_go_btn = lv_obj_create(s_screen);
    lv_obj_set_size(s_go_btn, Q_CONTENT, Q_GO_H);
    lv_obj_set_style_radius(s_go_btn, Q_GO_H / 2, 0);
    lv_obj_set_style_border_width(s_go_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_go_btn, 0, 0);
    lv_obj_set_style_pad_all(s_go_btn, 0, 0);
    lv_obj_set_style_bg_color(s_go_btn, lv_color_hex(s_accent), 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_go_btn, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_remove_flag(s_go_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_go_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_go_btn, 8);
    lv_obj_align(s_go_btn, LV_ALIGN_BOTTOM_MID, 0, -Q_GO_MARGIN);
    lv_obj_add_event_cb(s_go_btn, draw_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l = add_label(s_go_btn, "SORTEAR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(l);
}

// --- ciclo de vida da Tool ---------------------------------------

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    printf("[Quebra-Gelo] tool_init\n");
    s_api = ctx ? ctx->api : NULL;

    s_accent  = KIT_COLOR_BLUE;
    s_drawing = false;
    s_drawn   = false;
    s_last    = -1;
    s_target  = -1;
    s_tick    = 0;

    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(on_shake, NULL);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(KIT_COLOR_BG), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_titlebar();
    build_stage();
    build_footer();

    lv_screen_load(s_screen);
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void)
{
    printf("[Quebra-Gelo] tool_destroy\n");
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_api && s_api->imu)
        s_api->imu->register_shake_callback(NULL, NULL);
    s_drawing = false;

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_lead_in = s_phrase = s_lead_out = s_go_btn = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Quebra-Gelo stub] tool_init — compila nativo; UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
