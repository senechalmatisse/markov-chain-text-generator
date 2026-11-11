/**
@file repertoire.h
Fichier repertoire.h

Lecture de textes : iterateur des mots d'un fichier, liste des fichiers
d'un repertoire ou d'un corpus (plusieurs fichiers et repertoires) et
construction d'une chaine de Markov a partir de ces fichiers.

DEFINITION D'UN MOT
  Un mot est une suite maximale de lettres. Les lettres sont :
    - les lettres ASCII 'a' .. 'z' et 'A' .. 'Z' ;
    - les lettres latines accentuees de Latin-1 et de Latin etendu A, et
      les lettres roumaines a virgule (cf. lexique_est_lettre) : francais,
      allemand, langues scandinaves, polonais, tcheque, hongrois, turc,
      roumain, esperanto... Les fichiers sont en UTF-8, ou en Latin-1 : un
      octet qui ne forme pas une sequence UTF-8 valide est lu comme un
      caractere Latin-1.
  Les mots renvoyes sont en minuscules et sans accents :
    E -> e, e accent aigu -> e, c cedille -> c, oe lie -> oe, ae lie -> ae,
    eszett -> ss, l barre -> l, s cedille -> s, ...
  Ce sont donc des mots de l'alphabet {a, ..., z} (cf. hachage_mot_valide).
  Tout autre caractere (ponctuation, apostrophes, tirets, chiffres,
  espaces, symboles, guillemets, ...) separe les mots et est retire :
  "C'est-a-dire" donne les mots "c", "est", "a", "dire".
  L'iterateur conserve aussi, pour chaque mot, sa GRAPHIE d'origine (en
  UTF-8, avec ses accents et ses majuscules), le separateur qui le suit
  (LIAISON), les MARQUES qui le precedent et la PONCTUATION qui termine sa
  phrase (cf. texte.h).

MARQUES : GUILLEMETS, TIRETS ET PARENTHESES (cf. texte.h)
  Entre deux mots, ou devant le premier mot du fichier, l'iterateur
  reconnait :
    - les parentheses et les crochets, ouvrants et fermants ;
    - les guillemets ouvrants : guillemet francais ouvrant (U+00AB) et
      guillemet allemand ouvrant (U+201E) ;
    - le guillemet fermant francais (U+00BB) ;
    - le guillemet droit '"' et les guillemets anglais (U+201C, U+201D),
      dont le role depend de la langue et de la position : ils sont
      fermants s'ils sont suivis d'un blanc, d'une fin de ligne, de la fin
      du fichier, d'un signe de ponctuation ou d'un autre guillemet, et
      ouvrants s'ils sont colles a ce qui les suit ;
    - les tirets : tirets cadratin et demi-cadratin et barre horizontale
      (U+2014, U+2013, U+2015), deux ou trois '-' a la suite ("--Oui",
      usage des livres du projet Gutenberg ; quatre '-' ou plus forment un
      trait decoratif, ignore), et un '-' seul precede d'un blanc, d'un
      debut de ligne ou du debut du fichier ("- Oui").
  Les marques placees apres la ponctuation finale d'une phrase sont celles
  du premier mot de la phrase suivante.

DEFINITION D'UNE FIN DE PHRASE
  Un mot termine une phrase s'il est suivi, avant le mot suivant :
    - d'un '.', d'un '!', d'un '?' ou d'un caractere points de suspension,
      SAUF pour un '.' place juste apres une initiale ou une abreviation
      ("M. Dupont", "J.-J. Rousseau", "MM. Dupont"), pour un '.' place
      entre deux chiffres ("3.5"), et si le mot suivant commence par une
      minuscule : la ponctuation est alors interne a la phrase ("Ah! dit-il",
      "Qui? demanda-t-il", "il hesita... puis", "etc. et") et devient la
      liaison LIAISON_EXCLAMATION, LIAISON_INTERROGATION,
      LIAISON_SUSPENSION ou LIAISON_ABREVIATION ;
    - ou d'une ligne vide (changement de paragraphe, de strophe, titre) ;
    - ou de la fin du fichier (le dernier mot d'un fichier termine toujours
      une phrase) ;
    - ou, en lecture "une phrase par ligne" (poemes, chansons, listes :
      cf. iterateur_mots_phrase_par_ligne), d'une simple fin de ligne.

TEXTES DU PROJET GUTENBERG
  Les en-tetes et licences du projet Gutenberg (en anglais) sont ignores :
  si le fichier contient une ligne de debut ("*** START OF ... PROJECT
  GUTENBERG ..."), la lecture commence apres elle ; elle s'arrete a la
  premiere ligne de fin ("*** END OF ... PROJECT GUTENBERG ...", "End of
  the Project Gutenberg EBook ..." ou "End of Project Gutenberg's ...").

COURRIERS ELECTRONIQUES
  Un fichier qui commence comme un courrier (un message ".eml" ou une
  boite mbox : cf. mail_est_courrier) est remplace par le texte de ses
  messages, sans en-tetes, citations ni signatures, decode et converti en
  UTF-8 (cf. mail.h) : c'est ce texte que l'iterateur lit, chaque message
  se terminant par une ligne vide.

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
  A L'ORDRE k >= 3, un etat est le k-uplet des k derniers mots de la phrase
  (complete a gauche par des 0), range dans une table de n-uplets de taille
  k ; l'etat 0 est (0, ..., 0) et, pour k == 3, les transitions sont
      (0,0,0) -> (0,0,w1) -> (0,w1,w2) -> (w1,w2,w3) -> ... -> (0,0,0)
  Le mot suivant depend des k mots precedents : plus k est grand, plus le
  texte genere est coherent, et plus il recopie le corpus.

FILTRE DE LANGUE (option filtrer_langues)
  Le corpus est d'abord lu une premiere fois pour reconnaitre sa langue
  majoritaire et reperer les passages qui ne sont pas ecrits dans cette
  langue (cf. langue.h) ; les phrases de ces passages sont ensuite ignorees
  par l'apprentissage, comme si elles n'etaient pas dans les fichiers.

La table de hachage est stockee EN DEHORS de la chaine de Markov : les deux
modules restent independants (et testables separement) ; c'est l'appelant
qui fait le lien, l'etat i correspondant au mot d'identifiant i (ordre 1),
a la paire d'identifiant i (ordre 2) ou au n-uplet d'identifiant i.
*/
#ifndef __REPERTOIRE_H__
#define __REPERTOIRE_H__

