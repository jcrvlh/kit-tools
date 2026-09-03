/**
 * @file main.c
 * @brief Veto — mini-jogo de mesa para o KIT.
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

#include "lvgl.h"

#define KIT_DISPLAY_WIDTH   368
#define KIT_DISPLAY_HEIGHT  448

#define ESP_LOGI(tag, fmt, ...) printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[%s] WARN: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[%s] ERROR: " fmt "\n", tag, ##__VA_ARGS__)

static const kit_api_table_t *s_api = NULL;
static inline const kit_api_table_t *api(void) { return s_api; }

// Veto — linguagem "Brutalist Bauhaus" (ver docs/design/design-language.md).
// Mini-jogo de mesa: descreva a PALAVRA-ALVO sem dizer ela nem as PROIBIDAS.
// Filosofia do Bingo: o KIT sorteia (as cartas), cronometra e toca a cigarra;
// a mesa confere. O placar da vez (acertos/erros/pulos) fica ESCONDIDO
// enquanto ela corre e é revelado no overlay de TEMPO — nada de placar
// acumulado nem times no aparelho.
//
// Estrutura de 3 páginas, espelhando a Adedonha/Pavio:
//   0 AJUSTE   — TEMPO (60S/90S/120S/OFF) · PROIBIDAS (2/3) · BARALHO
//                (FÁCIL/TUDO) · PULOS (1/2/3/LIVRES/OFF) · REINICIAR BARALHO (dois
//                toques). Tudo em Storage (vt_tempo/vt_proib/vt_deck/vt_pulos).
//   1 JOGO     — a barra de tempo amarela drenando, a palavra-alvo grande
//                (kit_display_44), as proibidas em sequência horizontal e a barra
//                integrada com botões DISLIKE (👎), PULAR e JOINHA (👍).
//   2 COMO JOGA — as regras, corpo rolável.
//
// Cor da Tool: AMARELO. JOINHA/COMEÇAR e o overlay são amarelos (ação); o
// vermelho (quadrado = proibido) marca a lista de proibidas e o botão DISLIKE.
//
// Sem chacoalhar: quem descreve gesticula muito e viraria acerto acidental. E
// o palco NÃO é tocável — JOINHA é o botão amarelo cheio (ou o PWR); DISLIKE e
// PULAR são integrados na barra de ações inferior.
//
// Animação zero: só troca de texto por carta. Nada de transform_scale/rotation
// nem opa intermediário em container — força layer buffer e estoura o render
// no CO5300/PSRAM. A barra de tempo é lv_obj_set_width numa caixa (barato),
// atualizada a 5 Hz (o buffer do display é parcial de 40 linhas).

static const char *TAG = "KIT_VETO";

// ---------------------------------------------------------------------------
// Layout (espelha as métricas da Adedonha)
// ---------------------------------------------------------------------------
#define X_PAD        16
#define X_CONTENT    (KIT_DISPLAY_WIDTH - 2 * X_PAD)              // 336
#define X_TITLEBAR   88
#define X_PAGE_H     (KIT_DISPLAY_HEIGHT - X_TITLEBAR)            // 360
#define X_CHIP       56
#define PAGES        3

// Rodapé do JOGO: ERROU (👎), PULAR e CERTO (👍) integrados na mesma linha.
#define CERTO_H      74
#define CERTO_MARGIN 14
#define STAGE_H      (X_PAGE_H - (CERTO_MARGIN + CERTO_H + 12))   // 260

#define CLOCK_TICK_MS  200
#define TICK_LEAD_S    5        // tique nos últimos N s
#define WARN_LEAD_S    10       // barra pisca / relógio acende nos últimos N s
#define LIVRE_CAP_S    900      // teto do modo LIVRE (keep_awake não fica eterno)
#define ALARM_REPEAT_MS 3400    // re-toca o alarme no overlay de TEMPO
#define RESET_ARM_MS   4000     // REINICIAR BARALHO: janela do 2º toque

#define K_TEMPO      "vt_tempo"
#define K_PROIB      "vt_proib"
#define K_DECK       "vt_deck"
#define K_PULOS      "vt_pulos"
#define K_TEMPO_OLD  "tb_tempo"
#define K_PROIB_OLD  "tb_proib"
#define K_DECK_OLD   "tb_deck"
#define K_PULOS_OLD  "tb_pulos"

// TEMPO da vez (segundos; 0 = LIVRE).
static const int  TEMPO_S[4]        = { 60, 90, 120, 0 };
static const char *const TEMPO_LBL[4] = { "60S", "90S", "120S", "OFF" };
static const char *const PROIB_LBL[2] = { "2", "3" };
static const char *const DECK_LBL[2]  = { "FÁCIL", "TUDO" };
static const int  SKIP_LIM[5]         = { 1, 2, 3, -1, 0 };     // -1 = livres, 0 = off
static const char *const SKIP_LBL[5]  = { "1", "2", "3", "LIVRES", "OFF" };

#define DECK_FACIL 0
#define DECK_TUDO  1

// ---------------------------------------------------------------------------
// Ícones A8 — Joinha (👍) e Dislike (👎)
// ---------------------------------------------------------------------------
static const uint8_t s_icon_thumbs_up_map[784] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x60, 0x9F, 0x60, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9B, 0xFF, 0xFF, 0xFF, 0x9B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFB, 0xFF, 0xFF, 0xFF,
    0xFB, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0x60, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xBB, 0xFF, 0xFF, 0xFF, 0xFF, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x5C, 0xFF, 0xFF, 0xFF, 0xFF, 0xF3, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0xF3, 0xFF, 0xFF,
    0xFF, 0xFF, 0x9B, 0x80, 0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x80, 0x60, 0x00, 0x00,
    0x40, 0x83, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0x80, 0x80, 0x80, 0x80, 0x38, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xFF, 0xFF, 0xFF, 0x60, 0x00, 0x80, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF,
    0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00,
    0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF,
    0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00,
    0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xFF,
    0xFF, 0xFF, 0x60, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFB, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x80, 0x60, 0x00, 0x00,
    0x40, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x38, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const lv_image_dsc_t s_icon_thumbs_up = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_A8,
    .header.flags = 0,
    .header.w = 28,
    .header.h = 28,
    .header.stride = 28,
    .data_size = sizeof(s_icon_thumbs_up_map),
    .data = s_icon_thumbs_up_map,
};

static const uint8_t s_icon_thumbs_down_map[784] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
    0x80, 0x60, 0x00, 0x00, 0x40, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xFF, 0xFF, 0xFF, 0x60, 0x00,
    0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x38,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF,
    0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00,
    0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF,
    0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00,
    0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x60, 0xFF, 0xFF, 0xFF, 0x60, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60,
    0x80, 0x60, 0x00, 0x00, 0x40, 0x83, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0x80, 0x80,
    0x80, 0x80, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x48, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA3, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0xF3, 0xFF, 0xFF, 0xFF, 0xFF, 0x9B, 0x80,
    0x80, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5C, 0xFF, 0xFF, 0xFF, 0xFF, 0xF3, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xBB, 0xFF, 0xFF, 0xFF, 0xFF, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFB, 0xFF, 0xFF, 0xFF,
    0xFF, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFB, 0xFF, 0xFF, 0xFF, 0xFB, 0x14, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x9B, 0xFF, 0xFF, 0xFF, 0x9B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x60, 0x9F, 0x60, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const lv_image_dsc_t s_icon_thumbs_down = {
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_A8,
    .header.flags = 0,
    .header.w = 28,
    .header.h = 28,
    .header.stride = 28,
    .data_size = sizeof(s_icon_thumbs_down_map),
    .data = s_icon_thumbs_down_map,
};

// ---------------------------------------------------------------------------
// Baralho — palavra-alvo + 3 proibidas, tudo em CAIXA ALTA. O ALVO sai amarelo
// em kit_mono_26 (quebra em linhas em vez de cortar); as PROIBIDAS em
// kit_mono_20 (cobre Latin-1 inteiro), curtas — são as pistas óbvias.
// ---------------------------------------------------------------------------
typedef struct { const char *alvo; const char *proib[3]; } tb_card_t;

static const tb_card_t TB_FACIL[] = {
    { "CACHORRO",  { "LATIR", "OSSO", "FOCINHO" } },
    { "GATO",      { "MIAR", "BIGODE", "RATO" } },
    { "PRAIA",     { "MAR", "AREIA", "SOL" } },
    { "PIZZA",     { "QUEIJO", "FATIA", "ITÁLIA" } },
    { "CHUVA",     { "GUARDA-CHUVA", "MOLHAR", "NUVEM" } },
    { "FUTEBOL",   { "BOLA", "GOL", "TIME" } },
    { "PIPOCA",    { "MILHO", "CINEMA", "ESTOURAR" } },
    { "FOGUETE",   { "ESPAÇO", "LANÇAR", "NASA" } },
    { "CORAÇÃO",   { "AMOR", "BATER", "SANGUE" } },
    { "TUBARÃO",   { "MAR", "DENTE", "NADAR" } },
    { "AVIÃO",     { "VOAR", "AEROPORTO", "ASA" } },
    { "LIMÃO",     { "AZEDO", "VERDE", "CAIPIRINHA" } },
    { "DRAGÃO",    { "FOGO", "LENDA", "CUSPIR" } },
    { "BRUXA",     { "VASSOURA", "FEITIÇO", "CHAPÉU" } },
    { "PALHAÇO",   { "CIRCO", "NARIZ", "RIR" } },
    { "GUITARRA",  { "CORDA", "ROCK", "TOCAR" } },
    { "SANFONA",   { "FORRÓ", "FOLE", "NORDESTE" } },
    { "DENTISTA",  { "DENTE", "CÁRIE", "BROCA" } },
    { "BOMBEIRO",  { "FOGO", "MANGUEIRA", "RESGATE" } },
    { "SORVETE",   { "GELADO", "CASQUINHA", "SABOR" } },
    { "ESCOVA",    { "DENTE", "CABELO", "CERDA" } },
    { "GELADEIRA", { "GELO", "COZINHA", "PORTA" } },
    { "BICICLETA", { "PEDAL", "RODA", "GUIDÃO" } },
    { "TRAVESSEIRO", { "DORMIR", "CAMA", "MACIO" } },
    { "CHURRASCO", { "CARNE", "BRASA", "ESPETO" } },
    { "VULCÃO",    { "LAVA", "ERUPÇÃO", "MONTANHA" } },
    { "PINGUIM",   { "GELO", "ANTÁRTICA", "NADAR" } },
    { "GIRAFA",    { "PESCOÇO", "ÁFRICA", "ALTA" } },
    { "ELEFANTE",  { "TROMBA", "GRANDE", "MARFIM" } },
    { "FORMIGA",   { "PEQUENA", "FORMIGUEIRO", "AÇÚCAR" } },
    { "ARANHA",    { "TEIA", "OITO", "PICADA" } },
    { "ABELHA",    { "MEL", "FERRÃO", "COLMEIA" } },
    { "CENOURA",   { "LARANJA", "COELHO", "HORTA" } },
    { "BATATA",    { "FRITA", "PURÊ", "CHIPS" } },
    { "MELANCIA",  { "VERÃO", "SEMENTE", "VERMELHA" } },
    { "ABACAXI",   { "COROA", "ESPINHO", "CAIPIRINHA" } },
    { "PADARIA",   { "PÃO", "MANHÃ", "FILA" } },
    { "HOSPITAL",  { "MÉDICO", "DOENTE", "AMBULÂNCIA" } },
    { "CHINELO",   { "PÉ", "BORRACHA", "DEDO" } },
    { "ESPELHO",   { "REFLEXO", "BANHEIRO", "IMAGEM" } },
    { "TESOURA",   { "CORTAR", "PAPEL", "LÂMINA" } },
    { "MARTELO",   { "PREGO", "BATER", "FERRAMENTA" } },
    { "VASSOURA",  { "VARRER", "CABO", "CERDA" } },
    { "PANELA",    { "COZINHAR", "TAMPA", "FOGÃO" } },
    { "CANETA",    { "ESCREVER", "TINTA", "PAPEL" } },
    { "MOCHILA",   { "COSTAS", "ESCOLA", "ZÍPER" } },
    { "FAROL",     { "CARRO", "LUZ", "NOITE" } },
    { "PONTE",     { "RIO", "ATRAVESSAR", "CONCRETO" } },
    { "CASTELO",   { "REI", "MURALHA", "PRINCESA" } },
    { "PIRATA",    { "TESOURO", "NAVIO", "PAPAGAIO" } },
    { "ESQUELETO", { "OSSO", "CAVEIRA", "CORPO" } },
    { "VAMPIRO",   { "SANGUE", "MORCEGO", "PRESA" } },
    { "FANTASMA",  { "ASSOMBRAR", "LENÇOL", "MEDO" } },
    { "FOGUEIRA",  { "LENHA", "CHAMA", "ACAMPAMENTO" } },
    { "SEMÁFORO",  { "VERDE", "VERMELHO", "TRÂNSITO" } },
    { "CHUVEIRO",  { "BANHO", "ÁGUA", "QUENTE" } },
    { "GARFO",     { "COMER", "TALHER", "DENTE" } },
    { "JACARÉ",    { "PANTANAL", "RÉPTIL", "DENTE" } },
    { "CAJU",      { "CASTANHA", "FRUTA", "SUCO" } },
};
#define TB_FACIL_N ((int)(sizeof(TB_FACIL) / sizeof(TB_FACIL[0])))

static const tb_card_t TB_TUDO[] = {
    { "SAUDADE",   { "SENTIR", "FALTA", "LONGE" } },
    { "INVEJA",    { "CIÚME", "COBIÇA", "PECADO" } },
    { "DESTINO",   { "SORTE", "FUTURO", "CAMINHO" } },
    { "SEGREDO",   { "CONTAR", "ESCONDER", "SIGILO" } },
    { "MENTIRA",   { "VERDADE", "ENGANAR", "NARIZ" } },
    { "ORGULHO",   { "VAIDADE", "EGO", "HUMILDE" } },
    { "VERGONHA",  { "CORAR", "TIMIDEZ", "MICO" } },
    { "CORAGEM",   { "MEDO", "HERÓI", "ENFRENTAR" } },
    { "PREGUIÇA",  { "CANSAÇO", "SOFÁ", "BICHO" } },
    { "FOFOCA",    { "FALAR", "SEGREDO", "VIZINHA" } },
    { "PROMESSA",  { "JURAR", "CUMPRIR", "PALAVRA" } },
    { "PERDÃO",    { "DESCULPA", "PERDOAR", "CULPA" } },
    { "RESPEITO",  { "EDUCAÇÃO", "TRATAR", "ADMIRAR" } },
    { "ROTINA",    { "TODO DIA", "HÁBITO", "MESMICE" } },
    { "AMIZADE",   { "AMIGO", "PARCERIA", "CONFIANÇA" } },
    { "MEDO",      { "SUSTO", "ESCURO", "FOBIA" } },
    { "SORTE",     { "TREVO", "GANHAR", "ACASO" } },
    { "TEMPO",     { "RELÓGIO", "PASSAR", "HORAS" } },
    { "SONHO",     { "DORMIR", "ACORDAR", "DESEJO" } },
    { "VIAGEM",    { "MALA", "DESTINO", "PASSAGEM" } },
    { "DINHEIRO",  { "NOTA", "RICO", "CARTEIRA" } },
    { "TRABALHO",  { "EMPREGO", "CHEFE", "SALÁRIO" } },
    { "DANÇA",     { "PASSO", "RITMO", "SALÃO" } },
    { "TEATRO",    { "PALCO", "ATOR", "PLATEIA" } },
    { "CANÇÃO",    { "CANTAR", "MELODIA", "LETRA" } },
    { "NATUREZA",  { "FLORESTA", "VERDE", "ANIMAIS" } },
    { "UNIVERSO",  { "ESTRELA", "PLANETA", "INFINITO" } },
    { "IDIOMA",    { "LÍNGUA", "FALAR", "TRADUÇÃO" } },
    { "JORNAL",    { "REPÓRTER", "MANCHETE", "NOTÍCIA" } },
    { "CONTO",     { "NARRAR", "LIVRO", "HISTÓRIA" } },
};
#define TB_TUDO_N  ((int)(sizeof(TB_TUDO) / sizeof(TB_TUDO[0])))
#define DECK_MAX   (TB_FACIL_N + TB_TUDO_N)

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
typedef enum { ST_IDLE, ST_RUN, ST_TIMEUP } tb_state_t;

static uint32_t   s_accent = KIT_COLOR_YELLOW;
static tb_state_t s_state  = ST_IDLE;

static int s_tempo_i = 0;            // índice em TEMPO_S
static int s_proib_i = 1;            // 0 -> 2 proibidas, 1 -> 3
static int s_deck_i  = DECK_FACIL;
static int s_pulos_i = 3;            // índice em SKIP_LIM (3 -> LIVRES)

// Placar da vez (escondido até o overlay).
static int s_hits = 0, s_fouls = 0, s_skips = 0;

// Baralho: saco embaralhado sem reposição (uma sessão de festa = uma sentada).
static int s_bag[DECK_MAX];
static int s_bag_n = 0, s_bag_pos = 0;
static int s_last_card = -1;
static int s_cur = -1;              // carta na tela

// Relógio da vez.
static uint64_t s_started_at = 0;
static uint64_t s_deadline   = 0;   // 0 = LIVRE
static uint32_t s_total_ms   = 0;
static int      s_shown_sec  = -1;
static int      s_tick_sec   = -1;
static int      s_blink      = 0;
static bool     s_by_time    = true;   // overlay veio de tempo esgotado?

static lv_timer_t *s_clock = NULL;
static lv_timer_t *s_alarm = NULL;

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

// Página 0 — AJUSTE
static lv_obj_t *s_tempo_pills[4]; static lv_obj_t *s_tempo_lbls[4];
static lv_obj_t *s_proib_pills[2]; static lv_obj_t *s_proib_lbls[2];
static lv_obj_t *s_deck_pills[2];  static lv_obj_t *s_deck_lbls[2];
static lv_obj_t *s_pulos_pills[5]; static lv_obj_t *s_pulos_lbls[5];
static lv_obj_t *s_reset_btn = NULL;
static lv_obj_t *s_reset_lbl = NULL;

// Página 1 — JOGO
static lv_obj_t *s_kicker    = NULL;
static lv_obj_t *s_bar_track = NULL;
static lv_obj_t *s_bar_fill  = NULL;
static lv_obj_t *s_clock_l   = NULL;
static lv_obj_t *s_endstrip  = NULL;   // "ENCERRAR VEZ" no modo LIVRE
static lv_obj_t *s_stage_col = NULL;
static lv_obj_t *s_alvo      = NULL;
static lv_obj_t *s_proib_row[3];
static lv_obj_t *s_proib_word[3];
static lv_obj_t *s_idle_msg  = NULL;
static lv_obj_t *s_go_row    = NULL;   // linha ERROU + PULAR + CERTO
static lv_obj_t *s_foul_btn  = NULL;
static lv_obj_t *s_foul_img  = NULL;   // Ícone Dislike (👎)
static lv_obj_t *s_skip_btn  = NULL;   // PULAR — na mesma linha
static lv_obj_t *s_skip_lbl  = NULL;
static lv_obj_t *s_go_btn    = NULL;   // CERTO / COMEÇAR
static lv_obj_t *s_go_lbl    = NULL;
static lv_obj_t *s_go_img    = NULL;   // Ícone Joinha (👍)

// Overlay — TEMPO
static lv_obj_t *s_ov       = NULL;
static lv_obj_t *s_ov_title = NULL;
static lv_obj_t *s_ov_tally = NULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------



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

static int rnd(int min, int max)
{
    const kit_api_table_t *t = api();
    if (max < min) max = min;
    if (t && t->random) return (int)t->random->range(min, max);
    return min + rand() % (max - min + 1);
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

static int  tempo_secs(void)      { return TEMPO_S[s_tempo_i]; }
static int  forbidden_count(void) { return s_proib_i + 2; }
static int  skip_limit(void)      { return SKIP_LIM[s_pulos_i]; }

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
    return (s_deck_i == DECK_TUDO) ? TB_FACIL_N + TB_TUDO_N : TB_FACIL_N;
}

static const tb_card_t *card_at(int i)
{
    return (i < TB_FACIL_N) ? &TB_FACIL[i] : &TB_TUDO[i - TB_FACIL_N];
}

static void shuffle_bag(void)
{
    s_bag_n = active_n();
    for (int i = 0; i < s_bag_n; i++) s_bag[i] = i;
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

// PULAR: devolve a carta atual ao monte, numa posição aleatória mais à frente.
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
    t->storage->set_i32(K_PROIB, s_proib_i);
    t->storage->set_i32(K_DECK, s_deck_i);
    t->storage->set_i32(K_PULOS, s_pulos_i);
}

static void load_prefs(void)
{
    const kit_api_table_t *t = api();
    if (!t || !t->storage) return;
    int32_t v;
    if ((t->storage->get_i32(K_TEMPO, &v) == KIT_OK || t->storage->get_i32(K_TEMPO_OLD, &v) == KIT_OK) && v >= 0 && v <= 3) s_tempo_i = (int)v;
    if ((t->storage->get_i32(K_PROIB, &v) == KIT_OK || t->storage->get_i32(K_PROIB_OLD, &v) == KIT_OK) && (v == 0 || v == 1)) s_proib_i = (int)v;
    if ((t->storage->get_i32(K_DECK, &v) == KIT_OK || t->storage->get_i32(K_DECK_OLD, &v) == KIT_OK) && (v == DECK_FACIL || v == DECK_TUDO))
        s_deck_i = (int)v;
    if ((t->storage->get_i32(K_PULOS, &v) == KIT_OK || t->storage->get_i32(K_PULOS_OLD, &v) == KIT_OK) && v >= 0 && v <= 4) s_pulos_i = (int)v;
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
    sync_seg(s_proib_pills, s_proib_lbls, 2, s_proib_i);
    sync_seg(s_deck_pills, s_deck_lbls, 2, s_deck_i);
    sync_seg(s_pulos_pills, s_pulos_lbls, 5, s_pulos_i);
}

// PULAR só aparece EM JOGO, com PULOS != 0 e ainda sobrando pulo. Some quando
// esgota — sem opa intermediária (que forçaria layer buffer no CO5300).
static void update_skip_btn(void)
{
    if (!s_skip_btn) return;
    int lim = skip_limit();
    bool spent = (lim > 0 && s_skips >= lim);
    bool show = (s_state == ST_RUN && lim != 0 && !spent);
    vis(s_skip_btn, show);
    if (s_state == ST_RUN) {
        if (show) {
            lv_obj_set_flex_grow(s_foul_btn, 1);
            lv_obj_set_flex_grow(s_skip_btn, 1);
            lv_obj_set_flex_grow(s_go_btn, 2);
        } else {
            lv_obj_set_flex_grow(s_foul_btn, 1);
            lv_obj_set_flex_grow(s_go_btn, 2);
        }
    }
}

static void render_card(void)
{
    if (!s_alvo || s_cur < 0) return;
    const tb_card_t *c = card_at(s_cur);
    lv_label_set_text(s_alvo, c->alvo);
    lv_obj_set_style_text_color(s_alvo, lv_color_hex(s_accent), 0);

    // Palavras longas (ex: TRAVESSEIRO de 11 letras) usam kit_sans_28 para caber em uma linha
    const lv_font_t *f = (strlen(c->alvo) >= 10) ? &kit_sans_28 : &kit_display_44;
    lv_obj_set_style_text_font(s_alvo, f, 0);

    int nf = forbidden_count();
    for (int i = 0; i < 3; i++) {
        vis(s_proib_row[i], i < nf);
        if (i < nf) lv_label_set_text(s_proib_word[i], c->proib[i]);
    }
}

static void sync_stage(void)
{
    if (!s_alvo) return;
    bool run  = (s_state == ST_RUN);
    bool over = (s_state == ST_TIMEUP);
    bool timed = run && s_total_ms > 0;
    bool livre = run && s_total_ms == 0;

    vis(s_ov, over);
    vis(s_stage_col, run);
    vis(s_idle_msg, !run && !over);

    vis(s_bar_track, timed);
    vis(s_clock_l, timed);
    vis(s_endstrip, livre);

    vis(s_foul_btn, run);   // Dislike (👎) só em jogo
    vis(s_go_row, !over);

    lv_label_set_text(s_kicker, run ? "DESCREVA" : "VETO");

    if (run) {
        vis(s_go_lbl, false);
        vis(s_go_img, true);
    } else {
        vis(s_go_lbl, true);
        vis(s_go_img, false);
        lv_label_set_text(s_go_lbl, "COMEÇAR");
        lv_obj_set_flex_grow(s_go_btn, 1);
    }
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

    if (s_deadline == 0) {   // LIVRE — só o teto de segurança
        if ((int)((now - s_started_at) / 1000) >= LIVRE_CAP_S) time_up(true);
        return;
    }

    if (now >= s_deadline) { time_up(true); return; }

    uint32_t left_ms = (uint32_t)(s_deadline - now);
    int left = (int)((left_ms + 999) / 1000);

    int w = (int)((int64_t)X_CONTENT * left_ms / s_total_ms);
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
            lv_color_hex(s_blink ? 0xF4C766 : s_accent), 0);
    sfx(KIT_SFX_TIMER_DONE);
}

// ---------------------------------------------------------------------------
// Vez
// ---------------------------------------------------------------------------

static void fill_tally(void)
{
    if (!s_ov_tally) return;
    char b[48];
    snprintf(b, sizeof(b), "ACERTOS %d\nERROS %d\nPULOS %d", s_hits, s_fouls, s_skips);
    lv_label_set_text(s_ov_tally, b);
    if (s_ov_title) lv_label_set_text(s_ov_title, s_by_time ? "TEMPO" : "FIM");
}

static void time_up(bool by_time)
{
    stop_clock();
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

static void start_turn(void)
{
    s_hits = s_fouls = s_skips = 0;
    ensure_bag();
    s_cur = deck_next();
    render_card();

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

    keep_awake(true);
    set_tv_locked(true);
    if (s_tv) lv_tileview_set_tile_by_index(s_tv, 1, 0, LV_ANIM_OFF);
    if (s_ov) lv_obj_set_style_bg_color(s_ov, lv_color_hex(s_accent), 0);

    stop_clock();
    if (secs) s_clock = lv_timer_create(clock_cb, CLOCK_TICK_MS, NULL);

    sync_stage();
    sfx(KIT_SFX_CONFIRM);
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

static void mark_correct(void)
{
    if (s_state != ST_RUN) return;
    s_hits++;
    sfx(KIT_SFX_VETO_HIT);
    s_cur = deck_next();
    render_card();
}

static void mark_foul(void)
{
    if (s_state != ST_RUN) return;
    s_fouls++;
    sfx(KIT_SFX_VETO_FOUL);
    s_cur = deck_next();
    render_card();
}

static void do_skip(void)
{
    if (s_state != ST_RUN) return;
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

// ---------------------------------------------------------------------------
// Ação principal (PWR / botão CERTO / COMEÇAR)
// ---------------------------------------------------------------------------

void kit_veto_action(void)
{
    switch (s_state) {
    case ST_IDLE:   start_turn();   break;
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

static void go_cb(lv_event_t *e)      { (void)e; kit_veto_action(); }
static void foul_cb(lv_event_t *e)    { (void)e; mark_foul(); }
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

static void proib_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == s_proib_i) return;
    s_proib_i = v; sync_segs(); save_prefs();
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

    lv_obj_t *g = add_label(chip, KIT_ICON_BACK, KIT_COLOR_TEXT, &kit_display_44, 0);
    lv_obj_center(g);

    lv_obj_t *title = add_label(s_screen, "VETO", KIT_COLOR_TEXT, &kit_mono_26, 2);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, X_PAD + X_CHIP + 12, 30);

    lv_obj_t *dots = plain_box(s_screen);
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
    seg_row(p, "PROIBIDAS", PROIB_LBL, 2, proib_cb, s_proib_pills, s_proib_lbls);
    seg_row(p, "BARALHO", DECK_LBL, 2, deck_cb, s_deck_pills, s_deck_lbls);

    // PULOS em 2 linhas: 1, 2, 3 na Linha 1; LIVRES, OFF na Linha 2
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
        "EMBARALHA O MONTE. AS PALAVRAS NÃO SÃO SALVAS: AO REABRIR, PODEM REPETIR.",
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

    // Palco — NÃO tocável (decisão: sem toque acidental).
    lv_obj_t *stage = plain_box(box);
    lv_obj_set_size(stage, KIT_DISPLAY_WIDTH, STAGE_H);
    lv_obj_set_pos(stage, 0, 0);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // Barra de tempo (modo cronometrado).
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

    // Faixa "ENCERRAR VEZ" (modo LIVRE).
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
    lv_obj_t *el = add_label(s_endstrip, "ENCERRAR VEZ", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 2);
    lv_obj_center(el);

    // Coluna central — alvo + proibidas.
    s_stage_col = plain_box(stage);
    lv_obj_set_size(s_stage_col, KIT_DISPLAY_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_stage_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_stage_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_stage_col, 6, 0);
    lv_obj_add_flag(s_stage_col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(s_stage_col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_stage_col, LV_ALIGN_CENTER, 0, 10);

    s_kicker = add_label(s_stage_col, "DESCREVA", KIT_COLOR_TEXT_MUTED, &kit_mono_16, 3);

    // Alvo: grande (kit_display_44) e AMARELO.
    s_alvo = add_label(s_stage_col, "VETO", s_accent, &kit_display_44, 1);
    lv_label_set_long_mode(s_alvo, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_alvo, X_CONTENT);
    lv_obj_set_style_text_align(s_alvo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_alvo, 2, 0);
    lv_obj_set_style_pad_bottom(s_alvo, 4, 0);

    // Proibidas em sequência horizontal (row-wrap): kit_mono_26 para máxima legibilidade.
    lv_obj_t *pbox = plain_box(s_stage_col);
    lv_obj_set_size(pbox, X_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pbox, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(pbox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pbox, 14, 0);
    lv_obj_set_style_pad_row(pbox, 8, 0);
    lv_obj_set_style_pad_top(pbox, 6, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = plain_box(pbox);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        add_label(row, KIT_ICON_SQUARE, KIT_COLOR_RED, &kit_mono_20, 0);
        s_proib_word[i] = add_label(row, "", KIT_COLOR_TEXT, &kit_mono_26, 1);
        s_proib_row[i] = row;
    }

    // Mensagem de ocioso (irmã da coluna, centralizada).
    s_idle_msg = add_label(stage, "TOQUE EM\nCOMEÇAR", KIT_COLOR_TEXT_MUTED, &kit_mono_26, 2);
    lv_obj_set_style_text_align(s_idle_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_idle_msg);

    // Linha de Ações: ERROU (👎) + PULAR + CERTO (👍 / COMEÇAR).
    s_go_row = plain_box(box);
    lv_obj_set_size(s_go_row, X_CONTENT, CERTO_H);
    lv_obj_set_flex_flow(s_go_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_go_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_go_row, 8, 0);
    lv_obj_align(s_go_row, LV_ALIGN_BOTTOM_MID, 0, -CERTO_MARGIN);

    // ERROU / DISLIKE (👎) — contornado vermelho
    s_foul_btn = lv_obj_create(s_go_row);
    lv_obj_set_height(s_foul_btn, CERTO_H);
    lv_obj_set_flex_grow(s_foul_btn, 1);
    lv_obj_set_style_radius(s_foul_btn, CERTO_H / 2, 0);
    lv_obj_set_style_bg_color(s_foul_btn, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_set_style_bg_opa(s_foul_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_foul_btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_foul_btn, 2, 0);
    lv_obj_set_style_border_color(s_foul_btn, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_set_style_pad_all(s_foul_btn, 0, 0);
    lv_obj_clear_flag(s_foul_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_foul_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_foul_btn, 6);
    lv_obj_add_event_cb(s_foul_btn, foul_cb, LV_EVENT_CLICKED, NULL);

    s_foul_img = lv_image_create(s_foul_btn);
    lv_image_set_src(s_foul_img, &s_icon_thumbs_down);
    lv_obj_set_style_image_recolor_opa(s_foul_img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(s_foul_img, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_center(s_foul_img);

    // PULAR — integrado na mesma linha
    s_skip_btn = lv_obj_create(s_go_row);
    lv_obj_set_height(s_skip_btn, CERTO_H);
    lv_obj_set_flex_grow(s_skip_btn, 1);
    lv_obj_set_style_radius(s_skip_btn, CERTO_H / 2, 0);
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

    // CERTO (👍) / COMEÇAR — pílula amarela cheia
    s_go_btn = lv_obj_create(s_go_row);
    lv_obj_set_height(s_go_btn, CERTO_H);
    lv_obj_set_flex_grow(s_go_btn, 2);
    lv_obj_set_style_radius(s_go_btn, CERTO_H / 2, 0);
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

    s_go_lbl = add_label(s_go_btn, "COMEÇAR", on_accent(), &kit_mono_26, 3);
    lv_obj_center(s_go_lbl);

    s_go_img = lv_image_create(s_go_btn);
    lv_image_set_src(s_go_img, &s_icon_thumbs_up);
    lv_obj_set_style_image_recolor_opa(s_go_img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(s_go_img, lv_color_hex(on_accent()), 0);
    lv_obj_center(s_go_img);
    vis(s_go_img, false);
}

static const char TB_RULES[] =
    "Uma pessoa segura o KIT e vê a tela. O time dela adivinha.\n\n"
    "1. Toque em COMEÇAR. A palavra amarela é o ALVO. As de baixo, com o "
    "quadrado vermelho, são as PROIBIDAS.\n\n"
    "2. Descreva o ALVO sem dizer ele, nenhuma das proibidas, nem pedaços ou "
    "traduções delas.\n\n"
    "3. Acertou: toque no JOINHA ou aperte o PWR e cai a próxima carta. Falou "
    "uma proibida: toque no DISLIKE e a cigarra dispara.\n\n"
    "4. Sem ideia: toque em PULAR. A carta volta pro monte, se o Ajuste "
    "deixar.\n\n"
    "Quando o TEMPO acaba, o KIT mostra quantas você acertou. Se cada palpite "
    "valeu, quem decide é a mesa. Passe o KIT adiante.";

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
    lv_obj_t *body = add_label(p, TB_RULES, KIT_COLOR_TEXT, &kit_sans_28, 0);
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

    s_ov_title = add_label(col, "TEMPO", KIT_COLOR_ON_YELLOW, &kit_display_72, 0);
    s_ov_tally = add_label(col, "ACERTOS 0\nERROS 0\nPULOS 0", KIT_COLOR_ON_YELLOW, &kit_mono_26, 2);
    lv_obj_set_style_text_align(s_ov_tally, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_ov_tally, 8, 0);
    lv_obj_clear_flag(s_ov_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ov_tally, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *nb = lv_obj_create(s_ov);
    lv_obj_set_size(nb, X_CONTENT, CERTO_H);
    lv_obj_set_style_radius(nb, CERTO_H / 2, 0);
    lv_obj_set_style_border_width(nb, 2, 0);
    lv_obj_set_style_border_color(nb, lv_color_hex(KIT_COLOR_ON_YELLOW), 0);
    lv_obj_set_style_bg_opa(nb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(nb, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(nb, 0, 0);
    lv_obj_clear_flag(nb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(nb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(nb, 8);
    lv_obj_align(nb, LV_ALIGN_BOTTOM_MID, 0, -CERTO_MARGIN);
    lv_obj_add_event_cb(nb, ov_pass_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = add_label(nb, "PASSAR A VEZ", KIT_COLOR_ON_YELLOW, &kit_mono_26, 3);
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

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    if (s_screen) tool_destroy();

    s_api = ctx ? ctx->api : NULL;
    ESP_LOGI(TAG, "Montando Veto...");
    s_accent  = KIT_COLOR_YELLOW;
    s_state   = ST_IDLE;
    s_tempo_i = 0;
    s_proib_i = 1;
    s_deck_i  = DECK_FACIL;
    s_pulos_i = 3;   // índice 3 -> LIVRES
    s_hits = s_fouls = s_skips = 0;
    s_cur = -1;
    s_last_card = -1;
    s_reset_armed = false;
    load_prefs();

    s_bag_n = 0;
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

KIT_TOOL_EXPORT void tool_destroy(void)
{
    ESP_LOGI(TAG, "Encerrando Veto.");
    stop_clock();
    stop_alarm();
    if (s_reset_timer) { lv_timer_delete(s_reset_timer); s_reset_timer = NULL; }
    keep_awake(false);
    s_state = ST_IDLE;
    s_reset_armed = false;

    if (s_screen) { lv_obj_delete(s_screen); s_screen = NULL; }

    s_tv = NULL;
    for (int i = 0; i < PAGES; i++) { s_tiles[i] = NULL; s_dots[i] = NULL; }
    for (int i = 0; i < 4; i++) { s_tempo_pills[i] = NULL; s_tempo_lbls[i] = NULL; }
    for (int i = 0; i < 2; i++) {
        s_proib_pills[i] = s_deck_pills[i] = NULL;
        s_proib_lbls[i] = s_deck_lbls[i] = NULL;
    }
    for (int i = 0; i < 5; i++) {
        s_pulos_pills[i] = s_pulos_lbls[i] = NULL;
    }
    for (int i = 0; i < 3; i++) {
        s_proib_row[i] = s_proib_word[i] = NULL;
    }
    s_reset_btn = s_reset_lbl = NULL;
    s_kicker = s_bar_track = s_bar_fill = s_clock_l = s_endstrip = NULL;
    s_stage_col = s_alvo = s_idle_msg = NULL;
    s_skip_btn = s_skip_lbl = s_go_row = s_foul_btn = s_foul_img = s_go_btn = s_go_lbl = s_go_img = NULL;
    s_ov = s_ov_title = s_ov_tally = NULL;
    s_api = NULL;
}

#else /* KIT_SDK_STUBS */

#include "kit_tool_api.h"
#include <stdio.h>

KIT_TOOL_EXPORT kit_err_t tool_init(kit_tool_ctx_t *ctx)
{
    (void)ctx;
    printf("[Veto stub] tool_init — compila nativo; UI sob #ifndef KIT_SDK_STUBS\n");
    return KIT_OK;
}

KIT_TOOL_EXPORT void tool_destroy(void) {}

#endif /* KIT_SDK_STUBS */
