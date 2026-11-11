/**
@file langue.c
Fichier langue.c

Implementation de la detection de langue (cf. langue.h).

Representation du detecteur, pendant les ajouts :
  - mots : table de hachage des mots distincts (identifiants 0, 1, ...) ;
  - profils[id] : masque des langues dont le profil contient le mot id ;
  - occurrences[i] : identifiant du i-eme mot ajoute ;
  - passages[p] : premiere phrase, premiere occurrence et score dans chaque
    langue du passage p ; le passage en cours (ouvert) n'y figure pas
    encore, ses compteurs sont dans la structure.
Apres la conclusion, il ne reste que gardees[phrase] (un octet par phrase)
et les totaux : la table des mots et les occurrences sont liberees.

Invariants (pendant les ajouts) :
  - capacite_passages > nb_passages des que le passage ouvert contient un
    mot : le fermer ne demande jamais d'allocation ;
  - les passages se suivent : le passage p couvre les phrases
    [passages[p].premiere_phrase, passages[p + 1].premiere_phrase[ et les
    occurrences correspondantes ; le passage ouvert couvre la suite.
*/
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "contrat.h"
#include "hachage.h"
#include "langue.h"

/** Capacite initiale des tableaux dynamiques. */
#define CAPACITE_INITIALE ((size_t)64)

/** Un passage de n mots a une langue si son score atteint n / DIVISEUR. */
#define DIVISEUR_SCORE 5u

/** Ecart minimal de score pour qu'une langue en domine une autre. */
#define ECART_MINIMAL 2u

/** La langue majoritaire doit couvrir 1 / DIVISEUR_MAJORITE des mots. */
#define DIVISEUR_MAJORITE 4u

/** Un passage est garde si NUMERATEUR / DENOMINATEUR de ses mots sont connus. */
#define NUMERATEUR_CONNUS 7u
#define DENOMINATEUR_CONNUS 10u

/** Bit d'une langue dans un masque. */
#define BIT(langue) (1u << (unsigned int)(langue))

#define FR BIT(LANGUE_FRANCAIS)
#define EN BIT(LANGUE_ANGLAIS)
#define ES BIT(LANGUE_ESPAGNOL)
#define DE BIT(LANGUE_ALLEMAND)
#define IT BIT(LANGUE_ITALIEN)
#define PT BIT(LANGUE_PORTUGAIS)
#define NL BIT(LANGUE_NEERLANDAIS)

/** Mot-outil et langues dont il fait partie du profil. */
typedef struct {
  const char *mot;
  unsigned char langues;
} mot_outil_t;

/**
 * Profils des langues : mots-outils les plus frequents de chaque langue,
 * sans accents, tries dans l'ordre de strcmp (recherche dichotomique).
 * Les lettres seules sont les mots elides ("l'", "d'", "qu'", "'s", "'t").
 */
