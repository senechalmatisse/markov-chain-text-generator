/**
@file repertoire.h
Fichier repertoire.h

Lecture de textes : iterateur des mots d'un fichier, liste des fichiers
d'un repertoire et construction d'une chaine de Markov a partir de ces fichiers.

DEFINITION D'UN MOT
  Un mot est une suite maximale de lettres. Les lettres sont :
    - les lettres ASCII 'a' .. 'z' et 'A' .. 'Z' ;
    - les lettres accentuees latines (fichiers en UTF-8, ou en Latin-1 :
      un octet qui ne forme pas une sequence UTF-8 valide est lu comme un
      caractere Latin-1).
  Les mots renvoyes sont en minuscules et sans accents :
    E -> e, e accent aigu -> e, c cedille -> c, oe lie -> oe, ae lie -> ae,
    eszett -> ss, ...
  Ce sont donc des mots de l'alphabet {a, ..., z} (cf. hachage_mot_valide).
  Tout autre caractere (ponctuation, apostrophes, tirets, chiffres,
  espaces, symboles, guillemets, ...) separe les mots et est retire :
  "C'est-a-dire" donne les mots "c", "est", "a", "dire".
  L'iterateur conserve aussi, pour chaque mot, sa GRAPHIE d'origine (en
  UTF-8, avec ses accents et ses majuscules), le separateur qui le suit
  (LIAISON) et la PONCTUATION qui termine sa phrase (cf. texte.h).

DEFINITION D'UNE FIN DE PHRASE
  Un mot termine une phrase s'il est suivi, avant le mot suivant :
    - d'un '.', d'un '!', d'un '?' ou d'un caractere points de suspension,
      SAUF pour un '.' place juste apres une initiale ou une abreviation
      ("M. Dupont", "J.-J. Rousseau", "MM. Dupont") et pour un '.' place
      entre deux chiffres ("3.5") ;
    - ou d'une ligne vide (changement de paragraphe, de strophe, titre) ;
    - ou de la fin du fichier (le dernier mot d'un fichier termine toujours
      une phrase).

TEXTES DU PROJET GUTENBERG
  Les en-tetes et licences du projet Gutenberg (en anglais) sont ignores :
  si le fichier contient une ligne de debut ("*** START OF ... PROJECT
  GUTENBERG ..."), la lecture commence apres elle ; elle s'arrete a la
  premiere ligne de fin ("*** END OF ... PROJECT GUTENBERG ...", "End of
  the Project Gutenberg EBook ..." ou "End of Project Gutenberg's ...").

CHAINE DE MARKOV D'UN REPERTOIRE
  L'etat 0 (REPERTOIRE_DEBUT_PHRASE) correspond au mot vide "" et represente
  la frontiere entre deux phrases. Pour chaque phrase w1 w2 ... wn, on
  augmente de 1 les poids des transitions
      0 -> w1, w1 -> w2, ..., w(n-1) -> wn, wn -> 0        (ordre 1)
  La transition wn -> 0 permet, lors de la generation, de savoir quand une
  phrase se termine. Ainsi :
    poids(0, w)  = nombre de phrases commencant par w,
    poids(w, 0)  = nombre de phrases se terminant par w,
    poids(u, v)  = nombre de fois ou v suit u dans une meme phrase.
  Tout mot rencontre a au moins un voisin (le mot suivant ou la fin de
  phrase) : depuis n'importe quel etat atteignable, on peut se deplacer.
  A L'ORDRE 2, un etat est un bigramme (paire de mots consecutifs) de la
  table des paires ; l'etat 0 est la paire (0, 0) et les transitions sont
      (0,0) -> (0,w1) -> (w1,w2) -> ... -> (w(n-1),wn) -> (0,0)
  Le mot suivant depend alors des deux mots precedents.

La table de hachage est stockee EN DEHORS de la chaine de Markov : les deux
modules restent independants (et testables separement) ; c'est l'appelant
qui fait le lien, l'etat i correspondant au mot d'identifiant i (ordre 1)
ou a la paire d'identifiant i (ordre 2).
*/
#ifndef __REPERTOIRE_H__
#define __REPERTOIRE_H__

#include <stdbool.h>
#include <stddef.h>
#include "texte.h"
#include "hachage.h"
#include "markov.h"
#include "lexique.h"

/** Etat (et identifiant de mot) representant le debut / la fin de phrase. */
#define REPERTOIRE_DEBUT_PHRASE 0u

/**
 * Nombre d'etats "ponctuation finale" de la chaine de ponctuation : les
 * etats 0 .. 3 sont PONCTUATION_POINT, _EXCLAMATION, _INTERROGATION et
 * _SUSPENSION ; l'etat REPERTOIRE_NB_FINS + w correspond au mot w.
 */
