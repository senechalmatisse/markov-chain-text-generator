/**
Fichier test_hachage.c

Tests unitaires du module hachage (cf. src/hachage.h).
*/
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "outils_tests.h"
#include "../src/hachage.h"

/* ======================================================================== */
/*                                 Utilitaires                              */
/* ======================================================================== */

/**
 * Ecrit dans mot le n-ieme mot de l'ordre "militaire" (par longueur puis
 * ordre alphabetique) : 0 -> "a", 25 -> "z", 26 -> "aa", 27 -> "ab", ...
 * (numeration bijective en base 26).
 */
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

/** Generateur deterministe (xorshift). */
static uint64_t aleatoire(uint64_t *etat)
{
  *etat ^= *etat << 13;
  *etat ^= *etat >> 7;
  *etat ^= *etat << 17;
  return *etat;
}

/** Mot aleatoire de longueur 1 .. longueur_max (<= 31). */
static void mot_aleatoire(uint64_t *etat, size_t longueur_max, char *mot)
{
  size_t longueur = 1u + (size_t)(aleatoire(etat) % longueur_max);
  size_t i;

  for (i = 0u; i < longueur; i++) {
    mot[i] = (char)('a' + (aleatoire(etat) % 26u));
  }
  mot[longueur] = '\0';
}

static int comparer_codes(const void *a, const void *b)
{
  uint64_t ca = *(const uint64_t *)a;
  uint64_t cb = *(const uint64_t *)b;

  return (ca > cb) - (ca < cb);
}


/* ======================================================================== */
/*                             Fonction de hachage                          */
/* ======================================================================== */

static void test_mot_valide(void)
{
  assert(hachage_mot_valide(""));
  assert(hachage_mot_valide("a"));
  assert(hachage_mot_valide("abcdefghijklmnopqrstuvwxyz"));
  assert(!hachage_mot_valide("Abc"));
  assert(!hachage_mot_valide("a b"));
  assert(!hachage_mot_valide("l'eau"));
  assert(!hachage_mot_valide("mot1"));
  assert(!hachage_mot_valide("\xC3\xA9t\xC3\xA9"));   /* "ete" accentue */
}

static void test_code_valeurs(void)
{
  assert(hachage_code("") == 0u);
  assert(hachage_code("a") == 1u);
  assert(hachage_code("z") == 26u);
  assert(hachage_code("aa") == 28u);
  assert(hachage_code("ba") == 55u);
  assert(hachage_code("abc") == (1u * 27u * 27u) + (2u * 27u) + 3u);
  /* deterministe */
  assert(hachage_code("anticonstitutionnellement") == hachage_code("anticonstitutionnellement"));
}

static void test_code_injectif_mots_courts(void)
{
  /* tous les mots d'au plus 4 lettres (475 254 mots) ont des codes
     differents, compris entre 1 et 27^4 - 1 */
  const unsigned long nb_mots = 26ul + (26ul * 26ul) + (26ul * 26ul * 26ul) + (26ul * 26ul * 26ul * 26ul);
  const size_t nb_codes = 27u * 27u * 27u * 27u;
  unsigned char *vus = calloc(nb_codes, 1u);
  char mot[8];
  unsigned long n;
  uint64_t code;

  assert(vus != NULL);
  for (n = 0u; n < nb_mots; n++) {
    enieme_mot(n, mot);
    code = hachage_code(mot);
    assert(code > 0u && code < nb_codes);
    assert(vus[code] == 0u);
    vus[code] = 1u;
  }
  free(vus);
}

static void test_code_injectif_mots_longs(void)
{
  /* 100 000 mots aleatoires distincts de 9 a 13 lettres : codes distincts */
  const size_t nb = 100000u;
  uint64_t *codes = malloc(nb * sizeof(uint64_t));
  hachage_t *h = hachage_creer((unsigned int)nb);
  uint64_t etat = 0x123456789ABCDEFull;
  char mot[32];
  size_t n = 0u;
  size_t i;

  assert(codes != NULL && h != NULL);
  while (n < nb) {
    mot_aleatoire(&etat, 5u, mot);
    memmove(mot + 8, mot, strlen(mot) + 1u);
    memcpy(mot, "zyxwvuts", 8u);           /* 9 a 13 lettres */
    if (!hachage_contient(h, mot)) {
      assert(hachage_ajouter(h, mot) == n);
      codes[n] = hachage_code(mot);
      n++;
    }
  }
  qsort(codes, nb, sizeof(uint64_t), comparer_codes);
  for (i = 1u; i < nb; i++) {
    assert(codes[i - 1u] != codes[i]);
  }
  free(codes);
  hachage_detruire(&h);
}


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

