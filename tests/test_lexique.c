/**
Fichier test_lexique.c

Tests unitaires du module lexique (cf. src/lexique.h).
Les chaines UTF-8 sont ecrites avec des sequences \x.. : "\xC3\xA9" est le
e accent aigu, "\xC3\x89" le E accent aigu, "\xC5\x93" le oe lie.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "outils_tests.h"
#include "../src/lexique.h"

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/** Lexique de nb_mots mots, chacun avec le bigramme (0, mot). */
static lexique_t *lexique_avec_mots(unsigned int nb_mots)
{
  lexique_t *l = lexique_creer();
  unsigned int i;

  assert(l != NULL);
  for (i = 0u; i < nb_mots; i++) {
    assert(lexique_ajouter_mot(l) == i);
    assert(lexique_ajouter_bigramme(l, i) == i);
  }
  return l;
}

/** Observe n fois une graphie. */
static void observer(lexique_t *l, unsigned int bigramme, const char *graphie,
                     bool accentue, bool debut, liaison_t liaison, unsigned int n)
{
  unsigned int i;

  for (i = 0u; i < n; i++) {
    assert(lexique_observer(l, bigramme, graphie, accentue, debut, liaison));
  }
}

/** Verifie la graphie d'affichage d'un bigramme. */
static void verifier_ecriture(const lexique_t *l, unsigned int bigramme,
                              bool debut, const char *attendue)
{
  char tampon[64];
  size_t longueur = lexique_ecrire(l, bigramme, debut, tampon, sizeof(tampon));

  assert(longueur == strlen(attendue));
  if (strcmp(tampon, attendue) != 0) {
    fprintf(stderr, "\nattendu \"%s\", obtenu \"%s\"\n", attendue, tampon);
  }
  assert(strcmp(tampon, attendue) == 0);
}

/** Variante choisie (texte) d'un mot. */
static const char *variante_choisie(const lexique_t *l, unsigned int mot)
{
  return lexique_variante(l, mot, lexique_variante_mot(l, mot));
}


/* ======================================================================== */
/*                                 Graphies                                 */
/* ======================================================================== */

static void test_graphie_valide(void)
{
  assert(lexique_graphie_valide("ete"));
  assert(lexique_graphie_valide("\xC3\xA9t\xC3\xA9"));
  assert(lexique_graphie_valide("\xC3\x89T\xC3\x89"));
  assert(lexique_graphie_valide("\xC5\x93uvre"));
  assert(lexique_graphie_valide("\xC5\x92UVRE"));
  assert(lexique_graphie_valide("\xC3\xBF\xC5\xB8"));     /* y et Y trema */
  assert(lexique_graphie_valide("stra\xC3\x9F" "e"));
  assert(!lexique_graphie_valide(""));
  assert(!lexique_graphie_valide("a b"));
  assert(!lexique_graphie_valide("l'eau"));
  assert(!lexique_graphie_valide("mot1"));
  assert(!lexique_graphie_valide("\xC3\x97"));             /* multiplie */
  assert(!lexique_graphie_valide("\xC3\xB7"));             /* divise */
  assert(!lexique_graphie_valide("\xC3"));                 /* tronque */
  assert(!lexique_graphie_valide("\xC3("));
  assert(!lexique_graphie_valide("\xE2\x80\xA6"));         /* 3 octets */
  assert(!lexique_graphie_valide("\xE9t\xE9"));            /* Latin-1 */
}

static void test_variante_valide(void)
{
  assert(lexique_variante_valide("\xC3\xA9t\xC3\xA9"));
  assert(lexique_variante_valide("stra\xC3\x9F" "e"));
  assert(lexique_variante_valide("\xC3\xBF"));
  assert(!lexique_variante_valide("\xC3\x89t\xC3\xA9"));
  assert(!lexique_variante_valide("eTe"));
  assert(!lexique_variante_valide("\xC5\xB8"));
  assert(!lexique_variante_valide(""));
}

