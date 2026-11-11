/**
Fichier test_defaillances.c

Test des chemins d'erreur "memoire insuffisante" de tous les modules.

L'executable est lie avec l'option --wrap de l'editeur de liens GNU
(cf. cible test_defaillances du Makefile) : les appels a malloc, calloc,
realloc et free du projet passent par les fonctions __wrap_* ci-dessous,
qui comptent les blocs vivants et peuvent faire echouer une allocation.

Chaque scenario est rejoue en faisant echouer la 1re allocation, puis la
2e, ..., jusqu'a ce qu'il se deroule sans atteindre l'echec programme ;
puis de nouveau en faisant echouer toutes les allocations a partir de la
N-ieme (echecs persistants). A chaque fois on verifie :
  - que la fonction signale l'echec (NULL, false, ...) avec errno == ENOMEM ;
  - que la structure modifiee est inchangee, comme le promet le contrat ;
  - qu'aucun bloc n'est perdu (compteur de blocs vivants revenu a zero,
    et valgrind dans test_memoire).
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include "outils_tests.h"
#include "../src/hachage.h"
#include "../src/markov.h"
#include "../src/lexique.h"
#include "../src/langue.h"
#include "../src/repertoire.h"
#include "../src/sauvegarde.h"
#include "../src/modele.h"

/* ======================================================================== */
/*                        Allocateur instrumente                            */
/* ======================================================================== */

void *__real_malloc(size_t taille);
void *__real_calloc(size_t nombre, size_t taille);
void *__real_realloc(void *bloc, size_t taille);
void __real_free(void *bloc);
void *__wrap_malloc(size_t taille);
void *__wrap_calloc(size_t nombre, size_t taille);
void *__wrap_realloc(void *bloc, size_t taille);
void __wrap_free(void *bloc);

/** Les allocations comptent (et peuvent echouer) seulement sous
    surveillance, c'est-a-dire pendant l'appel d'une fonction testee. */
static bool surveillance = false;
/** Rang de l'allocation qui echoue (0 : aucune). */
static unsigned long rang_echec = 0u;
/** Toutes les allocations echouent a partir du rang d'echec. */
static bool echecs_persistants = false;
/** Nombre d'allocations surveillees depuis le debut du scenario. */
static unsigned long nb_allocations = 0u;
/** Un echec a ete provoque pendant le scenario. */
static bool echec_provoque = false;
/** Blocs alloues et pas encore liberes (surveilles ou non). */
static long blocs_vivants = 0;
/** Nombre total d'executions de scenarios. */
static unsigned long nb_executions = 0u;

static bool doit_echouer(void)
{
  bool echec = false;

  if (surveillance) {
    nb_allocations++;
    if (rang_echec != 0u
        && (nb_allocations == rang_echec
            || (echecs_persistants && nb_allocations > rang_echec))) {
      echec = true;
      echec_provoque = true;
    }
  }
  return echec;
}

void *__wrap_malloc(size_t taille)
{
  void *bloc = NULL;

  if (doit_echouer()) {
    errno = ENOMEM;
  } else {
    bloc = __real_malloc(taille);
    if (bloc != NULL) {
      blocs_vivants++;
    }
  }
  return bloc;
}

void *__wrap_calloc(size_t nombre, size_t taille)
{
  void *bloc = NULL;

  if (doit_echouer()) {
    errno = ENOMEM;
  } else {
    bloc = __real_calloc(nombre, taille);
    if (bloc != NULL) {
      blocs_vivants++;
    }
  }
  return bloc;
}

void *__wrap_realloc(void *bloc, size_t taille)
{
  void *nouveau = NULL;

  if (doit_echouer()) {
    errno = ENOMEM;                   /* l'ancien bloc reste valide */
  } else {
    nouveau = __real_realloc(bloc, taille);
    if (nouveau != NULL && bloc == NULL) {
      blocs_vivants++;
    }
  }
  return nouveau;
}

void __wrap_free(void *bloc)
{
  if (bloc != NULL) {
    blocs_vivants--;
  }
  __real_free(bloc);
}