static void test_creer_detruire(void)
{
  hachage_t *h = hachage_creer(0u);
  size_t capacite;

  assert(h != NULL);
  assert(hachage_nb_mots(h) == 0u);
  capacite = hachage_capacite(h);
  assert(capacite >= 16u);
  assert((capacite & (capacite - 1u)) == 0u);    /* puissance de 2 */
  assert(hachage_identifiant(h, "mot") == HACHAGE_ABSENT);
  assert(!hachage_contient(h, ""));
  assert(hachage_position(h, "mot") == HACHAGE_POSITION_ABSENTE);
  assert(hachage_nb_collisions(h) == 0u);
  assert(hachage_sondage_moyen(h) == 0.0);
  hachage_detruire(&h);
  assert(h == NULL);
  hachage_detruire(&h);
  assert(h == NULL);
}

static void test_creer_avec_prevision(void)
{
  hachage_t *h = hachage_creer(1000u);
  size_t capacite = hachage_capacite(h);
  char mot[16];
  unsigned long n;

  assert(capacite >= 2000u);
  /* les mots prevus sont inseres sans agrandissement */
  for (n = 0u; n < 1000u; n++) {
    enieme_mot(n, mot);
    assert(hachage_ajouter(h, mot) == n);
  }
  assert(hachage_capacite(h) == capacite);
  hachage_detruire(&h);
}


/* ======================================================================== */
/*                            Ajout et recherche                            */
/* ======================================================================== */

static void test_ajouter_identifiants(void)
{
  hachage_t *h = hachage_creer(0u);
  char copie[16];

  assert(hachage_ajouter(h, "chat") == 0u);
  assert(hachage_ajouter(h, "chien") == 1u);
  assert(hachage_ajouter(h, "souris") == 2u);
  assert(hachage_nb_mots(h) == 3u);
  /* ajouter un mot deja present renvoie son identifiant */
  assert(hachage_ajouter(h, "chien") == 1u);
  assert(hachage_ajouter(h, "chat") == 0u);
  assert(hachage_nb_mots(h) == 3u);

  assert(hachage_identifiant(h, "chat") == 0u);
  assert(hachage_identifiant(h, "souris") == 2u);
  assert(hachage_identifiant(h, "cha") == HACHAGE_ABSENT);
  assert(hachage_identifiant(h, "chats") == HACHAGE_ABSENT);
  assert(hachage_contient(h, "chien"));
  assert(!hachage_contient(h, "loup"));

  /* la table garde sa propre copie du mot */
  strcpy(copie, "loup");
  assert(hachage_ajouter(h, copie) == 3u);
  strcpy(copie, "xxxx");
  assert(strcmp(hachage_mot(h, 3u), "loup") == 0);
  assert(hachage_identifiant(h, "loup") == 3u);
  hachage_detruire(&h);
}

static void test_mot_par_identifiant(void)
{
  hachage_t *h = hachage_creer(0u);
  const char *mots[] = { "le", "petit", "prince", "", "a", "zzzzzz" };
  unsigned int i;

  for (i = 0u; i < 6u; i++) {
    assert(hachage_ajouter(h, mots[i]) == i);
  }
  for (i = 0u; i < 6u; i++) {
    assert(strcmp(hachage_mot(h, i), mots[i]) == 0);
    assert(hachage_identifiant(h, hachage_mot(h, i)) == i);
  }
  hachage_detruire(&h);
}

static void test_mot_vide(void)
{
  hachage_t *h = hachage_creer(0u);

  assert(hachage_ajouter(h, "") == 0u);
  assert(hachage_ajouter(h, "a") == 1u);
  assert(hachage_ajouter(h, "") == 0u);
  assert(hachage_contient(h, ""));
  assert(hachage_mot(h, 0u)[0] == '\0');
  hachage_detruire(&h);
}