static void test_casse(void)
{
  assert(lexique_casse("ete") == CASSE_MINUSCULE);
  assert(lexique_casse("Ete") == CASSE_CAPITALE);
  assert(lexique_casse("ETE") == CASSE_MAJUSCULE);
  assert(lexique_casse("E") == CASSE_CAPITALE);      /* une seule lettre */
  assert(lexique_casse("eTE") == CASSE_MINUSCULE);
  assert(lexique_casse("McDonald") == CASSE_CAPITALE);
  assert(lexique_casse("\xC3\x89T\xC3\x89") == CASSE_MAJUSCULE);
  assert(lexique_casse("\xC3\x89t\xC3\xA9") == CASSE_CAPITALE);
  assert(lexique_casse("\xC3\xA9t\xC3\xA9") == CASSE_MINUSCULE);
}

static void test_appliquer_casse(void)
{
  char ete[] = "\xC3\xA9t\xC3\xA9";
  char oeuvre[] = "\xC5\x93uvre";
  char y[] = "\xC3\xBF";
  char strasse[] = "stra\xC3\x9F" "e";
  char melange[] = "McDONALD";

  lexique_appliquer_casse(ete, CASSE_CAPITALE);
  assert(strcmp(ete, "\xC3\x89t\xC3\xA9") == 0);
  lexique_appliquer_casse(ete, CASSE_MAJUSCULE);
  assert(strcmp(ete, "\xC3\x89T\xC3\x89") == 0);
  lexique_appliquer_casse(ete, CASSE_MINUSCULE);
  assert(strcmp(ete, "\xC3\xA9t\xC3\xA9") == 0);
  lexique_appliquer_casse(oeuvre, CASSE_MAJUSCULE);
  assert(strcmp(oeuvre, "\xC5\x92UVRE") == 0);
  /* y trema : 2 octets dans les deux casses, mais pas le meme premier */
  lexique_appliquer_casse(y, CASSE_CAPITALE);
  assert(strcmp(y, "\xC5\xB8") == 0);
  lexique_appliquer_casse(y, CASSE_MINUSCULE);
  assert(strcmp(y, "\xC3\xBF") == 0);
  /* l'eszett n'a pas de majuscule d'un caractere */
  lexique_appliquer_casse(strasse, CASSE_MAJUSCULE);
  assert(strcmp(strasse, "STRA\xC3\x9F" "E") == 0);
  lexique_appliquer_casse(melange, CASSE_CAPITALE);
  assert(strcmp(melange, "Mcdonald") == 0);
}


/* ======================================================================== */
/*                        Mots, bigrammes, variantes                        */
/* ======================================================================== */

static void test_creer_detruire(void)
{
  lexique_t *l = lexique_creer();

  assert(l != NULL);
  assert(lexique_nb_mots(l) == 0u);
  assert(lexique_nb_bigrammes(l) == 0u);
  assert(!lexique_est_finalise(l));
  lexique_detruire(&l);
  assert(l == NULL);
  lexique_detruire(&l);
}

static void test_ajouter_mots_et_bigrammes(void)
{
  lexique_t *l = lexique_creer();
  unsigned int i;

  for (i = 0u; i < 1000u; i++) {
    assert(lexique_ajouter_mot(l) == i);
    assert(lexique_nb_variantes(l, i) == 0u);
  }
  for (i = 0u; i < 3000u; i++) {
    assert(lexique_ajouter_bigramme(l, i % 1000u) == i);
    assert(lexique_mot_bigramme(l, i) == i % 1000u);
  }
  assert(lexique_nb_mots(l) == 1000u);
  assert(lexique_nb_bigrammes(l) == 3000u);
  lexique_detruire(&l);
}

static void test_ajouter_variantes(void)
{
  lexique_t *l = lexique_avec_mots(2u);
  char longue[300];
  unsigned int i;

  assert(lexique_ajouter_variante(l, 1u, "ete") == 0u);
  assert(lexique_ajouter_variante(l, 1u, "\xC3\xA9t\xC3\xA9") == 1u);
  assert(lexique_ajouter_variante(l, 1u, "ete") == 0u);   /* deja connue */
  assert(lexique_nb_variantes(l, 1u) == 2u);
  assert(lexique_nb_variantes(l, 0u) == 0u);
  assert(strcmp(lexique_variante(l, 1u, 1u), "\xC3\xA9t\xC3\xA9") == 0);
  /* nombreuses variantes (agrandissements) et variante longue */
  for (i = 0u; i < 299u; i++) {
    longue[i] = 'a';
  }
  longue[299] = '\0';
  for (i = 0u; i < 40u; i++) {
    longue[i] = 'b';
    assert(lexique_ajouter_variante(l, 0u, longue) == i);
  }
  assert(lexique_nb_variantes(l, 0u) == 40u);
  assert(strncmp(lexique_variante(l, 0u, 39u), "bbbb", 4u) == 0);
  lexique_detruire(&l);
}


