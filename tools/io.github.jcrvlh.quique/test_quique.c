/* Testes da lógica pura do Quique (quique_game.c) — roda no desktop. */
#include "quique_game.h"

#include <stdio.h>
#include <string.h>

static int s_fail, s_pass;

#define CHECK(cond) do { \
    if (cond) s_pass++; \
    else { s_fail++; printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* RNG determinístico (LCG) */
static uint32_t s_seed = 12345;
static int32_t rng(int32_t lo, int32_t hi)
{
    s_seed = s_seed * 1103515245u + 12345u;
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int32_t)((s_seed >> 8) % span);
}

#define RANGE 2500

/* Inclinação (positivo = raquete pra direita) que põe a raquete embaixo da
 * bola 0 — jogador perfeito. No accel_tilt isso é tx = -tilt (a raquete corre
 * pro lado mais baixo). */
static int32_t perfect_roll(const qq_game_t *g, int i)
{
    int32_t lp[2];
    int n = qq_paddles(g, lp);
    int32_t w = qq_paddle_w(g);
    int32_t span = (n == 2) ? (lp[1] + w - lp[0]) : w;
    int32_t half = ((QQ_W - span) / 2) * QQ_FP;
    if (half <= 0) return 0;
    int32_t want = g->ball[i].x - (QQ_W / 2) * QQ_FP;
    if (n == 2) want -= ((span - w) / 2) * QQ_FP;   /* mira com a raquete da esquerda */
    int32_t roll = (want * RANGE) / half;
    roll += roll > 0 ? 80 : (roll < 0 ? -80 : 0);    /* compensa a zona morta */
    return roll;
}

/* roda até `ev_mask` acontecer (ou `max` passos); devolve os eventos somados */
static uint32_t run(qq_game_t *g, int max, uint32_t ev_mask, bool perfect)
{
    uint32_t acc = 0;
    for (int s = 0; s < max; s++) {
        int32_t roll = perfect ? perfect_roll(g, 0) : 0;
        uint32_t ev = qq_step(g, -roll, 0);
        acc |= ev;
        if (ev & ev_mask) break;
    }
    return acc;
}

static void test_trig(void)
{
    CHECK(qq_sin1024(0) == 0);
    CHECK(qq_sin1024(90) == 1024);
    CHECK(qq_sin1024(30) == 512);
    CHECK(qq_cos1024(60) == 512);
    CHECK(qq_sin1024(32) > 512 && qq_sin1024(32) < 587);
    CHECK(qq_sin1024(-5) == 0 && qq_sin1024(200) == 1024);
}

static void test_start(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    CHECK(g.state == QS_PLAY);
    CHECK(g.lives == QQ_START_LIVES);
    CHECK(g.ball[0].on && g.ball[0].stuck > 0);
    CHECK(!g.ball[1].on);
    CHECK(qq_paddle_w(&g) == QQ_PADDLE_W);
    CHECK(qq_mult_pct(&g) == 100);
    CHECK(qq_wall_y(&g) == QQ_WALL_Y);
    /* saque: a bola sai da raquete e sobe */
    uint32_t ev = run(&g, 200, QE_SERVE, false);
    CHECK(ev & QE_SERVE);
    CHECK(g.ball[0].stuck == 0 && g.ball[0].vy < 0);
}

