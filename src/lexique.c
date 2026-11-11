/**
@file lexique.c
Fichier lexique.c

Implementation du lexique d'affichage (cf. lexique.h).

Representation :
  - mots[id] : variantes du mot (chaines recopiees dans des blocs memoire
    jamais deplaces), avec pour chacune le nombre d'occurrences issues de
    fichiers accentues et de fichiers sans accent ; votes de casse ; choix ;
  - bigrammes[id] : mot du bigramme et trois votes de Boyer et Moore
    (candidat + compteur) pour la variante, la casse et le separateur
    (liaison et marques de dialogue, ranges dans un seul octet) ;
  - un tampon de travail pour mettre une graphie en minuscules.

Vote de Boyer et Moore : on garde un candidat et un compteur. Une voix pour
le candidat incremente le compteur, une voix contre le decremente ; a zero,
la voix suivante devient candidate. Si une valeur a la majorite absolue,
c'est le candidat final ; sinon le candidat est une valeur souvent votee.
*/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "contrat.h"
#include "lexique.h"

/** Capacite initiale des tableaux de mots et de bigrammes. */
#define CAPACITE_INITIALE 16u

/** Capacite initiale du tableau des variantes d'un mot. */
#define CAPACITE_VARIANTES_INITIALE 2u

/** Taille d'un bloc de stockage des variantes (64 Kio). */
#define TAILLE_BLOC ((size_t)65536)

/** Candidat de casse d'un bigramme sans vote. */
#define CASSE_SANS_VOTE 0xFFu

/** Variante d'un mot et ses votes. */
typedef struct {
  const char *texte;
  unsigned int voix_accentuees;  /* occurrences issues de fichiers accentues */
  unsigned int voix_autres;      /* occurrences issues des autres fichiers */
} variante_t;

/** Mot du lexique. */
typedef struct {
  variante_t *variantes;
  unsigned int nb_variantes;
  unsigned int capacite;
  unsigned int voix_casse[NB_CASSES];        /* hors debut de phrase */
  unsigned int voix_casse_debut[NB_CASSES];  /* en debut de phrase */
  unsigned int variante_choisie;             /* LEXIQUE_AUCUNE si aucune */
  casse_t casse_choisie;
} mot_t;

/** Bigramme du lexique : votes de Boyer et Moore. */
typedef struct {
  unsigned int mot;
  unsigned int variante;       /* candidat, ou LEXIQUE_AUCUNE */
  unsigned int voix_variante;
  unsigned int voix_casse;
  unsigned int voix_separateur;
  unsigned char casse;         /* candidat, ou CASSE_SANS_VOTE */
  unsigned char separateur;    /* candidat : liaison + NB_LIAISONS * marques
                                  (0, soit LIAISON_ESPACE sans marque, en
                                  l'absence de vote) */
} bigramme_t;

/** Bloc de stockage des variantes (membre tableau flexible C99). */
typedef struct bloc_s {
  struct bloc_s *precedent;
  size_t taille;
  size_t utilise;
  char donnees[];
} bloc_t;

struct lexique_s {
  mot_t *mots;
  unsigned int nb_mots;
  unsigned int capacite_mots;
  bigramme_t *bigrammes;
  unsigned int nb_bigrammes;
  unsigned int capacite_bigrammes;
  bloc_t *blocs;
  char *tampon;               /* tampon de travail (minuscules) */
  size_t capacite_tampon;
  bool finalise;
};


/* ======================================================================== */
/*                          Lettres UTF-8 reconnues                         */
/* ======================================================================== */

/** Lettre non ASCII reconnue (Latin-1, OE lies, Y trema majuscule). */
static bool est_lettre_etendue(unsigned int point_code)
{
  return (point_code >= 0xC0u && point_code <= 0xFFu
          && point_code != 0xD7u && point_code != 0xF7u)
         || point_code == 0x152u || point_code == 0x153u
         || point_code == 0x178u;
}