/* ======================================================================== */
/*                              Choix d'affichage                           */
/* ======================================================================== */

static void test_variante_selon_les_fichiers_accentues(void)
{
  lexique_t *l = lexique_avec_mots(3u);

  /* mot 1 : 5 fois sans accent (fichier sans accent), 2 fois accentue */
  observer(l, 1u, "etait", false, false, LIAISON_ESPACE, 5u);
  observer(l, 1u, "\xC3\xA9tait", true, false, LIAISON_ESPACE, 2u);
  /* mot 2 : seulement dans des fichiers sans accent */
  observer(l, 2u, "eveque", false, false, LIAISON_ESPACE, 3u);
  lexique_finaliser(l);
  assert(lexique_est_finalise(l));
  assert(strcmp(variante_choisie(l, 1u), "\xC3\xA9tait") == 0);
  assert(strcmp(variante_choisie(l, 2u), "eveque") == 0);
  assert(lexique_variante_mot(l, 0u) == LEXIQUE_AUCUNE);
  lexique_detruire(&l);
}

static void test_casse_hors_debut_de_phrase(void)
{
  lexique_t *l = lexique_avec_mots(5u);

  /* mot 1 : "Il" 10 fois en debut de phrase, "il" 3 fois ailleurs */
  observer(l, 1u, "Il", false, true, LIAISON_ESPACE, 10u);
  observer(l, 1u, "il", false, false, LIAISON_ESPACE, 3u);
  /* mot 2 : nom propre */
  observer(l, 2u, "Jean", false, true, LIAISON_ESPACE, 4u);
  observer(l, 2u, "Jean", false, false, LIAISON_ESPACE, 2u);
  /* mot 3 : seulement en debut de phrase */
  observer(l, 3u, "Toutefois", false, true, LIAISON_ESPACE, 2u);
  /* mot 4 : seulement en debut de phrase, tout en majuscules (titre) */
  observer(l, 4u, "PRELUDE", false, true, LIAISON_ESPACE, 1u);
  lexique_finaliser(l);
  assert(lexique_casse_mot(l, 1u) == CASSE_MINUSCULE);
  assert(lexique_casse_mot(l, 2u) == CASSE_CAPITALE);
  assert(lexique_casse_mot(l, 3u) == CASSE_MINUSCULE);
  assert(lexique_casse_mot(l, 4u) == CASSE_MAJUSCULE);
  /* une seule variante en minuscules par mot */
  assert(lexique_nb_variantes(l, 1u) == 1u);
  assert(strcmp(variante_choisie(l, 2u), "jean") == 0);
  lexique_detruire(&l);
}

static void test_choix_selon_le_contexte(void)
{
  /* mot 1 = "a" ; bigramme 1 = (0, a), 2 = (il, a), 3 = (va, a) */
  lexique_t *l = lexique_avec_mots(2u);

  assert(lexique_ajouter_bigramme(l, 1u) == 2u);
  assert(lexique_ajouter_bigramme(l, 1u) == 3u);
  observer(l, 2u, "a", true, false, LIAISON_ESPACE, 3u);
  observer(l, 3u, "\xC3\xA0", true, false, LIAISON_ESPACE, 5u);
  /* bigramme sans vote accentue : fichier sans accents seulement */
  observer(l, 1u, "A", false, true, LIAISON_ESPACE, 1u);
  lexique_finaliser(l);
  /* le mot seul : la variante la plus frequente des fichiers accentues */
  assert(strcmp(variante_choisie(l, 1u), "\xC3\xA0") == 0);
  /* chaque contexte retrouve sa variante */
  verifier_ecriture(l, 2u, false, "a");
  verifier_ecriture(l, 3u, false, "\xC3\xA0");
  assert(lexique_variante_bigramme(l, 1u) == LEXIQUE_AUCUNE);
  verifier_ecriture(l, 1u, true, "\xC3\x80");     /* repli sur le mot */
  lexique_detruire(&l);
}