static void test_tilt_moves_paddle(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_step(&g, 0, 0);                             /* nível = centro, sem calibrar */
    int32_t c = g.paddle;
    CHECK(c == (QQ_W / 2) * QQ_FP);
    qq_step(&g, -RANGE * 2, 0);                    /* borda direita BAIXA: raquete pra direita */
    CHECK(g.paddle == (QQ_W - QQ_PADDLE_W / 2) * QQ_FP);
    qq_step(&g, RANGE * 2, 0);                     /* borda direita alta: esquerda */
    CHECK(g.paddle == (QQ_PADDLE_W / 2) * QQ_FP);
    qq_step(&g, 50, 0);                            /* zona morta */
    CHECK(g.paddle == c);
    qq_step(&g, -RANGE / 2, 0);                    /* meio caminho, proporcional */
    CHECK(g.paddle > c && g.paddle < (QQ_W - QQ_PADDLE_W / 2) * QQ_FP);
    qq_step(&g, 0, 4000);                          /* frente/trás não mexe sem Prumo */
    CHECK(g.paddle == c);

    /* Espelho inverte */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_ESPELHO);
    qq_step(&g, -RANGE, 0);
    CHECK(g.paddle < c);

    /* Prumo: o eixo vira frente/trás, relativo a quando a carta entrou */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_PRUMO);
    CHECK(g.shield == 1);
    qq_step(&g, 0, -8500);                         /* em pé na mão: ~-85°, vira a referência */
    CHECK(g.paddle == c);
    qq_step(&g, -RANGE, -8500);                    /* lado não mexe mais */
    CHECK(g.paddle == c);
    qq_step(&g, 0, -8500 + RANGE + 200);           /* borda de baixo subindo: direita */
    CHECK(g.paddle == (QQ_W - QQ_PADDLE_W / 2) * QQ_FP);
}

static void test_rally_and_miss(void)
{
    qq_game_t g;
    s_seed = 7;
    qq_start(&g, RANGE, rng);
    uint32_t ev = run(&g, 2000, QE_HIT, true);
    CHECK(ev & QE_WALL);
    CHECK(ev & QE_HIT);
    CHECK(g.hits == 1 && g.score == 1);

    /* sem jogador: a bola cai e tira uma vida */
    ev = run(&g, 3000, QE_MISS, false);
    if (!(ev & QE_MISS)) ev |= run(&g, 3000, QE_MISS, false);
    CHECK(ev & QE_MISS);
    CHECK(g.lives == QQ_START_LIVES - 1);
    CHECK(g.ball[0].stuck > 0);                     /* volta pro saque */
}

static void test_game_over(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    uint32_t ev = 0;
    for (int k = 0; k < 20 && !(ev & QE_OVER); k++) ev |= run(&g, 4000, QE_OVER, false);
    CHECK(ev & QE_OVER);
    CHECK(g.state == QS_OVER && g.lives == 0);
    CHECK(qq_step(&g, 0, 0) == 0);                  /* parado */
}

static void test_pick_cadence(void)
{
    qq_game_t g;
    s_seed = 99;
    qq_start(&g, RANGE, rng);
    uint32_t ev = run(&g, 40000, QE_PICK, true);
    CHECK(ev & QE_PICK);
    CHECK(g.state == QS_PICK);
    CHECK(g.hits == 10);
    CHECK(g.noffer == 3);
    for (int i = 0; i < g.noffer; i++)
        for (int j = i + 1; j < g.noffer; j++) CHECK(g.offer[i] != g.offer[j]);
    /* no início ninguém tem ônus: Faxina/Exorcismo não aparecem */
    for (int i = 0; i < g.noffer; i++) CHECK(g.offer[i] != QC_FAXINA && g.offer[i] != QC_EXORCISMO);
    CHECK(qq_step(&g, 0, 0) == 0);                  /* pausado escolhendo */
    qq_take_offer(&g, 0);
    CHECK(g.state == QS_PLAY);
    CHECK(g.next_pick == 20);
    CHECK(g.ball[0].stuck > 0);                     /* volta com a bola na raquete */
}

static void test_ganancia(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_GANANCIA);
    qq_make_offer(&g);
    CHECK(g.noffer == 4);
    g.state = QS_PICK;
    g.hits = 10;
    qq_take_offer(&g, 0);
    CHECK(g.next_pick == 15);
}

