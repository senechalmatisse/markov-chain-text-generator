/**
Fichier test_modele.c

Tests unitaires du module modele (cf. src/modele.h) : generation de
phrases lisibles, sauvegarde, chargement et refus des fichiers invalides.
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
#include "../src/sauvegarde.h"

/** Modele sauvegarde attendu pour le corpus "Le chat dort." (ordre 1). */
static const char MODELE_VALIDE[] =
  "modele-markov 1\n"
  "ordre 1\n"
  "mots 4\n"
  "- - 0 0\n"
  "le 0 0 1 le\n"
  "chat 0 0 1 chat\n"
  "dort 0 0 1 dort\n"
  "bigrammes 4\n"
  "0 0 - - 0\n"
  "0 1 - - 0\n"
  "1 2 - 0 0\n"
  "2 3 - 0 0\n"
  "chaine 4\n"
  "1 1 1\n"
  "1 2 1\n"
  "1 3 1\n"
  "1 0 1\n"
  "ponctuation 8\n"
  "0\n0\n0\n0\n0\n0\n0\n"
  "1 0 1\n"
  "fin\n";

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/** Apprend un modele sur un corpus d'un seul fichier. */
static modele_t *apprendre_texte(const char *contenu, unsigned int ordre, unsigned int graine)
{
  char *repertoire = creer_repertoire_temporaire();
  modele_t *m;

  creer_fichier_dans(repertoire, "corpus.txt", contenu);
  m = modele_apprendre(repertoire, graine, ordre, false);
  assert(m != NULL);
  detruire_repertoire_temporaire(repertoire);
  return m;
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

/** Verifie qu'une version modifiee du modele valide est refusee. */
static void verifier_refus(const char *ancien, const char *nouveau)
{
  char *modifie = remplacer(MODELE_VALIDE, ancien, nouveau);
  int erreur = 0;
  modele_t *m = charger_texte(modifie, strlen(modifie), &erreur);

  if (m != NULL || erreur != EINVAL) {
    fprintf(stderr, "\nmodification acceptee : \"%s\" -> \"%s\"\n", ancien, nouveau);
  }
  assert(m == NULL);
  assert(erreur == EINVAL);
  free(modifie);
}

/** Verifie la forme d'une phrase et que tous ses mots sont du modele. */
static void verifier_phrase(const modele_t *m, const char *phrase)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "phrase.txt");
  iterateur_mots_t *it;
  const char *mot;
  size_t longueur = strlen(phrase);
  bool fin;
  unsigned int nb_mots = 0u;

  assert(longueur >= 2u && strchr(phrase, '\n') == NULL);
  assert(phrase[longueur - 1u] == '.' || phrase[longueur - 1u] == '!'
         || phrase[longueur - 1u] == '?');
  ecrire_fichier(chemin, phrase);
  it = iterateur_mots_creer(chemin);
  assert(it != NULL);
  while ((mot = iterateur_mots_suivant(it, &fin)) != NULL) {
    assert(hachage_contient(modele_mots(m), mot));
    /* premier mot en majuscule */
    assert(nb_mots > 0u || lexique_casse(iterateur_mots_graphie(it)) != CASSE_MINUSCULE);
    nb_mots++;
  }
  assert(nb_mots > 0u);
  assert(iterateur_mots_erreur(it) == 0);
  iterateur_mots_detruire(&it);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
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

/** Phrases que la chaine d'ordre 2 doit reproduire a l'identique. */
static const char *const PHRASES_LISIBLES[] = {
  "C'\xC3\xA9tait l'\xC3\xA9t\xC3\xA9.",
  "Il a dit: peut-\xC3\xAAtre!",
  "M. Dupont va \xC3\xA0 Paris?",
  "Oui, monsieur."
};


/* ======================================================================== */
/*                               Generation                                 */
/* ======================================================================== */