/**
 * Decode la lettre UTF-8 en tete de texte. Renvoie le nombre d'octets
 * qu'elle occupe (1 ou 2), ou 0 si le texte ne commence pas par une lettre
 * reconnue correctement encodee.
 */
static size_t lire_lettre(const char *texte, unsigned int *point_code)
{
  const unsigned char *octets = (const unsigned char *)texte;
  unsigned int code;
  size_t taille = 0u;

  *point_code = 0u;
  if ((octets[0] >= 'a' && octets[0] <= 'z')
      || (octets[0] >= 'A' && octets[0] <= 'Z')) {
    *point_code = octets[0];
    taille = 1u;
  } else if (octets[0] >= 0xC2u && octets[0] <= 0xDFu
             && (octets[1] & 0xC0u) == 0x80u) {
    code = ((octets[0] & 0x1Fu) << 6) | (octets[1] & 0x3Fu);
    if (est_lettre_etendue(code)) {
      *point_code = code;
      taille = 2u;
    }
  }
  return taille;
}

/** Ecrit une lettre en UTF-8 (1 octet en ASCII, 2 sinon). */
static size_t ecrire_lettre(char *destination, unsigned int point_code)
{
  size_t taille = 1u;

  if (point_code < 0x80u) {
    destination[0] = (char)point_code;
  } else {
    destination[0] = (char)(0xC0u | (point_code >> 6));
    destination[1] = (char)(0x80u | (point_code & 0x3Fu));
    taille = 2u;
  }
  return taille;
}

static bool est_majuscule(unsigned int point_code)
{
  return (point_code >= 'A' && point_code <= 'Z')
         || (point_code >= 0xC0u && point_code <= 0xDEu
             && point_code != 0xD7u)
         || point_code == 0x152u || point_code == 0x178u;
}

static unsigned int en_minuscule(unsigned int point_code)
{
  unsigned int resultat = point_code;

  if (point_code >= 'A' && point_code <= 'Z') {
    resultat = point_code + ('a' - 'A');
  } else if (point_code >= 0xC0u && point_code <= 0xDEu
             && point_code != 0xD7u) {
    resultat = point_code + 0x20u;
  } else if (point_code == 0x152u) {
    resultat = 0x153u;
  } else if (point_code == 0x178u) {
    resultat = 0xFFu;
  }
  return resultat;
}

static unsigned int en_majuscule(unsigned int point_code)
{
  unsigned int resultat = point_code;

  if (point_code >= 'a' && point_code <= 'z') {
    resultat = point_code - ('a' - 'A');
  } else if (point_code >= 0xE0u && point_code <= 0xFEu
             && point_code != 0xF7u) {
    resultat = point_code - 0x20u;
  } else if (point_code == 0x153u) {
    resultat = 0x152u;
  } else if (point_code == 0xFFu) {
    resultat = 0x178u;
  }
  /* l'eszett (U+00DF) n'a pas de majuscule d'un seul caractere */
  return resultat;
}

bool lexique_graphie_valide(const char *graphie)
{
  const char *c = graphie;
  unsigned int point_code;
  size_t taille = 1u;

  PRECONDITION(graphie != NULL);
  while (*c != '\0' && taille > 0u) {
    taille = lire_lettre(c, &point_code);
    c += taille;
  }
  return graphie[0] != '\0' && taille > 0u;
}

bool lexique_variante_valide(const char *variante)
{
  const char *c = variante;
  unsigned int point_code;
  bool valide;

  PRECONDITION(variante != NULL);
  valide = lexique_graphie_valide(variante);
  while (valide && *c != '\0') {
    c += lire_lettre(c, &point_code);
    valide = !est_majuscule(point_code);
  }
  return valide;
}

