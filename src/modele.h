/**
@file modele.h
Fichier modele.h

Modele de langue appris sur un corpus : tout ce qu'il faut pour generer des
phrases lisibles, et pour les sauvegarder dans un fichier ou les relire
sans reparcourir le corpus.

Un modele contient :
  - la table des mots (cles {a..z}, mot 0 = "" = frontiere de phrase) ;
  - la table des bigrammes (paires de mots consecutifs, paire 0 = (0, 0)) ;
  - le lexique (graphies avec accents et majuscules, liaisons, marques de
    dialogue, et accents des homographes selon les deux mots precedents) ;
  - la chaine de Markov, d'ordre 1 (un etat = un mot), 2 (un etat = un
    bigramme : le mot suivant depend des deux precedents) ou k, jusqu'a
    MODELE_ORDRE_MAX (un etat = les k derniers mots) ;
  - a partir de l'ordre 3, la table des etats (k-uplets de mots) ;
  - eventuellement une chaine de repli, d'ordre j < k (et ses j-uplets si
    j >= 3) : quand le contexte des k derniers mots n'a qu'un successeur
    dans le corpus, le mot suivant est tire selon les j derniers mots, ce
    qui evite de recopier des phrases entieres du corpus ;
  - la chaine de ponctuation (ponctuation finale selon le dernier mot).
Les phrases generees ont ainsi leurs accents ("ete" retrouve ses deux
accents aigus ; le verbe "a" et la preposition, qui prend un accent grave,
sont distingues selon les mots precedents), leurs majuscules ("Jean
Valjean"), leurs elisions et traits d'union ("c'etait", "peut-etre",
"dit-il"), les virgules et la ponctuation interne les plus frequentes
(", ", "; ", ": ", "! ", "? ", "... "), leurs tirets de dialogue, leurs
guillemets et leurs parentheses (toujours refermes), et une ponctuation
finale tiree selon le corpus ('.', '!', '?', '...').

FORMAT DU FICHIER (texte UTF-8, une information par ligne, champs separes
par une espace ; "-" designe une chaine vide ou une valeur absente) :
@verbatim
    modele-markov 3
    ordre <k>
    repli <j>                              (0 : sans chaine de repli)
    mots <n>
    <cle> <variante choisie|-> <casse> <v> <variante 0> ... <variante v-1>
                                                          (n lignes)
    bigrammes <b>
    <premier> <second> <variante|-> <casse|-> <liaison> <marques>
                                                          (b lignes)
    contextes <c>
    <mot avant> <bigramme> <variante>      (c lignes : les exceptions)
    etats <e>                              (seulement si k >= 3)
    <mot 1> ... <mot k>                    (e lignes : un k-uplet par etat)
    chaine <nombre d'etats>
    <t> <etat 1> <poids 1> ... <etat t> <poids t>        (une ligne par etat)
    etats_repli <e'>                       (seulement si j >= 3)
    <mot 1> ... <mot j>                    (e' lignes)
    chaine_repli <nombre d'etats>          (seulement si j >= 1)
    <t> <etat 1> <poids 1> ...                           (une ligne par etat)
    ponctuation <5 + n>
    <t> <etat 1> <poids 1> ...                           (une ligne par etat)
    fin
@endverbatim
Le mot 0 s'ecrit "- - 0 0". Les etats d'arrivee d'une ligne sont croissants.
Une chaine d'ordre 1 a n etats, une chaine d'ordre 2 en a b, une chaine
d'ordre superieur un par n-uplet. Seuls les contextes de deux mots dont la
variante differe de celle du bigramme sont ecrits. Les fichiers de la
version 2 ("modele-markov 2" : ni ligne repli ni section contextes) restent
lisibles.
*/
#ifndef __MODELE_H__
#define __MODELE_H__

#include <stdbool.h>
#include "hachage.h"
#include "markov.h"
#include "lexique.h"
#include "repertoire.h"

/** Type abstrait modele (structure definie dans modele.c). */
typedef struct modele_s modele_t;

/** Longueur maximale d'une phrase generee, en mots (la phrase est ensuite
    terminee par un point). */
#define MODELE_NB_MOTS_MAX_PHRASE 80u

/** Ordre maximal de la chaine d'un modele. */
#define MODELE_ORDRE_MAX REPERTOIRE_ORDRE_MAX