/** Execute une instruction en surveillant ses allocations. */
#define SURVEILLER(instruction) \
  do {                          \
    surveillance = true;        \
    instruction;                \
    surveillance = false;       \
  } while (0)

/**
 * Rejoue un scenario avec un echec a chaque rang possible, d'abord isole
 * puis persistant. Le scenario verifie lui-meme les post-conditions ; ici
 * on verifie qu'il ne perd aucun bloc.
 */
static void rejouer(void (*scenario)(void))
{
  unsigned int mode;
  unsigned long rang;
  long vivants_avant;

  for (mode = 0u; mode < 2u; mode++) {
    rang = 1u;
    do {
      vivants_avant = blocs_vivants;
      rang_echec = rang;
      echecs_persistants = (mode == 1u);
      nb_allocations = 0u;
      echec_provoque = false;
      scenario();
      assert(!surveillance);
      if (blocs_vivants != vivants_avant) {
        fprintf(stderr, "\nfuite : %ld bloc(s) perdu(s) (echec au rang %lu%s)"
                "\n", blocs_vivants - vivants_avant, rang,
                echecs_persistants ? ", persistant" : "");
      }
      assert(blocs_vivants == vivants_avant);
      nb_executions++;
      rang++;
    } while (echec_provoque);
  }
  rang_echec = 0u;
}


/* ======================================================================== */
/*                               Donnees                                    */
/* ======================================================================== */

/** Corpus de test : un fichier, et un autre dans un sous-repertoire. */
static char *corpus = NULL;
static char *chemin_texte = NULL;
static char *chemin_modele = NULL;
static char *chemin_modele_3 = NULL;
static char *chemin_sortie = NULL;
static modele_t *modele = NULL;
static modele_t *modele_3 = NULL;

/** Corpus du filtre de langue : deux textes francais, un texte anglais. */
static char *corpus_langues = NULL;
static char *chemin_anglais = NULL;

/** Textes courants pour le filtre de langue. */
static const char TEXTE_FRANCAIS[] =
  "Le chat dort sur le toit de la maison. Il regarde les oiseaux qui "
  "passent dans le ciel. La petite fille ouvre la porte et appelle son "
  "chat. Elle ne veut pas qu'il reste dehors pendant la nuit. Le chat "
  "descend du toit et vient pres d'elle. Ils rentrent tous les deux dans "
  "la cuisine. La mere a prepare une soupe pour le repas du soir.\n";
static const char TEXTE_ANGLAIS[] =
  "The old man walked to the river with his dog. He had not seen the water "
  "for many years. It was cold and the wind was strong. She said that they "
  "would come back in the morning. There was a small house on the hill.\n";

/** Mot tres long (plus grand qu'un bloc de stockage). */
static char mot_long[70001];

/** n-ieme mot de l'ordre "a, b, ..., z, aa, ab, ..." */
static void enieme_mot(unsigned long n, char *mot)
{
  char inverse[16];
  size_t longueur = 0u;
  size_t i;

  n++;
  while (n > 0u) {
    n--;
    inverse[longueur] = (char)('a' + (n % 26u));
    longueur++;
    n /= 26u;
  }
  for (i = 0u; i < longueur; i++) {
    mot[i] = inverse[longueur - 1u - i];
  }
  mot[longueur] = '\0';
}

/** i-ieme mot du scenario de la table (le 100e est tres long). */
static const char *mot_scenario(unsigned long i, char *tampon)
{
  enieme_mot(i, tampon);
  return (i == 100u) ? mot_long : tampon;
}


/* ======================================================================== */
/*                              Scenarios                                   */
/* ======================================================================== */

static void scenario_table_des_mots(void)
{
  hachage_t *h = NULL;
  char tampon[16];
  char autre[16];
  unsigned long i;
  unsigned long j;
  unsigned int identifiant;
  unsigned int nb_avant;
  bool arret = false;

  SURVEILLER(h = hachage_creer(0u));
  if (h == NULL) {
    assert(errno == ENOMEM);
  } else {
    for (i = 0u; !arret && i < 200u; i++) {
      nb_avant = hachage_nb_mots(h);
      SURVEILLER(identifiant = hachage_ajouter(h, mot_scenario(i, tampon)));
      if (identifiant == HACHAGE_ABSENT) {
        /* echec : la table contient exactement les memes mots */
        assert(errno == ENOMEM);
        assert(hachage_nb_mots(h) == nb_avant);
        assert(!hachage_contient(h, mot_scenario(i, tampon)));
        for (j = 0u; j < i; j++) {
          assert(hachage_identifiant(h, mot_scenario(j, autre)) == j);
        }
        arret = true;
      } else {
        assert(identifiant == i);
      }
    }
    hachage_detruire(&h);
  }
}

