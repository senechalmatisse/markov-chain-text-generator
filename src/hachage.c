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
  POSTCONDITION(identifiant == HACHAGE_ABSENT
                || strcmp(h->entrees[identifiant].mot, mot) == 0);
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
  size_t initiale = case_initiale(h->entrees[h->cases[p] - 1u].code,
                                  h->nb_bits);

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
/*                 Table de hachage de paires d'identifiants                */
/* ======================================================================== */

/* Le code d'une paire range ses deux elements dans 64 bits. */
#if UINT_MAX > 0xFFFFFFFFu
#error "les paires supposent des unsigned int d'au plus 32 bits"
#endif

/*
Representation (memes principes que la table des mots) :
  - cases[0 .. capacite-1] : 0 si la case est libre, identifiant + 1 sinon ;
  - codes[id] = premier * 2^32 + second pour chaque identifiant id < nb.
Invariants : capacite == 2^nb_bits ; 2 * nb <= capacite ; chaque paire est
atteignable depuis sa case initiale sans case libre.
*/
struct hachage_paires_s {
  unsigned int *cases;
  size_t capacite;
  unsigned int nb_bits;
  uint64_t *codes;
  unsigned int nb;
  unsigned int capacite_codes;
};

/** Code injectif d'une paire. */
static uint64_t code_paire(unsigned int premier, unsigned int second)
{
  return ((uint64_t)premier << 32) | (uint64_t)second;
}

/** Case contenant le code, ou premiere case libre rencontree. */
static size_t chercher_paire(const hachage_paires_t *p, uint64_t code)
{
  size_t masque = p->capacite - 1u;
  size_t i = case_initiale(code, p->nb_bits);

  while (p->cases[i] != 0u && p->codes[p->cases[i] - 1u] != code) {
    i = (i + 1u) & masque;
  }
  return i;
}

/** Double le nombre de cases et replace toutes les paires. */
static bool agrandir_cases_paires(hachage_paires_t *p)
{
  unsigned int nouveaux_bits = p->nb_bits + 1u;
  size_t nouvelle_capacite = 2u * p->capacite;
  size_t masque = nouvelle_capacite - 1u;
  unsigned int *nouvelles;
  unsigned int id;
  size_t i;
  bool succes = false;

  if (nouveaux_bits <= NB_BITS_MAXIMAL && nouvelle_capacite > p->capacite) {
    nouvelles = calloc(nouvelle_capacite, sizeof(unsigned int));
    if (nouvelles != NULL) {
      for (id = 0u; id < p->nb; id++) {
        i = case_initiale(p->codes[id], nouveaux_bits);
        while (nouvelles[i] != 0u) {
          i = (i + 1u) & masque;
        }
        nouvelles[i] = id + 1u;
      }
      free(p->cases);
      p->cases = nouvelles;
      p->capacite = nouvelle_capacite;
      p->nb_bits = nouveaux_bits;
      succes = true;
    }
  }
  return succes;
}

/** Double la capacite du tableau des codes. */
static bool agrandir_codes(hachage_paires_t *p)
{
  size_t nouvelle_capacite = 2u * (size_t)p->capacite_codes;
  uint64_t *codes;
  bool succes = false;

  if (nouvelle_capacite > HACHAGE_NB_MOTS_MAX) {
    nouvelle_capacite = HACHAGE_NB_MOTS_MAX;
  }
  if (nouvelle_capacite > p->capacite_codes
      && nouvelle_capacite <= SIZE_MAX / sizeof(uint64_t)) {
    codes = realloc(p->codes, nouvelle_capacite * sizeof(uint64_t));
    if (codes != NULL) {
      p->codes = codes;
      p->capacite_codes = (unsigned int)nouvelle_capacite;
      succes = true;
    }
  }
  return succes;
}

