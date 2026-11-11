/**
@file repertoire.c
Fichier repertoire.c

Implementation de l'iterateur de mots, de la liste des fichiers d'un
repertoire ou d'un corpus et de la construction d'une chaine de Markov (cf. repertoire.h).
Les fonctions POSIX opendir / readdir / closedir, stat, lstat et fstat
servent au parcours des repertoires.
*/
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "contrat.h"
#include "repertoire.h"

/** Valeur renvoyee par lire_caractere a la fin du fichier (ou sur erreur). */
#define FIN_FICHIER (-1L)

/** Absence de caractere lu par anticipation. */
#define AUCUN_CARACTERE (-2L)

/** Capacite initiale des tampons du mot et de la graphie courants. */
#define CAPACITE_MOT_INITIALE ((size_t)64)

/** Capacite initiale de la liste des fichiers d'un repertoire. */
#define CAPACITE_LISTE_INITIALE ((size_t)16)

/** Nombre maximal d'octets rendus lors du decodage d'une sequence UTF-8. */
#define NB_OCTETS_RENDUS_MAX 3

/** Nombre d'octets examines au debut de chaque ligne pour reconnaitre les
    marques du projet Gutenberg. */
#define TAILLE_PREFIXE_LIGNE 96

/** Profondeur maximale du parcours recursif des repertoires. */
#define PROFONDEUR_MAX 64u

struct iterateur_mots_s {
  FILE *fichier;
  char *mot;                                /* mot courant (cle {a..z}) */
  size_t longueur;
  size_t capacite;
  char *graphie;                            /* graphie courante (UTF-8) */
  size_t longueur_graphie;
  size_t capacite_graphie;
  long caractere_suivant;                   /* lu par anticipation */
  int octets_rendus[NB_OCTETS_RENDUS_MAX];  /* pile d'octets a relire */
  int nb_octets_rendus;
  long position;                            /* position du prochain octet */
  long fin;                                 /* fin du texte utile, ou -1 */
  bool accentue;                            /* octets non ASCII presents */
  bool mot_disponible;                      /* dernier appel : un mot */
  bool commence;                            /* un mot a deja ete demande */
  bool phrase_par_ligne;                    /* fin de ligne = fin de phrase */
  liaison_t liaison;                        /* separateur apres le mot */
  unsigned int marques;                     /* marques devant le mot */
  unsigned int marques_en_attente;          /* marques lues apres le mot :
                                               celles du mot suivant */
  ponctuation_t ponctuation;                /* ponctuation finale du mot */
  bool termine;                             /* plus aucun mot a renvoyer */
  int erreur;                               /* 0 ou code errno */
};


/* ======================================================================== */
/*                Decodage des caracteres (UTF-8 / Latin-1)                 */
/* ======================================================================== */

/**
 * Translitteration des caracteres U+00C0 .. U+00FF (Latin-1) en lettres
 * minuscules non accentuees ; NULL pour les deux symboles non alphabetiques
 * de cette plage (multiplication et division).
 */
static const char *const LETTRES_LATIN1[64] = {
  /* C0 */ "a", "a", "a", "a", "a", "a", "ae", "c",
  /* C8 */ "e", "e", "e", "e", "i", "i", "i", "i",
  /* D0 */ "d", "n", "o", "o", "o", "o", "o", NULL,
  /* D8 */ "o", "u", "u", "u", "u", "y", "th", "ss",
  /* E0 */ "a", "a", "a", "a", "a", "a", "ae", "c",
  /* E8 */ "e", "e", "e", "e", "i", "i", "i", "i",
  /* F0 */ "d", "n", "o", "o", "o", "o", "o", NULL,
  /* F8 */ "o", "u", "u", "u", "u", "y", "th", "y"
};

/**
 * Lit un octet : d'abord dans la pile des octets rendus, sinon dans le
 * fichier, sans depasser la fin du texte utile.
 */
static int lire_octet(iterateur_mots_t *it)
{
  int octet;

  if (it->nb_octets_rendus > 0) {
    it->nb_octets_rendus--;
    octet = it->octets_rendus[it->nb_octets_rendus];
  } else if (it->fin >= 0 && it->position >= it->fin) {
    octet = EOF;                            /* licence Gutenberg ignoree */
  } else {
    octet = getc(it->fichier);
    if (octet == EOF) {
      if (ferror(it->fichier) && it->erreur == 0) {
        it->erreur = EIO;
      }
    } else {
      it->position++;
    }
  }
  return octet;
}

/** Rend un octet : il sera relu au prochain appel de lire_octet. */
static void rendre_octet(iterateur_mots_t *it, int octet)
{
  INVARIANT(it->nb_octets_rendus < NB_OCTETS_RENDUS_MAX);
  it->octets_rendus[it->nb_octets_rendus] = octet;
  it->nb_octets_rendus++;
}

/**
 * Lit le caractere suivant et renvoie son point de code Unicode, ou
 * FIN_FICHIER. Les sequences UTF-8 valides sont decodees ; un octet qui ne
 * commence pas une sequence valide est interprete comme un caractere
 * Latin-1 (ISO-8859-1), et les octets suivants sont relus normalement.
 */
static long lire_caractere(iterateur_mots_t *it)
{
  int octet;
  int suite[NB_OCTETS_RENDUS_MAX];
  int nb_suite;
  int k;
  long point_code;

  if (it->caractere_suivant != AUCUN_CARACTERE) {
    point_code = it->caractere_suivant;
    it->caractere_suivant = AUCUN_CARACTERE;
  } else {
    octet = lire_octet(it);
    if (octet == EOF) {
      point_code = FIN_FICHIER;
    } else if (octet < 0x80) {
      point_code = octet;                   /* ASCII */
    } else {
      if (octet >= 0xC2 && octet <= 0xDF) {
        nb_suite = 1;
        point_code = octet & 0x1F;
      } else if (octet >= 0xE0 && octet <= 0xEF) {
        nb_suite = 2;
        point_code = octet & 0x0F;
      } else if (octet >= 0xF0 && octet <= 0xF4) {
        nb_suite = 3;
        point_code = octet & 0x07;
      } else {
        nb_suite = 0;                       /* octet isole : Latin-1 */
        point_code = octet;
      }
      /* lecture des octets de continuation (de la forme 10xxxxxx) */
      k = 0;
      while (k < nb_suite) {
        suite[k] = lire_octet(it);
        if (suite[k] == EOF || (suite[k] & 0xC0) != 0x80) {
          break;
        }
        point_code = (point_code << 6) | (long)(suite[k] & 0x3F);
        k++;
      }
      if (k < nb_suite) {
        /* sequence invalide : on rend les octets lus (dans l'ordre inverse,
           la pile restituera d'abord le premier) et on interprete le
           premier octet en Latin-1 */
        if (suite[k] != EOF) {
          rendre_octet(it, suite[k]);
        }
        while (k > 0) {
          k--;
          rendre_octet(it, suite[k]);
        }
        point_code = octet;
      }
    }
  }
  return point_code;
}

/**
 * Ecrit dans lettres la translitteration en minuscules non accentuees d'un
 * caractere, et indique s'il s'agit d'une majuscule.
 * Renvoie le nombre de lettres ecrites (1 ou 2), ou 0 si le caractere n'est
 * pas une lettre (c'est alors un separateur).
 */