static void scenario_table_des_paires(void)
{
  hachage_paires_t *p = NULL;
  unsigned int i;
  unsigned int j;
  unsigned int identifiant;
  bool arret = false;

  SURVEILLER(p = hachage_paires_creer(0u));
  if (p == NULL) {
    assert(errno == ENOMEM);
  } else {
    for (i = 0u; !arret && i < 2000u; i++) {
      SURVEILLER(identifiant = hachage_paires_ajouter(p, i / 40u, i % 40u));
      if (identifiant == HACHAGE_ABSENT) {
        assert(errno == ENOMEM);
        assert(hachage_paires_nb(p) == i);
        assert(hachage_paires_identifiant(p, i / 40u, i % 40u)
               == HACHAGE_ABSENT);
        for (j = 0u; j < i; j++) {
          assert(hachage_paires_identifiant(p, j / 40u, j % 40u) == j);
        }
        arret = true;
      } else {
        assert(identifiant == i);
      }
    }
    hachage_paires_detruire(&p);
  }
}

#define NB_ETATS_SCENARIO 40u

static void scenario_chaine_de_markov(void)
{
  static unsigned int attendus[NB_ETATS_SCENARIO][NB_ETATS_SCENARIO];
  markov_t *m = NULL;
  unsigned int i;
  unsigned int e;
  unsigned int a;
  unsigned int depart;
  unsigned int arrivee;
  unsigned int etat;
  bool succes = true;

  memset(attendus, 0, sizeof(attendus));
  SURVEILLER(m = markov_creer(1u));
  if (m == NULL) {
    assert(errno == ENOMEM);
  } else {
    for (i = 0u; succes && i < NB_ETATS_SCENARIO; i++) {
      SURVEILLER(etat = markov_ajouter_etat(m));
      succes = (etat == i);
      if (!succes) {
        assert(etat == MARKOV_ETAT_INVALIDE && errno == ENOMEM);
        assert(markov_nb_etats(m) == i);
      }
    }
    /* l'etat 0 vers tous les etats (agrandissements), les autres vers
       quelques etats */
    for (i = 0u; succes && i < 300u; i++) {
      depart = (i < NB_ETATS_SCENARIO) ? 0u : (i * 7u) % NB_ETATS_SCENARIO;
      arrivee = (i < NB_ETATS_SCENARIO) ? i : (i * 13u) % NB_ETATS_SCENARIO;
      SURVEILLER(succes = markov_augmenter_poids(m, depart, arrivee, 1u + i % 3u));
      if (succes) {
        attendus[depart][arrivee] += 1u + i % 3u;
      } else {
        assert(errno == ENOMEM);
      }
    }
    /* en cas d'echec comme apres la finalisation, les poids sont exacts */
    if (succes) {
      SURVEILLER(markov_finaliser(m));
    }
    if (markov_nb_etats(m) == NB_ETATS_SCENARIO) {
      for (e = 0u; e < NB_ETATS_SCENARIO; e++) {
        for (a = 0u; a < NB_ETATS_SCENARIO; a++) {
          assert(markov_poids(m, e, a) == attendus[e][a]);
        }
      }
    }
    if (succes) {
      for (i = 0u; i < 50u; i++) {
        assert(markov_poids(m, 0u, markov_deplacer(m, 0u)) > 0u);
      }
    }
    markov_detruire(&m);
  }
}

/** Graphies observees par le scenario du lexique. */
static const char *const GRAPHIES[] = {
  "\xC3\xA9t\xC3\xA9", "ete", "Ete", "\xC3\x89T\xC3\x89", "et\xC3\xA9",
  "\xC5\x92uvre"
};

