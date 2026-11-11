/**
Fichier test_programme.c

Test du programme principal : lance l'executable (premier argument, par
defaut ./projet) avec differentes options et verifie le texte produit :
une phrase par ligne, qui commence par une majuscule (apres un tiret de
dialogue et un guillemet eventuels), se termine par une ponctuation finale
(suivie eventuellement d'un guillemet) et ne contient que des mots du
corpus. Verifie aussi la reproductibilite (graine), les ordres superieurs,
le premier mot impose, les corpus de plusieurs chemins, la lecture "une
phrase par ligne", le filtre de langue, la sauvegarde et le rechargement
d'un modele, l'ancienne syntaxe et les codes de retour en cas d'erreur.
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
static hachage_t *mots_corpus_anglais = NULL;

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

/* Marques de dialogue des phrases generees, en UTF-8. */
#define TIRET "\xE2\x80\x94 "
#define GO "\xC2\xAB"
#define GF "\xC2\xBB"

/** Indique si la ligne commence par une lettre majuscule, apres un tiret
    de dialogue et un guillemet ouvrant eventuels. */
static bool commence_par_majuscule(const char *ligne)
{
  char lettre[3] = { '\0', '\0', '\0' };
  bool majuscule = false;

  if (strncmp(ligne, TIRET, strlen(TIRET)) == 0) {
    ligne += strlen(TIRET);
  }
  if (strncmp(ligne, GO, strlen(GO)) == 0) {
    ligne += strlen(GO);
  }
  lettre[0] = ligne[0];
  if ((unsigned char)ligne[0] >= 0x80u) {
    lettre[1] = ligne[1];
  }
  if (lexique_graphie_valide(lettre)) {
    majuscule = (lexique_casse(lettre) != CASSE_MINUSCULE);
  }
  return majuscule;
}

/** Indique si la ligne se termine par une ponctuation finale, suivie d'un
    guillemet fermant eventuel. */
static bool termine_par_ponctuation(const char *ligne, size_t longueur)
{
  if (longueur >= 2u && strcmp(ligne + longueur - 2u, GF) == 0) {
    longueur -= 2u;
  }
  return longueur >= 2u && (ligne[longueur - 1u] == '.'
                            || ligne[longueur - 1u] == '!'
                            || ligne[longueur - 1u] == '?');
}

/** Indique si les guillemets de la ligne sont bien formes. */
static bool guillemets_bien_formes(const char *ligne)
{
  bool ouvert = false;
  bool valide = true;
  const char *c;

  for (c = ligne; valide && *c != '\0'; c++) {
    if (strncmp(c, GO, 2u) == 0) {
      valide = !ouvert;
      ouvert = true;
    } else if (strncmp(c, GF, 2u) == 0) {
      valide = ouvert;
      ouvert = false;
    }
  }
  return valide && !ouvert;
}

/** Nombre de lignes d'un fichier qui commencent par un texte donne. */
static unsigned int compter_lignes_commencant(const char *chemin, const char *debut)
{
  char *contenu = lire_fichier(chemin);
  const char *ligne = contenu;
  unsigned int nb = 0u;

  assert(contenu != NULL);
  while (ligne != NULL && *ligne != '\0') {
    nb += (strncmp(ligne, debut, strlen(debut)) == 0) ? 1u : 0u;
    ligne = strchr(ligne, '\n');
    if (ligne != NULL) {
      ligne++;
    }
  }
  free(contenu);
  return nb;
}

/**
 * Verifie un texte genere : nb_phrases lignes, chacune commencant par une
 * majuscule et se terminant par une ponctuation finale (sauf si
 * sans_ponctuation), avec des guillemets bien formes ; tous les mots (au
 * sens de l'iterateur) appartiennent a la table donnee.
 */
static void verifier_texte_lu(const char *chemin, unsigned int nb_phrases,
                              const hachage_t *mots, bool sans_ponctuation)
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
    if (!commence_par_majuscule(ligne) || !guillemets_bien_formes(ligne)
        || (!sans_ponctuation && !termine_par_ponctuation(ligne, strlen(ligne)))) {
      fprintf(stderr, "\nphrase mal formee : \"%s\"\n", ligne);
    }
    assert(commence_par_majuscule(ligne));
    assert(guillemets_bien_formes(ligne));
    assert(sans_ponctuation || termine_par_ponctuation(ligne, strlen(ligne)));
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

/** Verifie un texte genere dont chaque phrase a une ponctuation finale. */
static void verifier_texte_genere(const char *chemin, unsigned int nb_phrases, const hachage_t *mots)
{
  verifier_texte_lu(chemin, nb_phrases, mots, false);
}

/** Indique si un fichier contient un texte. */
static bool contient(const char *chemin, const char *texte)
{
  char *contenu = lire_fichier(chemin);
  bool trouve;

  assert(contenu != NULL);
  trouve = (strstr(contenu, texte) != NULL);
  free(contenu);
  return trouve;
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
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false, false, NULL };
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
  assert(affiche != NULL && contenu != NULL && strstr(affiche, contenu) != NULL);
  free(affiche);
  free(contenu);
  free(texte);
  free(sortie);
}

