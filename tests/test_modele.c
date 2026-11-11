/**
Fichier test_modele.c

Tests unitaires du module modele (cf. src/modele.h) : generation de
phrases lisibles (accents, homographes, liaisons, ponctuation interne,
dialogues, parentheses), ordres 1 a MODELE_ORDRE_MAX, chaine de repli,
premier mot impose, corpus de plusieurs chemins, lecture "une phrase par
ligne", filtre de langue, sauvegarde, chargement (formats 2 et 3) et refus
des fichiers invalides.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include "outils_tests.h"
#include "../src/modele.h"
#include "../src/repertoire.h"
#include "../src/langue.h"
#include "../src/sauvegarde.h"

/** Modele sauvegarde attendu pour le corpus "Le chat dort." (ordre 1). */
static const char MODELE_VALIDE[] =
  "modele-markov 3\n"
  "ordre 1\n"
  "repli 0\n"
  "mots 4\n"
  "- - 0 0\n"
  "le 0 0 1 le\n"
  "chat 0 0 1 chat\n"
  "dort 0 0 1 dort\n"
  "bigrammes 4\n"
  "0 0 - - 0 0\n"
  "0 1 - - 0 0\n"
  "1 2 - 0 0 0\n"
  "2 3 - 0 0 0\n"
  "contextes 0\n"
  "chaine 4\n"
  "1 1 1\n"
  "1 2 1\n"
  "1 3 1\n"
  "1 0 1\n"
  "ponctuation 9\n"
  "0\n0\n0\n0\n0\n0\n0\n0\n"
  "1 0 1\n"
  "fin\n";

/** Le meme modele, au format de la version 2 (ni repli ni contextes). */
static const char MODELE_VERSION_2[] =
  "modele-markov 2\n"
  "ordre 1\n"
  "mots 4\n"
  "- - 0 0\n"
  "le 0 0 1 le\n"
  "chat 0 0 1 chat\n"
  "dort 0 0 1 dort\n"
  "bigrammes 4\n"
  "0 0 - - 0 0\n"
  "0 1 - - 0 0\n"
  "1 2 - 0 0 0\n"
  "2 3 - 0 0 0\n"
  "chaine 4\n"
  "1 1 1\n"
  "1 2 1\n"
  "1 3 1\n"
  "1 0 1\n"
  "ponctuation 9\n"
  "0\n0\n0\n0\n0\n0\n0\n0\n"
  "1 0 1\n"
  "fin\n";

/** Modele sauvegarde attendu pour le meme corpus, a l'ordre 3. */
static const char MODELE_VALIDE_3[] =
  "modele-markov 3\n"
  "ordre 3\n"
  "repli 0\n"
  "mots 4\n"
  "- - 0 0\n"
  "le 0 0 1 le\n"
  "chat 0 0 1 chat\n"
  "dort 0 0 1 dort\n"
  "bigrammes 4\n"
  "0 0 - - 0 0\n"
  "0 1 - - 0 0\n"
  "1 2 - 0 0 0\n"
  "2 3 - 0 0 0\n"
  "contextes 0\n"
  "etats 4\n"
  "0 0 0\n"
  "0 0 1\n"
  "0 1 2\n"
  "1 2 3\n"
  "chaine 4\n"
  "1 1 1\n"
  "1 2 1\n"
  "1 3 1\n"
  "1 0 1\n"
  "ponctuation 9\n"
  "0\n0\n0\n0\n0\n0\n0\n0\n"
  "1 0 1\n"
  "fin\n";

/** Modele sauvegarde attendu pour le meme corpus, a l'ordre 4 avec un repli a l'ordre 3. */
static const char MODELE_VALIDE_4_3[] =
  "modele-markov 3\n"
  "ordre 4\n"
  "repli 3\n"
  "mots 4\n"
  "- - 0 0\n"
  "le 0 0 1 le\n"
  "chat 0 0 1 chat\n"
  "dort 0 0 1 dort\n"
  "bigrammes 4\n"
  "0 0 - - 0 0\n"
  "0 1 - - 0 0\n"
  "1 2 - 0 0 0\n"
  "2 3 - 0 0 0\n"
  "contextes 0\n"
  "etats 4\n"
  "0 0 0 0\n"
  "0 0 0 1\n"
  "0 0 1 2\n"
  "0 1 2 3\n"
  "chaine 4\n"
  "1 1 1\n"
  "1 2 1\n"
  "1 3 1\n"
  "1 0 1\n"
  "etats_repli 4\n"
  "0 0 0\n"
  "0 0 1\n"
  "0 1 2\n"
  "1 2 3\n"
  "chaine_repli 4\n"
  "1 1 1\n"
  "1 2 1\n"
  "1 3 1\n"
  "1 0 1\n"
  "ponctuation 9\n"
  "0\n0\n0\n0\n0\n0\n0\n0\n"
  "1 0 1\n"
  "fin\n";

/** Corpus d'homographes : "la" (article) apres "Elle est", mais "la" avec
    accent grave (adverbe) apres "Qui est" et "Il est". */
static const char CORPUS_HOMOGRAPHES[] =
  "Qui est l\xC3\xA0 ? Elle est la reine. Il est l\xC3\xA0.";

/** Modele sauvegarde attendu pour CORPUS_HOMOGRAPHES, a l'ordre 2 avec un
    repli a l'ordre 1 : le bigramme (est, la) choisit l'accent grave, le
    contexte (elle, (est, la)) est une exception. */
static const char MODELE_VALIDE_REPLI[] =
  "modele-markov 3\n"
  "ordre 2\n"
  "repli 1\n"
  "mots 7\n"
  "- - 0 0\n"
  "qui 0 0 1 qui\n"
  "est 0 0 1 est\n"
  "la 0 0 2 l\xC3\xA0 la\n"
  "elle 0 0 1 elle\n"
  "reine 0 0 1 reine\n"
  "il 0 0 1 il\n"
  "bigrammes 9\n"
  "0 0 - - 0 0\n"
  "0 1 0 - 0 0\n"
  "1 2 0 0 0 0\n"
  "2 3 0 0 0 0\n"
  "0 4 0 - 0 0\n"
  "4 2 0 0 0 0\n"
  "3 5 0 0 0 0\n"
  "0 6 0 - 0 0\n"
  "6 2 0 0 0 0\n"
  "contextes 1\n"
  "4 3 1\n"
  "chaine 9\n"
  "3 1 1 4 1 7 1\n"
  "1 2 1\n"
  "1 3 1\n"
  "2 0 2 6 1\n"
  "1 5 1\n"
  "1 3 1\n"
  "1 0 1\n"
  "1 8 1\n"
  "1 3 1\n"
  "chaine_repli 7\n"
  "3 1 1 4 1 6 1\n"
  "1 2 1\n"
  "1 3 3\n"
  "2 0 2 5 1\n"
  "1 2 1\n"
  "1 0 1\n"
  "1 2 1\n"
  "ponctuation 12\n"
  "0\n0\n0\n0\n0\n0\n0\n0\n"
  "2 0 1 2 1\n"
  "0\n"
  "1 0 1\n"
  "0\n"
  "fin\n";

/* Marques de dialogue des phrases generees, en UTF-8. */
#define TIRET "\xE2\x80\x94 "
#define GO "\xC2\xAB"
#define GF "\xC2\xBB"

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/** Apprend un modele sur un corpus d'un seul fichier, avec des options. */
static modele_t *apprendre_avec(const char *contenu,
                                const modele_options_t *options,
                                unsigned int graine)
{
  char *repertoire = creer_repertoire_temporaire();
  const char *chemin = repertoire;
  modele_t *m;

  creer_fichier_dans(repertoire, "corpus.txt", contenu);
  m = modele_apprendre_corpus(&chemin, 1u, graine, options, NULL);
  assert(m != NULL);
  detruire_repertoire_temporaire(repertoire);
  return m;
}

/** Apprend un modele sur un corpus d'un seul fichier. */
static modele_t *apprendre_texte(const char *contenu, unsigned int ordre, unsigned int graine)
{
  modele_options_t options = { 1u, false, false, false, 0u };

  options.ordre = ordre;
  return apprendre_avec(contenu, &options, graine);
}

/** Copie de texte ou la premiere occurrence de ancien devient nouveau. */
static char *remplacer(const char *texte, const char *ancien, const char *nouveau)
{
  const char *position = strstr(texte, ancien);
  size_t avant;
  char *resultat;

  assert(position != NULL);
  avant = (size_t)(position - texte);
  resultat = malloc(strlen(texte) - strlen(ancien) + strlen(nouveau) + 1u);
  assert(resultat != NULL);
  memcpy(resultat, texte, avant);
  strcpy(resultat + avant, nouveau);
  strcat(resultat, position + strlen(ancien));
  return resultat;
}

/** Charge un modele ecrit dans un fichier temporaire. */
static modele_t *charger_texte(const char *contenu, size_t taille, int *erreur)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "modele.txt");
  modele_t *m;

  ecrire_octets(chemin, contenu, taille);
  errno = 0;
  m = modele_charger(chemin, 1u);
  *erreur = errno;
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
  return m;
}

/** Verifie qu'une version modifiee d'un modele valide est refusee. */
static void verifier_refus_dans(const char *modele, const char *ancien, const char *nouveau)
{
  char *modifie = remplacer(modele, ancien, nouveau);
  int erreur = 0;
  modele_t *m = charger_texte(modifie, strlen(modifie), &erreur);

  if (m != NULL || erreur != EINVAL) {
    fprintf(stderr, "\nmodification acceptee : \"%s\" -> \"%s\"\n", ancien, nouveau);
  }
  assert(m == NULL);
  assert(erreur == EINVAL);
  free(modifie);
}