casse_t lexique_casse(const char *graphie)
{
  const char *c = graphie;
  unsigned int point_code;
  unsigned int nb_lettres = 0u;
  unsigned int nb_majuscules = 0u;
  bool premiere_majuscule;
  casse_t casse = CASSE_MINUSCULE;

  PRECONDITION(graphie != NULL);
  PRECONDITION(lexique_graphie_valide(graphie));

  lire_lettre(c, &point_code);
  premiere_majuscule = est_majuscule(point_code);
  while (*c != '\0') {
    c += lire_lettre(c, &point_code);
    nb_lettres++;
    if (est_majuscule(point_code)) {
      nb_majuscules++;
    }
  }
  if (premiere_majuscule) {
    casse = (nb_lettres >= 2u && nb_majuscules == nb_lettres)
            ? CASSE_MAJUSCULE : CASSE_CAPITALE;
  }
  return casse;
}

void lexique_appliquer_casse(char *graphie, casse_t casse)
{
  char *c = graphie;
  unsigned int point_code;
  unsigned int rang = 0u;
  size_t taille;
  size_t ecrits;
  size_t longueur;

  PRECONDITION(graphie != NULL);
  PRECONDITION(lexique_graphie_valide(graphie));
  PRECONDITION(casse < NB_CASSES);

  longueur = strlen(graphie);
  while (*c != '\0') {
    taille = lire_lettre(c, &point_code);
    if (casse == CASSE_MAJUSCULE || (casse == CASSE_CAPITALE && rang == 0u)) {
      point_code = en_majuscule(point_code);
    } else {
      point_code = en_minuscule(point_code);
    }
    /* meme taille en majuscule et en minuscule : ecriture sur place */
    ecrits = ecrire_lettre(c, point_code);
    INVARIANT(ecrits == taille);
    c += ecrits;
    rang++;
  }
  POSTCONDITION(strlen(graphie) == longueur);
  POSTCONDITION(lexique_graphie_valide(graphie));
}


/* ======================================================================== */
/*                            Gestion memoire                               */
/* ======================================================================== */

/** Recopie une chaine dans les blocs ; NULL si la memoire manque. */
static char *stocker(lexique_t *l, const char *texte)
{
  size_t taille = strlen(texte) + 1u;
  size_t taille_bloc;
  bloc_t *bloc = l->blocs;
  char *copie = NULL;

  if (bloc == NULL || bloc->taille - bloc->utilise < taille) {
    taille_bloc = (taille > TAILLE_BLOC) ? taille : TAILLE_BLOC;
    bloc = NULL;
    if (taille_bloc <= SIZE_MAX - sizeof(bloc_t)) {
      bloc = malloc(sizeof(bloc_t) + taille_bloc);
    }
    if (bloc != NULL) {
      bloc->precedent = l->blocs;
      bloc->taille = taille_bloc;
      bloc->utilise = 0u;
      l->blocs = bloc;
    }
  }
  if (bloc != NULL) {
    copie = bloc->donnees + bloc->utilise;
    memcpy(copie, texte, taille);
    bloc->utilise += taille;
  }
  return copie;
}

/**
 * Garantit la place d'un element de plus dans un tableau dynamique
 * (doublement). Renvoie false si la memoire manque (tableau inchange).
 */
static bool reserver(void **tableau, unsigned int nb, unsigned int *capacite,
                     size_t taille_element)
{
  size_t nouvelle_capacite;
  void *agrandi;
  bool succes = true;

  if (nb == *capacite) {
    nouvelle_capacite = (*capacite == 0u) ? CAPACITE_INITIALE : 2u * (size_t)*capacite;
    if (nouvelle_capacite > LEXIQUE_NB_MAX) {
      nouvelle_capacite = LEXIQUE_NB_MAX;
    }
    agrandi = NULL;
    if (nouvelle_capacite > *capacite
        && nouvelle_capacite <= SIZE_MAX / taille_element) {
      agrandi = realloc(*tableau, nouvelle_capacite * taille_element);
    }
    if (agrandi == NULL) {
      succes = false;
    } else {
      *tableau = agrandi;
      *capacite = (unsigned int)nouvelle_capacite;
    }
  }
  return succes;
}

