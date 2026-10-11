/**
 * @file test_trinca.c
 * @brief Testes da lógica pura da TRINCA (compilação desktop, sem LVGL).
 *
 * O teste usa double à vontade: ele é a régua. A Tool não pode (inteiro puro).
 */
#include "trinca_game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

/* xorshift — reprodutível e sem o viés de módulo do rand() pequeno */
static uint32_t s_x = 2463534242u;
static uint32_t xs(void) { s_x ^= s_x << 13; s_x ^= s_x >> 17; s_x ^= s_x << 5; return s_x; }
static int32_t rng(int32_t lo, int32_t hi) { return lo + (int32_t)(xs() % (uint32_t)(hi - lo + 1)); }

static int32_t s_fixed[3];
static int     s_fixed_i;
static int32_t rng_fixed(int32_t lo, int32_t hi) { (void)lo; (void)hi; return s_fixed[s_fixed_i++ % 3]; }

static void test_strip(void)
{
    int total = 0;
    for (int s = 0; s < TRINCA_SYMBOLS; s++) {
        int w = trinca_weight((uint8_t)s);
        CHECK(w == (s == TRINCA_RARE ? 2 : 5));
        total += w;
    }
    CHECK(total == TRINCA_STOPS);
    for (int i = 0; i < TRINCA_STOPS; i++) {
        CHECK(TRINCA_STRIP[i] < TRINCA_SYMBOLS);
        CHECK(TRINCA_STRIP[i] != TRINCA_STRIP[(i + 1) % TRINCA_STOPS]);   /* não gagueja */
    }
    printf("ok  faixa: 6x5 + estrela x2, sem vizinho igual\n");
}

static void test_classify(void)
{
    uint8_t a[3] = { 1, 1, 1 }; CHECK(trinca_classify(a) == TRINCA_TRINCA);
    uint8_t b[3] = { 6, 6, 6 }; CHECK(trinca_classify(b) == TRINCA_RARA);
    uint8_t c[3] = { 1, 2, 1 }; CHECK(trinca_classify(c) == TRINCA_DUPLA);   /* dupla nas pontas vale */
    uint8_t d[3] = { 6, 6, 0 }; CHECK(trinca_classify(d) == TRINCA_DUPLA);   /* par de estrelas = dupla */
    uint8_t e[3] = { 0, 1, 2 }; CHECK(trinca_classify(e) == TRINCA_NADA);
    CHECK(trinca_points(TRINCA_NADA) == 0 && trinca_points(TRINCA_DUPLA) == 1);
    CHECK(trinca_points(TRINCA_TRINCA) == 3 && trinca_points(TRINCA_RARA) == 3);

    s_fixed[0] = 8; s_fixed[1] = 22; s_fixed[2] = 8; s_fixed_i = 0;    /* as 2 estrelas da faixa */
    uint8_t st[3];
    trinca_spin(st, rng_fixed);
    CHECK(st[0] == 8 && st[1] == 22 && st[2] == 8);
    CHECK(trinca_classify_stops(st) == TRINCA_RARA);
    printf("ok  classificação\n");
}

static void test_odds(void)
{
    uint32_t o[4];
    trinca_odds(o);
    /* a tabela do README, conferida contra a contagem exaustiva abaixo */
    CHECK(o[TRINCA_NADA] == 19500);
    CHECK(o[TRINCA_DUPLA] == 12510);
    CHECK(o[TRINCA_TRINCA] == 750);
    CHECK(o[TRINCA_RARA] == 8);
    CHECK(o[0] + o[1] + o[2] + o[3] == TRINCA_ONE);

    /* exaustivo: as 32^3 combinações de parada */
    uint32_t n[4] = { 0 };
    uint8_t st[3];
    for (int a = 0; a < TRINCA_STOPS; a++)
        for (int b = 0; b < TRINCA_STOPS; b++)
            for (int c = 0; c < TRINCA_STOPS; c++) {
                st[0] = (uint8_t)a; st[1] = (uint8_t)b; st[2] = (uint8_t)c;
                n[trinca_classify_stops(st)]++;
            }
    for (int k = 0; k < 4; k++) CHECK(n[k] == o[k]);
    printf("ok  chances: nada %u, dupla %u, trinca %u, rara %u (/32768)\n",
           (unsigned)o[0], (unsigned)o[1], (unsigned)o[2], (unsigned)o[3]);
}

