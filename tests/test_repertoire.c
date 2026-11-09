/**
Fichier test_repertoire.c

Tests unitaires du module repertoire (cf. src/repertoire.h) : iterateur des
mots d'un fichier et construction d'une chaine de Markov a partir d'un
repertoire. Les fichiers sont crees dans des repertoires temporaires.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "outils_tests.h"
#include "../src/repertoire.h"

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/**
 * Ecrit taille octets dans un fichier temporaire et verifie que l'iterateur
 * renvoie exactement les mots attendus. attendu contient les mots separes
 * par des espaces ; un '|' colle a la fin d'un mot indique que ce mot doit
 * terminer une phrase. Exemple : "le chat dort| il pleut|".
 */
static void verifier_lecture(const char *contenu, size_t taille,
                             const char *attendu)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  iterateur_mots_t *it;
  const char *p = attendu;
  const char *mot;
  char *mot_attendu = malloc(strlen(attendu) + 1u);
  size_t longueur;
  bool fin_attendue;
  bool fin = true;

  assert(mot_attendu != NULL);
  ecrire_octets(chemin, contenu, taille);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  while (*p != '\0') {
    while (*p == ' ') {
      p++;
    }
    if (*p != '\0') {
      longueur = 0u;
      while (p[longueur] != ' ' && p[longueur] != '\0') {
        longueur++;
      }
      memcpy(mot_attendu, p, longueur);
      mot_attendu[longueur] = '\0';
      p += longueur;
      fin_attendue = (mot_attendu[longueur - 1u] == '|');
      if (fin_attendue) {
        mot_attendu[longueur - 1u] = '\0';
      }
      mot = iterateur_mots_suivant(it, &fin);
      if (mot == NULL || strcmp(mot, mot_attendu) != 0
          || fin != fin_attendue) {
        fprintf(stderr, "\nattendu \"%s\"%s, obtenu \"%s\"%s\n", mot_attendu,
                fin_attendue ? " (fin de phrase)" : "",
                mot == NULL ? "(NULL)" : mot, fin ? " (fin de phrase)" : "");
      }
      assert(mot != NULL);
      assert(strcmp(mot, mot_attendu) == 0);
      assert(fin == fin_attendue);
    }
  }
  /* plus de mot, et la fonction renvoie ensuite toujours NULL */
  assert(iterateur_mots_suivant(it, &fin) == NULL);
  assert(!fin);
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  assert(iterateur_mots_erreur(it) == 0);
  iterateur_mots_detruire(&it);
  assert(it == NULL);
  free(mot_attendu);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Variante de verifier_lecture pour un contenu sans octet nul. */
static void verifier_texte(const char *contenu, const char *attendu)
{
  verifier_lecture(contenu, strlen(contenu), attendu);
}

/** Description complete attendue d'un mot lu par l'iterateur. */
typedef struct {
  const char *mot;
  const char *graphie;
  bool fin;
  liaison_t liaison;
  ponctuation_t ponctuation;
} lecture_t;

/** Verifie mot, graphie, fin de phrase, liaison et ponctuation. */
static void verifier_details(const char *contenu, const lecture_t *attendus,
                             size_t nb, bool accentue)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  iterateur_mots_t *it;
  const char *mot;
  bool fin;
  size_t i;

  ecrire_fichier(chemin, contenu);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  assert(iterateur_mots_accentue(it) == accentue);
  for (i = 0u; i < nb; i++) {
    mot = iterateur_mots_suivant(it, &fin);
    if (mot == NULL || strcmp(mot, attendus[i].mot) != 0
        || strcmp(iterateur_mots_graphie(it), attendus[i].graphie) != 0
        || fin != attendus[i].fin
        || iterateur_mots_liaison(it) != attendus[i].liaison
        || iterateur_mots_ponctuation(it) != attendus[i].ponctuation) {
      fprintf(stderr, "\nmot %u : attendu \"%s\" (%s, fin %d, liaison %d, "
              "ponctuation %d)\n", (unsigned int)i, attendus[i].mot,
              attendus[i].graphie, attendus[i].fin, attendus[i].liaison,
              attendus[i].ponctuation);
      if (mot != NULL) {
        fprintf(stderr, "        obtenu \"%s\" (%s, fin %d, liaison %d, "
                "ponctuation %d)\n", mot, iterateur_mots_graphie(it), fin,
                iterateur_mots_liaison(it), iterateur_mots_ponctuation(it));
      }
    }
    assert(mot != NULL && strcmp(mot, attendus[i].mot) == 0);
    assert(strcmp(iterateur_mots_graphie(it), attendus[i].graphie) == 0);
    assert(fin == attendus[i].fin);
    assert(iterateur_mots_liaison(it) == attendus[i].liaison);
    assert(iterateur_mots_ponctuation(it) == attendus[i].ponctuation);
  }
  assert(iterateur_mots_suivant(it, NULL) == NULL);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Identifiant d'un mot dont on sait qu'il est dans la table. */
static unsigned int id(const hachage_t *mots, const char *mot)
{
  unsigned int identifiant = hachage_identifiant(mots, mot);

  assert(identifiant != HACHAGE_ABSENT);
  return identifiant;
}


