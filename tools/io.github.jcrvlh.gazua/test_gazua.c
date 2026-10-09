/**
 * @file test_gazua.c
 * @brief Testes da lógica pura do GAZUA (compilação desktop, sem LVGL).
 *
 * O gerador é conferido por uma implementação independente e ingênua (sem
 * máscaras de bits): para cada puzzle, varre todas as combinações de regras
 * e todos os 125 códigos e exige exatamente uma combinação válida.
 */
#include "gazua_game.h"

#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

/* quantos códigos passam por todas as regras escolhidas, pulando `skip` */
static int naive_count(const gz_puzzle_t *p, const int ch[GZ_VERIFIERS], int skip, int *last)
{
    int n = 0;
    for (int k = 0; k < GZ_CODES; k++) {
        uint8_t c[3];
        gz_code_from_index(k, c);
        bool ok = true;
        for (int v = 0; v < GZ_VERIFIERS && ok; v++)
            if (v != skip && !gz_eval(p->card[v], ch[v], c)) ok = false;
        if (ok) { n++; if (last) *last = k; }
    }
    return n;
}

static void check_puzzle(const gz_puzzle_t *p, bool easy)
{
    for (int v = 0; v < GZ_VERIFIERS; v++) {
        CHECK(p->card[v] < GZ_NCARDS);
        CHECK(p->secret[v] < GZ_CARDS[p->card[v]].nopt);
        if (easy) CHECK(GZ_CARDS[p->card[v]].easy);
        for (int j = 0; j < v; j++) CHECK(p->card[j] != p->card[v]);
    }
    int valid = 0, secret_ok = 0;
    int ch[GZ_VERIFIERS] = { 0 };
    for (;;) {
        int last = -1;
        if (naive_count(p, ch, -1, &last) == 1) {
            bool needed = true;
            for (int v = 0; v < GZ_VERIFIERS; v++)
                if (naive_count(p, ch, v, NULL) <= 1) needed = false;
            if (needed) {
                valid++;
                if (memcmp(ch, (int[]){ p->secret[0], p->secret[1], p->secret[2], p->secret[3] }, sizeof ch) == 0
                    && last == gz_code_index(p->code)) secret_ok = 1;
            }
        }
        int q = 0;
        while (q < GZ_VERIFIERS) { if (++ch[q] < GZ_CARDS[p->card[q]].nopt) break; ch[q] = 0; q++; }
        if (q == GZ_VERIFIERS) break;
    }
    CHECK(valid == 1);
    CHECK(secret_ok);
    for (int v = 0; v < GZ_VERIFIERS; v++) CHECK(gz_answer(p, v, p->code));
}

static void test_generator(void)
{
    for (int e = 0; e < 2; e++) {
        int ok = 0;
        for (uint32_t s = 1; s <= 400; s++) {
            gz_puzzle_t p;
            uint32_t seed = s * 2654435761u;
            if (!gz_generate(&p, e == 0, seed)) continue;
            ok++;
            check_puzzle(&p, e == 0);
            gz_puzzle_t q;
            CHECK(gz_generate(&q, e == 0, seed));
            CHECK(memcmp(&p, &q, sizeof p) == 0);   /* determinístico */
        }
        CHECK(ok == 400);
        printf("ok  gerador %s: 400/400 puzzles com solução única e nenhum verificador sobrando\n",
               e == 0 ? "FÁCIL" : "DIFÍCIL");
    }
}

static void test_rules(void)
{
    /* cada carta: toda regra tem pelo menos um código, e os rótulos existem */
    for (int c = 0; c < GZ_NCARDS; c++) {
        char buf[64];
        gz_face(c, buf, sizeof buf);
        CHECK(buf[0] && buf[0] != '?');
        for (int o = 0; o < GZ_CARDS[c].nopt; o++) {
            int n = 0;
            for (int k = 0; k < GZ_CODES; k++) { uint8_t cc[3]; gz_code_from_index(k, cc); n += gz_eval(c, o, cc); }
            CHECK(n > 0);
            gz_opt_label(c, o, buf, sizeof buf);
            CHECK(buf[0] && buf[0] != '?');
        }
    }
    uint8_t c[3] = { 1, 5, 5 };
    CHECK(gz_eval(19, 0, c));      /* o menor: triângulo */
    CHECK(gz_eval(23, 1, c));      /* repete?: dois iguais */
    CHECK(gz_eval(24, 2, c));      /* ordem: 1<5=5 não é crescente estrita */
    for (int k = 0; k < GZ_CODES; k++) { uint8_t cc[3]; gz_code_from_index(k, cc); CHECK(gz_code_index(cc) == k); }
    printf("ok  regras e rótulos das %d cartas\n", GZ_NCARDS);
}

static void test_deductions(void)
{
    gz_puzzle_t p;
    CHECK(gz_generate(&p, false, 12345u));
    /* testa todos os códigos em todos os verificadores: só o código certo bate */
    static gz_obs_t obs[GZ_CODES * GZ_VERIFIERS];
    int n = 0;
    for (int k = 0; k < GZ_CODES; k++)
        for (int v = 0; v < GZ_VERIFIERS; v++) {
            gz_code_from_index(k, obs[n].code);
            obs[n].v = (uint8_t)v;
            obs[n].yes = gz_answer(&p, v, obs[n].code);
            n++;
        }
    CHECK(gz_guess_conflicts(&p, p.code, obs, n) == 0);
    for (int v = 0; v < GZ_VERIFIERS; v++)
        CHECK(!gz_contradicted(&p, v, p.secret[v], obs, n));
    /* sem nenhum teste, nada é contradição */
    for (int k = 0; k < GZ_CODES; k++) {
        uint8_t c[3];
        gz_code_from_index(k, c);
        uint8_t bad = gz_guess_conflicts(&p, c, obs, 0);
        for (int v = 0; v < GZ_VERIFIERS; v++) {
            bool any = false;
            for (int o = 0; o < GZ_CARDS[p.card[v]].nopt; o++) any |= gz_eval(p.card[v], o, c);
            CHECK(((bad >> v) & 1) == !any);
        }
    }
    printf("ok  deduções (contradição e conferência do palpite)\n");
}

static void test_days(void)
{
    CHECK(gz_day_number(2026, 1, 1) == 1);
    CHECK(gz_day_number(2026, 10, 9) == 282);
    CHECK(gz_day_number(2027, 1, 1) == 366);
    CHECK(gz_day_number(2028, 3, 1) == 365 + 365 + 31 + 29 + 1);   /* 2028 é bissexto */
    printf("ok  número do dia\n");
}

int main(void)
{
    gz_init();
    test_rules();
    test_generator();
    test_deductions();
    test_days();
    if (s_fail) { printf("%d falha(s)\n", s_fail); return 1; }
    printf("tudo ok\n");
    return 0;
}
