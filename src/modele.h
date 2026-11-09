/**
@file modele.h
Fichier modele.h

Modele de langue appris sur un corpus : tout ce qu'il faut pour generer des
phrases lisibles, et pour les sauvegarder dans un fichier ou les relire
sans reparcourir le corpus.

Un modele contient :
  - la table des mots (cles {a..z}, mot 0 = "" = frontiere de phrase) ;
  - la table des bigrammes (paires de mots consecutifs, paire 0 = (0, 0)) ;
  - le lexique (graphies avec accents et majuscules, liaisons) ;
  - la chaine de Markov, d'ordre 1 (un etat = un mot) ou 2 (un etat = un
    bigramme : le mot suivant depend des deux precedents) ;
  - la chaine de ponctuation (ponctuation finale selon le dernier mot).
Les phrases generees ont ainsi leurs accents ("ete" ecrit avec ses accents),
leurs majuscules ("Jean Valjean"), leurs elisions et traits d'union
("c'etait", "peut-etre", "dit-il"), les virgules les plus frequentes, et une
ponctuation finale tiree selon le corpus ('.', '!', '?', '...').

FORMAT DU FICHIER (texte UTF-8, une information par ligne, champs separes
par une espace ; "-" designe une chaine vide ou une valeur absente) :
@verbatim
    modele-markov 1
    ordre <1|2>
    mots <n>
    <cle> <variante choisie|-> <casse> <k> <variante 0> ... <variante k-1>
                                                          (n lignes)
    bigrammes <b>
    <premier> <second> <variante|-> <casse|-> <liaison>  (b lignes)
    chaine <nombre d'etats>
    <k> <etat 1> <poids 1> ... <etat k> <poids k>        (une ligne par etat)
    ponctuation <4 + n>
    <k> <etat 1> <poids 1> ...                           (une ligne par etat)
    fin
@endverbatim
Le mot 0 s'ecrit "- - 0 0". Les etats d'arrivee d'une ligne sont croissants.
*/
#ifndef __MODELE_H__
#define __MODELE_H__

#include <stdbool.h>
#include "hachage.h"
#include "markov.h"
#include "lexique.h"

/** Type abstrait modele (structure definie dans modele.c). */
typedef struct modele_s modele_t;

/** Longueur maximale d'une phrase generee, en mots (la phrase est ensuite
    terminee par un point). */
#define MODELE_NB_MOTS_MAX_PHRASE 80u

/*
Notations : peut_generer(m) = modele_peut_generer(m).
*/

/**
 * @brief  Apprend un modele a partir des fichiers d'un repertoire.
 * @param  chemin_repertoire  repertoire du corpus.
 * @param  graine             graine du generateur aleatoire.
 * @param  ordre              1 ou 2.
 * @param  recursif           lire aussi les sous-repertoires.
 * @pre    chemin_repertoire != NULL ; ordre == 1 || ordre == 2.
 * @post   si le resultat m est non NULL : modele_ordre(m) == ordre et
 *         modele_mots(m) contient les mots du corpus (cf. repertoire.h).
 * @return le modele (a liberer avec modele_detruire), ou NULL en cas
 *         d'echec, errno indiquant la cause (cf. repertoire_apprendre).
 * @note   Complexite : celle de repertoire_apprendre.
 */
modele_t *modele_apprendre(const char *chemin_repertoire, unsigned int graine,
                           unsigned int ordre, bool recursif);

/**
 * @brief  Sauvegarde un modele dans un fichier texte (format ci-dessus),
 *         a l'aide du module sauvegarde. Le fichier est cree ou remplace.
 * @pre    m != NULL ; chemin_fichier != NULL.
 * @post   en cas de succes, modele_charger(chemin_fichier, g) donne un
 *         modele identique (memes mots, memes choix d'affichage, memes
 *         poids : avec la meme graine, il genere exactement les memes
 *         phrases). En cas d'echec, le fichier incomplet est supprime
 *         (s'il s'agit d'un fichier ordinaire, cf. sauvegarde_supprimer).
 * @return true en cas de succes, false sinon (errno indique la cause).
 * @note   Complexite : O(n + b + T), T etant le nombre de transitions.
 */
bool modele_sauvegarder(const modele_t *m, const char *chemin_fichier);

/**
 * @brief  Charge un modele sauvegarde par modele_sauvegarder.
 *         Le fichier est entierement verifie : un fichier modifie ou
 *         incoherent est refuse, jamais accepte a moitie.
 * @param  chemin_fichier  fichier du modele.
 * @param  graine          graine du generateur aleatoire.
 * @pre    chemin_fichier != NULL.
 * @post   si le resultat est non NULL, il satisfait les memes proprietes
 *         qu'un modele appris (en particulier, modele_generer_phrase peut
 *         l'utiliser sans risque).
 * @return le modele (a liberer avec modele_detruire), ou NULL en cas
 *         d'echec : fichier illisible (errno de fopen), format invalide
 *         (EINVAL), memoire insuffisante (ENOMEM), erreur de lecture (EIO).
 * @note   Complexite : O(taille du fichier + T log k_max).
 */
modele_t *modele_charger(const char *chemin_fichier, unsigned int graine);

/**
 * @brief  Detruit un modele et libere toute sa memoire.
 * @pre    m != NULL (*m peut etre NULL : rien n'est fait).
 * @post   *m == NULL.
 */
void modele_detruire(modele_t **m);

/** @brief Ordre de la chaine (1 ou 2). @pre m != NULL. */
unsigned int modele_ordre(const modele_t *m);

/**
 * @brief  Nombre de mots distincts du corpus (sans le mot vide).
 * @pre    m != NULL.
 */
unsigned int modele_nb_mots(const modele_t *m);

/**
 * @brief  Indique si le modele permet de generer des phrases (le corpus
 *         contenait au moins un mot).
 * @pre    m != NULL.
 */
bool modele_peut_generer(const modele_t *m);

/** @brief Table des mots du modele. @pre m != NULL. */
const hachage_t *modele_mots(const modele_t *m);

/** @brief Table des bigrammes du modele. @pre m != NULL. */
const hachage_paires_t *modele_bigrammes(const modele_t *m);

/** @brief Lexique du modele (finalise). @pre m != NULL. */
const lexique_t *modele_lexique(const modele_t *m);

/** @brief Chaine de Markov du modele (finalisee). @pre m != NULL. */
const markov_t *modele_chaine(const modele_t *m);

/** @brief Chaine de ponctuation finale (finalisee). @pre m != NULL. */
const markov_t *modele_ponctuation(const modele_t *m);

/**
 * @brief  Genere une phrase aleatoire : marche aleatoire dans la chaine
 *         depuis l'etat "debut de phrase" jusqu'a y revenir (au plus
 *         MODELE_NB_MOTS_MAX_PHRASE mots), puis ponctuation finale tiree
 *         selon le dernier mot.
 * @pre    m != NULL ; peut_generer(m).
 * @post   si le resultat est non NULL : c'est une chaine UTF-8 non vide,
 *         sans saut de ligne, qui commence par une lettre majuscule et se
 *         termine par ".", "!", "?" ou "..." ; ses mots (au sens de
 *         l'iterateur) sont tous des mots du modele. Les generateurs des
 *         chaines ont avance.
 * @return la phrase (a liberer avec free), ou NULL si la memoire manque
 *         (le modele reste utilisable).
 * @note   Complexite : O(longueur de la phrase * log k_max).
 */
char *modele_generer_phrase(modele_t *m);

#endif
