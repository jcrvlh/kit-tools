/**
 * @file art.h
 * @brief Álbum — arte da figurinha, montada em objetos LVGL.
 *
 * A composição sai do hash do BSSID: a mesma rede dá sempre a mesma
 * figurinha. Cor de fundo = canal (1-4 vermelho, 5-8 amarelo, 9-13 azul),
 * moldura = segurança, formas = tipo de rede (oculta = fantasma
 * translúcido, celular = espalhadas, mesh = trio).
 */
#pragma once

#include "album.h"

#ifndef KIT_SDK_STUBS
#include "kit_lvgl.h"

/* Tamanhos com máscara de triângulo pronta (src/shapes.h). */
#define ART_SMALL 96
#define ART_MID   144   /* grade do álbum (2 colunas) */
#define ART_BIG   192   /* pacote e ficha */

/* Liga a API (acelerômetro). Chame no tool_init. */
void art_bind(const kit_api_table_t *api);

/* Figurinha da rede `idx` no tamanho ART_SMALL/ART_MID/ART_BIG. */
lv_obj_t *art_sticker(lv_obj_t *parent, const fig_album_t *a, int idx, int size);

/* Vaga vazia (contorno + marcador) no tamanho `size`. */
lv_obj_t *art_empty(lv_obj_t *parent, const char *mark, int size);

/* Movimento da ÚLTIMA figurinha criada: as formas se deslocam com a inclinação
 * do KIT (paralaxe) e um reflexo corre por cima — mais forte na rara. Sem
 * acelerômetro, só a rara ganha um brilho correndo sozinho. Uma por vez;
 * art_motion_stop() ANTES de apagar o pai da figurinha. */
void art_motion_start(lv_obj_t *sticker, int size, bool rare);
void art_motion_stop(void);
#endif
