/**
@file langue.h
Fichier langue.h

Detection de la langue d'un corpus : reconnaitre sa langue majoritaire et
reperer les passages qui ne sont pas ecrits dans cette langue (notes en
anglais dans un livre francais, lexique bilingue, chanson etrangere...),
pour que l'apprentissage puisse les ecarter.

Le module ne lit aucun fichier : on lui donne les mots d'un corpus (des
mots de l'alphabet {a, ..., z}, cf. hachage.h), phrase par phrase et texte
par texte, puis on lui demande de conclure.

PROFILS DE LANGUES
  Chaque langue reconnue a un profil : la liste de ses mots-outils les plus
  frequents (articles, pronoms, prepositions, auxiliaires...), ecrits sans
  accents. Dans un texte courant, cette centaine de mots forme environ la
  moitie des mots : elle suffit a reconnaitre la langue.
  Un meme mot peut appartenir a plusieurs profils ("la" : francais,
  espagnol, italien).

PASSAGES
  Les phrases consecutives d'un meme texte sont regroupees en PASSAGES d'au
  moins LANGUE_TAILLE_PASSAGE mots (une phrase isolee est trop courte pour
  etre jugee) ; le dernier passage d'un texte peut etre plus court. Toutes
  les phrases d'un passage ont le meme sort.

DECISION (langue_detecteur_conclure)
  1. Le SCORE d'un passage dans une langue est son nombre de mots presents
     dans le profil de cette langue.
  2. La LANGUE MAJORITAIRE M du corpus est celle dont la somme des scores
     est la plus grande, si elle atteint 1/4 des mots du corpus ; sinon la
     langue est inconnue et tout est garde.
  3. Un passage de n mots est ETRANGER si une autre langue L le domine :
     score(L) >= n / 5, score(L) > score(M) et score(L) - score(M) >= 2.
  4. Un passage non etranger est une REFERENCE si score(M) >= n / 5 : c'est
     surement un texte courant ecrit dans la langue M.
  5. Un mot est CONNU s'il apparait dans un passage de reference et si la
     moitie au moins de ses occurrences sont dans de tels passages.
  6. Un passage est GARDE s'il n'est pas etranger et si 7/10 au moins de ses
     mots sont connus.
  La regle 6 ecarte ce que les profils ne voient pas : un lexique bilingue
  ("eveil, m. awakening") contient peu de mots-outils, mais la moitie de ses
  mots n'apparait nulle part ailleurs dans le corpus. A l'inverse, une
  replique courte ou une liste de noms propres, faites de mots connus, sont
  gardees.

LIMITES : sept langues ont un profil (cf. langue_t). Un corpus ecrit dans
une autre langue a une langue "inconnue" : rien n'est alors ecarte.

Notations utilisees dans les contrats :
  conclu(d)       = langue_detecteur_est_conclu(d)
  nb_phrases(d)   = langue_detecteur_nb_phrases(d)
  nb_mots(d)      = langue_detecteur_nb_mots(d)
  valide(mot)     = hachage_mot_valide(mot)
*/
#ifndef __LANGUE_H__
#define __LANGUE_H__

#include <stdbool.h>
#include <stddef.h>

/** Langues reconnues (qui ont un profil), et langue inconnue. */
typedef enum {
  LANGUE_INCONNUE = 0,  /**< aucune langue reconnue */
  LANGUE_FRANCAIS,      /**< francais */
  LANGUE_ANGLAIS,       /**< anglais */
  LANGUE_ESPAGNOL,      /**< espagnol */
  LANGUE_ALLEMAND,      /**< allemand */
  LANGUE_ITALIEN,       /**< italien */
  LANGUE_PORTUGAIS,     /**< portugais */
  LANGUE_NEERLANDAIS,   /**< neerlandais */
  NB_LANGUES            /**< nombre de valeurs (langue inconnue comprise) */
} langue_t;

/** Nombre minimal de mots d'un passage (sauf en fin de texte). */
#define LANGUE_TAILLE_PASSAGE 40u


/* ======================================================================== */
/*                           Profils de langues                             */
/* ======================================================================== */

/**
 * @brief  Nom d'une langue, en francais, sans accent ni majuscule.
 * @pre    langue < NB_LANGUES.
 * @return une chaine constante non vide ("francais", "anglais", ... ;
 *         "inconnue" pour LANGUE_INCONNUE), a ne pas liberer.
 * @note   Complexite : O(1).
 */
const char *langue_nom(langue_t langue);

/**
 * @brief  Langues dont le profil contient un mot.
 * @param  mot  le mot (sans accents ni majuscules).
 * @pre    mot != NULL ; valide(mot).
 * @post   le bit de LANGUE_INCONNUE (bit 0) n'est jamais leve.
 * @return un masque de bits : le bit (1u << L) est leve si et seulement si
 *         mot est un mot-outil du profil de la langue L ; 0 si le mot
 *         n'appartient a aucun profil (en particulier pour le mot vide).
 * @note   Complexite : O(longueur du mot * log(taille des profils)).
 */
unsigned int langue_profils(const char *mot);


/* ======================================================================== */
/*                        Detecteur : creation, ajouts                      */
/* ======================================================================== */

/** Type abstrait detecteur (structure definie dans langue.c). */
typedef struct langue_detecteur_s langue_detecteur_t;

