/**
 * @file album.c
 * @brief Álbum — lógica pura do álbum (ver album.h).
 */

#include "album.h"
#include <stddef.h>
#include <string.h>

/* Primeira constante global alinhada (ver o README do hello_sd). */
const uint8_t FIG_PAGE_FIRST[FIG_PAGES] __attribute__((aligned(4))) = {
    FIG_S_CH1, FIG_S_ABERTA, FIG_S_MAD, FIG_S_LETRA_A, FIG_S_OCULTA
};
const uint8_t FIG_PAGE_COUNT[FIG_PAGES] = { 13, 4, 5, 26, 6 };

const char *const FIG_PLACE_NAME[FIG_PLACE_NAMES + 1] = {
    "", "CASA", "TRABALHO", "ESCOLA", "ACADEMIA", "FAMÍLIA", "OUTRO"
};

#define FIG_MAGIC   0x4D424C41u   /* "ALBM" */
#define FIG_VERSION 2u   /* 1 = sem a página Letras (28 vagas) */

/* Layout da versão 1 do arquivo, só pra migrar. */
typedef struct {
    uint32_t    magic;
    uint32_t    version;
    uint32_t    gen;
    uint16_t    nnets;
    uint16_t    visits;
    uint16_t    slots[FIG_SLOTS_V1];
    fig_place_t places[FIG_MAX_PLACES];
    fig_net_t   nets[FIG_MAX_NETS];
    uint32_t    crc;
} fig_album_v1_t;

static const char *const FILE_NAME[2] = { "album_a.bin", "album_b.bin" };

void fig_album_reset(fig_album_t *a)
{
    memset(a, 0, sizeof(*a));
    a->magic = FIG_MAGIC;
    a->version = FIG_VERSION;
}

/* --- calendário (algoritmo de Howard Hinnant, só inteiro de 32 bits) ---- */

static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

#define FIG_EPOCH_YEAR 2024

uint16_t fig_day_number(int year, int month, int day)
{
    if (year < FIG_EPOCH_YEAR || year > 2150 || month < 1 || month > 12 || day < 1 || day > 31)
        return 0;
    int32_t dn = days_from_civil(year, month, day) - days_from_civil(FIG_EPOCH_YEAR, 1, 1) + 1;
    return (dn < 1 || dn > 65535) ? 0 : (uint16_t)dn;
}

void fig_day_civil(uint16_t dn, int *year, int *month, int *day)
{
    int32_t z = (int32_t)dn - 1 + days_from_civil(FIG_EPOCH_YEAR, 1, 1) + 719468;
    int32_t era = z / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = yoe + era * 400;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    int32_t d = doy - (153 * mp + 2) / 5 + 1;
    int32_t m = mp < 10 ? mp + 3 : mp - 9;
    if (year)  *year = (int)(y + (m <= 2));
    if (month) *month = (int)m;
    if (day)   *day = (int)d;
}

int fig_weekday(uint16_t dn)
{
    return dn ? (int)((dn - 1) % 7) : 0;   /* 01/01/2024 foi segunda */
}

/* --- identidade ---------------------------------------------------------- */

uint32_t fig_bssid_hash(const uint8_t bssid[6])
{
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < 6; i++) { h ^= bssid[i]; h *= 0x01000193u; }
    return h ? h : 1;
}

/* Sem memcmp: o loader do KIT não exporta. */
static bool same_bssid(const uint8_t *x, const uint8_t *y)
{
    for (int i = 0; i < 6; i++) if (x[i] != y[i]) return false;
    return true;
}

int fig_find(const fig_album_t *a, const uint8_t bssid[6])
{
    for (int i = 0; i < a->nnets; i++)
        if (same_bssid(a->nets[i].bssid, bssid)) return i;
    return -1;
}

bool fig_is_hidden(const fig_net_t *n)  { return n->ssid[0] == '\0'; }

/* Hotspot de celular costuma usar endereço "local" (bit 0x02 do 1º byte). */
bool fig_is_hotspot(const fig_net_t *n) { return !fig_is_hidden(n) && (n->bssid[0] & 0x02); }