/** Verifie qu'une version modifiee du modele d'ordre 1 est refusee. */
static void verifier_refus(const char *ancien, const char *nouveau)
{
  verifier_refus_dans(MODELE_VALIDE, ancien, nouveau);
}

/** Verifie qu'une version modifiee du modele d'ordre 3 est refusee. */
static void verifier_refus_3(const char *ancien, const char *nouveau)
{
  verifier_refus_dans(MODELE_VALIDE_3, ancien, nouveau);
}

/** Verifie qu'une version modifiee du modele d'ordre 4 (repli a l'ordre 3) est refusee. */
static void verifier_refus_4_3(const char *ancien, const char *nouveau)
{
  verifier_refus_dans(MODELE_VALIDE_4_3, ancien, nouveau);
}

/** Verifie qu'une version modifiee du modele des homographes (ordre 2,
    repli a l'ordre 1, un contexte) est refusee. */
static void verifier_refus_repli(const char *ancien, const char *nouveau)
{
  verifier_refus_dans(MODELE_VALIDE_REPLI, ancien, nouveau);
}

/**
 * Verifie la forme d'une phrase generee et que tous ses mots sont du
 * modele : guillemets et parentheses bien formes ; au debut, un tiret de
 * dialogue, une parenthese et un guillemet ouvrants eventuels, puis le
 * premier mot, avec une majuscule ; a la fin, une ponctuation finale
 * (facultative si sans_ponctuation) suivie de marques fermantes
 * eventuelles.
 */
static void verifier_phrase_lue(const modele_t *m, const char *phrase, bool sans_ponctuation)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "phrase.txt");
  iterateur_mots_t *it;
  const char *mot;
  const char *debut = apres_marques_ouvrantes(phrase);
  bool fin_phrase;
  unsigned int nb_mots = 0u;

  assert(strlen(phrase) >= 1u && strchr(phrase, '\n') == NULL);
  if (!marques_bien_formees(phrase) || !(sans_ponctuation || termine_par_ponctuation(phrase))) {
    fprintf(stderr, "\nphrase mal formee : \"%s\"\n", phrase);
  }
  assert(marques_bien_formees(phrase));
  assert(sans_ponctuation || termine_par_ponctuation(phrase));
  ecrire_fichier(chemin, phrase);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  while ((mot = iterateur_mots_suivant(it, &fin_phrase)) != NULL) {
    assert(hachage_contient(modele_mots(m), mot));
    if (nb_mots == 0u) {
      /* le premier mot suit directement les marques, avec une majuscule */
      assert(strncmp(debut, iterateur_mots_graphie(it), strlen(iterateur_mots_graphie(it))) == 0);
      assert(lexique_casse(iterateur_mots_graphie(it)) != CASSE_MINUSCULE);
    }
    nb_mots++;
  }
  assert(nb_mots > 0u);
  assert(iterateur_mots_erreur(it) == 0);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Verifie une phrase qui doit se terminer par une ponctuation finale. */
static void verifier_phrase(const modele_t *m, const char *phrase)
{
  verifier_phrase_lue(m, phrase, false);
}

/** Cle (mot de l'iterateur) du premier mot d'une phrase (a liberer). */
static char *premier_mot(const char *phrase)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "phrase.txt");
  iterateur_mots_t *it;
  const char *mot;
  char *copie;

  ecrire_fichier(chemin, phrase);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  mot = iterateur_mots_suivant(it, NULL);
  assert(mot != NULL);
  copie = malloc(strlen(mot) + 1u);
  assert(copie != NULL);
  strcpy(copie, mot);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
  return copie;
}

/** Indique si une phrase fait partie d'une liste (terminee par NULL). */
static bool dans_la_liste(const char *phrase, const char *const *liste)
{
  bool trouvee = false;

  while (!trouvee && *liste != NULL) {
    trouvee = (strcmp(phrase, *liste) == 0);
    liste++;
  }
  return trouvee;
}

/**
 * Genere nb phrases (depuis le mot donne, ou d'un debut de phrase s'il vaut
 * HACHAGE_ABSENT) et verifie qu'elles font toutes partie de la liste
 * attendue, et que chaque phrase de la liste a ete obtenue.
 */
static void verifier_phrases_possibles(modele_t *m, unsigned int mot,
                                       const char *const *attendues, unsigned int nb)
{
  bool vues[16] = { false };
  char *phrase;
  unsigned int i;
  unsigned int k;

  for (i = 0u; i < nb; i++) {
    phrase = (mot == HACHAGE_ABSENT) ? modele_generer_phrase(m)
                                     : modele_generer_phrase_depuis(m, mot);
    assert(phrase != NULL);
    if (!dans_la_liste(phrase, attendues)) {
      fprintf(stderr, "\nphrase inattendue (ordre %u) : \"%s\"\n",
              modele_ordre(m), phrase);
    }
    assert(dans_la_liste(phrase, attendues));
    for (k = 0u; attendues[k] != NULL; k++) {
      vues[k] = vues[k] || (strcmp(phrase, attendues[k]) == 0);
    }
    free(phrase);
  }
  for (k = 0u; attendues[k] != NULL; k++) {
    if (!vues[k]) {
      fprintf(stderr, "\nphrase jamais obtenue (ordre %u) : \"%s\"\n",
              modele_ordre(m), attendues[k]);
    }
    assert(vues[k]);
  }
}

/** Les deux chaines ont exactement les memes transitions. */
static void verifier_chaines_egales(const markov_t *a, const markov_t *b)
{
  unsigned int e;
  unsigned int r;
  unsigned int poids_a;
  unsigned int poids_b;

  assert(markov_nb_etats(a) == markov_nb_etats(b));
  for (e = 0u; e < markov_nb_etats(a); e++) {
    assert(markov_nb_voisins(a, e) == markov_nb_voisins(b, e));
    for (r = 0u; r < markov_nb_voisins(a, e); r++) {
      assert(markov_voisin(a, e, r, &poids_a) == markov_voisin(b, e, r, &poids_b));
      assert(poids_a == poids_b);
    }
  }
}

/** Corpus de test avec accents, elisions, majuscules et ponctuation. */
static const char CORPUS_LISIBLE[] =
  "C'\xC3\xA9tait l'\xC3\xA9t\xC3\xA9. Il a dit : peut-\xC3\xAAtre ! "
  "M. Dupont va \xC3\xA0 Paris ? Oui, monsieur.";

/** Phrases que les chaines d'ordre 2 ou plus doivent reproduire a l'identique. */
static const char *const PHRASES_LISIBLES[] = {
  "C'\xC3\xA9tait l'\xC3\xA9t\xC3\xA9.",
  "Il a dit: peut-\xC3\xAAtre!",
  "M. Dupont va \xC3\xA0 Paris?",
  "Oui, monsieur.",
  NULL
};

/** Corpus de dialogues : tirets "--" des livres du projet Gutenberg,
    guillemets francais et droits. */
static const char CORPUS_DIALOGUES[] =
  "--Oui, dit-il.\n"
  "--Non, dit-elle.\n"
  "Il dit: \xC2\xABViens ici!\xC2\xBB Puis il sortit.\n"
  "Elle cria: \"Au secours!\"\n";

/** Corpus de ponctuation interne : '!', '?', "..." et point d'abreviation
    suivis d'un mot en minuscules, parentheses. */
static const char CORPUS_PONCTUATION_INTERNE[] =
  "Ah! dit-il (en riant). \xC2\xAB" "D'o\xC3\xB9 viens-tu?\xC2\xBB me "
  "cria-t-elle. Il attendit... puis il partit. Il dit (etc.) et partit.";

/** Phrases que les chaines d'ordre 2 ou plus doivent en tirer : le signe
    d'une ponctuation interne precede les marques fermantes, une marque dont
    le contenu commence par une minuscule se ferme avant la ponctuation
    finale. */
static const char *const PHRASES_PONCTUATION_INTERNE[] = {
  "Ah! dit-il (en riant).",
  GO "D'o\xC3\xB9 viens-tu?" GF " me cria-t-elle.",
  "Il attendit... puis il partit.",
  "Il dit (etc.) et partit.",
  NULL
};

/** Phrases que les chaines d'ordre 2 ou plus doivent en tirer. */
static const char *const PHRASES_DIALOGUES[] = {
  TIRET "Oui, dit-il.",
  TIRET "Non, dit-elle.",
  "Il dit: " GO "Viens ici!" GF,
  "Puis il sortit.",
  "Elle cria: " GO "Au secours!" GF,
  NULL
};


/* ======================================================================== */
/*                               Generation                                 */
/* ======================================================================== */

static void test_generation_lisible(void)
{
  modele_t *m;
  char *phrase;
  unsigned int ordre;
  unsigned int i;

  /* chaque phrase du corpus est unique : a partir de l'ordre 2, la chaine
     les reproduit exactement, avec accents, elisions et liaisons */
  for (ordre = 2u; ordre <= MODELE_ORDRE_MAX; ordre++) {
    m = apprendre_texte(CORPUS_LISIBLE, ordre, 11u);
    assert(modele_ordre(m) == ordre);
    assert(modele_peut_generer(m));
    verifier_phrases_possibles(m, HACHAGE_ABSENT, PHRASES_LISIBLES, 80u);
    for (i = 0u; i < 20u; i++) {
      phrase = modele_generer_phrase(m);
      verifier_phrase(m, phrase);
      free(phrase);
    }
    modele_detruire(&m);
    assert(m == NULL);
  }
}

