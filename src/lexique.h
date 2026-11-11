/**
@file lexique.h
Fichier lexique.h

Lexique d'affichage : memorise comment chaque mot s'ecrit dans les textes
appris, pour afficher le texte genere avec ses accents, ses majuscules, ses
liaisons ("c'etait", "peut-etre", "M. Madeleine", "oui, monsieur") et ses
marques de dialogue (tirets et guillemets, cf. texte.h).

Les mots et les bigrammes (mot precedent, mot) sont designes par leurs
identifiants, attribues par l'appelant dans l'ordre 0, 1, 2, ... comme les
etats d'une chaine de Markov : en pratique, ceux de la table des mots et de
la table des paires (hachage.h). Le bigramme (u, v) decrit le mot v quand il
suit le mot u ; le bigramme (0, v) decrit v en debut de phrase.

VOCABULAIRE
  - graphie : ecriture d'un mot telle qu'elle apparait dans un texte, en
    UTF-8 ("Ete", "ete", "ETE", avec ou sans accents) ;
  - variante : graphie en minuscules ("ete" avec ou sans accents) ;
  - casse : minuscules ("ete"), capitale initiale ("Ete") ou tout en
    majuscules ("ETE").

PHASE D'APPRENTISSAGE : on ajoute les mots et les bigrammes, puis on
observe chaque occurrence. lexique_finaliser calcule alors les choix :
  - VARIANTE d'un mot : la plus frequente parmi les occurrences issues de
    fichiers ACCENTUES ; a defaut (mot present seulement dans des fichiers
    sans aucun accent), la plus frequente de toutes. Un fichier sans accent
    ne renseigne pas sur les accents : sans cette regle, "etait" (5 449 fois
    dans lesmiserables.txt, ecrit sans accents) l'emporterait sur la forme
    accentuee (1 085 fois dans les autres textes).
  - CASSE d'un mot : la plus frequente, sans compter les debuts de phrase,
    dont la majuscule est imposee par la position ("Il" en debut de phrase
    ne vote pas). Un mot vu seulement en debut de phrase est en minuscules,
    sauf s'il y etait le plus souvent tout en majuscules (titres).
  - pour chaque BIGRAMME, la variante et la casse majoritaires de son mot
    dans ce contexte (memes regles) : le contexte distingue le verbe "a" de
    la preposition dans "il a" / "va a", ou "il m'a" de "M. Madeleine". Le
    choix du mot sert quand le bigramme n'a pas de vote.
  - SEPARATEUR d'un bigramme : couple (liaison, marques de dialogue)
    majoritaire entre ses deux mots. Pour un bigramme (0, v), ce sont les
    marques placees devant v en debut de phrase : un mot qui commence le
    plus souvent une replique ("--Oui, dit-il") garde son tiret.
  Les votes de bigramme utilisent l'algorithme de Boyer et Moore (majorite
  en une passe, 8 octets par vote) : la memoire reste proportionnelle au
  nombre de bigrammes.

PHASE D'UTILISATION : consultation des choix et ecriture des graphies.
lexique_fixer_mot et lexique_fixer_bigramme redefinissent les choix (pour
recharger un modele sauvegarde).

LETTRES RECONNUES (dans les graphies) : 'a'..'z', 'A'..'Z', les lettres de
U+00C0 a U+00FF (sauf les signes multiplication et division), OE et oe lies
(U+0152, U+0153) et Y trema majuscule (U+0178), encodees en UTF-8.
*/
#ifndef __LEXIQUE_H__
#define __LEXIQUE_H__

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include "texte.h"

/** Type abstrait lexique (structure definie dans lexique.c). */
typedef struct lexique_s lexique_t;

/** Casse d'une graphie. */
typedef enum {
  CASSE_MINUSCULE = 0,  /**< "ete" */
  CASSE_CAPITALE,       /**< "Ete" (et toute graphie dont la premiere lettre
                             est majuscule sans que toutes le soient) */
  CASSE_MAJUSCULE,      /**< "ETE" (au moins deux lettres, toutes
                             majuscules) */
  NB_CASSES             /**< nombre de casses */
} casse_t;