/** Vote de Boyer et Moore pour une valeur entiere. */
static void voter(unsigned int *candidat, unsigned int *voix,
                  unsigned int valeur)
{
  if (*voix == 0u) {
    *candidat = valeur;
    *voix = 1u;
  } else if (*candidat == valeur) {
    if (*voix < UINT_MAX) {
      (*voix)++;
    }
  } else {
    (*voix)--;
  }
}

/** Vote de Boyer et Moore pour une valeur d'un octet. */
static void voter_octet(unsigned char *candidat, unsigned int *voix, unsigned char valeur)
{
  unsigned int candidat_entier = *candidat;

  voter(&candidat_entier, voix, valeur);
  *candidat = (unsigned char)candidat_entier;
}

/** Range une liaison et des marques de dialogue dans un octet. */
static unsigned char code_separateur(liaison_t liaison, unsigned int marques)
{
  return (unsigned char)((unsigned int)liaison + ((unsigned int)NB_LIAISONS * marques));
}

/** Incremente un compteur sans depassement. */
static void incrementer(unsigned int *compteur)
{
  if (*compteur < UINT_MAX) {
    (*compteur)++;
  }
}


/* ======================================================================== */
/*                          Creation et destruction                         */
/* ======================================================================== */

lexique_t *lexique_creer(void)
{
  lexique_t *l = malloc(sizeof(lexique_t));

  if (l == NULL) {
    errno = ENOMEM;
  } else {
    l->mots = NULL;
    l->nb_mots = 0u;
    l->capacite_mots = 0u;
    l->bigrammes = NULL;
    l->nb_bigrammes = 0u;
    l->capacite_bigrammes = 0u;
    l->blocs = NULL;
    l->tampon = NULL;
    l->capacite_tampon = 0u;
    l->finalise = false;
  }
  return l;
}

void lexique_detruire(lexique_t **l)
{
  unsigned int i;
  bloc_t *bloc;
  bloc_t *precedent;

  PRECONDITION(l != NULL);
  if (*l != NULL) {
    for (i = 0u; i < (*l)->nb_mots; i++) {
      free((*l)->mots[i].variantes);
    }
    free((*l)->mots);
    free((*l)->bigrammes);
    bloc = (*l)->blocs;
    while (bloc != NULL) {
      precedent = bloc->precedent;
      free(bloc);
      bloc = precedent;
    }
    free((*l)->tampon);
    free(*l);
    *l = NULL;
  }
  POSTCONDITION(*l == NULL);
}


/* ======================================================================== */
/*                          Phase d'apprentissage                           */
/* ======================================================================== */

unsigned int lexique_ajouter_mot(lexique_t *l)
{
  unsigned int identifiant = LEXIQUE_ABSENT;
  mot_t *mot;
  void *tableau;
  casse_t c;

  PRECONDITION(l != NULL);
  PRECONDITION(!l->finalise);

  tableau = l->mots;
  if (l->nb_mots >= LEXIQUE_NB_MAX) {
    errno = ERANGE;
  } else if (!reserver(&tableau, l->nb_mots, &l->capacite_mots, sizeof(mot_t))) {
    errno = ENOMEM;
  } else {
    l->mots = tableau;
    identifiant = l->nb_mots;
    mot = &l->mots[identifiant];
    mot->variantes = NULL;
    mot->nb_variantes = 0u;
    mot->capacite = 0u;
    for (c = CASSE_MINUSCULE; c < NB_CASSES; c++) {
      mot->voix_casse[c] = 0u;
      mot->voix_casse_debut[c] = 0u;
    }
    mot->variante_choisie = LEXIQUE_AUCUNE;
    mot->casse_choisie = CASSE_MINUSCULE;
    l->nb_mots++;
  }
  return identifiant;
}

