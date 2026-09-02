/**
 * @file fora_words.h
 * @brief Declarações do banco de palavras para a Tool FORA.
 *
 * 20 categorias com 50+ palavras cada, todas em CAIXA ALTA (UTF-8).
 * Os dados estão definidos em fora_words.c.
 */

#pragma once

#include <stdint.h>

/* -----------------------------------------------------------------------
 * Estrutura de Categoria
 * ----------------------------------------------------------------------- */

typedef struct {
    const char *name;            /**< Nome da categoria em CAIXA ALTA. */
    const char *words_blob;      /**< Bloco contínuo de palavras separadas por \0. */
    const uint16_t *offsets;     /**< Offsets de cada palavra em words_blob. */
    int count;                   /**< Número de palavras no array. */
} fora_category_t;

/* -----------------------------------------------------------------------
 * Dados (definidos em fora_words.c)
 * ----------------------------------------------------------------------- */

/** Número total de categorias (10 categorias essenciais). */
#define FORA_CATEGORY_COUNT  10

/** Tabela de categorias. */
extern const fora_category_t FORA_CATEGORIES[FORA_CATEGORY_COUNT];

/** Índice especial para o modo MIX (sorteia categoria aleatória). */
#define FORA_MIX_INDEX       (-1)

/** Obtém a palavra de uma categoria por índice. */
const char *fora_words_get(int cat_index, int word_index);