static void test_stacking(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_TABUA);
    CHECK(qq_paddle_w(&g) == (QQ_PADDLE_W * 140) / 100);
    qq_take(&g, QC_TABUA);
    qq_take(&g, QC_TABUA);
    CHECK(qq_paddle_w(&g) == (QQ_PADDLE_W * 220) / 100);   /* teto */
    CHECK(qq_speed_pct(&g) == 145);                         /* 3 × +15% */

    /* multiplicadores somam: Agulha (+100) + Prensa (+100) = ×3 */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_AGULHA);
    qq_take(&g, QC_PRENSA);
    CHECK(qq_mult_pct(&g) == 300);
    CHECK(qq_wall_y(&g) == QQ_WALL_Y + QQ_PRENSA_PX);

    /* raquete tem piso */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_AGULHA);
    qq_take(&g, QC_AGULHA);
    qq_take(&g, QC_IMA);
    CHECK(qq_paddle_w(&g) == (QQ_PADDLE_W * 40) / 100);

    /* limite de cópias por carta */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_ESPELHO);
    for (int k = 0; k < 50; k++) {
        qq_make_offer(&g);
        for (int i = 0; i < g.noffer; i++) CHECK(g.offer[i] != QC_ESPELHO);
    }

    /* bola: Chumbo encolhe, Melancia cresce, com teto e piso */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_CHUMBO); qq_take(&g, QC_CHUMBO); qq_take(&g, QC_CHUMBO);
    CHECK(qq_ball_r(&g) == 4);
    CHECK(qq_speed_pct(&g) == 50);
}

static void test_lives_cards(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_VIDRO);
    CHECK(g.lives == QQ_START_LIVES + 2);
    uint32_t ev = 0;
    for (int k = 0; k < 4 && !(ev & QE_MISS); k++) ev |= run(&g, 4000, QE_MISS, false);
    CHECK(g.lives == QQ_START_LIVES);               /* Vidro: perdeu 2 */

    /* Fênix revive uma vez e cobra 25% no fim */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_FENIX);
    g.score = 100;
    g.lives = 1;
    ev = 0;
    for (int k = 0; k < 4 && !(ev & QE_MISS); k++) ev |= run(&g, 4000, QE_MISS, false);
    CHECK(ev & QE_PHOENIX);
    CHECK(g.state == QS_PLAY && g.lives == 1);
    ev = 0;
    for (int k = 0; k < 4 && !(ev & QE_MISS); k++) ev |= run(&g, 4000, QE_MISS, false);
    CHECK(ev & QE_OVER);
    CHECK(g.score >= 100 && qq_final_score(&g) == (g.score * 3) / 4);   /* -25% do que fez */

    /* máximo de vidas */
    qq_start(&g, RANGE, rng);
    for (int k = 0; k < 10; k++) qq_take(&g, QC_VIDRO);
    CHECK(g.lives == QQ_MAX_LIVES);
}

static void test_faxina_exorcismo(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_TABUA);       /* +raquete / +velocidade */
    qq_take(&g, QC_ESPELHO);     /* +pontos / controle invertido */
    CHECK(qq_onus_active(&g) == 2);
    qq_take(&g, QC_FAXINA);      /* tira ônus E bônus da Tábua (a mais antiga) */
    CHECK(g.onu[QC_TABUA] == 0 && g.bon[QC_TABUA] == 0);
    CHECK(g.onu[QC_ESPELHO] == 1 && g.bon[QC_ESPELHO] == 1);
    CHECK(qq_paddle_w(&g) == QQ_PADDLE_W);

    char buf[96];
    qq_fx_text(&g, buf, sizeof buf);
    CHECK(strcmp(buf, "ESPELHO") == 0);

    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_TABUA);
    qq_take(&g, QC_ESPELHO);
    qq_take(&g, QC_MOLA);
    qq_take(&g, QC_GEMEAS);
    CHECK(g.ball[1].on);
    qq_take(&g, QC_EXORCISMO);
    CHECK(qq_onus_active(&g) == 0);
    CHECK(g.lives == 1);
    CHECK(g.bon[QC_TABUA] == 1 && g.bon[QC_GEMEAS] == 1);
    CHECK(!g.ball[1].on);                           /* sem o ônus, a 2ª bola sai */
}