static void test_liaisons(void)
{
  lexique_t *l = lexique_avec_mots(4u);

  observer(l, 1u, "etait", false, false, LIAISON_APOSTROPHE, 3u);
  observer(l, 1u, "etait", false, false, LIAISON_ESPACE, 1u);
  observer(l, 2u, "il", false, true, LIAISON_TRAIT_UNION, 4u);  /* ignore */
  observer(l, 3u, "monsieur", false, false, LIAISON_VIRGULE, 1u);
  observer(l, 3u, "monsieur", false, false, LIAISON_ESPACE, 1u);
  observer(l, 3u, "monsieur", false, false, LIAISON_VIRGULE, 1u);
  lexique_finaliser(l);
  assert(lexique_liaison(l, 1u) == LIAISON_APOSTROPHE);
  assert(lexique_liaison(l, 2u) == LIAISON_ESPACE);     /* pas de vote */
  assert(lexique_liaison(l, 3u) == LIAISON_VIRGULE);
  assert(lexique_liaison(l, 0u) == LIAISON_ESPACE);
  lexique_detruire(&l);
}

static void test_vote_majoritaire(void)
{
  /* Boyer et Moore : A B A C A B A -> A (majorite absolue) */
  const char *const votes[] = { "a", "b", "a", "c", "a", "b", "a" };
  lexique_t *l = lexique_creer();
  unsigned int mots[3];
  unsigned int i;

  for (i = 0u; i < 3u; i++) {
    mots[i] = lexique_ajouter_mot(l);
  }
  /* un bigramme dont le mot a trois variantes de casse differente */
  assert(lexique_ajouter_bigramme(l, mots[1]) == 0u);
  for (i = 0u; i < 7u; i++) {
    observer(l, 0u, (votes[i][0] == 'a') ? "Mot" : (votes[i][0] == 'b')
                    ? "MOT" : "mot", false, false, LIAISON_ESPACE, 1u);
  }
  lexique_finaliser(l);
  assert(lexique_casse_bigramme(l, 0u) == CASSE_CAPITALE);
  verifier_ecriture(l, 0u, false, "Mot");
  lexique_detruire(&l);
}

static void test_ecrire(void)
{
  lexique_t *l = lexique_avec_mots(3u);
  char petit[4] = "xyz";
  char juste[7];

  observer(l, 1u, "\xC3\xA9t\xC3\xA9", true, false, LIAISON_ESPACE, 1u);
  observer(l, 2u, "ONU", true, false, LIAISON_ESPACE, 1u);
  lexique_finaliser(l);
  /* comme snprintf : longueur sans rien ecrire si le tampon est trop petit */
  assert(lexique_ecrire(l, 1u, false, NULL, 0u) == 5u);
  assert(lexique_ecrire(l, 1u, false, petit, sizeof(petit)) == 5u);
  assert(strcmp(petit, "xyz") == 0);
  assert(lexique_ecrire(l, 1u, false, juste, 6u) == 5u);
  assert(lexique_ecrire(l, 1u, false, juste, sizeof(juste)) == 5u);
  assert(strcmp(juste, "\xC3\xA9t\xC3\xA9") == 0);
  /* debut de phrase : capitale ; un sigle reste en majuscules */
  verifier_ecriture(l, 1u, true, "\xC3\x89t\xC3\xA9");
  verifier_ecriture(l, 2u, false, "ONU");
  verifier_ecriture(l, 2u, true, "ONU");
  lexique_detruire(&l);
}