static const mot_outil_t MOTS_OUTILS[] = {
  { "a", FR | EN | ES | IT | PT },
  { "aan", NL },
  { "aber", DE },
  { "about", EN },
  { "after", EN },
  { "ai", FR | IT },
  { "ainsi", FR },
  { "al", ES | IT | NL },
  { "all", EN | IT },
  { "alla", IT },
  { "alle", DE | IT },
  { "allo", IT },
  { "alors", FR },
  { "als", DE | NL },
  { "am", DE },
  { "an", EN | DE },
  { "anche", IT },
  { "ancora", IT },
  { "and", EN },
  { "ante", ES | PT },
  { "any", EN },
  { "ao", PT },
  { "aos", PT },
  { "apres", FR },
  { "are", EN },
  { "as", FR | EN | PT },
  { "at", EN },
  { "ate", PT },
  { "au", FR },
  { "auch", DE },
  { "auf", DE },
  { "aus", DE },
  { "aussi", FR },
  { "aux", FR },
  { "avait", FR },
  { "avec", FR },
  { "avoir", FR },
  { "be", EN },
  { "been", EN },
  { "bei", DE },
  { "bien", FR },
  { "bij", NL },
  { "bis", DE },
  { "but", EN },
  { "by", EN },
  { "c", FR | IT },
  { "ca", FR },
  { "can", EN },
  { "car", FR },
  { "ce", FR },
  { "cela", FR },
  { "ces", FR },
  { "cet", FR },
  { "cette", FR },
  { "che", IT },
  { "chez", FR },
  { "chi", IT },
  { "ci", IT },
  { "com", PT },
  { "come", IT },
  { "comme", FR },
  { "como", ES | PT },
  { "con", ES | IT },
  { "contra", ES | PT },
  { "cosi", IT },
  { "could", EN },
  { "cual", ES },
  { "cuando", ES },
  { "cui", IT },
  { "d", FR | EN | IT },
  { "da", DE | IT | PT },
  { "daar", NL },
  { "dal", IT },
  { "dalla", IT },
  { "dan", NL },
  { "dans", FR },
  { "das", DE | PT },
  { "dass", DE },
  { "dat", NL },
  { "de", FR | ES | PT | NL },
  { "degli", IT },
  { "dei", IT },
  { "del", ES | IT },
  { "dell", IT },
  { "della", IT },
  { "delle", IT },
  { "dello", IT },
  { "dem", DE },
  { "den", DE | NL },
  { "depois", PT },
  { "der", DE | NL },
  { "des", FR | DE },
  { "desde", ES | PT },
  { "deux", FR },
  { "deze", NL },
  { "di", IT },
  { "did", EN },
  { "die", DE | NL },
  { "dit", NL },
  { "do", EN | PT },
  { "doch", DE },
  { "donc", FR },
  { "donde", ES },
  { "dont", FR },
  { "door", NL },
  { "dos", PT },
  { "dove", IT },
  { "du", FR | DE },
  { "durante", ES | IT | PT },
  { "durch", DE },
  { "e", ES | IT | PT },
  { "ed", IT },
  { "een", NL },
  { "ein", DE },
  { "eine", DE },
  { "einem", DE },
  { "einen", DE },
  { "einer", DE },
  { "eines", DE },
  { "el", ES },
  { "ela", PT },
  { "elas", PT },
  { "ele", PT },
  { "eles", PT },
  { "ella", ES },
  { "elle", FR },
  { "elles", FR },
  { "ellos", ES },
  { "em", PT },
  { "en", FR | ES | NL },
  { "encore", FR },
  { "entre", FR | ES | PT },
  { "er", DE | NL },
  { "era", ES | IT | PT },
  { "es", FR | ES | DE },
  { "esa", ES },
  { "ese", ES },
  { "eso", ES },
  { "essa", PT },
  { "esse", PT },
  { "essere", IT },
  { "est", FR },
  { "esta", ES | PT },
  { "estas", ES },
  { "este", ES | PT },
  { "esto", ES },
  { "estos", ES },
  { "et", FR },
  { "etait", FR },
  { "ete", FR },
  { "etre", FR },
  { "eu", PT },
  { "fait", FR },
  { "foi", PT },
  { "for", EN },
  { "from", EN },
  { "fue", ES },
  { "fur", DE },
  { "geen", NL },
  { "gia", IT },
  { "gli", IT },
  { "ha", ES | IT | PT },
  { "haar", NL },
  { "haben", DE },
  { "habia", ES },
  { "had", EN | NL },
  { "han", ES },
  { "hanno", IT },
  { "has", EN },
  { "hasta", ES },
  { "hat", DE },
  { "have", EN },
  { "hay", ES },
  { "he", EN },
  { "hebben", NL },
  { "heeft", NL },
  { "hem", NL },
  { "her", EN },
  { "het", NL },
  { "hier", DE | NL },
  { "hij", NL },
  { "him", EN },
  { "his", EN },
  { "ho", IT },
  { "hun", NL },
  { "i", EN | IT },
  { "ich", DE },
  { "if", EN },
  { "ihm", DE },
  { "ihn", DE },
  { "ihr", DE },
  { "ihre", DE },
  { "ik", NL },
  { "il", FR | IT },
  { "ils", FR },
  { "im", DE },
  { "in", EN | DE | IT | NL },
  { "into", EN },
  { "io", IT },
  { "is", EN | NL },
  { "isso", PT },
  { "ist", DE },
  { "isto", PT },
  { "it", EN },
  { "its", EN },
  { "j", FR },
  { "ja", PT },
  { "je", FR | NL },
  { "kan", NL },
  { "kann", DE },
  { "l", FR | IT },
  { "la", FR | ES | IT },
  { "las", ES },
  { "le", FR | ES | IT },
  { "lei", IT },
  { "les", FR | ES },
  { "leur", FR },
  { "leurs", FR },
  { "lhe", PT },
  { "ll", EN },
  { "lo", ES | IT },
  { "loro", IT },
  { "los", ES },
  { "lui", FR | IT },
  { "m", FR | EN },
  { "ma", FR | IT },
  { "maar", NL },
  { "mais", FR | PT },
  { "man", DE },
  { "mas", ES | PT },
  { "me", FR | EN | ES | PT },
  { "meer", NL },
  { "mehr", DE },
  { "meme", FR },
  { "mes", FR },
  { "mesmo", PT },
  { "met", NL },
  { "meu", PT },
  { "mi", ES | IT },
  { "mich", DE },
  { "mij", NL },
  { "mijn", NL },
  { "minha", PT },
  { "mir", DE },
  { "mit", DE },
  { "moet", NL },
  { "moi", FR },
  { "molto", IT },
  { "mon", FR },
  { "more", EN },
  { "mucho", ES },
  { "muito", PT },
  { "muy", ES },
  { "my", EN },
  { "n", FR },
  { "na", PT },
  { "naar", NL },
  { "nach", DE },
  { "nada", ES | PT },
  { "nao", PT },
  { "nas", PT },
  { "ne", FR | IT },
  { "nei", IT },
  { "nel", IT },
  { "nell", IT },
  { "nella", IT },
  { "nelle", IT },
  { "ni", FR | ES },
  { "nicht", DE },
  { "niet", NL },
  { "no", EN | ES | IT | PT },
  { "noch", DE },
  { "nog", NL },
  { "noi", IT },
  { "non", FR | IT },
  { "nos", FR | ES | PT },
  { "not", EN },
  { "notre", FR },
  { "nous", FR },
  { "nu", NL },
  { "num", PT },
  { "numa", PT },
  { "nur", DE },
  { "o", ES | IT | PT },
  { "oder", DE },
  { "of", EN | NL },
  { "om", NL },
  { "on", FR | EN },
  { "onde", PT },
  { "one", EN },
  { "only", EN },
  { "ont", FR },
  { "onze", NL },
  { "ook", NL },
  { "op", NL },
  { "or", FR | EN },
  { "os", PT },
  { "other", EN },
  { "otra", ES },
  { "otro", ES },
  { "otros", ES },
  { "ou", FR | PT },
  { "oui", FR },
  { "our", EN },
  { "out", EN },
  { "over", NL },
  { "par", FR },
  { "para", ES | PT },
  { "pas", FR },
  { "pela", PT },
  { "pelo", PT },
  { "per", IT },
  { "perche", IT },
  { "pero", ES },
  { "peu", FR },
  { "piu", IT },
  { "plus", FR },
  { "poco", ES | IT },
  { "poi", IT },
  { "por", ES | PT },
  { "porque", ES | PT },
  { "pour", FR },
  { "puis", FR },
  { "qu", FR },
  { "quand", FR },
  { "quando", IT | PT },
  { "que", FR | ES | PT },
  { "quella", IT },
  { "quello", IT },
  { "quem", PT },
  { "questa", IT },
  { "questo", IT },
  { "qui", FR },
  { "quien", ES },
  { "re", EN },
  { "rien", FR },
  { "s", FR | EN },
  { "sa", FR },
  { "said", EN },
  { "sans", FR },
  { "sao", PT },
  { "schon", DE },
  { "se", FR | ES | IT | PT },
  { "sei", DE },
  { "sein", DE },
  { "seine", DE },
  { "sem", PT },
  { "senza", IT },
  { "ser", ES | PT },
  { "ses", FR },
  { "seu", PT },
  { "seus", PT },
  { "she", EN },
  { "si", FR | ES | IT },
  { "sich", DE },
  { "sie", DE },
  { "sin", ES },
  { "sind", DE },
  { "so", EN | DE | PT },
  { "sobre", ES | PT },
  { "soi", FR },
  { "some", EN },
  { "son", FR | ES },
  { "sono", IT },
  { "sont", FR },
  { "sous", FR },
  { "su", ES | IT },
  { "sua", IT | PT },
  { "suas", PT },
  { "such", EN },
  { "sue", IT },
  { "sull", IT },
  { "suo", IT },
  { "suoi", IT },
  { "sur", FR },
  { "sus", ES },
  { "t", FR | EN },
  { "tambem", PT },
  { "tambien", ES },
  { "tanto", ES | IT | PT },
  { "te", FR | ES | PT | NL },
  { "tem", PT },
  { "ter", PT },
  { "than", EN },
  { "that", EN },
  { "the", EN },
  { "their", EN },
  { "them", EN },
  { "then", EN },
  { "there", EN },
  { "these", EN },
  { "they", EN },
  { "this", EN },
  { "ti", IT },
  { "tinha", PT },
  { "to", EN },
  { "todo", ES | PT },
  { "todos", ES | PT },
  { "toen", NL },
  { "toi", FR },
  { "tot", NL },
  { "toujours", FR },
  { "tous", FR },
  { "tout", FR },
  { "toute", FR },
  { "toutes", FR },
  { "tres", FR },
  { "tu", FR | ES | IT | PT },
  { "tutti", IT },
  { "tutto", IT },
  { "u", NL },
  { "uber", DE },
  { "uit", NL },
  { "um", DE | PT },
  { "uma", PT },
  { "un", FR | ES | IT },
  { "una", ES | IT },
  { "unas", ES },
  { "und", DE },
  { "une", FR },
  { "uno", ES | IT },
  { "unos", ES },
  { "uns", PT },
  { "up", EN },
  { "upon", EN },
  { "us", EN },
  { "van", NL },
  { "ve", EN },
  { "veel", NL },
  { "vers", FR },
  { "very", EN },
  { "vi", IT },
  { "voi", IT },
  { "von", DE },
  { "voor", NL },
  { "vor", DE },
  { "votre", FR },
  { "vous", FR },
  { "waar", NL },
  { "war", DE },
  { "was", EN | DE | NL },
  { "wat", NL },
  { "we", EN | NL },
  { "wel", NL },
  { "wenn", DE },
  { "werd", NL },
  { "werden", DE },
  { "were", EN },
  { "what", EN },
  { "when", EN },
  { "which", EN },
  { "who", EN },
  { "wie", DE | NL },
  { "wieder", DE },
  { "wij", NL },
  { "will", EN },
  { "wir", DE },
  { "wird", DE },
  { "with", EN },
  { "wo", DE },
  { "worden", NL },
  { "wordt", NL },
  { "would", EN },
  { "wurde", DE },
  { "y", FR | ES },
  { "ya", ES },
  { "yo", ES },
  { "you", EN },
  { "your", EN },
  { "ze", NL },
  { "zich", NL },
  { "zij", NL },
  { "zijn", NL },
  { "zo", NL },
  { "zou", NL },
  { "zu", DE },
  { "zum", DE },
  { "zur", DE },
};