static void test_ordre_superieur_moins_de_melanges(void)
{
  const char *const corpus = "Le chat de la voisine dort. "
                             "Le chien de la maison mange.";
  const char *const originales[] = {
    "Le chat de la voisine dort.", "Le chien de la maison mange.", NULL
  };
  const char *const melangees[] = {
    "Le chat de la voisine dort.", "Le chien de la maison mange.",
    "Le chat de la maison mange.", "Le chien de la voisine dort.", NULL
  };
  modele_t *m;
  unsigned int ordre;

  /* ordre 2 : apres "de la", "voisine" et "maison" sont possibles */
  m = apprendre_texte(corpus, 2u, 4u);
  verifier_phrases_possibles(m, HACHAGE_ABSENT, melangees, 200u);
  modele_detruire(&m);
  /* ordre 3 et plus : "chat de la" n'est suivi que de "voisine" */
  for (ordre = 3u; ordre <= MODELE_ORDRE_MAX; ordre++) {
    m = apprendre_texte(corpus, ordre, 4u);
    verifier_phrases_possibles(m, HACHAGE_ABSENT, originales, 100u);
    modele_detruire(&m);
  }
}

static void test_dialogues(void)
{
  modele_t *m;
  char *phrase;
  unsigned int ordre;
  unsigned int i;

  /* tirets et guillemets du corpus reproduits a partir de l'ordre 2 (un
     guillemet ouvert par une phrase est referme a sa fin) */
  for (ordre = 2u; ordre <= 4u; ordre++) {
    m = apprendre_texte(CORPUS_DIALOGUES, ordre, 8u);
    verifier_phrases_possibles(m, HACHAGE_ABSENT, PHRASES_DIALOGUES, 150u);
    modele_detruire(&m);
  }
  /* ordre 1 : les phrases melangent les repliques, les guillemets restent
     bien formes */
  m = apprendre_texte(CORPUS_DIALOGUES, 1u, 8u);
  for (i = 0u; i < 500u; i++) {
    phrase = modele_generer_phrase(m);
    verifier_phrase(m, phrase);
    free(phrase);
  }
  modele_detruire(&m);
  /* guillemets imbriques : un seul niveau est reproduit, jamais deux
     guillemets ouverts a la fois */
  m = apprendre_texte("Elle cria: \xC2\xABIl dit: \xC2\xABViens."
                      "\xC2\xBB\xC2\xBB", 2u, 8u);
  phrase = modele_generer_phrase(m);
  assert(strcmp(phrase, "Elle cria: " GO "Il dit: Viens." GF) == 0);
  verifier_phrase(m, phrase);
  free(phrase);
  modele_detruire(&m);
}

static void test_ponctuation_interne(void)
{
  modele_t *m;
  char *phrase;
  unsigned int ordre;
  unsigned int i;

  for (ordre = 2u; ordre <= 4u; ordre++) {
    m = apprendre_texte(CORPUS_PONCTUATION_INTERNE, ordre, 9u);
    verifier_phrases_possibles(m, HACHAGE_ABSENT, PHRASES_PONCTUATION_INTERNE, 120u);
    modele_detruire(&m);
  }
  /* ordre 1 : les phrases melangent les passages, parentheses et
     guillemets restent bien formes */
  m = apprendre_texte(CORPUS_PONCTUATION_INTERNE, 1u, 9u);
  for (i = 0u; i < 500u; i++) {
    phrase = modele_generer_phrase(m);
    verifier_phrase(m, phrase);
    free(phrase);
  }
  modele_detruire(&m);
  /* passage entre parentheses qui commence par une majuscule : c'est une
     phrase citee, sa ponctuation finale reste a l'interieur */
  m = apprendre_texte("Il sourit (Vraiment!)", 2u, 9u);
  phrase = modele_generer_phrase(m);
  assert(strcmp(phrase, "Il sourit (Vraiment!)") == 0);
  free(phrase);
  modele_detruire(&m);
}

static void test_homographes(void)
{
  /* "la" apres "Elle est", "la" avec accent grave apres "Qui est" et "Il
     est" : le contexte des deux mots precedents choisit l'accent, a tout
     ordre (a l'ordre 1, le mot suivant ne depend que de "est") */
  const char *const attendues[] = {
    "Qui est l\xC3\xA0.", "Qui est l\xC3\xA0?", "Qui est l\xC3\xA0 reine.",
    "Elle est la.", "Elle est la?", "Elle est la reine.",
    "Il est l\xC3\xA0.", "Il est l\xC3\xA0?", "Il est l\xC3\xA0 reine.", NULL
  };
  const char *const originales[] = {
    "Qui est l\xC3\xA0.", "Qui est l\xC3\xA0?", "Elle est la reine.",
    "Il est l\xC3\xA0.", "Il est l\xC3\xA0?", NULL
  };
  modele_t *m;
  unsigned int ordre;

  for (ordre = 1u; ordre <= 3u; ordre++) {
    m = apprendre_texte(CORPUS_HOMOGRAPHES, ordre, 7u);
    verifier_phrases_possibles(m, HACHAGE_ABSENT, (ordre <= 2u) ? attendues : originales, 900u);
    modele_detruire(&m);
  }
}

static void test_repli(void)
{
  const char *const corpus = "Le chat de la voisine dort. "
                             "Le chien de la maison mange.";
  const char *const melangees[] = {
    "Le chat de la voisine dort.", "Le chien de la maison mange.",
    "Le chat de la maison mange.", "Le chien de la voisine dort.", NULL
  };
  const char *const repas[] = {
    "Il mange une pomme.", "Il mange une poire.", "Elle voit une maison.",
    "Elle voit une pomme.", "Elle voit une poire.", NULL
  };
  const char *const animaux_originaux[] = {
    "Le chat dort bien.", "Un chat mange vite.", NULL
  };
  const char *const animaux_melanges[] = {
    "Le chat dort bien.", "Un chat mange vite.", "Le chat mange vite.",
    "Un chat dort bien.", NULL
  };
  const char *const depuis_un[] = {
    "Un chat mange vite.", "Un chat dort bien.", NULL
  };
  const char *const animaux = "Le chat dort bien. Un chat mange vite.";
  modele_options_t options = { 3u, false, false, false, 2u };
  modele_t *m;

  /* ordre 3 : "chat de la" n'a qu'un successeur ; avec un repli a l'ordre
     2, le mot suivant est tire selon "de la" (sans repli, cf.
     test_ordre_superieur_moins_de_melanges : les phrases du corpus) */
  m = apprendre_avec(corpus, &options, 4u);
  verifier_phrases_possibles(m, HACHAGE_ABSENT, melangees, 200u);
  modele_detruire(&m);
  /* le repli n'a lieu que si le contexte n'a qu'un successeur : "mange
     une" en a deux (pomme, poire), jamais "maison" ; "voit une" n'en a
     qu'un, tous les successeurs de "une" sont possibles */
  options.ordre = 2u;
  options.ordre_repli = 1u;
  m = apprendre_avec("Il mange une pomme. Il mange une poire. "
                     "Elle voit une maison.", &options, 5u);
  verifier_phrases_possibles(m, HACHAGE_ABSENT, repas, 400u);
  modele_detruire(&m);
  /* ordre 3 : repli a l'ordre 2 sans effet ("le chat" et "un chat" n'ont
     qu'un successeur) ; a l'ordre 1, "chat" en a deux, puis le contexte
     "le chat mange", absent du corpus, impose un nouveau repli */
  options.ordre = 3u;
  options.ordre_repli = 2u;
  m = apprendre_avec(animaux, &options, 6u);
  verifier_phrases_possibles(m, HACHAGE_ABSENT, animaux_originaux, 100u);
  modele_detruire(&m);
  options.ordre_repli = 1u;
  m = apprendre_avec(animaux, &options, 6u);
  verifier_phrases_possibles(m, HACHAGE_ABSENT, animaux_melanges, 200u);
  /* depuis un mot : meme regle */
  verifier_phrases_possibles(m, modele_mot(m, "un"), depuis_un, 100u);
  modele_detruire(&m);
}

/**
 * Pour chaque ordre et chaque ordre de repli : phrases bien formees, de
 * tout debut et depuis chaque mot ; la chaine de repli est celle d'un
 * modele de cet ordre appris sur le meme corpus.
 */
static void test_repli_tous_les_ordres(void)
{
  char *corpus = malloc(sizeof(CORPUS_LISIBLE) + sizeof(CORPUS_DIALOGUES)
                        + sizeof(CORPUS_PONCTUATION_INTERNE)
                        + sizeof(CORPUS_HOMOGRAPHES) + 4u);
  modele_options_t options = { 1u, false, false, false, 0u };
  modele_t *m;
  modele_t *reference;
  char *phrase;
  unsigned int i;

  assert(corpus != NULL);
  strcpy(corpus, CORPUS_LISIBLE);
  strcat(corpus, "\n");
  strcat(corpus, CORPUS_DIALOGUES);
  strcat(corpus, CORPUS_PONCTUATION_INTERNE);
  strcat(corpus, "\n");
  strcat(corpus, CORPUS_HOMOGRAPHES);
  for (options.ordre = 2u; options.ordre <= MODELE_ORDRE_MAX; options.ordre++) {
    for (options.ordre_repli = 1u; options.ordre_repli < options.ordre; options.ordre_repli++) {
      m = apprendre_avec(corpus, &options, options.ordre);
      reference = apprendre_texte(corpus, options.ordre_repli, 1u);
      assert(modele_ordre_repli(m) == options.ordre_repli);
      verifier_chaines_egales(modele_repli(m), modele_chaine(reference));
      assert((modele_etats_repli(m) == NULL) == (options.ordre_repli < 3u));
      if (modele_etats_repli(m) != NULL) {
        assert(hachage_nuplets_nb(modele_etats_repli(m)) == hachage_nuplets_nb(modele_etats(reference)));
      }
      modele_detruire(&reference);
      for (i = 0u; i < 40u; i++) {
        phrase = modele_generer_phrase(m);
        verifier_phrase(m, phrase);
        free(phrase);
      }
      for (i = 1u; i <= modele_nb_mots(m); i++) {
        phrase = modele_generer_phrase_depuis(m, i);
        verifier_phrase(m, phrase);
        free(phrase);
      }
      modele_detruire(&m);
    }
  }
  free(corpus);
}