/** Options d'apprentissage d'un modele (cf. repertoire_options_t). */
typedef struct {
  unsigned int ordre;     /**< ordre de la chaine, de 1 a MODELE_ORDRE_MAX */
  bool recursif;          /**< lire aussi les sous-repertoires */
  bool phrase_par_ligne;  /**< chaque fin de ligne termine une phrase
                               (poemes, chansons, listes) */
  bool filtrer_langues;   /**< ecarter les passages qui ne sont pas dans la
                               langue majoritaire du corpus (cf. langue.h) */
  unsigned int ordre_repli; /**< ordre de la chaine de repli : 0 (sans
                                 repli) ou de 1 a ordre - 1 */
} modele_options_t;

/*
Notations : peut_generer(m) = modele_peut_generer(m) ;
            nb_mots(m) = modele_nb_mots(m).
*/

/**
 * @brief  Apprend un modele a partir d'un corpus : des fichiers et des
 *         repertoires de fichiers texte.
 * @param  chemins     chemins des fichiers et repertoires du corpus.
 * @param  nb_chemins  nombre de chemins.
 * @param  graine      graine du generateur aleatoire.
 * @param  options     options d'apprentissage.
 * @param  bilan       NULL, ou structure qui recoit le bilan de la lecture
 *                     (fichiers, phrases, mots, langue, passages ecartes).
 * @pre    chemins != NULL ; nb_chemins >= 1 ; chaque chemins[i] != NULL ;
 *         options != NULL ; 1 <= options->ordre <= MODELE_ORDRE_MAX ;
 *         options->ordre_repli < options->ordre.
 * @post   si le resultat m est non NULL : modele_ordre(m) == options->ordre,
 *         modele_ordre_repli(m) == options->ordre_repli, modele_mots(m)
 *         contient les mots appris et *bilan est rempli (cf.
 *         repertoire_apprendre_corpus). En cas d'echec, *bilan est inchange.
 * @return le modele (a liberer avec modele_detruire), ou NULL en cas
 *         d'echec, errno indiquant la cause (cf.
 *         repertoire_apprendre_corpus).
 * @note   Complexite : celle de repertoire_apprendre_corpus.
 */
modele_t *modele_apprendre_corpus(const char *const *chemins,
                                  size_t nb_chemins, unsigned int graine,
                                  const modele_options_t *options,
                                  repertoire_bilan_t *bilan);

/**
 * @brief  Apprend un modele a partir des fichiers d'un repertoire (ou d'un
 *         fichier), sans filtre de langue ni repli : equivaut a
 *         modele_apprendre_corpus avec ce seul chemin et les options
 *         { ordre, recursif, false, false, 0 }.
 * @param  chemin_repertoire  repertoire du corpus.
 * @param  graine             graine du generateur aleatoire.
 * @param  ordre              ordre de la chaine.
 * @param  recursif           lire aussi les sous-repertoires.
 * @pre    chemin_repertoire != NULL ; 1 <= ordre <= MODELE_ORDRE_MAX.
 * @post   si le resultat m est non NULL : modele_ordre(m) == ordre et
 *         modele_mots(m) contient les mots du corpus (cf. repertoire.h).
 * @return le modele (a liberer avec modele_detruire), ou NULL en cas
 *         d'echec, errno indiquant la cause (cf.
 *         repertoire_apprendre_corpus).
 * @note   Complexite : celle de repertoire_apprendre_corpus.
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
 * @post   si le resultat m est non NULL, il satisfait les memes proprietes
 *         qu'un modele appris (en particulier, modele_generer_phrase et
 *         modele_generer_phrase_depuis peuvent l'utiliser sans risque) ;
 *         modele_ordre_repli(m) < modele_ordre(m).
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

/** @brief Ordre de la chaine (1 a MODELE_ORDRE_MAX). @pre m != NULL. */
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

/**
 * @brief  Table des etats du modele : les k-uplets de mots, k etant
 *         l'ordre.
 * @pre    m != NULL.
 * @return la table si modele_ordre(m) >= 3 ; NULL sinon (les etats sont
 *         alors les mots ou les bigrammes).
 */
const hachage_nuplets_t *modele_etats(const modele_t *m);

/**
 * @brief  Ordre de la chaine de repli : 0 si le modele n'en a pas, sinon de
 *         1 a modele_ordre(m) - 1.
 * @pre    m != NULL.
 */
