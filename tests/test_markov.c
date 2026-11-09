/**
Fichier test_markov.c

Tests unitaires du module markov (cf. src/markov.h).
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "outils_tests.h"
#include "../src/markov.h"

/** Precision des comparaisons de probabilites. */
#define EPSILON 1e-12

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/** Cree une chaine de nb_etats etats sans transition. */
static markov_t *chaine_avec_etats(unsigned int nb_etats, unsigned int graine)
{
  markov_t *m = markov_creer(graine);
  unsigned int i;

  assert(m != NULL);
  for (i = 0u; i < nb_etats; i++) {
    assert(markov_ajouter_etat(m) == i);
  }
  return m;
}

/** Generateur deterministe (xorshift) pour construire des graphes de test. */
static uint64_t aleatoire(uint64_t *etat)
{
  *etat ^= *etat << 13;
  *etat ^= *etat >> 7;
  *etat ^= *etat << 17;
  return *etat;
}

/** Nombre d'etats et de transitions du graphe aleatoire de test. */
#define NB_ETATS_GRAPHE 150u
#define NB_AUGMENTATIONS_GRAPHE 6000u

/**
 * Construit un graphe aleatoire et memorise les poids attendus dans
 * attendus[depart * NB_ETATS_GRAPHE + arrivee].
 */
static markov_t *graphe_aleatoire(unsigned int *attendus, unsigned int graine)
{
  markov_t *m = chaine_avec_etats(NB_ETATS_GRAPHE, graine);
  uint64_t etat = 88172645463325252u;
  unsigned int i;
  unsigned int depart;
  unsigned int arrivee;
  unsigned int increment;

  for (i = 0u; i < NB_ETATS_GRAPHE * NB_ETATS_GRAPHE; i++) {
    attendus[i] = 0u;
  }
  for (i = 0u; i < NB_AUGMENTATIONS_GRAPHE; i++) {
    /* les premiers etats sont plus souvent choisis (graphe irregulier) */
    depart = (unsigned int)(aleatoire(&etat) % NB_ETATS_GRAPHE);
    depart = depart % (1u + (unsigned int)(aleatoire(&etat) % NB_ETATS_GRAPHE));
    arrivee = (unsigned int)(aleatoire(&etat) % NB_ETATS_GRAPHE);
    increment = 1u + (unsigned int)(aleatoire(&etat) % 5u);
    assert(markov_augmenter_poids(m, depart, arrivee, increment));
    attendus[(depart * NB_ETATS_GRAPHE) + arrivee] += increment;
  }
  return m;
}

/** Verifie que la chaine contient exactement les poids attendus. */
static void verifier_poids(const markov_t *m, const unsigned int *attendus)
{
  unsigned int depart;
  unsigned int arrivee;
  unsigned int total;
  unsigned int voisins;
  unsigned int poids;

  for (depart = 0u; depart < NB_ETATS_GRAPHE; depart++) {
    total = 0u;
    voisins = 0u;
    for (arrivee = 0u; arrivee < NB_ETATS_GRAPHE; arrivee++) {
      poids = attendus[(depart * NB_ETATS_GRAPHE) + arrivee];
      assert(markov_poids(m, depart, arrivee) == poids);
      total += poids;
      voisins += (poids > 0u) ? 1u : 0u;
    }
    assert(markov_poids_total(m, depart) == total);
    assert(markov_nb_voisins(m, depart) == voisins);
  }
}


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

static void test_creer_detruire(void)
{
  markov_t *m = markov_creer(42u);

  assert(m != NULL);
  assert(markov_nb_etats(m) == 0u);
  assert(!markov_est_finalisee(m));
  markov_detruire(&m);
  assert(m == NULL);
  /* detruire un pointeur deja nul est sans effet */
  markov_detruire(&m);
  assert(m == NULL);
}

static void test_ajouter_etats(void)
{
  markov_t *m = markov_creer(1u);
  unsigned int i;

  for (i = 0u; i < 5000u; i++) {
    assert(markov_ajouter_etat(m) == i);
    assert(markov_nb_etats(m) == i + 1u);
    assert(markov_nb_voisins(m, i) == 0u);
    assert(markov_poids_total(m, i) == 0u);
  }
  markov_detruire(&m);
}