/** Nombre de mots-outils. */
#define NB_MOTS_OUTILS (sizeof(MOTS_OUTILS) / sizeof(MOTS_OUTILS[0]))

/** Nom de chaque langue (cf. langue_t). */
static const char *const NOMS_LANGUES[NB_LANGUES] = {
  "inconnue", "francais", "anglais", "espagnol", "allemand", "italien",
  "portugais", "neerlandais"
};

/** Passage : suite de phrases consecutives d'un meme texte. */
typedef struct {
  size_t premiere_phrase;
  size_t premiere_occurrence;
  unsigned int scores[NB_LANGUES];
} passage_t;

struct langue_detecteur_s {
  hachage_t *mots;               /* NULL apres la conclusion */
  unsigned char *profils;        /* par identifiant de mot */
  size_t capacite_profils;
  unsigned int *occurrences;     /* identifiant de chaque mot ajoute */
  size_t nb_occurrences;
  size_t capacite_occurrences;
  passage_t *passages;           /* passages fermes */
  size_t nb_passages;
  size_t capacite_passages;
  passage_t ouvert;              /* passage en cours */
  size_t nb_phrases;
  size_t premier_passage_texte;  /* premier passage du texte en cours */
  bool phrase_en_cours;          /* le dernier mot ne finissait pas sa phrase */
  /* resultats (apres la conclusion) */
  bool conclu;
  langue_t majoritaire;
  unsigned char *gardees;        /* par phrase : 1 si gardee */
  size_t nb_phrases_ecartees;
  size_t nb_mots_ecartes;
};


