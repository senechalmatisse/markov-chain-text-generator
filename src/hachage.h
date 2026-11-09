/**
@file hachage.h
Fichier hachage.h

Table de hachage de mots definis sur l'alphabet {a, ..., z} (le mot vide
est autorise : il sert a representer le debut de phrase), et table de
hachage de paires d'identifiants (fin du fichier).

Chaque mot insere recoit un IDENTIFIANT UNIQUE : 0 pour le premier mot
insere, 1 pour le deuxieme, etc. La table realise donc une bijection entre
l'ensemble des mots stockes et l'intervalle d'entiers [0, nb_mots[, qui est
aussi l'ensemble des etats de la chaine de Markov (le mot d'identifiant i
correspond a l'etat i).

Choix de conception :
  - fonction de hachage (hachage_code) : le mot est lu comme un nombre ecrit
    en base 27 avec les chiffres a = 1, ..., z = 26, calcule modulo 2^64.
    Ce code est INJECTIF sur les mots d'au plus 13 lettres (27^13 < 2^64) :
    deux mots differents de cette longueur n'ont jamais le meme code ;
  - position initiale d'un mot : hachage de Fibonacci (multiplication du
    code par 2^64 / phi, phi etant le nombre d'or, puis conservation des b
    bits de poids fort pour une table de 2^b cases), qui repartit
    uniformement les codes, meme tres proches, dans la table ;
  - collisions : adressage ouvert avec sondage lineaire ; le taux de
    remplissage est maintenu <= 1/2 par doublement automatique de la table
    (en moyenne, moins de 1,5 case examinee pour retrouver un mot present) ;
  - les mots sont recopies dans de grands blocs memoire (peu d'allocations,
    pas de surcout par mot) qui ne sont jamais deplaces : un pointeur
    renvoye par hachage_mot reste valide jusqu'a la destruction de la table ;
  - pas de suppression de mot : elle casserait la bijection mots <-> etats.

Notations utilisees dans les contrats :
  nb_mots(h)   = hachage_nb_mots(h)
  capacite(h)  = hachage_capacite(h)
  valide(mot)  = hachage_mot_valide(mot)
  ancien(x)    = valeur de x avant l'appel
*/
#ifndef __HACHAGE_H__
#define __HACHAGE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/** Type abstrait table de hachage (structure definie dans hachage.c). */
typedef struct hachage_s hachage_t;

/** Identifiant renvoye pour un mot absent ou en cas d'echec d'insertion. */
#define HACHAGE_ABSENT UINT_MAX

/** Position renvoyee pour un mot absent de la table. */
#define HACHAGE_POSITION_ABSENTE SIZE_MAX

/** Nombre maximal de mots dans une table. */
#define HACHAGE_NB_MOTS_MAX (UINT_MAX - 1u)


/* ======================================================================== */
/*                             Fonction de hachage                          */
/* ======================================================================== */

/**
 * @brief  Indique si une chaine est un mot de l'alphabet {a, ..., z}.
 * @param  mot  la chaine a tester.
 * @pre    mot != NULL.
 * @return true si tous les caracteres de mot sont des lettres minuscules
 *         non accentuees 'a' .. 'z' (true pour le mot vide), false sinon.
 * @note   Complexite : O(longueur du mot).
 */
bool hachage_mot_valide(const char *mot);

/**
 * @brief  Fonction de hachage des mots : code du mot en base 27 modulo 2^64.
 *         code(m1 ... mn) = somme_{i=1..n} (mi - 'a' + 1) * 27^(n-i) mod 2^64
 * @param  mot  le mot a hacher.
 * @pre    mot != NULL ; valide(mot).
 * @post   code("") == 0 ; code("a") == 1 ; code("z") == 26 ; code("ba") == 55.
 *         Pour deux mots valides differents d'au plus 13 lettres, les codes
 *         sont differents.
 * @return le code du mot.
 * @note   Complexite : O(longueur du mot).
 */
uint64_t hachage_code(const char *mot);


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