/* ======================================================================== */
/*                          Phase d'apprentissage                           */
/* ======================================================================== */

static void test_augmenter_poids(void)
{
  markov_t *m = chaine_avec_etats(4u, 1u);

  assert(markov_poids(m, 0u, 1u) == 0u);
  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  assert(markov_poids(m, 0u, 1u) == 1u);
  assert(markov_augmenter_poids(m, 0u, 1u, 2u));
  assert(markov_poids(m, 0u, 1u) == 3u);
  assert(markov_augmenter_poids(m, 0u, 2u, 5u));
  /* boucle sur un etat */
  assert(markov_augmenter_poids(m, 0u, 0u, 1u));
  assert(markov_poids(m, 0u, 0u) == 1u);

  assert(markov_poids_total(m, 0u) == 9u);
  assert(markov_nb_voisins(m, 0u) == 3u);
  assert(markov_poids(m, 0u, 3u) == 0u);
  /* les transitions sont orientees */
  assert(markov_poids(m, 1u, 0u) == 0u);
  assert(markov_nb_voisins(m, 1u) == 0u);
  assert(markov_poids_total(m, 1u) == 0u);
  markov_detruire(&m);
}

static void test_nombreux_voisins(void)
{
  const unsigned int nb = 20000u;
  markov_t *m = chaine_avec_etats(nb, 3u);
  unsigned int e;
  unsigned long long total = 0u;

  /* un etat relie a tous les autres : nombreux agrandissements */
  for (e = 0u; e < nb; e++) {
    assert(markov_augmenter_poids(m, 7u, e, (e % 7u) + 1u));
    total += (e % 7u) + 1u;
  }
  for (e = 0u; e < nb; e++) {
    assert(markov_poids(m, 7u, e) == (e % 7u) + 1u);
  }
  assert(markov_nb_voisins(m, 7u) == nb);
  assert(markov_poids_total(m, 7u) == total);
  markov_finaliser(m);
  for (e = 0u; e < nb; e++) {
    assert(markov_poids(m, 7u, e) == (e % 7u) + 1u);
  }
  assert(markov_nb_voisins(m, 7u) == nb);
  markov_detruire(&m);
}

static void test_depassement_poids(void)
{
  markov_t *m = chaine_avec_etats(3u, 1u);

  assert(markov_augmenter_poids(m, 0u, 1u, MARKOV_POIDS_MAX - 1u));
  assert(markov_augmenter_poids(m, 0u, 2u, 1u));
  assert(markov_poids_total(m, 0u) == MARKOV_POIDS_MAX);
  /* le total depasserait MARKOV_POIDS_MAX : echec, chaine inchangee */
  assert(!markov_augmenter_poids(m, 0u, 1u, 1u));
  assert(!markov_augmenter_poids(m, 0u, 0u, 1u));
  assert(markov_poids(m, 0u, 1u) == MARKOV_POIDS_MAX - 1u);
  assert(markov_poids(m, 0u, 0u) == 0u);
  assert(markov_nb_voisins(m, 0u) == 2u);
  /* les autres etats ne sont pas concernes */
  assert(markov_augmenter_poids(m, 1u, 0u, MARKOV_POIDS_MAX));
  markov_finaliser(m);
  assert(markov_poids(m, 0u, 1u) == MARKOV_POIDS_MAX - 1u);
  assert(markov_poids(m, 0u, 2u) == 1u);
  assert(ecart(markov_probabilite(m, 1u, 0u), 1.0) < EPSILON);
  markov_detruire(&m);
}


/* ======================================================================== */
/*                               Finalisation                               */
/* ======================================================================== */

static void test_finaliser_conserve_poids(void)
{
  unsigned int *attendus = malloc(NB_ETATS_GRAPHE * NB_ETATS_GRAPHE * sizeof(unsigned int));
  markov_t *m;

  assert(attendus != NULL);
  m = graphe_aleatoire(attendus, 5u);
  verifier_poids(m, attendus);
  markov_finaliser(m);
  assert(markov_est_finalisee(m));
  assert(markov_nb_etats(m) == NB_ETATS_GRAPHE);
  verifier_poids(m, attendus);
  markov_detruire(&m);
  free(attendus);
}