static void scenario_lexique(void)
{
  static char longue[3001];
  lexique_t *l = NULL;
  unsigned int i;
  unsigned int r;
  unsigned int mot;
  unsigned int nb_avant;
  unsigned int resultat;
  char tampon[4000];
  bool succes = true;
  bool observe;

  memset(longue, 'z', sizeof(longue) - 1u);
  SURVEILLER(l = lexique_creer());
  if (l == NULL) {
    assert(errno == ENOMEM);
  } else {
    for (i = 0u; succes && i < 30u; i++) {
      SURVEILLER(resultat = lexique_ajouter_mot(l));
      succes = (resultat == i);
      if (succes) {
        SURVEILLER(resultat = lexique_ajouter_bigramme(l, i));
        succes = (resultat == i);
        if (!succes) {
          assert(resultat == LEXIQUE_ABSENT && errno == ENOMEM);
          assert(lexique_nb_bigrammes(l) == i);
        }
      } else {
        assert(resultat == LEXIQUE_ABSENT && errno == ENOMEM);
        assert(lexique_nb_mots(l) == i);
      }
    }
    for (i = 0u; succes && i < 200u; i++) {
      mot = i % 29u + 1u;
      nb_avant = (mot < lexique_nb_mots(l)) ? lexique_nb_variantes(l, mot) : 0u;
      if (mot < lexique_nb_bigrammes(l)) {
        SURVEILLER(observe = lexique_observer(l, mot,
                                              (i == 77u) ? longue
                                              : GRAPHIES[i % 6u],
                                              i % 2u == 0u, i % 5u == 0u,
                                              LIAISON_VIRGULE,
                                              (i % 3u == 0u)
                                              ? MARQUE_TIRET
                                              : MARQUE_AUCUNE));
        if (!observe) {
          /* echec : ni nouvelle variante, ni variante modifiee */
          assert(errno == ENOMEM);
          assert(lexique_nb_variantes(l, mot) == nb_avant);
          for (r = 0u; r < nb_avant; r++) {
            assert(lexique_variante_valide(lexique_variante(l, mot, r)));
          }
          succes = false;
        }
      }
    }
    if (succes) {
      SURVEILLER(lexique_finaliser(l));
      assert(lexique_ecrire(l, 1u, true, tampon, sizeof(tampon)) > 0u);
    }
    lexique_detruire(&l);
  }
}

static void scenario_iterateur(void)
{
  iterateur_mots_t *it = NULL;
  const char *mot;
  unsigned int nb_mots = 0u;
  bool fin;

  SURVEILLER(it = iterateur_mots_creer(chemin_texte));
  if (it == NULL) {
    assert(errno == ENOMEM);
  } else {
    do {
      SURVEILLER(mot = iterateur_mots_suivant(it, &fin));
      nb_mots += (mot != NULL) ? 1u : 0u;
    } while (mot != NULL);
    /* fin normale (tous les mots lus) ou erreur ENOMEM signalee */
    assert(iterateur_mots_erreur(it) == ENOMEM || (iterateur_mots_erreur(it) == 0 && nb_mots == 7u));
    iterateur_mots_detruire(&it);
  }
}

static void scenario_liste_des_fichiers(void)
{
  char **fichiers = NULL;
  size_t nb = 0u;

  SURVEILLER(fichiers = repertoire_lister_fichiers(corpus, true, &nb));
  if (fichiers == NULL) {
    assert(errno == ENOMEM);
  } else {
    assert(nb == 2u);
    repertoire_liberer_fichiers(&fichiers, nb);
  }
}

static void scenario_repertoire_creer_markov(void)
{
  hachage_t *mots = hachage_creer(0u);
  markov_t *m = NULL;

  assert(mots != NULL);
  SURVEILLER(m = repertoire_creer_markov(corpus, 1u, mots));
  if (m == NULL) {
    assert(errno == ENOMEM);
  } else {
    assert(markov_nb_etats(m) == hachage_nb_mots(mots));
    markov_detruire(&m);
  }
  hachage_detruire(&mots);
}

