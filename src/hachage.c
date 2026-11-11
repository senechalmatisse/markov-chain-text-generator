/**
Fichier hachage.c

Implementation de la table de hachage (cf. hachage.h).

Representation :
  - cases[0 .. capacite-1] : table a adressage ouvert ; cases[p] vaut 0 si
    la case p est libre, et identifiant + 1 si elle contient le mot
    d'identifiant identifiant ;
  - entrees[id] : pour chaque identifiant id < nb_mots, adresse du mot et
    code de hachage (conserve pour comparer rapidement et pour replacer les
    mots lors d'un agrandissement sans les relire) ;
  - blocs : liste chainee de blocs memoire dans lesquels les mots sont
    recopies les uns a la suite des autres (allocation "par arene"). Les
    blocs ne sont jamais deplaces : les adresses des mots sont stables.

Invariants :
  - capacite == 2^nb_bits, nb_bits >= NB_BITS_MINIMAL ;
  - 2 * nb_mots <= capacite (taux de remplissage <= 1/2) ;
  - chaque identifiant id < nb_mots figure dans exactement une case, que l'on
    atteint depuis la case initiale du mot sans rencontrer de case libre
    (propriete du sondage lineaire sans suppression).
*/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "contrat.h"
#include "hachage.h"

/** Base du code de hachage : plus petite base > 26 (chiffres 1 .. 26). */
#define BASE_CODE UINT64_C(27)

/** Table d'au moins 2^NB_BITS_MINIMAL = 16 cases. */
#define NB_BITS_MINIMAL 4u

/** Table d'au plus 2^NB_BITS_MAXIMAL cases (63 avec un size_t de 64 bits). */
#define NB_BITS_MAXIMAL ((unsigned int)(sizeof(size_t) * CHAR_BIT) - 1u)

/** Taille d'un bloc de stockage des mots (64 Kio). */
#define TAILLE_BLOC ((size_t)65536)

/** Capacite initiale minimale du tableau des entrees. */
#define CAPACITE_ENTREES_MINIMALE 16u

/** 2^64 / phi (phi = nombre d'or) : multiplicateur du hachage de Fibonacci. */
#define MULTIPLICATEUR_FIBONACCI UINT64_C(0x9E3779B97F4A7C15)

/** Bloc de stockage des mots (membre tableau flexible C99). */
typedef struct bloc_s {
  struct bloc_s *precedent;  /* bloc alloue avant celui-ci */
  size_t taille;             /* nombre d'octets de donnees */
  size_t utilise;            /* nombre d'octets deja occupes */
  char donnees[];
} bloc_t;

/** Informations associees a un identifiant. */
typedef struct {
  const char *mot;
  uint64_t code;
} entree_t;

struct hachage_s {
  unsigned int *cases;
  size_t capacite;
  unsigned int nb_bits;
  entree_t *entrees;
  unsigned int nb_mots;
  unsigned int capacite_entrees;
  bloc_t *blocs;  /* bloc le plus recent (en cours de remplissage) */
};


/* ======================================================================== */
/*                             Fonction de hachage                          */
/* ======================================================================== */

bool hachage_mot_valide(const char *mot)
{
  const char *c;
  bool valide = true;

  PRECONDITION(mot != NULL);
  for (c = mot; valide && *c != '\0'; c++) {
    valide = (*c >= 'a' && *c <= 'z');
  }
  return valide;
}

uint64_t hachage_code(const char *mot)
{
  const char *c;
  uint64_t code = 0u;

  PRECONDITION(mot != NULL);
  PRECONDITION(hachage_mot_valide(mot));
  /* schema de Horner : code = (((c1 * 27) + c2) * 27 + c3) ... */
  for (c = mot; *c != '\0'; c++) {
    code = (code * BASE_CODE) + (uint64_t)(*c - 'a' + 1);
  }
  return code;
}

/**
 * Case initiale d'un code dans une table de 2^nb_bits cases (hachage de
 * Fibonacci : bits de poids fort du produit par 2^64 / phi).
 */
static size_t case_initiale(uint64_t code, unsigned int nb_bits)
{
  return (size_t)((code * MULTIPLICATEUR_FIBONACCI) >> (64u - nb_bits));
}

/**
 * Indice de la case contenant le mot, ou, s'il est absent, de la premiere
 * case libre rencontree (la ou il devrait etre insere).
 */