static void test_mots_longs(void)
{
  hachage_t *h = hachage_creer(0u);
  const size_t longueur = 200000u;              /* plus grand qu'un bloc */
  char *long1 = malloc(longueur + 1u);
  char *long2 = malloc(longueur + 1u);
  char prefixe[40];

  assert(long1 != NULL && long2 != NULL);
  memset(long1, 'q', longueur);
  long1[longueur] = '\0';
  strcpy(long2, long1);
  long2[longueur - 1u] = 'r';                   /* differe a la fin */
  assert(hachage_ajouter(h, long1) == 0u);
  assert(hachage_ajouter(h, long2) == 1u);
  assert(hachage_ajouter(h, "court") == 2u);
  assert(strcmp(hachage_mot(h, 0u), long1) == 0);
  assert(strcmp(hachage_mot(h, 1u), long2) == 0);
  assert(hachage_identifiant(h, long2) == 1u);

  /* mots de plus de 13 lettres partageant un long prefixe */
  strcpy(prefixe, "anticonstitutionnellementa");
  assert(hachage_ajouter(h, prefixe) == 3u);
  prefixe[25] = 'b';
  assert(hachage_ajouter(h, prefixe) == 4u);
  assert(hachage_identifiant(h, "anticonstitutionnellementa") == 3u);
  free(long1);
  free(long2);
  hachage_detruire(&h);
}

static void test_position(void)
{
  hachage_t *h = hachage_creer(0u);
  const char *mots[] = { "chat", "chien", "souris", "", "oiseau" };
  size_t positions[5];
  size_t p;
  unsigned int i;
  unsigned int j;
  unsigned int nb_occupees = 0u;

  for (i = 0u; i < 5u; i++) {
    assert(hachage_ajouter(h, mots[i]) == i);
  }
  for (i = 0u; i < 5u; i++) {
    positions[i] = hachage_position(h, mots[i]);
    assert(positions[i] < hachage_capacite(h));
    assert(hachage_identifiant_en_position(h, positions[i]) == i);
    for (j = 0u; j < i; j++) {
      assert(positions[i] != positions[j]);
    }
  }
  assert(hachage_position(h, "absent") == HACHAGE_POSITION_ABSENTE);
  /* exactement nb_mots cases occupees */
  for (p = 0u; p < hachage_capacite(h); p++) {
    if (hachage_identifiant_en_position(h, p) != HACHAGE_ABSENT) {
      nb_occupees++;
    }
  }
  assert(nb_occupees == hachage_nb_mots(h));
  hachage_detruire(&h);
}

static void test_agrandissement(void)
{
  const unsigned long nb = 200000u;
  hachage_t *h = hachage_creer(0u);
  const char *adresses[64];
  size_t capacite_initiale = hachage_capacite(h);
  char mot[16];
  unsigned long n;

  for (n = 0u; n < nb; n++) {
    enieme_mot(n, mot);
    assert(hachage_ajouter(h, mot) == n);
    if (n < 64u) {
      adresses[n] = hachage_mot(h, (unsigned int)n);
    }
    assert(2u * (size_t)hachage_nb_mots(h) <= hachage_capacite(h));
  }
  assert(hachage_capacite(h) > capacite_initiale);
  assert(hachage_nb_mots(h) == nb);
  for (n = 0u; n < nb; n++) {
    enieme_mot(n, mot);
    assert(hachage_identifiant(h, mot) == n);
    assert(strcmp(hachage_mot(h, (unsigned int)n), mot) == 0);
    assert(hachage_identifiant_en_position(h, hachage_position(h, mot)) == n);
  }
  /* les adresses des mots n'ont pas change malgre les agrandissements */
  for (n = 0u; n < 64u; n++) {
    assert(adresses[n] == hachage_mot(h, (unsigned int)n));
  }
  hachage_detruire(&h);
}

static void test_peu_de_collisions(void)
{
  /* sur 100 000 mots aleatoires, le nombre moyen de cases examinees reste
     proche de l'optimum (1,0) ; en hachage uniforme on attend au plus 1,5
     pour un taux de remplissage de 1/2 */
  const unsigned int nb = 100000u;
  hachage_t *h = hachage_creer(0u);
  uint64_t etat = 2463534242u;
  char mot[32];
  double sondage;
  size_t collisions;

  while (hachage_nb_mots(h) < nb) {
    mot_aleatoire(&etat, 12u, mot);
    assert(hachage_ajouter(h, mot) != HACHAGE_ABSENT);
  }
  sondage = hachage_sondage_moyen(h);
  collisions = hachage_nb_collisions(h);
  assert(sondage >= 1.0 && sondage < 1.5);
  assert(collisions < (size_t)nb / 2u);
  hachage_detruire(&h);

  /* meme verification sur des mots tres reguliers (aa, ab, ..., zz, aaa...) */
  h = hachage_creer(0u);
  for (etat = 0u; etat < nb; etat++) {
    enieme_mot((unsigned long)etat, mot);
    assert(hachage_ajouter(h, mot) == etat);
  }
  assert(hachage_sondage_moyen(h) < 1.5);
  hachage_detruire(&h);
}