static void test_generation_lisible(void)
{
  modele_t *m = apprendre_texte(CORPUS_LISIBLE, 2u, 11u);
  bool vues[4] = { false, false, false, false };
  char *phrase;
  unsigned int i;
  unsigned int k;
  bool trouvee;

  assert(modele_ordre(m) == 2u);
  assert(modele_peut_generer(m));
  for (i = 0u; i < 60u; i++) {
    phrase = modele_generer_phrase(m);
    assert(phrase != NULL);
    trouvee = false;
    for (k = 0u; k < 4u; k++) {
      if (strcmp(phrase, PHRASES_LISIBLES[k]) == 0) {
        vues[k] = true;
        trouvee = true;
      }
    }
    if (!trouvee) {
      fprintf(stderr, "\nphrase inattendue : \"%s\"\n", phrase);
    }
    assert(trouvee);
    verifier_phrase(m, phrase);
    free(phrase);
  }
  assert(vues[0] && vues[1] && vues[2] && vues[3]);
  modele_detruire(&m);
  assert(m == NULL);
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
  assert(markov_nb_etats(modele_chaine(m))
         == hachage_paires_nb(modele_bigrammes(m)));
  assert(lexique_est_finalise(modele_lexique(m)));
  assert(markov_nb_etats(modele_ponctuation(m)) == REPERTOIRE_NB_FINS + 5u);
  modele_detruire(&m);
}

static void test_apprendre_erreurs(void)
{
  errno = 0;
  assert(modele_apprendre("/tmp/repertoire_absent_markov", 1u, 1u, false) == NULL);
  assert(errno == ENOENT);
  errno = 0;
  assert(modele_apprendre("Makefile", 1u, 2u, true) == NULL);
  assert(errno == ENOTDIR);
}


/* ======================================================================== */
/*                         Sauvegarde et chargement                         */
/* ======================================================================== */

static void test_format_de_sauvegarde(void)
{
  char *repertoire = creer_repertoire_temporaire();
  char *chemin = chemin_dans(repertoire, "modele.txt");
  modele_t *m = apprendre_texte("Le chat dort.", 1u, 1u);
  char *contenu;
  char *phrase;
  int erreur = 0;

  assert(modele_sauvegarder(m, chemin));
  contenu = lire_fichier(chemin);
  assert(contenu != NULL);
  if (strcmp(contenu, MODELE_VALIDE) != 0) {
    fprintf(stderr, "\nfichier obtenu :\n%s", contenu);
  }
  assert(strcmp(contenu, MODELE_VALIDE) == 0);
  free(contenu);
  modele_detruire(&m);
  /* un fichier ecrit a la main au bon format est accepte */
  m = charger_texte(MODELE_VALIDE, strlen(MODELE_VALIDE), &erreur);
  assert(m != NULL && modele_peut_generer(m));
  phrase = modele_generer_phrase(m);
  assert(strcmp(phrase, "Le chat dort.") == 0);
  free(phrase);
  modele_detruire(&m);
  free(chemin);
  detruire_repertoire_temporaire(repertoire);
}

