/**
@file markov.h
Fichier markov.h

Chaine de Markov dont les etats sont des entiers 0, 1, 2, ... (numerotes
dans l'ordre de leur creation). Soit E l'ensemble des etats.

La chaine s'utilise en DEUX PHASES :

  1. Phase d'APPRENTISSAGE (juste apres markov_creer) :
     on ajoute des etats et on augmente les poids des transitions
     (poids : E x E -> N, poids nul = pas de transition).

  2. Phase d'UTILISATION (apres markov_finaliser) :
     les poids sont figes et transformes en loi de probabilite :
         pi(e1, e2) = poids(e1, e2) / somme_{e dans E} poids(e1, e)
     et on peut se deplacer aleatoirement d'un etat vers l'un de ses
     voisins (etats e2 tels que poids(e1, e2) > 0) selon cette loi.

Choix de representation (efficacite en temps et en espace) :
  - seules les transitions de poids non nul sont stockees (matrice creuse) :
    l'espace occupe est en O(|E| + T), T etant le nombre de transitions
    distinctes, et non en O(|E|^2) comme avec une matrice pleine ;
  - en apprentissage, chaque etat possede une table de hachage (adressage
    ouvert) de ses successeurs : augmenter un poids coute O(1) en moyenne ;
  - la finalisation compacte chaque table en un tableau trie par etat
    d'arrivee contenant les poids cumules (fonction de repartition) :
    probabilite et deplacement coutent O(log k), k etant le nombre de
    voisins de l'etat de depart. Le tirage est exact (calcul entier) ;
  - chaque chaine possede son propre generateur pseudo-aleatoire
    (SplitMix64) : une meme graine donne toujours la meme suite de
    deplacements, quelle que soit la plateforme.

Notations utilisees dans les contrats :
  nb_etats(m)        = markov_nb_etats(m)
  poids(m, e1, e2)   = markov_poids(m, e1, e2)
  total(m, e)        = markov_poids_total(m, e)
  voisins(m, e)      = markov_nb_voisins(m, e)
  finalisee(m)       = markov_est_finalisee(m)
  ancien(x)          = valeur de x avant l'appel
*/
#ifndef __MARKOV_H__
#define __MARKOV_H__

#include <stdbool.h>
#include <limits.h>

/** Type abstrait chaine de Markov (structure definie dans markov.c). */
typedef struct markov_s markov_t;

/** Identifiant renvoye par markov_ajouter_etat en cas d'echec. */
#define MARKOV_ETAT_INVALIDE UINT_MAX

/** Nombre maximal d'etats d'une chaine. */
#define MARKOV_NB_ETATS_MAX (UINT_MAX - 1u)

/** Valeur maximale du poids total sortant d'un etat. */
#define MARKOV_POIDS_MAX UINT_MAX


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

/**
 * @brief  Cree une chaine de Markov vide, en phase d'apprentissage.
 * @param  graine  graine du generateur pseudo-aleatoire de la chaine.
 * @pre    aucune.
 * @post   si le resultat r est non NULL : nb_etats(r) == 0,
 *         !finalisee(r), et le generateur de r est initialise avec graine :
 *         deux chaines creees avec la meme graine, puis construites et
 *         utilisees par la meme suite d'appels, effectuent exactement les
 *         memes deplacements.
 * @return la nouvelle chaine, ou NULL si la memoire manque.
 *         La chaine doit etre liberee avec markov_detruire.
 * @note   Complexite : O(1).
 */
markov_t *markov_creer(unsigned int graine);

/**
 * @brief  Detruit une chaine et libere toute la memoire qu'elle utilise.
 * @param  m  adresse du pointeur vers la chaine.
 * @pre    m != NULL (*m peut etre NULL : la fonction ne fait alors rien).
 * @post   toute la memoire de la chaine est liberee et *m == NULL.
 * @note   Complexite : O(nb_etats).
 */
void markov_detruire(markov_t **m);


/* ======================================================================== */
/*                          Phase d'apprentissage                           */
/* ======================================================================== */

/**
 * @brief  Ajoute un nouvel etat, sans transition, a la chaine.
 * @param  m  la chaine.
 * @pre    m != NULL ; !finalisee(m).
 * @post   en cas de succes : le resultat vaut ancien(nb_etats(m)),
 *         nb_etats(m) == ancien(nb_etats(m)) + 1,
 *         voisins(m, resultat) == 0 et total(m, resultat) == 0.
 *         En cas d'echec : la chaine est inchangee.
 * @return l'identifiant du nouvel etat, ou MARKOV_ETAT_INVALIDE si la
 *         memoire manque ou si la chaine contient deja MARKOV_NB_ETATS_MAX
 *         etats.
 * @note   Complexite : O(1) amortie.
 */
unsigned int markov_ajouter_etat(markov_t *m);

/**
 * @brief  Augmente le poids de la transition depart -> arrivee.
 * @param  m          la chaine.
 * @param  depart     etat de depart.
 * @param  arrivee    etat d'arrivee (depart == arrivee est autorise).
 * @param  increment  valeur ajoutee au poids.
 * @pre    m != NULL ; !finalisee(m) ;
 *         depart < nb_etats(m) ; arrivee < nb_etats(m) ; increment > 0.
 * @post   en cas de succes :
 *           poids(m, depart, arrivee) == ancien(poids(m, depart, arrivee))
 *                                        + increment,
 *           total(m, depart) == ancien(total(m, depart)) + increment,
 *           voisins(m, depart) == ancien(voisins(m, depart)) + 1 si
 *           l'ancien poids etait nul, inchange sinon ;
 *           tous les autres poids sont inchanges.
 *         En cas d'echec : la chaine est inchangee.
 * @return true en cas de succes ; false si la memoire manque ou si le
 *         poids total sortant de depart depasserait MARKOV_POIDS_MAX.
 * @note   Complexite : O(1) en moyenne (O(1) amortie pour l'allocation).
 */
