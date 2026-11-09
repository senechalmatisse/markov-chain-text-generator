/**
Fichier test_sauvegarde.c

Tests unitaires du module sauvegarde (cf. src/sauvegarde.h).
Les fichiers sont crees dans un repertoire temporaire, supprime a la fin
de chaque test.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include "outils_tests.h"
#include "../src/sauvegarde.h"

/* ======================================================================== */
/*                    Creation d'un fichier avec un texte                   */
/* ======================================================================== */

static void test_creer_fichier(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  char *contenu;

  assert(!sauvegarde_existe(chemin));
  assert(sauvegarde_creer_fichier("Bonjour\nle monde.\n", chemin));
  assert(sauvegarde_existe(chemin));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL && strcmp(contenu, "Bonjour\nle monde.\n") == 0);
  free(contenu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_creer_fichier_ecrase(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  char *contenu;

  assert(sauvegarde_creer_fichier("un texte assez long", chemin));
  assert(sauvegarde_creer_fichier("court", chemin));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL && strcmp(contenu, "court") == 0);
  free(contenu);
  /* texte vide : fichier vide */
  assert(sauvegarde_creer_fichier("", chemin));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL && contenu[0] == '\0');
  free(contenu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_creer_fichier_long(void)
{
  const size_t taille = 3u * 1024u * 1024u;
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "long.txt");
  char *texte = malloc(taille + 1u);
  char *contenu;
  size_t i;

  assert(texte != NULL);
  for (i = 0u; i < taille; i++) {
    texte[i] = (char)('a' + (i % 26u));
  }
  texte[taille] = '\0';
  assert(sauvegarde_creer_fichier(texte, chemin));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL && strcmp(contenu, texte) == 0);
  free(contenu);
  free(texte);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_creer_fichier_chemin_relatif(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *absolu = chemin_dans(repertoire, "relatif.txt");
  char courant[4096];
  char *contenu;

  assert(getcwd(courant, sizeof(courant)) != NULL);
  assert(chdir(repertoire) == 0);
  assert(sauvegarde_creer_fichier("relatif", "relatif.txt"));
  assert(sauvegarde_existe("relatif.txt"));
  assert(chdir(courant) == 0);
  /* le fichier a bien ete cree dans le repertoire courant d'alors */
  contenu = lire_fichier(absolu);
  assert(contenu != NULL && strcmp(contenu, "relatif") == 0);
  free(contenu);
  free(absolu);
  detruire_repertoire_temporaire(repertoire);
}

