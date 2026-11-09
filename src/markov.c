/**
Fichier markov.c

Implementation de la chaine de Markov (cf. markov.h).

Representation d'un etat :

  Phase d'apprentissage :
    transitions est une table de hachage a adressage ouvert (sondage
    lineaire) de capacite cases (0, ou une puissance de 2). Une case est
    libre si et seulement si son poids est nul. nb_transitions cases sont
    occupees, avec 4 * nb_transitions <= 3 * capacite (il reste toujours au
    moins une case libre, ce qui garantit l'arret des recherches).

  Phase d'utilisation (apres markov_finaliser) :
    transitions est un tableau compact de nb_transitions cases, triees par
    destination strictement croissante, dont le champ poids contient le
    poids CUMULE :
        transitions[i].poids = somme des poids des transitions 0 .. i
    (fonction de repartition entiere). On a donc
        transitions[nb_transitions - 1].poids == poids_total.

  Dans les deux phases, poids_total est la somme des poids des transitions
  sortantes et nb_transitions leur nombre.
*/
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "contrat.h"
#include "markov.h"

/** Capacite initiale du tableau des etats. */
#define CAPACITE_ETATS_INITIALE 16u

/** Capacite initiale de la table des transitions d'un etat (puissance de 2). */
#define CAPACITE_TRANSITIONS_INITIALE 2u

/** Transition vers un etat d'arrivee (cf. representation ci-dessus). */
typedef struct {
  unsigned int destination;
  unsigned int poids;
} transition_t;

/** Etat de la chaine : ensemble de ses transitions sortantes. */
typedef struct {
  transition_t *transitions;
  unsigned int capacite;
  unsigned int nb_transitions;
  unsigned int poids_total;
} etat_t;

struct markov_s {
  etat_t *etats;          /* etats[0 .. nb_etats-1] */
  unsigned int nb_etats;
  unsigned int capacite;  /* nombre d'etats alloues */
  bool finalisee;
  uint64_t generateur;    /* etat du generateur pseudo-aleatoire */
};


/* ======================================================================== */
/*                     Generateur pseudo-aleatoire                          */
/* ======================================================================== */

/**
 * Generateur SplitMix64 (S. Vigna) : periode 2^64, excellente repartition,
 * deterministe et identique sur toutes les plateformes (contrairement a
 * rand(), dont l'algorithme et RAND_MAX dependent de la bibliotheque C).
 */
static uint64_t generateur_suivant(uint64_t *etat)
{
  uint64_t z;

  *etat += UINT64_C(0x9E3779B97F4A7C15);
  z = *etat;
  z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
  z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
  return z ^ (z >> 31);
}

/**
 * Tire un entier uniformement dans [0, borne[, sans biais : les tirages
 * inferieurs a (2^64 mod borne) sont rejetes, de sorte que les valeurs
 * restantes se repartissent en un nombre entier de blocs de taille borne.
 * Pre : borne > 0.
 */
static uint64_t tirage_uniforme(uint64_t *etat, uint64_t borne)
{
  uint64_t seuil;
  uint64_t tirage;

  PRECONDITION(borne > 0u);
  seuil = (UINT64_C(0) - borne) % borne;
  do {
    tirage = generateur_suivant(etat);
  } while (tirage < seuil);
  return tirage % borne;
}


/* ======================================================================== */
/*               Table des transitions (phase d'apprentissage)              */
/* ======================================================================== */

/**
 * Case initiale d'une destination dans une table de capacite cases
 * (puissance de 2). Le melange (finaliseur de MurmurHash3) repartit les
 * identifiants consecutifs dans toute la table.
 */
static unsigned int case_initiale(unsigned int destination, unsigned int capacite)
{
  uint32_t x = (uint32_t)destination;

  x ^= x >> 16;
  x *= UINT32_C(0x85EBCA6B);
  x ^= x >> 13;
  x *= UINT32_C(0xC2B2AE35);
  x ^= x >> 16;
  return (unsigned int)x & (capacite - 1u);
}

/**
 * Indice de la case contenant la destination, ou, si elle est absente, de
 * la case libre ou elle devrait etre inseree.
 * Pre : etat->capacite > 0 (et au moins une case libre, cf. invariant).
 */
static unsigned int chercher_transition(const etat_t *etat, unsigned int destination)
{
  unsigned int masque = etat->capacite - 1u;
  unsigned int i = case_initiale(destination, etat->capacite);

  while (etat->transitions[i].poids != 0u
         && etat->transitions[i].destination != destination) {
    i = (i + 1u) & masque;
  }
  return i;
}

/**
 * Double la capacite de la table des transitions d'un etat (ou l'alloue si
 * elle est vide) et y replace les transitions.
 * Renvoie false si la memoire manque (l'etat est alors inchange).
 */
