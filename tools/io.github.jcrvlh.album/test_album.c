/* Teste da lógica pura do Álbum (album.c) — roda no desktop. */
#include "album.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

static char s_dir[256];

static FILE *open_tmp(const char *name, const char *mode)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", s_dir, name);
    return fopen(p, mode);
}

static kit_wifi_ap_t ap(uint8_t a0, uint8_t id, int8_t rssi, uint8_t ch, uint8_t auth, const char *ssid)
{
    kit_wifi_ap_t r;
    memset(&r, 0, sizeof(r));
    uint8_t b[6] = { a0, 0x11, 0x22, 0x33, 0x44, id };
    memcpy(r.bssid, b, 6);
    r.rssi = rssi;
    r.channel = ch;
    r.auth = auth;
    strncpy(r.ssid, ssid, sizeof(r.ssid) - 1);
    return r;
}

static fig_clock_t at(int y, int m, int d, int h)
{
    fig_clock_t c = { true, fig_day_number(y, m, d), (uint8_t)h };
    return c;
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(s_dir, sizeof(s_dir), "%s/album_test", tmp ? tmp : "/tmp");
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", s_dir);
    assert(system(cmd) == 0);

    /* calendário */
    assert(fig_day_number(2024, 1, 1) == 1);
    assert(fig_day_number(2024, 3, 1) == 61);          /* 2024 é bissexto */
    assert(fig_weekday(fig_day_number(2026, 10, 15)) == 3);   /* quinta */
    int y, m, d;
    fig_day_civil(fig_day_number(2026, 10, 15), &y, &m, &d);
    assert(y == 2026 && m == 10 && d == 15);
    assert(fig_day_number(2023, 12, 31) == 0);

    /* SSID: emoji vira '?', acento latino fica */
    char out[KIT_WIFI_SSID_MAX];
    fig_sanitize_ssid(out, "Caf\xC3\xA9 \xF0\x9F\x98\x80!");
    assert(strcmp(out, "Caf\xC3\xA9 ?!") == 0);

    fig_album_t *a = malloc(sizeof(*a));
    fig_album_reset(a);
    printf("sizeof(fig_album_t) = %u\n", (unsigned)sizeof(*a));

    /* 1ª abertura em casa: 4 redes, uma longe */
    fig_session_t s = { 0 };
    fig_scan_out_t o;
    fig_clock_t c1 = at(2026, 10, 13, 21);
    kit_wifi_ap_t casa[5] = {
        ap(0x3C, 1, -45, 6, KIT_WIFI_AUTH_WPA2, "Mesh-Casa"),
        ap(0x3C, 2, -60, 6, KIT_WIFI_AUTH_WPA2, "Mesh-Casa"),
        ap(0xA4, 3, -58, 6, KIT_WIFI_AUTH_WPA2, "Apto_302"),   /* mesmas vagas: repetida */
        ap(0x00, 4, -70, 11, KIT_WIFI_AUTH_WPA2, ""),          /* oculta */
        ap(0x6C, 5, -82, 3, KIT_WIFI_AUTH_OPEN, "Longe"),      /* longe */
    };
    fig_process_scan(a, &s, &c1, casa, 5, &o);
    assert(s.place == 0 && a->places[0].days == 1);
    assert(s.far == 1);
    /* inéditas: Mesh(1) (canal 6, WPA2, noite, M), Apto (letra A) e a oculta */
    assert(o.new_ineditas == 3 && o.new_reps == 1);
    assert(fig_sealed_count(a) == 3);
    assert(fig_filled(a) == 0);                       /* lacradas não contam */
    int first = fig_sealed_first(a);
    fig_reveal(a, first);
    assert(fig_filled(a) == 4);                       /* canal 6, WPA2, noite, M */
    while (fig_sealed_first(a) >= 0) fig_reveal(a, fig_sealed_first(a));
    assert(fig_slot_net(a, FIG_S_OCULTA) >= 0);

    /* mesma abertura, 2ª varredura: nada novo, dia não conta de novo */
    fig_process_scan(a, &s, &c1, casa, 5, &o);
    assert(o.new_ineditas == 0 && o.new_reps == 0);
    assert(a->nets[0].days == 1);

    /* dia 2 e 3 em casa: o 3º nó da mesh cola BANDO; no 3º dia pergunta o lugar */
    fig_session_t s2 = { 0 };
    fig_clock_t c2 = at(2026, 10, 14, 21);
    kit_wifi_ap_t casa2[5];
    memcpy(casa2, casa, sizeof(casa));
    casa2[4] = ap(0x3C, 6, -50, 6, KIT_WIFI_AUTH_WPA2, "Mesh-Casa");
    fig_process_scan(a, &s2, &c2, casa2, 5, &o);
    assert(s2.place == 0 && a->places[0].days == 2 && o.ask_place == -1);
    assert(o.new_ineditas == 1 && a->slots[FIG_S_BANDO] != 0);
    assert(a->nets[0].days == 2);

    fig_session_t s3 = { 0 };
    fig_clock_t c3 = at(2026, 10, 15, 22);
    fig_process_scan(a, &s3, &c3, casa2, 5, &o);
    assert(o.ask_place == 0);
    assert(fig_suggest_place(a, &c3) == 1);           /* noite -> CASA */
    a->places[0].name = 1;
    a->places[0].asked = 1;
    assert(fig_suggest_place(a, &c3) == 0);           /* CASA já existe */

    /* lugar novo com redes diferentes vira outro lugar */
    fig_session_t s4 = { 0 };
    fig_clock_t c4 = at(2026, 10, 16, 10);            /* sexta, manhã */
    kit_wifi_ap_t trab[3] = {
        ap(0x10, 7, -62, 1, KIT_WIFI_AUTH_WPA3, "Escritorio"),
        ap(0x10, 8, -74, 13, KIT_WIFI_AUTH_WPA2, "Impressora"),
        ap(0x9A, 9, -55, 11, KIT_WIFI_AUTH_WPA2, "Celular do Rafa"),   /* bit local: nômade */
    };
    fig_process_scan(a, &s4, &c4, trab, 3, &o);
    assert(s4.place == 1);
    assert(fig_suggest_place(a, &c4) == 2);           /* dia útil de manhã -> TRABALHO */
    assert(a->slots[FIG_S_NOMADE] != 0 && a->slots[FIG_S_CH1 + 12] != 0);

    /* FIEL: 7 dias diferentes */
    for (int k = 0; k < 7; k++) {
        fig_session_t sk = { 0 };
        fig_clock_t ck = at(2026, 10, 20 + k, 21);
        fig_process_scan(a, &sk, &ck, casa2, 4, &o);
    }
    assert(a->slots[FIG_S_FIEL] == 1);

    /* gravação A/B: grava duas vezes, lê a mais nova */
    uint16_t nn = a->nnets;
    assert(fig_album_save(a, open_tmp));
    a->visits = 77;
    assert(fig_album_save(a, open_tmp));
    fig_album_t *b = malloc(sizeof(*b));
    assert(fig_album_load(b, open_tmp, NULL));
    assert(b->nnets == nn && b->visits == 77 && b->places[0].name == 1);

    /* arquivo mais novo corrompido: cai pro anterior */
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", s_dir, (b->gen & 1u) ? "album_b.bin" : "album_a.bin");
    FILE *f = fopen(p, "r+b");
    fseek(f, 100, SEEK_SET);
    fputc(0x5A, f);
    fclose(f);
    assert(fig_album_load(b, open_tmp, NULL));
    assert(b->visits != 77 && b->nnets == nn);

    /* letras: 1ª letra do nome, acento pela letra base */
    assert(fig_letter_slot("Apto_302") == FIG_S_LETRA_A);
    assert(fig_letter_slot("  zeca") == FIG_S_LETRA_A + 25);
    assert(fig_letter_slot("\xC3\x89" "rica") == FIG_S_LETRA_A + 4);     /* É */
    assert(fig_letter_slot("\xC3\xA7" "asa") == FIG_S_LETRA_A + 2);      /* ç */
    assert(fig_letter_slot("2G-casa") == -1 && fig_letter_slot("") == -1);
    assert(fig_slot_net(a, FIG_S_LETRA_A + 12) >= 0);                /* M de Mesh-Casa */

    /* arquivo da versão 1 (sem Letras) migra e preenche as letras */
    {
        static uint8_t v1buf[sizeof(fig_album_t)];
        fig_album_t *c = malloc(sizeof(*c));
        fig_album_reset(c);
        fig_session_t sv = { 0 };
        fig_clock_t cv = at(2026, 10, 13, 21);
        fig_process_scan(c, &sv, &cv, casa, 4, &o);
        while (fig_sealed_first(c) >= 0) fig_reveal(c, fig_sealed_first(c));
        c->slots[FIG_S_LETRA_A + 12] = 0;   /* como se a v1 não tivesse letras */
        /* monta o layout v1: cabeçalho + 28 vagas + resto deslocado */
        size_t hdr = offsetof(fig_album_t, slots);
        size_t v1_places = hdr + sizeof(uint16_t) * FIG_SLOTS_V1;
        size_t rest = sizeof(fig_place_t) * FIG_MAX_PLACES + sizeof(fig_net_t) * FIG_MAX_NETS;
        size_t v1_size = v1_places + rest + sizeof(uint32_t);
        c->version = 1;
        memcpy(v1buf, c, hdr);
        memcpy(v1buf + hdr, c->slots, sizeof(uint16_t) * FIG_SLOTS_V1);
        memcpy(v1buf + v1_places, (uint8_t *)c + offsetof(fig_album_t, places), rest);
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < v1_size - 4; i++) {
            crc ^= v1buf[i];
            for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        crc = ~crc;
        memcpy(v1buf + v1_size - 4, &crc, 4);
        snprintf(p, sizeof(p), "%s/album_a.bin", s_dir);
        FILE *fa = fopen(p, "wb");
        fwrite(v1buf, 1, v1_size, fa);
        fclose(fa);
        snprintf(p, sizeof(p), "%s/album_b.bin", s_dir);
        remove(p);
        bool up = false;
        assert(fig_album_load(b, open_tmp, &up) && up);
        assert(b->nnets == c->nnets && b->version == 2);
        assert(strcmp(b->nets[0].ssid, "Mesh-Casa") == 0);
        assert(fig_slot_net(b, FIG_S_LETRA_A + 12) >= 0);                /* M preenchida */
        assert(fig_slot_net(b, FIG_S_LETRA_A) >= 0);                     /* A de Apto_302 */
        free(c);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf %s", s_dir);
    system(cmd);
    free(a);
    free(b);
    printf("ok\n");
    return 0;
}
