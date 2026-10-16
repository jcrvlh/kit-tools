/**
 * @file album.h
 * @brief Álbum — lógica pura do álbum (sem LVGL), testável no desktop.
 *
 * Cada rede Wi-Fi capturada é uma figurinha (identidade = BSSID). O álbum tem
 * FIG_SLOTS vagas fixas em 4 páginas; uma rede nova que cola vaga ainda vazia
 * é INÉDITA (fica lacrada no envelope até a pessoa abrir), uma que não cola
 * nada é REPETIDA (entra direto em "Todas as redes").
 *
 * Lugar = impressão digital das redes vistas juntas: a 1ª varredura de cada
 * abertura procura um lugar com >= 50% das mesmas redes; um lugar vira "seu"
 * depois de 3 dias diferentes e aí a Tool pergunta o nome uma vez.
 *
 * Tudo inteiro de 32 bits (o runtime do .so não resolve float nem divisão de
 * 64 bits).
 */
#pragma once

#include "kit_tool_api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define FIG_MAX_NETS    384
#define FIG_MAX_PLACES  12
#define FIG_FP_MAX      24      /* hashes de BSSID por lugar */
#define FIG_LIMIAR      (-75)   /* dBm: mais fraco que isso = "longe", não pega */
#define FIG_ENCOSTADO   (-35)   /* dBm: vaga ENCOSTADO */
#define FIG_MULTIDAO    20      /* redes num scan só: vaga MULTIDÃO */
#define FIG_FIEL_DIAS   7       /* dias diferentes: vaga FIEL */
#define FIG_LUGAR_DIAS  3       /* dias diferentes pra perguntar o nome do lugar */
#define FIG_NO_HOUR     255
#define FIG_NO_PLACE    0       /* place = 0: Passeio */

/* --- vagas: 13 canais + 4 segurança + 5 horários + 6 especiais + 26 letras
 * = 54. As 28 primeiras são da versão 1 do arquivo; a ordem nunca muda (o
 * arquivo guarda o índice). ------------------------------------------------ */
enum {
    FIG_S_CH1 = 0,                  /* ..FIG_S_CH1+12 = canal 13 */
    FIG_S_ABERTA = 13, FIG_S_WEP, FIG_S_WPA2, FIG_S_WPA3,
    FIG_S_MAD = 17, FIG_S_MAN, FIG_S_ALM, FIG_S_TAR, FIG_S_NOI,
    FIG_S_OCULTA = 22, FIG_S_NOMADE, FIG_S_BANDO, FIG_S_ENCOSTADO, FIG_S_FIEL, FIG_S_MULTIDAO,
    FIG_S_LETRA_A = 28,             /* ..FIG_S_LETRA_A+25 = letra Z (1ª letra do nome) */
    FIG_SLOTS = 54
};
#define FIG_SLOTS_V1 28

/* Páginas do álbum, na ordem de exibição: [primeira vaga, quantidade]. */
#define FIG_PAGES 5
extern const uint8_t FIG_PAGE_FIRST[FIG_PAGES];
extern const uint8_t FIG_PAGE_COUNT[FIG_PAGES];

#define FIG_F_SEALED  0x01   /* inédita ainda no envelope */

typedef struct {
    uint8_t  bssid[6];
    int8_t   rssi;        /* sinal na captura */
    uint8_t  ch;
    uint8_t  auth;        /* kit_wifi_auth_t */
    uint8_t  flags;       /* FIG_F_* */
    uint8_t  place;       /* 1..FIG_MAX_PLACES, ou FIG_NO_PLACE (Passeio) */
    uint8_t  hour;        /* hora da captura, FIG_NO_HOUR sem relógio */
    uint16_t first_day;   /* dia da captura (fig_day_number), 0 = sem relógio */
    uint16_t last_day;
    uint16_t days;        /* dias diferentes em que foi vista */
    char     ssid[KIT_WIFI_SSID_MAX];   /* "" = oculta; só ASCII + Latin-1 */
} fig_net_t;

/* Nomes de lugar (índice 1..6; 0 = sem nome). */
#define FIG_PLACE_NAMES 6
extern const char *const FIG_PLACE_NAME[FIG_PLACE_NAMES + 1];

typedef struct {
    uint8_t  name;        /* 0 = sem nome, 1..FIG_PLACE_NAMES */
    uint8_t  asked;       /* já perguntou o nome (não pergunta de novo) */
    uint8_t  nfp;
    uint8_t  used;
    uint16_t days;
    uint16_t last_day;
    uint32_t fp[FIG_FP_MAX];
} fig_place_t;