/* ======================================================================== */
/*                        Table de paires d'identifiants                    */
/* ======================================================================== */

static void test_paires_creer_detruire(void)
{
  hachage_paires_t *p = hachage_paires_creer(0u);
  size_t capacite;

  assert(p != NULL);
  assert(hachage_paires_nb(p) == 0u);
  capacite = hachage_paires_capacite(p);
  assert(capacite >= 16u && (capacite & (capacite - 1u)) == 0u);
  assert(hachage_paires_identifiant(p, 0u, 0u) == HACHAGE_ABSENT);
  assert(hachage_paires_sondage_moyen(p) == 0.0);
  hachage_paires_detruire(&p);
  assert(p == NULL);
  hachage_paires_detruire(&p);
  p = hachage_paires_creer(1000u);
  assert(hachage_paires_capacite(p) >= 2000u);
  hachage_paires_detruire(&p);
}

static void test_paires_ajouter(void)
{
  hachage_paires_t *p = hachage_paires_creer(0u);

  assert(hachage_paires_ajouter(p, 0u, 0u) == 0u);
  assert(hachage_paires_ajouter(p, 1u, 2u) == 1u);
  assert(hachage_paires_ajouter(p, 2u, 1u) == 2u);       /* ordonnees */
  assert(hachage_paires_ajouter(p, UINT_MAX - 1u, 0u) == 3u);
  assert(hachage_paires_ajouter(p, 0u, UINT_MAX) == 4u);
  assert(hachage_paires_nb(p) == 5u);
  /* une paire deja presente garde son identifiant */
  assert(hachage_paires_ajouter(p, 1u, 2u) == 1u);
  assert(hachage_paires_ajouter(p, 0u, 0u) == 0u);
  assert(hachage_paires_nb(p) == 5u);
  assert(hachage_paires_identifiant(p, 2u, 1u) == 2u);
  assert(hachage_paires_identifiant(p, 2u, 2u) == HACHAGE_ABSENT);
  assert(hachage_paires_premier(p, 3u) == UINT_MAX - 1u);
  assert(hachage_paires_second(p, 3u) == 0u);
  assert(hachage_paires_premier(p, 4u) == 0u);
  assert(hachage_paires_second(p, 4u) == UINT_MAX);
  hachage_paires_detruire(&p);
}

static void test_paires_agrandissement(void)
{
  hachage_paires_t *p = hachage_paires_creer(0u);
  unsigned int i;
  unsigned int j;
  unsigned int id = 0u;

  /* 300 x 300 paires : nombreux agrandissements */
  for (i = 0u; i < 300u; i++) {
    for (j = 0u; j < 300u; j++) {
      assert(hachage_paires_ajouter(p, i, j) == id);
      id++;
    }
    assert(2u * (size_t)hachage_paires_nb(p) <= hachage_paires_capacite(p));
  }
  id = 0u;
  for (i = 0u; i < 300u; i++) {
    for (j = 0u; j < 300u; j++) {
      assert(hachage_paires_identifiant(p, i, j) == id);
      assert(hachage_paires_premier(p, id) == i);
      assert(hachage_paires_second(p, id) == j);
      id++;
    }
  }
  /* paires tres regulieres : la repartition reste bonne */
  assert(hachage_paires_sondage_moyen(p) >= 1.0);
  assert(hachage_paires_sondage_moyen(p) < 1.5);
  hachage_paires_detruire(&p);
}


/* ======================================================================== */
/*                      Table de n-uplets d'identifiants                    */
/* ======================================================================== */