static void test_generation_ordre_1(void)
{
  modele_t *m = apprendre_texte(CORPUS_LISIBLE, 1u, 5u);
  char *phrase;
  unsigned int i;

  for (i = 0u; i < 200u; i++) {
    phrase = modele_generer_phrase(m);
    assert(phrase != NULL);
    verifier_phrase(m, phrase);
    /* "a" s'ecrit selon le mot qui le precede */
    assert(strstr(phrase, "va a") == NULL && strstr(phrase, "Il \xC3\xA0") == NULL);
    free(phrase);
  }
  modele_detruire(&m);
}

static void test_ponctuation_tiree(void)
{
  modele_t *m = apprendre_texte("Viens ! Viens ! Viens ?", 1u, 3u);
  unsigned int nb_exclamations = 0u;
  unsigned int nb_questions = 0u;
  unsigned int i;
  char *phrase;

  for (i = 0u; i < 3000u; i++) {
    phrase = modele_generer_phrase(m);
    if (strcmp(phrase, "Viens!") == 0) {
      nb_exclamations++;
    } else {
      assert(strcmp(phrase, "Viens?") == 0);
      nb_questions++;
    }
    free(phrase);
  }
  /* 2/3 et 1/3 (graine fixee : test deterministe) */
  assert(ecart((double)nb_exclamations / 3000.0, 2.0 / 3.0) < 0.05);
  assert(ecart((double)nb_questions / 3000.0, 1.0 / 3.0) < 0.05);
  modele_detruire(&m);
}

static void test_phrase_coupee(void)
{
  /* une phrase de 100 mots differents (aa ab ... dv) : coupee a 80 mots */
  char texte[400];
  char *phrase;
  modele_t *m;
  unsigned int i;
  unsigned int nb_espaces = 0u;
  size_t n = 0u;

  for (i = 0u; i < 100u; i++) {
    texte[n] = (char)('a' + (i / 26u));
    texte[n + 1u] = (char)('a' + (i % 26u));
    texte[n + 2u] = ' ';
    n += 3u;
  }
  texte[n - 1u] = '.';
  texte[n] = '\0';
  m = apprendre_texte(texte, 2u, 1u);
  phrase = modele_generer_phrase(m);
  for (i = 0u; phrase[i] != '\0'; i++) {
    nb_espaces += (phrase[i] == ' ') ? 1u : 0u;
  }
  assert(nb_espaces + 1u == MODELE_NB_MOTS_MAX_PHRASE);
  assert(phrase[strlen(phrase) - 1u] == '.');
  assert(strncmp(phrase, "Aa ab ac", 8u) == 0);
  free(phrase);
  modele_detruire(&m);
}

static void test_modele_vide(void)
{
  modele_t *m = apprendre_texte("... 42 !!!", 1u, 1u);

  assert(!modele_peut_generer(m));
  assert(modele_nb_mots(m) == 0u);
  modele_detruire(&m);
}

static void test_accesseurs(void)
{
  modele_t *m = apprendre_texte("Le chat dort. Le chien dort.", 2u, 1u);

  assert(modele_nb_mots(m) == 4u);
  assert(hachage_nb_mots(modele_mots(m)) == 5u);
  assert(markov_nb_etats(modele_chaine(m)) == hachage_paires_nb(modele_bigrammes(m)));
  assert(modele_etats(m) == NULL);
  assert(lexique_est_finalise(modele_lexique(m)));
  assert(markov_nb_etats(modele_ponctuation(m)) == REPERTOIRE_NB_FINS + 5u);
  modele_detruire(&m);
  /* ordre 3 : les etats sont des triplets de mots */
  m = apprendre_texte("Le chat dort. Le chien dort.", 3u, 1u);
  assert(modele_ordre(m) == 3u);
  assert(modele_etats(m) != NULL);
  assert(hachage_nuplets_taille(modele_etats(m)) == 3u);
  assert(markov_nb_etats(modele_chaine(m)) == hachage_nuplets_nb(modele_etats(m)));
  /* (0,0,0) (0,0,le) (0,le,chat) (le,chat,dort) (0,le,chien) (le,chien,dort) */
  assert(hachage_nuplets_nb(modele_etats(m)) == 6u);
  assert(modele_ordre_repli(m) == 0u);
  assert(modele_repli(m) == NULL && modele_etats_repli(m) == NULL);
  modele_detruire(&m);
}

static void test_accesseurs_repli(void)
{
  modele_options_t options = { 3u, false, false, false, 2u };
  modele_t *m = apprendre_avec("Le chat dort. Le chien dort.", &options, 1u);

  /* repli a l'ordre 2 : un etat par bigramme */
  assert(modele_ordre(m) == 3u && modele_ordre_repli(m) == 2u);
  assert(modele_repli(m) != NULL && modele_etats_repli(m) == NULL);
  assert(markov_nb_etats(modele_repli(m)) == hachage_paires_nb(modele_bigrammes(m)));
  modele_detruire(&m);
  /* repli a l'ordre 1 : un etat par mot */
  options.ordre_repli = 1u;
  m = apprendre_avec("Le chat dort. Le chien dort.", &options, 1u);
  assert(markov_nb_etats(modele_repli(m)) == modele_nb_mots(m) + 1u);
  modele_detruire(&m);
  /* ordre 5, repli a l'ordre 3 : les etats de repli sont des triplets */
  options.ordre = 5u;
  options.ordre_repli = 3u;
  m = apprendre_avec("Le chat dort. Le chien dort.", &options, 1u);
  assert(modele_etats_repli(m) != NULL);
  assert(hachage_nuplets_taille(modele_etats_repli(m)) == 3u);
  assert(hachage_nuplets_nb(modele_etats_repli(m)) == 6u);
  assert(markov_nb_etats(modele_repli(m)) == 6u);
  assert(hachage_nuplets_taille(modele_etats(m)) == 5u);
  modele_detruire(&m);
}

static void test_apprendre_erreurs(void)
{
  modele_t *m;

  errno = 0;
  assert(modele_apprendre("/tmp/repertoire_absent_markov", 1u, 1u, false) == NULL);
  assert(errno == ENOENT);
  errno = 0;
  assert(modele_apprendre("Makefile/absent", 1u, 2u, true) == NULL);
  assert(errno == ENOTDIR);
  /* un fichier seul est un corpus */
  m = modele_apprendre("Makefile", 1u, 2u, true);
  assert(m != NULL && modele_peut_generer(m));
  assert(modele_mot(m, "gcc") != HACHAGE_ABSENT);
  modele_detruire(&m);
}


/* ======================================================================== */
/*                              Premier mot                                 */
/* ======================================================================== */

static void test_trouver_un_mot(void)
{
  modele_t *m = apprendre_texte("L'\xC3\xA9t\xC3\xA9 est beau. Le chat "
                                "dort.", 1u, 1u);
  unsigned int ete = modele_mot(m, "\xC3\xA9t\xC3\xA9");

  /* accents et majuscules facultatifs */
  assert(ete != HACHAGE_ABSENT);
  assert(strcmp(hachage_mot(modele_mots(m), ete), "ete") == 0);
  assert(modele_mot(m, "ete") == ete);
  assert(modele_mot(m, "\xC3\x89T\xC3\x89") == ete);
  assert(modele_mot(m, "Ete") == ete);
  assert(modele_mot(m, "CHAT") == modele_mot(m, "chat"));
  /* mot absent : ENOENT ; pas un mot seul : EINVAL */
  errno = 0;
  assert(modele_mot(m, "chien") == HACHAGE_ABSENT && errno == ENOENT);
  errno = 0;
  assert(modele_mot(m, "l'\xC3\xA9t\xC3\xA9") == HACHAGE_ABSENT && errno == EINVAL);
  errno = 0;
  assert(modele_mot(m, "") == HACHAGE_ABSENT && errno == EINVAL);
  errno = 0;
  assert(modele_mot(m, "le chat") == HACHAGE_ABSENT && errno == EINVAL);
  modele_detruire(&m);
}