/* ======================================================================== */
/*                          Iterateur : les mots                            */
/* ======================================================================== */

static void test_iterateur_phrases_simples(void)
{
  verifier_texte("Le chat dort. Le chien mange !",
                 "le chat dort| le chien mange|");
  verifier_texte("Quoi ?! Non !!! Si.", "quoi| non| si|");
}

static void test_iterateur_ponctuation_retiree(void)
{
  verifier_texte("Bonjour, monde ; c'est-a-dire (voila) : \"fin\"",
                 "bonjour monde c est a dire voila fin|");
  verifier_texte("_Ne jugez point._ [1] {x} <y> * # @ & %",
                 "ne jugez point| x y|");
  verifier_texte("... debut", "debut|");
}

static void test_iterateur_majuscules(void)
{
  verifier_texte("BONJOUR Monde eT TOI", "bonjour monde et toi|");
}

static void test_iterateur_accents_utf8(void)
{
  /* "Ete a l'ecole, OEuvre CA naif." avec accents, apostrophe typographique
     et ligature, en UTF-8 */
  verifier_texte("\xC3\x89t\xC3\xA9 \xC3\xA0 l\xE2\x80\x99\xC3\xA9" "cole, "
                 "\xC5\x92uvre \xC3\x87" "A na\xC3\xAF" "f.",
                 "ete a l ecole oeuvre ca naif|");
  /* ligature ae, eszett, guillemets francais */
  verifier_texte("c\xC3\xA6sar \xC2\xAB stra\xC3\x9F" "e \xC2\xBB",
                 "caesar strasse|");
}

static void test_iterateur_latin1(void)
{
  /* meme texte encode en Latin-1 (ISO-8859-1) */
  verifier_texte("\xC9t\xE9 \xE0 l'\xE9" "cole, \xC7" "A na\xEF" "f.",
                 "ete a l ecole ca naif|");
  /* octet Latin-1 suivi d'un octet de continuation isole */
  verifier_texte("caf\xE9\x80x", "cafe x|");
}

static void test_iterateur_bom_et_crlf(void)
{
  verifier_texte("\xEF\xBB\xBFTitre\r\n\r\nPremier texte\r\nsuite.",
                 "titre| premier texte suite|");
}

static void test_iterateur_mot_tres_long(void)
{
  const size_t longueur = 100000u;
  char *contenu = malloc(longueur + sizeof(" a"));
  char *attendu = malloc(longueur + sizeof(" a|"));

  assert(contenu != NULL && attendu != NULL);
  memset(contenu, 'Z', longueur);
  strcpy(contenu + longueur, " a");
  memset(attendu, 'z', longueur);
  strcpy(attendu + longueur, " a|");
  verifier_lecture(contenu, strlen(contenu), attendu);
  free(contenu);
  free(attendu);
}


/* ======================================================================== */
/*                       Iterateur : les fins de phrase                     */
/* ======================================================================== */

static void test_iterateur_fin_de_fichier(void)
{
  /* le dernier mot du fichier termine la phrase, meme sans ponctuation */
  verifier_texte("Je compte les jours\nSur mes doigts",
                 "je compte les jours sur mes doigts|");
  verifier_texte("Fin\n\n\n", "fin|");
}

static void test_iterateur_lignes_vides(void)
{
  /* strophes de chanson : seule la ligne vide termine la phrase */
  verifier_texte("I want you\nI want you so bad\n\nShe's so heavy\n",
                 "i want you i want you so bad| she s so heavy|");
  /* ligne ne contenant que des blancs */
  verifier_texte("Titre\n  \t \nTexte", "titre| texte|");
  /* une ligne de tirets n'est pas une ligne vide */
  verifier_texte("Avant\n--\nApres", "avant apres|");
}

static void test_iterateur_abreviations(void)
{
  verifier_texte("M. Dupont parle a J.-J. Rousseau. MM. Durand et "
                 "Martin viennent.",
                 "m dupont parle a j j rousseau| mm durand et martin "
                 "viennent|");
  /* une minuscule isolee suivie d'un point termine bien la phrase */
  verifier_texte("Il a dit a. Puis b", "il a dit a| puis b|");
}

static void test_iterateur_nombres(void)
{
  verifier_texte("Il a 3.5 ans et 2 chats. En 1815. Fin",
                 "il a ans et chats| en| fin|");
}

static void test_iterateur_points_de_suspension(void)
{
  verifier_texte("Attends\xE2\x80\xA6 bon", "attends| bon|");
  verifier_texte("She's so...", "she s so|");
}

static void test_iterateur_sans_mot(void)
{
  verifier_texte("", "");
  verifier_texte(" ... !!! 1234 \n\n -- ", "");
  verifier_lecture("\0\0\0", 3u, "");
}


/* ======================================================================== */
/*            Iterateur : graphies, liaisons, ponctuation, accents          */
/* ======================================================================== */

