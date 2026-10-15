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

#define ART_SMALL 96    /* grade do álbum */
#define ART_BIG   192   /* pacote e ficha: exatamente 2x */

/* Figurinha da rede `idx`, tamanho ART_SMALL ou ART_BIG. */
lv_obj_t *art_sticker(lv_obj_t *parent, const fig_album_t *a, int idx, int size);

/* Vaga vazia (contorno + marcador) do tamanho ART_SMALL. */
lv_obj_t *art_empty(lv_obj_t *parent, const char *mark);

/* Brilho correndo na figurinha rara (uma por vez). art_sheen_stop() ANTES
 * de apagar o pai da figurinha. */
void art_sheen_start(lv_obj_t *sticker, int size);
void art_sheen_stop(void);
#endif