unsigned int modele_ordre_repli(const modele_t *m);

/**
 * @brief  Chaine de repli (finalisee) : elle a les etats d'une chaine
 *         d'ordre modele_ordre_repli(m) apprise sur le meme corpus.
 * @pre    m != NULL.
 * @return la chaine, ou NULL si modele_ordre_repli(m) == 0.
 */
const markov_t *modele_repli(const modele_t *m);

/**
 * @brief  Table des etats de la chaine de repli : les j-uplets de mots,
 *         j etant l'ordre de repli.
 * @pre    m != NULL.
 * @return la table si modele_ordre_repli(m) >= 3 ; NULL sinon.
 */
const hachage_nuplets_t *modele_etats_repli(const modele_t *m);

/**
 * @brief  Retrouve un mot du modele a partir d'une de ses graphies, avec ou
 *         sans accents et majuscules ("Marius", "ETE", "ete").
 * @param  m        le modele.
 * @param  graphie  le mot, en UTF-8.
 * @pre    m != NULL ; graphie != NULL.
 * @post   si le resultat est different de HACHAGE_ABSENT :
 *         1 <= resultat <= nb_mots(m).
 * @return l'identifiant du mot, ou HACHAGE_ABSENT : si graphie n'est pas un
 *         mot seul (errno == EINVAL, cf. repertoire_cle), si le mot n'est
 *         pas dans le modele (errno == ENOENT) ou si la memoire manque
 *         (errno == ENOMEM).
 * @note   Complexite : O(longueur) en moyenne.
 */
unsigned int modele_mot(const modele_t *m, const char *graphie);

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
 *         selon le dernier mot. Avec une chaine de repli, un mot est tire
 *         dans la chaine de repli (selon les j derniers mots) quand le
 *         contexte des k derniers mots n'a qu'un successeur dans le corpus
 *         ou n'y apparait pas.
 * @pre    m != NULL ; peut_generer(m).
 * @post   si le resultat est non NULL : c'est une chaine UTF-8 non vide,
 *         sans saut de ligne. Elle commence par une lettre majuscule (sauf
 *         si cette lettre n'en a pas, comme l'eszett), precedee
 *         eventuellement d'un tiret de dialogue (tiret cadratin et espace),
 *         d'une parenthese ouvrante puis d'un guillemet francais ouvrant.
 *         Elle se termine par ".", "!", "?" ou "..." (ou sans ponctuation,
 *         seulement si le corpus a ete lu "une phrase par ligne"),
 *         precedes ou suivis des marques fermantes (guillemet francais,
 *         parenthese). Les guillemets et les parentheses sont bien formes :
 *         jamais deux guillemets ni deux parentheses ouverts a la fois,
 *         bien imbriques, et tout ce qui est ouvert est referme. Ses mots
 *         (au sens de l'iterateur) sont tous des mots du modele. Les
 *         generateurs des chaines ont avance.
 * @return la phrase (a liberer avec free), ou NULL si la memoire manque
 *         (errno == ENOMEM ; le modele reste utilisable).
 * @note   Complexite : O(longueur de la phrase * log k_max).
 */
char *modele_generer_phrase(modele_t *m);

/**
 * @brief  Genere une phrase aleatoire qui commence par un mot donne.
 *         Si ce mot commence des phrases dans le corpus, la marche part de
 *         l'etat correspondant ; sinon elle part du contexte le plus
 *         frequent du mot (l'etat de plus grand poids total dont il est le
 *         dernier mot ; a poids egal, le premier cree), et le mot est ecrit
 *         avec une majuscule.
 * @param  m    le modele.
 * @param  mot  identifiant du premier mot (cf. modele_mot).
 * @pre    m != NULL ; 1 <= mot <= nb_mots(m).
 * @post   comme modele_generer_phrase ; de plus le premier mot de la phrase
 *         (au sens de l'iterateur) est le mot d'identifiant mot.
 * @return la phrase (a liberer avec free), ou NULL si la memoire manque
 *         (errno == ENOMEM ; le modele reste utilisable).
 * @note   Complexite : O(longueur de la phrase * log k_max), plus
 *         O(nombre d'etats) quand le mot change d'un appel au suivant et
 *         ne commence aucune phrase du corpus.
 */
char *modele_generer_phrase_depuis(modele_t *m, unsigned int mot);

#endif