#define REPERTOIRE_NB_FINS 4u


/* ======================================================================== */
/*                       Iterateur des mots d'un fichier                    */
/* ======================================================================== */

/** Type abstrait iterateur (structure definie dans repertoire.c). */
typedef struct iterateur_mots_s iterateur_mots_t;

/**
 * @brief  Cree un iterateur sur les mots d'un fichier texte.
 *         Le fichier est d'abord parcouru une fois pour reperer les
 *         marques du projet Gutenberg et savoir s'il contient des accents.
 * @param  chemin_fichier  chemin relatif ou absolu du fichier.
 * @pre    chemin_fichier != NULL.
 * @post   si le resultat est non NULL, il est positionne avant le premier
 *         mot du fichier.
 * @return l'iterateur, a liberer avec iterateur_mots_detruire ; NULL si le
 *         fichier ne peut pas etre ouvert (inexistant, droits insuffisants,
 *         repertoire : errno vaut alors EISDIR), ne peut pas etre relu
 *         depuis le debut (tube : ESPIPE) ou si la memoire manque (errno
 *         indique la cause).
 * @note   Complexite : O(taille du fichier) (premier parcours).
 */
iterateur_mots_t *iterateur_mots_creer(const char *chemin_fichier);

/**
 * @brief  Detruit un iterateur : ferme le fichier et libere la memoire.
 * @param  iterateur  adresse du pointeur vers l'iterateur.
 * @pre    iterateur != NULL (*iterateur peut etre NULL : rien n'est fait).
 * @post   *iterateur == NULL ; les mots deja renvoyes ne sont plus valides.
 * @note   Complexite : O(1).
 */
void iterateur_mots_detruire(iterateur_mots_t **iterateur);

/**
 * @brief  Renvoie le mot suivant du fichier et indique s'il termine une
 *         phrase.
 * @param  iterateur   l'iterateur.
 * @param  fin_phrase  si non NULL, recoit true si le mot renvoye est le
 *                     dernier de sa phrase (cf. definition ci-dessus), false
 *                     sinon (et false quand la fonction renvoie NULL).
 * @pre    iterateur != NULL.
 * @post   si le resultat est non NULL : c'est un mot non vide et
 *         hachage_mot_valide(resultat) ; il est stocke dans l'iterateur et
 *         reste valide jusqu'au prochain appel de iterateur_mots_suivant ou
 *         de iterateur_mots_detruire (le recopier pour le conserver) ;
 *         iterateur_mots_graphie, _liaison et _ponctuation decrivent ce mot.
 *         Une fois que la fonction a renvoye NULL, elle renvoie toujours NULL.
 * @return le mot suivant, ou NULL s'il n'y a plus de mot (fin du fichier)
 *         ou en cas d'erreur (cf. iterateur_mots_erreur).
 * @note   Complexite : O(longueur du mot et des separateurs qui le suivent)
 *         amortie.
 */
const char *iterateur_mots_suivant(iterateur_mots_t *iterateur, bool *fin_phrase);

/**
 * @brief  Graphie d'origine du dernier mot renvoye (UTF-8, avec accents et
 *         majuscules ; un texte Latin-1 est converti en UTF-8).
 * @pre    iterateur != NULL ; le dernier appel de iterateur_mots_suivant a
 *         renvoye un mot.
 * @post   lexique_graphie_valide(resultat) et
 *         repertoire_graphie_valide(resultat, mot renvoye) ; meme duree de
 *         validite que le mot.
 * @return la graphie.
 * @note   Complexite : O(1).
 */
const char *iterateur_mots_graphie(const iterateur_mots_t *iterateur);

/**
 * @brief  Separateur entre le dernier mot renvoye et le mot suivant de la
 *         meme phrase.
 * @pre    iterateur != NULL ; le dernier appel de iterateur_mots_suivant a
 *         renvoye un mot.
 * @return la liaison ; LIAISON_ESPACE si le mot termine sa phrase.
 *         Apostrophe ou trait d'union : seulement s'il s'agit du seul
 *         caractere entre les deux mots ("c'est", "dit-il") ; abreviation :
 *         "M. Dupont" ; sinon le signe le plus fort parmi ';' , ':' puis
 *         ',' ; sinon LIAISON_ESPACE.
 * @note   Complexite : O(1).
 */
liaison_t iterateur_mots_liaison(const iterateur_mots_t *iterateur);