/** Apprentissage complet d'un modele, d'ordre donne. */
static void apprendre_modele(unsigned int ordre)
{
  modele_t *m = NULL;
  char *phrase;

  SURVEILLER(m = modele_apprendre(corpus, 3u, ordre, true));
  if (m == NULL) {
    assert(errno == ENOMEM);
  } else {
    phrase = modele_generer_phrase(m);
    assert(phrase != NULL);
    free(phrase);
    modele_detruire(&m);
  }
}

static void scenario_apprendre_ordre_1(void)
{
  apprendre_modele(1u);
}

static void scenario_apprendre_ordre_2(void)
{
  apprendre_modele(2u);
}

static void scenario_generer_phrase(void)
{
  char *phrase = NULL;

  SURVEILLER(phrase = modele_generer_phrase(modele));
  if (phrase == NULL) {
    assert(errno == ENOMEM);
  }
  free(phrase);
  /* le modele reste utilisable */
  phrase = modele_generer_phrase(modele);
  assert(phrase != NULL);
  free(phrase);
}

static void scenario_sauvegarder(void)
{
  bool succes;

  SURVEILLER(succes = modele_sauvegarder(modele, chemin_sortie));
  if (succes) {
    assert(remove(chemin_sortie) == 0);
  } else {
    /* pas de modele incomplet */
    assert(errno == ENOMEM);
    assert(!sauvegarde_existe(chemin_sortie));
  }
}

static void scenario_charger(void)
{
  modele_t *m = NULL;

  SURVEILLER(m = modele_charger(chemin_modele, 1u));
  if (m == NULL) {
    assert(errno == ENOMEM);
  } else {
    assert(modele_nb_mots(m) == modele_nb_mots(modele));
    modele_detruire(&m);
  }
}


static void scenario_table_des_nuplets(void)
{
  hachage_nuplets_t *t = NULL;
  unsigned int elements[3];
  unsigned int i;
  unsigned int j;
  unsigned int identifiant;
  bool arret = false;

  SURVEILLER(t = hachage_nuplets_creer(3u, 0u));
  if (t == NULL) {
    assert(errno == ENOMEM);
  } else {
    for (i = 0u; !arret && i < 2000u; i++) {
      elements[0] = i / 100u;
      elements[1] = (i / 10u) % 10u;
      elements[2] = i % 10u;
      SURVEILLER(identifiant = hachage_nuplets_ajouter(t, elements));
      if (identifiant == HACHAGE_ABSENT) {
        /* echec : memes n-uplets, memes identifiants */
        assert(errno == ENOMEM);
        assert(hachage_nuplets_nb(t) == i);
        assert(hachage_nuplets_identifiant(t, elements) == HACHAGE_ABSENT);
        for (j = 0u; j < i; j++) {
          elements[0] = j / 100u;
          elements[1] = (j / 10u) % 10u;
          elements[2] = j % 10u;
          assert(hachage_nuplets_identifiant(t, elements) == j);
          assert(hachage_nuplets_element(t, j, 2u) == j % 10u);
        }
        arret = true;
      } else {
        assert(identifiant == i);
      }
    }
    hachage_nuplets_detruire(&t);
  }
}

/** Mots (sur {a..z}) donnes au detecteur de langue, un point = fin de phrase. */
static const char *const MOTS_LANGUES[] = {
  "le", "chat", "dort", "sur", "le", "toit", "de", "la", "maison", ".",
  "il", "regarde", "les", "oiseaux", "qui", "passent", "dans", "le", "ciel",
  ".", "the", "old", "man", "walked", "to", "the", "river", "with", "his",
  "dog", ".", "he", "had", "not", "seen", "the", "water", "for", "many",
  "years", ".", "la", "petite", "fille", "ouvre", "la", "porte", "et",
  "appelle", "son", "chat", ".", "elle", "ne", "veut", "pas", "qu", "il",
  "reste", "dehors", "pendant", "la", "nuit", "."
};