/* ======================================================================== */
/*                           Profils de langues                             */
/* ======================================================================== */

const char *langue_nom(langue_t langue)
{
  PRECONDITION((unsigned int)langue < (unsigned int)NB_LANGUES);
  return NOMS_LANGUES[langue];
}

unsigned int langue_profils(const char *mot)
{
  size_t gauche = 0u;
  size_t droite = NB_MOTS_OUTILS;
  size_t milieu;
  unsigned int masque = 0u;
  int comparaison;

  PRECONDITION(mot != NULL);
  PRECONDITION(hachage_mot_valide(mot));

  /* recherche dichotomique dans [gauche, droite[ */
  while (masque == 0u && gauche < droite) {
    milieu = gauche + ((droite - gauche) / 2u);
    comparaison = strcmp(mot, MOTS_OUTILS[milieu].mot);
    if (comparaison == 0) {
      masque = MOTS_OUTILS[milieu].langues;
    } else if (comparaison < 0) {
      droite = milieu;
    } else {
      gauche = milieu + 1u;
    }
  }
  POSTCONDITION((masque & BIT(LANGUE_INCONNUE)) == 0u);
  return masque;
}


/* ======================================================================== */
/*                        Detecteur : creation, ajouts                      */
/* ======================================================================== */

/**
 * Garantit qu'un tableau dynamique peut recevoir un element de plus que
 * nb (doublement de la capacite). Renvoie false si la memoire manque (le
 * tableau est alors inchange).
 */