static void test_iterateur_graphies(void)
{
  /* "Ete a L'Ecole, OEUVRE ca" avec accents, ligature et majuscules */
  const lecture_t attendus[] = {
    { "ete", "\xC3\x89t\xC3\xA9", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "a", "\xC3\xA0", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "l", "L", false, LIAISON_APOSTROPHE, PONCTUATION_AUCUNE },
    { "ecole", "\xC3\x89" "cole", false, LIAISON_VIRGULE, PONCTUATION_AUCUNE },
    { "oeuvre", "\xC5\x92UVRE", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "ca", "\xC3\xA7" "a", true, LIAISON_ESPACE, PONCTUATION_AUCUNE }
  };

  verifier_details("\xC3\x89t\xC3\xA9 \xC3\xA0 L'\xC3\x89" "cole, "
                   "\xC5\x92UVRE \xC3\xA7" "a", attendus, 6u, true);
}

static void test_iterateur_graphie_latin1(void)
{
  /* un texte Latin-1 donne des graphies en UTF-8 */
  const lecture_t attendus[] = {
    { "ete", "\xC3\x89t\xC3\xA9", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  verifier_details("\xC9t\xE9.", attendus, 1u, true);
}

static void test_iterateur_liaisons(void)
{
  /* "C'est peut-etre, dit-il; oui: M. Dupont << bien >> bon ' va-et-vient."
     (apostrophe typographique, trait d'union insecable) */
  const lecture_t attendus[] = {
    { "c", "C", false, LIAISON_APOSTROPHE, PONCTUATION_AUCUNE },
    { "est", "est", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "peut", "peut", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "etre", "\xC3\xAAtre", false, LIAISON_VIRGULE, PONCTUATION_AUCUNE },
    { "dit", "dit", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "il", "il", false, LIAISON_POINT_VIRGULE, PONCTUATION_AUCUNE },
    { "oui", "oui", false, LIAISON_DEUX_POINTS, PONCTUATION_AUCUNE },
    { "m", "M", false, LIAISON_ABREVIATION, PONCTUATION_AUCUNE },
    { "dupont", "Dupont", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "bien", "bien", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "bon", "bon", false, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "va", "va", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "et", "et", false, LIAISON_TRAIT_UNION, PONCTUATION_AUCUNE },
    { "vient", "vient", true, LIAISON_ESPACE, PONCTUATION_POINT }
  };

  verifier_details("C\xE2\x80\x99" "est peut-\xC3\xAAtre, dit-il; oui: "
                   "M. Dupont \xC2\xAB bien \xC2\xBB bon ' va\xE2\x80\x91"
                   "et-vient.", attendus, 14u, true);
}

static void test_iterateur_ponctuations(void)
{
  const lecture_t attendus[] = {
    { "quoi", "Quoi", true, LIAISON_ESPACE, PONCTUATION_INTERROGATION },
    { "non", "Non", true, LIAISON_ESPACE, PONCTUATION_EXCLAMATION },
    { "bon", "Bon", true, LIAISON_ESPACE, PONCTUATION_SUSPENSION },
    { "attends", "Attends", true, LIAISON_ESPACE, PONCTUATION_SUSPENSION },
    { "fin", "Fin", true, LIAISON_ESPACE, PONCTUATION_POINT },
    { "titre", "Titre", true, LIAISON_ESPACE, PONCTUATION_AUCUNE },
    { "suite", "Suite", true, LIAISON_ESPACE, PONCTUATION_AUCUNE }
  };

  verifier_details("Quoi ?! Non !!! Bon... Attends\xE2\x80\xA6 Fin. "
                   "Titre\n\nSuite", attendus, 7u, true);
}

static void test_iterateur_accentue(void)
{
  const lecture_t ascii[] = {
    { "etait", "etait", true, LIAISON_ESPACE, PONCTUATION_AUCUNE }
  };

  verifier_details("etait", ascii, 1u, false);
  /* des accents seulement dans la licence ignoree ne comptent pas */
  verifier_details("etait\n*** END OF THE PROJECT GUTENBERG EBOOK X ***\n"
                   "\xC3\xA9t\xC3\xA9\n", ascii, 1u, false);
  /* ni dans l'en-tete ignore */
  verifier_details("\xC3\xA9t\xC3\xA9\n*** START OF THE PROJECT GUTENBERG "
                   "EBOOK X ***\netait", ascii, 1u, false);
}

static void test_iterateur_gutenberg(void)
{
  verifier_texte("Avant.\n*** START OF THE PROJECT GUTENBERG EBOOK X ***\n"
                 "Texte utile.\n*** END OF THE PROJECT GUTENBERG EBOOK X ***\n"
                 "License words.", "texte utile|");
  verifier_texte("Texte.\nEnd of the Project Gutenberg EBook of X\nlicense",
                 "texte|");
  verifier_texte("Texte.\nEnd of Project Gutenberg's X, by Y\n\n*** END OF "
                 "THIS PROJECT GUTENBERG EBOOK X ***\nlicence", "texte|");
  /* casse et espaces en debut de ligne indifferents */
  verifier_texte("Texte.\n   ***end of this project gutenberg ebook\nfin",
                 "texte|");
  /* pas de marque : tout est lu */
  verifier_texte("Texte.\n*** END OF THE WORLD ***\nfin",
                 "texte| end of the world fin|");
  verifier_texte("Le texte *** END OF THE PROJECT GUTENBERG",
                 "le texte end of the project gutenberg|");
}

static void test_graphie_valide(void)
{
  assert(repertoire_graphie_valide("\xC3\x89t\xC3\xA9", "ete"));
  assert(repertoire_graphie_valide("ETE", "ete"));
  assert(repertoire_graphie_valide("\xC5\x93uvre", "oeuvre"));
  assert(repertoire_graphie_valide("stra\xC3\x9F" "e", "strasse"));
  assert(repertoire_graphie_valide("\xC3\x86sop", "aesop"));
  assert(!repertoire_graphie_valide("\xC3\x89t\xC3\xA9", "et"));
  assert(!repertoire_graphie_valide("\xC3\x89t", "ete"));
  assert(!repertoire_graphie_valide("l'eau", "leau"));
  assert(!repertoire_graphie_valide("", ""));
  assert(!repertoire_graphie_valide("\xC3", "a"));
}


/* ======================================================================== */
/*                           Iterateur : les erreurs                        */
/* ======================================================================== */

static void test_iterateur_fichier_inexistant(void)
{
  errno = 0;
  assert(iterateur_mots_creer("/tmp/fichier_qui_n_existe_pas.txt") == NULL);
  assert(errno == ENOENT);
}

static void test_iterateur_repertoire(void)
{
  char *repertoire = creer_repertoire_temporaire();

  errno = 0;
  assert(iterateur_mots_creer(repertoire) == NULL);
  assert(errno == EISDIR);
  detruire_repertoire_temporaire(repertoire);
}

static void test_iterateur_detruire_en_cours(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "texte.txt");
  iterateur_mots_t *it;

  ecrire_fichier(chemin, "un deux trois quatre");
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  assert(strcmp(iterateur_mots_suivant(it, NULL), "un") == 0);
  /* destruction avant la fin du fichier : pas de fuite */
  iterateur_mots_detruire(&it);
  assert(it == NULL);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                     Chaine de Markov d'un repertoire                     */
/* ======================================================================== */

static void test_markov_poids(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;
  unsigned int le;
  unsigned int chat;
  unsigned int dort;
  unsigned int chien;
  unsigned int mange;

  creer_fichier_dans(repertoire, "a.txt", "Le chat dort. Le chien dort.");
  creer_fichier_dans(repertoire, "b.txt", "Le chat mange.");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_est_finalisee(m));
  assert(markov_nb_etats(m) == 6u);
  assert(hachage_nb_mots(mots) == 6u);
  assert(strcmp(hachage_mot(mots, REPERTOIRE_DEBUT_PHRASE), "") == 0);

  le = id(mots, "le");
  chat = id(mots, "chat");
  dort = id(mots, "dort");
  chien = id(mots, "chien");
  mange = id(mots, "mange");
  /* identifiants dans l'ordre d'apparition */
  assert(le == 1u && chat == 2u && dort == 3u && chien == 4u && mange == 5u);

  assert(markov_poids(m, REPERTOIRE_DEBUT_PHRASE, le) == 3u);
  assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) == 1u);
  assert(markov_poids(m, le, chat) == 2u);
  assert(markov_poids(m, le, chien) == 1u);
  assert(markov_poids(m, chat, dort) == 1u);
  assert(markov_poids(m, chat, mange) == 1u);
  assert(markov_poids(m, chien, dort) == 1u);
  assert(markov_poids(m, dort, REPERTOIRE_DEBUT_PHRASE) == 2u);
  assert(markov_poids(m, mange, REPERTOIRE_DEBUT_PHRASE) == 1u);
  assert(markov_poids(m, dort, le) == 0u);
  assert(markov_poids_total(m, le) == 3u);

  assert(ecart(markov_probabilite(m, le, chat), 2.0 / 3.0) < 1e-12);
  assert(ecart(markov_probabilite(m, le, chien), 1.0 / 3.0) < 1e-12);
  assert(markov_probabilite(m, REPERTOIRE_DEBUT_PHRASE, le) == 1.0);

  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_fichiers_independants(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  /* le premier fichier se termine sans ponctuation : son dernier mot ne
     doit pas etre relie au premier mot du fichier suivant */
  creer_fichier_dans(repertoire, "1.txt", "Fin du premier");
  creer_fichier_dans(repertoire, "2.txt", "Debut du second.");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_poids(m, id(mots, "premier"), id(mots, "debut")) == 0u);
  assert(markov_poids(m, id(mots, "premier"), REPERTOIRE_DEBUT_PHRASE) == 1u);
  assert(markov_poids(m, REPERTOIRE_DEBUT_PHRASE, id(mots, "debut")) == 1u);
  /* "du" est suivi de "premier" puis de "second" */
  assert(markov_nb_voisins(m, id(mots, "du")) == 2u);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_ordre_des_fichiers(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  /* cree dans le desordre : lu dans l'ordre alphabetique des noms */
  creer_fichier_dans(repertoire, "c.txt", "Gamma.");
  creer_fichier_dans(repertoire, "a.txt", "Alpha.");
  creer_fichier_dans(repertoire, "b.txt", "Beta.");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(strcmp(hachage_mot(mots, 1u), "alpha") == 0);
  assert(strcmp(hachage_mot(mots, 2u), "beta") == 0);
  assert(strcmp(hachage_mot(mots, 3u), "gamma") == 0);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_fichiers_ignores(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sous_repertoire = chemin_dans(repertoire, "sous");
  char *lien = chemin_dans(repertoire, "lien_casse.txt");
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  creer_fichier_dans(repertoire, "texte.txt", "Visible.");
  creer_fichier_dans(repertoire, ".cache", "Cache.");
  assert(mkdir(sous_repertoire, 0700) == 0);
  creer_fichier_dans(sous_repertoire, "texte.txt", "Souterrain.");
  assert(symlink("/tmp/cible_inexistante_markov", lien) == 0);
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(hachage_nb_mots(mots) == 2u);
  assert(hachage_contient(mots, "visible"));
  assert(!hachage_contient(mots, "cache"));
  assert(!hachage_contient(mots, "souterrain"));
  markov_detruire(&m);
  hachage_detruire(&mots);
  free(sous_repertoire);
  free(lien);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_repertoire_vide(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  markov_t *m;

  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_nb_etats(m) == 1u);
  assert(hachage_nb_mots(mots) == 1u);
  assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) == 0u);
  markov_detruire(&m);
  hachage_detruire(&mots);

  /* fichiers sans aucun mot */
  mots = hachage_creer(0u);
  creer_fichier_dans(repertoire, "vide.txt", "");
  creer_fichier_dans(repertoire, "ponctuation.txt", "... !!! 42");
  m = repertoire_creer_markov(repertoire, 1u, mots);
  assert(m != NULL);
  assert(markov_nb_etats(m) == 1u);
  assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) == 0u);
  markov_detruire(&m);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_erreurs(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *fichier = chemin_dans(repertoire, "texte.txt");
  char *inexistant = chemin_dans(repertoire, "inexistant");
  hachage_t *mots = hachage_creer(0u);

  creer_fichier_dans(repertoire, "texte.txt", "Un texte.");
  /* repertoire inexistant : NULL, ENOENT et table inchangee */
  errno = 0;
  assert(repertoire_creer_markov(inexistant, 1u, mots) == NULL);
  assert(errno == ENOENT);
  assert(hachage_nb_mots(mots) == 0u);
  /* le chemin designe un fichier */
  errno = 0;
  assert(repertoire_creer_markov(fichier, 1u, mots) == NULL);
  assert(errno == ENOTDIR);
  assert(hachage_nb_mots(mots) == 0u);
  /* fichier illisible (sans objet pour root, qui peut tout lire) */
  if (geteuid() != 0) {
    assert(chmod(fichier, 0000) == 0);
    errno = 0;
    assert(repertoire_creer_markov(repertoire, 1u, mots) == NULL);
    assert(errno == EACCES);
    assert(chmod(fichier, 0600) == 0);
  }
  hachage_detruire(&mots);
  free(fichier);
  free(inexistant);
  detruire_repertoire_temporaire(repertoire);
}