/**
 * @brief  Cree une table de hachage vide.
 * @param  nb_mots_prevus  nombre de mots que l'on prevoit d'inserer (0 si
 *                         inconnu) : la table est dimensionnee pour les
 *                         accueillir sans redimensionnement. La table
 *                         s'agrandit de toute facon automatiquement.
 * @pre    nb_mots_prevus <= HACHAGE_NB_MOTS_MAX.
 * @post   si le resultat h est non NULL : nb_mots(h) == 0 et
 *         capacite(h) >= 2 * nb_mots_prevus.
 * @return la nouvelle table, ou NULL si la memoire manque.
 *         La table doit etre liberee avec hachage_detruire.
 * @note   Complexite : O(capacite).
 */
hachage_t *hachage_creer(unsigned int nb_mots_prevus);

/**
 * @brief  Detruit une table et libere toute la memoire qu'elle utilise
 *         (y compris les mots stockes).
 * @param  h  adresse du pointeur vers la table.
 * @pre    h != NULL (*h peut etre NULL : la fonction ne fait alors rien).
 * @post   toute la memoire de la table est liberee et *h == NULL ; les
 *         pointeurs renvoyes par hachage_mot ne sont plus valides.
 * @note   Complexite : O(1 + taille totale des mots / 64 Kio) (les mots
 *         sont stockes par blocs de 64 Kio).
 */
void hachage_detruire(hachage_t **h);


/* ======================================================================== */
/*                            Ajout et recherche                            */
/* ======================================================================== */

/**
 * @brief  Ajoute un mot a la table (s'il n'y est pas deja) et renvoie son
 *         identifiant unique.
 * @param  h    la table.
 * @param  mot  le mot (il est recopie : l'appelant garde la propriete de
 *              sa chaine).
 * @pre    h != NULL ; mot != NULL ; valide(mot).
 * @post   en cas de succes, soit id le resultat :
 *           - si mot etait deja present, id est son identifiant et la table
 *             est inchangee ;
 *           - sinon id == ancien(nb_mots(h)) et
 *             nb_mots(h) == ancien(nb_mots(h)) + 1 ;
 *           - strcmp(hachage_mot(h, id), mot) == 0 et
 *             hachage_identifiant(h, mot) == id ;
 *           - les identifiants des autres mots, et les pointeurs deja
 *             renvoyes par hachage_mot, sont inchanges ; les POSITIONS
 *             peuvent changer (agrandissement de la table) ;
 *           - nb_mots(h) <= capacite(h) / 2.
 *         En cas d'echec : la table contient exactement les memes mots,
 *         avec les memes identifiants.
 * @return l'identifiant du mot, ou HACHAGE_ABSENT si la memoire manque ou
 *         si la table contient deja HACHAGE_NB_MOTS_MAX mots.
 * @note   Complexite : O(longueur du mot) en moyenne (O(1) amortie pour
 *         les agrandissements).
 */
unsigned int hachage_ajouter(hachage_t *h, const char *mot);

/**
 * @brief  Identifiant unique d'un mot.
 * @pre    h != NULL ; mot != NULL ; valide(mot).
 * @return l'identifiant du mot (< nb_mots(h)), ou HACHAGE_ABSENT si le mot
 *         n'est pas dans la table.
 * @note   Complexite : O(longueur du mot) en moyenne.
 */
unsigned int hachage_identifiant(const hachage_t *h, const char *mot);

/**
 * @brief  Indique si un mot est present dans la table.
 * @pre    h != NULL ; mot != NULL ; valide(mot).
 * @return true si et seulement si hachage_identifiant(h, mot) != HACHAGE_ABSENT.
 * @note   Complexite : O(longueur du mot) en moyenne.
 */
bool hachage_contient(const hachage_t *h, const char *mot);