static void test_shield_and_portal(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_PRUMO);
    uint32_t ev = 0;
    for (int k = 0; k < 4 && !(ev & (QE_SHIELD | QE_MISS)); k++)
        ev |= run(&g, 4000, QE_SHIELD | QE_MISS, false);
    CHECK(ev & QE_SHIELD);
    CHECK(!(ev & QE_MISS));
    CHECK(g.shield == 0 && g.lives == QQ_START_LIVES);

    /* portal: a bola sai de um lado e entra do outro */
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_PORTAL);
    g.ball[0].stuck = 0;
    g.ball[0].x = 1 * QQ_FP;
    g.ball[0].y = 250 * QQ_FP;
    g.ball[0].vx = -40;
    g.ball[0].vy = -20;
    ev = qq_step(&g, 0, 0);
    CHECK(ev & QE_PORTAL);
    CHECK(g.ball[0].x > (QQ_W - 10) * QQ_FP);
    CHECK(g.ball[0].portal);
}

static void test_shake(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    CHECK(qq_shake(&g) == 0);                       /* sem cartas, nada */
    qq_take(&g, QC_SACODE);
    qq_take(&g, QC_FREIO);
    CHECK(g.smash_charges == 3 && g.slow_charges == 2);
    g.ball[0].stuck = 0;
    g.ball[0].y = 300 * QQ_FP;
    g.ball[0].vx = 20; g.ball[0].vy = -60;           /* subindo -> cortada */
    CHECK(qq_shake(&g) == QE_SMASH);
    CHECK(g.ball[0].vx == 0 && g.ball[0].smash);
    CHECK(g.smash_charges == 2);
    int32_t before = g.score;
    uint32_t ev = run(&g, 500, QE_WALL, false);
    CHECK(ev & QE_SMASH_PT);
    CHECK(g.score == before + 3);
    /* descendo -> câmera lenta */
    int32_t sp = qq_speed_pct(&g);
    CHECK(qq_shake(&g) == QE_SLOWMO);
    CHECK(qq_speed_pct(&g) < sp);
}

static void test_gemeas_and_points(void)
{
    qq_game_t g;
    s_seed = 3;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_GEMEAS);
    CHECK(g.ball[1].on);
    uint32_t ev = run(&g, 3000, QE_HIT, true);
    CHECK(ev & QE_HIT);
    CHECK(g.score == 2);                            /* rebatida vale 2 */
}

static void test_cometa_preview(void)
{
    qq_game_t g;
    int16_t xs[QQ_PREVIEW_N], ys[QQ_PREVIEW_N];
    qq_start(&g, RANGE, rng);
    CHECK(qq_preview(&g, xs, ys) == 0);
    qq_take(&g, QC_COMETA);
    run(&g, 200, QE_SERVE, false);
    qq_step(&g, 0, 0);
    int n = qq_preview(&g, xs, ys);
    CHECK(n > 0);
    for (int i = 0; i < n; i++) CHECK(xs[i] >= 0 && xs[i] < QQ_W && ys[i] >= QQ_WALL_Y && ys[i] < QQ_PADDLE_Y);
}

static void test_visibility(void)
{
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_FANTASMA);
    CHECK(g.lives == QQ_START_LIVES + 1);
    g.ball[0].stuck = 0;
    int32_t mid = (qq_wall_y(&g) + QQ_PADDLE_Y) / 2;
    g.ball[0].y = mid * QQ_FP;
    CHECK(!qq_ball_visible(&g, 0));
    g.ball[0].y = (QQ_PADDLE_Y - 10) * QQ_FP;
    CHECK(qq_ball_visible(&g, 0));
    g.blackout_t = 10;
    CHECK(!qq_ball_visible(&g, 0));

    qq_take(&g, QC_NEBLINA);
    CHECK(qq_fog_y(&g) > qq_wall_y(&g));
}

static void test_fx_text(void)
{
    qq_game_t g;
    char buf[128];
    qq_start(&g, RANGE, rng);
    qq_fx_text(&g, buf, sizeof buf);
    CHECK(buf[0] == 0);
    qq_take(&g, QC_TABUA);
    qq_take(&g, QC_TABUA);
    qq_take(&g, QC_MUDO);
    qq_fx_text(&g, buf, sizeof buf);
    CHECK(strcmp(buf, "TÁBUAx2 · MUDO") == 0);
    qq_fx_text(&g, buf, 6);                         /* corta sem estourar */
    CHECK(strlen(buf) == 5);
}