static size_t chercher_case(const hachage_t *h, const char *mot, uint64_t code)
{
  size_t masque = h->capacite - 1u;
  size_t p = case_initiale(code, h->nb_bits);
  const entree_t *entree;
  bool trouve = false;

  while (!trouve && h->cases[p] != 0u) {
    entree = &h->entrees[h->cases[p] - 1u];
    /* comparaison des codes d'abord : strcmp n'est presque jamais appele
       pour deux mots differents */
    trouve = (entree->code == code && strcmp(entree->mot, mot) == 0);
    if (!trouve) {
      p = (p + 1u) & masque;
    }
  }
  return p;
}


/* ======================================================================== */
/*                          Gestion de la memoire                           */
/* ======================================================================== */

/** Double le nombre de cases et y replace tous les mots. */
static bool agrandir_cases(hachage_t *h)
{
  unsigned int nouveaux_bits = h->nb_bits + 1u;
  size_t nouvelle_capacite = 2u * h->capacite;
  size_t masque = nouvelle_capacite - 1u;
  unsigned int *nouvelles;
  unsigned int id;
  size_t p;
  bool succes = false;

  if (nouveaux_bits <= NB_BITS_MAXIMAL && nouvelle_capacite > h->capacite) {
    nouvelles = calloc(nouvelle_capacite, sizeof(unsigned int));
    if (nouvelles != NULL) {
      /* les mots sont tous differents : on les place sans comparaison */
      for (id = 0u; id < h->nb_mots; id++) {
        p = case_initiale(h->entrees[id].code, nouveaux_bits);
        while (nouvelles[p] != 0u) {
          p = (p + 1u) & masque;
        }
        nouvelles[p] = id + 1u;
      }
      free(h->cases);
      h->cases = nouvelles;
      h->capacite = nouvelle_capacite;
      h->nb_bits = nouveaux_bits;
      succes = true;
    }
  }
  return succes;
}

/** Double la capacite du tableau des entrees. */
static bool agrandir_entrees(hachage_t *h)
{
  size_t nouvelle_capacite = 2u * (size_t)h->capacite_entrees;
  entree_t *entrees;
  bool succes = false;

  if (nouvelle_capacite > HACHAGE_NB_MOTS_MAX) {
    nouvelle_capacite = HACHAGE_NB_MOTS_MAX;
  }
  if (nouvelle_capacite > h->capacite_entrees
      && nouvelle_capacite <= SIZE_MAX / sizeof(entree_t)) {
    entrees = realloc(h->entrees, nouvelle_capacite * sizeof(entree_t));
    if (entrees != NULL) {
      h->entrees = entrees;
      h->capacite_entrees = (unsigned int)nouvelle_capacite;
      succes = true;
    }
  }
  return succes;
}

/**
 * Recopie un mot dans le bloc courant (ou dans un nouveau bloc s'il n'y a
 * plus assez de place) et renvoie l'adresse de la copie, ou NULL si la
 * memoire manque.
 */
static char *stocker_mot(hachage_t *h, const char *mot)
{
  size_t taille = strlen(mot) + 1u;
  size_t taille_bloc;
  bloc_t *bloc = h->blocs;
  char *copie = NULL;

  if (bloc == NULL || bloc->taille - bloc->utilise < taille) {
    taille_bloc = (taille > TAILLE_BLOC) ? taille : TAILLE_BLOC;
    if (taille_bloc <= SIZE_MAX - sizeof(bloc_t)) {
      bloc = malloc(sizeof(bloc_t) + taille_bloc);
      if (bloc != NULL) {
        bloc->precedent = h->blocs;
        bloc->taille = taille_bloc;
        bloc->utilise = 0u;
        h->blocs = bloc;
      }
    } else {
      bloc = NULL;
    }
  }
  if (bloc != NULL) {
    copie = bloc->donnees + bloc->utilise;
    memcpy(copie, mot, taille);
    bloc->utilise += taille;
  }
  return copie;
}


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