static bool reserver(void **tableau, size_t nb, size_t *capacite, size_t taille_element)
{
  size_t nouvelle_capacite;
  void *agrandi = NULL;
  bool succes = true;

  if (nb >= *capacite) {
    nouvelle_capacite = (*capacite == 0u) ? CAPACITE_INITIALE : 2u * *capacite;
    if (nouvelle_capacite > *capacite
        && nouvelle_capacite <= SIZE_MAX / taille_element) {
      agrandi = realloc(*tableau, nouvelle_capacite * taille_element);
    }
    if (agrandi == NULL) {
      succes = false;
    } else {
      *tableau = agrandi;
      *capacite = nouvelle_capacite;
    }
  }
  return succes;
}

/** Commence un passage vide a la position courante. */
static void ouvrir_passage(langue_detecteur_t *d)
{
  unsigned int langue;

  d->ouvert.premiere_phrase = d->nb_phrases;
  d->ouvert.premiere_occurrence = d->nb_occurrences;
  for (langue = 0u; langue < (unsigned int)NB_LANGUES; langue++) {
    d->ouvert.scores[langue] = 0u;
  }
}

/** Nombre de mots du passage ouvert. */
static size_t taille_passage_ouvert(const langue_detecteur_t *d)
{
  return d->nb_occurrences - d->ouvert.premiere_occurrence;
}

/** Ferme le passage ouvert (non vide) : il devient un passage a part. */
static void fermer_passage(langue_detecteur_t *d)
{
  INVARIANT(d->nb_passages < d->capacite_passages);
  d->passages[d->nb_passages] = d->ouvert;
  d->nb_passages++;
  ouvrir_passage(d);
}