/** Pas de choix propre (variante ou casse d'un bigramme sans vote). */
#define LEXIQUE_AUCUNE UINT_MAX

/** Valeur renvoyee en cas d'echec d'un ajout. */
#define LEXIQUE_ABSENT UINT_MAX

/** Nombre maximal de mots, de bigrammes ou de variantes d'un mot. */
#define LEXIQUE_NB_MAX (UINT_MAX - 1u)

/**
 * @brief  Indique si un couple (liaison, marques) decrit un separateur
 *         possible : une apostrophe ou un trait d'union est le seul signe
 *         entre deux mots, il n'est donc accompagne d'aucune marque.
 * @pre    aucune.
 * @return true si liaison < NB_LIAISONS, marques <= MARQUES_TOUTES et
 *         (marques == MARQUE_AUCUNE ou liaison n'est ni LIAISON_APOSTROPHE
 *         ni LIAISON_TRAIT_UNION) ; false sinon.
 * @note   Complexite : O(1).
 */
bool lexique_separateur_valide(liaison_t liaison, unsigned int marques);

/*
Notations utilisees dans les contrats :
  nb_mots(l), nb_bigrammes(l)  = lexique_nb_mots(l), lexique_nb_bigrammes(l)
  mot(l, b)                    = lexique_mot_bigramme(l, b)
  nb_variantes(l, m)           = lexique_nb_variantes(l, m)
  finalise(l)                  = lexique_est_finalise(l)
  valide(g)                    = lexique_graphie_valide(g)
  minuscules(g)                = g ecrite en minuscules
*/


/* ======================================================================== */
/*                       Graphies (fonctions sans lexique)                  */
/* ======================================================================== */

/**
 * @brief  Indique si une chaine est une graphie : suite non vide de lettres
 *         reconnues, encodee en UTF-8 valide.
 * @pre    graphie != NULL.
 * @return true si c'est une graphie, false sinon.
 * @note   Complexite : O(longueur).
 */
bool lexique_graphie_valide(const char *graphie);

/**
 * @brief  Indique si une chaine est une variante : graphie sans majuscule.
 * @pre    variante != NULL.
 * @return true si valide(variante) et variante == minuscules(variante).
 * @note   Complexite : O(longueur).
 */
bool lexique_variante_valide(const char *variante);

/**
 * @brief  Casse d'une graphie.
 * @pre    graphie != NULL ; valide(graphie).
 * @return CASSE_MINUSCULE si sa premiere lettre est minuscule ;
 *         CASSE_MAJUSCULE si elle a au moins deux lettres, toutes
 *         majuscules ; CASSE_CAPITALE sinon.
 * @note   Complexite : O(longueur).
 */
casse_t lexique_casse(const char *graphie);

/**
 * @brief  Recrit une graphie, sur place, avec la casse demandee :
 *         minuscules, capitale initiale (le reste en minuscules) ou tout en
 *         majuscules.
 * @pre    graphie != NULL ; valide(graphie) ; casse < NB_CASSES.
 * @post   la longueur en octets est inchangee (les lettres reconnues ont la
 *         meme taille en majuscule et en minuscule) ; valide(graphie) ;
 *         minuscules(graphie) == minuscules(ancienne graphie).
 * @note   Complexite : O(longueur).
 */
void lexique_appliquer_casse(char *graphie, casse_t casse);


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

/**
 * @brief  Cree un lexique vide, en phase d'apprentissage.
 * @pre    aucune.
 * @post   si le resultat l est non NULL : nb_mots(l) == 0,
 *         nb_bigrammes(l) == 0 et !finalise(l).
 * @return le lexique (a liberer avec lexique_detruire), ou NULL si la
 *         memoire manque.
 * @note   Complexite : O(1).
 */
lexique_t *lexique_creer(void);

/**
 * @brief  Detruit un lexique et libere toute sa memoire.
 * @pre    l != NULL (*l peut etre NULL : rien n'est fait).
 * @post   *l == NULL ; les chaines renvoyees par lexique_variante ne sont
 *         plus valides.
 * @note   Complexite : O(nb_mots).
 */