static void test_fixer(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  assert(lexique_ajouter_variante(l, 1u, "ete") == 0u);
  assert(lexique_ajouter_variante(l, 1u, "\xC3\xA9t\xC3\xA9") == 1u);
  lexique_finaliser(l);
  lexique_fixer_mot(l, 1u, 1u, CASSE_MAJUSCULE);
  assert(lexique_variante_mot(l, 1u) == 1u);
  assert(lexique_casse_mot(l, 1u) == CASSE_MAJUSCULE);
  verifier_ecriture(l, 1u, false, "\xC3\x89T\xC3\x89");
  lexique_fixer_bigramme(l, 1u, 0u, CASSE_MINUSCULE, LIAISON_TRAIT_UNION);
  verifier_ecriture(l, 1u, false, "ete");
  assert(lexique_liaison(l, 1u) == LIAISON_TRAIT_UNION);
  lexique_fixer_bigramme(l, 1u, LEXIQUE_AUCUNE, LEXIQUE_AUCUNE, LIAISON_ESPACE);
  assert(lexique_casse_bigramme(l, 1u) == LEXIQUE_AUCUNE);
  verifier_ecriture(l, 1u, false, "\xC3\x89T\xC3\x89");
  lexique_detruire(&l);
}


/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_lexique_nul(void)
{
  lexique_nb_mots(NULL);
}

static void violation_observer_apres_finalisation(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  lexique_finaliser(l);
  lexique_observer(l, 1u, "mot", false, false, LIAISON_ESPACE);
}

static void violation_graphie_invalide(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  lexique_observer(l, 1u, "l'eau", false, false, LIAISON_ESPACE);
}

static void violation_variante_majuscule(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  lexique_ajouter_variante(l, 1u, "Ete");
}

static void violation_bigramme_mot_inexistant(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  lexique_ajouter_bigramme(l, 2u);
}

static void violation_ecrire_avant_finalisation(void)
{
  lexique_t *l = lexique_avec_mots(2u);
  char tampon[16];

  observer(l, 1u, "mot", false, false, LIAISON_ESPACE, 1u);
  lexique_ecrire(l, 1u, false, tampon, sizeof(tampon));
}

static void violation_ecrire_mot_sans_variante(void)
{
  lexique_t *l = lexique_avec_mots(2u);
  char tampon[16];

  lexique_finaliser(l);
  lexique_ecrire(l, 1u, false, tampon, sizeof(tampon));
}

static void violation_fixer_variante_inexistante(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  observer(l, 1u, "mot", false, false, LIAISON_ESPACE, 1u);
  lexique_finaliser(l);
  lexique_fixer_mot(l, 1u, 1u, CASSE_MINUSCULE);
}

static void violation_liaison_invalide(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  lexique_observer(l, 1u, "mot", false, false, NB_LIAISONS);
}

static void sans_violation(void)
{
  lexique_t *l = lexique_avec_mots(2u);

  observer(l, 1u, "mot", false, false, LIAISON_ESPACE, 1u);
  lexique_finaliser(l);
  verifier_ecriture(l, 1u, false, "mot");
  lexique_detruire(&l);
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_lexique_nul));
  assert(viole_contrat(violation_observer_apres_finalisation));
  assert(viole_contrat(violation_graphie_invalide));
  assert(viole_contrat(violation_variante_majuscule));
  assert(viole_contrat(violation_bigramme_mot_inexistant));
  assert(viole_contrat(violation_ecrire_avant_finalisation));
  assert(viole_contrat(violation_ecrire_mot_sans_variante));
  assert(viole_contrat(violation_fixer_variante_inexistante));
  assert(viole_contrat(violation_liaison_invalide));
}


int main(void)
{
  printf("Tests du module lexique :\n");
  LANCER_TEST(test_graphie_valide);
  LANCER_TEST(test_variante_valide);
  LANCER_TEST(test_casse);
  LANCER_TEST(test_appliquer_casse);
  LANCER_TEST(test_creer_detruire);
  LANCER_TEST(test_ajouter_mots_et_bigrammes);
  LANCER_TEST(test_ajouter_variantes);
  LANCER_TEST(test_variante_selon_les_fichiers_accentues);
  LANCER_TEST(test_casse_hors_debut_de_phrase);
  LANCER_TEST(test_choix_selon_le_contexte);
  LANCER_TEST(test_liaisons);
  LANCER_TEST(test_vote_majoritaire);
  LANCER_TEST(test_ecrire);
  LANCER_TEST(test_fixer);
  LANCER_TEST(test_contrats);
  afficher_bilan("lexique");
  return EXIT_SUCCESS;
}