static void scenario_langue(void)
{
  const size_t nb = sizeof(MOTS_LANGUES) / sizeof(MOTS_LANGUES[0]);
  langue_detecteur_t *d = NULL;
  size_t nb_mots_texte = 0u;
  size_t i;
  size_t r;
  size_t nb_mots_avant;
  size_t nb_phrases_avant;
  bool succes = true;
  bool fin;

  for (i = 0u; i < nb; i++) {
    nb_mots_texte += (MOTS_LANGUES[i][0] != '.') ? 1u : 0u;
  }
  SURVEILLER(d = langue_detecteur_creer());
  if (d == NULL) {
    assert(errno == ENOMEM);
  } else {
    /* le texte est donne trois fois (trois textes) : agrandissements */
    for (r = 0u; succes && r < 3u; r++) {
      for (i = 0u; succes && i < nb; i++) {
        if (MOTS_LANGUES[i][0] != '.') {
          fin = (i + 1u < nb && MOTS_LANGUES[i + 1u][0] == '.');
          nb_mots_avant = langue_detecteur_nb_mots(d);
          nb_phrases_avant = langue_detecteur_nb_phrases(d);
          SURVEILLER(succes = langue_detecteur_ajouter_mot(d, MOTS_LANGUES[i], fin));
          if (!succes) {
            /* echec : rien n'a change */
            assert(errno == ENOMEM);
            assert(langue_detecteur_nb_mots(d) == nb_mots_avant);
            assert(langue_detecteur_nb_phrases(d) == nb_phrases_avant);
          }
        }
      }
      langue_detecteur_fin_texte(d);
    }
    if (succes) {
      SURVEILLER(succes = langue_detecteur_conclure(d));
      if (!succes) {
        /* echec : pas de conclusion, et on peut recommencer */
        assert(errno == ENOMEM);
        assert(!langue_detecteur_est_conclu(d));
        assert(langue_detecteur_nb_mots(d) == 3u * nb_mots_texte);
        assert(langue_detecteur_conclure(d));
      }
      assert(langue_detecteur_majoritaire(d) == LANGUE_FRANCAIS);
      assert(langue_detecteur_nb_phrases(d) == 3u * 6u);
    }
    langue_detecteur_detruire(&d);
  }
}

static void scenario_liste_du_corpus(void)
{
  const char *chemins[2];
  char **fichiers = NULL;
  size_t nb = 0u;

  chemins[0] = chemin_texte;
  chemins[1] = corpus;
  SURVEILLER(fichiers = repertoire_lister_corpus(chemins, 2u, true, &nb));
  if (fichiers == NULL) {
    assert(errno == ENOMEM);
  } else {
    assert(nb == 3u);
    assert(strcmp(fichiers[0], chemin_texte) == 0);
    repertoire_liberer_fichiers(&fichiers, nb);
  }
}

static void scenario_cle(void)
{
  char *cle = NULL;

  SURVEILLER(cle = repertoire_cle("\xC3\x89t\xC3\xA9"));
  if (cle == NULL) {
    assert(errno == ENOMEM);
  } else {
    assert(strcmp(cle, "ete") == 0);
    free(cle);
  }
}

static void scenario_modele_mot(void)
{
  unsigned int mot = 0u;

  SURVEILLER(mot = modele_mot(modele, "\xC3\x89T\xC3\x89"));
  if (mot == HACHAGE_ABSENT) {
    assert(errno == ENOMEM);
  } else {
    assert(strcmp(hachage_mot(modele_mots(modele), mot), "ete") == 0);
  }
}

/**
 * Apprentissage complet a l'ordre 3, avec toutes les options : corpus de
 * deux chemins, filtre de langue, sous-repertoires, une phrase par ligne.
 */
static void scenario_apprendre_toutes_options(void)
{
  const repertoire_bilan_t temoin = { 7u, 7u, 7u, 7u, 7u, LANGUE_ALLEMAND };
  repertoire_bilan_t bilan = temoin;
  modele_options_t options = { 3u, true, true, true };
  const char *chemins[2];
  modele_t *m = NULL;
  char *phrase;

  chemins[0] = corpus_langues;
  chemins[1] = chemin_anglais;
  SURVEILLER(m = modele_apprendre_corpus(chemins, 2u, 3u, &options, &bilan));
  if (m == NULL) {
    /* echec : bilan inchange */
    assert(errno == ENOMEM);
    assert(bilan.nb_fichiers == temoin.nb_fichiers && bilan.langue == temoin.langue);
  } else {
    assert(bilan.nb_fichiers == 3u && bilan.langue == LANGUE_FRANCAIS);
    assert(bilan.nb_phrases_ecartees == 5u);
    assert(modele_mot(m, "river") == HACHAGE_ABSENT);
    phrase = modele_generer_phrase(m);
    assert(phrase != NULL);
    free(phrase);
    modele_detruire(&m);
  }
}

