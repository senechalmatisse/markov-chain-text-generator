/**
Fichier test_aleatoire.c

Test aleatoire (fuzzing) des lectures de fichiers : on donne au programme
des octets au hasard et on verifie que les contrats tiennent toujours.
Compile avec AddressSanitizer et UndefinedBehaviorSanitizer (cible
test_aleatoire du Makefile) : tout debordement de tampon, lecture hors
bornes ou comportement indefini arrete le test.

  1. Iterateur : fichiers d'octets quelconques ou de fragments "piegeux"
     (UTF-8 tronque, Latin-1, marques Gutenberg, octets nuls, ...). Chaque
     mot doit etre non vide, sur {a..z}, avec une graphie valide qui lui
     correspond ; le dernier mot termine une phrase.
  2. Chaine complete : apprentissage sur des corpus aleatoires (ordre 1 et
     2), generation, sauvegarde et rechargement (memes phrases).
  3. Chargement de modeles abimes : un modele valide est modifie au hasard
     (octets changes, lignes supprimees ou dupliquees, chiffres modifies,
     fichier tronque). Le chargement doit refuser le fichier ou donner un
     modele utilisable sans risque.

Usage : ./test_aleatoire [nombre d'iterations] [graine] (defaut : 3000
iterations, graine fixe). Le hasard est deterministe : un echec est
toujours reproductible avec les memes arguments.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include "outils_tests.h"
#include "../src/hachage.h"
#include "../src/lexique.h"
#include "../src/repertoire.h"
#include "../src/modele.h"

/** Nombre d'iterations par defaut. */
#define ITERATIONS_DEFAUT 3000u

/** Taille maximale d'un fichier aleatoire. */
#define TAILLE_MAX 3000u

static uint64_t etat_aleatoire = UINT64_C(0x9E3779B97F4A7C15);
static unsigned int nb_iterations = ITERATIONS_DEFAUT;
static char *travail = NULL;

/** Generateur deterministe (xorshift64*). */
static uint64_t aleatoire(void)
{
  etat_aleatoire ^= etat_aleatoire >> 12;
  etat_aleatoire ^= etat_aleatoire << 25;
  etat_aleatoire ^= etat_aleatoire >> 27;
  return etat_aleatoire * UINT64_C(0x2545F4914F6CDD1D);
}

static size_t au_hasard(size_t borne)
{
  return (size_t)(aleatoire() % (uint64_t)borne);
}

/** Fragments qui exercent les cas delicats de la lecture. */
static const char *const FRAGMENTS[] = {
  "le ", "chat", "Dort", "ETE", " ", ".", "!", "?", "...", ",", ";", ":",
  "'", "\xE2\x80\x99", "-", "\xE2\x80\x91", "\n", "\n\n", "\r\n", "\t",
  "\xC3\xA9", "\xC3\x89", "\xC5\x93", "\xC5\x92", "\xC3\x9F", "\xC3\xBF",
  "\xC5\xB8", "\xC3\x97", "\xE2\x80\xA6", "\xEF\xBB\xBF", "\xC2\xA0",
  "\xC3", "\xE2\x80", "\xF0\x9F\x98\x80", "\xE9", "\xFF", "\x80", "\xC0\xAF",
  "M. ", "MM. ", "J.-J. ", "3.5", "42", "\xC2\xAB ", " \xC2\xBB",
  "*** START OF THE PROJECT GUTENBERG EBOOK X ***\n",
  "*** END OF THE PROJECT GUTENBERG EBOOK X ***\n",
  "End of the Project Gutenberg EBook\n", "***", "\n***\n"
};

/**
 * Remplit tampon au hasard : octets quelconques (mode 0) ou suite de
 * fragments (mode 1). Renvoie le nombre d'octets.
 */
static size_t generer(char *tampon, size_t capacite, unsigned int mode)
{
  size_t taille = au_hasard(capacite);
  size_t n = 0u;
  size_t longueur;
  const char *fragment;

  if (mode == 0u) {
    for (n = 0u; n < taille; n++) {
      tampon[n] = (char)au_hasard(256u);
    }
  } else {
    while (n < taille) {
      fragment = FRAGMENTS[au_hasard(sizeof(FRAGMENTS) / sizeof(FRAGMENTS[0]))];
      longueur = strlen(fragment);
      if (au_hasard(40u) == 0u) {
        fragment = "\0";                    /* octet nul */
        longueur = 1u;
      }
      if (n + longueur > capacite) {
        longueur = capacite - n;
      }
      memcpy(tampon + n, fragment, longueur);
      n += longueur;
    }
  }
  return n;
}