hachage_t *hachage_creer(unsigned int nb_mots_prevus)
{
  hachage_t *h;
  unsigned int nb_bits = NB_BITS_MINIMAL;
  unsigned int capacite_entrees;

  PRECONDITION(nb_mots_prevus <= HACHAGE_NB_MOTS_MAX);

  /* plus petite puissance de 2 >= 2 * nb_mots_prevus (au moins 16) */
  while (nb_bits < NB_BITS_MAXIMAL
         && ((size_t)1 << nb_bits) / 2u < (size_t)nb_mots_prevus) {
    nb_bits++;
  }
  capacite_entrees = (nb_mots_prevus > CAPACITE_ENTREES_MINIMALE)
                     ? nb_mots_prevus : CAPACITE_ENTREES_MINIMALE;

  h = malloc(sizeof(hachage_t));
  if (h != NULL) {
    h->capacite = (size_t)1 << nb_bits;
    h->nb_bits = nb_bits;
    h->nb_mots = 0u;
    h->capacite_entrees = capacite_entrees;
    h->blocs = NULL;
    h->cases = calloc(h->capacite, sizeof(unsigned int));
    /* calloc verifie le depassement de capacite du produit des tailles */
    h->entrees = calloc(capacite_entrees, sizeof(entree_t));
    if (h->cases == NULL || h->entrees == NULL) {
      free(h->cases);
      free(h->entrees);
      free(h);
      h = NULL;
    }
  }
  if (h == NULL) {
    errno = ENOMEM;
  }
  POSTCONDITION(h == NULL || (hachage_nb_mots(h) == 0u
                              && hachage_capacite(h) / 2u
                                 >= (size_t)nb_mots_prevus));
  return h;
}

void hachage_detruire(hachage_t **h)
{
  bloc_t *bloc;
  bloc_t *precedent;

  PRECONDITION(h != NULL);
  if (*h != NULL) {
    bloc = (*h)->blocs;
    while (bloc != NULL) {
      precedent = bloc->precedent;
      free(bloc);
      bloc = precedent;
    }
    free((*h)->cases);
    free((*h)->entrees);
    free(*h);
    *h = NULL;
  }
  POSTCONDITION(*h == NULL);
}


/* ======================================================================== */
/*                            Ajout et recherche                            */
/* ======================================================================== */

unsigned int hachage_ajouter(hachage_t *h, const char *mot)
{
  uint64_t code;
  size_t p;
  char *copie;
  unsigned int identifiant = HACHAGE_ABSENT;
  bool succes = true;

  PRECONDITION(h != NULL);
  PRECONDITION(mot != NULL);
  PRECONDITION(hachage_mot_valide(mot));

  code = hachage_code(mot);
  p = chercher_case(h, mot, code);
  if (h->cases[p] != 0u) {
    /* mot deja present */
    identifiant = h->cases[p] - 1u;
  } else if (h->nb_mots >= HACHAGE_NB_MOTS_MAX) {
    errno = ERANGE;
  } else {
    /* 1. agrandissement eventuel (taux de remplissage <= 1/2) */
    if (2u * ((size_t)h->nb_mots + 1u) > h->capacite) {
      succes = agrandir_cases(h);
      if (succes) {
        p = chercher_case(h, mot, code);
      }
    }
    if (succes && h->nb_mots == h->capacite_entrees) {
      succes = agrandir_entrees(h);
    }
    /* 2. recopie du mot */
    copie = NULL;
    if (succes) {
      copie = stocker_mot(h, mot);
      succes = (copie != NULL);
    }
    /* 3. validation : rien n'a ete modifie si une allocation a echoue
          (seule la capacite a pu augmenter) */
    if (succes) {
      identifiant = h->nb_mots;
      h->entrees[identifiant].mot = copie;
      h->entrees[identifiant].code = code;
      h->cases[p] = identifiant + 1u;
      h->nb_mots++;
    } else {
      errno = ENOMEM;
    }
  }
  INVARIANT(2u * (size_t)h->nb_mots <= h->capacite);
  POSTCONDITION(identifiant == HACHAGE_ABSENT || strcmp(h->entrees[identifiant].mot, mot) == 0);
  return identifiant;
}

unsigned int hachage_identifiant(const hachage_t *h, const char *mot)
{
  size_t p;
  unsigned int identifiant = HACHAGE_ABSENT;

  PRECONDITION(h != NULL);
  PRECONDITION(mot != NULL);
  PRECONDITION(hachage_mot_valide(mot));

  p = chercher_case(h, mot, hachage_code(mot));
  if (h->cases[p] != 0u) {
    identifiant = h->cases[p] - 1u;
  }
  POSTCONDITION(identifiant == HACHAGE_ABSENT || identifiant < h->nb_mots);
  return identifiant;
}