static void test_ordre_2_et_recursif(void)
{
  char *texte = fichier("texte_k2.txt");
  const char *const arguments[] = { "-k", "2", "-r", "-g", "3", "-n", "300", "-o", texte, corpus, NULL };

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
  const char *const charger[] = { "-m", modele, "-g", "9", "-n", "60", "-o", recharge, NULL };

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
  const char *const options[] = { "-g", "42", "-n", "5", "-o", nouvelle, corpus_anglais, NULL };

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

static void test_ordres_superieurs(void)
{
  char *texte = fichier("texte_k.txt");
  char *modele = fichier("modele_k.txt");
  char *recharge = fichier("recharge_k.txt");
  const char *const ordre_3[] = { "-k", "3", "-g", "5", "-n", "200", "-o", texte, corpus, NULL };
  const char *const ordre_8[] = { "-k", "8", "-g", "5", "-n", "50", "-s",
                                  modele, "-o", texte, corpus, NULL };
  const char *const charger[] = { "-m", modele, "-g", "5", "-n", "50", "-o", recharge, NULL };

  assert(lancer(ordre_3, NULL) == 0);
  verifier_texte_genere(texte, 200u, mots_corpus);
  assert(lancer(ordre_8, NULL) == 0);
  verifier_texte_genere(texte, 50u, mots_corpus);
  assert(lancer(charger, NULL) == 0);
  assert(fichiers_identiques(texte, recharge));
  free(texte);
  free(modele);
  free(recharge);
}

static void test_premier_mot(void)
{
  char *texte = fichier("premier.txt");
  char *sortie = fichier("sortie.txt");
  char *modele = fichier("modele_p.txt");
  char *recharge = fichier("recharge_p.txt");
  /* "Marius" commence des phrases ; "fauteuil" (presque) jamais ; "ete"
     s'ecrit avec ses accents */
  const char *const marius[] = { "-p", "Marius", "-g", "1", "-n", "30", "-o", texte, corpus, NULL };
  const char *const fauteuil[] = { "-k", "2", "-p", "FAUTEUIL", "-g", "1",
                                   "-n", "30", "-s", modele, "-o", texte,
                                   corpus, NULL };
  const char *const charge[] = { "-m", modele, "-p", "fauteuil", "-g", "1",
                                 "-n", "30", "-o", recharge, NULL };
  const char *const ete[] = { "-k", "3", "-p", "ete", "-g", "1", "-n", "30",
                              "-o", texte, corpus, NULL };

  assert(lancer(marius, NULL) == 0);
  verifier_texte_genere(texte, 30u, mots_corpus);
  assert(compter_lignes_commencant(texte, "Marius") == 30u);
  assert(lancer(fauteuil, NULL) == 0);
  verifier_texte_genere(texte, 30u, mots_corpus);
  assert(compter_lignes_commencant(texte, "Fauteuil") == 30u);
  /* le modele recharge donne les memes phrases depuis le meme mot */
  assert(lancer(charge, NULL) == 0);
  assert(fichiers_identiques(texte, recharge));
  assert(lancer(ete, NULL) == 0);
  verifier_texte_genere(texte, 30u, mots_corpus);
  assert(compter_lignes_commencant(texte, "\xC3\x89t\xC3\xA9") == 30u);
  free(texte);
  free(sortie);
  free(modele);
  free(recharge);
}

static void test_plusieurs_chemins(void)
{
  char *texte = fichier("plusieurs.txt");
  char *sortie = fichier("sortie.txt");
  char *dylan = chemin_dans(corpus_anglais, "bobdylan_iwysb.txt");
  char *beatles = chemin_dans(corpus_anglais, "thebeatles_iwy.txt");
  const char *const deux_fichiers[] = { "-g", "2", "-n", "20", "-o", texte,
                                        dylan, beatles, NULL };
  const char *const un_fichier[] = { "-g", "2", "-n", "20", "-o", texte, dylan, NULL };

  /* deux fichiers donnes un par un : comme le repertoire qui les contient
     (memes fichiers, meme ordre) */
  assert(lancer(deux_fichiers, NULL) == 0);
  verifier_texte_genere(texte, 20u, mots_corpus_anglais);
  assert(contient(sortie, "2 fichier(s)"));
  assert(contient(sortie, "Langue du corpus : anglais"));
  assert(lancer(un_fichier, NULL) == 0);
  verifier_texte_genere(texte, 20u, mots_corpus_anglais);
  assert(contient(sortie, "1 fichier(s)"));
  free(texte);
  free(sortie);
  free(dylan);
  free(beatles);
}

static void test_phrase_par_ligne(void)
{
  char *texte = fichier("chansons.txt");
  const char *const chansons[] = { "-l", "-k", "2", "-g", "4", "-n", "60",
                                   "-o", texte, corpus_anglais, NULL };

  /* une phrase par vers : des lignes peuvent finir sans ponctuation */
  assert(lancer(chansons, NULL) == 0);
  verifier_texte_lu(texte, 60u, mots_corpus_anglais, true);
  free(texte);
}

static void test_filtre_de_langue(void)
{
  char *modele = fichier("modele_filtre.txt");
  char *modele_tout = fichier("modele_tout.txt");
  char *texte = fichier("texte_filtre.txt");
  char *sortie = fichier("sortie.txt");
  /* corpus francais et chansons anglaises : l'anglais est ecarte */
  const char *const filtre[] = { "-g", "1", "-n", "5", "-s", modele, "-o",
                                 texte, corpus, corpus_anglais, NULL };
  const char *const tout[] = { "-t", "-g", "1", "-n", "5", "-s", modele_tout,
                               "-o", texte, corpus, corpus_anglais, NULL };

  assert(lancer(filtre, NULL) == 0);
  assert(contient(sortie, "Langue du corpus : francais"));
  assert(contient(sortie, "ecartee(s)"));
  /* dans le modele, la ligne d'un mot commence par sa cle : "shyness"
     (notes anglaises de contes.txt) et "yeah" (qui n'apparait que dans
     les chansons) sont absents */
  assert(!contient(modele, "\nshyness ") && !contient(modele, "\nyeah "));
  assert(lancer(tout, NULL) == 0);
  assert(!contient(sortie, "Langue du corpus"));
  assert(contient(modele_tout, "\nshyness ") && contient(modele_tout, "\nyeah "));
  free(modele);
  free(modele_tout);
  free(texte);
  free(sortie);
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
  const char *const ordre_9[] = { "-k", "9", corpus_anglais, NULL };
  const char *const ordre_0[] = { "-k", "0", corpus_anglais, NULL };
  const char *const mot_absent[] = { "-p", "xyzzy", corpus_anglais, NULL };
  const char *const mot_invalide[] = { "-p", "l'eau", corpus_anglais, NULL };
  const char *const mot_vide[] = { "-p", "", corpus_anglais, NULL };
  const char *const modele_et_lignes[] = { "-m", modele, "-l", NULL };
  const char *const modele_et_tout[] = { "-m", modele, "-t", NULL };
  const char *const second_chemin_absent[] = { corpus_anglais, "/tmp/absent_markov", NULL };
  const char *const tube[] = { "/dev/null", NULL };
  const char *const trop[] = { corpus_anglais, "1", "2", "x.txt", "y", NULL };
  const char *const sortie_vide[] = { "-o", "", corpus_anglais, NULL };
  const char *const modele_et_corpus[] = { "-m", modele, corpus_anglais, NULL };
  const char *const modele_et_ordre[] = { "-m", modele, "-k", "2", NULL };
  const char *const modele_absent[] = { "-m", "/tmp/modele_absent_markov", NULL };
  const char *const modele_invalide[] = { "-m", "Makefile", NULL };
  const char *const corpus_absent[] = { "/tmp/corpus_absent_markov", NULL };
  const char *const corpus_vide[] = { vide, NULL };
  const char *const sortie_impossible[] = { "-o", sans_parent, corpus_anglais, NULL };
  const char *const sauvegarde_impossible[] = { "-s", modele_sans_parent, corpus_anglais, NULL };
  const char *const sortie_deux_fois[] = { "-o", "a.txt", "-o", "b.txt", corpus_anglais, NULL };
  const char *const nombre_deux_fois[] = { "-n", "1", "-n", "2", corpus_anglais, NULL };
  const char *const nombre_conflit[] = { corpus_anglais, "1", "2", "-n", "3", NULL };
  const char *const modele_deux_fois[] = { "-m", modele, "-m", modele, NULL };
  const char *const *const cas[] = {
    aucun, inconnue, sans_valeur, graine_texte, graine_negative,
    graine_deux_fois, graine_conflit, zero_phrase, ordre_9, ordre_0,
    mot_absent, mot_invalide, mot_vide, modele_et_lignes, modele_et_tout,
    second_chemin_absent, tube, trop,
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
      fprintf(stderr, "\ncas d'erreur %u : code de retour different de 1\n", (unsigned int)i);
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
  mots_corpus_anglais = mots_du_corpus(corpus_anglais, false);

  printf("Tests du programme principal (%s) :\n", programme);
  LANCER_TEST(test_aide);
  LANCER_TEST(test_texte_genere_valide);
  LANCER_TEST(test_ordre_2_et_recursif);
  LANCER_TEST(test_reproductible);
  LANCER_TEST(test_modele_sauvegarde_puis_charge);
  LANCER_TEST(test_syntaxe_historique);
  LANCER_TEST(test_nom_automatique_jamais_ecrase);
  LANCER_TEST(test_ordres_superieurs);
  LANCER_TEST(test_premier_mot);
  LANCER_TEST(test_plusieurs_chemins);
  LANCER_TEST(test_phrase_par_ligne);
  LANCER_TEST(test_filtre_de_langue);
  LANCER_TEST(test_erreurs);
  afficher_bilan("programme principal");

  hachage_detruire(&mots_corpus);
  hachage_detruire(&mots_corpus_recursif);
  hachage_detruire(&mots_corpus_anglais);
  detruire_repertoire_temporaire(travail);
  free(programme);
  free(corpus);
  free(corpus_anglais);
  return EXIT_SUCCESS;
}