static void test_markov_corpus_reel(void)
{
  /* corpus du projet (si les tests sont lances depuis la racine) */
  const char *corpus = "dataset/en";
  struct stat informations;
  hachage_t *mots;
  markov_t *m;
  unsigned int e;
  unsigned int etat = REPERTOIRE_DEBUT_PHRASE;
  unsigned int nb_phrases = 0u;
  unsigned int i;

  if (stat(corpus, &informations) == 0 && S_ISDIR(informations.st_mode)) {
    mots = hachage_creer(0u);
    m = repertoire_creer_markov(corpus, 2024u, mots);
    assert(m != NULL);
    assert(markov_nb_etats(m) == hachage_nb_mots(mots));
    assert(markov_nb_voisins(m, REPERTOIRE_DEBUT_PHRASE) > 0u);
    assert(hachage_contient(mots, "want") && hachage_contient(mots, "you"));
    /* tout mot a au moins un voisin : on peut toujours se deplacer */
    for (e = 1u; e < markov_nb_etats(m); e++) {
      assert(markov_nb_voisins(m, e) > 0u);
      assert(hachage_mot_valide(hachage_mot(mots, e)));
      assert(hachage_mot(mots, e)[0] != '\0');
    }
    for (i = 0u; i < 20000u; i++) {
      etat = markov_deplacer(m, etat);
      nb_phrases += (etat == REPERTOIRE_DEBUT_PHRASE) ? 1u : 0u;
    }
    assert(nb_phrases > 0u);
    markov_detruire(&m);
    hachage_detruire(&mots);
  }
}