bool hachage_contient(const hachage_t *h, const char *mot)
{
  return hachage_identifiant(h, mot) != HACHAGE_ABSENT;
}

size_t hachage_position(const hachage_t *h, const char *mot)
{
  size_t p;
  size_t position = HACHAGE_POSITION_ABSENTE;

  PRECONDITION(h != NULL);
  PRECONDITION(mot != NULL);
  PRECONDITION(hachage_mot_valide(mot));

  p = chercher_case(h, mot, hachage_code(mot));
  if (h->cases[p] != 0u) {
    position = p;
  }
  POSTCONDITION(position == HACHAGE_POSITION_ABSENTE
                || position < h->capacite);
  return position;
}

unsigned int hachage_identifiant_en_position(const hachage_t *h, size_t position)
{
  unsigned int identifiant = HACHAGE_ABSENT;

  PRECONDITION(h != NULL);
  PRECONDITION(position < h->capacite);

  if (h->cases[position] != 0u) {
    identifiant = h->cases[position] - 1u;
  }
  return identifiant;
}

const char *hachage_mot(const hachage_t *h, unsigned int identifiant)
{
  PRECONDITION(h != NULL);
  PRECONDITION(identifiant < h->nb_mots);
  return h->entrees[identifiant].mot;
}


/* ======================================================================== */
/*                        Informations et statistiques                      */
/* ======================================================================== */

unsigned int hachage_nb_mots(const hachage_t *h)
{
  PRECONDITION(h != NULL);
  return h->nb_mots;
}

size_t hachage_capacite(const hachage_t *h)
{
  PRECONDITION(h != NULL);
  return h->capacite;
}

/** Distance (en cases) entre la case initiale d'un mot et sa case reelle. */
static size_t deplacement(const hachage_t *h, size_t p)
{
  size_t initiale = case_initiale(h->entrees[h->cases[p] - 1u].code, h->nb_bits);

  return (p - initiale) & (h->capacite - 1u);
}

size_t hachage_nb_collisions(const hachage_t *h)
{
  size_t p;
  size_t nb = 0u;

  PRECONDITION(h != NULL);
  for (p = 0u; p < h->capacite; p++) {
    if (h->cases[p] != 0u && deplacement(h, p) != 0u) {
      nb++;
    }
  }
  POSTCONDITION(nb <= h->nb_mots);
  return nb;
}

double hachage_sondage_moyen(const hachage_t *h)
{
  size_t p;
  double total = 0.0;
  double moyenne = 0.0;

  PRECONDITION(h != NULL);
  if (h->nb_mots > 0u) {
    for (p = 0u; p < h->capacite; p++) {
      if (h->cases[p] != 0u) {
        total += (double)(deplacement(h, p) + 1u);
      }
    }
    moyenne = total / (double)h->nb_mots;
  }
  POSTCONDITION(h->nb_mots == 0u || moyenne >= 1.0);
  return moyenne;
}


/* ======================================================================== */
/*                Table de hachage de n-uplets d'identifiants               */
/* ======================================================================== */

/** Multiplicateur impair du melange des elements d'un n-uplet. */
#define MULTIPLICATEUR_NUPLET UINT64_C(0xFF51AFD7ED558CCD)

/*
Representation (memes principes que la table des mots) :
  - cases[0 .. capacite-1] : 0 si la case est libre, identifiant + 1 sinon ;
  - elements[taille * id + r] : element de rang r du n-uplet id < nb.
Invariants : capacite == 2^nb_bits ; 2 * nb <= capacite ; chaque n-uplet est
atteignable depuis sa case initiale sans case libre ;
nb <= capacite_nuplets (nombre de n-uplets que peut contenir elements).
*/
struct hachage_nuplets_s {
  unsigned int *cases;
  size_t capacite;
  unsigned int nb_bits;
  unsigned int *elements;
  unsigned int taille;
  unsigned int nb;
  unsigned int capacite_nuplets;
};

/** Une table de paires est une table de n-uplets de taille 2. */
struct hachage_paires_s {
  struct hachage_nuplets_s table;
};

/**
 * Code de hachage d'un n-uplet : chaque element est melange au code par un
 * ou-exclusif, une multiplication impaire et un decalage (les bits de
 * poids fort se repandent ainsi sur tout le code).
 */
