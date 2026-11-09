/**
Fichier test_programme.c

Test du programme principal : lance l'executable (premier argument, par
defaut ./projet) avec differentes options et verifie le texte produit :
une phrase par ligne, qui commence par une majuscule, se termine par une
ponctuation finale et ne contient que des mots du corpus. Verifie aussi la
reproductibilite (graine), la sauvegarde et le rechargement d'un modele,
l'ancienne syntaxe et les codes de retour en cas d'erreur.
A lancer depuis la racine du projet (le corpus est dataset/).
*/
/* realpath est une fonction POSIX de l'extension XSI */
#define _XOPEN_SOURCE 700

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "outils_tests.h"
#include "../src/repertoire.h"
#include "../src/lexique.h"

/** Executable teste (chemin absolu) et corpus (chemins absolus). */
static char *programme = NULL;
static char *corpus = NULL;
static char *corpus_anglais = NULL;

/** Repertoire des fichiers produits par les tests. */
static char *travail = NULL;

/** Mots du corpus (non recursif, puis avec les sous-repertoires). */
static hachage_t *mots_corpus = NULL;
static hachage_t *mots_corpus_recursif = NULL;

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/**
 * Lance le programme avec les arguments donnes (tableau termine par NULL),
 * depuis le repertoire courant donne (ou celui du test si NULL). Ses
 * sorties sont ecrites dans travail/sortie.txt et travail/erreurs.txt.
 * Renvoie son code de retour, ou -1 s'il a ete arrete par un signal.
 */
static int lancer(const char *const arguments[], const char *courant)
{
  const char *argv[16];
  char *sortie = chemin_dans(travail, "sortie.txt");
  char *erreurs = chemin_dans(travail, "erreurs.txt");
  size_t n = 0u;
  pid_t fils;
  int statut = 0;
  int code = -1;

  argv[0] = programme;
  while (arguments[n] != NULL) {
    assert(n + 2u < sizeof(argv) / sizeof(argv[0]));
    argv[n + 1u] = arguments[n];
    n++;
  }
  argv[n + 1u] = NULL;
  fflush(stdout);
  fflush(stderr);
  fils = fork();
  if (fils == 0) {
    if (freopen(sortie, "w", stdout) == NULL
        || freopen(erreurs, "w", stderr) == NULL
        || (courant != NULL && chdir(courant) != 0)) {
      _exit(126);
    }
    execv(programme, (char *const *)argv);
    _exit(127);
  }
  assert(fils > 0);
  assert(waitpid(fils, &statut, 0) == fils);
  if (WIFEXITED(statut)) {
    code = WEXITSTATUS(statut);
  }
  free(sortie);
  free(erreurs);
  return code;
}

/** Chemin d'un fichier du repertoire de travail (a liberer). */
static char *fichier(const char *nom)
{
  return chemin_dans(travail, nom);
}

/** Indique si la ligne commence par une lettre majuscule. */
static bool commence_par_majuscule(const char *ligne)
{
  char lettre[3] = { '\0', '\0', '\0' };
  bool majuscule = false;

  lettre[0] = ligne[0];
  if ((unsigned char)ligne[0] >= 0x80u) {
    lettre[1] = ligne[1];
  }
  if (lexique_graphie_valide(lettre)) {
    majuscule = (lexique_casse(lettre) != CASSE_MINUSCULE);
  }
  return majuscule;
}

/** Indique si la ligne se termine par une ponctuation finale. */
static bool termine_par_ponctuation(const char *ligne, size_t longueur)
{
  return longueur >= 2u && (ligne[longueur - 1u] == '.'
                            || ligne[longueur - 1u] == '!'
                            || ligne[longueur - 1u] == '?');
}

/**
 * Verifie un texte genere : nb_phrases lignes, chacune commencant par une
 * majuscule et se terminant par une ponctuation finale ; tous les mots
 * (au sens de l'iterateur) appartiennent a la table donnee.
 */