#include <stdbool.h>
#include <stddef.h>
#include "texte.h"
#include "hachage.h"
#include "markov.h"
#include "lexique.h"
#include "langue.h"

/** Etat (et identifiant de mot) representant le debut / la fin de phrase. */
#define REPERTOIRE_DEBUT_PHRASE 0u

/** Ordre maximal d'une chaine de Markov apprise. */
#define REPERTOIRE_ORDRE_MAX 8u

/**
 * Nombre d'etats "fin de phrase" de la chaine de ponctuation : les etats
 * 0 .. 4 sont PONCTUATION_POINT, _EXCLAMATION, _INTERROGATION, _SUSPENSION
 * et _AUCUNE (phrase terminee sans ponctuation) ; l'etat
 * REPERTOIRE_NB_FINS + w correspond au mot w.
 */
#define REPERTOIRE_NB_FINS 5u


/* ======================================================================== */
/*                       Iterateur des mots d'un fichier                    */
/* ======================================================================== */

/** Type abstrait iterateur (structure definie dans repertoire.c). */
typedef struct iterateur_mots_s iterateur_mots_t;

/**
 * @brief  Cree un iterateur sur les mots d'un fichier texte, ou du texte
 *         des messages d'un courrier (cf. COURRIERS ELECTRONIQUES, en
 *         tete de ce fichier). Le texte est d'abord parcouru une fois pour
 *         reperer les marques du projet Gutenberg et savoir s'il contient des accents.
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
 * @brief  Choisit la lecture "une phrase par ligne" : chaque fin de ligne
 *         termine alors la phrase en cours (poemes, chansons, listes), en
 *         plus des fins de phrase habituelles.
 * @param  iterateur  l'iterateur.
 * @param  actif      true pour une phrase par ligne, false (valeur a la
 *                    creation) pour la lecture habituelle.
 * @pre    iterateur != NULL ; iterateur_mots_suivant n'a pas encore ete
 *         appele sur cet iterateur.
 * @post   les appels suivants de iterateur_mots_suivant appliquent ce choix.
 * @note   Complexite : O(1).
 */