langue_detecteur_t *langue_detecteur_creer(void)
{
  langue_detecteur_t *d = malloc(sizeof(langue_detecteur_t));

  if (d != NULL) {
    d->mots = hachage_creer(0u);
    d->profils = NULL;
    d->capacite_profils = 0u;
    d->occurrences = NULL;
    d->nb_occurrences = 0u;
    d->capacite_occurrences = 0u;
    d->passages = NULL;
    d->nb_passages = 0u;
    d->capacite_passages = 0u;
    d->nb_phrases = 0u;
    d->premier_passage_texte = 0u;
    d->phrase_en_cours = false;
    d->conclu = false;
    d->majoritaire = LANGUE_INCONNUE;
    d->gardees = NULL;
    d->nb_phrases_ecartees = 0u;
    d->nb_mots_ecartes = 0u;
    ouvrir_passage(d);
    if (d->mots == NULL) {
      free(d);
      d = NULL;
    }
  }
  if (d == NULL) {
    errno = ENOMEM;
  }
  POSTCONDITION(d == NULL || (!d->conclu && d->nb_phrases == 0u
                              && d->nb_occurrences == 0u));
  return d;
}

void langue_detecteur_detruire(langue_detecteur_t **d)
{
  PRECONDITION(d != NULL);
  if (*d != NULL) {
    hachage_detruire(&(*d)->mots);
    free((*d)->profils);
    free((*d)->occurrences);
    free((*d)->passages);
    free((*d)->gardees);
    free(*d);
    *d = NULL;
  }
  POSTCONDITION(*d == NULL);
}

bool langue_detecteur_ajouter_mot(langue_detecteur_t *d, const char *mot, bool fin_phrase)
{
  void *tableau;
  unsigned int identifiant = HACHAGE_ABSENT;
  unsigned int nb_mots;
  unsigned int langue;
  bool succes;

  PRECONDITION(d != NULL);
  PRECONDITION(!d->conclu);
  PRECONDITION(mot != NULL);
  PRECONDITION(mot[0] != '\0' && hachage_mot_valide(mot));

  /* 1. toutes les reservations d'abord : un echec ne modifie ni le nombre
        de mots ni les passages */
  tableau = d->occurrences;
  succes = reserver(&tableau, d->nb_occurrences, &d->capacite_occurrences, sizeof(unsigned int));
  d->occurrences = tableau;
  if (succes) {
    tableau = d->passages;
    succes = reserver(&tableau, d->nb_passages, &d->capacite_passages, sizeof(passage_t));
    d->passages = tableau;
  }
  if (succes) {
    nb_mots = hachage_nb_mots(d->mots);
    tableau = d->profils;
    succes = reserver(&tableau, nb_mots, &d->capacite_profils, sizeof(unsigned char));
    d->profils = tableau;
    if (succes) {
      identifiant = hachage_ajouter(d->mots, mot);
      succes = (identifiant != HACHAGE_ABSENT);
    }
    if (succes && identifiant == nb_mots) {
      d->profils[identifiant] = (unsigned char)langue_profils(mot);
    }
  }
  /* 2. prise en compte du mot (plus aucune allocation) */
  if (succes) {
    d->occurrences[d->nb_occurrences] = identifiant;
    d->nb_occurrences++;
    for (langue = 1u; langue < (unsigned int)NB_LANGUES; langue++) {
      if ((d->profils[identifiant] & BIT(langue)) != 0u) {
        d->ouvert.scores[langue]++;
      }
    }
    d->phrase_en_cours = !fin_phrase;
    if (fin_phrase) {
      d->nb_phrases++;
      if (taille_passage_ouvert(d) >= LANGUE_TAILLE_PASSAGE) {
        fermer_passage(d);
      }
    }
  } else {
    errno = ENOMEM;
  }
  INVARIANT(taille_passage_ouvert(d) == 0u || d->capacite_passages > d->nb_passages);
  return succes;
}