/** Lit tout un fichier et verifie les contrats de l'iterateur. */
static unsigned int verifier_iterateur(const char *chemin)
{
  iterateur_mots_t *it = iterateur_mots_creer(chemin);
  const char *mot;
  const char *graphie;
  bool fin = true;
  bool dernier_fin = true;
  unsigned int nb_mots = 0u;

  assert(it != NULL);
  while ((mot = iterateur_mots_suivant(it, &fin)) != NULL) {
    graphie = iterateur_mots_graphie(it);
    assert(mot[0] != '\0' && hachage_mot_valide(mot));
    assert(lexique_graphie_valide(graphie));
    assert(repertoire_graphie_valide(graphie, mot));
    assert(iterateur_mots_liaison(it) < NB_LIAISONS);
    assert(iterateur_mots_ponctuation(it) < NB_PONCTUATIONS);
    assert(fin || iterateur_mots_ponctuation(it) == PONCTUATION_AUCUNE);
    assert(!fin || iterateur_mots_liaison(it) == LIAISON_ESPACE);
    dernier_fin = fin;
    nb_mots++;
  }
  assert(!fin);
  assert(dernier_fin);                      /* le dernier mot finit une phrase */
  assert(iterateur_mots_erreur(it) == 0);
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  iterateur_mots_detruire(&it);
  return nb_mots;
}

/** Verifie une phrase generee : forme, et mots du modele. */
static void verifier_phrase(const modele_t *m, const char *phrase)
{
  char *chemin = chemin_dans(travail, "phrase.txt");
  iterateur_mots_t *it;
  const char *mot;
  size_t longueur = strlen(phrase);

  assert(longueur >= 2u && strchr(phrase, '\n') == NULL);
  assert(phrase[longueur - 1u] == '.' || phrase[longueur - 1u] == '!'
         || phrase[longueur - 1u] == '?');
  ecrire_fichier(chemin, phrase);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  while ((mot = iterateur_mots_suivant(it, NULL)) != NULL) {
    assert(hachage_contient(modele_mots(m), mot));
  }
  iterateur_mots_detruire(&it);
  free(chemin);
}


/* ======================================================================== */
/*                                   Tests                                  */
/* ======================================================================== */

static void test_iterateur_octets_au_hasard(void)
{
  static char tampon[TAILLE_MAX];
  char *chemin = chemin_dans(travail, "aleatoire.txt");
  unsigned int i;
  size_t taille;
  unsigned long nb_mots = 0u;

  for (i = 0u; i < nb_iterations; i++) {
    taille = generer(tampon, TAILLE_MAX, i % 2u);
    ecrire_octets(chemin, tampon, taille);
    nb_mots += verifier_iterateur(chemin);
  }
  assert(nb_mots > nb_iterations);          /* le hasard produit des mots */
  free(chemin);
}

static void test_chaine_complete(void)
{
  static char tampon[TAILLE_MAX];
  char *corpus = chemin_dans(travail, "corpus");
  char *chemin_modele = chemin_dans(travail, "modele.txt");
  char *chemin_fichier;
  char nom[32];
  modele_t *m;
  modele_t *recharge;
  char *phrase;
  char *phrase_rechargee;
  unsigned int i;
  unsigned int f;
  unsigned int k;
  size_t taille;
  unsigned int graine;

  for (i = 0u; i < nb_iterations / 10u + 1u; i++) {
    supprimer_arborescence(corpus);
    assert(mkdir(corpus, 0700) == 0);
    for (f = 0u; f < 1u + (unsigned int)au_hasard(3u); f++) {
      taille = generer(tampon, TAILLE_MAX, 1u);
      snprintf(nom, sizeof(nom), "f%u.txt", f);
      chemin_fichier = chemin_dans(corpus, nom);
      ecrire_octets(chemin_fichier, tampon, taille);
      free(chemin_fichier);
    }
    graine = (unsigned int)aleatoire();
    m = modele_apprendre(corpus, graine, 1u + i % 2u, false);
    assert(m != NULL);
    if (modele_peut_generer(m)) {
      assert(modele_sauvegarder(m, chemin_modele));
      recharge = modele_charger(chemin_modele, graine);
      assert(recharge != NULL);
      for (k = 0u; k < 5u; k++) {
        phrase = modele_generer_phrase(m);
        phrase_rechargee = modele_generer_phrase(recharge);
        assert(phrase != NULL && phrase_rechargee != NULL);
        assert(strcmp(phrase, phrase_rechargee) == 0);
        verifier_phrase(m, phrase);
        free(phrase);
        free(phrase_rechargee);
      }
      modele_detruire(&recharge);
    }
    modele_detruire(&m);
  }
  supprimer_arborescence(corpus);
  free(corpus);
  free(chemin_modele);
}