void iterateur_mots_phrase_par_ligne(iterateur_mots_t *iterateur, bool actif);

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
 *         iterateur_mots_graphie, _liaison, _marques et _ponctuation
 *         decrivent ce mot.
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
 *         caractere entre les deux mots ("c'est", "dit-il") ; ponctuation
 *         interne (cf. DEFINITION D'UNE FIN DE PHRASE) : '?', '!', points
 *         de suspension, ou point ("etc. et", comme "M. Dupont" :
 *         LIAISON_ABREVIATION) ; sinon le signe le plus fort parmi ';' ,
 *         ':' puis ',' ; sinon LIAISON_ESPACE.
 * @note   Complexite : O(1).
 */
liaison_t iterateur_mots_liaison(const iterateur_mots_t *iterateur);

/**
 * @brief  Marques (guillemets, tirets, parentheses) placees DEVANT le
 *         dernier mot renvoye : entre le mot precedent et lui (meme si une
 *         fin de phrase les separe), ou avant lui s'il est le premier mot
 *         du fichier.
 * @pre    iterateur != NULL ; le dernier appel de iterateur_mots_suivant a
 *         renvoye un mot.
 * @post   resultat <= MARQUES_TOUTES ; si le mot precedent lui est lie par
 *         une apostrophe ou un trait d'union, resultat == MARQUE_AUCUNE.
 * @return la combinaison (par |) des marques MARQUE_* (cf. texte.h et
 *         l'en-tete de ce fichier).
 * @note   Complexite : O(1).
 */
unsigned int iterateur_mots_marques(const iterateur_mots_t *iterateur);

/**
 * @brief  Ponctuation qui termine la phrase du dernier mot renvoye.
 * @pre    iterateur != NULL ; le dernier appel de iterateur_mots_suivant a
 *         renvoye un mot.
 * @return PONCTUATION_AUCUNE si le mot ne termine pas sa phrase, ou s'il la
 *         termine sans ponctuation (ligne vide, fin de ligne en lecture
 *         "une phrase par ligne", fin du fichier) ; sinon,
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
 * @brief  Nombre de messages du courrier lu (cf. COURRIERS ELECTRONIQUES).
 * @pre    iterateur != NULL.
 * @return 0 pour un fichier texte ordinaire ; sinon le nombre de messages
 *         (1 pour un message seul, au moins 1 pour une boite mbox).
 * @note   Complexite : O(1).
 */
size_t iterateur_mots_nb_messages(const iterateur_mots_t *iterateur);

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

/**
 * @brief  Cle d'une graphie : le mot de l'alphabet {a, ..., z} que
 *         l'iterateur renvoie pour cette graphie (sans accents ni
 *         majuscules). Sert a retrouver dans la table des mots un mot tape
 *         par l'utilisateur ("Ete" avec ses accents a pour cle "ete").
 * @pre    graphie != NULL.
 * @post   si le resultat est non NULL : c'est un mot non vide,
 *         hachage_mot_valide(resultat) et
 *         repertoire_graphie_valide(graphie, resultat).
 * @return la cle, a liberer avec free ; NULL si graphie n'est pas une
 *         graphie (suite non vide de lettres reconnues en UTF-8, cf.
 *         lexique_graphie_valide : errno == EINVAL) ou si la memoire manque
 *         (errno == ENOMEM).
 * @note   Complexite : O(longueur).
 */
char *repertoire_cle(const char *graphie);


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
 * @brief  Liste les fichiers d'un corpus : chaque chemin designe soit un
 *         fichier ordinaire, pris tel quel, soit un repertoire, dont les
 *         fichiers sont listes comme par repertoire_lister_fichiers.
 * @param  chemins      chemins des fichiers et repertoires du corpus.
 * @param  nb_chemins   nombre de chemins.
 * @param  recursif     parcourir aussi les sous-repertoires des repertoires.
 * @param  nb_fichiers  recoit le nombre de fichiers.
 * @pre    chemins != NULL ; nb_chemins >= 1 ; chaque chemins[i] != NULL ;
 *         nb_fichiers != NULL.
 * @post   en cas de succes : le resultat contient *nb_fichiers chemins,
 *         dans l'ordre des chemins donnes ; les fichiers d'un meme
 *         repertoire se suivent, tries par ordre des octets (strcmp). Un
 *         fichier donne deux fois figure deux fois.
 * @return le tableau des chemins (a liberer avec repertoire_liberer_fichiers,
 *         non NULL meme s'il est vide), ou NULL en cas d'echec, errno
 *         indiquant la cause : cf. repertoire_lister_fichiers, et EINVAL si
 *         un chemin ne designe ni un fichier ordinaire ni un repertoire
 *         (tube, peripherique...).
 * @note   Complexite : O(F log F) pour F entrees.
 */
char **repertoire_lister_corpus(const char *const *chemins, size_t nb_chemins,
                                bool recursif, size_t *nb_fichiers);

/**
 * @brief  Libere un tableau renvoye par repertoire_lister_fichiers ou par
 *         repertoire_lister_corpus.
 * @pre    fichiers != NULL (*fichiers peut etre NULL : rien n'est fait) ;
 *         nb_fichiers est le nombre renvoye lors de la creation.
 * @post   *fichiers == NULL.
 */
void repertoire_liberer_fichiers(char ***fichiers, size_t nb_fichiers);


/* ======================================================================== */
/*                     Chaine de Markov d'un repertoire                     */
/* ======================================================================== */

/** Bilan d'un apprentissage (cf. repertoire_options_t). */
typedef struct {
  size_t nb_fichiers;          /**< fichiers lus */
  size_t nb_messages;          /**< messages des courriers lus (cf.
                                    iterateur_mots_nb_messages) */
  size_t nb_phrases;           /**< phrases lues (ecartees comprises) */
  size_t nb_mots;              /**< mots lus (occurrences, ecartes compris) */
  size_t nb_phrases_ecartees;  /**< phrases ecartees par le filtre de langue */
  size_t nb_mots_ecartes;      /**< mots des phrases ecartees */
  langue_t langue;             /**< langue majoritaire ; LANGUE_INCONNUE si
                                    elle n'est pas reconnue ou sans filtre */
} repertoire_bilan_t;

/**
 * Options de repertoire_apprendre et de repertoire_apprendre_corpus. Les
 * options { 1u, false, NULL, NULL, NULL, NULL, false, false, NULL, 0u,
 * NULL, NULL } demandent une chaine d'ordre 1 seule.
 */
typedef struct {
  unsigned int ordre;           /**< 1 : un etat = un mot ; 2 : une paire ;
                                     k >= 3 : un k-uplet de mots */
  bool recursif;                /**< lire aussi les sous-repertoires */
  hachage_paires_t *bigrammes;  /**< NULL, ou table VIDE qui recoit les
                                     bigrammes ; obligatoire a l'ordre 2 et
                                     avec un lexique */
  lexique_t *lexique;           /**< NULL, ou lexique VIDE qui recoit les
                                     graphies, liaisons et marques */
  markov_t *ponctuation;        /**< NULL, ou chaine VIDE qui recoit la
                                     ponctuation finale de chaque mot */
  hachage_nuplets_t *nuplets;   /**< table VIDE de n-uplets de taille ordre
                                     qui recoit les etats : obligatoire si
                                     ordre >= 3, NULL sinon */
  bool phrase_par_ligne;        /**< chaque fin de ligne termine une phrase
                                     (cf. iterateur_mots_phrase_par_ligne) */
  bool filtrer_langues;         /**< ecarter les passages qui ne sont pas
                                     dans la langue majoritaire */
  repertoire_bilan_t *bilan;    /**< NULL, ou structure qui recoit le bilan
                                     en cas de succes */
  unsigned int ordre_repli;     /**< 0, ou ordre j < ordre de la chaine de
                                     repli, apprise en meme temps */
  markov_t *repli;              /**< chaine VIDE qui recoit la chaine de
                                     repli si ordre_repli >= 1, NULL sinon */
  hachage_nuplets_t *nuplets_repli; /**< table VIDE de n-uplets de taille
                                     ordre_repli qui recoit les etats de
                                     repli si ordre_repli >= 3, NULL sinon */
} repertoire_options_t;

/**
 * @brief  Construit une chaine de Markov a partir des mots des fichiers
 *         d'un corpus (cf. repertoire_lister_corpus pour les fichiers lus
 *         et leur ordre : une meme graine donne toujours le meme texte), et
 *         remplit les structures facultatives des options.
 * @param  chemins     chemins des fichiers et repertoires du corpus.
 * @param  nb_chemins  nombre de chemins.
 * @param  graine      graine du generateur de la chaine.
 * @param  mots        table de hachage VIDE, creee par l'appelant.
 * @param  options     options (cf. repertoire_options_t).
 * @pre    chemins != NULL ; nb_chemins >= 1 ; chaque chemins[i] != NULL ;
 *         mots != NULL ; hachage_nb_mots(mots) == 0 ; options != NULL ;
 *         1 <= options->ordre <= REPERTOIRE_ORDRE_MAX ;
 *         options->bigrammes != NULL si ordre == 2 ou lexique != NULL ;
 *         options->nuplets != NULL si et seulement si ordre >= 3, et alors
 *         hachage_nuplets_taille(options->nuplets) == ordre ;
 *         ordre_repli < ordre ; options->repli != NULL si et seulement si
 *         ordre_repli >= 1 ; options->bigrammes != NULL si
 *         ordre_repli == 2 ; options->nuplets_repli != NULL si et
 *         seulement si ordre_repli >= 3, et alors de taille ordre_repli ;
 *         chaque structure facultative fournie est vide (0 paire, 0
 *         n-uplet, 0 mot, 0 etat) et le lexique n'est pas finalise.
 * @post   en cas de succes, soit m le resultat et n = hachage_nb_mots(mots) :
 *           - markov_est_finalisee(m) ; le mot 0 est le mot vide "" ;
 *           - ordre 1 : markov_nb_etats(m) == n, l'etat i est le mot i ;
 *             ordre 2 : markov_nb_etats(m) == hachage_paires_nb(bigrammes),
 *             l'etat i est la paire i ; la paire 0 est (0, 0) ;
 *             ordre k >= 3 : markov_nb_etats(m) == hachage_nuplets_nb(nuplets),
 *             l'etat i est le n-uplet i ; le n-uplet 0 est (0, ..., 0) ;
 *           - repli (si ordre_repli >= 1) : finalisee, chaine d'ordre
 *             ordre_repli apprise sur les memes phrases (memes regles :
 *             etats = mots, paires ou n-uplets de nuplets_repli) ;
 *           - bigrammes (si fourni) : la paire 0 est (0, 0) ; il contient
 *             chaque (0, w1) de debut de phrase et chaque couple (u, v) de
 *             mots consecutifs d'une meme phrase, et seulement eux ;
 *           - lexique (si fourni) : finalise, n mots, un bigramme par paire
 *             (le bigramme i a pour mot le second element de la paire i) ;
 *             chaque mot i > 0 a une variante choisie ; pour chaque
 *             occurrence "t u v" d'un fichier accentue (t == 0 si u commence
 *             la phrase), la variante de v a vote dans le contexte (t, bigramme (u, v)) ;
 *           - ponctuation (si fournie) : finalisee, REPERTOIRE_NB_FINS + n
 *             etats ; le poids de (REPERTOIRE_NB_FINS + w) -> p est le
 *             nombre de phrases terminees par w avec la ponctuation p. Une
 *             fin sans ponctuation compte comme un point, sauf en lecture
 *             "une phrase par ligne" ou elle est comptee telle quelle (p == PONCTUATION_AUCUNE) ;
 *           - markov_nb_voisins(m, 0) == 0 si et seulement si aucun mot n'a
 *             ete appris ; tout etat i > 0 a au moins un voisin ; si un etat
 *             a pour voisin 0, son dernier mot a au moins un voisin dans la
 *             chaine de ponctuation ; tout mot i > 0 est le dernier mot d'au moins un etat ;
 *           - avec filtrer_langues, les phrases ecartees n'ont laisse
 *             aucune trace (ni mot, ni transition, ni graphie) ;
 *           - bilan (si fourni) : rempli ; nb_phrases_ecartees <= nb_phrases
 *             et nb_mots_ecartes <= nb_mots ; sans filtrer_langues, rien
 *             n'est ecarte et langue == LANGUE_INCONNUE.
 *         En cas d'echec : la chaine n'est pas creee et le bilan n'est pas
 *         modifie ; si la liste des fichiers ne peut pas etre etablie, les
 *         structures fournies sont inchangees, sinon elles peuvent etre
 *         partiellement remplies (l'appelant doit de toute facon les
 *         detruire).
 * @return la chaine (a liberer avec markov_detruire), ou NULL en cas
 *         d'echec, errno indiquant la cause : chemin inexistant (ENOENT),
 *         ni fichier ordinaire ni repertoire (EINVAL, ENOTDIR), droits
 *         insuffisants (EACCES), erreur de lecture (EIO), memoire
 *         insuffisante (ENOMEM), poids trop grand (ERANGE), arborescence
 *         trop profonde (ELOOP).
 * @note   Complexite : O(N + F log F + T log k_max) en moyenne, N etant le
 *         nombre total de caracteres lus (deux fois avec filtrer_langues),
 *         F le nombre de fichiers et T le nombre de transitions distinctes.
 */
markov_t *repertoire_apprendre_corpus(const char *const *chemins,
                                      size_t nb_chemins, unsigned int graine,
                                      hachage_t *mots,
                                      const repertoire_options_t *options);

/**
 * @brief  Construit une chaine de Markov a partir d'un seul chemin, celui
 *         d'un repertoire (ou d'un fichier) : equivaut a
 *         repertoire_apprendre_corpus avec ce seul chemin.
 * @param  chemin_repertoire  chemin relatif ou absolu du repertoire.
 * @param  graine             graine du generateur de la chaine.
 * @param  mots               table de hachage VIDE, creee par l'appelant.
 * @param  options            options (cf. repertoire_options_t).
 * @pre    chemin_repertoire != NULL ; pour le reste, cf. repertoire_apprendre_corpus.
 * @post   cf. repertoire_apprendre_corpus.
 * @return la chaine (a liberer avec markov_detruire), ou NULL en cas
 *         d'echec (errno indique la cause, cf. repertoire_apprendre_corpus).
 */
markov_t *repertoire_apprendre(const char *chemin_repertoire,
                               unsigned int graine, hachage_t *mots,
                               const repertoire_options_t *options);

/**
 * @brief  Construit une chaine de Markov d'ordre 1 a partir des mots de tous
 *         les fichiers d'un repertoire (sans les sous-repertoires) :
 *         equivaut a repertoire_apprendre avec les options
 *         { 1, false, NULL, NULL, NULL, NULL, false, false, NULL }.
 * @param  chemin_repertoire  chemin relatif ou absolu du repertoire.
 * @param  graine             graine du generateur de la chaine.
 * @param  mots               table de hachage VIDE, creee par l'appelant,
 *                            qui recoit les mots rencontres.
 * @pre    chemin_repertoire != NULL ; mots != NULL ;
 *         hachage_nb_mots(mots) == 0.
 * @post   cf. repertoire_apprendre_corpus (ordre 1).
 * @return la chaine (a liberer avec markov_detruire), ou NULL en cas
 *         d'echec (errno indique la cause, cf. repertoire_apprendre_corpus).
 */
markov_t *repertoire_creer_markov(const char *chemin_repertoire,unsigned int graine, hachage_t *mots);

#endif