bool fig_is_rare(const fig_net_t *n)
{
    return fig_is_hidden(n) || fig_is_hotspot(n) || n->auth == KIT_WIFI_AUTH_WEP ||
           n->auth == KIT_WIFI_AUTH_WPA3 || n->ch >= 12;
}

int fig_mesh_count(const fig_album_t *a, const char *ssid)
{
    if (!ssid[0]) return 0;
    int c = 0;
    for (int i = 0; i < a->nnets; i++)
        if (strcmp(a->nets[i].ssid, ssid) == 0) c++;
    return c;
}

static int hour_slot(uint8_t h)
{
    if (h == FIG_NO_HOUR) return -1;
    if (h < 6)  return FIG_S_MAD;
    if (h < 12) return FIG_S_MAN;
    if (h < 14) return FIG_S_ALM;
    if (h < 18) return FIG_S_TAR;
    return FIG_S_NOI;
}

static int auth_slot(uint8_t auth)
{
    switch (auth) {
    case KIT_WIFI_AUTH_OPEN: return FIG_S_ABERTA;
    case KIT_WIFI_AUTH_WEP:  return FIG_S_WEP;
    case KIT_WIFI_AUTH_WPA:
    case KIT_WIFI_AUTH_WPA2: return FIG_S_WPA2;
    case KIT_WIFI_AUTH_WPA3: return FIG_S_WPA3;
    default:                 return -1;   /* Enterprise e afins: só "Todas" */
    }
}

int fig_letter_slot(const char *ssid)
{
    const uint8_t *p = (const uint8_t *)ssid;
    while (*p == ' ') p++;
    uint8_t c = p[0];
    if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
    if (c >= 'A' && c <= 'Z') return FIG_S_LETRA_A + (c - 'A');
    if (c != 0xC3) return -1;
    /* Latin-1 em UTF-8 (C3 xx): maiúscula 80-9F, minúscula A0-BF */
    uint8_t d = (uint8_t)(p[1] | 0x20);
    char base = 0;
    if (d >= 0xA0 && d <= 0xA5)      base = 'A';
    else if (d == 0xA7)              base = 'C';
    else if (d >= 0xA8 && d <= 0xAB) base = 'E';
    else if (d >= 0xAC && d <= 0xAF) base = 'I';
    else if (d == 0xB1)              base = 'N';
    else if (d >= 0xB2 && d <= 0xB6) base = 'O';
    else if (d >= 0xB9 && d <= 0xBC) base = 'U';
    else if (d == 0xBD)              base = 'Y';
    return base ? FIG_S_LETRA_A + (base - 'A') : -1;
}

int fig_keys(const fig_album_t *a, const fig_net_t *n, int extra_mesh, uint8_t *out, int max)
{
    int k = 0;
#define PUSH(s) do { if (k < max) out[k++] = (uint8_t)(s); } while (0)
    if (n->ch >= 1 && n->ch <= 13) PUSH(FIG_S_CH1 + n->ch - 1);
    int s = auth_slot(n->auth);
    if (s >= 0) PUSH(s);
    s = hour_slot(n->hour);
    if (s >= 0) PUSH(s);
    if (fig_is_hidden(n))  PUSH(FIG_S_OCULTA);
    if (fig_is_hotspot(n)) PUSH(FIG_S_NOMADE);
    if (fig_mesh_count(a, n->ssid) + extra_mesh >= 3) PUSH(FIG_S_BANDO);
    if (n->rssi >= FIG_ENCOSTADO) PUSH(FIG_S_ENCOSTADO);
    if (n->days >= FIG_FIEL_DIAS) PUSH(FIG_S_FIEL);
    s = fig_letter_slot(n->ssid);
    if (s >= 0) PUSH(s);
#undef PUSH
    return k;
}

/* --- vagas ----------------------------------------------------------------- */

int fig_slot_net(const fig_album_t *a, int slot)
{
    if (slot < 0 || slot >= FIG_SLOTS || !a->slots[slot]) return -1;
    int idx = a->slots[slot] - 1;
    if (idx >= a->nnets || (a->nets[idx].flags & FIG_F_SEALED)) return -1;
    return idx;
}