static void test_generer_depuis_un_mot(void)
{
  const char *const corpus = "Le chat dort. Le chien mange. "
                             "L'\xC3\xA9t\xC3\xA9 est beau. "
                             "\xC3\x89t\xC3\xA9 comme hiver.";
  const char *const depuis_le[] = { "Le chat dort.", "Le chien mange.",
                                    NULL };
  const char *const depuis_chat[] = { "Chat dort.", NULL };
  const char *const depuis_dort[] = { "Dort.", NULL };
  const char *const depuis_ete_1[] = { "\xC3\x89t\xC3\xA9 comme hiver.",
                                       "\xC3\x89t\xC3\xA9 est beau.", NULL };
  const char *const depuis_ete_2[] = { "\xC3\x89t\xC3\xA9 comme hiver.", NULL };
  modele_t *m;
  unsigned int ordre;

  for (ordre = 1u; ordre <= 4u; ordre++) {
    m = apprendre_texte(corpus, ordre, 6u);
    /* "le" commence des phrases : on part d'un debut de phrase */
    verifier_phrases_possibles(m, modele_mot(m, "le"), depuis_le, 60u);
    /* "chat" et "dort" n'en commencent pas : majuscule ajoutee */
    verifier_phrases_possibles(m, modele_mot(m, "chat"), depuis_chat, 10u);
    verifier_phrases_possibles(m, modele_mot(m, "dort"), depuis_dort, 10u);
    verifier_phrases_possibles(m, modele_mot(m, "chat"), depuis_chat, 10u);
    /* "ete" commence "Ete comme hiver." ; a l'ordre 1, il peut aussi etre
       suivi de "est" ("l'ete est beau") */
    verifier_phrases_possibles(m, modele_mot(m, "ete"),
                               (ordre == 1u) ? depuis_ete_1 : depuis_ete_2, 60u);
    modele_detruire(&m);
  }
}

static void test_contexte_le_plus_frequent(void)
{
  /* "une" ne commence aucune phrase : on part de son contexte le plus
     frequent ("mange une" : 2 fois), et non du premier rencontre ("prend
     une" : 1 fois) */
  const char *const corpus = "Elle prend une poire. Il mange une pomme. "
                             "Il mange une pomme.";
  const char *const ordre_1[] = { "Une pomme.", "Une poire.", NULL };
  const char *const ordre_2[] = { "Une pomme.", NULL };
  modele_t *m = apprendre_texte(corpus, 1u, 2u);
  char *phrase;
  char *mot;
  unsigned int i;

  verifier_phrases_possibles(m, modele_mot(m, "une"), ordre_1, 60u);
  modele_detruire(&m);
  m = apprendre_texte(corpus, 2u, 2u);
  verifier_phrases_possibles(m, modele_mot(m, "une"), ordre_2, 20u);
  /* chaque phrase commence bien par le mot demande */
  for (i = 1u; i <= modele_nb_mots(m); i++) {
    phrase = modele_generer_phrase_depuis(m, i);
    verifier_phrase(m, phrase);
    mot = premier_mot(phrase);
    assert(strcmp(mot, hachage_mot(modele_mots(m), i)) == 0);
    free(mot);
    free(phrase);
  }
  modele_detruire(&m);
}


/* ======================================================================== */
/*                      Corpus, lignes, filtre de langue                    */
/* ======================================================================== */

static void test_phrase_par_ligne(void)
{
  const char *const poeme = "Un vers sans fin\nUn autre vers\n"
                            "Et un dernier vers!\n";
  const char *const attendues[] = {
    "Un vers sans fin", "Un autre vers", "Un autre vers!",
    "Et un dernier vers", "Et un dernier vers!", NULL
  };
  modele_options_t options = { 2u, false, true, false, 0u };
  modele_t *m = apprendre_avec(poeme, &options, 3u);
  char *phrase;
  unsigned int i;

  /* une phrase par vers ; la fin sans ponctuation est apprise */
  verifier_phrases_possibles(m, HACHAGE_ABSENT, attendues, 300u);
  for (i = 0u; i < 20u; i++) {
    phrase = modele_generer_phrase(m);
    verifier_phrase_lue(m, phrase, true);
    free(phrase);
  }
  modele_detruire(&m);
  /* lecture habituelle : un seul paragraphe, donc une seule phrase */
  options.phrase_par_ligne = false;
  m = apprendre_avec(poeme, &options, 3u);
  phrase = modele_generer_phrase(m);
  assert(strcmp(phrase, "Un vers sans fin Un autre vers Et un dernier vers!") == 0);
  free(phrase);
  modele_detruire(&m);
}

static void test_corpus_de_plusieurs_chemins(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *ailleurs = creer_repertoire_temporaire();
  char *fichier = chemin_dans(ailleurs, "lettre.txt");
  const char *chemins[2];
  modele_options_t options = { 2u, false, false, false, 0u };
  repertoire_bilan_t bilan;
  modele_t *m;

  creer_fichier_dans(repertoire, "a.txt", "Le chat dort.");
  creer_fichier_dans(repertoire, "b.txt", "Le chien mange.");
  ecrire_fichier(fichier, "Cher ami.");
  chemins[0] = repertoire;
  chemins[1] = fichier;
  m = modele_apprendre_corpus(chemins, 2u, 1u, &options, &bilan);
  assert(m != NULL);
  assert(bilan.nb_fichiers == 3u && bilan.nb_phrases == 3u);
  assert(bilan.nb_mots == 8u && bilan.nb_phrases_ecartees == 0u);
  assert(bilan.langue == LANGUE_INCONNUE);
  assert(modele_nb_mots(m) == 7u);
  assert(modele_mot(m, "ami") != HACHAGE_ABSENT);
  assert(modele_mot(m, "chien") != HACHAGE_ABSENT);
  modele_detruire(&m);
  free(fichier);
  detruire_repertoire_temporaire(ailleurs);
  detruire_repertoire_temporaire(repertoire);
}

static void test_filtre_de_langue(void)
{
  const char *const francais =
    "Le chat dort sur le toit de la maison. Il regarde les oiseaux qui "
    "passent dans le ciel. La petite fille ouvre la porte et appelle son "
    "chat. Elle ne veut pas qu'il reste dehors pendant la nuit. Le chat "
    "descend du toit et vient pres d'elle. Ils rentrent tous les deux dans "
    "la cuisine.\n";
  const char *const anglais =
    "The old man walked to the river with his dog. He had not seen the "
    "water for many years. It was cold and the wind was strong.\n";
  char *repertoire = creer_repertoire_temporaire();
  const char *chemin = repertoire;
  modele_options_t options = { 2u, false, false, true, 0u };
  repertoire_bilan_t bilan;
  modele_t *m;
  char *phrase;
  unsigned int i;

  creer_fichier_dans(repertoire, "a.txt", francais);
  creer_fichier_dans(repertoire, "b.txt", anglais);
  creer_fichier_dans(repertoire, "c.txt", francais);
  m = modele_apprendre_corpus(&chemin, 1u, 1u, &options, &bilan);
  assert(m != NULL);
  assert(bilan.langue == LANGUE_FRANCAIS);
  assert(bilan.nb_phrases_ecartees == 3u);
  assert(modele_mot(m, "river") == HACHAGE_ABSENT);
  assert(modele_mot(m, "the") == HACHAGE_ABSENT);
  for (i = 0u; i < 50u; i++) {
    phrase = modele_generer_phrase(m);
    verifier_phrase(m, phrase);
    free(phrase);
  }
  modele_detruire(&m);
  /* sans le filtre, l'anglais est appris */
  options.filtrer_langues = false;
  m = modele_apprendre_corpus(&chemin, 1u, 1u, &options, &bilan);
  assert(m != NULL);
  assert(bilan.langue == LANGUE_INCONNUE && bilan.nb_phrases_ecartees == 0u);
  assert(modele_mot(m, "river") != HACHAGE_ABSENT);
  modele_detruire(&m);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                         Sauvegarde et chargement                         */
/* ======================================================================== */

/**
 * Apprend un corpus et verifie le fichier sauvegarde (attendu), puis charge
 * ce texte ecrit a la main et verifie la premiere phrase generee (graine 1
 * dans les deux cas).
 */
static void verifier_format(const char *corpus,
                            const modele_options_t *options,
                            const char *attendu, const char *premiere_phrase)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "modele.txt");
  modele_t *m = apprendre_avec(corpus, options, 1u);
  char *contenu;
  char *phrase;
  int erreur = 0;

  assert(modele_sauvegarder(m, chemin));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL);
  if (strcmp(contenu, attendu) != 0) {
    fprintf(stderr, "\nfichier obtenu :\n%s", contenu);
  }
  assert(strcmp(contenu, attendu) == 0);
  free(contenu);
  modele_detruire(&m);
  m = charger_texte(attendu, strlen(attendu), &erreur);
  assert(m != NULL && modele_peut_generer(m));
  assert(modele_ordre(m) == options->ordre);
  assert(modele_ordre_repli(m) == options->ordre_repli);
  phrase = modele_generer_phrase(m);
  if (strcmp(phrase, premiere_phrase) != 0) {
    fprintf(stderr, "\nphrase obtenue : \"%s\"\n", phrase);
  }
  assert(strcmp(phrase, premiere_phrase) == 0);
  free(phrase);
  modele_detruire(&m);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_format_de_sauvegarde(void)
{
  modele_options_t options = { 1u, false, false, false, 0u };

  verifier_format("Le chat dort.", &options, MODELE_VALIDE, "Le chat dort.");
  /* ordre 3 : la section des etats s'ajoute */
  options.ordre = 3u;
  verifier_format("Le chat dort.", &options, MODELE_VALIDE_3, "Le chat dort.");
  /* ordre 4, repli a l'ordre 3 : etats et chaine de repli */
  options.ordre = 4u;
  options.ordre_repli = 3u;
  verifier_format("Le chat dort.", &options, MODELE_VALIDE_4_3, "Le chat dort.");
  /* ordre 2, repli a l'ordre 1 (un etat par mot), un contexte */
  options.ordre = 2u;
  options.ordre_repli = 1u;
  verifier_format(CORPUS_HOMOGRAPHES, &options, MODELE_VALIDE_REPLI, "Il est l\xC3\xA0?");
}