static uint64_t code_nuplet(const unsigned int *elements, unsigned int taille)
{
  uint64_t code = 0u;
  unsigned int r;

  for (r = 0u; r < taille; r++) {
    code = (code ^ (uint64_t)elements[r]) * MULTIPLICATEUR_NUPLET;
    code ^= code >> 32;
  }
  return code;
}

/** Elements du n-uplet d'identifiant donne. */
static const unsigned int *elements_de(const struct hachage_nuplets_s *t, unsigned int identifiant)
{
  return t->elements + ((size_t)t->taille * (size_t)identifiant);
}

/** Case contenant le n-uplet, ou premiere case libre rencontree. */
static size_t chercher_nuplet(const struct hachage_nuplets_s *t, const unsigned int *elements)
{
  size_t masque = t->capacite - 1u;
  size_t octets = (size_t)t->taille * sizeof(unsigned int);
  size_t i = case_initiale(code_nuplet(elements, t->taille), t->nb_bits);

  while (t->cases[i] != 0u && memcmp(elements_de(t, t->cases[i] - 1u), elements, octets) != 0) {
    i = (i + 1u) & masque;
  }
  return i;
}

/** Double le nombre de cases et replace tous les n-uplets. */
static bool agrandir_cases_nuplets(struct hachage_nuplets_s *t)
{
  unsigned int nouveaux_bits = t->nb_bits + 1u;
  size_t nouvelle_capacite = 2u * t->capacite;
  size_t masque = nouvelle_capacite - 1u;
  unsigned int *nouvelles;
  unsigned int id;
  size_t i;
  bool succes = false;

  if (nouveaux_bits <= NB_BITS_MAXIMAL && nouvelle_capacite > t->capacite) {
    nouvelles = calloc(nouvelle_capacite, sizeof(unsigned int));
    if (nouvelles != NULL) {
      for (id = 0u; id < t->nb; id++) {
        i = case_initiale(code_nuplet(elements_de(t, id), t->taille), nouveaux_bits);
        while (nouvelles[i] != 0u) {
          i = (i + 1u) & masque;
        }
        nouvelles[i] = id + 1u;
      }
      free(t->cases);
      t->cases = nouvelles;
      t->capacite = nouvelle_capacite;
      t->nb_bits = nouveaux_bits;
      succes = true;
    }
  }
  return succes;
}

/** Double la capacite du tableau des elements. */
static bool agrandir_elements(struct hachage_nuplets_s *t)
{
  size_t nouvelle_capacite = 2u * (size_t)t->capacite_nuplets;
  size_t octets_par_nuplet = (size_t)t->taille * sizeof(unsigned int);
  unsigned int *elements;
  bool succes = false;

  if (nouvelle_capacite > HACHAGE_NB_MOTS_MAX) {
    nouvelle_capacite = HACHAGE_NB_MOTS_MAX;
  }
  if (nouvelle_capacite > t->capacite_nuplets && nouvelle_capacite <= SIZE_MAX / octets_par_nuplet) {
    elements = realloc(t->elements, nouvelle_capacite * octets_par_nuplet);
    if (elements != NULL) {
      t->elements = elements;
      t->capacite_nuplets = (unsigned int)nouvelle_capacite;
      succes = true;
    }
  }
  return succes;
}

/**
 * Initialise une table vide (structure deja allouee). Renvoie false si la
 * memoire manque : la structure ne possede alors aucune memoire.
 */
static bool initialiser_nuplets(struct hachage_nuplets_s *t, unsigned int taille, unsigned int nb_prevus)
{
  unsigned int nb_bits = NB_BITS_MINIMAL;

  while (nb_bits < NB_BITS_MAXIMAL && ((size_t)1 << nb_bits) / 2u < (size_t)nb_prevus) {
    nb_bits++;
  }
  t->capacite = (size_t)1 << nb_bits;
  t->nb_bits = nb_bits;
  t->taille = taille;
  t->nb = 0u;
  t->capacite_nuplets = (nb_prevus > CAPACITE_ENTREES_MINIMALE) ? nb_prevus : CAPACITE_ENTREES_MINIMALE;
  t->cases = calloc(t->capacite, sizeof(unsigned int));
  t->elements = NULL;
  if ((size_t)t->capacite_nuplets <= SIZE_MAX / ((size_t)taille * sizeof(unsigned int))) {
    t->elements = malloc((size_t)t->capacite_nuplets * (size_t)taille * sizeof(unsigned int));
  }
  if (t->cases == NULL || t->elements == NULL) {
    free(t->cases);
    free(t->elements);
    t->cases = NULL;
    t->elements = NULL;
  }
  return t->cases != NULL;
}