static void scenario_apprendre_ordre_3(void)
{
  apprendre_modele(3u);
}

static void scenario_generer_phrase_depuis(void)
{
  unsigned int mots[3];
  char *phrase = NULL;
  unsigned int i;

  /* un mot qui commence une phrase, un qui n'en commence pas (recherche
     du contexte), et le meme encore (contexte memorise) */
  mots[0] = modele_mot(modele_3, "oui");
  mots[1] = modele_mot(modele_3, "dupont");
  mots[2] = mots[1];
  assert(mots[0] != HACHAGE_ABSENT && mots[1] != HACHAGE_ABSENT);
  for (i = 0u; i < 3u; i++) {
    SURVEILLER(phrase = modele_generer_phrase_depuis(modele_3, mots[i]));
    if (phrase == NULL) {
      assert(errno == ENOMEM);
    }
    free(phrase);
  }
  /* le modele reste utilisable */
  phrase = modele_generer_phrase_depuis(modele_3, mots[1]);
  assert(phrase != NULL && strncmp(phrase, "Dupont", 6u) == 0);
  free(phrase);
}

static void scenario_charger_ordre_3(void)
{
  modele_t *m = NULL;

  SURVEILLER(m = modele_charger(chemin_modele_3, 1u));
  if (m == NULL) {
    assert(errno == ENOMEM);
  } else {
    assert(modele_ordre(m) == 3u);
    assert(modele_nb_mots(m) == modele_nb_mots(modele_3));
    modele_detruire(&m);
  }
}


/* ======================================================================== */
/*                                 Tests                                    */
/* ======================================================================== */

static void test_table_des_mots(void)
{
  rejouer(scenario_table_des_mots);
}

static void test_table_des_paires(void)
{
  rejouer(scenario_table_des_paires);
}

static void test_chaine_de_markov(void)
{
  rejouer(scenario_chaine_de_markov);
}

static void test_lexique(void)
{
  rejouer(scenario_lexique);
}

static void test_iterateur(void)
{
  rejouer(scenario_iterateur);
}

static void test_liste_des_fichiers(void)
{
  rejouer(scenario_liste_des_fichiers);
}

static void test_repertoire_creer_markov(void)
{
  rejouer(scenario_repertoire_creer_markov);
}

static void test_apprendre_ordre_1(void)
{
  rejouer(scenario_apprendre_ordre_1);
}

static void test_apprendre_ordre_2(void)
{
  rejouer(scenario_apprendre_ordre_2);
}

static void test_generer_phrase(void)
{
  rejouer(scenario_generer_phrase);
}

static void test_sauvegarder(void)
{
  rejouer(scenario_sauvegarder);
}

static void test_charger(void)
{
  rejouer(scenario_charger);
}

static void test_table_des_nuplets(void)
{
  rejouer(scenario_table_des_nuplets);
}

static void test_langue(void)
{
  rejouer(scenario_langue);
}

static void test_liste_du_corpus(void)
{
  rejouer(scenario_liste_du_corpus);
}

static void test_cle(void)
{
  rejouer(scenario_cle);
}

static void test_modele_mot(void)
{
  rejouer(scenario_modele_mot);
}

static void test_apprendre_ordre_3(void)
{
  rejouer(scenario_apprendre_ordre_3);
}

static void test_apprendre_toutes_options(void)
{
  rejouer(scenario_apprendre_toutes_options);
}

static void test_generer_phrase_depuis(void)
{
  rejouer(scenario_generer_phrase_depuis);
}

static void test_charger_ordre_3(void)
{
  rejouer(scenario_charger_ordre_3);
}