static void test_markov_meme_graine_meme_texte(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots_a = hachage_creer(0u);
  hachage_t *mots_b = hachage_creer(0u);
  markov_t *a;
  markov_t *b;
  unsigned int ea = REPERTOIRE_DEBUT_PHRASE;
  unsigned int eb = REPERTOIRE_DEBUT_PHRASE;
  unsigned int i;

  creer_fichier_dans(repertoire, "a.txt",
                     "Le chat dort. Le chien dort. Le chat mange. Un chat "
                     "mange le chien. Le chien mange un chat.");
  a = repertoire_creer_markov(repertoire, 99u, mots_a);
  b = repertoire_creer_markov(repertoire, 99u, mots_b);
  assert(a != NULL && b != NULL);
  for (i = 0u; i < 1000u; i++) {
    ea = markov_deplacer(a, ea);
    eb = markov_deplacer(b, eb);
    assert(strcmp(hachage_mot(mots_a, ea), hachage_mot(mots_b, eb)) == 0);
  }
  markov_detruire(&a);
  markov_detruire(&b);
  hachage_detruire(&mots_a);
  hachage_detruire(&mots_b);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                        Liste des fichiers                                */
/* ======================================================================== */

/** Verifie la liste des fichiers (chemins relatifs au repertoire). */
static void verifier_liste(const char *repertoire, bool recursif,
                           const char *const *attendus, size_t nb)
{
  size_t nb_fichiers = 99u;
  char **fichiers = repertoire_lister_fichiers(repertoire, recursif, &nb_fichiers);
  char *chemin;
  size_t i;

  assert(fichiers != NULL);
  assert(nb_fichiers == nb);
  for (i = 0u; i < nb; i++) {
    chemin = chemin_dans(repertoire, attendus[i]);
    assert(strcmp(fichiers[i], chemin) == 0);
    free(chemin);
  }
  repertoire_liberer_fichiers(&fichiers, nb_fichiers);
  assert(fichiers == NULL);
}

static void test_lister_fichiers(void)
{
  char *racine = creer_repertoire_temporaire();
  char *sous = chemin_dans(racine, "sous");
  char *profond = chemin_dans(sous, "profond");
  char *cache = chemin_dans(sous, ".cache");
  char *cache_racine = chemin_dans(racine, ".rep_cache");
  char *lien_rep = chemin_dans(racine, "lien_rep");
  char *lien_fichier = chemin_dans(racine, "lien_fichier");
  char *cible = chemin_dans(racine, "a.txt");
  char *vide = chemin_dans(cache_racine, "vide");
  const char *const directs[] = { "a.txt", "b.txt", "lien_fichier" };
  const char *const tous[] = { "a.txt", "b.txt", "lien_fichier",
                               "sous/c.txt", "sous/profond/e.txt" };
  size_t nb = 99u;
  char **fichiers;

  assert(mkdir(sous, 0700) == 0 && mkdir(profond, 0700) == 0);
  assert(mkdir(cache, 0700) == 0 && mkdir(cache_racine, 0700) == 0);
  assert(mkdir(vide, 0700) == 0);
  creer_fichier_dans(racine, "b.txt", "b");
  creer_fichier_dans(racine, "a.txt", "a");
  creer_fichier_dans(racine, ".cache.txt", "x");
  creer_fichier_dans(sous, "c.txt", "c");
  creer_fichier_dans(profond, "e.txt", "e");
  creer_fichier_dans(cache, "d.txt", "d");
  creer_fichier_dans(cache_racine, "f.txt", "f");
  assert(symlink(sous, lien_rep) == 0);       /* lien vers un repertoire */
  assert(symlink(cible, lien_fichier) == 0);  /* lien vers un fichier */

  verifier_liste(racine, false, directs, 3u);
  verifier_liste(racine, true, tous, 5u);
  /* repertoire vide : tableau vide mais non NULL */
  verifier_liste(vide, true, tous, 0u);
  errno = 0;
  assert(repertoire_lister_fichiers(cible, false, &nb) == NULL);
  assert(errno == ENOTDIR);
  errno = 0;
  assert(repertoire_lister_fichiers("/tmp/repertoire_absent_markov", true,
                                    &nb) == NULL);
  assert(errno == ENOENT);
  /* sous-repertoire illisible : genant seulement en mode recursif */
  if (geteuid() != 0) {
    assert(chmod(sous, 0000) == 0);
    verifier_liste(racine, false, directs, 3u);
    errno = 0;
    fichiers = repertoire_lister_fichiers(racine, true, &nb);
    assert(fichiers == NULL && errno == EACCES);
    assert(chmod(sous, 0700) == 0);
  }
  free(sous);
  free(profond);
  free(cache);
  free(cache_racine);
  free(lien_rep);
  free(lien_fichier);
  free(cible);
  free(vide);
  detruire_repertoire_temporaire(racine);
}


static void test_lister_profondeur_maximale(void)
{
  char *racine = creer_repertoire_temporaire();
  char *chemin = malloc(strlen(racine) + 2u * 80u + 16u);
  size_t nb = 99u;
  char **fichiers;
  unsigned int niveau;

  /* 70 niveaux de sous-repertoires, un fichier tout au fond */
  assert(chemin != NULL);
  strcpy(chemin, racine);
  for (niveau = 0u; niveau < 70u; niveau++) {
    strcat(chemin, "/d");
    assert(mkdir(chemin, 0700) == 0);
  }
  creer_fichier_dans(chemin, "fond.txt", "Profond.");
  errno = 0;
  fichiers = repertoire_lister_fichiers(racine, true, &nb);
  assert(fichiers == NULL && errno == ELOOP);
  /* sans recursivite, la profondeur n'importe pas */
  fichiers = repertoire_lister_fichiers(racine, false, &nb);
  assert(fichiers != NULL && nb == 0u);
  repertoire_liberer_fichiers(&fichiers, nb);
  free(chemin);
  detruire_repertoire_temporaire(racine);
}


/* ======================================================================== */
/*              Apprentissage : ordre 2, lexique, ponctuation               */
/* ======================================================================== */

static void test_apprendre_ordre_2(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  repertoire_options_t options = { 2u, false, NULL, NULL, NULL };
  markov_t *m;
  unsigned int le;
  unsigned int chat;
  unsigned int chien;
  unsigned int dort;

  options.bigrammes = bigrammes;
  creer_fichier_dans(repertoire, "a.txt", "Le chat dort. Le chien dort.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL && markov_est_finalisee(m));
  le = id(mots, "le");
  chat = id(mots, "chat");
  chien = id(mots, "chien");
  dort = id(mots, "dort");
  /* etats = paires : (0,0) (0,le) (le,chat) (chat,dort) (le,chien)
     (chien,dort) */
  assert(hachage_paires_nb(bigrammes) == 6u);
  assert(markov_nb_etats(m) == 6u);
  assert(hachage_paires_identifiant(bigrammes, 0u, 0u) == 0u);
  assert(markov_poids(m, 0u, hachage_paires_identifiant(bigrammes, 0u, le)) == 2u);
  assert(markov_poids(m, hachage_paires_identifiant(bigrammes, 0u, le),
                      hachage_paires_identifiant(bigrammes, le, chat)) == 1u);
  assert(markov_poids(m, hachage_paires_identifiant(bigrammes, le, chien),
                      hachage_paires_identifiant(bigrammes, chien, dort)) == 1u);
  assert(markov_poids(m, hachage_paires_identifiant(bigrammes, chat, dort), 0u) == 1u);
  assert(markov_nb_voisins(m, hachage_paires_identifiant(bigrammes, 0u, le)) == 2u);
  markov_detruire(&m);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_lexique_ponctuation(void)
{
  char *repertoire = creer_repertoire_temporaire();
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  lexique_t *lexique = lexique_creer();
  markov_t *ponctuation = markov_creer(5u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL };
  markov_t *m;
  unsigned int oui;
  unsigned int non;
  unsigned int titre;

  options.bigrammes = bigrammes;
  options.lexique = lexique;
  options.ponctuation = ponctuation;
  creer_fichier_dans(repertoire, "a.txt", "Oui ? Oui ! Non. Titre\n\nC'\xC3\xA9tait oui.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  oui = id(mots, "oui");
  non = id(mots, "non");
  titre = id(mots, "titre");
  /* ponctuation finale selon le dernier mot (fin sans ponctuation = point) */
  assert(markov_est_finalisee(ponctuation));
  assert(markov_nb_etats(ponctuation)
         == REPERTOIRE_NB_FINS + hachage_nb_mots(mots));
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + oui,
                      PONCTUATION_INTERROGATION) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + oui,
                      PONCTUATION_EXCLAMATION) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + oui,
                      PONCTUATION_POINT) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + non,
                      PONCTUATION_POINT) == 1u);
  assert(markov_poids(ponctuation, REPERTOIRE_NB_FINS + titre,
                      PONCTUATION_POINT) == 1u);
  /* lexique synchronise avec les mots et les paires */
  assert(lexique_est_finalise(lexique));
  assert(lexique_nb_mots(lexique) == hachage_nb_mots(mots));
  assert(lexique_nb_bigrammes(lexique) == hachage_paires_nb(bigrammes));
  assert(strcmp(lexique_variante(lexique, id(mots, "etait"),
                                 lexique_variante_mot(lexique, id(mots, "etait"))),
                "\xC3\xA9tait") == 0);
  assert(lexique_liaison(lexique, hachage_paires_identifiant(
                           bigrammes, id(mots, "c"), id(mots, "etait")))
         == LIAISON_APOSTROPHE);
  markov_detruire(&m);
  markov_detruire(&ponctuation);
  lexique_detruire(&lexique);
  hachage_paires_detruire(&bigrammes);
  hachage_detruire(&mots);
  detruire_repertoire_temporaire(repertoire);
}