void langue_detecteur_fin_texte(langue_detecteur_t *d)
{
  passage_t *dernier;
  unsigned int langue;

  PRECONDITION(d != NULL);
  PRECONDITION(!d->conclu);

  if (d->phrase_en_cours) {
    d->nb_phrases++;
    d->phrase_en_cours = false;
  }
  if (taille_passage_ouvert(d) > 0u) {
    if (taille_passage_ouvert(d) < LANGUE_TAILLE_PASSAGE / 2u
        && d->nb_passages > d->premier_passage_texte) {
      /* fin de texte trop courte pour etre jugee seule : elle rejoint le
         passage precedent du meme texte */
      dernier = &d->passages[d->nb_passages - 1u];
      for (langue = 0u; langue < (unsigned int)NB_LANGUES; langue++) {
        dernier->scores[langue] += d->ouvert.scores[langue];
      }
      ouvrir_passage(d);
    } else {
      fermer_passage(d);
    }
  }
  d->premier_passage_texte = d->nb_passages;
}


/* ======================================================================== */
/*                               Conclusion                                 */
/* ======================================================================== */

/** Nombre de mots du passage ferme p. */
static size_t taille_passage(const langue_detecteur_t *d, size_t p)
{
  size_t fin = (p + 1u < d->nb_passages) ? d->passages[p + 1u].premiere_occurrence : d->nb_occurrences;

  return fin - d->passages[p].premiere_occurrence;
}

/** Nombre de phrases du passage ferme p. */
static size_t phrases_passage(const langue_detecteur_t *d, size_t p)
{
  size_t fin = (p + 1u < d->nb_passages) ? d->passages[p + 1u].premiere_phrase : d->nb_phrases;

  return fin - d->passages[p].premiere_phrase;
}

/** Langue majoritaire : plus grande somme de scores, si elle couvre une
    part suffisante des mots (regle 2). */
static langue_t langue_majoritaire(const langue_detecteur_t *d)
{
  size_t totaux[NB_LANGUES] = { 0u };
  size_t p;
  unsigned int langue;
  unsigned int meilleure = (unsigned int)LANGUE_INCONNUE;

  for (p = 0u; p < d->nb_passages; p++) {
    for (langue = 1u; langue < (unsigned int)NB_LANGUES; langue++) {
      totaux[langue] += d->passages[p].scores[langue];
    }
  }
  for (langue = 1u; langue < (unsigned int)NB_LANGUES; langue++) {
    if (totaux[langue] > totaux[meilleure]) {
      meilleure = langue;
    }
  }
  /* totaux[LANGUE_INCONNUE] == 0 : un corpus sans mot-outil est inconnu */
  if (totaux[meilleure] < (d->nb_occurrences + DIVISEUR_MAJORITE - 1u) / DIVISEUR_MAJORITE) {
    meilleure = (unsigned int)LANGUE_INCONNUE;
  }
  return (langue_t)meilleure;
}

/** Indique si une autre langue domine la langue m dans le passage p
    (regle 3). */
static bool est_etranger(const langue_detecteur_t *d, size_t p, langue_t m)
{
  const unsigned int *scores = d->passages[p].scores;
  size_t n = taille_passage(d, p);
  unsigned int langue;
  bool etranger = false;

  for (langue = 1u; !etranger && langue < (unsigned int)NB_LANGUES;
       langue++) {
    etranger = (langue != (unsigned int)m
                && (size_t)scores[langue] * DIVISEUR_SCORE >= n
                && scores[langue] > scores[m]
                && scores[langue] - scores[m] >= ECART_MINIMAL);
  }
  return etranger;
}