unsigned int lexique_ajouter_bigramme(lexique_t *l, unsigned int mot)
{
  unsigned int identifiant = LEXIQUE_ABSENT;
  bigramme_t *bigramme;
  void *tableau;

  PRECONDITION(l != NULL);
  PRECONDITION(!l->finalise);
  PRECONDITION(mot < l->nb_mots);

  tableau = l->bigrammes;
  if (l->nb_bigrammes >= LEXIQUE_NB_MAX) {
    errno = ERANGE;
  } else if (!reserver(&tableau, l->nb_bigrammes, &l->capacite_bigrammes, sizeof(bigramme_t))) {
    errno = ENOMEM;
  } else {
    l->bigrammes = tableau;
    identifiant = l->nb_bigrammes;
    bigramme = &l->bigrammes[identifiant];
    bigramme->mot = mot;
    bigramme->variante = LEXIQUE_AUCUNE;
    bigramme->voix_variante = 0u;
    bigramme->voix_casse = 0u;
    bigramme->voix_separateur = 0u;
    bigramme->casse = CASSE_SANS_VOTE;
    bigramme->separateur = code_separateur(LIAISON_ESPACE, MARQUE_AUCUNE);
    l->nb_bigrammes++;
  }
  return identifiant;
}

unsigned int lexique_ajouter_variante(lexique_t *l, unsigned int mot, const char *variante)
{
  mot_t *m;
  variante_t *variantes;
  size_t nouvelle_capacite;
  const char *copie;
  unsigned int rang = 0u;
  unsigned int resultat = LEXIQUE_ABSENT;

  PRECONDITION(l != NULL);
  PRECONDITION(!l->finalise);
  PRECONDITION(mot < l->nb_mots);
  PRECONDITION(variante != NULL);
  PRECONDITION(lexique_variante_valide(variante));

  m = &l->mots[mot];
  while (rang < m->nb_variantes && strcmp(m->variantes[rang].texte, variante) != 0) {
    rang++;
  }
  if (rang < m->nb_variantes) {
    resultat = rang;                        /* variante deja connue */
  } else if (m->nb_variantes >= LEXIQUE_NB_MAX) {
    errno = ERANGE;
  } else {
    /* 1. place dans le tableau des variantes */
    if (m->nb_variantes == m->capacite) {
      nouvelle_capacite = (m->capacite == 0u)
                          ? CAPACITE_VARIANTES_INITIALE
                          : 2u * (size_t)m->capacite;
      if (nouvelle_capacite > LEXIQUE_NB_MAX) {
        nouvelle_capacite = LEXIQUE_NB_MAX;
      }
      variantes = NULL;
      if (nouvelle_capacite <= SIZE_MAX / sizeof(variante_t)) {
        variantes = realloc(m->variantes, nouvelle_capacite * sizeof(variante_t));
      }
      if (variantes != NULL) {
        m->variantes = variantes;
        m->capacite = (unsigned int)nouvelle_capacite;
      }
    }
    /* 2. recopie de la chaine (rien n'est modifie si elle echoue) */
    copie = NULL;
    if (m->nb_variantes < m->capacite) {
      copie = stocker(l, variante);
    }
    if (copie == NULL) {
      errno = ENOMEM;
    } else {
      resultat = m->nb_variantes;
      m->variantes[resultat].texte = copie;
      m->variantes[resultat].voix_accentuees = 0u;
      m->variantes[resultat].voix_autres = 0u;
      m->nb_variantes++;
    }
  }
  POSTCONDITION(resultat == LEXIQUE_ABSENT || strcmp(m->variantes[resultat].texte, variante) == 0);
  return resultat;
}