void lexique_detruire(lexique_t **l);


/* ======================================================================== */
/*                          Phase d'apprentissage                           */
/* ======================================================================== */

/**
 * @brief  Ajoute un mot (sans variante) au lexique.
 * @pre    l != NULL ; !finalise(l).
 * @post   en cas de succes : resultat == ancien(nb_mots(l)),
 *         nb_mots(l) == ancien(nb_mots(l)) + 1, nb_variantes(l, resultat) == 0.
 *         En cas d'echec : lexique inchange.
 * @return l'identifiant du mot, ou LEXIQUE_ABSENT si la memoire manque ou si
 *         le lexique contient deja LEXIQUE_NB_MAX mots.
 * @note   Complexite : O(1) amortie.
 */
unsigned int lexique_ajouter_mot(lexique_t *l);

/**
 * @brief  Ajoute un bigramme, dont le mot (second element) est donne.
 * @pre    l != NULL ; !finalise(l) ; mot < nb_mots(l).
 * @post   en cas de succes : resultat == ancien(nb_bigrammes(l)),
 *         nb_bigrammes(l) == ancien(nb_bigrammes(l)) + 1 et
 *         mot(l, resultat) == mot. En cas d'echec : lexique inchange.
 * @return l'identifiant du bigramme, ou LEXIQUE_ABSENT si la memoire manque
 *         ou si le lexique contient deja LEXIQUE_NB_MAX bigrammes.
 * @note   Complexite : O(1) amortie.
 */
unsigned int lexique_ajouter_bigramme(lexique_t *l, unsigned int mot);

/**
 * @brief  Ajoute une variante a un mot (si elle n'y est pas deja).
 * @pre    l != NULL ; !finalise(l) ; mot < nb_mots(l) ;
 *         variante != NULL ; lexique_variante_valide(variante).
 * @post   en cas de succes : strcmp(lexique_variante(l, mot, resultat),
 *         variante) == 0 ; si la variante etait nouvelle,
 *         resultat == ancien(nb_variantes(l, mot)) et nb_variantes augmente
 *         de 1 ; sinon le lexique est inchange. En cas d'echec : lexique
 *         inchange.
 * @return le rang de la variante parmi celles du mot, ou LEXIQUE_ABSENT si
 *         la memoire manque.
 * @note   Complexite : O(nombre de variantes du mot * longueur).
 */
unsigned int lexique_ajouter_variante(lexique_t *l, unsigned int mot, const char *variante);

/**
 * @brief  Observe une occurrence du mot d'un bigramme dans un texte.
 * @param  l             le lexique.
 * @param  bigramme      bigramme (mot precedent, mot) de l'occurrence ;
 *                       (0, mot) en debut de phrase.
 * @param  graphie       graphie de l'occurrence.
 * @param  accentue      true si le texte contient des accents (sa graphie
 *                       renseigne sur les accents du mot).
 * @param  debut_phrase  true si le mot commence une phrase : sa casse ne
 *                       vote alors qu'en dernier recours, la liaison est
 *                       ignoree (LIAISON_ESPACE) ainsi que la marque
 *                       MARQUE_FERME_GUILLEMET (elle concerne la phrase
 *                       precedente).
 * @param  liaison       liaison entre le mot precedent et ce mot.
 * @param  marques       marques de dialogue placees devant ce mot.
 * @pre    l != NULL ; !finalise(l) ; bigramme < nb_bigrammes(l) ;
 *         graphie != NULL ; valide(graphie) ;
 *         lexique_separateur_valide(liaison, marques).
 * @post   en cas de succes : minuscules(graphie) est une variante du mot
 *         (ajoutee si besoin) ; les votes du mot et du bigramme sont mis a
 *         jour comme decrit en tete de fichier. En cas d'echec : lexique
 *         inchange.
 * @return true en cas de succes, false si la memoire manque.
 * @note   Complexite : O(nombre de variantes du mot * longueur).
 */
bool lexique_observer(lexique_t *l, unsigned int bigramme,
                      const char *graphie, bool accentue, bool debut_phrase,
                      liaison_t liaison, unsigned int marques);