static void test_nuplets_creer_detruire(void)
{
  hachage_nuplets_t *t = hachage_nuplets_creer(3u, 0u);
  const unsigned int zeros[3] = { 0u, 0u, 0u };
  size_t capacite;

  assert(t != NULL);
  assert(hachage_nuplets_nb(t) == 0u);
  assert(hachage_nuplets_taille(t) == 3u);
  capacite = hachage_nuplets_capacite(t);
  assert(capacite >= 16u && (capacite & (capacite - 1u)) == 0u);
  assert(hachage_nuplets_identifiant(t, zeros) == HACHAGE_ABSENT);
  assert(hachage_nuplets_sondage_moyen(t) == 0.0);
  hachage_nuplets_detruire(&t);
  assert(t == NULL);
  hachage_nuplets_detruire(&t);            /* sans effet sur NULL */
  /* dimensionnement a la creation, tailles extremes */
  t = hachage_nuplets_creer(1u, 1000u);
  assert(hachage_nuplets_capacite(t) >= 2000u);
  hachage_nuplets_detruire(&t);
  t = hachage_nuplets_creer(HACHAGE_TAILLE_NUPLET_MAX, 0u);
  assert(hachage_nuplets_taille(t) == HACHAGE_TAILLE_NUPLET_MAX);
  hachage_nuplets_detruire(&t);
}

static void test_nuplets_ajouter(void)
{
  hachage_nuplets_t *t = hachage_nuplets_creer(3u, 0u);
  const unsigned int zeros[3] = { 0u, 0u, 0u };
  const unsigned int abc[3] = { 1u, 2u, 3u };
  const unsigned int cba[3] = { 3u, 2u, 1u };
  const unsigned int debut[3] = { 0u, 0u, 1u };
  const unsigned int extremes[3] = { UINT_MAX, 0u, UINT_MAX - 1u };
  const unsigned int absent[3] = { 1u, 2u, 4u };
  unsigned int copie[3] = { 1u, 2u, 3u };

  assert(hachage_nuplets_ajouter(t, zeros) == 0u);
  assert(hachage_nuplets_ajouter(t, abc) == 1u);
  assert(hachage_nuplets_ajouter(t, cba) == 2u);        /* ordonnes */
  assert(hachage_nuplets_ajouter(t, debut) == 3u);
  assert(hachage_nuplets_ajouter(t, extremes) == 4u);
  assert(hachage_nuplets_nb(t) == 5u);
  /* un n-uplet deja present garde son identifiant ; les elements sont
     recopies : l'appelant peut modifier son tableau */
  assert(hachage_nuplets_ajouter(t, copie) == 1u);
  copie[0] = 9u;
  assert(hachage_nuplets_identifiant(t, abc) == 1u);
  assert(hachage_nuplets_identifiant(t, copie) == HACHAGE_ABSENT);
  assert(hachage_nuplets_ajouter(t, zeros) == 0u);
  assert(hachage_nuplets_nb(t) == 5u);
  assert(hachage_nuplets_identifiant(t, cba) == 2u);
  assert(hachage_nuplets_identifiant(t, absent) == HACHAGE_ABSENT);
  assert(hachage_nuplets_element(t, 2u, 0u) == 3u);
  assert(hachage_nuplets_element(t, 2u, 1u) == 2u);
  assert(hachage_nuplets_element(t, 2u, 2u) == 1u);
  assert(hachage_nuplets_element(t, 4u, 0u) == UINT_MAX);
  assert(hachage_nuplets_element(t, 4u, 2u) == UINT_MAX - 1u);
  hachage_nuplets_detruire(&t);
}

static void test_nuplets_toutes_tailles(void)
{
  /* pour chaque taille : n-uplets (i, i+1, ..., i+n-1) et leurs decales */
  unsigned int elements[HACHAGE_TAILLE_NUPLET_MAX];
  hachage_nuplets_t *t;
  unsigned int taille;
  unsigned int i;
  unsigned int r;

  for (taille = 1u; taille <= HACHAGE_TAILLE_NUPLET_MAX; taille += 7u) {
    t = hachage_nuplets_creer(taille, 0u);
    assert(t != NULL);
    for (i = 0u; i < 200u; i++) {
      for (r = 0u; r < taille; r++) {
        elements[r] = i + r;
      }
      assert(hachage_nuplets_ajouter(t, elements) == i);
    }
    for (i = 0u; i < 200u; i++) {
      for (r = 0u; r < taille; r++) {
        elements[r] = i + r;
      }
      assert(hachage_nuplets_identifiant(t, elements) == i);
      assert(hachage_nuplets_element(t, i, 0u) == i);
      assert(hachage_nuplets_element(t, i, taille - 1u) == i + taille - 1u);
      /* un seul element change : n-uplet absent */
      elements[taille - 1u] += 1000u;
      assert(hachage_nuplets_identifiant(t, elements) == HACHAGE_ABSENT);
    }
    hachage_nuplets_detruire(&t);
  }
}