static void test_finaliser_chaine_vide(void)
{
  markov_t *m = markov_creer(0u);

  markov_finaliser(m);
  assert(markov_est_finalisee(m));
  assert(markov_nb_etats(m) == 0u);
  markov_detruire(&m);
}


/* ======================================================================== */
/*                               Probabilites                               */
/* ======================================================================== */

static void test_probabilites(void)
{
  markov_t *m = chaine_avec_etats(4u, 1u);

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  assert(markov_augmenter_poids(m, 0u, 2u, 3u));
  assert(markov_augmenter_poids(m, 0u, 3u, 6u));
  assert(markov_augmenter_poids(m, 2u, 2u, 4u));
  markov_finaliser(m);

  assert(ecart(markov_probabilite(m, 0u, 1u), 0.1) < EPSILON);
  assert(ecart(markov_probabilite(m, 0u, 2u), 0.3) < EPSILON);
  assert(ecart(markov_probabilite(m, 0u, 3u), 0.6) < EPSILON);
  assert(markov_probabilite(m, 0u, 0u) == 0.0);
  assert(markov_probabilite(m, 2u, 2u) == 1.0);
  /* un etat sans voisin a une probabilite nulle vers tous les etats */
  assert(markov_probabilite(m, 1u, 0u) == 0.0);
  assert(markov_probabilite(m, 1u, 1u) == 0.0);
  markov_detruire(&m);
}

static void test_somme_probabilites(void)
{
  unsigned int *attendus = malloc(NB_ETATS_GRAPHE * NB_ETATS_GRAPHE * sizeof(unsigned int));
  markov_t *m;
  unsigned int depart;
  unsigned int arrivee;
  double somme;
  double attendue;

  assert(attendus != NULL);
  m = graphe_aleatoire(attendus, 9u);
  markov_finaliser(m);
  for (depart = 0u; depart < NB_ETATS_GRAPHE; depart++) {
    somme = 0.0;
    for (arrivee = 0u; arrivee < NB_ETATS_GRAPHE; arrivee++) {
      attendue = 0.0;
      if (markov_poids_total(m, depart) > 0u) {
        attendue = (double)attendus[(depart * NB_ETATS_GRAPHE) + arrivee]
                   / (double)markov_poids_total(m, depart);
      }
      assert(ecart(markov_probabilite(m, depart, arrivee), attendue) < EPSILON);
      somme += markov_probabilite(m, depart, arrivee);
    }
    if (markov_nb_voisins(m, depart) > 0u) {
      assert(ecart(somme, 1.0) < 1e-9);
    } else {
      assert(somme == 0.0);
    }
  }
  markov_detruire(&m);
  free(attendus);
}


/* ======================================================================== */
/*                                Deplacements                              */
/* ======================================================================== */

static void test_deplacer_vers_un_voisin(void)
{
  unsigned int *attendus = malloc(NB_ETATS_GRAPHE * NB_ETATS_GRAPHE * sizeof(unsigned int));
  markov_t *m;
  unsigned int depart;
  unsigned int arrivee;
  unsigned int i;

  assert(attendus != NULL);
  m = graphe_aleatoire(attendus, 11u);
  markov_finaliser(m);
  for (depart = 0u; depart < NB_ETATS_GRAPHE; depart++) {
    if (markov_nb_voisins(m, depart) > 0u) {
      for (i = 0u; i < 50u; i++) {
        arrivee = markov_deplacer(m, depart);
        assert(arrivee < NB_ETATS_GRAPHE);
        assert(markov_poids(m, depart, arrivee) > 0u);
      }
    }
  }
  markov_detruire(&m);
  free(attendus);
}

static void test_deplacer_voisin_unique(void)
{
  markov_t *m = chaine_avec_etats(3u, 1u);
  unsigned int i;

  assert(markov_augmenter_poids(m, 0u, 2u, 17u));
  assert(markov_augmenter_poids(m, 2u, 2u, 1u));
  markov_finaliser(m);
  for (i = 0u; i < 1000u; i++) {
    assert(markov_deplacer(m, 0u) == 2u);
    assert(markov_deplacer(m, 2u) == 2u);
  }
  markov_detruire(&m);
}