static bool agrandir_transitions(etat_t *etat)
{
  unsigned int ancienne_capacite = etat->capacite;
  unsigned int nouvelle_capacite;
  transition_t *anciennes = etat->transitions;
  transition_t *nouvelles;
  unsigned int i;
  unsigned int j;
  bool succes = false;

  if (ancienne_capacite == 0u) {
    nouvelle_capacite = CAPACITE_TRANSITIONS_INITIALE;
  } else {
    nouvelle_capacite = 2u * ancienne_capacite;
  }
  if (nouvelle_capacite > ancienne_capacite) {
    /* calloc : toutes les cases sont libres (poids nul) */
    nouvelles = calloc(nouvelle_capacite, sizeof(transition_t));
    if (nouvelles != NULL) {
      etat->transitions = nouvelles;
      etat->capacite = nouvelle_capacite;
      for (i = 0u; i < ancienne_capacite; i++) {
        if (anciennes[i].poids != 0u) {
          j = chercher_transition(etat, anciennes[i].destination);
          etat->transitions[j] = anciennes[i];
        }
      }
      free(anciennes);
      succes = true;
    }
  }
  return succes;
}


/* ======================================================================== */
/*               Tableau compact (phase d'utilisation)                      */
/* ======================================================================== */

/** Comparaison de deux transitions par destination croissante (qsort). */
static int comparer_transitions(const void *a, const void *b)
{
  unsigned int da = ((const transition_t *)a)->destination;
  unsigned int db = ((const transition_t *)b)->destination;

  return (da > db) - (da < db);
}

/**
 * Indice de la transition vers destination dans le tableau compact d'un
 * etat finalise (recherche dichotomique), ou nb_transitions si absente.
 */
static unsigned int rechercher_destination(const etat_t *etat, unsigned int destination)
{
  unsigned int bas = 0u;
  unsigned int haut = etat->nb_transitions;
  unsigned int milieu;

  while (bas < haut) {
    milieu = bas + ((haut - bas) / 2u);
    if (etat->transitions[milieu].destination < destination) {
      bas = milieu + 1u;
    } else {
      haut = milieu;
    }
  }
  if (bas < etat->nb_transitions
      && etat->transitions[bas].destination != destination) {
    bas = etat->nb_transitions;
  }
  return bas;
}

/** Poids (non cumule) de la i-eme transition d'un etat finalise. */
static unsigned int poids_finalise(const etat_t *etat, unsigned int i)
{
  unsigned int poids = etat->transitions[i].poids;

  if (i > 0u) {
    poids -= etat->transitions[i - 1u].poids;
  }
  return poids;
}

/**
 * Transforme la table de hachage d'un etat en tableau compact trie par
 * destination, contenant les poids cumules. Ne peut pas echouer.
 */
static void finaliser_etat(etat_t *etat)
{
  unsigned int nb = 0u;
  unsigned int i;
  transition_t *compact;

  /* 1. Compactage en place des cases occupees (nb <= i a tout moment) */
  for (i = 0u; i < etat->capacite; i++) {
    if (etat->transitions[i].poids != 0u) {
      etat->transitions[nb] = etat->transitions[i];
      nb++;
    }
  }
  INVARIANT(nb == etat->nb_transitions);

  if (nb == 0u) {
    free(etat->transitions);
    etat->transitions = NULL;
  } else {
    /* 2. Tri par destination (recherche dichotomique ensuite) */
    qsort(etat->transitions, nb, sizeof(transition_t), comparer_transitions);
    /* 3. Poids -> poids cumules (fonction de repartition) */
    for (i = 1u; i < nb; i++) {
      etat->transitions[i].poids += etat->transitions[i - 1u].poids;
    }
    POSTCONDITION(etat->transitions[nb - 1u].poids == etat->poids_total);
    /* 4. Liberation des cases inutiles (si realloc echoue, le bloc garde
          simplement sa taille d'origine) */
    compact = realloc(etat->transitions, nb * sizeof(transition_t));
    if (compact != NULL) {
      etat->transitions = compact;
    }
  }
  etat->capacite = nb;
}


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

markov_t *markov_creer(unsigned int graine)
{
  markov_t *m = malloc(sizeof(markov_t));

  if (m != NULL) {
    m->etats = NULL;
    m->nb_etats = 0u;
    m->capacite = 0u;
    m->finalisee = false;
    m->generateur = (uint64_t)graine;
    POSTCONDITION(markov_nb_etats(m) == 0u && !markov_est_finalisee(m));
  }
  return m;
}