static void test_monte_carlo(void)
{
    const int N = 2000000;
    uint32_t o[4], n[4] = { 0 };
    uint32_t per_stop[TRINCA_STOPS] = { 0 };
    trinca_odds(o);
    uint8_t st[3];
    for (int i = 0; i < N; i++) {
        trinca_spin(st, rng);
        per_stop[st[0]]++;
        n[trinca_classify_stops(st)]++;
    }
    for (int k = 0; k < 4; k++) {
        double p = (double)o[k] / TRINCA_ONE;
        double sd = sqrt(N * p * (1 - p));
        CHECK(fabs(n[k] - N * p) < 5 * sd + 1);
    }
    for (int i = 0; i < TRINCA_STOPS; i++) {
        double p = 1.0 / TRINCA_STOPS, sd = sqrt(N * p * (1 - p));
        CHECK(fabs(per_stop[i] - N * p) < 5 * sd);
    }
    printf("ok  %d giros sorteados batem com a tabela (5 sigma)\n", N);
}

/* distribuição exata em double, pra comparar com a de inteiro */
static void ref_dist(int n, double *out)
{
    uint32_t o[4];
    trinca_odds(o);
    double k[4] = { o[0] / 32768.0, o[1] / 32768.0, 0, (o[2] + o[3]) / 32768.0 };
    memset(out, 0, sizeof(double) * TRINCA_DIST_LEN);
    out[0] = 1;
    for (int g = 0; g < n; g++)
        for (int s = 3 * (g + 1); s >= 0; s--) {
            double v = 0;
            for (int p = 0; p < 4; p++) if (s - p >= 0) v += out[s - p] * k[p];
            out[s] = v;
        }
}

static double ref_permille(int n, int sum)
{
    static double d[TRINCA_DIST_LEN];
    ref_dist(n, d);
    double below = 0;
    for (int i = 0; i < sum; i++) below += d[i];
    return 1000.0 * (below + d[sum] / 2);
}

/* empurra no contador uma sequência com `pts` pontos em `n` giros */
static void feed(trinca_luck_t *l, int n, int duplas, int trincas)
{
    trinca_luck_reset(l);
    for (int i = 0; i < n; i++) {
        trinca_kind_t k = TRINCA_NADA;
        if (i < trincas) k = TRINCA_TRINCA;
        else if (i < trincas + duplas) k = TRINCA_DUPLA;
        trinca_luck_add(l, k);
    }
}

static void test_luck_exact(void)
{
    static trinca_luck_t l;
    static double d[TRINCA_DIST_LEN];

    /* a distribuição de inteiro acompanha a de double até a janela cheia */
    trinca_luck_reset(&l);
    double worst = 0;
    for (int n = 1; n <= TRINCA_WINDOW; n++) {
        trinca_luck_add(&l, TRINCA_NADA);
        if (n % 25 && n != 1) continue;
        ref_dist(n, d);
        double tot = 0;
        for (int s = 0; s <= 3 * n; s++) tot += l.dist[s];
        for (int s = 0; s <= 3 * n; s++) {
            double e = fabs(l.dist[s] / tot - d[s]);
            if (e > worst) worst = e;
        }
    }
    CHECK(worst < 1e-6);
    CHECK(l.n_dist == TRINCA_WINDOW);

    /* percentil: varre combinações e compara com a régua em double */
    double pworst = 0;
    int ns[] = { 20, 37, 100, 250, 300 };
    for (unsigned a = 0; a < sizeof ns / sizeof ns[0]; a++) {
        int n = ns[a];
        for (int t = 0; t <= n / 10; t += 1 + n / 60)
            for (int du = 0; du + t <= n; du += 1 + n / 25) {
                feed(&l, n, du, t);
                double ref = ref_permille(n, du + 3 * t);
                double e = fabs(trinca_luck_permille(&l) - ref);
                if (e > pworst) pworst = e;
            }
    }
    CHECK(pworst <= 1.0);   /* arredondamento de 1 milésimo, no máximo */
    printf("ok  percentil inteiro = exato em double (erro máx %.2f/1000, massa %.1e)\n",
           pworst, worst);
}