/** Apprend, sauvegarde, recharge et compare (ordre donne). */
static void verifier_aller_retour(unsigned int ordre)
{
  char *repertoire = creer_repertoire_temporaire();
  char *corpus = chemin_dans(repertoire, "corpus");
  char *chemin = chemin_dans(repertoire, "modele.txt");
  char *chemin_bis = chemin_dans(repertoire, "modele_bis.txt");
  modele_t *appris;
  modele_t *charge;
  const lexique_t *la;
  const lexique_t *lc;
  char *pa;
  char *pc;
  char *contenu;
  char *contenu_bis;
  unsigned int i;

  assert(mkdir(corpus, 0700) == 0);
  creer_fichier_dans(corpus, "a.txt", CORPUS_LISIBLE);
  creer_fichier_dans(corpus, "b.txt", "\xC9t\xE9 comme hiver, il va \xE0 la "
                     "plage. L'\xE9t\xE9, c'est la plage !");  /* Latin-1 */
  creer_fichier_dans(corpus, "c.txt", "etait il a dit la plage\n\nFIN");
  appris = modele_apprendre(corpus, 77u, ordre, false);
  assert(appris != NULL);
  assert(modele_sauvegarder(appris, chemin));
  charge = modele_charger(chemin, 77u);
  assert(charge != NULL);
  /* memes mots, memes paires, memes chaines, memes choix */
  assert(modele_ordre(charge) == ordre);
  assert(modele_nb_mots(charge) == modele_nb_mots(appris));
  for (i = 1u; i <= modele_nb_mots(appris); i++) {
    assert(strcmp(hachage_mot(modele_mots(charge), i),
                  hachage_mot(modele_mots(appris), i)) == 0);
  }
  assert(hachage_paires_nb(modele_bigrammes(charge))
         == hachage_paires_nb(modele_bigrammes(appris)));
  verifier_chaines_egales(modele_chaine(appris), modele_chaine(charge));
  verifier_chaines_egales(modele_ponctuation(appris), modele_ponctuation(charge));
  la = modele_lexique(appris);
  lc = modele_lexique(charge);
  for (i = 0u; i < lexique_nb_bigrammes(la); i++) {
    assert(lexique_variante_bigramme(la, i) == lexique_variante_bigramme(lc, i));
    assert(lexique_casse_bigramme(la, i) == lexique_casse_bigramme(lc, i));
    assert(lexique_liaison(la, i) == lexique_liaison(lc, i));
  }
  /* meme graine : exactement les memes phrases */
  for (i = 0u; i < 300u; i++) {
    pa = modele_generer_phrase(appris);
    pc = modele_generer_phrase(charge);
    assert(pa != NULL && pc != NULL && strcmp(pa, pc) == 0);
    verifier_phrase(charge, pc);
    free(pa);
    free(pc);
  }
  /* resauvegarder le modele charge redonne le meme fichier */
  assert(modele_sauvegarder(charge, chemin_bis));
  contenu = lire_fichier(chemin);
  contenu_bis = lire_fichier(chemin_bis);
  assert(contenu != NULL && contenu_bis != NULL
         && strcmp(contenu, contenu_bis) == 0);
  free(contenu);
  free(contenu_bis);
  modele_detruire(&appris);
  modele_detruire(&charge);
  free(corpus);
  free(chemin);
  free(chemin_bis);
  detruire_repertoire_temporaire(repertoire);
}

static void test_aller_retour_ordre_1(void)
{
  verifier_aller_retour(1u);
}

static void test_aller_retour_ordre_2(void)
{
  verifier_aller_retour(2u);
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

static void test_charger_fichiers_invalides(void)
{
  char *crlf;
  char *avec_nul;
  size_t i;
  size_t j;
  int erreur = 0;
  char *repertoire;

  /* en-tete, ordre, nombres */
  verifier_refus("modele-markov 1", "modele-markov 2");
  verifier_refus("ordre 1", "ordre 3");
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
  verifier_refus("0 0 - - 0", "1 1 - - 0");
  verifier_refus("2 3 - 0 0", "2 0 - 0 0");
  verifier_refus("2 3 - 0 0", "1 2 - 0 0");                  /* doublon */
  verifier_refus("1 2 - 0 0", "1 2 - 0 7");                  /* liaison */
  verifier_refus("1 2 - 0 0", "1 2 1 0 0");                  /* variante */
  verifier_refus("1 2 - 0 0", "1 7 - 0 0");
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
  verifier_refus("0\n1 0 1\nfin", "0\n0\nfin");              /* fin sans . */
  verifier_refus("0\n1 0 1\nfin", "0\n1 5 1\nfin");          /* vers un mot */
  verifier_refus("ponctuation 8\n0", "ponctuation 8\n1 1 1");
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
  modele_apprendre("/tmp", 1u, 3u, false);
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
  modele_detruire(&m);
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_generer_modele_vide));
  assert(viole_contrat(violation_ordre_invalide));
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
  LANCER_TEST(test_ponctuation_tiree);
  LANCER_TEST(test_phrase_coupee);
  LANCER_TEST(test_modele_vide);
  LANCER_TEST(test_accesseurs);
  LANCER_TEST(test_apprendre_erreurs);
  LANCER_TEST(test_format_de_sauvegarde);
  LANCER_TEST(test_aller_retour_ordre_1);
  LANCER_TEST(test_aller_retour_ordre_2);
  LANCER_TEST(test_aller_retour_modele_vide);
  LANCER_TEST(test_sauvegarder_erreurs);
  LANCER_TEST(test_charger_fichiers_invalides);
  LANCER_TEST(test_contrats);
  afficher_bilan("modele");
  return EXIT_SUCCESS;
}