void markov_detruire(markov_t **m)
{
  unsigned int e;

  PRECONDITION(m != NULL);
  if (*m != NULL) {
    for (e = 0u; e < (*m)->nb_etats; e++) {
      free((*m)->etats[e].transitions);
    }
    free((*m)->etats);
    free(*m);
    *m = NULL;
  }
  POSTCONDITION(*m == NULL);
}


/* ======================================================================== */
/*                          Phase d'apprentissage                           */
/* ======================================================================== */

/** Double la capacite du tableau des etats. Renvoie false si echec. */
static bool agrandir_etats(markov_t *m)
{
  size_t nouvelle_capacite;
  etat_t *etats;
  bool succes = false;

  if (m->capacite == 0u) {
    nouvelle_capacite = CAPACITE_ETATS_INITIALE;
  } else {
    nouvelle_capacite = 2u * (size_t)m->capacite;
  }
  if (nouvelle_capacite > MARKOV_NB_ETATS_MAX) {
    nouvelle_capacite = MARKOV_NB_ETATS_MAX;
  }
  if (nouvelle_capacite > m->capacite
      && nouvelle_capacite <= SIZE_MAX / sizeof(etat_t)) {
    etats = realloc(m->etats, nouvelle_capacite * sizeof(etat_t));
    if (etats != NULL) {
      m->etats = etats;
      m->capacite = (unsigned int)nouvelle_capacite;
      succes = true;
    }
  }
  if (!succes) {
    errno = ENOMEM;
  }
  return succes;
}

unsigned int markov_ajouter_etat(markov_t *m)
{
  unsigned int identifiant = MARKOV_ETAT_INVALIDE;
  bool place_disponible;

  PRECONDITION(m != NULL);
  PRECONDITION(!m->finalisee);

  if (m->nb_etats >= MARKOV_NB_ETATS_MAX) {
    errno = ERANGE;
  } else {
    place_disponible = (m->nb_etats < m->capacite);
    if (!place_disponible) {
      place_disponible = agrandir_etats(m);
    }
    if (place_disponible) {
      identifiant = m->nb_etats;
      m->etats[identifiant].transitions = NULL;
      m->etats[identifiant].capacite = 0u;
      m->etats[identifiant].nb_transitions = 0u;
      m->etats[identifiant].poids_total = 0u;
      m->nb_etats++;
      POSTCONDITION(markov_nb_voisins(m, identifiant) == 0u);
      POSTCONDITION(markov_poids_total(m, identifiant) == 0u);
    }
  }
  return identifiant;
}

bool markov_augmenter_poids(markov_t *m, unsigned int depart,
                            unsigned int arrivee, unsigned int increment)
{
  etat_t *etat;
  unsigned int i = 0u;
  bool trouvee = false;
  bool succes = true;

  PRECONDITION(m != NULL);
  PRECONDITION(!m->finalisee);
  PRECONDITION(depart < m->nb_etats);
  PRECONDITION(arrivee < m->nb_etats);
  PRECONDITION(increment > 0u);

  etat = &m->etats[depart];
  if (etat->poids_total > MARKOV_POIDS_MAX - increment) {
    /* depassement de capacite : la chaine est inchangee */
    errno = ERANGE;
    succes = false;
  } else {
    if (etat->capacite > 0u) {
      i = chercher_transition(etat, arrivee);
      trouvee = (etat->transitions[i].poids != 0u);
    }
    if (!trouvee) {
      /* nouvelle transition : agrandir si le taux depasserait 3/4 */
      if (4u * ((uint64_t)etat->nb_transitions + 1u)
          > 3u * (uint64_t)etat->capacite) {
        succes = agrandir_transitions(etat);
        if (!succes) {
          errno = ENOMEM;
        }
      }
      if (succes) {
        i = chercher_transition(etat, arrivee);
        etat->transitions[i].destination = arrivee;
        etat->transitions[i].poids = 0u;
        etat->nb_transitions++;
      }
    }
    if (succes) {
      etat->transitions[i].poids += increment;
      etat->poids_total += increment;
    }
  }
  INVARIANT(etat->capacite == 0u
            || 4u * (uint64_t)etat->nb_transitions
               <= 3u * (uint64_t)etat->capacite);
  POSTCONDITION(!succes || markov_poids(m, depart, arrivee) >= increment);
  return succes;
}

void markov_finaliser(markov_t *m)
{
  unsigned int e;
  etat_t *etats;

  PRECONDITION(m != NULL);
  PRECONDITION(!m->finalisee);

  for (e = 0u; e < m->nb_etats; e++) {
    finaliser_etat(&m->etats[e]);
  }
  /* liberation des etats alloues mais inutilises */
  if (m->nb_etats > 0u && m->nb_etats < m->capacite) {
    etats = realloc(m->etats, m->nb_etats * sizeof(etat_t));
    if (etats != NULL) {
      m->etats = etats;
      m->capacite = m->nb_etats;
    }
  }
  m->finalisee = true;
  POSTCONDITION(markov_est_finalisee(m));
}