static size_t translitterer(long point_code, char lettres[2], bool *majuscule)
{
  const char *latin;
  size_t nb = 0u;

  *majuscule = false;
  if (point_code >= 'a' && point_code <= 'z') {
    lettres[0] = (char)point_code;
    nb = 1u;
  } else if (point_code >= 'A' && point_code <= 'Z') {
    lettres[0] = (char)(point_code - 'A' + 'a');
    *majuscule = true;
    nb = 1u;
  } else if (point_code >= 0xC0 && point_code <= 0xFF) {
    latin = LETTRES_LATIN1[point_code - 0xC0];
    if (latin != NULL) {
      lettres[0] = latin[0];
      nb = 1u;
      if (latin[1] != '\0') {
        lettres[1] = latin[1];
        nb = 2u;
      }
      *majuscule = (point_code <= 0xDE);
    }
  } else if (point_code == 0x152 || point_code == 0x153) {
    lettres[0] = 'o';                      /* OE / oe lies */
    lettres[1] = 'e';
    nb = 2u;
    *majuscule = (point_code == 0x152);
  } else if (point_code == 0x178) {
    lettres[0] = 'y';                      /* Y trema majuscule */
    nb = 1u;
    *majuscule = true;
  }
  return nb;
}

/** Ponctuation de fin de phrase : . ! ? et points de suspension (U+2026). */
static bool est_ponctuation_finale(long point_code)
{
  return point_code == '.' || point_code == '!' || point_code == '?' || point_code == 0x2026;
}

static bool est_chiffre(long point_code)
{
  return point_code >= '0' && point_code <= '9';
}

/** Blancs (hors saut de ligne) : ne rompent pas une ligne vide. */
static bool est_blanc(long point_code)
{
  return point_code == ' ' || point_code == '\t' || point_code == '\r'
         || point_code == '\v' || point_code == '\f'
         || point_code == 0xA0 || point_code == 0x202F   /* insecables */
         || point_code == 0xFEFF;                         /* BOM */
}

/** Apostrophes : droite, typographique, lettre modificative. */
static bool est_apostrophe(long point_code)
{
  return point_code == '\'' || point_code == 0x2019 || point_code == 0x2BC;
}

/** Traits d'union : ASCII, trait d'union et trait d'union insecable. */
static bool est_trait_union(long point_code)
{
  return point_code == '-' || point_code == 0x2010 || point_code == 0x2011;
}

/** Guillemets toujours ouvrants : francais, et allemand (en bas). */
static bool est_guillemet_ouvrant(long point_code)
{
  return point_code == 0xAB || point_code == 0x201E;
}

/** Guillemet toujours fermant : francais. */
static bool est_guillemet_fermant(long point_code)
{
  return point_code == 0xBB;
}

/**
 * Guillemets ouvrants ou fermants selon la langue et la position : droit
 * ('"') et anglais (U+201C, U+201D ; le premier ferme une citation en
 * allemand, le second en ouvre une en suedois).
 */
static bool est_guillemet_ambigu(long point_code)
{
  return point_code == '"' || point_code == 0x201C || point_code == 0x201D;
}

/** Tirets : cadratin, demi-cadratin et barre horizontale. */
static bool est_tiret(long point_code)
{
  return point_code == 0x2014 || point_code == 0x2013 || point_code == 0x2015;
}

/**
 * Indique si un guillemet ambigu suivi de ce caractere est fermant : il est
 * suivi d'un blanc, d'une fin de ligne ou de fichier, ou d'un signe de
 * ponctuation ; sinon il est colle a ce qu'il ouvre.
 */
static bool ferme_guillemet_ambigu(long suivant)
{
  return suivant == FIN_FICHIER || suivant == '\n' || est_blanc(suivant)
         || est_ponctuation_finale(suivant) || suivant == ','
         || suivant == ';' || suivant == ':' || suivant == ')'
         || suivant == ']' || est_guillemet_ambigu(suivant)
         || est_guillemet_fermant(suivant);
}

/** Marques de dialogue reconnues dans une suite de separateurs. */
typedef struct {
  unsigned int marques;     /* marques reconnues jusqu'ici */
  unsigned int nb_traits;   /* longueur (au plus 4) de la suite de '-' en cours */
  bool blanc_avant_traits;  /* cette suite de '-' suit un blanc ou un debut de ligne */
} separateurs_t;

/**
 * Prend en compte un separateur (caractere qui n'est pas une lettre),
 * connaissant le caractere qui le precede et celui qui le suit, et met a
 * jour les marques de dialogue (cf. GUILLEMETS ET TIRETS dans repertoire.h).
 */
static void observer_separateur(separateurs_t *s, long caractere, long precedent, long suivant)
{
  if (caractere == '-') {
    if (s->nb_traits == 0u) {
      s->blanc_avant_traits = (precedent == '\n' || est_blanc(precedent));
    }
    if (s->nb_traits < 4u) {
      s->nb_traits++;
    }
    if (suivant != '-') {
      /* fin de la suite de '-' : "--" et "---" sont des tirets, "-" aussi
         s'il n'est pas colle au mot qui le precede ; "----" est un trait */
      if (s->nb_traits == 2u || s->nb_traits == 3u
          || (s->nb_traits == 1u && s->blanc_avant_traits)) {
        s->marques |= MARQUE_TIRET;
      }
      s->nb_traits = 0u;
    }
  } else if (est_tiret(caractere)) {
    s->marques |= MARQUE_TIRET;
  } else if (est_guillemet_ouvrant(caractere)
             || (est_guillemet_ambigu(caractere)
                 && !ferme_guillemet_ambigu(suivant))) {
    s->marques |= MARQUE_OUVRE_GUILLEMET;
  } else if (est_guillemet_fermant(caractere) || est_guillemet_ambigu(caractere)) {
    s->marques |= MARQUE_FERME_GUILLEMET;
  }
}


/* ======================================================================== */
/*               Premier parcours : marques Gutenberg et accents            */
/* ======================================================================== */