bool lexique_separateur_valide(liaison_t liaison, unsigned int marques)
{
  return (unsigned int)liaison < (unsigned int)NB_LIAISONS
         && marques <= MARQUES_TOUTES
         && (marques == MARQUE_AUCUNE
             || (liaison != LIAISON_APOSTROPHE
                 && liaison != LIAISON_TRAIT_UNION));
}

bool lexique_observer(lexique_t *l, unsigned int bigramme,
                      const char *graphie, bool accentue, bool debut_phrase,
                      liaison_t liaison, unsigned int marques)
{
  bigramme_t *b;
  mot_t *m;
  variante_t *v;
  size_t longueur;
  char *tampon;
  unsigned int rang = LEXIQUE_ABSENT;
  casse_t casse;
  bool succes = true;

  PRECONDITION(l != NULL);
  PRECONDITION(!l->finalise);
  PRECONDITION(bigramme < l->nb_bigrammes);
  PRECONDITION(graphie != NULL);
  PRECONDITION(lexique_graphie_valide(graphie));
  PRECONDITION(lexique_separateur_valide(liaison, marques));

  b = &l->bigrammes[bigramme];
  /* 1. variante = graphie en minuscules, dans le tampon de travail */
  longueur = strlen(graphie);
  if (longueur + 1u > l->capacite_tampon) {
    tampon = realloc(l->tampon, longueur + 1u);
    if (tampon == NULL) {
      errno = ENOMEM;
      succes = false;
    } else {
      l->tampon = tampon;
      l->capacite_tampon = longueur + 1u;
    }
  }
  if (succes) {
    memcpy(l->tampon, graphie, longueur + 1u);
    lexique_appliquer_casse(l->tampon, CASSE_MINUSCULE);
    rang = lexique_ajouter_variante(l, b->mot, l->tampon);
    succes = (rang != LEXIQUE_ABSENT);
  }
  /* 2. votes (plus aucune allocation : ne peut plus echouer) */
  if (succes) {
    m = &l->mots[b->mot];
    v = &m->variantes[rang];
    casse = lexique_casse(graphie);
    if (accentue) {
      incrementer(&v->voix_accentuees);
      voter(&b->variante, &b->voix_variante, rang);
    } else {
      incrementer(&v->voix_autres);
    }
    if (debut_phrase) {
      /* devant le premier mot d'une phrase : ni liaison ni guillemet
         fermant (il appartient a la phrase precedente) */
      incrementer(&m->voix_casse_debut[casse]);
      voter_octet(&b->separateur, &b->voix_separateur,
                  code_separateur(LIAISON_ESPACE, marques
                                  & ~(unsigned int)MARQUE_FERME_GUILLEMET));
    } else {
      incrementer(&m->voix_casse[casse]);
      voter_octet(&b->casse, &b->voix_casse, (unsigned char)casse);
      voter_octet(&b->separateur, &b->voix_separateur, code_separateur(liaison, marques));
    }
  }
  return succes;
}