static void test_deplacer_respecte_probabilites(void)
{
  const unsigned int nb_tirages = 200000u;
  const unsigned int poids[4] = { 1u, 2u, 7u, 30u };  /* total 40 */
  unsigned int compte[5] = { 0u, 0u, 0u, 0u, 0u };
  markov_t *m = chaine_avec_etats(5u, 2023u);
  unsigned int i;
  double frequence;

  for (i = 0u; i < 4u; i++) {
    assert(markov_augmenter_poids(m, 0u, i + 1u, poids[i]));
  }
  markov_finaliser(m);
  for (i = 0u; i < nb_tirages; i++) {
    compte[markov_deplacer(m, 0u)]++;
  }
  assert(compte[0] == 0u);
  /* graine fixee : test deterministe ; l'ecart-type de chaque frequence
     est inferieur a 0,0012, la tolerance de 0,006 vaut donc 5 ecarts-types */
  for (i = 0u; i < 4u; i++) {
    frequence = (double)compte[i + 1u] / (double)nb_tirages;
    assert(ecart(frequence, markov_probabilite(m, 0u, i + 1u)) < 0.006);
  }
  markov_detruire(&m);
}

static void test_meme_graine_memes_deplacements(void)
{
  markov_t *a = chaine_avec_etats(10u, 77u);
  markov_t *b = chaine_avec_etats(10u, 77u);
  markov_t *c = chaine_avec_etats(10u, 78u);
  unsigned int e;
  unsigned int i;
  unsigned int ea = 0u;
  unsigned int eb = 0u;
  unsigned int ec = 0u;
  unsigned int nb_differences = 0u;

  for (e = 0u; e < 10u; e++) {
    for (i = 0u; i < 10u; i++) {
      assert(markov_augmenter_poids(a, e, i, 1u + i));
      assert(markov_augmenter_poids(b, e, i, 1u + i));
      assert(markov_augmenter_poids(c, e, i, 1u + i));
    }
  }
  markov_finaliser(a);
  markov_finaliser(b);
  markov_finaliser(c);
  for (i = 0u; i < 1000u; i++) {
    ea = markov_deplacer(a, ea);
    eb = markov_deplacer(b, eb);
    ec = markov_deplacer(c, ec);
    assert(ea == eb);
    nb_differences += (ea != ec) ? 1u : 0u;
  }
  /* une autre graine donne une autre suite de deplacements */
  assert(nb_differences > 0u);
  markov_detruire(&a);
  markov_detruire(&b);
  markov_detruire(&c);
}


/* ======================================================================== */
/*                        Enumeration des voisins                           */
/* ======================================================================== */

static void test_enumerer_voisins(void)
{
  unsigned int *attendus = malloc(NB_ETATS_GRAPHE * NB_ETATS_GRAPHE * sizeof(unsigned int));
  markov_t *m;
  unsigned int depart;
  unsigned int rang;
  unsigned int voisin;
  unsigned int precedent;
  unsigned int poids;
  unsigned int total;
  unsigned int nb_attendus;
  unsigned int arrivee;

  assert(attendus != NULL);
  m = graphe_aleatoire(attendus, 13u);
  markov_finaliser(m);
  for (depart = 0u; depart < NB_ETATS_GRAPHE; depart++) {
    total = 0u;
    precedent = 0u;
    for (rang = 0u; rang < markov_nb_voisins(m, depart); rang++) {
      voisin = markov_voisin(m, depart, rang, &poids);
      /* voisins croissants, poids exacts */
      assert(rang == 0u || voisin > precedent);
      assert(poids == attendus[(depart * NB_ETATS_GRAPHE) + voisin]);
      assert(poids == markov_poids(m, depart, voisin));
      total += poids;
      precedent = voisin;
    }
    nb_attendus = 0u;
    for (arrivee = 0u; arrivee < NB_ETATS_GRAPHE; arrivee++) {
      nb_attendus += (attendus[(depart * NB_ETATS_GRAPHE) + arrivee] > 0u)
                     ? 1u : 0u;
    }
    assert(markov_nb_voisins(m, depart) == nb_attendus);
    assert(total == markov_poids_total(m, depart));
  }
  markov_detruire(&m);
  free(attendus);
}