static void verifier_texte_genere(const char *chemin, unsigned int nb_phrases, const hachage_t *mots)
{
  char *contenu = lire_fichier(chemin);
  char *ligne;
  char *fin_ligne;
  iterateur_mots_t *it;
  const char *mot;
  unsigned int nb_lignes = 0u;
  unsigned int nb_mots = 0u;

  assert(contenu != NULL);
  ligne = contenu;
  while (*ligne != '\0') {
    fin_ligne = strchr(ligne, '\n');
    assert(fin_ligne != NULL);              /* chaque phrase finit la ligne */
    *fin_ligne = '\0';
    if (!commence_par_majuscule(ligne)
        || !termine_par_ponctuation(ligne, strlen(ligne))) {
      fprintf(stderr, "\nphrase mal formee : \"%s\"\n", ligne);
    }
    assert(commence_par_majuscule(ligne));
    assert(termine_par_ponctuation(ligne, strlen(ligne)));
    nb_lignes++;
    ligne = fin_ligne + 1;
  }
  assert(nb_lignes == nb_phrases);
  free(contenu);
  /* tous les mots sont des mots du corpus */
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  while ((mot = iterateur_mots_suivant(it, NULL)) != NULL) {
    if (!hachage_contient(mots, mot)) {
      fprintf(stderr, "\nmot hors du corpus : \"%s\"\n", mot);
    }
    assert(hachage_contient(mots, mot));
    nb_mots++;
  }
  assert(iterateur_mots_erreur(it) == 0);
  assert(nb_mots >= nb_phrases);
  iterateur_mots_detruire(&it);
}

/** Indique si deux fichiers ont exactement le meme contenu. */
static bool fichiers_identiques(const char *a, const char *b)
{
  char *contenu_a = lire_fichier(a);
  char *contenu_b = lire_fichier(b);
  bool identiques;

  assert(contenu_a != NULL && contenu_b != NULL);
  identiques = (strcmp(contenu_a, contenu_b) == 0);
  free(contenu_a);
  free(contenu_b);
  return identiques;
}

/** Table des mots d'un corpus. */
static hachage_t *mots_du_corpus(const char *chemin, bool recursif)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL };
  markov_t *chaine;

  options.recursif = recursif;
  chaine = repertoire_apprendre(chemin, 0u, mots, &options);
  assert(chaine != NULL);
  markov_detruire(&chaine);
  return mots;
}


/* ======================================================================== */
/*                                   Tests                                  */
/* ======================================================================== */

static void test_aide(void)
{
  const char *const aide[] = { "-h", NULL };
  const char *const aide_longue[] = { "--aide", NULL };
  char *sortie = fichier("sortie.txt");
  char *contenu;

  assert(lancer(aide, NULL) == 0);
  contenu = lire_fichier(sortie);
  assert(contenu != NULL && strstr(contenu, "Usage") != NULL);
  free(contenu);
  assert(lancer(aide_longue, NULL) == 0);
  free(sortie);
}

static void test_texte_genere_valide(void)
{
  char *texte = fichier("texte.txt");
  char *sortie = fichier("sortie.txt");
  char *affiche;
  char *contenu;
  const char *const arguments[] = { "-g", "42", "-n", "300", "-o", texte, corpus, NULL };

  assert(lancer(arguments, NULL) == 0);
  verifier_texte_genere(texte, 300u, mots_corpus);
  /* le texte est aussi affiche */
  affiche = lire_fichier(sortie);
  contenu = lire_fichier(texte);
  assert(affiche != NULL && contenu != NULL
         && strstr(affiche, contenu) != NULL);
  free(affiche);
  free(contenu);
  free(texte);
  free(sortie);
}

static void test_ordre_2_et_recursif(void)
{
  char *texte = fichier("texte_k2.txt");
  const char *const arguments[] = { "-k", "2", "-r", "-g", "3", "-n", "300",
                                    "-o", texte, corpus, NULL };

  assert(lancer(arguments, NULL) == 0);
  verifier_texte_genere(texte, 300u, mots_corpus_recursif);
  free(texte);
}

static void test_reproductible(void)
{
  char *a = fichier("a.txt");
  char *b = fichier("b.txt");
  char *c = fichier("c.txt");
  const char *const premier[] = { "-g", "123", "-n", "40", "-o", a, corpus, NULL };
  const char *const second[] = { "-g", "123", "-n", "40", "-o", b, corpus, NULL };
  const char *const autre[] = { "-g", "124", "-n", "40", "-o", c, corpus, NULL };

  assert(lancer(premier, NULL) == 0 && lancer(second, NULL) == 0);
  assert(fichiers_identiques(a, b));
  assert(lancer(autre, NULL) == 0);
  assert(!fichiers_identiques(a, c));
  free(a);
  free(b);
  free(c);
}