static void test_hs(void)
{
    int32_t hs[QQ_HS_N] = { 0 };
    CHECK(qq_hs_insert(hs, 0) == -1);
    CHECK(qq_hs_insert(hs, 10) == 0);
    CHECK(qq_hs_insert(hs, 30) == 0);
    CHECK(qq_hs_insert(hs, 20) == 1);
    CHECK(hs[0] == 30 && hs[1] == 20 && hs[2] == 10);
    qq_hs_insert(hs, 5); qq_hs_insert(hs, 4);
    CHECK(qq_hs_insert(hs, 3) == -1);
    CHECK(qq_hs_insert(hs, 50) == 0 && hs[4] == 5);
}

/* partida longa com cartas aleatórias: invariantes nunca quebram */
static void test_fuzz(void)
{
    for (uint32_t seed = 1; seed <= 40; seed++) {
        qq_game_t g;
        s_seed = seed;
        qq_start(&g, 1500 + (int32_t)(seed % 3) * 1000, rng);
        int picks = 0;
        for (int s = 0; s < 60000 && g.state != QS_OVER; s++) {
            int32_t roll = (seed % 4 == 0) ? rng(-3000, 3000) : perfect_roll(&g, 0);
            if (g.onu[QC_ESPELHO]) roll = -roll;
            int32_t ref = g.pitch_ref_ok ? g.zero_pitch16 / 16 : -8000;
            uint32_t ev = g.onu[QC_PRUMO] ? qq_step(&g, 0, ref + roll) : qq_step(&g, -roll, 0);
            if (s % 97 == 0) qq_shake(&g);
            if (ev & QE_PICK) { qq_take_offer(&g, rng(0, g.noffer - 1)); picks++; }
            for (int i = 0; i < QQ_MAX_BALLS; i++) {
                const qq_ball_t *b = &g.ball[i];
                if (!b->on) continue;
                if (!(b->x >= -QQ_FP && b->x <= (QQ_W + 1) * QQ_FP)) { CHECK(0); s = 1 << 30; break; }
                if (!(b->y >= qq_wall_y(&g) * QQ_FP)) { CHECK(0); s = 1 << 30; break; }
            }
            int32_t lp[2];
            qq_paddles(&g, lp);
            if (lp[0] < 0 || g.lives < 0 || g.lives > QQ_MAX_LIVES) { CHECK(0); break; }
            if (qq_speed_pct(&g) > 270) { CHECK(0); break; }
        }
        CHECK(g.nhist <= QQ_MAX_HIST);
        (void)picks;
    }
}


static void test_pick_index(void)
{
    /* 3 cartas em ±25°: nível = meio; direita (borda direita BAIXA) = carta 2 */
    CHECK(qq_pick_index(0, 2500, 3, -1) == 1);
    CHECK(qq_pick_index(-2400, 2500, 3, -1) == 2);
    CHECK(qq_pick_index(2400, 2500, 3, -1) == 0);
    CHECK(qq_pick_index(-9000, 2500, 3, -1) == 2);   /* além do limite: segura na ponta */
    /* fronteira entre 1 e 2 em t = +833: histerese de 1,5° */
    CHECK(qq_pick_index(-900, 2500, 3, 1) == 1);     /* passou só 0,7°: fica */
    CHECK(qq_pick_index(-1000, 2500, 3, 1) == 2);    /* passou 1,7°: troca */
    CHECK(qq_pick_index(-800, 2500, 3, 2) == 2);     /* voltou só um pouco: fica na 2 */
    CHECK(qq_pick_index(-600, 2500, 3, 2) == 1);
    /* 4 cartas: nível cai na fronteira 1|2, e a histerese segura a atual */
    CHECK(qq_pick_index(0, 2500, 4, 1) == 1);
    CHECK(qq_pick_index(0, 2500, 4, 2) == 2);
    CHECK(qq_pick_index(0, 2500, 1, 0) == 0);
    CHECK(strcmp(qq_card_group(QC_TABUA), "RAQUETE") == 0);
    CHECK(strcmp(qq_card_group(QC_METRONOMO), "PERCEPÇÃO") == 0);
    CHECK(strcmp(qq_card_group(QC_GANANCIA), "META") == 0);
}