static void test_format_version_2(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "modele.txt");
  modele_t *m;
  char *phrase;
  char *contenu;
  int erreur = 0;

  /* un modele de la version 2 (ni repli ni contextes) reste lisible ; il
     est resauvegarde au format courant */
  m = charger_texte(MODELE_VERSION_2, strlen(MODELE_VERSION_2), &erreur);
  assert(m != NULL && modele_ordre(m) == 1u && modele_ordre_repli(m) == 0u);
  phrase = modele_generer_phrase(m);
  assert(strcmp(phrase, "Le chat dort.") == 0);
  free(phrase);
  assert(modele_sauvegarder(m, chemin));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL && strcmp(contenu, MODELE_VALIDE) == 0);
  free(contenu);
  modele_detruire(&m);
  /* la version 2 n'a ni ligne repli ni section contextes, toutes deux
     obligatoires dans la version 3 */
  verifier_refus_dans(MODELE_VERSION_2, "ordre 1\n", "ordre 1\nrepli 0\n");
  verifier_refus_dans(MODELE_VERSION_2, "chaine 4", "contextes 0\nchaine 4");
  verifier_refus("repli 0\n", "");
  verifier_refus("contextes 0\n", "");
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Apprend, sauvegarde, recharge et compare (ordre et ordre de repli donnes). */
static void verifier_aller_retour(unsigned int ordre, unsigned int ordre_repli)
{
  char *repertoire = creer_repertoire_temporaire();
  char *corpus = chemin_dans(repertoire, "corpus");
  char *chemin = chemin_dans(repertoire, "modele.txt");
  char *chemin_bis = chemin_dans(repertoire, "modele_bis.txt");
  const char *chemins[1];
  modele_options_t options = { 1u, false, false, false, 0u };
  modele_t *appris;
  modele_t *charge;
  const lexique_t *la;
  const lexique_t *lc;
  char *pa;
  char *pc;
  char *contenu;
  char *contenu_bis;
  unsigned int i;
  unsigned int variante;
  unsigned int mot_avant;
  unsigned int bigramme;
  unsigned int nb_exceptions = 0u;

  assert(mkdir(corpus, 0700) == 0);
  creer_fichier_dans(corpus, "a.txt", CORPUS_LISIBLE);
  creer_fichier_dans(corpus, "b.txt", "\xC9t\xE9 comme hiver, il va \xE0 la "
                     "plage. L'\xE9t\xE9, c'est la plage !");  /* Latin-1 */
  creer_fichier_dans(corpus, "c.txt", "etait il a dit la plage\n\nFIN");
  creer_fichier_dans(corpus, "d.txt", CORPUS_DIALOGUES);
  creer_fichier_dans(corpus, "e.txt", CORPUS_HOMOGRAPHES);
  creer_fichier_dans(corpus, "f.txt", CORPUS_PONCTUATION_INTERNE);
  chemins[0] = corpus;
  options.ordre = ordre;
  options.ordre_repli = ordre_repli;
  appris = modele_apprendre_corpus(chemins, 1u, 77u, &options, NULL);
  assert(appris != NULL);
  assert(modele_sauvegarder(appris, chemin));
  charge = modele_charger(chemin, 77u);
  assert(charge != NULL);
  /* memes mots, memes paires, memes chaines, memes choix */
  assert(modele_ordre(charge) == ordre);
  assert(modele_nb_mots(charge) == modele_nb_mots(appris));
  for (i = 1u; i <= modele_nb_mots(appris); i++) {
    assert(strcmp(hachage_mot(modele_mots(charge), i), hachage_mot(modele_mots(appris), i)) == 0);
  }
  assert(hachage_paires_nb(modele_bigrammes(charge)) == hachage_paires_nb(modele_bigrammes(appris)));
  verifier_chaines_egales(modele_chaine(appris), modele_chaine(charge));
  verifier_chaines_egales(modele_ponctuation(appris), modele_ponctuation(charge));
  la = modele_lexique(appris);
  lc = modele_lexique(charge);
  for (i = 0u; i < lexique_nb_bigrammes(la); i++) {
    assert(lexique_variante_bigramme(la, i) == lexique_variante_bigramme(lc, i));
    assert(lexique_casse_bigramme(la, i) == lexique_casse_bigramme(lc, i));
    assert(lexique_liaison(la, i) == lexique_liaison(lc, i));
    assert(lexique_marques(la, i) == lexique_marques(lc, i));
  }
  assert((modele_etats(appris) == NULL) == (ordre < 3u));
  assert((modele_etats(charge) == NULL) == (ordre < 3u));
  /* meme repli */
  assert(modele_ordre_repli(charge) == ordre_repli);
  assert((modele_repli(charge) == NULL) == (ordre_repli == 0u));
  assert((modele_etats_repli(charge) == NULL) == (ordre_repli < 3u));
  if (ordre_repli > 0u) {
    verifier_chaines_egales(modele_repli(appris), modele_repli(charge));
  }
  /* memes contextes : les exceptions du modele appris, seules
     sauvegardees */
  for (i = 0u; i < lexique_nb_contextes(la); i++) {
    variante = lexique_contexte(la, i, &mot_avant, &bigramme);
    if (variante != LEXIQUE_AUCUNE) {
      nb_exceptions++;
      assert(lexique_variante_contexte(lc, mot_avant, bigramme) == variante);
    }
  }
  assert(nb_exceptions > 0u);
  assert(lexique_nb_contextes(lc) == nb_exceptions);
  /* meme graine : exactement les memes phrases, de tout debut et depuis
     chaque mot */
  for (i = 0u; i < 300u; i++) {
    pa = modele_generer_phrase(appris);
    pc = modele_generer_phrase(charge);
    assert(pa != NULL && pc != NULL && strcmp(pa, pc) == 0);
    verifier_phrase(charge, pc);
    free(pa);
    free(pc);
  }
  for (i = 1u; i <= modele_nb_mots(appris); i++) {
    pa = modele_generer_phrase_depuis(appris, i);
    pc = modele_generer_phrase_depuis(charge, i);
    assert(pa != NULL && pc != NULL && strcmp(pa, pc) == 0);
    verifier_phrase(charge, pc);
    free(pa);
    free(pc);
  }
  /* resauvegarder le modele charge redonne le meme fichier */
  assert(modele_sauvegarder(charge, chemin_bis));
  contenu = lire_fichier(chemin);
  contenu_bis = lire_fichier(chemin_bis);
  assert(contenu != NULL && contenu_bis != NULL && strcmp(contenu, contenu_bis) == 0);
  free(contenu);
  free(contenu_bis);
  modele_detruire(&appris);
  modele_detruire(&charge);
  free(corpus);
  free(chemin);
  free(chemin_bis);
  detruire_repertoire_temporaire(repertoire);
}

static void test_aller_retour_tous_les_ordres(void)
{
  unsigned int ordre;
  unsigned int ordre_repli;

  for (ordre = 1u; ordre <= MODELE_ORDRE_MAX; ordre++) {
    for (ordre_repli = 0u; ordre_repli < ordre; ordre_repli++) {
      verifier_aller_retour(ordre, ordre_repli);
    }
  }
}

static void test_aller_retour_modele_vide(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "vide.txt");
  modele_t *m = apprendre_texte("", 1u, 1u);

  assert(modele_sauvegarder(m, chemin));
  modele_detruire(&m);
  m = modele_charger(chemin, 1u);
  assert(m != NULL && !modele_peut_generer(m));
  modele_detruire(&m);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

static void test_sauvegarder_erreurs(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *sans_parent = chemin_dans(repertoire, "absent/modele.txt");
  modele_t *m = apprendre_texte("Le chat dort.", 1u, 1u);

  errno = 0;
  assert(!modele_sauvegarder(m, sans_parent));
  assert(errno == ENOENT);
  assert(!sauvegarde_existe(sans_parent));
  errno = 0;
  assert(!modele_sauvegarder(m, repertoire));
  assert(errno == EISDIR);
  /* disque plein : erreur signalee, peripherique conserve */
  if (sauvegarde_existe("/dev/full")) {
    errno = 0;
    assert(!modele_sauvegarder(m, "/dev/full"));
    assert(errno == ENOSPC);
    assert(sauvegarde_existe("/dev/full"));
  }
  modele_detruire(&m);
  free(sans_parent);
  detruire_repertoire_temporaire(repertoire);
}

/** Charge une modification d'un modele valide qui doit etre acceptee, et verifie la phrase generee. */
static void verifier_accepte(const char *modele, const char *ancien,
                             const char *nouveau, const char *phrase_attendue)
{
  char *modifie = remplacer(modele, ancien, nouveau);
  int erreur = 0;
  modele_t *m = charger_texte(modifie, strlen(modifie), &erreur);
  char *phrase;

  assert(m != NULL);
  phrase = modele_generer_phrase(m);
  assert(phrase != NULL && strcmp(phrase, phrase_attendue) == 0);
  free(phrase);
  modele_detruire(&m);
  free(modifie);
}