int main(void)
{
  static char texte_long[5100];
  char *sous;
  char *autre;
  long vivants_initiaux;

  /* donnees communes, creees sans surveillance */
  memset(mot_long, 'q', sizeof(mot_long) - 1u);
  corpus = creer_repertoire_temporaire();
  sous = chemin_dans(corpus, "sous");
  assert(mkdir(sous, 0700) == 0);
  creer_fichier_dans(corpus, "a.txt",
                     "C'\xC3\xA9tait l'\xC3\xA9t\xC3\xA9. Il a dit : "
                     "peut-\xC3\xAAtre ! M. Dupont va \xC3\xA0 Paris ?");
  creer_fichier_dans(sous, "b.txt", "Oui, monsieur. Le chat dort.");
  /* texte de l'iterateur (hors corpus) : 7 mots dont un de 5000 lettres,
     qui oblige a agrandir plusieurs fois les tampons du mot */
  autre = creer_repertoire_temporaire();
  strcpy(texte_long, "Oui, monsieur. Le chat dort. ");
  memset(texte_long + strlen(texte_long), 'x', 5000u);
  strcpy(texte_long + 29u + 5000u, " fin.");
  chemin_texte = chemin_dans(autre, "texte.txt");
  ecrire_fichier(chemin_texte, texte_long);
  chemin_modele = chemin_dans(autre, "modele.sauvegarde");
  chemin_modele_3 = chemin_dans(autre, "modele_3.sauvegarde");
  chemin_sortie = chemin_dans(autre, "sortie.sauvegarde");
  modele = modele_apprendre(corpus, 5u, 2u, true);
  assert(modele != NULL);
  assert(modele_sauvegarder(modele, chemin_modele));
  modele_3 = modele_apprendre(corpus, 5u, 3u, true);
  assert(modele_3 != NULL);
  assert(modele_sauvegarder(modele_3, chemin_modele_3));
  /* filtre de langue : deux textes francais, un anglais (5 phrases) */
  corpus_langues = creer_repertoire_temporaire();
  creer_fichier_dans(corpus_langues, "a.txt", TEXTE_FRANCAIS);
  creer_fichier_dans(corpus_langues, "b.txt", TEXTE_FRANCAIS);
  chemin_anglais = chemin_dans(autre, "anglais.txt");
  ecrire_fichier(chemin_anglais, TEXTE_ANGLAIS);
  vivants_initiaux = blocs_vivants;

  printf("Tests des defaillances d'allocation :\n");
  LANCER_TEST(test_table_des_mots);
  LANCER_TEST(test_table_des_paires);
  LANCER_TEST(test_chaine_de_markov);
  LANCER_TEST(test_lexique);
  LANCER_TEST(test_iterateur);
  LANCER_TEST(test_liste_des_fichiers);
  LANCER_TEST(test_repertoire_creer_markov);
  LANCER_TEST(test_apprendre_ordre_1);
  LANCER_TEST(test_apprendre_ordre_2);
  LANCER_TEST(test_generer_phrase);
  LANCER_TEST(test_sauvegarder);
  LANCER_TEST(test_charger);
  LANCER_TEST(test_table_des_nuplets);
  LANCER_TEST(test_langue);
  LANCER_TEST(test_liste_du_corpus);
  LANCER_TEST(test_cle);
  LANCER_TEST(test_modele_mot);
  LANCER_TEST(test_apprendre_ordre_3);
  LANCER_TEST(test_apprendre_toutes_options);
  LANCER_TEST(test_generer_phrase_depuis);
  LANCER_TEST(test_charger_ordre_3);
  printf("  (%lu executions de scenarios, chacune avec un echec "
         "d'allocation different)\n", nb_executions);
  afficher_bilan("defaillances");

  assert(blocs_vivants == vivants_initiaux);
  modele_detruire(&modele);
  modele_detruire(&modele_3);
  free(sous);
  free(chemin_texte);
  free(chemin_modele);
  free(chemin_modele_3);
  free(chemin_sortie);
  free(chemin_anglais);
  detruire_repertoire_temporaire(corpus);
  detruire_repertoire_temporaire(corpus_langues);
  detruire_repertoire_temporaire(autre);
  assert(blocs_vivants == 0);
  return EXIT_SUCCESS;
}