int fig_filled(const fig_album_t *a)
{
    int c = 0;
    for (int i = 0; i < FIG_SLOTS; i++) if (fig_slot_net(a, i) >= 0) c++;
    return c;
}

int fig_page_filled(const fig_album_t *a, int page)
{
    int c = 0;
    for (int i = 0; i < FIG_PAGE_COUNT[page]; i++)
        if (fig_slot_net(a, FIG_PAGE_FIRST[page] + i) >= 0) c++;
    return c;
}

int fig_slots_of(const fig_album_t *a, int idx, uint8_t *out, int max)
{
    int k = 0;
    for (int i = 0; i < FIG_SLOTS && k < max; i++)
        if (a->slots[i] == idx + 1) out[k++] = (uint8_t)i;
    return k;
}

int fig_sealed_count(const fig_album_t *a)
{
    int c = 0;
    for (int i = 0; i < a->nnets; i++) if (a->nets[i].flags & FIG_F_SEALED) c++;
    return c;
}

int fig_sealed_first(const fig_album_t *a)
{
    for (int i = 0; i < a->nnets; i++) if (a->nets[i].flags & FIG_F_SEALED) return i;
    return -1;
}

void fig_reveal(fig_album_t *a, int idx)
{
    if (idx >= 0 && idx < a->nnets) a->nets[idx].flags &= (uint8_t)~FIG_F_SEALED;
}

/* --- SSID ------------------------------------------------------------------ */

void fig_sanitize_ssid(char *dst, const char *src)
{
    int o = 0;
    for (int i = 0; src[i] && i < KIT_WIFI_SSID_MAX - 1 && o < KIT_WIFI_SSID_MAX - 1; i++) {
        uint8_t c = (uint8_t)src[i];
        if (c >= 0x20 && c < 0x7F) {
            dst[o++] = (char)c;
        } else if ((c == 0xC2 || c == 0xC3) && (uint8_t)src[i + 1] >= 0x80 &&
                   (uint8_t)src[i + 1] <= 0xBF && o < KIT_WIFI_SSID_MAX - 2) {
            uint8_t c2 = (uint8_t)src[i + 1];
            if (c == 0xC2 && c2 < 0xA0) {
                dst[o++] = '?';                 /* controle C1: a fonte não tem */
            } else {
                dst[o++] = (char)c;
                dst[o++] = (char)c2;
            }
            i++;
        } else if (c >= 0x80 && c <= 0xBF) {
            /* continuação de um caractere que já virou '?' */
        } else {
            dst[o++] = '?';
        }
    }
    dst[o] = '\0';
}

/* --- varredura ------------------------------------------------------------- */

static bool fp_has(const fig_place_t *p, uint32_t h)
{
    for (int i = 0; i < p->nfp; i++) if (p->fp[i] == h) return true;
    return false;
}

/* Lugar livre; cheio, recicla o sem nome com menos dias. -1 se todos têm nome. */
static int place_alloc(fig_album_t *a)
{
    int pick = -1;
    for (int i = 0; i < FIG_MAX_PLACES; i++) {
        if (!a->places[i].used) { pick = i; break; }
        if (!a->places[i].name && (pick < 0 || a->places[i].days < a->places[pick].days)) pick = i;
    }
    if (pick < 0) return -1;
    if (a->places[pick].used) {
        /* as redes capturadas lá viram "Passeio" */
        for (int i = 0; i < a->nnets; i++)
            if (a->nets[i].place == pick + 1) a->nets[i].place = FIG_NO_PLACE;
    }
    memset(&a->places[pick], 0, sizeof(a->places[pick]));
    a->places[pick].used = 1;
    return pick;
}