hachage_paires_t *hachage_paires_creer(unsigned int nb_paires_prevues)
{
  hachage_paires_t *p;
  unsigned int nb_bits = NB_BITS_MINIMAL;
  unsigned int capacite_codes;

  PRECONDITION(nb_paires_prevues <= HACHAGE_NB_MOTS_MAX);

  while (nb_bits < NB_BITS_MAXIMAL
         && ((size_t)1 << nb_bits) / 2u < (size_t)nb_paires_prevues) {
    nb_bits++;
  }
  capacite_codes = (nb_paires_prevues > CAPACITE_ENTREES_MINIMALE)
                   ? nb_paires_prevues : CAPACITE_ENTREES_MINIMALE;
  p = malloc(sizeof(hachage_paires_t));
  if (p != NULL) {
    p->capacite = (size_t)1 << nb_bits;
    p->nb_bits = nb_bits;
    p->nb = 0u;
    p->capacite_codes = capacite_codes;
    p->cases = calloc(p->capacite, sizeof(unsigned int));
    p->codes = calloc(capacite_codes, sizeof(uint64_t));
    if (p->cases == NULL || p->codes == NULL) {
      free(p->cases);
      free(p->codes);
      free(p);
      p = NULL;
    }
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
    free((*p)->cases);
    free((*p)->codes);
    free(*p);
    *p = NULL;
  }
  POSTCONDITION(*p == NULL);
}

unsigned int hachage_paires_ajouter(hachage_paires_t *p, unsigned int premier,
                                    unsigned int second)
{
  uint64_t code = code_paire(premier, second);
  size_t i;
  unsigned int identifiant = HACHAGE_ABSENT;
  bool succes = true;

  PRECONDITION(p != NULL);

  i = chercher_paire(p, code);
  if (p->cases[i] != 0u) {
    identifiant = p->cases[i] - 1u;
  } else if (p->nb >= HACHAGE_NB_MOTS_MAX) {
    errno = ERANGE;
  } else {
    if (2u * ((size_t)p->nb + 1u) > p->capacite) {
      succes = agrandir_cases_paires(p);
      if (succes) {
        i = chercher_paire(p, code);
      }
    }
    if (succes && p->nb == p->capacite_codes) {
      succes = agrandir_codes(p);
    }
    if (succes) {
      identifiant = p->nb;
      p->codes[identifiant] = code;
      p->cases[i] = identifiant + 1u;
      p->nb++;
    } else {
      errno = ENOMEM;
    }
  }
  INVARIANT(2u * (size_t)p->nb <= p->capacite);
  POSTCONDITION(identifiant == HACHAGE_ABSENT
                || p->codes[identifiant] == code);
  return identifiant;
}

unsigned int hachage_paires_identifiant(const hachage_paires_t *p,
                                        unsigned int premier,
                                        unsigned int second)
{
  size_t i;
  unsigned int identifiant = HACHAGE_ABSENT;

  PRECONDITION(p != NULL);
  i = chercher_paire(p, code_paire(premier, second));
  if (p->cases[i] != 0u) {
    identifiant = p->cases[i] - 1u;
  }
  return identifiant;
}

unsigned int hachage_paires_premier(const hachage_paires_t *p,
                                    unsigned int identifiant)
{
  PRECONDITION(p != NULL);
  PRECONDITION(identifiant < p->nb);
  return (unsigned int)(p->codes[identifiant] >> 32);
}

unsigned int hachage_paires_second(const hachage_paires_t *p,
                                   unsigned int identifiant)
{
  PRECONDITION(p != NULL);
  PRECONDITION(identifiant < p->nb);
  return (unsigned int)(p->codes[identifiant] & UINT64_C(0xFFFFFFFF));
}

unsigned int hachage_paires_nb(const hachage_paires_t *p)
{
  PRECONDITION(p != NULL);
  return p->nb;
}

size_t hachage_paires_capacite(const hachage_paires_t *p)
{
  PRECONDITION(p != NULL);
  return p->capacite;
}

double hachage_paires_sondage_moyen(const hachage_paires_t *p)
{
  size_t i;
  size_t initiale;
  double total = 0.0;
  double moyenne = 0.0;

  PRECONDITION(p != NULL);
  if (p->nb > 0u) {
    for (i = 0u; i < p->capacite; i++) {
      if (p->cases[i] != 0u) {
        initiale = case_initiale(p->codes[p->cases[i] - 1u], p->nb_bits);
        total += (double)(((i - initiale) & (p->capacite - 1u)) + 1u);
      }
    }
    moyenne = total / (double)p->nb;
  }
  POSTCONDITION(p->nb == 0u || moyenne >= 1.0);
  return moyenne;
}