static int near(int32_t v, int32_t want, int32_t tol) { return v >= want - tol && v <= want + tol; }

static void test_atan_and_tilt(void)
{
    CHECK(qq_atan2_cdeg(0, 1000) == 0);
    CHECK(near(qq_atan2_cdeg(1000, 1000), 4500, 5));
    CHECK(near(qq_atan2_cdeg(1000, 0), 9000, 5));
    CHECK(near(qq_atan2_cdeg(-1000, 0), -9000, 5));
    CHECK(near(qq_atan2_cdeg(500, 866), 3000, 30));
    CHECK(near(qq_atan2_cdeg(500, -866), 15000, 30));
    CHECK(near(qq_atan2_cdeg(-500, -866), -15000, 30));
    for (int d = -85; d <= 85; d += 5) {            /* contra a tabela de seno */
        int32_t s5 = qq_sin1024(d < 0 ? -d : d), c5 = qq_cos1024(d < 0 ? -d : d);
        int32_t a = qq_atan2_cdeg(d < 0 ? -s5 : s5, c5);
        CHECK(near(a, d * 100, 40));
    }

    int32_t tx, pitch;
    /* em pé na mão, tela pro rosto: (0, -1000, 0) */
    qq_tilt_from_mg(0, -1000, 0, &tx, &pitch);
    CHECK(near(tx, 0, 5) && near(pitch, 0, 5));
    /* deitado de tela pra cima: (0, 0, 1000) */
    qq_tilt_from_mg(0, 0, 1000, &tx, &pitch);
    CHECK(near(tx, 0, 5) && near(pitch, 9000, 5));
    /* em pé, 30° pra trás e 30° pra frente: sinais OPOSTOS (o accel_tilt dobrava) */
    qq_tilt_from_mg(0, -866, 500, &tx, &pitch);
    CHECK(near(pitch, 3000, 30));
    qq_tilt_from_mg(0, -866, -500, &tx, &pitch);
    CHECK(near(pitch, -3000, 30));
    /* borda direita 20° acima, em pé: x = +342, y = -940 */
    qq_tilt_from_mg(342, -940, 0, &tx, &pitch);
    CHECK(near(tx, 2000, 30));
    /* ...e deitado: o mesmo ângulo lateral */
    qq_tilt_from_mg(342, 0, 940, &tx, &pitch);
    CHECK(near(tx, 2000, 30));
    /* tranco de mão (2 g) não estoura */
    qq_tilt_from_mg(8000, -8000, 8000, &tx, &pitch);
    CHECK(tx > 0 && pitch > 0);

    /* Prumo de verdade: em pé, inclinar pra frente vai pra esquerda e pra
     * trás vai pra direita (antes, os dois iam pro mesmo lado) */
    qq_game_t g;
    qq_start(&g, RANGE, rng);
    qq_take(&g, QC_PRUMO);
    qq_tilt_from_mg(0, -1000, 0, &tx, &pitch);
    qq_step(&g, tx, pitch);
    int32_t c = g.paddle;
    qq_tilt_from_mg(0, -906, 423, &tx, &pitch);     /* 25° pra trás */
    qq_step(&g, tx, pitch);
    CHECK(g.paddle > c + 100 * QQ_FP);
    qq_tilt_from_mg(0, -906, -423, &tx, &pitch);    /* 25° pra frente */
    qq_step(&g, tx, pitch);
    CHECK(g.paddle < c - 100 * QQ_FP);
}

int main(void)
{
    test_trig();
    test_start();
    test_tilt_moves_paddle();
    test_rally_and_miss();
    test_game_over();
    test_pick_cadence();
    test_ganancia();
    test_stacking();
    test_lives_cards();
    test_faxina_exorcismo();
    test_shield_and_portal();
    test_shake();
    test_gemeas_and_points();
    test_cometa_preview();
    test_visibility();
    test_fx_text();
    test_hs();
    test_pick_index();
    test_atan_and_tilt();
    test_fuzz();
    printf("%d ok, %d falharam\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