static void test_modele_sauvegarde_puis_charge(void)
{
  char *modele = fichier("modele.txt");
  char *direct = fichier("direct.txt");
  char *recharge = fichier("recharge.txt");
  const char *const apprendre[] = { "-k", "2", "-g", "9", "-n", "60", "-s",
                                    modele, "-o", direct, corpus, NULL };
  const char *const charger[] = { "-m", modele, "-g", "9", "-n", "60", "-o",
                                  recharge, NULL };

  assert(lancer(apprendre, NULL) == 0);
  assert(lancer(charger, NULL) == 0);
  /* le modele recharge donne exactement le meme texte */
  assert(fichiers_identiques(direct, recharge));
  verifier_texte_genere(recharge, 60u, mots_corpus);
  free(modele);
  free(direct);
  free(recharge);
}

static void test_syntaxe_historique(void)
{
  char *ancienne = fichier("ancienne.txt");
  char *nouvelle = fichier("nouvelle.txt");
  const char *const historique[] = { corpus_anglais, "42", "5", ancienne, NULL };
  const char *const options[] = { "-g", "42", "-n", "5", "-o", nouvelle,
                                  corpus_anglais, NULL };

  const char *const separateur[] = { "-g", "42", "-n", "5", "-o", nouvelle,
                                     "--", corpus_anglais, NULL };

  assert(lancer(historique, NULL) == 0);
  assert(lancer(options, NULL) == 0);
  assert(fichiers_identiques(ancienne, nouvelle));
  /* "--" : la suite n'est faite que d'arguments positionnels */
  assert(lancer(separateur, NULL) == 0);
  assert(fichiers_identiques(ancienne, nouvelle));
  free(ancienne);
  free(nouvelle);
}

static void test_nom_automatique_jamais_ecrase(void)
{
  char *dossier = fichier("automatique");
  char *premier = chemin_dans(dossier, "texte_genere_5.txt");
  char *second = chemin_dans(dossier, "texte_genere_5_2.txt");
  const char *const arguments[] = { "-g", "5", "-n", "2", corpus_anglais, NULL };
  char *contenu;

  assert(mkdir(dossier, 0700) == 0);
  assert(lancer(arguments, dossier) == 0);
  contenu = lire_fichier(premier);
  assert(contenu != NULL);
  assert(lancer(arguments, dossier) == 0);
  /* le premier fichier n'a pas ete ecrase, un second a ete cree */
  assert(fichiers_identiques(premier, second));
  free(contenu);
  free(dossier);
  free(premier);
  free(second);
}