/* ======================================================================== */
/*                               Consultation                               */
/* ======================================================================== */

unsigned int markov_nb_etats(const markov_t *m)
{
  PRECONDITION(m != NULL);
  return m->nb_etats;
}

bool markov_est_finalisee(const markov_t *m)
{
  PRECONDITION(m != NULL);
  return m->finalisee;
}

unsigned int markov_poids(const markov_t *m, unsigned int depart, unsigned int arrivee)
{
  const etat_t *etat;
  unsigned int poids = 0u;
  unsigned int i;

  PRECONDITION(m != NULL);
  PRECONDITION(depart < m->nb_etats);
  PRECONDITION(arrivee < m->nb_etats);

  etat = &m->etats[depart];
  if (etat->nb_transitions > 0u) {
    if (m->finalisee) {
      i = rechercher_destination(etat, arrivee);
      if (i < etat->nb_transitions) {
        poids = poids_finalise(etat, i);
      }
    } else {
      i = chercher_transition(etat, arrivee);
      poids = etat->transitions[i].poids;
    }
  }
  POSTCONDITION(poids <= etat->poids_total);
  return poids;
}

unsigned int markov_poids_total(const markov_t *m, unsigned int depart)
{
  PRECONDITION(m != NULL);
  PRECONDITION(depart < m->nb_etats);
  return m->etats[depart].poids_total;
}

unsigned int markov_nb_voisins(const markov_t *m, unsigned int depart)
{
  PRECONDITION(m != NULL);
  PRECONDITION(depart < m->nb_etats);
  return m->etats[depart].nb_transitions;
}

unsigned int markov_voisin(const markov_t *m, unsigned int depart,
                           unsigned int rang, unsigned int *poids)
{
  const etat_t *etat;

  PRECONDITION(m != NULL);
  PRECONDITION(m->finalisee);
  PRECONDITION(depart < m->nb_etats);
  PRECONDITION(rang < m->etats[depart].nb_transitions);
  PRECONDITION(poids != NULL);

  /* tableau compact trie par destination : le rang est un indice */
  etat = &m->etats[depart];
  *poids = poids_finalise(etat, rang);
  POSTCONDITION(*poids > 0u);
  POSTCONDITION(rang == 0u
                || etat->transitions[rang - 1u].destination
                   < etat->transitions[rang].destination);
  return etat->transitions[rang].destination;
}


/* ======================================================================== */
/*                           Phase d'utilisation                            */
/* ======================================================================== */

double markov_probabilite(const markov_t *m, unsigned int depart,
                          unsigned int arrivee)
{
  const etat_t *etat;
  double probabilite = 0.0;

  PRECONDITION(m != NULL);
  PRECONDITION(m->finalisee);
  PRECONDITION(depart < m->nb_etats);
  PRECONDITION(arrivee < m->nb_etats);

  etat = &m->etats[depart];
  if (etat->poids_total > 0u) {
    probabilite = (double)markov_poids(m, depart, arrivee)
                  / (double)etat->poids_total;
  }
  POSTCONDITION(probabilite >= 0.0 && probabilite <= 1.0);
  return probabilite;
}

unsigned int markov_deplacer(markov_t *m, unsigned int depart)
{
  const etat_t *etat;
  uint64_t tirage;
  unsigned int bas = 0u;
  unsigned int haut;
  unsigned int milieu;

  PRECONDITION(m != NULL);
  PRECONDITION(m->finalisee);
  PRECONDITION(depart < m->nb_etats);
  PRECONDITION(m->etats[depart].nb_transitions > 0u);

  etat = &m->etats[depart];
  /* tirage uniforme dans [0, poids_total[ puis recherche dichotomique de la
     premiere transition dont le poids cumule depasse le tirage : la
     transition i est choisie avec la probabilite
     (cumul[i] - cumul[i-1]) / poids_total = pi(depart, destination_i) */
  tirage = tirage_uniforme(&m->generateur, (uint64_t)etat->poids_total);
  haut = etat->nb_transitions - 1u;
  while (bas < haut) {
    milieu = bas + ((haut - bas) / 2u);
    if ((uint64_t)etat->transitions[milieu].poids > tirage) {
      haut = milieu;
    } else {
      bas = milieu + 1u;
    }
  }
  POSTCONDITION((uint64_t)etat->transitions[bas].poids > tirage);
  POSTCONDITION(bas == 0u
                || (uint64_t)etat->transitions[bas - 1u].poids <= tirage);
  return etat->transitions[bas].destination;
}