static void test_apprendre_recursif(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sous = chemin_dans(repertoire, "sous");
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, true, NULL, NULL, NULL };
  markov_t *m;

  assert(mkdir(sous, 0700) == 0);
  creer_fichier_dans(repertoire, "a.txt", "Surface.");
  creer_fichier_dans(sous, "b.txt", "Profondeur.");
  m = repertoire_apprendre(repertoire, 3u, mots, &options);
  assert(m != NULL);
  assert(hachage_contient(mots, "surface"));
  assert(hachage_contient(mots, "profondeur"));
  markov_detruire(&m);
  hachage_detruire(&mots);
  free(sous);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_graphie_sans_mot(void)
{
  iterateur_mots_t *it = iterateur_mots_creer("Makefile");

  iterateur_mots_graphie(it);
}

static void violation_liaison_apres_fin(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "a.txt");
  iterateur_mots_t *it;

  ecrire_fichier(chemin, "seul");
  it = iterateur_mots_creer(chemin);
  iterateur_mots_suivant(it, NULL);
  iterateur_mots_suivant(it, NULL);
  iterateur_mots_liaison(it);
}

static void violation_options_nulles(void)
{
  hachage_t *mots = hachage_creer(0u);

  repertoire_apprendre("/tmp", 1u, mots, NULL);
}