static void test_nuplets_agrandissement(void)
{
  hachage_nuplets_t *t = hachage_nuplets_creer(3u, 0u);
  unsigned int elements[3];
  unsigned int i;
  unsigned int j;
  unsigned int k;
  unsigned int id = 0u;

  /* 40 x 40 x 40 triplets : nombreux agrandissements */
  for (i = 0u; i < 40u; i++) {
    for (j = 0u; j < 40u; j++) {
      for (k = 0u; k < 40u; k++) {
        elements[0] = i;
        elements[1] = j;
        elements[2] = k;
        assert(hachage_nuplets_ajouter(t, elements) == id);
        id++;
      }
    }
    assert(2u * (size_t)hachage_nuplets_nb(t) <= hachage_nuplets_capacite(t));
  }
  assert(hachage_nuplets_nb(t) == 64000u);
  id = 0u;
  for (i = 0u; i < 40u; i++) {
    for (j = 0u; j < 40u; j++) {
      for (k = 0u; k < 40u; k++) {
        elements[0] = i;
        elements[1] = j;
        elements[2] = k;
        assert(hachage_nuplets_identifiant(t, elements) == id);
        assert(hachage_nuplets_element(t, id, 0u) == i);
        assert(hachage_nuplets_element(t, id, 1u) == j);
        assert(hachage_nuplets_element(t, id, 2u) == k);
        id++;
      }
    }
  }
  /* triplets tres reguliers : la repartition reste bonne */
  assert(hachage_nuplets_sondage_moyen(t) >= 1.0);
  assert(hachage_nuplets_sondage_moyen(t) < 1.5);
  hachage_nuplets_detruire(&t);
}

static void test_paires_et_nuplets_de_taille_2(void)
{
  /* une table de paires se comporte comme une table de n-uplets de
     taille 2 : memes identifiants pour les memes insertions */
  hachage_paires_t *p = hachage_paires_creer(0u);
  hachage_nuplets_t *t = hachage_nuplets_creer(2u, 0u);
  unsigned int elements[2];
  unsigned int i;

  for (i = 0u; i < 5000u; i++) {
    elements[0] = (i * 7u) % 97u;
    elements[1] = (i * 13u) % 89u;
    assert(hachage_paires_ajouter(p, elements[0], elements[1]) == hachage_nuplets_ajouter(t, elements));
  }
  assert(hachage_paires_nb(p) == hachage_nuplets_nb(t));
  assert(hachage_paires_capacite(p) == hachage_nuplets_capacite(t));
  assert(hachage_paires_sondage_moyen(p) == hachage_nuplets_sondage_moyen(t));
  for (i = 0u; i < hachage_paires_nb(p); i++) {
    assert(hachage_paires_premier(p, i) == hachage_nuplets_element(t, i, 0u));
    assert(hachage_paires_second(p, i) == hachage_nuplets_element(t, i, 1u));
  }
  hachage_paires_detruire(&p);
  hachage_nuplets_detruire(&t);
}


/* ======================================================================== */
/*                        Verification des contrats                         */
/* ======================================================================== */

static void violation_paires_nulle(void)
{
  hachage_paires_nb(NULL);
}

static void violation_paire_inexistante(void)
{
  hachage_paires_t *p = hachage_paires_creer(0u);

  hachage_paires_ajouter(p, 1u, 1u);
  hachage_paires_second(p, 1u);
}

static void violation_mot_majuscule(void)
{
  hachage_t *h = hachage_creer(0u);

  hachage_ajouter(h, "Chat");
}

static void violation_mot_accentue(void)
{
  hachage_t *h = hachage_creer(0u);

  hachage_identifiant(h, "\xC3\xA9t\xC3\xA9");
}

static void violation_mot_nul(void)
{
  hachage_t *h = hachage_creer(0u);

  hachage_ajouter(h, NULL);
}

static void violation_table_nulle(void)
{
  hachage_nb_mots(NULL);
}