static void test_charger_fichiers_invalides(void)
{
  const char *const mot_sans_etat =
    "modele-markov 3\nordre 3\nrepli 0\nmots 5\n- - 0 0\nle 0 0 1 le\n"
    "chat 0 0 1 chat\ndort 0 0 1 dort\nchien 0 0 1 chien\nbigrammes 4\n"
    "0 0 - - 0 0\n0 1 - - 0 0\n1 2 - 0 0 0\n2 3 - 0 0 0\ncontextes 0\n"
    "etats 4\n0 0 0\n"
    "0 0 1\n0 1 2\n1 2 3\nchaine 4\n1 1 1\n1 2 1\n1 3 1\n1 0 1\n"
    "ponctuation 10\n0\n0\n0\n0\n0\n0\n0\n0\n1 0 1\n0\nfin\n";
  char *crlf;
  char *avec_nul;
  char *modifie;
  size_t i;
  size_t j;
  int erreur = 0;
  char *repertoire;

  /* en-tete (l'ancien format 1 est refuse), ordre, repli, nombres */
  verifier_refus("modele-markov 3", "modele-markov 1");
  verifier_refus("modele-markov 3", "modele-markov 4");
  verifier_refus("repli 0", "repli 1");                 /* pas inferieur */
  verifier_refus("repli 0", "repli");
  verifier_refus("repli 0", "repli 0 0");
  verifier_refus("contextes 0", "contextes 1");         /* ligne absente */
  verifier_refus("contextes 0", "contextes");
  verifier_refus("ordre 1", "ordre 0");
  verifier_refus("ordre 1", "ordre 9");
  verifier_refus("ordre 1", "ordre 3");                      /* sans etats */
  verifier_refus("ordre 1", "ordre 1 ");
  verifier_refus("mots 4", "mots 0");
  verifier_refus("mots 4", "mots 5");
  verifier_refus("mots 4", "mots 99999999999");
  verifier_refus("mots 4", "mots -1");
  /* fin du fichier */
  verifier_refus("fin\n", "");
  verifier_refus("fin\n", "fin\nautre chose\n");
  verifier_refus("1 0 1\nfin", "1 0 1\n");
  /* mots */
  verifier_refus("- - 0 0", "- 0 0 1 x");
  verifier_refus("dort 0 0 1 dort", "chat 0 0 1 chat");      /* doublon */
  verifier_refus("chat 0 0 1 chat", "Chat 0 0 1 chat");      /* cle */
  verifier_refus("chat 0 0 1 chat", "chat 0 0 1 chien");     /* variante */
  verifier_refus("chat 0 0 1 chat", "chat 0 0 1 Chat");      /* minuscules */
  verifier_refus("chat 0 0 1 chat", "chat 1 0 1 chat");      /* choix */
  verifier_refus("chat 0 0 1 chat", "chat 0 3 1 chat");      /* casse */
  verifier_refus("chat 0 0 1 chat", "chat 0 0 2 chat chat");
  verifier_refus("chat 0 0 1 chat", "chat 0 0 1 chat  ");
  verifier_refus("chat 0 0 1 chat", "chat 0  0 1 chat");
  /* bigrammes */
  verifier_refus("bigrammes 4", "bigrammes 0");
  verifier_refus("0 0 - - 0 0", "1 1 - - 0 0");
  verifier_refus("2 3 - 0 0 0", "2 0 - 0 0 0");
  verifier_refus("2 3 - 0 0 0", "1 2 - 0 0 0");              /* doublon */
  verifier_refus("1 2 - 0 0 0", "1 2 - 0 10 0");             /* liaison */
  verifier_refus("1 2 - 0 0 0", "1 2 1 0 0 0");              /* variante */
  verifier_refus("1 2 - 0 0 0", "1 7 - 0 0 0");
  verifier_refus("1 2 - 0 0 0", "1 2 - 0 0");                /* sans marques */
  verifier_refus("1 2 - 0 0 0", "1 2 - 0 0 32");             /* marques */
  verifier_refus("1 2 - 0 0 0", "1 2 - 0 1 2");              /* ' et tiret */
  verifier_refus("1 2 - 0 0 0", "1 2 - 0 2 4");              /* - et << */
  verifier_refus("1 2 - 0 0 0", "1 2 - 0 0 0 0");
  /* chaine */
  verifier_refus("chaine 4", "chaine 5");
  verifier_refus("chaine 4\n1 1 1", "chaine 4\n1 4 1");      /* hors bornes */
  verifier_refus("chaine 4\n1 1 1", "chaine 4\n1 1 0");      /* poids nul */
  verifier_refus("chaine 4\n1 1 1", "chaine 4\n2 1 1 1 1");  /* doublon */
  verifier_refus("chaine 4\n1 1 1", "chaine 4\n2 0 1 1 1");  /* phrase vide */
  verifier_refus("chaine 4\n1 1 1", "chaine 4\n2 1 1");      /* tronquee */
  verifier_refus("1 1 1\n1 2 1\n", "1 1 1\n1 3 1\n");        /* sans paire */
  verifier_refus("1 2 1\n1 3 1\n", "1 2 1\n0\n");            /* sans voisin */
  /* poids total sortant superieur a MARKOV_POIDS_MAX */
  verifier_refus("0\n1 0 1\nfin", "0\n2 0 4294967295 1 1\nfin");
  /* ponctuation */
  verifier_refus("ponctuation 9", "ponctuation 8");
  verifier_refus("0\n1 0 1\nfin", "0\n0\nfin");              /* fin sans . */
  verifier_refus("0\n1 0 1\nfin", "0\n1 5 1\nfin");          /* vers un mot */
  verifier_refus("ponctuation 9\n0", "ponctuation 9\n1 1 1");
  /* acceptes : fin sans ponctuation, marques compatibles avec la liaison */
  verifier_accepte(MODELE_VALIDE, "0\n1 0 1\nfin", "0\n1 4 1\nfin",
                   "Le chat dort");
  verifier_accepte(MODELE_VALIDE, "0 1 - - 0 0", "0 1 - - 0 6",
                   TIRET GO "Le chat dort." GF);
  verifier_accepte(MODELE_VALIDE, "1 2 - 0 0 0", "1 2 - 0 4 7",
                   "Le, " TIRET GO "chat dort" GF ".");
  verifier_accepte(MODELE_VALIDE, "1 2 - 0 0 0", "1 2 - 0 7 0",
                   "Le! chat dort.");
  verifier_accepte(MODELE_VALIDE, "0 1 - - 0 0", "0 1 - - 0 8",
                   "(Le chat dort.)");
  verifier_accepte(MODELE_VALIDE, "1 2 - 0 0 0", "1 2 - 0 0 8",
                   "Le (chat dort).");
  verifier_accepte(MODELE_VALIDE, "1 2 - 0 0 0", "1 2 - 0 9 12",
                   "Le... (" GO "chat dort" GF ").");
  verifier_accepte(MODELE_VALIDE, "2 3 - 0 0 0", "2 3 - 0 8 17",
                   "Le chat? dort.");             /* rien a fermer */
  /* signe de la ponctuation interne avant les marques fermantes, virgule
     apres */
  modifie = remplacer(MODELE_VALIDE, "1 2 - 0 0 0", "1 2 - 0 0 8");
  verifier_accepte(modifie, "2 3 - 0 0 0", "2 3 - 0 7 16", "Le (chat!) dort.");
  verifier_accepte(modifie, "2 3 - 0 0 0", "2 3 - 0 4 16", "Le (chat), dort.");
  verifier_accepte(modifie, "2 3 - 0 0 0", "2 3 - 0 3 16", "Le (chat.) dort.");
  free(modifie);
  modifie = remplacer(MODELE_VALIDE, "1 2 - 0 0 0", "1 2 - 0 0 12");
  verifier_accepte(modifie, "2 3 - 0 0 0", "2 3 - 0 8 1",
                   "Le (" GO "chat?" GF " dort).");
  verifier_accepte(modifie, "2 3 - 0 0 0", "2 3 - 0 8 16",
                   "Le (" GO "chat?" GF ") dort.");
  free(modifie);
  /* etats (ordre 3) */
  verifier_refus_3("etats 4", "etats 0");
  verifier_refus_3("etats 4", "etats 5");
  verifier_refus_3("etats 4\n0 0 0\n0 0 1", "etats 4\n0 0 1\n0 0 0");
  verifier_refus_3("0 1 2\n", "1 0 2\n");                    /* 0 au milieu */
  verifier_refus_3("1 2 3\n", "1 2 0\n");                    /* sans mot */
  verifier_refus_3("1 2 3\n", "1 2 4\n");                    /* hors bornes */
  verifier_refus_3("1 2 3\n", "0 1 2\n");                    /* doublon */
  verifier_refus_3("1 2 3\n", "1 2\n");
  verifier_refus_3("1 2 3\n", "1 2 3 3\n");
  verifier_refus_3("1 2 3\n", "2 2 3\n");      /* fenetre incoherente */
  verifier_refus_3("2 3 - 0 0 0", "1 3 - 0 0 0");            /* paire */
  verifier_refus_3("ordre 3", "ordre 4");
  /* contextes de deux mots */
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n7 3 1");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 9 1");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 3 2");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 3 0");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 1 0");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 0 0");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n5 3 1");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 3");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 3 1 1");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 1\n4 3 -");
  verifier_refus_repli("contextes 1\n4 3 1", "contextes 2\n4 3 1\n4 3 1");
  verifier_refus_repli("contextes 1", "contextes 2");
  /* bigramme (est, la) sans variante propre : celle du mot (accent
     grave) ; le contexte doit en differer */
  modifie = remplacer(MODELE_VALIDE_REPLI, "2 3 0 0 0 0", "2 3 - 0 0 0");
  verifier_accepte(modifie, "fin", "fin", "Il est l\xC3\xA0?");
  verifier_refus_dans(modifie, "contextes 1\n4 3 1", "contextes 1\n4 3 0");
  free(modifie);
  /* "qui est la" sans accent : autre exception, acceptee */
  verifier_accepte(MODELE_VALIDE_REPLI, "contextes 1\n4 3 1",
                   "contextes 1\n6 3 1", "Il est la?");
  /* chaine de repli */
  verifier_refus_repli("repli 1", "repli 2");
  verifier_refus_repli("repli 1", "repli 0");
  verifier_refus_repli("chaine_repli 7", "chaine_repli 9");
  verifier_refus_repli("chaine_repli 7", "chaine_repli 6");
  verifier_refus_repli("chaine_repli 7\n3 1 1 4 1 6 1", "chaine_repli 7\n0");         /* pas de debut */
  verifier_refus_repli("chaine_repli 7\n3 1 1 4 1 6 1",
                       "chaine_repli 7\n4 0 1 1 1 4 1 6 1");  /* vide */
  verifier_refus_repli("1 2 1\n1 3 3\n", "1 2 1\n1 4 3\n");  /* est elle */
  verifier_refus_repli("1 2 1\n1 3 3\n", "1 2 1\n0\n");    /* sans voisin */
  verifier_refus_repli("2 0 2 5 1\n", "2 0 2 4 1\n");      /* la elle */
  verifier_refus_repli("1 0 1\n1 2 1\nponctuation", "1 0 1\nponctuation");
  /* etats de repli (ordre 3) */
  verifier_refus_4_3("repli 3", "repli 4");
  verifier_refus_4_3("etats_repli 4", "etats_repli 0");
  verifier_refus_4_3("etats_repli 4", "etats_repli 5");
  verifier_refus_4_3("etats_repli 4\n0 0 0\n0 0 1", "etats_repli 4\n0 0 1\n0 0 0");
  verifier_refus_4_3("0 1 2\n1 2 3\nchaine_repli", "0 1 2\n2 2 3\nchaine_repli");     /* incoherent */
  verifier_refus_4_3("0 1 2\n1 2 3\nchaine_repli", "0 1 2\n1 2 3 3\nchaine_repli");
  verifier_refus_4_3("0 1 2\n1 2 3\nchaine_repli", "0 1 2\n0 1 2\nchaine_repli");     /* doublon */
  verifier_refus_4_3("chaine_repli 4", "chaine_repli 3");
  /* les trois derniers mots de l'etat (le, chat, dort) doivent former un
     etat de repli : la chaine de repli de "le chat" seul est refusee */
  modifie = remplacer(MODELE_VALIDE_4_3,
                      "etats_repli 4\n0 0 0\n0 0 1\n0 1 2\n1 2 3\n"
                      "chaine_repli 4\n1 1 1\n1 2 1\n1 3 1\n1 0 1\n",
                      "etats_repli 3\n0 0 0\n0 0 1\n0 1 2\n"
                      "chaine_repli 3\n1 1 1\n1 2 1\n1 0 1\n");
  verifier_refus_dans(modifie, "0\n0\n0\n0\n0\n0\n0\n0\n1 0 1\nfin",
                      "0\n0\n0\n0\n0\n0\n0\n1 0 1\n1 0 1\nfin");
  free(modifie);
  /* un mot qui ne termine aucun etat : on ne pourrait pas partir de lui */
  assert(charger_texte(mot_sans_etat, strlen(mot_sans_etat), &erreur) == NULL
         && erreur == EINVAL);
  /* fins de ligne Windows, octet nul, fichier vide */
  crlf = malloc(2u * sizeof(MODELE_VALIDE));
  avec_nul = malloc(sizeof(MODELE_VALIDE));
  assert(crlf != NULL && avec_nul != NULL);
  for (i = 0u, j = 0u; MODELE_VALIDE[i] != '\0'; i++) {
    if (MODELE_VALIDE[i] == '\n') {
      crlf[j] = '\r';
      j++;
    }
    crlf[j] = MODELE_VALIDE[i];
    j++;
  }
  assert(charger_texte(crlf, j, &erreur) == NULL && erreur == EINVAL);
  memcpy(avec_nul, MODELE_VALIDE, sizeof(MODELE_VALIDE));
  avec_nul[20] = '\0';
  assert(charger_texte(avec_nul, sizeof(MODELE_VALIDE) - 1u, &erreur) == NULL
         && erreur == EINVAL);
  assert(charger_texte("", 0u, &erreur) == NULL && erreur == EINVAL);
  free(crlf);
  free(avec_nul);
  /* fichier absent, repertoire */
  errno = 0;
  assert(modele_charger("/tmp/modele_absent_markov.txt", 1u) == NULL);
  assert(errno == ENOENT);
  repertoire = creer_repertoire_temporaire();
  errno = 0;
  assert(modele_charger(repertoire, 1u) == NULL && errno != 0);
  detruire_repertoire_temporaire(repertoire);
}