static void violation_ordre_3(void)
{
  hachage_t *mots = hachage_creer(0u);
  hachage_paires_t *bigrammes = hachage_paires_creer(0u);
  repertoire_options_t options = { 3u, false, NULL, NULL, NULL };

  options.bigrammes = bigrammes;
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_lexique_sans_bigrammes(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL };

  options.lexique = lexique_creer();
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_ponctuation_non_vide(void)
{
  hachage_t *mots = hachage_creer(0u);
  repertoire_options_t options = { 1u, false, NULL, NULL, NULL };

  options.ponctuation = markov_creer(1u);
  markov_ajouter_etat(options.ponctuation);
  repertoire_apprendre("/tmp", 1u, mots, &options);
}

static void violation_lister_nombre_nul(void)
{
  repertoire_lister_fichiers("/tmp", false, NULL);
}

static void violation_iterateur_chemin_nul(void)
{
  iterateur_mots_creer(NULL);
}

static void violation_iterateur_nul(void)
{
  iterateur_mots_suivant(NULL, NULL);
}

static void violation_iterateur_detruire_nul(void)
{
  iterateur_mots_detruire(NULL);
}

static void violation_iterateur_erreur_nul(void)
{
  iterateur_mots_erreur(NULL);
}

static void violation_markov_chemin_nul(void)
{
  hachage_t *mots = hachage_creer(0u);

  repertoire_creer_markov(NULL, 1u, mots);
}

static void violation_markov_table_nulle(void)
{
  repertoire_creer_markov("/tmp", 1u, NULL);
}

static void violation_markov_table_non_vide(void)
{
  hachage_t *mots = hachage_creer(0u);

  hachage_ajouter(mots, "deja");
  repertoire_creer_markov("/tmp", 1u, mots);
}

static void sans_violation(void)
{
  hachage_t *mots = hachage_creer(0u);
  markov_t *m = repertoire_creer_markov("/tmp/repertoire_absent_markov", 1u, mots);

  assert(m == NULL);
  hachage_detruire(&mots);
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_iterateur_chemin_nul));
  assert(viole_contrat(violation_iterateur_nul));
  assert(viole_contrat(violation_iterateur_detruire_nul));
  assert(viole_contrat(violation_iterateur_erreur_nul));
  assert(viole_contrat(violation_markov_chemin_nul));
  assert(viole_contrat(violation_markov_table_nulle));
  assert(viole_contrat(violation_markov_table_non_vide));
  assert(viole_contrat(violation_graphie_sans_mot));
  assert(viole_contrat(violation_liaison_apres_fin));
  assert(viole_contrat(violation_options_nulles));
  assert(viole_contrat(violation_ordre_3));
  assert(viole_contrat(violation_lexique_sans_bigrammes));
  assert(viole_contrat(violation_ponctuation_non_vide));
  assert(viole_contrat(violation_lister_nombre_nul));
}