static void violation_identifiant_inexistant(void)
{
  hachage_t *h = hachage_creer(0u);

  hachage_ajouter(h, "chat");
  hachage_mot(h, 1u);
}

static void violation_position_hors_table(void)
{
  hachage_t *h = hachage_creer(0u);

  hachage_identifiant_en_position(h, hachage_capacite(h));
}

static void violation_code_mot_invalide(void)
{
  hachage_code("mot-compose");
}

static void sans_violation(void)
{
  hachage_t *h = hachage_creer(0u);

  assert(hachage_ajouter(h, "chat") == 0u);
  hachage_detruire(&h);
}

static void violation_nuplets_taille_nulle(void)
{
  hachage_nuplets_creer(0u, 0u);
}

static void violation_nuplets_taille_trop_grande(void)
{
  hachage_nuplets_creer(HACHAGE_TAILLE_NUPLET_MAX + 1u, 0u);
}

static void violation_nuplets_table_nulle(void)
{
  const unsigned int elements[2] = { 0u, 0u };

  hachage_nuplets_ajouter(NULL, elements);
}

static void violation_nuplets_elements_nuls(void)
{
  hachage_nuplets_t *t = hachage_nuplets_creer(2u, 0u);

  hachage_nuplets_identifiant(t, NULL);
}

static void violation_nuplet_inexistant(void)
{
  hachage_nuplets_t *t = hachage_nuplets_creer(2u, 0u);
  const unsigned int elements[2] = { 1u, 1u };

  hachage_nuplets_ajouter(t, elements);
  hachage_nuplets_element(t, 1u, 0u);       /* seul l'identifiant 0 existe */
}

static void violation_nuplet_rang_trop_grand(void)
{
  hachage_nuplets_t *t = hachage_nuplets_creer(2u, 0u);
  const unsigned int elements[2] = { 1u, 1u };

  hachage_nuplets_ajouter(t, elements);
  hachage_nuplets_element(t, 0u, 2u);       /* rangs 0 et 1 seulement */
}

static void test_contrats(void)
{
  assert(!viole_contrat(sans_violation));
  assert(viole_contrat(violation_mot_majuscule));
  assert(viole_contrat(violation_mot_accentue));
  assert(viole_contrat(violation_mot_nul));
  assert(viole_contrat(violation_table_nulle));
  assert(viole_contrat(violation_identifiant_inexistant));
  assert(viole_contrat(violation_position_hors_table));
  assert(viole_contrat(violation_code_mot_invalide));
  assert(viole_contrat(violation_paires_nulle));
  assert(viole_contrat(violation_paire_inexistante));
  assert(viole_contrat(violation_nuplets_taille_nulle));
  assert(viole_contrat(violation_nuplets_taille_trop_grande));
  assert(viole_contrat(violation_nuplets_table_nulle));
  assert(viole_contrat(violation_nuplets_elements_nuls));
  assert(viole_contrat(violation_nuplet_inexistant));
  assert(viole_contrat(violation_nuplet_rang_trop_grand));
}


int main(void)
{
  printf("Tests du module hachage :\n");
  LANCER_TEST(test_mot_valide);
  LANCER_TEST(test_code_valeurs);
  LANCER_TEST(test_code_injectif_mots_courts);
  LANCER_TEST(test_code_injectif_mots_longs);
  LANCER_TEST(test_creer_detruire);
  LANCER_TEST(test_creer_avec_prevision);
  LANCER_TEST(test_ajouter_identifiants);
  LANCER_TEST(test_mot_par_identifiant);
  LANCER_TEST(test_mot_vide);
  LANCER_TEST(test_mots_longs);
  LANCER_TEST(test_position);
  LANCER_TEST(test_agrandissement);
  LANCER_TEST(test_peu_de_collisions);
  LANCER_TEST(test_paires_creer_detruire);
  LANCER_TEST(test_paires_ajouter);
  LANCER_TEST(test_paires_agrandissement);
  LANCER_TEST(test_nuplets_creer_detruire);
  LANCER_TEST(test_nuplets_ajouter);
  LANCER_TEST(test_nuplets_toutes_tailles);
  LANCER_TEST(test_nuplets_agrandissement);
  LANCER_TEST(test_paires_et_nuplets_de_taille_2);
  LANCER_TEST(test_contrats);
  afficher_bilan("hachage");
  return EXIT_SUCCESS;
}
