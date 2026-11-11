/**
@file texte.h
Auteur(s):

Types communs decrivant la structure d'un texte : separateurs entre deux
mots d'une meme phrase (liaisons, y compris la ponctuation interne), marques
(guillemets, tirets, parentheses) et ponctuation qui termine une phrase.
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
  LIAISON_ESPACE = 0,     /**< "le chat" : blancs et autres signes */
  LIAISON_APOSTROPHE,     /**< "c'est" : une apostrophe seule entre deux
                               lettres (elision ou contraction) */
  LIAISON_TRAIT_UNION,    /**< "peut-etre" : un trait d'union seul entre
                               deux lettres */
  LIAISON_ABREVIATION,    /**< "M. Dupont" : point d'une abreviation */
  LIAISON_VIRGULE,        /**< "oui, monsieur" */
  LIAISON_POINT_VIRGULE,  /**< "il pleut; il vente" */
  LIAISON_DEUX_POINTS,    /**< "il dit: viens" */
  LIAISON_EXCLAMATION,    /**< "Ah! dit-il" : point d'exclamation suivi
                               d'un mot en minuscule */
  LIAISON_INTERROGATION,  /**< "Qui? demanda-t-il" : point
                               d'interrogation suivi d'une minuscule */
  LIAISON_SUSPENSION,     /**< "il hesita... puis" : points de suspension
                               suivis d'une minuscule */
  NB_LIAISONS             /**< nombre de liaisons */
} liaison_t;

/*
MARQUES
  Signes places entre deux mots consecutifs, ou devant le premier mot d'une
  phrase : guillemets et tirets de dialogue, parentheses (et crochets). Ce
  sont des bits que l'on combine par "ou" (operateur |) ; ils s'ajoutent a
  la liaison : dans

      Il dit: <<Viens>>, puis sortit (sans bruit).
                                        (<< et >> : guillemets)

  le separateur entre "dit" et "Viens" est (LIAISON_DEUX_POINTS,
  MARQUE_OUVRE_GUILLEMET), celui entre "Viens" et "puis" est
  (LIAISON_VIRGULE, MARQUE_FERME_GUILLEMET) et celui entre "sortit" et
  "sans" est (LIAISON_ESPACE, MARQUE_OUVRE_PARENTHESE). A l'affichage,
  l'ordre est toujours : marques fermantes (dans l'ordre inverse de leur
  ouverture), liaison, tiret, parenthese ouvrante, guillemet ouvrant.
*/

/** Aucune marque de dialogue. */
#define MARQUE_AUCUNE 0u

/** Guillemet fermant apres le premier des deux mots. */
#define MARQUE_FERME_GUILLEMET 1u

/** Tiret de dialogue (debut de replique) ou d'incise. */
#define MARQUE_TIRET 2u

/** Guillemet ouvrant devant le second des deux mots. */
#define MARQUE_OUVRE_GUILLEMET 4u

/** Parenthese (ou crochet) ouvrante devant le second des deux mots. */
#define MARQUE_OUVRE_PARENTHESE 8u

/** Parenthese (ou crochet) fermante apres le premier des deux mots. */
#define MARQUE_FERME_PARENTHESE 16u

/** Marques qui ferment ce qui precede (elles n'ont pas de sens devant le
    premier mot d'une phrase). */
#define MARQUES_FERMANTES (MARQUE_FERME_GUILLEMET | MARQUE_FERME_PARENTHESE)

/** Toutes les marques : toute combinaison de marques est <= MARQUES_TOUTES. */
#define MARQUES_TOUTES 31u

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