void lexique_finaliser(lexique_t *l)
{
  unsigned int i;
  unsigned int r;
  mot_t *m;
  const variante_t *v;
  const variante_t *meilleure;
  unsigned int total_casse;
  casse_t c;

  PRECONDITION(l != NULL);
  PRECONDITION(!l->finalise);

  for (i = 0u; i < l->nb_mots; i++) {
    m = &l->mots[i];
    /* variante : la plus votee dans les fichiers accentues, puis dans les
       autres ; a egalite, la premiere rencontree */
    if (m->nb_variantes > 0u) {
      m->variante_choisie = 0u;
      for (r = 1u; r < m->nb_variantes; r++) {
        v = &m->variantes[r];
        meilleure = &m->variantes[m->variante_choisie];
        if (v->voix_accentuees > meilleure->voix_accentuees
            || (v->voix_accentuees == meilleure->voix_accentuees
                && v->voix_autres > meilleure->voix_autres)) {
          m->variante_choisie = r;
        }
      }
    }
    /* casse : la plus votee hors debut de phrase ; sinon minuscules, sauf
       si le mot etait surtout ecrit tout en majuscules (titres) */
    total_casse = 0u;
    m->casse_choisie = CASSE_MINUSCULE;
    for (c = CASSE_MINUSCULE; c < NB_CASSES; c++) {
      total_casse += m->voix_casse[c];
      if (m->voix_casse[c] > m->voix_casse[m->casse_choisie]) {
        m->casse_choisie = c;
      }
    }
    if (total_casse == 0u
        && m->voix_casse_debut[CASSE_MAJUSCULE]
           > m->voix_casse_debut[CASSE_MINUSCULE]
             + m->voix_casse_debut[CASSE_CAPITALE]) {
      m->casse_choisie = CASSE_MAJUSCULE;
    }
    POSTCONDITION(m->nb_variantes == 0u || m->variante_choisie < m->nb_variantes);
  }
  /* les candidats de Boyer et Moore des bigrammes sont deja les choix */
  free(l->tampon);
  l->tampon = NULL;
  l->capacite_tampon = 0u;
  l->finalise = true;
  POSTCONDITION(lexique_est_finalise(l));
}


/* ======================================================================== */
/*                               Consultation                               */
/* ======================================================================== */

bool lexique_est_finalise(const lexique_t *l)
{
  PRECONDITION(l != NULL);
  return l->finalise;
}

unsigned int lexique_nb_mots(const lexique_t *l)
{
  PRECONDITION(l != NULL);
  return l->nb_mots;
}

unsigned int lexique_nb_bigrammes(const lexique_t *l)
{
  PRECONDITION(l != NULL);
  return l->nb_bigrammes;
}

unsigned int lexique_mot_bigramme(const lexique_t *l, unsigned int bigramme)
{
  PRECONDITION(l != NULL);
  PRECONDITION(bigramme < l->nb_bigrammes);
  return l->bigrammes[bigramme].mot;
}

unsigned int lexique_nb_variantes(const lexique_t *l, unsigned int mot)
{
  PRECONDITION(l != NULL);
  PRECONDITION(mot < l->nb_mots);
  return l->mots[mot].nb_variantes;
}

const char *lexique_variante(const lexique_t *l, unsigned int mot, unsigned int rang)
{
  PRECONDITION(l != NULL);
  PRECONDITION(mot < l->nb_mots);
  PRECONDITION(rang < l->mots[mot].nb_variantes);
  return l->mots[mot].variantes[rang].texte;
}

unsigned int lexique_variante_mot(const lexique_t *l, unsigned int mot)
{
  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(mot < l->nb_mots);
  return l->mots[mot].variante_choisie;
}

casse_t lexique_casse_mot(const lexique_t *l, unsigned int mot)
{
  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(mot < l->nb_mots);
  return l->mots[mot].casse_choisie;
}

unsigned int lexique_variante_bigramme(const lexique_t *l, unsigned int bigramme)
{
  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(bigramme < l->nb_bigrammes);
  return l->bigrammes[bigramme].variante;
}

unsigned int lexique_casse_bigramme(const lexique_t *l, unsigned int bigramme)
{
  unsigned int casse;

  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(bigramme < l->nb_bigrammes);
  casse = l->bigrammes[bigramme].casse;
  return (casse == CASSE_SANS_VOTE) ? LEXIQUE_AUCUNE : casse;
}

liaison_t lexique_liaison(const lexique_t *l, unsigned int bigramme)
{
  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(bigramme < l->nb_bigrammes);
  return (liaison_t)(l->bigrammes[bigramme].separateur % (unsigned int)NB_LIAISONS);
}

unsigned int lexique_marques(const lexique_t *l, unsigned int bigramme)
{
  unsigned int marques;

  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(bigramme < l->nb_bigrammes);
  marques = l->bigrammes[bigramme].separateur / (unsigned int)NB_LIAISONS;
  POSTCONDITION(lexique_separateur_valide(lexique_liaison(l, bigramme),
                                          marques));
  return marques;
}

