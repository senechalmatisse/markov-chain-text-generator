/**
@file texte.h

Types communs decrivant la structure d'un texte : separateurs entre deux
mots d'une meme phrase (liaisons) et ponctuation qui termine une phrase.
Ils sont produits par l'iterateur de mots (repertoire.h), memorises par le
lexique (lexique.h) et reproduits lors de la generation (modele.h).
Fichier d'en-tete seul : aucun code associe.
*/
#ifndef __TEXTE_H__
#define __TEXTE_H__

/**
 * Separateur entre deux mots consecutifs d'une meme phrase.
 */
typedef enum {
  LIAISON_ESPACE = 0,     /**< "le chat" : blancs, guillemets, tirets... */
  LIAISON_APOSTROPHE,     /**< "c'est" : une apostrophe seule entre deux
                               lettres (elision ou contraction) */
  LIAISON_TRAIT_UNION,    /**< "peut-etre" : un trait d'union seul entre
                               deux lettres */
  LIAISON_ABREVIATION,    /**< "M. Dupont" : point d'une abreviation */
  LIAISON_VIRGULE,        /**< "oui, monsieur" */
  LIAISON_POINT_VIRGULE,  /**< "il pleut; il vente" */
  LIAISON_DEUX_POINTS,    /**< "il dit: viens" */
  NB_LIAISONS             /**< nombre de liaisons */
} liaison_t;

/**
 * Ponctuation qui termine une phrase.
 */
typedef enum {
  PONCTUATION_POINT = 0,      /**< point final "." */
  PONCTUATION_EXCLAMATION,    /**< point d'exclamation "!" */
  PONCTUATION_INTERROGATION,  /**< point d'interrogation "?" */
  PONCTUATION_SUSPENSION,     /**< points de suspension "..." (ou caractere
                                   points de suspension) */
  PONCTUATION_AUCUNE,         /**< ligne vide ou fin de fichier, sans
                                   ponctuation */
  NB_PONCTUATIONS             /**< nombre de ponctuations */
} ponctuation_t;

#endif