typedef struct {
    uint32_t    magic;
    uint32_t    version;
    uint32_t    gen;          /* sobe a cada gravação (A/B) */
    uint16_t    nnets;
    uint16_t    visits;
    uint16_t    slots[FIG_SLOTS];   /* índice da rede + 1; 0 = vaga vazia */
    fig_place_t places[FIG_MAX_PLACES];
    fig_net_t   nets[FIG_MAX_NETS];
    uint32_t    crc;
} fig_album_t;

/* Relógio da varredura. valid = false se o RTC ainda não foi acertado. */
typedef struct {
    bool     valid;
    uint16_t day;     /* fig_day_number */
    uint8_t  hour;
} fig_clock_t;

/* Estado de uma abertura da Tool (não persiste). */
typedef struct {
    bool     placed;      /* a 1ª varredura já decidiu o lugar */
    int      place;       /* índice 0-based em places[], -1 = Passeio */
    uint16_t env;         /* redes na última varredura */
    uint16_t far;         /* redes novas longe demais */
    uint16_t reps;        /* repetidas desta abertura */
    uint16_t ineditas;    /* inéditas desta abertura */
} fig_session_t;

/* Resultado de processar uma varredura. */
typedef struct {
    uint16_t new_ineditas;
    uint16_t new_reps;
    int      fiel;        /* índice da rede que colou FIEL agora, -1 */
    int      multidao;    /* índice da rede que colou MULTIDÃO agora, -1 */
    int      ask_place;   /* índice do lugar pra perguntar o nome, -1 */
    bool     full;        /* álbum cheio: redes novas ignoradas */
    bool     changed;     /* precisa gravar */
} fig_scan_out_t;

void fig_album_reset(fig_album_t *a);

/* Dia corrido desde 01/01/2024 (2024-01-01 = 1). 0 se a data não é válida. */
uint16_t fig_day_number(int year, int month, int day);
/* Inverso (pra mostrar "14 OUT"). */
void     fig_day_civil(uint16_t dn, int *year, int *month, int *day);
/* 0 = segunda ... 6 = domingo. */
int      fig_weekday(uint16_t dn);

uint32_t fig_bssid_hash(const uint8_t bssid[6]);
int      fig_find(const fig_album_t *a, const uint8_t bssid[6]);

/* Vagas que a rede ocuparia (sem olhar se estão livres). Devolve quantas. */
int  fig_keys(const fig_album_t *a, const fig_net_t *n, int extra_mesh, uint8_t *out, int max);
bool fig_is_hidden(const fig_net_t *n);
bool fig_is_hotspot(const fig_net_t *n);
bool fig_is_rare(const fig_net_t *n);
int  fig_mesh_count(const fig_album_t *a, const char *ssid);

/* Vagas coladas (só as reveladas). */
int  fig_filled(const fig_album_t *a);
int  fig_page_filled(const fig_album_t *a, int page);
/* Rede revelada que ocupa a vaga, ou -1 (vazia ou ainda no envelope). */
int  fig_slot_net(const fig_album_t *a, int slot);
/* Vagas que a rede `idx` ocupa. Devolve quantas. */
int  fig_slots_of(const fig_album_t *a, int idx, uint8_t *out, int max);

/* Inéditas ainda no envelope, em ordem de captura. */
int  fig_sealed_count(const fig_album_t *a);
int  fig_sealed_first(const fig_album_t *a);
void fig_reveal(fig_album_t *a, int idx);

/* Processa uma varredura (aps vem do mais forte pro mais fraco). */
void fig_process_scan(fig_album_t *a, fig_session_t *s, const fig_clock_t *clk,
                      const kit_wifi_ap_t *aps, size_t count, fig_scan_out_t *out);

/* Sugestão de nome pelo horário (1..FIG_PLACE_NAMES) ou 0. */
int  fig_suggest_place(const fig_album_t *a, const fig_clock_t *clk);

/* Copia o SSID trocando o que a fonte do KIT não desenha por '?'. */
void fig_sanitize_ssid(char *dst, const char *src);

/* Gravação A/B (dois arquivos, vale o de maior gen com CRC ok). */
typedef FILE *(*fig_open_fn)(const char *name, const char *mode);
/* `upgraded` (pode ser NULL) = true se o arquivo era da versão 1 e ganhou a
 * página Letras agora (as redes já reveladas preenchem as letras). */
bool fig_album_load(fig_album_t *a, fig_open_fn open_fn, bool *upgraded);

/* Vaga de letra (A-Z) pela 1ª letra do nome, acento contando pela letra base;
 * -1 se não começa com letra. */
int  fig_letter_slot(const char *ssid);
bool fig_album_save(fig_album_t *a, fig_open_fn open_fn);