int main(void)
{
  printf("Tests du module repertoire :\n");
  LANCER_TEST(test_iterateur_phrases_simples);
  LANCER_TEST(test_iterateur_ponctuation_retiree);
  LANCER_TEST(test_iterateur_majuscules);
  LANCER_TEST(test_iterateur_accents_utf8);
  LANCER_TEST(test_iterateur_latin1);
  LANCER_TEST(test_iterateur_bom_et_crlf);
  LANCER_TEST(test_iterateur_mot_tres_long);
  LANCER_TEST(test_iterateur_fin_de_fichier);
  LANCER_TEST(test_iterateur_lignes_vides);
  LANCER_TEST(test_iterateur_abreviations);
  LANCER_TEST(test_iterateur_nombres);
  LANCER_TEST(test_iterateur_points_de_suspension);
  LANCER_TEST(test_iterateur_sans_mot);
  LANCER_TEST(test_iterateur_graphies);
  LANCER_TEST(test_iterateur_graphie_latin1);
  LANCER_TEST(test_iterateur_liaisons);
  LANCER_TEST(test_iterateur_ponctuations);
  LANCER_TEST(test_iterateur_accentue);
  LANCER_TEST(test_iterateur_gutenberg);
  LANCER_TEST(test_graphie_valide);
  LANCER_TEST(test_iterateur_fichier_inexistant);
  LANCER_TEST(test_iterateur_repertoire);
  LANCER_TEST(test_iterateur_detruire_en_cours);
  LANCER_TEST(test_markov_poids);
  LANCER_TEST(test_markov_fichiers_independants);
  LANCER_TEST(test_markov_ordre_des_fichiers);
  LANCER_TEST(test_markov_fichiers_ignores);
  LANCER_TEST(test_markov_repertoire_vide);
  LANCER_TEST(test_markov_erreurs);
  LANCER_TEST(test_markov_corpus_reel);
  LANCER_TEST(test_markov_meme_graine_meme_texte);
  LANCER_TEST(test_lister_fichiers);
  LANCER_TEST(test_lister_profondeur_maximale);
  LANCER_TEST(test_apprendre_ordre_2);
  LANCER_TEST(test_apprendre_lexique_ponctuation);
  LANCER_TEST(test_apprendre_recursif);
  LANCER_TEST(test_contrats);
  afficher_bilan("repertoire");
  return EXIT_SUCCESS;
}