/**
 * @brief  Ponctuation qui termine la phrase du dernier mot renvoye.
 * @pre    iterateur != NULL ; le dernier appel de iterateur_mots_suivant a
 *         renvoye un mot.
 * @return PONCTUATION_AUCUNE si le mot ne termine pas sa phrase, ou s'il la
 *         termine sans ponctuation (ligne vide, fin du fichier) ; sinon,
 *         par priorite : '?' -> _INTERROGATION, '!' -> _EXCLAMATION,
 *         points de suspension (ou au moins deux '.') -> _SUSPENSION,
 *         '.' -> _POINT.
 * @note   Complexite : O(1).
 */
ponctuation_t iterateur_mots_ponctuation(const iterateur_mots_t *iterateur);

/**
 * @brief  Indique si le fichier contient des caracteres non ASCII (accents)
 *         dans sa partie lue : les graphies d'un fichier sans aucun accent
 *         ne renseignent pas sur les accents des mots.
 * @pre    iterateur != NULL.
 * @return true si au moins un octet non ASCII est present.
 * @note   Complexite : O(1).
 */
bool iterateur_mots_accentue(const iterateur_mots_t *iterateur);

/**
 * @brief  Indique si une erreur s'est produite pendant la lecture.
 * @pre    iterateur != NULL.
 * @return 0 si aucune erreur ne s'est produite ; sinon le code errno de
 *         l'erreur (EIO pour une erreur de lecture, ENOMEM si la memoire a
 *         manque) : la lecture a alors ete interrompue.
 * @note   Complexite : O(1).
 */
int iterateur_mots_erreur(const iterateur_mots_t *iterateur);

/**
 * @brief  Indique si une graphie s'ecrit, sans accents ni majuscules, comme
 *         le mot donne (meme translitteration que l'iterateur).
 * @pre    graphie != NULL ; cle != NULL.
 * @return true si graphie est une graphie valide (lexique_graphie_valide)
 *         dont la translitteration est exactement cle ; false sinon.
 * @note   Complexite : O(longueur).
 */
bool repertoire_graphie_valide(const char *graphie, const char *cle);


/* ======================================================================== */
/*                         Fichiers d'un repertoire                         */
/* ======================================================================== */

/**
 * @brief  Liste les fichiers d'un repertoire : fichiers ordinaires (ou liens
 *         vers des fichiers ordinaires) dont le nom ne commence pas par '.',
 *         et, si recursif, ceux des sous-repertoires (non caches, sans
 *         suivre les liens vers des repertoires, a 64 niveaux au plus).
 * @param  chemin_repertoire  chemin relatif ou absolu du repertoire.
 * @param  recursif           parcourir aussi les sous-repertoires.
 * @param  nb_fichiers        recoit le nombre de fichiers.
 * @pre    chemin_repertoire != NULL ; nb_fichiers != NULL.
 * @post   en cas de succes : le resultat contient *nb_fichiers chemins
 *         (repertoire + "/" + nom ...) tries par ordre des octets (strcmp),
 *         ce qui rend les resultats independants de l'ordre du systeme.
 * @return le tableau des chemins (a liberer avec repertoire_liberer_fichiers,
 *         non NULL meme s'il est vide), ou NULL en cas d'echec, errno
 *         indiquant la cause (ENOENT, ENOTDIR, EACCES, ENOMEM, ELOOP si la
 *         profondeur maximale est depassee).
 * @note   Complexite : O(F log F) pour F entrees.
 */
char **repertoire_lister_fichiers(const char *chemin_repertoire, bool recursif, size_t *nb_fichiers);

/**
 * @brief  Libere un tableau renvoye par repertoire_lister_fichiers.
 * @pre    fichiers != NULL (*fichiers peut etre NULL : rien n'est fait) ;
 *         nb_fichiers est le nombre renvoye lors de la creation.
 * @post   *fichiers == NULL.
 */
void repertoire_liberer_fichiers(char ***fichiers, size_t nb_fichiers);


/* ======================================================================== */
/*                     Chaine de Markov d'un repertoire                     */
/* ======================================================================== */

/** Options de repertoire_apprendre. */
typedef struct {
  unsigned int ordre;           /**< 1 : un etat = un mot ; 2 : une paire */
  bool recursif;                /**< lire aussi les sous-repertoires */
  hachage_paires_t *bigrammes;  /**< NULL, ou table VIDE qui recoit les
                                     bigrammes ; obligatoire a l'ordre 2 et
                                     avec un lexique */
  lexique_t *lexique;           /**< NULL, ou lexique VIDE qui recoit les
                                     graphies et liaisons */
  markov_t *ponctuation;        /**< NULL, ou chaine VIDE qui recoit la
                                     ponctuation finale de chaque mot */
} repertoire_options_t;