bool langue_detecteur_conclure(langue_detecteur_t *d)
{
  unsigned int nb_mots;
  unsigned int *total = NULL;      /* occurrences de chaque mot */
  unsigned int *reference = NULL;  /* ... dans les passages de reference */
  unsigned char *gardees;
  langue_t m;
  size_t p;
  size_t i;
  size_t debut;
  size_t n;
  size_t connus;
  unsigned int mot;
  bool garde;
  bool succes;

  PRECONDITION(d != NULL);
  PRECONDITION(!d->conclu);

  langue_detecteur_fin_texte(d);
  nb_mots = hachage_nb_mots(d->mots);
  /* toutes les allocations d'abord (calloc(0) peut renvoyer NULL) */
  gardees = malloc((d->nb_phrases > 0u) ? d->nb_phrases : 1u);
  total = calloc((size_t)nb_mots + 1u, sizeof(unsigned int));
  reference = calloc((size_t)nb_mots + 1u, sizeof(unsigned int));
  succes = (gardees != NULL && total != NULL && reference != NULL);
  if (succes) {
    memset(gardees, 1, (d->nb_phrases > 0u) ? d->nb_phrases : 1u);
    m = langue_majoritaire(d);
    if (m != LANGUE_INCONNUE) {
      /* regles 4 et 5 : occurrences de chaque mot, au total et dans les
         passages de reference */
      for (p = 0u; p < d->nb_passages; p++) {
        debut = d->passages[p].premiere_occurrence;
        n = taille_passage(d, p);
        garde = !est_etranger(d, p, m) && (size_t)d->passages[p].scores[m] * DIVISEUR_SCORE >= n;
        for (i = debut; i < debut + n; i++) {
          mot = d->occurrences[i];
          /* compteurs satures : reference[mot] <= total[mot] reste vrai */
          if (total[mot] < UINT_MAX) {
            total[mot]++;
            if (garde) {
              reference[mot]++;
            }
          }
        }
      }
      /* regle 6 : sort de chaque passage */
      for (p = 0u; p < d->nb_passages; p++) {
        debut = d->passages[p].premiere_occurrence;
        n = taille_passage(d, p);
        garde = !est_etranger(d, p, m);
        if (garde) {
          connus = 0u;
          for (i = debut; i < debut + n; i++) {
            mot = d->occurrences[i];
            /* total - reference <= reference : sans depassement */
            if (reference[mot] > 0u
                && total[mot] - reference[mot] <= reference[mot]) {
              connus++;
            }
          }
          garde = (connus * DENOMINATEUR_CONNUS >= n * NUMERATEUR_CONNUS);
        }
        if (!garde) {
          memset(gardees + d->passages[p].premiere_phrase, 0,
                 phrases_passage(d, p));
          d->nb_phrases_ecartees += phrases_passage(d, p);
          d->nb_mots_ecartes += n;
        }
      }
    }
    d->majoritaire = m;
    d->gardees = gardees;
    d->conclu = true;
    /* les mots ne servent plus : seuls restent les resultats */
    hachage_detruire(&d->mots);
    free(d->profils);
    free(d->occurrences);
    free(d->passages);
    d->profils = NULL;
    d->occurrences = NULL;
    d->passages = NULL;
    d->nb_passages = 0u;
  } else {
    free(gardees);
    errno = ENOMEM;
  }
  free(total);
  free(reference);
  POSTCONDITION(d->conclu == succes);
  return succes;
}


/* ======================================================================== */
/*                         Detecteur : consultation                         */
/* ======================================================================== */

bool langue_detecteur_est_conclu(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  return d->conclu;
}

size_t langue_detecteur_nb_phrases(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  return d->nb_phrases;
}

size_t langue_detecteur_nb_mots(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  return d->nb_occurrences;
}

langue_t langue_detecteur_majoritaire(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  return d->majoritaire;
}

bool langue_detecteur_phrase_gardee(const langue_detecteur_t *d, size_t phrase)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  PRECONDITION(phrase < d->nb_phrases);
  return d->gardees[phrase] != 0u;
}

size_t langue_detecteur_nb_phrases_ecartees(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  POSTCONDITION(d->nb_phrases_ecartees <= d->nb_phrases);
  return d->nb_phrases_ecartees;
}

size_t langue_detecteur_nb_mots_ecartes(const langue_detecteur_t *d)
{
  PRECONDITION(d != NULL);
  PRECONDITION(d->conclu);
  POSTCONDITION(d->nb_mots_ecartes <= d->nb_occurrences);
  return d->nb_mots_ecartes;
}