/** Ajout d'un n-uplet (cf. hachage_nuplets_ajouter). */
static unsigned int ajouter_nuplet(struct hachage_nuplets_s *t, const unsigned int *elements)
{
  size_t i;
  unsigned int identifiant = HACHAGE_ABSENT;
  bool succes = true;

  i = chercher_nuplet(t, elements);
  if (t->cases[i] != 0u) {
    identifiant = t->cases[i] - 1u;
  } else if (t->nb >= HACHAGE_NB_MOTS_MAX) {
    errno = ERANGE;
  } else {
    if (2u * ((size_t)t->nb + 1u) > t->capacite) {
      succes = agrandir_cases_nuplets(t);
      if (succes) {
        i = chercher_nuplet(t, elements);
      }
    }
    if (succes && t->nb == t->capacite_nuplets) {
      succes = agrandir_elements(t);
    }
    if (succes) {
      identifiant = t->nb;
      memcpy(t->elements + ((size_t)t->taille * (size_t)identifiant),
             elements, (size_t)t->taille * sizeof(unsigned int));
      t->cases[i] = identifiant + 1u;
      t->nb++;
    } else {
      errno = ENOMEM;
    }
  }
  INVARIANT(2u * (size_t)t->nb <= t->capacite);
  INVARIANT(t->nb <= t->capacite_nuplets);
  return identifiant;
}

/** Identifiant d'un n-uplet, ou HACHAGE_ABSENT. */
static unsigned int identifiant_nuplet(const struct hachage_nuplets_s *t, const unsigned int *elements)
{
  size_t i = chercher_nuplet(t, elements);

  return (t->cases[i] != 0u) ? t->cases[i] - 1u : HACHAGE_ABSENT;
}

/** Nombre moyen de sondages d'une recherche fructueuse. */
static double sondage_moyen_nuplets(const struct hachage_nuplets_s *t)
{
  size_t i;
  size_t initiale;
  double total = 0.0;
  double moyenne = 0.0;

  if (t->nb > 0u) {
    for (i = 0u; i < t->capacite; i++) {
      if (t->cases[i] != 0u) {
        initiale = case_initiale(code_nuplet(elements_de(t, t->cases[i] - 1u), t->taille), t->nb_bits);
        total += (double)(((i - initiale) & (t->capacite - 1u)) + 1u);
      }
    }
    moyenne = total / (double)t->nb;
  }
  return moyenne;
}

hachage_nuplets_t *hachage_nuplets_creer(unsigned int taille, unsigned int nb_prevus)
{
  hachage_nuplets_t *t;

  PRECONDITION(taille >= 1u && taille <= HACHAGE_TAILLE_NUPLET_MAX);
  PRECONDITION(nb_prevus <= HACHAGE_NB_MOTS_MAX);

  t = malloc(sizeof(hachage_nuplets_t));
  if (t != NULL && !initialiser_nuplets(t, taille, nb_prevus)) {
    free(t);
    t = NULL;
  }
  if (t == NULL) {
    errno = ENOMEM;
  }
  POSTCONDITION(t == NULL || (hachage_nuplets_nb(t) == 0u
                              && hachage_nuplets_taille(t) == taille
                              && hachage_nuplets_capacite(t) / 2u >= (size_t)nb_prevus));
  return t;
}

void hachage_nuplets_detruire(hachage_nuplets_t **t)
{
  PRECONDITION(t != NULL);
  if (*t != NULL) {
    free((*t)->cases);
    free((*t)->elements);
    free(*t);
    *t = NULL;
  }
  POSTCONDITION(*t == NULL);
}

unsigned int hachage_nuplets_ajouter(hachage_nuplets_t *t, const unsigned int *elements)
{
  unsigned int identifiant;

  PRECONDITION(t != NULL);
  PRECONDITION(elements != NULL);
  identifiant = ajouter_nuplet(t, elements);
  POSTCONDITION(identifiant == HACHAGE_ABSENT
                || (identifiant < t->nb
                    && identifiant_nuplet(t, elements) == identifiant));
  return identifiant;
}