/**
 * Recopie une variante dans le tampon avec la casse voulue (capitale en
 * debut de phrase) si le tampon est assez grand ; renvoie sa longueur.
 */
static size_t ecrire_variante(const char *texte, casse_t casse,
                              bool debut_phrase, char *tampon, size_t taille)
{
  size_t longueur = strlen(texte);

  if (debut_phrase && casse == CASSE_MINUSCULE) {
    casse = CASSE_CAPITALE;
  }
  if (taille > longueur) {
    memcpy(tampon, texte, longueur + 1u);
    lexique_appliquer_casse(tampon, casse);
  }
  return longueur;
}

size_t lexique_ecrire(const lexique_t *l, unsigned int bigramme,
                      bool debut_phrase, char *tampon, size_t taille)
{
  const bigramme_t *b;
  const mot_t *m;
  unsigned int rang;
  casse_t casse;

  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(bigramme < l->nb_bigrammes);
  PRECONDITION(l->mots[l->bigrammes[bigramme].mot].variante_choisie != LEXIQUE_AUCUNE);
  PRECONDITION(tampon != NULL || taille == 0u);

  b = &l->bigrammes[bigramme];
  m = &l->mots[b->mot];
  rang = (b->variante != LEXIQUE_AUCUNE) ? b->variante : m->variante_choisie;
  casse = (b->casse != CASSE_SANS_VOTE) ? (casse_t)b->casse : m->casse_choisie;
  return ecrire_variante(m->variantes[rang].texte, casse, debut_phrase, tampon, taille);
}

size_t lexique_ecrire_mot(const lexique_t *l, unsigned int mot,
                          bool debut_phrase, char *tampon, size_t taille)
{
  const mot_t *m;

  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(mot < l->nb_mots);
  PRECONDITION(l->mots[mot].variante_choisie != LEXIQUE_AUCUNE);
  PRECONDITION(tampon != NULL || taille == 0u);

  m = &l->mots[mot];
  return ecrire_variante(m->variantes[m->variante_choisie].texte,
                         m->casse_choisie, debut_phrase, tampon, taille);
}

void lexique_fixer_mot(lexique_t *l, unsigned int mot, unsigned int variante, casse_t casse)
{
  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(mot < l->nb_mots);
  PRECONDITION(variante < l->mots[mot].nb_variantes);
  PRECONDITION(casse < NB_CASSES);

  l->mots[mot].variante_choisie = variante;
  l->mots[mot].casse_choisie = casse;
}

void lexique_fixer_bigramme(lexique_t *l, unsigned int bigramme,
                            unsigned int variante, unsigned int casse,
                            liaison_t liaison, unsigned int marques)
{
  bigramme_t *b;

  PRECONDITION(l != NULL);
  PRECONDITION(l->finalise);
  PRECONDITION(bigramme < l->nb_bigrammes);
  PRECONDITION(variante == LEXIQUE_AUCUNE
               || variante < l->mots[l->bigrammes[bigramme].mot].nb_variantes);
  PRECONDITION(casse == LEXIQUE_AUCUNE || casse < NB_CASSES);
  PRECONDITION(lexique_separateur_valide(liaison, marques));

  b = &l->bigrammes[bigramme];
  b->variante = variante;
  b->casse = (casse == LEXIQUE_AUCUNE) ? (unsigned char)CASSE_SANS_VOTE : (unsigned char)casse;
  b->separateur = code_separateur(liaison, marques);
  POSTCONDITION(lexique_variante_bigramme(l, bigramme) == variante);
  POSTCONDITION(lexique_casse_bigramme(l, bigramme) == casse);
  POSTCONDITION(lexique_liaison(l, bigramme) == liaison);
  POSTCONDITION(lexique_marques(l, bigramme) == marques);
}