static void test_creer_fichier_erreurs(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sans_parent = chemin_dans(repertoire, "inexistant/texte.txt");
  char *protege = chemin_dans(repertoire, "protege");
  char *dans_protege = chemin_dans(protege, "texte.txt");

  /* repertoire parent inexistant */
  errno = 0;
  assert(!sauvegarde_creer_fichier("texte", sans_parent));
  assert(errno == ENOENT);
  assert(!sauvegarde_existe(sans_parent));
  /* le chemin designe un repertoire */
  errno = 0;
  assert(!sauvegarde_creer_fichier("texte", repertoire));
  assert(errno == EISDIR);
  /* chemin vide */
  assert(!sauvegarde_creer_fichier("texte", ""));
  /* repertoire sans droit d'ecriture (sans objet pour root) */
  assert(mkdir(protege, 0500) == 0);
  if (geteuid() != 0) {
    errno = 0;
    assert(!sauvegarde_creer_fichier("texte", dans_protege));
    assert(errno == EACCES);
  }
  free(sans_parent);
  free(dans_protege);
  free(protege);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                         Ouverture en mode ajout                          */
/* ======================================================================== */

static void test_ouvrir_ajout_cree_le_fichier(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "ajout.txt");
  FILE *fichier;
  char *contenu;

  fichier = sauvegarde_ouvrir_ajout(chemin);
  assert(fichier != NULL);
  assert(sauvegarde_existe(chemin));
  assert(sauvegarde_ecrire(fichier, "abc"));
  assert(sauvegarde_ecrire(fichier, ""));
  assert(sauvegarde_ecrire(fichier, "def\n"));
  assert(sauvegarde_fermer(fichier));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL && strcmp(contenu, "abcdef\n") == 0);
  free(contenu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_ouvrir_ajout_conserve_le_contenu(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "ajout.txt");
  FILE *fichier;
  char *contenu;
  int i;

  assert(sauvegarde_creer_fichier("debut ", chemin));
  for (i = 0; i < 3; i++) {
    fichier = sauvegarde_ouvrir_ajout(chemin);
    assert(fichier != NULL);
    assert(sauvegarde_ecrire(fichier, "suite "));
    assert(sauvegarde_fermer(fichier));
  }
  contenu = lire_fichier(chemin);
  assert(contenu != NULL
         && strcmp(contenu, "debut suite suite suite ") == 0);
  free(contenu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_ouvrir_ajout_erreurs(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sans_parent = chemin_dans(repertoire, "inexistant/ajout.txt");

  errno = 0;
  assert(sauvegarde_ouvrir_ajout(sans_parent) == NULL);
  assert(errno == ENOENT);
  errno = 0;
  assert(sauvegarde_ouvrir_ajout(repertoire) == NULL);
  assert(errno == EISDIR);
  assert(sauvegarde_ouvrir_ajout("") == NULL);
  free(sans_parent);
  detruire_repertoire_temporaire(repertoire);
}

static void test_ecrire_nombreuses_chaines(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "mots.txt");
  FILE *fichier = sauvegarde_ouvrir_ajout(chemin);
  char *contenu;
  size_t i;

  assert(fichier != NULL);
  for (i = 0u; i < 10000u; i++) {
    assert(sauvegarde_ecrire(fichier, "mot "));
  }
  assert(sauvegarde_fermer(fichier));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL && strlen(contenu) == 40000u);
  for (i = 0u; i < 10000u; i++) {
    assert(strncmp(contenu + (4u * i), "mot ", 4u) == 0);
  }
  free(contenu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                                 Existence                                */
/* ======================================================================== */

static void test_existe(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *fichier = chemin_dans(repertoire, "present.txt");
  char *absent = chemin_dans(repertoire, "absent.txt");

  ecrire_fichier(fichier, "x");
  assert(sauvegarde_existe(fichier));
  assert(sauvegarde_existe(repertoire));
  assert(!sauvegarde_existe(absent));
  assert(!sauvegarde_existe(""));
  free(fichier);
  free(absent);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                        Suppression et disque plein                       */
/* ======================================================================== */

static void test_supprimer(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *fichier = chemin_dans(repertoire, "a.txt");
  char *lien = chemin_dans(repertoire, "lien");
  char *sous = chemin_dans(repertoire, "sous");

  ecrire_fichier(fichier, "x");
  assert(symlink(fichier, lien) == 0);
  assert(mkdir(sous, 0700) == 0);
  /* ni lien, ni repertoire, ni peripherique */
  errno = 0;
  assert(!sauvegarde_supprimer(lien) && errno == EPERM);
  assert(sauvegarde_existe(lien) && sauvegarde_existe(fichier));
  errno = 0;
  assert(!sauvegarde_supprimer(sous) && errno == EPERM);
  assert(sauvegarde_existe(sous));
  errno = 0;
  assert(!sauvegarde_supprimer("/dev/null") && errno == EPERM);
  assert(sauvegarde_existe("/dev/null"));
  /* un fichier ordinaire, une seule fois */
  assert(sauvegarde_supprimer(fichier));
  assert(!sauvegarde_existe(fichier));
  errno = 0;
  assert(!sauvegarde_supprimer(fichier) && errno == ENOENT);
  free(fichier);
  free(lien);
  free(sous);
  detruire_repertoire_temporaire(repertoire);
}

static void test_disque_plein(void)
{
  /* /dev/full accepte l'ouverture mais toute ecriture echoue (ENOSPC) */
  FILE *fichier;
  bool ecrit = true;
  bool ferme;
  unsigned int i;

  if (sauvegarde_existe("/dev/full")) {
    errno = 0;
    assert(!sauvegarde_creer_fichier("un texte", "/dev/full"));
    assert(errno == ENOSPC);
    /* le peripherique n'a surtout pas ete supprime */
    assert(sauvegarde_existe("/dev/full"));
    fichier = sauvegarde_ouvrir_ajout("/dev/full");
    assert(fichier != NULL);
    for (i = 0u; ecrit && i < 2000u; i++) {
      ecrit = sauvegarde_ecrire(fichier, "0123456789012345678901234567890123"
                                         "456789012345678901234567890123\n");
    }
    ferme = sauvegarde_fermer(fichier);
    /* l'erreur est signalee a l'ecriture ou, au plus tard, a la fermeture */
    assert(!ecrit || !ferme);
    assert(sauvegarde_existe("/dev/full"));
  }
}


/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_supprimer_nul(void)
{
  sauvegarde_supprimer(NULL);
}

static void violation_texte_nul(void)
{
  sauvegarde_creer_fichier(NULL, "/tmp/test_sauvegarde_inutile.txt");
}

static void violation_chemin_nul(void)
{
  sauvegarde_creer_fichier("texte", NULL);
}

static void violation_ouvrir_chemin_nul(void)
{
  sauvegarde_ouvrir_ajout(NULL);
}

static void violation_ecrire_fichier_nul(void)
{
  sauvegarde_ecrire(NULL, "texte");
}

static void violation_ecrire_chaine_nulle(void)
{
  FILE *fichier = tmpfile();

  sauvegarde_ecrire(fichier, NULL);
}

static void violation_ecrire_hors_mode_ajout(void)
{
  /* tmpfile ouvre en "w+b" : ce n'est pas le mode ajout */
  FILE *fichier = tmpfile();

  sauvegarde_ecrire(fichier, "texte");
}

static void violation_ecrire_lecture_seule(void)
{
  FILE *fichier = fopen("/dev/null", "r");

  sauvegarde_ecrire(fichier, "texte");
}

static void violation_fermer_nul(void)
{
  sauvegarde_fermer(NULL);
}

static void violation_existe_nul(void)
{
  sauvegarde_existe(NULL);
}

static void sans_violation(void)
{
  FILE *fichier = sauvegarde_ouvrir_ajout("/dev/null");

  assert(fichier != NULL);
  assert(sauvegarde_ecrire(fichier, "texte"));
  assert(sauvegarde_fermer(fichier));
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_texte_nul));
  assert(viole_contrat(violation_chemin_nul));
  assert(viole_contrat(violation_ouvrir_chemin_nul));
  assert(viole_contrat(violation_ecrire_fichier_nul));
  assert(viole_contrat(violation_ecrire_chaine_nulle));
  assert(viole_contrat(violation_ecrire_hors_mode_ajout));
  assert(viole_contrat(violation_ecrire_lecture_seule));
  assert(viole_contrat(violation_fermer_nul));
  assert(viole_contrat(violation_existe_nul));
  assert(viole_contrat(violation_supprimer_nul));
}


int main(void)
{
  printf("Tests du module sauvegarde :\n");
  LANCER_TEST(test_creer_fichier);
  LANCER_TEST(test_creer_fichier_ecrase);
  LANCER_TEST(test_creer_fichier_long);
  LANCER_TEST(test_creer_fichier_chemin_relatif);
  LANCER_TEST(test_creer_fichier_erreurs);
  LANCER_TEST(test_ouvrir_ajout_cree_le_fichier);
  LANCER_TEST(test_ouvrir_ajout_conserve_le_contenu);
  LANCER_TEST(test_ouvrir_ajout_erreurs);
  LANCER_TEST(test_ecrire_nombreuses_chaines);
  LANCER_TEST(test_existe);
  LANCER_TEST(test_supprimer);
  LANCER_TEST(test_disque_plein);
  LANCER_TEST(test_contrats);
  afficher_bilan("sauvegarde");
  return EXIT_SUCCESS;
}