unsigned int hachage_nuplets_identifiant(const hachage_nuplets_t *t, const unsigned int *elements)
{
  PRECONDITION(t != NULL);
  PRECONDITION(elements != NULL);
  return identifiant_nuplet(t, elements);
}

unsigned int hachage_nuplets_element(const hachage_nuplets_t *t,
                                     unsigned int identifiant,
                                     unsigned int rang)
{
  PRECONDITION(t != NULL);
  PRECONDITION(identifiant < t->nb);
  PRECONDITION(rang < t->taille);
  return elements_de(t, identifiant)[rang];
}

unsigned int hachage_nuplets_taille(const hachage_nuplets_t *t)
{
  PRECONDITION(t != NULL);
  return t->taille;
}

unsigned int hachage_nuplets_nb(const hachage_nuplets_t *t)
{
  PRECONDITION(t != NULL);
  return t->nb;
}

size_t hachage_nuplets_capacite(const hachage_nuplets_t *t)
{
  PRECONDITION(t != NULL);
  return t->capacite;
}

double hachage_nuplets_sondage_moyen(const hachage_nuplets_t *t)
{
  double moyenne;

  PRECONDITION(t != NULL);
  moyenne = sondage_moyen_nuplets(t);
  POSTCONDITION(t->nb == 0u || moyenne >= 1.0);
  return moyenne;
}


/* ======================================================================== */
/*                 Table de hachage de paires d'identifiants                */
/* ======================================================================== */

hachage_paires_t *hachage_paires_creer(unsigned int nb_paires_prevues)
{
  hachage_paires_t *p;

  PRECONDITION(nb_paires_prevues <= HACHAGE_NB_MOTS_MAX);

  p = malloc(sizeof(hachage_paires_t));
  if (p != NULL && !initialiser_nuplets(&p->table, 2u, nb_paires_prevues)) {
    free(p);
    p = NULL;
  }
  if (p == NULL) {
    errno = ENOMEM;
  }
  POSTCONDITION(p == NULL || (hachage_paires_nb(p) == 0u
                              && hachage_paires_capacite(p) / 2u
                                 >= (size_t)nb_paires_prevues));
  return p;
}

void hachage_paires_detruire(hachage_paires_t **p)
{
  PRECONDITION(p != NULL);
  if (*p != NULL) {
    free((*p)->table.cases);
    free((*p)->table.elements);
    free(*p);
    *p = NULL;
  }
  POSTCONDITION(*p == NULL);
}

unsigned int hachage_paires_ajouter(hachage_paires_t *p, unsigned int premier, unsigned int second)
{
  unsigned int elements[2];
  unsigned int identifiant;

  PRECONDITION(p != NULL);
  elements[0] = premier;
  elements[1] = second;
  identifiant = ajouter_nuplet(&p->table, elements);
  POSTCONDITION(identifiant == HACHAGE_ABSENT
                || (hachage_paires_premier(p, identifiant) == premier
                    && hachage_paires_second(p, identifiant) == second));
  return identifiant;
}

unsigned int hachage_paires_identifiant(const hachage_paires_t *p,
                                        unsigned int premier,
                                        unsigned int second)
{
  unsigned int elements[2];

  PRECONDITION(p != NULL);
  elements[0] = premier;
  elements[1] = second;
  return identifiant_nuplet(&p->table, elements);
}

unsigned int hachage_paires_premier(const hachage_paires_t *p, unsigned int identifiant)
{
  PRECONDITION(p != NULL);
  PRECONDITION(identifiant < p->table.nb);
  return elements_de(&p->table, identifiant)[0];
}

unsigned int hachage_paires_second(const hachage_paires_t *p, unsigned int identifiant)
{
  PRECONDITION(p != NULL);
  PRECONDITION(identifiant < p->table.nb);
  return elements_de(&p->table, identifiant)[1];
}

unsigned int hachage_paires_nb(const hachage_paires_t *p)
{
  PRECONDITION(p != NULL);
  return p->table.nb;
}

size_t hachage_paires_capacite(const hachage_paires_t *p)
{
  PRECONDITION(p != NULL);
  return p->table.capacite;
}

double hachage_paires_sondage_moyen(const hachage_paires_t *p)
{
  double moyenne;

  PRECONDITION(p != NULL);
  moyenne = sondage_moyen_nuplets(&p->table);
  POSTCONDITION(p->table.nb == 0u || moyenne >= 1.0);
  return moyenne;
}