/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_generer_modele_vide(void)
{
  modele_t *m = apprendre_texte("", 1u, 1u);

  modele_generer_phrase(m);
}

static void violation_ordre_invalide(void)
{
  modele_apprendre("/tmp", 1u, MODELE_ORDRE_MAX + 1u, false);
}

static void violation_ordre_nul(void)
{
  modele_apprendre("/tmp", 1u, 0u, false);
}

static void violation_corpus_sans_chemin(void)
{
  modele_options_t options = { 1u, false, false, false, 0u };
  const char *chemins[1] = { "/tmp" };

  modele_apprendre_corpus(chemins, 0u, 1u, &options, NULL);
}

static void violation_repli_trop_grand(void)
{
  modele_options_t options = { 2u, false, false, false, 2u };
  const char *chemins[1] = { "/tmp" };

  modele_apprendre_corpus(chemins, 1u, 1u, &options, NULL);
}

static void violation_options_nulles(void)
{
  const char *chemins[1] = { "/tmp" };

  modele_apprendre_corpus(chemins, 1u, 1u, NULL, NULL);
}

static void violation_mot_nul(void)
{
  modele_t *m = apprendre_texte("Le chat dort.", 1u, 1u);

  modele_generer_phrase_depuis(m, 0u);       /* le mot vide */
}

static void violation_mot_trop_grand(void)
{
  modele_t *m = apprendre_texte("Le chat dort.", 2u, 1u);

  modele_generer_phrase_depuis(m, modele_nb_mots(m) + 1u);
}

static void violation_graphie_nulle(void)
{
  modele_t *m = apprendre_texte("Le chat dort.", 1u, 1u);

  modele_mot(m, NULL);
}

static void violation_chemin_nul(void)
{
  modele_apprendre(NULL, 1u, 1u, false);
}

static void violation_charger_chemin_nul(void)
{
  modele_charger(NULL, 1u);
}

static void violation_sauvegarder_modele_nul(void)
{
  modele_sauvegarder(NULL, "/tmp/inutile.txt");
}

static void violation_detruire_nul(void)
{
  modele_detruire(NULL);
}

static void sans_violation(void)
{
  modele_t *m = apprendre_texte("Le chat dort.", 1u, 1u);
  char *phrase = modele_generer_phrase(m);

  assert(phrase != NULL);
  free(phrase);
  phrase = modele_generer_phrase_depuis(m, modele_nb_mots(m));
  assert(phrase != NULL);
  free(phrase);
  modele_detruire(&m);
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_generer_modele_vide));
  assert(viole_contrat(violation_ordre_invalide));
  assert(viole_contrat(violation_ordre_nul));
  assert(viole_contrat(violation_corpus_sans_chemin));
  assert(viole_contrat(violation_options_nulles));
  assert(viole_contrat(violation_repli_trop_grand));
  assert(viole_contrat(violation_mot_nul));
  assert(viole_contrat(violation_mot_trop_grand));
  assert(viole_contrat(violation_graphie_nulle));
  assert(viole_contrat(violation_chemin_nul));
  assert(viole_contrat(violation_charger_chemin_nul));
  assert(viole_contrat(violation_sauvegarder_modele_nul));
  assert(viole_contrat(violation_detruire_nul));
}


int main(void)
{
  printf("Tests du module modele :\n");
  LANCER_TEST(test_generation_lisible);
  LANCER_TEST(test_generation_ordre_1);
  LANCER_TEST(test_ponctuation_interne);
  LANCER_TEST(test_homographes);
  LANCER_TEST(test_repli);
  LANCER_TEST(test_repli_tous_les_ordres);
  LANCER_TEST(test_ordre_superieur_moins_de_melanges);
  LANCER_TEST(test_dialogues);
  LANCER_TEST(test_ponctuation_tiree);
  LANCER_TEST(test_phrase_coupee);
  LANCER_TEST(test_modele_vide);
  LANCER_TEST(test_accesseurs);
  LANCER_TEST(test_accesseurs_repli);
  LANCER_TEST(test_apprendre_erreurs);
  LANCER_TEST(test_trouver_un_mot);
  LANCER_TEST(test_generer_depuis_un_mot);
  LANCER_TEST(test_contexte_le_plus_frequent);
  LANCER_TEST(test_phrase_par_ligne);
  LANCER_TEST(test_corpus_de_plusieurs_chemins);
  LANCER_TEST(test_filtre_de_langue);
  LANCER_TEST(test_format_de_sauvegarde);
  LANCER_TEST(test_format_version_2);
  LANCER_TEST(test_aller_retour_tous_les_ordres);
  LANCER_TEST(test_aller_retour_modele_vide);
  LANCER_TEST(test_sauvegarder_erreurs);
  LANCER_TEST(test_charger_fichiers_invalides);
  LANCER_TEST(test_contrats);
  afficher_bilan("modele");
  return EXIT_SUCCESS;
}