/**
 * @brief  Position (indice de case) d'un mot dans la table de hachage.
 * @pre    h != NULL ; mot != NULL ; valide(mot).
 * @post   si le mot est present : resultat < capacite(h) et
 *         hachage_identifiant_en_position(h, resultat)
 *           == hachage_identifiant(h, mot) ;
 *         deux mots differents ont des positions differentes.
 *         La position reste valable jusqu'au prochain ajout d'un nouveau mot.
 * @return la position du mot, ou HACHAGE_POSITION_ABSENTE s'il est absent.
 * @note   Complexite : O(longueur du mot) en moyenne.
 */
size_t hachage_position(const hachage_t *h, const char *mot);

/**
 * @brief  Identifiant du mot range dans une case de la table.
 * @pre    h != NULL ; position < capacite(h).
 * @return l'identifiant du mot range a cette position, ou HACHAGE_ABSENT si
 *         la case est libre.
 * @note   Complexite : O(1).
 */
unsigned int hachage_identifiant_en_position(const hachage_t *h, size_t position);

/**
 * @brief  Retrouve un mot a partir de son identifiant unique.
 * @pre    h != NULL ; identifiant < nb_mots(h).
 * @post   resultat != NULL ; valide(resultat) ;
 *         hachage_identifiant(h, resultat) == identifiant.
 * @return le mot, stocke dans la table : il ne doit ni etre modifie ni etre
 *         libere ; le pointeur reste valide jusqu'a hachage_detruire.
 * @note   Complexite : O(1).
 */
const char *hachage_mot(const hachage_t *h, unsigned int identifiant);


/* ======================================================================== */
/*                        Informations et statistiques                      */
/* ======================================================================== */

/**
 * @brief  Nombre de mots stockes.
 * @pre    h != NULL.
 * @return le nombre de mots ; les identifiants sont 0 .. resultat-1.
 * @note   Complexite : O(1).
 */
unsigned int hachage_nb_mots(const hachage_t *h);

/**
 * @brief  Nombre de cases de la table (puissance de 2, au moins 16).
 * @pre    h != NULL.
 * @post   resultat >= 2 * nb_mots(h).
 * @return la capacite.
 * @note   Complexite : O(1).
 */
size_t hachage_capacite(const hachage_t *h);

/**
 * @brief  Nombre de collisions : nombre de mots qui ne sont pas ranges dans
 *         leur case initiale (car elle etait deja occupee).
 * @pre    h != NULL.
 * @post   resultat <= nb_mots(h).
 * @return le nombre de mots deplaces par une collision.
 * @note   Complexite : O(capacite).
 */
size_t hachage_nb_collisions(const hachage_t *h);

/**
 * @brief  Nombre moyen de cases examinees pour retrouver un mot present
 *         (1.0 signifie qu'aucun mot n'a subi de collision).
 * @pre    h != NULL.
 * @post   resultat >= 1.0 si nb_mots(h) > 0 ; resultat == 0.0 sinon.
 * @return le nombre moyen de sondages d'une recherche fructueuse.
 * @note   Complexite : O(capacite).
 */
double hachage_sondage_moyen(const hachage_t *h);


/* ======================================================================== */
/*                 Table de hachage de paires d'identifiants                */
/* ======================================================================== */

/*
Table de hachage de paires (premier, second) d'entiers non signes, par
exemple des couples de mots consecutifs (bigrammes). Chaque paire inseree
recoit un identifiant unique : 0 pour la premiere, 1 pour la deuxieme, etc.
Elle sert aux chaines de Markov d'ordre 2 (un etat = une paire de mots) et
au lexique (graphie et liaison d'un mot selon le mot qui le precede).

Choix de conception :
  - code de la paire : premier * 2^32 + second sur 64 bits (code injectif :
    deux paires differentes n'ont jamais le meme code) ;
  - position initiale : hachage de Fibonacci, comme pour les mots ;
  - collisions : sondage lineaire, taux de remplissage <= 1/2 ;
  - chaque paire occupe 8 octets (son code) plus une case de 4 octets.

Notations : nb_paires(p) = hachage_paires_nb(p).
*/

/** Type abstrait table de paires (structure definie dans hachage.c). */
typedef struct hachage_paires_s hachage_paires_t;