static void decide_place(fig_album_t *a, fig_session_t *s, const fig_clock_t *clk,
                         const kit_wifi_ap_t *aps, size_t count, fig_scan_out_t *out)
{
    s->placed = true;
    a->visits++;

    int best = -1, best_ov = 0;
    for (int p = 0; p < FIG_MAX_PLACES; p++) {
        if (!a->places[p].used) continue;
        int ov = 0;
        for (size_t i = 0; i < count; i++)
            if (fp_has(&a->places[p], fig_bssid_hash(aps[i].bssid))) ov++;
        if (ov * 2 >= (int)count && ov > best_ov) { best = p; best_ov = ov; }
    }
    if (best < 0 && count >= 3) best = place_alloc(a);
    s->place = best;
    if (best < 0) return;

    fig_place_t *p = &a->places[best];
    if (clk->valid && p->last_day != clk->day) {
        p->days++;
        p->last_day = clk->day;
    }
    if (!p->name && !p->asked && p->days >= FIG_LUGAR_DIAS) out->ask_place = best;
}

void fig_process_scan(fig_album_t *a, fig_session_t *s, const fig_clock_t *clk,
                      const kit_wifi_ap_t *aps, size_t count, fig_scan_out_t *out)
{
    memset(out, 0, sizeof(*out));
    out->fiel = out->multidao = out->ask_place = -1;
    s->env = (uint16_t)count;
    s->far = 0;
    if (count == 0) return;

    if (!s->placed) {
        decide_place(a, s, clk, aps, count, out);
        out->changed = true;
    }
    if (s->place >= 0) {
        fig_place_t *p = &a->places[s->place];
        for (size_t i = 0; i < count && p->nfp < FIG_FP_MAX; i++) {
            uint32_t h = fig_bssid_hash(aps[i].bssid);
            if (!fp_has(p, h)) { p->fp[p->nfp++] = h; out->changed = true; }
        }
    }

    for (size_t i = 0; i < count; i++) {
        const kit_wifi_ap_t *ap = &aps[i];
        int idx = fig_find(a, ap->bssid);
        if (idx >= 0) {
            fig_net_t *n = &a->nets[idx];
            if (clk->valid && n->last_day != clk->day) {
                n->days++;
                n->last_day = clk->day;
                out->changed = true;
                if (n->days >= FIG_FIEL_DIAS && !a->slots[FIG_S_FIEL]) {
                    a->slots[FIG_S_FIEL] = (uint16_t)(idx + 1);
                    out->fiel = idx;
                }
            }
            continue;
        }
        if (ap->rssi < FIG_LIMIAR) { s->far++; continue; }
        if (a->nnets >= FIG_MAX_NETS) { out->full = true; continue; }

        fig_net_t n;
        memset(&n, 0, sizeof(n));
        memcpy(n.bssid, ap->bssid, 6);
        n.rssi = ap->rssi;
        n.ch = ap->channel;
        n.auth = ap->auth;
        n.place = s->place >= 0 ? (uint8_t)(s->place + 1) : FIG_NO_PLACE;
        n.hour = clk->valid ? clk->hour : FIG_NO_HOUR;
        n.first_day = n.last_day = clk->valid ? clk->day : 0;
        n.days = 1;
        fig_sanitize_ssid(n.ssid, ap->ssid);

        uint8_t keys[FIG_SLOTS];
        int nk = fig_keys(a, &n, 1, keys, FIG_SLOTS);
        int nidx = a->nnets++;
        bool inedita = false;
        for (int k = 0; k < nk; k++) {
            if (!a->slots[keys[k]]) {
                a->slots[keys[k]] = (uint16_t)(nidx + 1);
                inedita = true;
            }
        }
        if (inedita) { n.flags |= FIG_F_SEALED; out->new_ineditas++; }
        else         { out->new_reps++; }
        a->nets[nidx] = n;
        out->changed = true;
    }

    if (count >= FIG_MULTIDAO && !a->slots[FIG_S_MULTIDAO]) {
        for (size_t i = 0; i < count; i++) {
            int idx = fig_find(a, aps[i].bssid);
            if (idx < 0) continue;
            a->slots[FIG_S_MULTIDAO] = (uint16_t)(idx + 1);
            out->multidao = idx;
            out->changed = true;
            break;
        }
    }

    s->ineditas += out->new_ineditas;
    s->reps += out->new_reps;
}

