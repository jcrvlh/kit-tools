/**
 * @file tarot_deck.h
 * @brief Dados do baralho "KIT Tarot" — 78 cartas (V1).
 *
 * Ordem dos índices:
 *   0–21   Arcanos Maiores (O Louco … O Mundo), numeração Rider–Waite–Smith
 *          (VIII = A Força, XI = A Justiça).
 *   22–35  Paus    (Ás, Dois … Dez, Valete, Cavaleiro, Rainha, Rei)
 *   36–49  Copas   (mesma ordem de postos)
 *   50–63  Espadas
 *   64–77  Ouros
 *
 * Todo o texto é ORIGINAL, escrito para o KIT (licença CC0 — ver README.md).
 * Tom: conciso, reflexivo, não fatalista. Nada de previsão determinística.
 */
#pragma once

/** Uma carta do baralho. Ver "As três camadas de texto" no DESIGN.md §6.1. */
typedef struct {
    const char *name;        /**< "O Enforcado", "Três de Copas" (≤ 24 ch)   */
    const char *arcana;      /**< "XII · Arcano Maior" | "Copas · Água"      */
    const char *about;       /**< o que a carta É, p/ quem não conhece tarot */
    const char *keywords;    /**< "Pausa · Entrega · Limite" (≤ 3 termos)    */
    const char *upright;     /**< significado normal                         */
    const char *reversed;    /**< significado invertido                      */
    const char *as_past;     /**< lente "Passado"  (1 frase)                 */
    const char *as_present;  /**< lente "Presente" (1 frase)                 */
    const char *as_future;   /**< lente "Futuro"   (1 frase)                 */
} tarot_card_t;

/** Baralho completo. */
extern const tarot_card_t tarot_deck[];

/** Número de cartas presentes em `tarot_deck` (78 quando completo). */
extern const int tarot_deck_count;