static void test_erreurs(void)
{
  char *vide = fichier("vide");
  char *modele = fichier("modele.txt");
  char *sans_parent = fichier("absent/texte.txt");
  char *modele_sans_parent = fichier("absent/modele.txt");
  const char *const aucun[] = { NULL };
  const char *const inconnue[] = { "-z", corpus_anglais, NULL };
  const char *const sans_valeur[] = { corpus_anglais, "-g", NULL };
  const char *const graine_texte[] = { "-g", "abc", corpus_anglais, NULL };
  const char *const graine_negative[] = { "-g", "-1", corpus_anglais, NULL };
  const char *const graine_deux_fois[] = { "-g", "1", "-g", "2", corpus_anglais, NULL };
  const char *const graine_conflit[] = { corpus_anglais, "1", "-g", "2", NULL };
  const char *const zero_phrase[] = { "-n", "0", corpus_anglais, NULL };
  const char *const ordre_3[] = { "-k", "3", corpus_anglais, NULL };
  const char *const trop[] = { corpus_anglais, "1", "2", "x.txt", "y", NULL };
  const char *const sortie_vide[] = { "-o", "", corpus_anglais, NULL };
  const char *const modele_et_corpus[] = { "-m", modele, corpus_anglais, NULL };
  const char *const modele_et_ordre[] = { "-m", modele, "-k", "2", NULL };
  const char *const modele_absent[] = { "-m", "/tmp/modele_absent_markov", NULL };
  const char *const modele_invalide[] = { "-m", "Makefile", NULL };
  const char *const corpus_absent[] = { "/tmp/corpus_absent_markov", NULL };
  const char *const corpus_vide[] = { vide, NULL };
  const char *const sortie_impossible[] = { "-o", sans_parent, corpus_anglais, NULL };
  const char *const sauvegarde_impossible[] = { "-s", modele_sans_parent,
                                                corpus_anglais, NULL };
  const char *const sortie_deux_fois[] = { "-o", "a.txt", "-o", "b.txt",
                                           corpus_anglais, NULL };
  const char *const nombre_deux_fois[] = { "-n", "1", "-n", "2", corpus_anglais, NULL };
  const char *const nombre_conflit[] = { corpus_anglais, "1", "2", "-n", "3", NULL };
  const char *const modele_deux_fois[] = { "-m", modele, "-m", modele, NULL };
  const char *const *const cas[] = {
    aucun, inconnue, sans_valeur, graine_texte, graine_negative,
    graine_deux_fois, graine_conflit, zero_phrase, ordre_3, trop,
    sortie_vide, modele_et_corpus, modele_et_ordre, modele_absent,
    modele_invalide, corpus_absent, corpus_vide, sortie_impossible,
    sauvegarde_impossible, sortie_deux_fois, nombre_deux_fois,
    nombre_conflit, modele_deux_fois
  };
  /* disque plein pour le texte ou pour le modele */
  const char *const texte_disque_plein[] = { "-o", "/dev/full", corpus_anglais, NULL };
  const char *const modele_disque_plein[] = { "-s", "/dev/full", corpus_anglais, NULL };
  struct stat informations;
  size_t i;

  assert(mkdir(vide, 0700) == 0);
  for (i = 0u; i < sizeof(cas) / sizeof(cas[0]); i++) {
    if (lancer(cas[i], NULL) != 1) {
      fprintf(stderr, "\ncas d'erreur %u : code de retour different de 1\n",
              (unsigned int)i);
    }
    assert(lancer(cas[i], NULL) == 1);
  }
  if (stat("/dev/full", &informations) == 0) {
    assert(lancer(texte_disque_plein, NULL) == 1);
    assert(lancer(modele_disque_plein, NULL) == 1);
    assert(stat("/dev/full", &informations) == 0);  /* jamais supprime */
  }
  free(vide);
  free(modele);
  free(sans_parent);
  free(modele_sans_parent);
}


int main(int argc, char *argv[])
{
  struct stat informations;

  programme = realpath((argc > 1) ? argv[1] : "./projet", NULL);
  corpus = realpath("dataset", NULL);
  corpus_anglais = realpath("dataset/en", NULL);
  if (programme == NULL || corpus == NULL || corpus_anglais == NULL
      || stat(programme, &informations) != 0) {
    fprintf(stderr, "test_programme : lancer depuis la racine du projet, "
            "apres make (usage : %s [executable]).\n", argv[0]);
    free(programme);
    free(corpus);
    free(corpus_anglais);
    return EXIT_FAILURE;
  }
  travail = creer_repertoire_temporaire();
  mots_corpus = mots_du_corpus(corpus, false);
  mots_corpus_recursif = mots_du_corpus(corpus, true);

  printf("Tests du programme principal (%s) :\n", programme);
  LANCER_TEST(test_aide);
  LANCER_TEST(test_texte_genere_valide);
  LANCER_TEST(test_ordre_2_et_recursif);
  LANCER_TEST(test_reproductible);
  LANCER_TEST(test_modele_sauvegarde_puis_charge);
  LANCER_TEST(test_syntaxe_historique);
  LANCER_TEST(test_nom_automatique_jamais_ecrase);
  LANCER_TEST(test_erreurs);
  afficher_bilan("programme principal");

  hachage_detruire(&mots_corpus);
  hachage_detruire(&mots_corpus_recursif);
  detruire_repertoire_temporaire(travail);
  free(programme);
  free(corpus);
  free(corpus_anglais);
  return EXIT_SUCCESS;
}