/** Lettre ASCII en majuscule (les autres octets sont inchanges). */
static char majuscule_ascii(char c)
{
  return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/**
 * Si texte commence par motif (sans tenir compte de la casse ASCII),
 * renvoie la suite du texte ; sinon NULL.
 */
static const char *apres_motif(const char *texte, const char *motif)
{
  while (*motif != '\0' && majuscule_ascii(*texte) == *motif) {
    texte++;
    motif++;
  }
  return (*motif == '\0') ? texte : NULL;
}

/** Indique si texte contient motif (en majuscules ASCII), sans casse. */
static bool contient_motif(const char *texte, const char *motif)
{
  bool trouve = false;

  while (!trouve && *texte != '\0') {
    trouve = (apres_motif(texte, motif) != NULL);
    texte++;
  }
  return trouve;
}

/**
 * Indique si une ligne (son debut) est une marque "*** START OF ...
 * GUTENBERG" (debut = true) ou "*** END OF ... GUTENBERG" (debut = false).
 */
static bool est_marque_etoilee(const char *ligne, bool debut)
{
  const char *suite;
  bool marque = false;

  while (*ligne == ' ' || *ligne == '\t') {
    ligne++;
  }
  suite = apres_motif(ligne, "***");
  if (suite != NULL) {
    while (*suite == '*' || *suite == ' ') {
      suite++;
    }
    marque = apres_motif(suite, debut ? "START OF" : "END OF") != NULL
             && contient_motif(suite, "GUTENBERG");
  }
  return marque;
}

/** Ligne de fin du texte d'un livre du projet Gutenberg. */
static bool est_marque_fin(const char *ligne)
{
  const char *debut = ligne;

  while (*debut == ' ' || *debut == '\t') {
    debut++;
  }
  return est_marque_etoilee(ligne, false)
         || apres_motif(debut, "END OF THE PROJECT GUTENBERG") != NULL
         || apres_motif(debut, "END OF PROJECT GUTENBERG") != NULL;
}

/**
 * Premier parcours du fichier : reperer le debut et la fin du texte utile
 * (marques Gutenberg) et la presence d'octets non ASCII dans ce texte, puis
 * se replacer au debut du texte utile. Renvoie 0 ou un code errno.
 */
static int analyser_fichier(iterateur_mots_t *it)
{
  char prefixe[TAILLE_PREFIXE_LIGNE + 1];
  size_t nb_prefixe = 0u;
  long position = 0;
  long debut_ligne = 0;
  long debut_texte = 0;
  bool non_ascii = false;
  bool accentue = false;
  bool fini = false;
  int octet;
  int erreur = 0;

  while (!fini) {
    octet = getc(it->fichier);
    if (octet != EOF) {
      position++;
      if (octet >= 0x80) {
        non_ascii = true;
      }
      if (nb_prefixe < TAILLE_PREFIXE_LIGNE) {
        prefixe[nb_prefixe] = (char)octet;
        nb_prefixe++;
      }
    }
    if (octet == '\n' || octet == EOF) {
      prefixe[nb_prefixe] = '\0';
      if (est_marque_etoilee(prefixe, true)) {
        debut_texte = position;            /* l'en-tete est ignore */
        accentue = false;
      } else if (est_marque_fin(prefixe)) {
        it->fin = debut_ligne;             /* la licence est ignoree */
        fini = true;
      } else if (non_ascii) {
        accentue = true;
      }
      debut_ligne = position;
      nb_prefixe = 0u;
      non_ascii = false;
      if (octet == EOF) {
        fini = true;
      }
    }
  }
  if (ferror(it->fichier)) {
    erreur = EIO;
  } else {
    clearerr(it->fichier);
    if (fseek(it->fichier, debut_texte, SEEK_SET) != 0) {
      erreur = (errno != 0) ? errno : EIO;
    } else {
      it->position = debut_texte;
      it->accentue = accentue;
    }
  }
  return erreur;
}


/* ======================================================================== */
/*                       Iterateur des mots d'un fichier                    */
/* ======================================================================== */

/**
 * Ajoute nb octets a un tampon termine par '\0' (agrandi si besoin).
 * Renvoie false si la memoire manque (tampon inchange).
 */
static bool ajouter_octets(char **tampon, size_t *longueur, size_t *capacite,
                           const char *octets, size_t nb)
{
  size_t nouvelle_capacite;
  char *agrandi;
  bool succes = true;

  if (*longueur + nb + 1u > *capacite) {
    nouvelle_capacite = 2u * *capacite;
    agrandi = NULL;
    if (nouvelle_capacite > *capacite) {
      agrandi = realloc(*tampon, nouvelle_capacite);
    }
    if (agrandi == NULL) {
      succes = false;
    } else {
      *tampon = agrandi;
      *capacite = nouvelle_capacite;
    }
  }
  if (succes) {
    memcpy(*tampon + *longueur, octets, nb);
    *longueur += nb;
    (*tampon)[*longueur] = '\0';
  }
  return succes;
}

/**
 * Ajoute une lettre au mot courant : sa translitteration a la cle, son
 * encodage UTF-8 a la graphie. Renvoie false si la memoire manque.
 */
static bool ajouter_lettre(iterateur_mots_t *it, long point_code,
                           const char lettres[2], size_t nb_lettres)
{
  char utf8[2];
  size_t taille = 1u;

  if (point_code < 0x80) {
    utf8[0] = (char)point_code;
  } else {
    /* lettres reconnues : au plus U+0178, soit deux octets en UTF-8 */
    utf8[0] = (char)(0xC0 | (point_code >> 6));
    utf8[1] = (char)(0x80 | (point_code & 0x3F));
    taille = 2u;
  }
  return ajouter_octets(&it->mot, &it->longueur, &it->capacite, lettres, nb_lettres)
         && ajouter_octets(&it->graphie, &it->longueur_graphie,
                           &it->capacite_graphie, utf8, taille);
}

iterateur_mots_t *iterateur_mots_creer(const char *chemin_fichier)
{
  iterateur_mots_t *it;
  struct stat informations;
  int erreur = 0;

  PRECONDITION(chemin_fichier != NULL);

  it = malloc(sizeof(iterateur_mots_t));
  if (it == NULL) {
    erreur = ENOMEM;
  } else {
    it->fichier = NULL;
    it->mot = malloc(CAPACITE_MOT_INITIALE);
    it->graphie = malloc(CAPACITE_MOT_INITIALE);
    if (it->mot == NULL || it->graphie == NULL) {
      erreur = ENOMEM;
    } else {
      it->fichier = fopen(chemin_fichier, "rb");
      if (it->fichier == NULL) {
        erreur = errno;
      } else if (fstat(fileno(it->fichier), &informations) != 0) {
        erreur = errno;
      } else if (S_ISDIR(informations.st_mode)) {
        /* fopen accepte un repertoire, mais on ne peut pas le lire */
        erreur = EISDIR;
      } else {
        it->fin = -1;
        it->accentue = false;
        erreur = analyser_fichier(it);
      }
    }
    if (erreur != 0) {
      if (it->fichier != NULL) {
        fclose(it->fichier);
      }
      free(it->mot);
      free(it->graphie);
      free(it);
      it = NULL;
    } else {
      it->mot[0] = '\0';
      it->longueur = 0u;
      it->capacite = CAPACITE_MOT_INITIALE;
      it->graphie[0] = '\0';
      it->longueur_graphie = 0u;
      it->capacite_graphie = CAPACITE_MOT_INITIALE;
      it->caractere_suivant = AUCUN_CARACTERE;
      it->nb_octets_rendus = 0;
      it->mot_disponible = false;
      it->commence = false;
      it->phrase_par_ligne = false;
      it->liaison = LIAISON_ESPACE;
      it->marques = MARQUE_AUCUNE;
      it->marques_en_attente = MARQUE_AUCUNE;
      it->ponctuation = PONCTUATION_AUCUNE;
      it->termine = false;
      it->erreur = 0;
    }
  }
  if (erreur != 0) {
    errno = erreur;
  }
  return it;
}

void iterateur_mots_detruire(iterateur_mots_t **iterateur)
{
  PRECONDITION(iterateur != NULL);
  if (*iterateur != NULL) {
    fclose((*iterateur)->fichier);
    free((*iterateur)->mot);
    free((*iterateur)->graphie);
    free(*iterateur);
    *iterateur = NULL;
  }
  POSTCONDITION(*iterateur == NULL);
}

void iterateur_mots_phrase_par_ligne(iterateur_mots_t *iterateur, bool actif)
{
  PRECONDITION(iterateur != NULL);
  PRECONDITION(!iterateur->commence);
  iterateur->phrase_par_ligne = actif;
}

int iterateur_mots_erreur(const iterateur_mots_t *iterateur)
{
  PRECONDITION(iterateur != NULL);
  return iterateur->erreur;
}

const char *iterateur_mots_graphie(const iterateur_mots_t *iterateur)
{
  PRECONDITION(iterateur != NULL);
  PRECONDITION(iterateur->mot_disponible);
  return iterateur->graphie;
}

liaison_t iterateur_mots_liaison(const iterateur_mots_t *iterateur)
{
  PRECONDITION(iterateur != NULL);
  PRECONDITION(iterateur->mot_disponible);
  return iterateur->liaison;
}

unsigned int iterateur_mots_marques(const iterateur_mots_t *iterateur)
{
  PRECONDITION(iterateur != NULL);
  PRECONDITION(iterateur->mot_disponible);
  POSTCONDITION(iterateur->marques <= MARQUES_TOUTES);
  return iterateur->marques;
}

ponctuation_t iterateur_mots_ponctuation(const iterateur_mots_t *iterateur)
{
  PRECONDITION(iterateur != NULL);
  PRECONDITION(iterateur->mot_disponible);
  return iterateur->ponctuation;
}

bool iterateur_mots_accentue(const iterateur_mots_t *iterateur)
{
  PRECONDITION(iterateur != NULL);
  return iterateur->accentue;
}

const char *iterateur_mots_suivant(iterateur_mots_t *iterateur, bool *fin_phrase)
{
  iterateur_mots_t *it = iterateur;
  const char *resultat = NULL;
  bool fin = false;
  char lettres[2];
  size_t nb_lettres = 0u;
  bool majuscule = false;
  bool initiale_majuscule = false;
  bool tout_en_majuscules;
  bool abreviation;
  bool premier_separateur;
  bool abreviation_vue = false;
  bool virgule = false;
  bool point_virgule = false;
  bool deux_points = false;
  bool exclamation = false;
  bool interrogation = false;
  bool suspension = false;
  unsigned int nb_points = 0u;
  unsigned int nb_sauts_ligne;
  unsigned int nb_sauts_fin;
  unsigned int nb_separateurs = 0u;
  separateurs_t avant = { MARQUE_AUCUNE, 0u, false };
  separateurs_t apres = { MARQUE_AUCUNE, 0u, false };
  long premier = AUCUN_CARACTERE;
  long caractere = FIN_FICHIER;
  long point_code;
  long suivant;
  long precedent;

  PRECONDITION(iterateur != NULL);

  it->mot_disponible = false;
  it->commence = true;
  if (!it->termine) {
    /* 1. Recherche du debut du mot (au debut du fichier : sinon le
          caractere suivant est deja une lettre, lue par anticipation lors
          de l'appel precedent). Le debut du fichier est un debut de ligne. */
    avant.marques = it->marques_en_attente;
    precedent = '\n';
    caractere = lire_caractere(it);
    if (caractere != FIN_FICHIER) {
      nb_lettres = translitterer(caractere, lettres, &initiale_majuscule);
    }
    while (caractere != FIN_FICHIER && nb_lettres == 0u) {
      suivant = lire_caractere(it);
      observer_separateur(&avant, caractere, precedent, suivant);
      precedent = caractere;
      caractere = suivant;
      if (caractere != FIN_FICHIER) {
        nb_lettres = translitterer(caractere, lettres, &initiale_majuscule);
      }
    }

    if (caractere == FIN_FICHIER) {
      it->termine = true;
    } else {
      /* 2. Lecture des lettres du mot (cle et graphie) */
      it->longueur = 0u;
      it->longueur_graphie = 0u;
      tout_en_majuscules = initiale_majuscule;
      do {
        if (!ajouter_lettre(it, caractere, lettres, nb_lettres)) {
          it->erreur = ENOMEM;
        }
        point_code = lire_caractere(it);
        caractere = point_code;
        nb_lettres = 0u;
        if (caractere != FIN_FICHIER) {
          nb_lettres = translitterer(caractere, lettres, &majuscule);
          if (nb_lettres > 0u && !majuscule) {
            tout_en_majuscules = false;
          }
        }
      } while (it->erreur == 0 && nb_lettres > 0u);

      /* 3. Analyse des separateurs qui suivent le mot, jusqu'au debut du
            mot suivant (lu par anticipation) ou jusqu'a la fin du fichier */
      abreviation = (it->longueur == 1u && initiale_majuscule)
                    || (tout_en_majuscules && strcmp(it->mot, "mm") == 0);
      premier_separateur = true;
      nb_sauts_ligne = 0u;
      nb_sauts_fin = it->phrase_par_ligne ? 1u : 2u;
      precedent = AUCUN_CARACTERE;
      while (it->erreur == 0 && caractere != FIN_FICHIER && nb_lettres == 0u) {
        suivant = lire_caractere(it);
        observer_separateur(&apres, caractere, precedent, suivant);
        if (nb_separateurs < 2u) {
          nb_separateurs++;                 /* 2 signifie "plusieurs" */
        }
        if (nb_separateurs == 1u) {
          premier = caractere;
        }
        if (est_ponctuation_finale(caractere)) {
          if (caractere == '.' && premier_separateur && abreviation) {
            abreviation_vue = true;         /* "M. Dupont" */
          } else if (caractere == '.' && est_chiffre(precedent)
                     && est_chiffre(suivant)) {
            /* nombre decimal "3.5" : pas une fin de phrase */
          } else {
            fin = true;
            if (caractere == '!') {
              exclamation = true;
            } else if (caractere == '?') {
              interrogation = true;
            } else if (caractere == 0x2026) {
              suspension = true;
            } else {
              nb_points++;
            }
          }
          nb_sauts_ligne = 0u;
        } else if (caractere == '\n') {
          if (nb_sauts_ligne < 2u) {
            nb_sauts_ligne++;
          }
          if (nb_sauts_ligne >= nb_sauts_fin) {
            fin = true;                     /* ligne vide (ou fin de ligne
                                               en lecture par ligne) */
          }
        } else if (!est_blanc(caractere)) {
          nb_sauts_ligne = 0u;
          virgule = virgule || caractere == ',';
          point_virgule = point_virgule || caractere == ';';
          deux_points = deux_points || caractere == ':';
        }
        precedent = caractere;
        premier_separateur = false;
        caractere = suivant;
        nb_lettres = 0u;
        if (caractere != FIN_FICHIER) {
          nb_lettres = translitterer(caractere, lettres, &majuscule);
        }
      }

      if (it->erreur != 0) {
        it->termine = true;
        fin = false;
      } else {
        if (caractere == FIN_FICHIER) {
          fin = true;                       /* dernier mot du fichier */
          it->termine = true;
        } else {
          it->caractere_suivant = caractere;
        }
        /* 4. Classement des separateurs : ponctuation finale ou liaison */
        it->liaison = LIAISON_ESPACE;
        it->ponctuation = PONCTUATION_AUCUNE;
        if (fin) {
          if (interrogation) {
            it->ponctuation = PONCTUATION_INTERROGATION;
          } else if (exclamation) {
            it->ponctuation = PONCTUATION_EXCLAMATION;
          } else if (suspension || nb_points >= 2u) {
            it->ponctuation = PONCTUATION_SUSPENSION;
          } else if (nb_points == 1u) {
            it->ponctuation = PONCTUATION_POINT;
          }
        } else if (nb_separateurs == 1u && est_apostrophe(premier)) {
          it->liaison = LIAISON_APOSTROPHE;
        } else if (nb_separateurs == 1u && est_trait_union(premier)) {
          it->liaison = LIAISON_TRAIT_UNION;
        } else if (abreviation_vue) {
          it->liaison = LIAISON_ABREVIATION;
        } else if (point_virgule) {
          it->liaison = LIAISON_POINT_VIRGULE;
        } else if (deux_points) {
          it->liaison = LIAISON_DEUX_POINTS;
        } else if (virgule) {
          it->liaison = LIAISON_VIRGULE;
        }
        /* marques : celles lues devant ce mot ; celles lues apres lui attendent le mot suivant */
        it->marques = avant.marques;
        it->marques_en_attente = apres.marques;
        it->mot_disponible = true;
        resultat = it->mot;
      }
    }
  }
  if (fin_phrase != NULL) {
    *fin_phrase = fin;
  }
  POSTCONDITION(resultat == NULL
                || (resultat[0] != '\0' && hachage_mot_valide(resultat)
                    && repertoire_graphie_valide(it->graphie, resultat)));
  return resultat;
}

bool repertoire_graphie_valide(const char *graphie, const char *cle)
{
  const unsigned char *octets = (const unsigned char *)graphie;
  const char *c = cle;
  char lettres[2];
  size_t nb;
  size_t i;
  long point_code;
  bool majuscule;
  bool valide;

  PRECONDITION(graphie != NULL);
  PRECONDITION(cle != NULL);

  valide = lexique_graphie_valide(graphie);
  while (valide && *octets != '\0') {
    /* graphie valide : lettre ASCII (1 octet) ou lettre de 2 octets */
    if (*octets < 0x80u) {
      point_code = *octets;
      octets++;
    } else {
      point_code = ((long)(octets[0] & 0x1Fu) << 6) | (long)(octets[1] & 0x3Fu);
      octets += 2;
    }
    nb = translitterer(point_code, lettres, &majuscule);
    for (i = 0u; valide && i < nb; i++) {
      valide = (*c == lettres[i]);
      c++;
    }
  }
  return valide && *c == '\0';
}

char *repertoire_cle(const char *graphie)
{
  const unsigned char *octets = (const unsigned char *)graphie;
  char *cle = NULL;
  char lettres[2];
  size_t longueur = 0u;
  size_t nb;
  size_t i;
  long point_code;
  bool majuscule;

  PRECONDITION(graphie != NULL);

  if (!lexique_graphie_valide(graphie)) {
    errno = EINVAL;
  } else {
    /* chaque lettre (1 ou 2 octets) donne au plus 2 lettres de la cle */
    cle = malloc((2u * strlen(graphie)) + 1u);
    if (cle == NULL) {
      errno = ENOMEM;
    } else {
      while (*octets != '\0') {
        if (*octets < 0x80u) {
          point_code = *octets;
          octets++;
        } else {
          point_code = ((long)(octets[0] & 0x1Fu) << 6) | (long)(octets[1] & 0x3Fu);
          octets += 2;
        }
        nb = translitterer(point_code, lettres, &majuscule);
        for (i = 0u; i < nb; i++) {
          cle[longueur] = lettres[i];
          longueur++;
        }
      }
      cle[longueur] = '\0';
    }
  }
  POSTCONDITION(cle == NULL || (cle[0] != '\0' && hachage_mot_valide(cle)
                                && repertoire_graphie_valide(graphie, cle)));
  return cle;
}


/* ======================================================================== */
/*                         Fichiers d'un repertoire                         */
/* ======================================================================== */

/** Liste dynamique de chemins de fichiers. */
typedef struct {
  char **chemins;
  size_t nb;
  size_t capacite;
} liste_fichiers_t;

/** Ajoute un chemin (dont la liste prend la propriete) a la liste. */
static bool ajouter_a_liste(liste_fichiers_t *liste, char *chemin)
{
  size_t nouvelle_capacite;
  char **chemins;
  bool succes = true;

  if (liste->nb == liste->capacite) {
    nouvelle_capacite = (liste->capacite == 0u) ? CAPACITE_LISTE_INITIALE : 2u * liste->capacite;
    chemins = NULL;
    if (nouvelle_capacite <= SIZE_MAX / sizeof(char *)) {
      chemins = realloc(liste->chemins, nouvelle_capacite * sizeof(char *));
    }
    if (chemins == NULL) {
      succes = false;
    } else {
      liste->chemins = chemins;
      liste->capacite = nouvelle_capacite;
    }
  }
  if (succes) {
    liste->chemins[liste->nb] = chemin;
    liste->nb++;
  }
  return succes;
}

/** Comparaison de deux chemins (qsort) : ordre des octets, independant de la langue du systeme. */
static int comparer_chemins(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

/** Concatene un repertoire et un nom de fichier (resultat a liberer). */
static char *joindre_chemin(const char *repertoire, const char *nom)
{
  size_t longueur_repertoire = strlen(repertoire);
  size_t longueur_nom = strlen(nom);
  bool separateur = (longueur_repertoire > 0u && repertoire[longueur_repertoire - 1u] != '/');
  char *chemin = malloc(longueur_repertoire + (separateur ? 1u : 0u) + longueur_nom + 1u);

  if (chemin != NULL) {
    memcpy(chemin, repertoire, longueur_repertoire);
    if (separateur) {
      chemin[longueur_repertoire] = '/';
      longueur_repertoire++;
    }
    memcpy(chemin + longueur_repertoire, nom, longueur_nom + 1u);
  }
  return chemin;
}

/**
 * Ajoute a la liste les fichiers du repertoire (et de ses sous-repertoires
 * si recursif). Renvoie 0 ou un code errno.
 */
static int lister(const char *chemin_repertoire, bool recursif,
                  unsigned int profondeur, liste_fichiers_t *liste)
{
  DIR *repertoire;
  struct dirent *entree;
  struct stat informations;
  char *chemin;
  bool fini = false;
  int erreur = 0;

  repertoire = opendir(chemin_repertoire);
  if (repertoire == NULL) {
    erreur = errno;
  }
  while (erreur == 0 && !fini) {
    errno = 0;
    entree = readdir(repertoire);
    if (entree == NULL) {
      fini = true;
      erreur = errno;
    } else if (entree->d_name[0] != '.') {  /* ., .. et fichiers caches */
      chemin = joindre_chemin(chemin_repertoire, entree->d_name);
      if (chemin == NULL) {
        erreur = ENOMEM;
      } else if (lstat(chemin, &informations) != 0) {
        if (errno != ENOENT) {              /* entree supprimee : ignoree */
          erreur = errno;
        }
        free(chemin);
      } else if (S_ISDIR(informations.st_mode)) {
        /* vrai sous-repertoire (un lien vers un repertoire n'est pas suivi,
           ce qui exclut les boucles) */
        if (recursif) {
          if (profondeur >= PROFONDEUR_MAX) {
            erreur = ELOOP;
          } else {
            erreur = lister(chemin, true, profondeur + 1u, liste);
          }
        }
        free(chemin);
      } else if (stat(chemin, &informations) != 0) {
        if (errno != ENOENT) {              /* lien casse : ignore */
          erreur = errno;
        }
        free(chemin);
      } else if (!S_ISREG(informations.st_mode)) {
        free(chemin);                       /* tube, peripherique, ... */
      } else if (!ajouter_a_liste(liste, chemin)) {
        free(chemin);
        erreur = ENOMEM;
      }
    }
  }
  if (repertoire != NULL) {
    closedir(repertoire);
  }
  return erreur;
}

char **repertoire_lister_fichiers(const char *chemin_repertoire, bool recursif, size_t *nb_fichiers)
{
  liste_fichiers_t liste = { NULL, 0u, 0u };
  int erreur;
  size_t i;

  PRECONDITION(chemin_repertoire != NULL);
  PRECONDITION(nb_fichiers != NULL);

  erreur = lister(chemin_repertoire, recursif, 0u, &liste);
  if (erreur == 0 && liste.chemins == NULL) {
    /* repertoire vide : tableau vide mais non NULL */
    liste.chemins = malloc(sizeof(char *));
    if (liste.chemins == NULL) {
      erreur = ENOMEM;
    }
  }
  if (erreur == 0) {
    if (liste.nb > 1u) {
      qsort(liste.chemins, liste.nb, sizeof(char *), comparer_chemins);
    }
    *nb_fichiers = liste.nb;
  } else {
    for (i = 0u; i < liste.nb; i++) {
      free(liste.chemins[i]);
    }
    free(liste.chemins);
    liste.chemins = NULL;
    errno = erreur;
  }
  return liste.chemins;
}

/** Copie d'une chaine (a liberer avec free), ou NULL si la memoire manque. */
static char *copier_chaine(const char *chaine)
{
  size_t taille = strlen(chaine) + 1u;
  char *copie = malloc(taille);

  if (copie != NULL) {
    memcpy(copie, chaine, taille);
  }
  return copie;
}

/**
 * Ajoute a la liste les fichiers designes par un chemin du corpus : le
 * fichier lui-meme, ou les fichiers du repertoire, tries. Renvoie 0 ou un
 * code errno.
 */
static int lister_chemin(const char *chemin, bool recursif, liste_fichiers_t *liste)
{
  struct stat informations;
  size_t debut = liste->nb;
  char *copie;
  int erreur = 0;

  if (stat(chemin, &informations) != 0) {
    erreur = errno;
  } else if (S_ISDIR(informations.st_mode)) {
    erreur = lister(chemin, recursif, 0u, liste);
    if (erreur == 0 && liste->nb - debut > 1u) {
      qsort(liste->chemins + debut, liste->nb - debut, sizeof(char *), comparer_chemins);
    }
  } else if (!S_ISREG(informations.st_mode)) {
    erreur = EINVAL;                        /* tube, peripherique, ... */
  } else {
    copie = copier_chaine(chemin);
    if (copie == NULL || !ajouter_a_liste(liste, copie)) {
      free(copie);
      erreur = ENOMEM;
    }
  }
  return erreur;
}

char **repertoire_lister_corpus(const char *const *chemins, size_t nb_chemins,
                                bool recursif, size_t *nb_fichiers)
{
  liste_fichiers_t liste = { NULL, 0u, 0u };
  int erreur = 0;
  size_t i;

  PRECONDITION(chemins != NULL);
  PRECONDITION(nb_chemins >= 1u);
  PRECONDITION(nb_fichiers != NULL);

  for (i = 0u; erreur == 0 && i < nb_chemins; i++) {
    PRECONDITION(chemins[i] != NULL);
    erreur = lister_chemin(chemins[i], recursif, &liste);
  }
  if (erreur == 0 && liste.chemins == NULL) {
    /* corpus sans fichier : tableau vide mais non NULL */
    liste.chemins = malloc(sizeof(char *));
    if (liste.chemins == NULL) {
      erreur = ENOMEM;
    }
  }
  if (erreur == 0) {
    *nb_fichiers = liste.nb;
  } else {
    for (i = 0u; i < liste.nb; i++) {
      free(liste.chemins[i]);
    }
    free(liste.chemins);
    liste.chemins = NULL;
    errno = erreur;
  }
  return liste.chemins;
}

void repertoire_liberer_fichiers(char ***fichiers, size_t nb_fichiers)
{
  size_t i;

  PRECONDITION(fichiers != NULL);
  if (*fichiers != NULL) {
    for (i = 0u; i < nb_fichiers; i++) {
      free((*fichiers)[i]);
    }
    free(*fichiers);
    *fichiers = NULL;
  }
  POSTCONDITION(*fichiers == NULL);
}


/* ======================================================================== */
/*                     Chaine de Markov d'un repertoire                     */
/* ======================================================================== */

/** Structures remplies pendant l'apprentissage. */
typedef struct {
  hachage_t *mots;
  markov_t *chaine;
  const repertoire_options_t *options;
  langue_detecteur_t *detecteur;  /* NULL sans filtre de langue */
  size_t phrase;                  /* numero, dans le corpus, de la phrase en cours de lecture */
  repertoire_bilan_t bilan;
} apprentissage_t;

/** Etat de lecture d'une phrase. */
typedef struct {
  unsigned int mot;        /* mot precedent (0 en debut de phrase) */
  unsigned int etat;       /* etat precedent de la chaine */
  liaison_t liaison;       /* separateur apres le mot precedent */
  unsigned int fenetre[REPERTOIRE_ORDRE_MAX];  /* derniers mots de la phrase (ordre >= 3) */
} phrase_t;

/** Replace l'etat de lecture au debut d'une phrase. */
static void debuter_phrase(phrase_t *phrase)
{
  unsigned int r;

  phrase->mot = REPERTOIRE_DEBUT_PHRASE;
  phrase->etat = REPERTOIRE_DEBUT_PHRASE;
  phrase->liaison = LIAISON_ESPACE;
  for (r = 0u; r < REPERTOIRE_ORDRE_MAX; r++) {
    phrase->fenetre[r] = REPERTOIRE_DEBUT_PHRASE;
  }
}

/**
 * Cree, dans les structures synchronisees avec la table des mots, l'entree
 * du nouveau mot d'identifiant mot. Renvoie false en cas d'echec (errno).
 */
static bool ajouter_nouveau_mot(apprentissage_t *a, unsigned int mot)
{
  const repertoire_options_t *o = a->options;
  bool succes = true;

  if (o->ordre == 1u) {
    succes = (markov_ajouter_etat(a->chaine) == mot);
  }
  if (succes && o->lexique != NULL) {
    succes = (lexique_ajouter_mot(o->lexique) == mot);
  }
  if (succes && o->ponctuation != NULL) {
    succes = (markov_ajouter_etat(o->ponctuation) == REPERTOIRE_NB_FINS + mot);
  }
  return succes;
}

/**
 * Etat de la chaine a l'ordre k >= 3 apres la lecture du mot identifiant :
 * le k-uplet des k derniers mots (la fenetre est decalee d'un mot). Un
 * nouvel etat est ajoute a la chaine. Renvoie HACHAGE_ABSENT en cas d'echec.
 */
static unsigned int etat_nuplet(apprentissage_t *a, phrase_t *phrase, unsigned int identifiant)
{
  const repertoire_options_t *o = a->options;
  unsigned int nb_nuplets = hachage_nuplets_nb(o->nuplets);
  unsigned int etat;
  unsigned int r;

  for (r = 0u; r + 1u < o->ordre; r++) {
    phrase->fenetre[r] = phrase->fenetre[r + 1u];
  }
  phrase->fenetre[o->ordre - 1u] = identifiant;
  etat = hachage_nuplets_ajouter(o->nuplets, phrase->fenetre);
  if (etat == nb_nuplets && markov_ajouter_etat(a->chaine) != etat) {
    etat = HACHAGE_ABSENT;
  }
  return etat;
}

/**
 * Prend en compte une occurrence de mot : table des mots, bigrammes,
 * lexique, transitions de la chaine et ponctuation finale.
 * Renvoie false en cas d'echec (errno indique la cause).
 */
static bool traiter_occurrence(apprentissage_t *a,
                               const iterateur_mots_t *it, const char *mot,
                               bool fin_phrase, phrase_t *phrase)
{
  const repertoire_options_t *o = a->options;
  unsigned int nb_mots = hachage_nb_mots(a->mots);
  unsigned int nb_bigrammes;
  unsigned int identifiant;
  unsigned int bigramme = HACHAGE_ABSENT;
  unsigned int etat;
  ponctuation_t ponctuation;
  bool succes;

  /* (a) le mot est ajoute a la table s'il n'y est pas ; un nouveau mot
         recoit l'identifiant nb_mots, aussi attribue dans les structures
         synchronisees (bijection mots <-> etats a l'ordre 1) */
  identifiant = hachage_ajouter(a->mots, mot);
  succes = (identifiant != HACHAGE_ABSENT);
  if (succes && identifiant == nb_mots) {
    succes = ajouter_nouveau_mot(a, identifiant);
  }
  etat = identifiant;
  /* bigramme (mot precedent, mot) : etat de la chaine a l'ordre 2 */
  if (succes && o->bigrammes != NULL) {
    nb_bigrammes = hachage_paires_nb(o->bigrammes);
    bigramme = hachage_paires_ajouter(o->bigrammes, phrase->mot, identifiant);
    succes = (bigramme != HACHAGE_ABSENT);
    if (succes && bigramme == nb_bigrammes) {
      if (o->lexique != NULL) {
        succes = (lexique_ajouter_bigramme(o->lexique, identifiant)
                  == bigramme);
      }
      if (succes && o->ordre == 2u) {
        succes = (markov_ajouter_etat(a->chaine) == bigramme);
      }
    }
    if (o->ordre == 2u) {
      etat = bigramme;
    }
  }
  /* k-uplet des k derniers mots : etat de la chaine a l'ordre k >= 3 */
  if (succes && o->ordre >= 3u) {
    etat = etat_nuplet(a, phrase, identifiant);
    succes = (etat != HACHAGE_ABSENT);
  }
  /* graphie, casse, liaison et marques de dialogue de cette occurrence */
  if (succes && o->lexique != NULL) {
    succes = lexique_observer(o->lexique, bigramme,
                              iterateur_mots_graphie(it),
                              iterateur_mots_accentue(it),
                              phrase->mot == REPERTOIRE_DEBUT_PHRASE,
                              phrase->liaison, iterateur_mots_marques(it));
  }
  /* (b) poids(etat precedent -> etat) + 1, le debut de phrase etant 0 */
  if (succes) {
    succes = markov_augmenter_poids(a->chaine, phrase->etat, etat, 1u);
  }
  if (succes && fin_phrase) {
    /* fin de phrase : poids(etat -> 0) + 1 et ponctuation finale */
    succes = markov_augmenter_poids(a->chaine, etat, REPERTOIRE_DEBUT_PHRASE, 1u);
    if (succes && o->ponctuation != NULL) {
      ponctuation = iterateur_mots_ponctuation(it);
      if (ponctuation == PONCTUATION_AUCUNE && !o->phrase_par_ligne) {
        ponctuation = PONCTUATION_POINT;
      }
      succes = markov_augmenter_poids(o->ponctuation,
                                      REPERTOIRE_NB_FINS + identifiant,
                                      (unsigned int)ponctuation, 1u);
    }
    debuter_phrase(phrase);
  } else if (succes) {
    phrase->mot = identifiant;
    phrase->etat = etat;
    phrase->liaison = iterateur_mots_liaison(it);
  }
  return succes;
}

/** Ouvre un fichier du corpus avec le mode de lecture des options. */
static iterateur_mots_t *ouvrir_fichier(const apprentissage_t *a, const char *chemin)
{
  iterateur_mots_t *it = iterateur_mots_creer(chemin);

  if (it != NULL) {
    iterateur_mots_phrase_par_ligne(it, a->options->phrase_par_ligne);
  }
  return it;
}

/**
 * Indique si la phrase en cours de lecture doit etre apprise : toujours
 * sans filtre de langue ; sinon selon le detecteur (une phrase qu'il n'a
 * pas vue, le fichier ayant change entre les deux lectures, est gardee).
 */
static bool phrase_a_apprendre(const apprentissage_t *a)
{
  return a->detecteur == NULL
         || a->phrase >= langue_detecteur_nb_phrases(a->detecteur)
         || langue_detecteur_phrase_gardee(a->detecteur, a->phrase);
}

/**
 * Lit tous les mots d'un fichier et met a jour les structures. Renvoie
 * false en cas d'erreur (errno indique la cause).
 */
static bool apprendre_fichier(apprentissage_t *a, const char *chemin)
{
  iterateur_mots_t *it;
  const char *mot;
  phrase_t phrase;
  bool fin_phrase = false;
  bool debut_phrase = true;
  bool garder = true;
  bool succes;
  int erreur = 0;

  debuter_phrase(&phrase);
  it = ouvrir_fichier(a, chemin);
  succes = (it != NULL);
  if (!succes) {
    erreur = errno;
  }
  while (succes && (mot = iterateur_mots_suivant(it, &fin_phrase)) != NULL) {
    if (debut_phrase) {
      garder = phrase_a_apprendre(a);
      debut_phrase = false;
    }
    a->bilan.nb_mots++;
    if (garder) {
      succes = traiter_occurrence(a, it, mot, fin_phrase, &phrase);
      if (!succes) {
        erreur = errno;
      }
    } else {
      a->bilan.nb_mots_ecartes++;
    }
    if (fin_phrase) {
      a->bilan.nb_phrases++;
      if (!garder) {
        a->bilan.nb_phrases_ecartees++;
      }
      a->phrase++;
      debut_phrase = true;
    }
  }
  if (succes && iterateur_mots_erreur(it) != 0) {
    erreur = iterateur_mots_erreur(it);
    succes = false;
  }
  iterateur_mots_detruire(&it);
  if (!succes) {
    errno = erreur;
  }
  return succes;
}

/**
 * Premiere lecture d'un fichier, pour le filtre de langue : donne ses mots
 * au detecteur. Renvoie false en cas d'erreur (errno indique la cause).
 */
static bool observer_fichier(apprentissage_t *a, const char *chemin)
{
  iterateur_mots_t *it;
  const char *mot;
  bool fin_phrase = false;
  bool succes;
  int erreur = 0;

  it = ouvrir_fichier(a, chemin);
  succes = (it != NULL);
  if (!succes) {
    erreur = errno;
  }
  while (succes && (mot = iterateur_mots_suivant(it, &fin_phrase)) != NULL) {
    succes = langue_detecteur_ajouter_mot(a->detecteur, mot, fin_phrase);
    if (!succes) {
      erreur = errno;
    }
  }
  if (succes && iterateur_mots_erreur(it) != 0) {
    erreur = iterateur_mots_erreur(it);
    succes = false;
  }
  iterateur_mots_detruire(&it);
  if (succes) {
    langue_detecteur_fin_texte(a->detecteur);
  } else {
    errno = erreur;
  }
  return succes;
}

/**
 * Filtre de langue : cree le detecteur, lui donne tous les fichiers et
 * conclut. Renvoie false en cas d'erreur (errno indique la cause).
 */
static bool detecter_langues(apprentissage_t *a, char *const *fichiers, size_t nb_fichiers)
{
  size_t i;
  bool succes;

  a->detecteur = langue_detecteur_creer();
  succes = (a->detecteur != NULL);
  for (i = 0u; succes && i < nb_fichiers; i++) {
    succes = observer_fichier(a, fichiers[i]);
  }
  return succes && langue_detecteur_conclure(a->detecteur);
}

/**
 * Cree les entrees initiales : mot 0 = "", etat 0, paire (0, 0), n-uplet
 * (0, ..., 0), etats de ponctuation. Renvoie false si la memoire manque.
 */
static bool initialiser(apprentissage_t *a)
{
  const repertoire_options_t *o = a->options;
  const unsigned int zeros[REPERTOIRE_ORDRE_MAX] = { 0u };
  unsigned int p;
  bool succes;

  succes = hachage_ajouter(a->mots, "") == REPERTOIRE_DEBUT_PHRASE
           && markov_ajouter_etat(a->chaine) == REPERTOIRE_DEBUT_PHRASE;
  if (succes && o->lexique != NULL) {
    succes = (lexique_ajouter_mot(o->lexique) == REPERTOIRE_DEBUT_PHRASE);
  }
  for (p = 0u; succes && o->ponctuation != NULL && p <= REPERTOIRE_NB_FINS; p++) {
    /* etats 0 .. NB_FINS - 1 : fins de phrase ; etat NB_FINS : mot 0 */
    succes = (markov_ajouter_etat(o->ponctuation) == p);
  }
  if (succes && o->bigrammes != NULL) {
    succes = (hachage_paires_ajouter(o->bigrammes, REPERTOIRE_DEBUT_PHRASE, REPERTOIRE_DEBUT_PHRASE) == 0u);
    if (succes && o->lexique != NULL) {
      succes = (lexique_ajouter_bigramme(o->lexique, REPERTOIRE_DEBUT_PHRASE) == 0u);
    }
  }
  if (succes && o->nuplets != NULL) {
    succes = (hachage_nuplets_ajouter(o->nuplets, zeros) == 0u);
  }
  return succes;
}

/** Nombre d'etats attendu de la chaine apprise, selon l'ordre. */
static unsigned int nb_etats_attendu(const apprentissage_t *a)
{
  const repertoire_options_t *o = a->options;
  unsigned int nb;

  if (o->ordre == 1u) {
    nb = hachage_nb_mots(a->mots);
  } else if (o->ordre == 2u) {
    nb = hachage_paires_nb(o->bigrammes);
  } else {
    nb = hachage_nuplets_nb(o->nuplets);
  }
  return nb;
}

markov_t *repertoire_apprendre_corpus(const char *const *chemins,
                                      size_t nb_chemins, unsigned int graine,
                                      hachage_t *mots,
                                      const repertoire_options_t *options)
{
  const repertoire_bilan_t bilan_vide = { 0u, 0u, 0u, 0u, 0u, LANGUE_INCONNUE };
  apprentissage_t a;
  char **fichiers;
  size_t nb_fichiers = 0u;
  size_t i;
  bool succes;
  int erreur = 0;

  PRECONDITION(chemins != NULL);
  PRECONDITION(nb_chemins >= 1u);
  PRECONDITION(mots != NULL);
  PRECONDITION(hachage_nb_mots(mots) == 0u);
  PRECONDITION(options != NULL);
  PRECONDITION(options->ordre >= 1u && options->ordre <= REPERTOIRE_ORDRE_MAX);
  PRECONDITION(options->bigrammes != NULL || (options->ordre != 2u && options->lexique == NULL));
  PRECONDITION(options->bigrammes == NULL || hachage_paires_nb(options->bigrammes) == 0u);
  PRECONDITION((options->nuplets != NULL) == (options->ordre >= 3u));
  PRECONDITION(options->nuplets == NULL
               || (hachage_nuplets_taille(options->nuplets) == options->ordre
                   && hachage_nuplets_nb(options->nuplets) == 0u));
  PRECONDITION(options->lexique == NULL
               || (lexique_nb_mots(options->lexique) == 0u
                   && lexique_nb_bigrammes(options->lexique) == 0u
                   && !lexique_est_finalise(options->lexique)));
  PRECONDITION(options->ponctuation == NULL
               || (markov_nb_etats(options->ponctuation) == 0u
                   && !markov_est_finalisee(options->ponctuation)));

  a.mots = mots;
  a.chaine = NULL;
  a.options = options;
  a.detecteur = NULL;
  a.phrase = 0u;
  a.bilan = bilan_vide;
  /* 1. Liste des fichiers (en premier : si un chemin n'existe pas, les
        structures fournies ne sont pas modifiees) */
  fichiers = repertoire_lister_corpus(chemins, nb_chemins, options->recursif, &nb_fichiers);
  succes = (fichiers != NULL);
  /* 2. Filtre de langue : premiere lecture du corpus */
  if (succes && options->filtrer_langues) {
    succes = detecter_langues(&a, fichiers, nb_fichiers);
  }
  if (!succes) {
    erreur = errno;
  }
  /* 3. Chaine vide et etat 0 = mot vide = debut de phrase */
  if (succes) {
    a.chaine = markov_creer(graine);
    succes = (a.chaine != NULL && initialiser(&a));
    if (!succes) {
      erreur = ENOMEM;
    }
  }
  /* 4. Apprentissage, fichier par fichier */
  for (i = 0u; succes && i < nb_fichiers; i++) {
    succes = apprendre_fichier(&a, fichiers[i]);
    if (!succes) {
      erreur = errno;
    }
  }
  repertoire_liberer_fichiers(&fichiers, nb_fichiers);
  /* 5. Finalisation : les poids deviennent des probabilites */
  if (succes) {
    markov_finaliser(a.chaine);
    if (options->lexique != NULL) {
      lexique_finaliser(options->lexique);
    }
    if (options->ponctuation != NULL) {
      markov_finaliser(options->ponctuation);
    }
    a.bilan.nb_fichiers = nb_fichiers;
    if (a.detecteur != NULL) {
      a.bilan.langue = langue_detecteur_majoritaire(a.detecteur);
    }
    if (options->bilan != NULL) {
      *options->bilan = a.bilan;
    }
    POSTCONDITION(markov_nb_etats(a.chaine) == nb_etats_attendu(&a));
    POSTCONDITION(hachage_mot(mots, REPERTOIRE_DEBUT_PHRASE)[0] == '\0');
    POSTCONDITION(options->ponctuation == NULL
                  || markov_nb_etats(options->ponctuation)
                     == REPERTOIRE_NB_FINS + hachage_nb_mots(mots));
    POSTCONDITION(a.bilan.nb_phrases_ecartees <= a.bilan.nb_phrases
                  && a.bilan.nb_mots_ecartes <= a.bilan.nb_mots);
  } else {
    markov_detruire(&a.chaine);
  }
  langue_detecteur_detruire(&a.detecteur);
  if (!succes) {
    errno = erreur;
  }
  return a.chaine;
}

markov_t *repertoire_apprendre(const char *chemin_repertoire,
                               unsigned int graine, hachage_t *mots,
                               const repertoire_options_t *options)
{
  PRECONDITION(chemin_repertoire != NULL);
  return repertoire_apprendre_corpus(&chemin_repertoire, 1u, graine, mots, options);
}

markov_t *repertoire_creer_markov(const char *chemin_repertoire, unsigned int graine, hachage_t *mots)
{
  const repertoire_options_t options = { 1u, false, NULL, NULL, NULL, NULL, false, false, NULL };

  PRECONDITION(chemin_repertoire != NULL);
  PRECONDITION(mots != NULL);
  PRECONDITION(hachage_nb_mots(mots) == 0u);
  return repertoire_apprendre(chemin_repertoire, graine, mots, &options);
}