/**
 * @brief  Termine l'apprentissage : calcule les choix d'affichage de chaque
 *         mot et de chaque bigramme a partir des votes.
 * @pre    l != NULL ; !finalise(l).
 * @post   finalise(l) ; chaque mot qui a au moins une variante a une
 *         variante choisie ; les variantes et les nombres de mots et de
 *         bigrammes sont inchanges. Ne peut pas echouer.
 * @note   Complexite : O(nb_mots + nombre de variantes + nb_bigrammes).
 */
void lexique_finaliser(lexique_t *l);


/* ======================================================================== */
/*                    Consultation (dans les deux phases)                   */
/* ======================================================================== */

/** @brief Indique si le lexique est finalise. @pre l != NULL. */
bool lexique_est_finalise(const lexique_t *l);

/** @brief Nombre de mots. @pre l != NULL. */
unsigned int lexique_nb_mots(const lexique_t *l);

/** @brief Nombre de bigrammes. @pre l != NULL. */
unsigned int lexique_nb_bigrammes(const lexique_t *l);

/**
 * @brief  Mot (second element) d'un bigramme.
 * @pre    l != NULL ; bigramme < nb_bigrammes(l).
 * @return l'identifiant du mot.
 */
unsigned int lexique_mot_bigramme(const lexique_t *l, unsigned int bigramme);

/**
 * @brief  Nombre de variantes d'un mot.
 * @pre    l != NULL ; mot < nb_mots(l).
 * @return le nombre de variantes (0 si le mot n'a jamais ete observe).
 */
unsigned int lexique_nb_variantes(const lexique_t *l, unsigned int mot);

/**
 * @brief  Variante de rang donne d'un mot.
 * @pre    l != NULL ; mot < nb_mots(l) ; rang < nb_variantes(l, mot).
 * @post   lexique_variante_valide(resultat).
 * @return la variante, stockee dans le lexique (ni a modifier ni a liberer ;
 *         valide jusqu'a lexique_detruire).
 */
const char *lexique_variante(const lexique_t *l, unsigned int mot, unsigned int rang);


/* ======================================================================== */
/*                           Phase d'utilisation                            */
/* ======================================================================== */

/**
 * @brief  Variante choisie pour un mot.
 * @pre    l != NULL ; finalise(l) ; mot < nb_mots(l).
 * @return son rang, ou LEXIQUE_AUCUNE si le mot n'a aucune variante.
 */
unsigned int lexique_variante_mot(const lexique_t *l, unsigned int mot);

/**
 * @brief  Casse choisie pour un mot.
 * @pre    l != NULL ; finalise(l) ; mot < nb_mots(l).
 * @return la casse (CASSE_MINUSCULE pour un mot sans vote).
 */
casse_t lexique_casse_mot(const lexique_t *l, unsigned int mot);

/**
 * @brief  Variante choisie pour le mot d'un bigramme dans ce contexte.
 * @pre    l != NULL ; finalise(l) ; bigramme < nb_bigrammes(l).
 * @return son rang parmi les variantes du mot, ou LEXIQUE_AUCUNE si le
 *         bigramme n'a pas de vote (le choix du mot s'applique alors).
 */
unsigned int lexique_variante_bigramme(const lexique_t *l, unsigned int bigramme);

/**
 * @brief  Casse choisie pour le mot d'un bigramme dans ce contexte.
 * @pre    l != NULL ; finalise(l) ; bigramme < nb_bigrammes(l).
 * @return la casse (valeur de casse_t), ou LEXIQUE_AUCUNE si le bigramme
 *         n'a pas de vote.
 */
unsigned int lexique_casse_bigramme(const lexique_t *l, unsigned int bigramme);

/**
 * @brief  Liaison choisie entre les deux mots d'un bigramme.
 * @pre    l != NULL ; finalise(l) ; bigramme < nb_bigrammes(l).
 * @return la liaison (LIAISON_ESPACE si le bigramme n'a pas de vote).
 */
liaison_t lexique_liaison(const lexique_t *l, unsigned int bigramme);