/**
 * @brief  Cree un detecteur vide.
 * @pre    aucune.
 * @post   si le resultat d est non NULL : !conclu(d), nb_phrases(d) == 0 et
 *         nb_mots(d) == 0.
 * @return le detecteur (a liberer avec langue_detecteur_detruire), ou NULL
 *         si la memoire manque (errno == ENOMEM).
 * @note   Complexite : O(1).
 */
langue_detecteur_t *langue_detecteur_creer(void);

/**
 * @brief  Detruit un detecteur et libere toute sa memoire.
 * @pre    d != NULL (*d peut etre NULL : rien n'est fait).
 * @post   *d == NULL.
 * @note   Complexite : O(1 + nombre de mots distincts).
 */
void langue_detecteur_detruire(langue_detecteur_t **d);

/**
 * @brief  Ajoute le mot suivant du texte en cours.
 * @param  d           le detecteur.
 * @param  mot         le mot (il est recopie).
 * @param  fin_phrase  true si ce mot est le dernier de sa phrase.
 * @pre    d != NULL ; !conclu(d) ; mot != NULL ; valide(mot) ; mot non vide.
 * @post   en cas de succes : nb_mots(d) == ancien(nb_mots(d)) + 1 ; si
 *         fin_phrase, nb_phrases(d) == ancien(nb_phrases(d)) + 1 : la
 *         phrase qui se termine porte le numero ancien(nb_phrases(d)).
 *         En cas d'echec : nb_mots(d) et nb_phrases(d) sont inchanges et le
 *         detecteur reste utilisable.
 * @return true en cas de succes, false si la memoire manque
 *         (errno == ENOMEM).
 * @note   Complexite : O(longueur du mot) en moyenne, amortie.
 */
bool langue_detecteur_ajouter_mot(langue_detecteur_t *d, const char *mot, bool fin_phrase);

/**
 * @brief  Termine le texte en cours (fin d'un fichier) : un passage ne
 *         s'etend jamais sur deux textes. Si le dernier mot ajoute ne
 *         terminait pas sa phrase, la phrase en cours est terminee.
 * @pre    d != NULL ; !conclu(d).
 * @post   le prochain mot ajoute commencera une phrase, un passage et un
 *         texte ; nb_mots(d) est inchange ; nb_phrases(d) n'augmente (de 1)
 *         que si une phrase etait en cours. Ne peut pas echouer.
 * @note   Complexite : O(1).
 */
void langue_detecteur_fin_texte(langue_detecteur_t *d);

/**
 * @brief  Termine les ajouts et calcule la langue majoritaire et le sort de
 *         chaque phrase (cf. DECISION en tete de fichier). Le texte en
 *         cours est d'abord termine (cf. langue_detecteur_fin_texte).
 * @pre    d != NULL ; !conclu(d).
 * @post   en cas de succes : conclu(d) ; nb_mots(d) est inchange. En cas
 *         d'echec : !conclu(d) ; le texte en cours est termine, rien d'autre
 *         n'a change (on peut appeler de nouveau la fonction).
 * @return true en cas de succes, false si la memoire manque
 *         (errno == ENOMEM).
 * @note   Complexite : O(nb_mots + nombre de mots distincts).
 */
bool langue_detecteur_conclure(langue_detecteur_t *d);


/* ======================================================================== */
/*                         Detecteur : consultation                         */
/* ======================================================================== */

/** @brief Indique si le detecteur a conclu. @pre d != NULL. */
bool langue_detecteur_est_conclu(const langue_detecteur_t *d);

/**
 * @brief  Nombre de phrases terminees ; elles sont numerotees de 0 a
 *         resultat - 1 dans l'ordre des ajouts.
 * @pre    d != NULL.
 */
size_t langue_detecteur_nb_phrases(const langue_detecteur_t *d);

/** @brief Nombre de mots ajoutes (occurrences). @pre d != NULL. */
size_t langue_detecteur_nb_mots(const langue_detecteur_t *d);

/**
 * @brief  Langue majoritaire du corpus.
 * @pre    d != NULL ; conclu(d).
 * @return la langue, ou LANGUE_INCONNUE si aucun profil ne couvre 1/4 des
 *         mots (en particulier pour un corpus vide).
 * @note   Complexite : O(1).
 */
langue_t langue_detecteur_majoritaire(const langue_detecteur_t *d);

/**
 * @brief  Indique si une phrase est gardee (ecrite dans la langue
 *         majoritaire, autant que l'on puisse en juger).
 * @pre    d != NULL ; conclu(d) ; phrase < nb_phrases(d).
 * @post   si langue_detecteur_majoritaire(d) == LANGUE_INCONNUE, le
 *         resultat est true ; deux phrases d'un meme passage ont le meme
 *         resultat.
 * @return true si la phrase est gardee, false si elle est ecartee.
 * @note   Complexite : O(1).
 */
bool langue_detecteur_phrase_gardee(const langue_detecteur_t *d, size_t phrase);

/**
 * @brief  Nombre de phrases ecartees.
 * @pre    d != NULL ; conclu(d).
 * @post   resultat <= nb_phrases(d).
 */
size_t langue_detecteur_nb_phrases_ecartees(const langue_detecteur_t *d);

/**
 * @brief  Nombre de mots (occurrences) des phrases ecartees.
 * @pre    d != NULL ; conclu(d).
 * @post   resultat <= nb_mots(d).
 */
size_t langue_detecteur_nb_mots_ecartes(const langue_detecteur_t *d);

#endif