/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_voisin_avant_finalisation(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);
  unsigned int poids;

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  markov_voisin(m, 0u, 0u, &poids);
}

static void violation_voisin_rang(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);
  unsigned int poids;

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  markov_finaliser(m);
  markov_voisin(m, 0u, 1u, &poids);
}

static void violation_voisin_poids_nul(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  markov_finaliser(m);
  markov_voisin(m, 0u, 0u, NULL);
}

static void violation_chaine_nulle(void)
{
  markov_nb_etats(NULL);
}

static void violation_detruire_nul(void)
{
  markov_detruire(NULL);
}

static void violation_etat_inexistant(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  markov_poids(m, 0u, 2u);
}

static void violation_augmenter_etat_inexistant(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  markov_augmenter_poids(m, 2u, 0u, 1u);
}

static void violation_increment_nul(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  markov_augmenter_poids(m, 0u, 1u, 0u);
}

static void violation_augmenter_apres_finalisation(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  markov_finaliser(m);
  markov_augmenter_poids(m, 0u, 1u, 1u);
}

static void violation_ajouter_etat_apres_finalisation(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  markov_finaliser(m);
  markov_ajouter_etat(m);
}

static void violation_double_finalisation(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  markov_finaliser(m);
  markov_finaliser(m);
}

static void violation_probabilite_avant_finalisation(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  markov_probabilite(m, 0u, 1u);
}

static void violation_deplacer_avant_finalisation(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  markov_deplacer(m, 0u);
}

static void violation_deplacer_sans_voisin(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  markov_finaliser(m);
  markov_deplacer(m, 1u);
}

static void sans_violation(void)
{
  markov_t *m = chaine_avec_etats(2u, 1u);

  assert(markov_augmenter_poids(m, 0u, 1u, 1u));
  markov_finaliser(m);
  assert(markov_deplacer(m, 0u) == 1u);
  markov_detruire(&m);
}

static void test_contrats(void)
{
  /* temoin : un usage correct n'est pas detecte comme une violation */
  assert(!viole_contrat(sans_violation));

  assert(viole_contrat(violation_chaine_nulle));
  assert(viole_contrat(violation_detruire_nul));
  assert(viole_contrat(violation_etat_inexistant));
  assert(viole_contrat(violation_augmenter_etat_inexistant));
  assert(viole_contrat(violation_increment_nul));
  assert(viole_contrat(violation_augmenter_apres_finalisation));
  assert(viole_contrat(violation_ajouter_etat_apres_finalisation));
  assert(viole_contrat(violation_double_finalisation));
  assert(viole_contrat(violation_probabilite_avant_finalisation));
  assert(viole_contrat(violation_deplacer_avant_finalisation));
  assert(viole_contrat(violation_deplacer_sans_voisin));
  assert(viole_contrat(violation_voisin_avant_finalisation));
  assert(viole_contrat(violation_voisin_rang));
  assert(viole_contrat(violation_voisin_poids_nul));
}


int main(void)
{
  printf("Tests du module markov :\n");
  LANCER_TEST(test_creer_detruire);
  LANCER_TEST(test_ajouter_etats);
  LANCER_TEST(test_augmenter_poids);
  LANCER_TEST(test_nombreux_voisins);
  LANCER_TEST(test_depassement_poids);
  LANCER_TEST(test_finaliser_conserve_poids);
  LANCER_TEST(test_finaliser_chaine_vide);
  LANCER_TEST(test_probabilites);
  LANCER_TEST(test_somme_probabilites);
  LANCER_TEST(test_deplacer_vers_un_voisin);
  LANCER_TEST(test_deplacer_voisin_unique);
  LANCER_TEST(test_deplacer_respecte_probabilites);
  LANCER_TEST(test_meme_graine_memes_deplacements);
  LANCER_TEST(test_enumerer_voisins);
  LANCER_TEST(test_contrats);
  afficher_bilan("markov");
  return EXIT_SUCCESS;
}