/**
 * @brief  Marques de dialogue choisies entre les deux mots d'un bigramme
 *         (devant le mot, en debut de phrase, pour un bigramme (0, mot)).
 * @pre    l != NULL ; finalise(l) ; bigramme < nb_bigrammes(l).
 * @post   lexique_separateur_valide(lexique_liaison(l, bigramme), resultat).
 * @return les marques (MARQUE_AUCUNE si le bigramme n'a pas de vote).
 */
unsigned int lexique_marques(const lexique_t *l, unsigned int bigramme);

/**
 * @brief  Ecrit la graphie d'affichage du mot d'un bigramme : variante et
 *         casse du bigramme si elles existent, sinon celles du mot ; en
 *         debut de phrase, la premiere lettre est mise en majuscule.
 * @param  l             le lexique.
 * @param  bigramme      le bigramme.
 * @param  debut_phrase  true pour le premier mot d'une phrase.
 * @param  tampon        destination (peut etre NULL si taille == 0).
 * @param  taille        taille du tampon en octets.
 * @pre    l != NULL ; finalise(l) ; bigramme < nb_bigrammes(l) ;
 *         lexique_variante_mot(l, mot(l, bigramme)) != LEXIQUE_AUCUNE ;
 *         tampon != NULL || taille == 0.
 * @post   comme snprintf : si taille > resultat, tampon contient la graphie
 *         complete (valide, terminee par '\0') ; sinon rien n'est ecrit
 *         (pour ne jamais couper une lettre UTF-8 en deux).
 * @return la longueur en octets de la graphie (sans le '\0').
 * @note   Complexite : O(longueur).
 */
size_t lexique_ecrire(const lexique_t *l, unsigned int bigramme,
                      bool debut_phrase, char *tampon, size_t taille);

/**
 * @brief  Ecrit la graphie d'affichage d'un mot hors de tout contexte :
 *         variante et casse choisies pour le mot ; en debut de phrase, la
 *         premiere lettre est mise en majuscule.
 * @param  l             le lexique.
 * @param  mot           le mot.
 * @param  debut_phrase  true pour le premier mot d'une phrase.
 * @param  tampon        destination (peut etre NULL si taille == 0).
 * @param  taille        taille du tampon en octets.
 * @pre    l != NULL ; finalise(l) ; mot < nb_mots(l) ;
 *         lexique_variante_mot(l, mot) != LEXIQUE_AUCUNE ;
 *         tampon != NULL || taille == 0.
 * @post   comme lexique_ecrire.
 * @return la longueur en octets de la graphie (sans le '\0').
 * @note   Complexite : O(longueur).
 */
size_t lexique_ecrire_mot(const lexique_t *l, unsigned int mot,
                          bool debut_phrase, char *tampon, size_t taille);

/**
 * @brief  Redefinit les choix d'un mot (chargement d'un modele).
 * @pre    l != NULL ; finalise(l) ; mot < nb_mots(l) ;
 *         variante < nb_variantes(l, mot) ; casse < NB_CASSES.
 * @post   lexique_variante_mot(l, mot) == variante et
 *         lexique_casse_mot(l, mot) == casse.
 */
void lexique_fixer_mot(lexique_t *l, unsigned int mot, unsigned int variante, casse_t casse);

/**
 * @brief  Redefinit les choix d'un bigramme (chargement d'un modele).
 * @pre    l != NULL ; finalise(l) ; bigramme < nb_bigrammes(l) ;
 *         variante == LEXIQUE_AUCUNE
 *           || variante < nb_variantes(l, mot(l, bigramme)) ;
 *         casse == LEXIQUE_AUCUNE || casse < NB_CASSES ;
 *         lexique_separateur_valide(liaison, marques).
 * @post   lexique_variante_bigramme, lexique_casse_bigramme,
 *         lexique_liaison et lexique_marques renvoient les valeurs donnees.
 */
void lexique_fixer_bigramme(lexique_t *l, unsigned int bigramme,
                            unsigned int variante, unsigned int casse,
                            liaison_t liaison, unsigned int marques);

#endif