bool markov_augmenter_poids(markov_t *m, unsigned int depart,
                            unsigned int arrivee, unsigned int increment);

/**
 * @brief  Termine l'apprentissage : transforme les poids en loi de
 *         probabilite (fonction de repartition de chaque etat) et compacte
 *         la memoire.
 * @param  m  la chaine.
 * @pre    m != NULL ; !finalisee(m).
 * @post   finalisee(m) ; nb_etats, poids, totaux et nombres de voisins sont
 *         inchanges ; markov_probabilite et markov_deplacer sont utilisables.
 *         Aucune allocation n'est necessaire : la fonction ne peut pas
 *         echouer.
 * @note   Complexite : O(nb_etats + T log k_max), T etant le nombre de
 *         transitions et k_max le nombre maximal de voisins d'un etat.
 */
void markov_finaliser(markov_t *m);


/* ======================================================================== */
/*                    Consultation (dans les deux phases)                   */
/* ======================================================================== */

/**
 * @brief  Nombre d'etats de la chaine.
 * @pre    m != NULL.
 * @return le nombre d'etats ; les etats sont les entiers 0 .. resultat-1.
 * @note   Complexite : O(1).
 */
unsigned int markov_nb_etats(const markov_t *m);

/**
 * @brief  Indique si la chaine a ete finalisee (phase d'utilisation).
 * @pre    m != NULL.
 * @return true apres markov_finaliser, false avant.
 * @note   Complexite : O(1).
 */
bool markov_est_finalisee(const markov_t *m);

/**
 * @brief  Poids de la transition depart -> arrivee.
 * @pre    m != NULL ; depart < nb_etats(m) ; arrivee < nb_etats(m).
 * @return le poids (0 s'il n'y a pas de transition).
 * @note   Complexite : O(1) en moyenne avant finalisation, O(log k) apres.
 */
unsigned int markov_poids(const markov_t *m, unsigned int depart, unsigned int arrivee);

/**
 * @brief  Poids total sortant d'un etat : somme_{e dans E} poids(depart, e).
 * @pre    m != NULL ; depart < nb_etats(m).
 * @return le poids total (0 si l'etat n'a aucun voisin).
 * @note   Complexite : O(1).
 */
unsigned int markov_poids_total(const markov_t *m, unsigned int depart);

/**
 * @brief  Nombre de voisins d'un etat : nombre d'etats e tels que
 *         poids(depart, e) > 0.
 * @pre    m != NULL ; depart < nb_etats(m).
 * @return le nombre de voisins.
 * @note   Complexite : O(1).
 */
unsigned int markov_nb_voisins(const markov_t *m, unsigned int depart);


/* ======================================================================== */
/*                           Phase d'utilisation                            */
/* ======================================================================== */

/**
 * @brief  Voisin de rang donne d'un etat : permet d'enumerer les
 *         transitions (par exemple pour sauvegarder la chaine). Les voisins
 *         sont ranges par identifiant strictement croissant.
 * @param  m       la chaine.
 * @param  depart  etat de depart.
 * @param  rang    rang du voisin, de 0 a voisins(m, depart) - 1.
 * @param  poids   recoit le poids de la transition.
 * @pre    m != NULL ; finalisee(m) ; depart < nb_etats(m) ;
 *         rang < voisins(m, depart) ; poids != NULL.
 * @post   resultat < nb_etats(m) ; *poids == poids(m, depart, resultat) > 0 ;
 *         si rang > 0, le voisin de rang (rang - 1) est strictement
 *         inferieur au resultat.
 * @return l'identifiant du voisin.
 * @note   Complexite : O(1).
 */
unsigned int markov_voisin(const markov_t *m, unsigned int depart,
                           unsigned int rang, unsigned int *poids);

/**
 * @brief  Probabilite de passer de l'etat depart a l'etat arrivee.
 * @pre    m != NULL ; finalisee(m) ;
 *         depart < nb_etats(m) ; arrivee < nb_etats(m).
 * @post   0 <= resultat <= 1 ;
 *         si total(m, depart) > 0 :
 *           resultat == poids(m, depart, arrivee) / total(m, depart)
 *           et la somme des probabilites sur tous les etats d'arrivee vaut 1 ;
 *         si total(m, depart) == 0 (aucun voisin) : resultat == 0.
 * @return pi(depart, arrivee).
 * @note   Complexite : O(log k), k = voisins(m, depart).
 */
double markov_probabilite(const markov_t *m, unsigned int depart, unsigned int arrivee);

/**
 * @brief  Se deplace aleatoirement depuis l'etat depart vers l'un de ses
 *         voisins, choisi selon les probabilites pi(depart, .).
 * @pre    m != NULL ; finalisee(m) ; depart < nb_etats(m) ;
 *         voisins(m, depart) > 0.
 * @post   le resultat e verifie e < nb_etats(m) et poids(m, depart, e) > 0 ;
 *         chaque voisin e est choisi avec la probabilite pi(depart, e)
 *         (tirage exact, sans biais) ; le generateur de m a avance.
 * @return l'etat d'arrivee.
 * @note   Complexite : O(log k), k = voisins(m, depart).
 */
unsigned int markov_deplacer(markov_t *m, unsigned int depart);

#endif
