/**
@file texte.h

Types communs decrivant la structure d'un texte : separateurs entre deux
mots d'une meme phrase (liaisons), marques de dialogue (guillemets et
tirets) et ponctuation qui termine une phrase.
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

/*
MARQUES DE DIALOGUE
  Signes de dialogue places entre deux mots consecutifs, ou devant le
  premier mot d'une phrase. Ce sont des bits que l'on combine par "ou"
  (operateur |) ; ils s'ajoutent a la liaison : dans

      Il dit: <<Viens>>, puis sortit.     (<< et >> : guillemets)

  le separateur entre "dit" et "Viens" est (LIAISON_DEUX_POINTS,
  MARQUE_OUVRE_GUILLEMET) et celui entre "Viens" et "puis" est
  (LIAISON_VIRGULE, MARQUE_FERME_GUILLEMET). A l'affichage, l'ordre est
  toujours : guillemet fermant, liaison, tiret, guillemet ouvrant.
*/

/** Aucune marque de dialogue. */
#define MARQUE_AUCUNE 0u

/** Guillemet fermant apres le premier des deux mots. */
#define MARQUE_FERME_GUILLEMET 1u

/** Tiret de dialogue (debut de replique) ou d'incise. */
#define MARQUE_TIRET 2u

/** Guillemet ouvrant devant le second des deux mots. */
#define MARQUE_OUVRE_GUILLEMET 4u

/** Toutes les marques : toute combinaison de marques est <= MARQUES_TOUTES. */
#define MARQUES_TOUTES 7u

/**
 * Ponctuation qui termine une phrase.
 */
typedef enum {
  PONCTUATION_POINT = 0,      /**< point final "." */
  PONCTUATION_EXCLAMATION,    /**< point d'exclamation "!" */
  PONCTUATION_INTERROGATION,  /**< point d'interrogation "?" */
  PONCTUATION_SUSPENSION,     /**< points de suspension "..." (ou caractere
                                   points de suspension) */
  PONCTUATION_AUCUNE,         /**< ligne vide, fin de ligne (lecture "une
                                   phrase par ligne") ou fin de fichier,
                                   sans ponctuation */
  NB_PONCTUATIONS             /**< nombre de ponctuations */
} ponctuation_t;

#endif