int fig_suggest_place(const fig_album_t *a, const fig_clock_t *clk)
{
    if (!clk->valid) return 0;
    int h = clk->hour, wd = fig_weekday(clk->day), pick = 0;
    if (h >= 20 || h < 6)                     pick = 1;   /* CASA */
    else if (wd < 5 && h >= 9 && h < 18)      pick = 2;   /* TRABALHO */
    if (!pick) return 0;
    for (int i = 0; i < FIG_MAX_PLACES; i++)
        if (a->places[i].used && a->places[i].name == pick) return 0;
    return pick;
}

/* --- gravação A/B ------------------------------------------------------------ */

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

#define FIG_CRC_LEN (sizeof(fig_album_t) - sizeof(uint32_t))

/* Lê um arquivo por cima de `dst`. Devolve a versão válida encontrada (2 ou
 * 1, com o conteúdo ainda no layout da v1) ou 0. */
static int read_one(fig_album_t *dst, fig_open_fn open_fn, const char *name)
{
    FILE *f = open_fn(name, "rb");
    if (!f) return 0;
    size_t got = fread(dst, 1, sizeof(*dst), f);
    fclose(f);
    if (dst->magic != FIG_MAGIC || dst->nnets > FIG_MAX_NETS) return 0;
    if (dst->version == FIG_VERSION && got == sizeof(*dst) &&
        dst->crc == crc32((const uint8_t *)dst, FIG_CRC_LEN))
        return 2;
    if (dst->version == 1 && got == sizeof(fig_album_v1_t)) {
        const fig_album_v1_t *v1 = (const fig_album_v1_t *)(const void *)dst;
        if (v1->crc == crc32((const uint8_t *)dst, sizeof(fig_album_v1_t) - sizeof(uint32_t)))
            return 1;
    }
    return 0;
}

/* v1 -> v2 no lugar: abre espaço pras 26 vagas novas e cola, em cada letra, a
 * primeira rede já revelada que começa com ela. */
static void upgrade_v1(fig_album_t *a)
{
    uint8_t *p = (uint8_t *)a;
    size_t len = sizeof(fig_place_t) * FIG_MAX_PLACES + sizeof(fig_net_t) * FIG_MAX_NETS;
    memmove(p + offsetof(fig_album_t, places), p + offsetof(fig_album_v1_t, places), len);
    for (int i = FIG_SLOTS_V1; i < FIG_SLOTS; i++) a->slots[i] = 0;
    a->version = FIG_VERSION;
    for (int i = 0; i < a->nnets; i++) {
        if (a->nets[i].flags & FIG_F_SEALED) continue;
        int s = fig_letter_slot(a->nets[i].ssid);
        if (s >= 0 && !a->slots[s]) a->slots[s] = (uint16_t)(i + 1);
    }
}

bool fig_album_load(fig_album_t *a, fig_open_fn open_fn, bool *upgraded)
{
    if (upgraded) *upgraded = false;
    /* Lê os dois por cima de `a`, um de cada vez, e fica com o de maior gen. */
    uint32_t best_gen = 0;
    int best = -1;
    for (int i = 0; i < 2; i++) {
        if (read_one(a, open_fn, FILE_NAME[i]) && (best < 0 || a->gen > best_gen)) {
            best = i;
            best_gen = a->gen;
        }
    }
    int v = best >= 0 ? read_one(a, open_fn, FILE_NAME[best]) : 0;
    if (v == 2) return true;
    if (v == 1) {
        upgrade_v1(a);
        if (upgraded) *upgraded = true;
        return true;
    }
    fig_album_reset(a);
    return false;
}

bool fig_album_save(fig_album_t *a, fig_open_fn open_fn)
{
    a->magic = FIG_MAGIC;
    a->version = FIG_VERSION;
    a->gen++;
    a->crc = crc32((const uint8_t *)a, FIG_CRC_LEN);
    FILE *f = open_fn(FILE_NAME[a->gen & 1u], "wb");
    if (!f) return false;
    size_t put = fwrite(a, 1, sizeof(*a), f);
    fclose(f);
    return put == sizeof(*a);
}