static void test_luck_behaviour(void)
{
    static trinca_luck_t l;

    trinca_luck_reset(&l);
    for (int i = 0; i < TRINCA_MIN_SPINS - 1; i++) trinca_luck_add(&l, TRINCA_DUPLA);
    CHECK(trinca_luck_permille(&l) == -1);           /* poucos giros: sem número */
    trinca_luck_add(&l, TRINCA_DUPLA);
    CHECK(trinca_luck_permille(&l) > 990);           /* 20 duplas seguidas: sorte rara */

    feed(&l, 100, 0, 0);
    CHECK(trinca_luck_permille(&l) < 5);             /* 100 giros secos */

    /* mais pontos, mais sorte — sempre */
    int prev = -1;
    for (int du = 0; du <= 80; du++) {
        feed(&l, 100, du, 0);
        int pm = trinca_luck_permille(&l);
        CHECK(pm >= prev);
        prev = pm;
    }

    /* o exemplo do papo: 100 giros, 38 duplas (o esperado), 5 trincas */
    feed(&l, 100, 38, 5);
    int pm = trinca_luck_permille(&l);
    CHECK(pm > 600 && pm < 950);
    printf("ok  100 giros, 38 duplas, 5 trincas -> mais sorte que %d,%d%%\n", pm / 10, pm % 10);

    /* janela: depois de 300, só os últimos 300 contam */
    trinca_luck_reset(&l);
    for (int i = 0; i < 300; i++) trinca_luck_add(&l, TRINCA_TRINCA);
    for (int i = 0; i < 300; i++) trinca_luck_add(&l, TRINCA_NADA);
    CHECK(l.window_sum == 0 && trinca_luck_n(&l) == 300 && l.spins == 600);
    CHECK(trinca_luck_permille(&l) < 5);
    for (int i = 0; i < 10; i++) trinca_luck_add(&l, TRINCA_RARA);
    CHECK(l.window_sum == 30 && l.raras == 10 && l.trincas == 310);

    /* sessão aleatória longa: fica no meio, na média */
    long acc = 0, cnt = 0;
    for (int rep = 0; rep < 200; rep++) {
        trinca_luck_reset(&l);
        uint8_t st[3];
        for (int i = 0; i < 120; i++) {
            trinca_spin(st, rng);
            trinca_luck_add(&l, trinca_classify_stops(st));
        }
        acc += trinca_luck_permille(&l);
        cnt++;
    }
    long mean = acc / cnt;
    CHECK(mean > 430 && mean < 570);
    printf("ok  200 sessões de 120 giros: percentil médio %ld/1000 (acaso puro = 500)\n", mean);
}

static void test_expected(void)
{
    CHECK(trinca_expected_x10(0, 758) == 0);
    CHECK(trinca_expected_x10(32768, 758) == 7580);
    CHECK(trinca_expected_x10(1000, 758) == 231);          /* 23,13 */
    CHECK(trinca_expected_x10(4096, 8) == 10);             /* 1 rara a cada 4096 */
    uint32_t big = 6000000;
    double ref = big * 12510.0 / 32768 * 10;
    CHECK(fabs((double)trinca_expected_x10(big, 12510) - ref) <= 1.0);
    printf("ok  valor esperado em décimos\n");
}

int main(void)
{
    test_strip();
    test_classify();
    test_odds();
    test_monte_carlo();
    test_luck_exact();
    test_luck_behaviour();
    test_expected();
    if (s_fail) { printf("%d falha(s)\n", s_fail); return 1; }
    printf("tudo certo\n");
    return 0;
}