/**
 * @brief  Cree une table de paires vide.
 * @param  nb_paires_prevues  nombre de paires prevues (0 si inconnu).
 * @pre    nb_paires_prevues <= HACHAGE_NB_MOTS_MAX.
 * @post   si le resultat p est non NULL : nb_paires(p) == 0 et
 *         hachage_paires_capacite(p) >= 2 * nb_paires_prevues.
 * @return la nouvelle table (a liberer avec hachage_paires_detruire), ou
 *         NULL si la memoire manque.
 * @note   Complexite : O(capacite).
 */
hachage_paires_t *hachage_paires_creer(unsigned int nb_paires_prevues);

/**
 * @brief  Detruit une table de paires et libere sa memoire.
 * @pre    p != NULL (*p peut etre NULL : rien n'est fait).
 * @post   *p == NULL.
 * @note   Complexite : O(1).
 */
void hachage_paires_detruire(hachage_paires_t **p);

/**
 * @brief  Ajoute la paire (premier, second) si elle est absente et renvoie
 *         son identifiant.
 * @pre    p != NULL.
 * @post   en cas de succes, soit id le resultat :
 *           - si la paire etait presente, id est son identifiant et la table
 *             est inchangee ;
 *           - sinon id == ancien(nb_paires(p)) et
 *             nb_paires(p) == ancien(nb_paires(p)) + 1 ;
 *           - hachage_paires_premier(p, id) == premier et
 *             hachage_paires_second(p, id) == second.
 *         En cas d'echec : la table contient les memes paires, avec les
 *         memes identifiants.
 * @return l'identifiant de la paire, ou HACHAGE_ABSENT si la memoire manque
 *         ou si la table contient deja HACHAGE_NB_MOTS_MAX paires.
 * @note   Complexite : O(1) en moyenne (O(1) amortie pour l'agrandissement).
 */
unsigned int hachage_paires_ajouter(hachage_paires_t *p, unsigned int premier,
                                    unsigned int second);

/**
 * @brief  Identifiant d'une paire.
 * @pre    p != NULL.
 * @return l'identifiant (< nb_paires(p)), ou HACHAGE_ABSENT si la paire est
 *         absente.
 * @note   Complexite : O(1) en moyenne.
 */
unsigned int hachage_paires_identifiant(const hachage_paires_t *p,
                                        unsigned int premier,
                                        unsigned int second);

/**
 * @brief  Premier element de la paire d'identifiant donne.
 * @pre    p != NULL ; identifiant < nb_paires(p).
 * @return le premier element.
 * @note   Complexite : O(1).
 */
unsigned int hachage_paires_premier(const hachage_paires_t *p, unsigned int identifiant);

/**
 * @brief  Second element de la paire d'identifiant donne.
 * @pre    p != NULL ; identifiant < nb_paires(p).
 * @return le second element.
 * @note   Complexite : O(1).
 */
unsigned int hachage_paires_second(const hachage_paires_t *p, unsigned int identifiant);

/**
 * @brief  Nombre de paires stockees.
 * @pre    p != NULL.
 * @return le nombre de paires ; les identifiants sont 0 .. resultat-1.
 * @note   Complexite : O(1).
 */
unsigned int hachage_paires_nb(const hachage_paires_t *p);

/**
 * @brief  Nombre de cases de la table (puissance de 2, au moins 16).
 * @pre    p != NULL.
 * @post   resultat >= 2 * nb_paires(p).
 * @return la capacite.
 * @note   Complexite : O(1).
 */
size_t hachage_paires_capacite(const hachage_paires_t *p);

/**
 * @brief  Nombre moyen de cases examinees pour retrouver une paire presente
 *         (1.0 : aucune collision).
 * @pre    p != NULL.
 * @post   resultat >= 1.0 si nb_paires(p) > 0 ; resultat == 0.0 sinon.
 * @return le nombre moyen de sondages d'une recherche fructueuse.
 * @note   Complexite : O(capacite).
 */
double hachage_paires_sondage_moyen(const hachage_paires_t *p);

#endif