/** Applique une modification aleatoire a un texte (resultat a liberer). */
static char *abimer(const char *texte, size_t taille, size_t *nouvelle_taille)
{
  char *resultat = malloc(2u * taille + 16u);
  size_t debut;
  size_t fin;
  size_t n;
  size_t i;

  assert(resultat != NULL);
  memcpy(resultat, texte, taille);
  n = taille;
  switch (au_hasard(5u)) {
  case 0u:                                  /* quelques octets changes */
    for (i = 0u; i < 1u + au_hasard(3u); i++) {
      resultat[au_hasard(n)] = (char)au_hasard(256u);
    }
    break;
  case 1u:                                  /* un chiffre modifie */
    for (i = au_hasard(n); i < n && (resultat[i] < '0' || resultat[i] > '9');
         i++) {
    }
    if (i < n) {
      resultat[i] = (char)('0' + au_hasard(10u));
    }
    break;
  case 2u:                                  /* un passage supprime */
    debut = au_hasard(n);
    fin = debut + au_hasard(n - debut + 1u);
    memmove(resultat + debut, resultat + fin, n - fin);
    n -= fin - debut;
    break;
  case 3u:                                  /* un passage duplique */
    debut = au_hasard(n);
    fin = debut + au_hasard(n - debut + 1u);
    memmove(resultat + fin + (fin - debut), resultat + fin, n - fin);
    memcpy(resultat + fin, resultat + debut, fin - debut);
    n += fin - debut;
    break;
  default:                                  /* fichier tronque */
    n = au_hasard(n + 1u);
    break;
  }
  *nouvelle_taille = n;
  return resultat;
}

static void test_charger_modeles_abimes(void)
{
  char *repertoire = chemin_dans(travail, "corpus_modele");
  char *chemin = chemin_dans(travail, "modele_valide.txt");
  char *abime = chemin_dans(travail, "modele_abime.txt");
  char *valide;
  char *modifie;
  char *phrase;
  modele_t *m;
  size_t taille;
  size_t nouvelle_taille;
  unsigned int i;
  unsigned int k;
  unsigned int nb_acceptes = 0u;

  assert(mkdir(repertoire, 0700) == 0);
  creer_fichier_dans(repertoire, "a.txt",
                     "C'\xC3\xA9tait l'\xC3\xA9t\xC3\xA9. Il a dit : "
                     "peut-\xC3\xAAtre ! M. Dupont va \xC3\xA0 Paris ? "
                     "Oui, monsieur. Le chat dort, le chien aussi.");
  for (k = 1u; k <= 2u; k++) {
    m = modele_apprendre(repertoire, 1u, k, false);
    assert(m != NULL && modele_sauvegarder(m, chemin));
    modele_detruire(&m);
    valide = lire_fichier(chemin);
    assert(valide != NULL);
    taille = strlen(valide);
    for (i = 0u; i < nb_iterations / 2u + 1u; i++) {
      modifie = abimer(valide, taille, &nouvelle_taille);
      ecrire_octets(abime, modifie, nouvelle_taille);
      m = modele_charger(abime, (unsigned int)i);
      if (m != NULL) {
        /* modele accepte : il doit etre utilisable sans risque */
        nb_acceptes++;
        if (modele_peut_generer(m)) {
          phrase = modele_generer_phrase(m);
          assert(phrase != NULL);
          free(phrase);
        }
        modele_detruire(&m);
      } else {
        assert(errno != 0);
      }
      free(modifie);
    }
    free(valide);
  }
  /* la plupart des modifications sont detectees */
  assert(nb_acceptes < nb_iterations);
  supprimer_arborescence(repertoire);
  free(repertoire);
  free(chemin);
  free(abime);
}


int main(int argc, char *argv[])
{
  if (argc > 1) {
    nb_iterations = (unsigned int)strtoul(argv[1], NULL, 10);
    if (nb_iterations == 0u) {
      nb_iterations = 1u;
    }
  }
  if (argc > 2) {
    /* autre graine : explorer d'autres suites aleatoires */
    etat_aleatoire ^= (uint64_t)strtoull(argv[2], NULL, 10);
    if (etat_aleatoire == 0u) {
      etat_aleatoire = 1u;
    }
  }
  travail = creer_repertoire_temporaire();
  printf("Tests aleatoires (%u iterations) :\n", nb_iterations);
  LANCER_TEST(test_iterateur_octets_au_hasard);
  LANCER_TEST(test_chaine_complete);
  LANCER_TEST(test_charger_modeles_abimes);
  afficher_bilan("aleatoire");
  detruire_repertoire_temporaire(travail);
  return EXIT_SUCCESS;
}