/**
 * @brief  Construit une chaine de Markov a partir des mots des fichiers d'un
 *         repertoire (cf. repertoire_lister_fichiers pour les fichiers lus,
 *         dans l'ordre alphabetique de leurs chemins : une meme graine
 *         donne toujours le meme texte), et remplit les structures
 *         facultatives des options.
 * @param  chemin_repertoire  chemin relatif ou absolu du repertoire.
 * @param  graine             graine du generateur de la chaine.
 * @param  mots               table de hachage VIDE, creee par l'appelant.
 * @param  options            options (cf. repertoire_options_t).
 * @pre    chemin_repertoire != NULL ; mots != NULL ;
 *         hachage_nb_mots(mots) == 0 ; options != NULL ;
 *         options->ordre == 1 || options->ordre == 2 ;
 *         options->bigrammes != NULL si ordre == 2 ou lexique != NULL ;
 *         chaque structure facultative fournie est vide (0 paire, 0 mot,
 *         0 etat) et le lexique n'est pas finalise.
 * @post   en cas de succes, soit m le resultat et n = hachage_nb_mots(mots) :
 *           - markov_est_finalisee(m) ; le mot 0 est le mot vide "" ;
 *           - ordre 1 : markov_nb_etats(m) == n, l'etat i est le mot i ;
 *             ordre 2 : markov_nb_etats(m) == hachage_paires_nb(bigrammes),
 *             l'etat i est la paire i ; la paire 0 est (0, 0) ;
 *           - bigrammes (si fourni) : la paire 0 est (0, 0) ; il contient
 *             chaque (0, w1) de debut de phrase et chaque couple (u, v) de
 *             mots consecutifs d'une meme phrase, et seulement eux ;
 *           - lexique (si fourni) : finalise, n mots, un bigramme par paire
 *             (le bigramme i a pour mot le second element de la paire i) ;
 *             chaque mot i > 0 a une variante choisie ;
 *           - ponctuation (si fournie) : finalisee, REPERTOIRE_NB_FINS + n
 *             etats ; le poids de (REPERTOIRE_NB_FINS + w) -> p est le
 *             nombre de phrases terminees par w avec la ponctuation p (une
 *             fin sans ponctuation compte comme un point) ;
 *           - markov_nb_voisins(m, 0) == 0 si et seulement si aucun mot n'a
 *             ete trouve ; tout etat i > 0 a au moins un voisin ; si un etat
 *             a pour voisin 0, son dernier mot a au moins un voisin dans la
 *             chaine de ponctuation.
 *         En cas d'echec : la chaine n'est pas creee ; si le repertoire ne
 *         peut pas etre ouvert, les structures fournies sont inchangees,
 *         sinon elles peuvent etre partiellement remplies (l'appelant doit
 *         de toute facon les detruire).
 * @return la chaine (a liberer avec markov_detruire), ou NULL en cas
 *         d'echec, errno indiquant la cause : repertoire inexistant
 *         (ENOENT), pas un repertoire (ENOTDIR), droits insuffisants
 *         (EACCES), erreur de lecture (EIO), memoire insuffisante (ENOMEM),
 *         poids trop grand (ERANGE), arborescence trop profonde (ELOOP).
 * @note   Complexite : O(N + F log F + T log k_max) en moyenne, N etant le
 *         nombre total de caracteres lus, F le nombre de fichiers et T le
 *         nombre de transitions distinctes.
 */
markov_t *repertoire_apprendre(const char *chemin_repertoire,
                               unsigned int graine, hachage_t *mots,
                               const repertoire_options_t *options);

/**
 * @brief  Construit une chaine de Markov d'ordre 1 a partir des mots de tous
 *         les fichiers d'un repertoire (sans les sous-repertoires) :
 *         equivaut a repertoire_apprendre avec les options
 *         { 1, false, NULL, NULL, NULL }.
 * @param  chemin_repertoire  chemin relatif ou absolu du repertoire.
 * @param  graine             graine du generateur de la chaine.
 * @param  mots               table de hachage VIDE, creee par l'appelant,
 *                            qui recoit les mots rencontres.
 * @pre    chemin_repertoire != NULL ; mots != NULL ;
 *         hachage_nb_mots(mots) == 0.
 * @post   cf. repertoire_apprendre (ordre 1).
 * @return la chaine (a liberer avec markov_detruire), ou NULL en cas
 *         d'echec (errno indique la cause, cf. repertoire_apprendre).
 */
markov_t *repertoire_creer_markov(const char *chemin_repertoire, unsigned int graine, hachage_t *mots);

#endif
