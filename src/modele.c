/**
@file modele.c
Fichier modele.c

Implementation du modele de langue (cf. modele.h) : apprentissage,
generation de phrases lisibles, sauvegarde et chargement.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "contrat.h"
#include "modele.h"
#include "repertoire.h"
#include "sauvegarde.h"

/** Premiere ligne d'un fichier de modele (format version 1). */
#define ENTETE_MODELE "modele-markov 1"

/** Melange de la graine pour la chaine de ponctuation : ses tirages sont
    independants de ceux de la chaine des mots. */
#define MELANGE_GRAINE_PONCTUATION 0x5BD1E995u

/** Taille a partir de laquelle le tampon d'ecriture est vide dans le
    fichier lors d'une sauvegarde. */
#define TAILLE_ECRITURE ((size_t)65536)

/** Capacite initiale des tampons de texte. */
#define CAPACITE_TAMPON_INITIALE ((size_t)128)

/** Texte de chaque liaison (cf. liaison_t). */
static const char *const TEXTE_LIAISON[NB_LIAISONS] = {
  " ", "'", "-", ". ", ", ", "; ", ": "
};

/** Texte de chaque ponctuation finale (etats 0 .. 3 de la ponctuation). */
static const char *const TEXTE_PONCTUATION[REPERTOIRE_NB_FINS] = {
  ".", "!", "?", "..."
};

struct modele_s {
  unsigned int ordre;
  hachage_t *mots;
  hachage_paires_t *bigrammes;
  lexique_t *lexique;
  markov_t *chaine;
  markov_t *ponctuation;
};


/* ======================================================================== */
/*                            Tampon de texte                               */
/* ======================================================================== */

/** Texte en construction (toujours termine par '\0' une fois alloue). */
typedef struct {
  char *texte;
  size_t longueur;
  size_t capacite;
} tampon_t;

/** Garantit la place de supplement octets (plus le '\0'). */
static bool reserver(tampon_t *t, size_t supplement)
{
  size_t capacite = t->capacite;
  char *agrandi;
  bool succes = true;

  if (supplement > SIZE_MAX / 4u - t->longueur) {
    succes = false;
  } else if (t->longueur + supplement + 1u > t->capacite) {
    if (capacite == 0u) {
      capacite = CAPACITE_TAMPON_INITIALE;
    }
    while (t->longueur + supplement + 1u > capacite) {
      capacite *= 2u;
    }
    agrandi = realloc(t->texte, capacite);
    if (agrandi == NULL) {
      succes = false;
    } else {
      t->texte = agrandi;
      t->capacite = capacite;
    }
  }
  if (!succes) {
    errno = ENOMEM;
  }
  return succes;
}

static bool ajouter(tampon_t *t, const char *texte)
{
  size_t longueur = strlen(texte);
  bool succes = reserver(t, longueur);

  if (succes) {
    memcpy(t->texte + t->longueur, texte, longueur + 1u);
    t->longueur += longueur;
  }
  return succes;
}

static bool ajouter_nombre(tampon_t *t, unsigned int nombre)
{
  char texte[16];

  snprintf(texte, sizeof(texte), "%u", nombre);
  return ajouter(t, texte);
}

/** Ajoute " <nombre>". */
static bool ajouter_champ(tampon_t *t, unsigned int nombre)
{
  return ajouter(t, " ") && ajouter_nombre(t, nombre);
}

/** Ajoute " <nombre>", ou " -" si nombre == LEXIQUE_AUCUNE (choix absent). */
static bool ajouter_choix(tampon_t *t, unsigned int nombre)
{
  return ajouter(t, " ")
         && ((nombre == LEXIQUE_AUCUNE) ? ajouter(t, "-")
                                         : ajouter_nombre(t, nombre));
}


/* ======================================================================== */
/*                         Creation et destruction                          */
/* ======================================================================== */

/** Modele dont toutes les structures sont absentes. */
static modele_t *modele_vide(unsigned int ordre)
{
  modele_t *m = malloc(sizeof(modele_t));

  if (m == NULL) {
    errno = ENOMEM;
  } else {
    m->ordre = ordre;
    m->mots = NULL;
    m->bigrammes = NULL;
    m->lexique = NULL;
    m->chaine = NULL;
    m->ponctuation = NULL;
  }
  return m;
}

void modele_detruire(modele_t **m)
{
  PRECONDITION(m != NULL);
  if (*m != NULL) {
    hachage_detruire(&(*m)->mots);
    hachage_paires_detruire(&(*m)->bigrammes);
    lexique_detruire(&(*m)->lexique);
    markov_detruire(&(*m)->chaine);
    markov_detruire(&(*m)->ponctuation);
    free(*m);
    *m = NULL;
  }
  POSTCONDITION(*m == NULL);
}

modele_t *modele_apprendre(const char *chemin_repertoire, unsigned int graine,
                           unsigned int ordre, bool recursif)
{
  modele_t *m;
  repertoire_options_t options;
  int erreur;

  PRECONDITION(chemin_repertoire != NULL);
  PRECONDITION(ordre == 1u || ordre == 2u);

  m = modele_vide(ordre);
  if (m != NULL) {
    m->mots = hachage_creer(0u);
    m->bigrammes = hachage_paires_creer(0u);
    m->lexique = lexique_creer();
    m->ponctuation = markov_creer(graine ^ MELANGE_GRAINE_PONCTUATION);
    if (m->mots == NULL || m->bigrammes == NULL || m->lexique == NULL
        || m->ponctuation == NULL) {
      modele_detruire(&m);
      errno = ENOMEM;
    }
  }
  if (m != NULL) {
    options.ordre = ordre;
    options.recursif = recursif;
    options.bigrammes = m->bigrammes;
    options.lexique = m->lexique;
    options.ponctuation = m->ponctuation;
    m->chaine = repertoire_apprendre(chemin_repertoire, graine, m->mots,
                                     &options);
    if (m->chaine == NULL) {
      erreur = errno;
      modele_detruire(&m);
      errno = erreur;
    }
  }
  POSTCONDITION(m == NULL || modele_ordre(m) == ordre);
  return m;
}


/* ======================================================================== */
/*                               Consultation                               */
/* ======================================================================== */

unsigned int modele_ordre(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->ordre;
}

unsigned int modele_nb_mots(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return hachage_nb_mots(m->mots) - 1u;
}

bool modele_peut_generer(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return markov_nb_voisins(m->chaine, REPERTOIRE_DEBUT_PHRASE) > 0u;
}

const hachage_t *modele_mots(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->mots;
}

const hachage_paires_t *modele_bigrammes(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->bigrammes;
}

const lexique_t *modele_lexique(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->lexique;
}

const markov_t *modele_chaine(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->chaine;
}

const markov_t *modele_ponctuation(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->ponctuation;
}


/* ======================================================================== */
/*                               Generation                                 */
/* ======================================================================== */

/** Ajoute la graphie d'affichage du mot d'un bigramme. */
static bool ajouter_graphie(tampon_t *t, const lexique_t *l,
                            unsigned int bigramme, bool debut_phrase)
{
  size_t longueur = lexique_ecrire(l, bigramme, debut_phrase, NULL, 0u);
  bool succes = reserver(t, longueur);

  if (succes) {
    lexique_ecrire(l, bigramme, debut_phrase, t->texte + t->longueur,
                   t->capacite - t->longueur);
    t->longueur += longueur;
  }
  return succes;
}

char *modele_generer_phrase(modele_t *m)
{
  tampon_t phrase = { NULL, 0u, 0u };
  unsigned int etat;
  unsigned int mot = REPERTOIRE_DEBUT_PHRASE;
  unsigned int precedent = REPERTOIRE_DEBUT_PHRASE;
  unsigned int bigramme;
  unsigned int nb_mots = 0u;
  unsigned int ponctuation = (unsigned int)PONCTUATION_POINT;
  bool succes = true;

  PRECONDITION(m != NULL);
  PRECONDITION(modele_peut_generer(m));

  etat = markov_deplacer(m->chaine, REPERTOIRE_DEBUT_PHRASE);
  while (etat != REPERTOIRE_DEBUT_PHRASE
         && nb_mots < MODELE_NB_MOTS_MAX_PHRASE) {
    /* bigramme (mot precedent, mot) : c'est l'etat a l'ordre 2 ; a l'ordre
       1, il existe car la transition precedent -> mot a ete observee */
    if (m->ordre == 1u) {
      mot = etat;
      bigramme = hachage_paires_identifiant(m->bigrammes, precedent, mot);
    } else {
      bigramme = etat;
      mot = hachage_paires_second(m->bigrammes, bigramme);
    }
    INVARIANT(bigramme != HACHAGE_ABSENT);
    if (succes && nb_mots > 0u) {
      succes = ajouter(&phrase,
                       TEXTE_LIAISON[lexique_liaison(m->lexique, bigramme)]);
    }
    if (succes) {
      succes = ajouter_graphie(&phrase, m->lexique, bigramme, nb_mots == 0u);
    }
    precedent = mot;
    nb_mots++;
    etat = markov_deplacer(m->chaine, etat);
  }
  /* ponctuation finale tiree selon le dernier mot (sauf phrase coupee) */
  if (etat == REPERTOIRE_DEBUT_PHRASE) {
    ponctuation = markov_deplacer(m->ponctuation,
                                  REPERTOIRE_NB_FINS + precedent);
  }
  INVARIANT(ponctuation < REPERTOIRE_NB_FINS);
  if (succes) {
    succes = ajouter(&phrase, TEXTE_PONCTUATION[ponctuation]);
  }
  if (!succes) {
    free(phrase.texte);
    phrase.texte = NULL;
    errno = ENOMEM;
  }
  POSTCONDITION(phrase.texte == NULL || phrase.longueur > 1u);
  return phrase.texte;
}


/* ======================================================================== */
/*                               Sauvegarde                                 */
/* ======================================================================== */

/** Ecrit le tampon dans le fichier puis le vide. */
static bool vider(tampon_t *t, FILE *fichier)
{
  bool succes = true;

  if (t->longueur > 0u) {
    succes = sauvegarde_ecrire(fichier, t->texte);
    t->longueur = 0u;
    t->texte[0] = '\0';
  }
  return succes;
}

/** Termine une ligne et vide le tampon s'il est assez rempli. */
static bool fin_de_ligne(tampon_t *t, FILE *fichier)
{
  bool succes = ajouter(t, "\n");

  if (succes && t->longueur >= TAILLE_ECRITURE) {
    succes = vider(t, fichier);
  }
  return succes;
}

/** Ecrit "<titre> <nombre>". */
static bool ecrire_section(tampon_t *t, FILE *fichier, const char *titre,
                           unsigned int nombre)
{
  return ajouter(t, titre) && ajouter_champ(t, nombre)
         && fin_de_ligne(t, fichier);
}

/** Ecrit les transitions de chaque etat d'une chaine finalisee. */
static bool ecrire_chaine(tampon_t *t, FILE *fichier, const char *titre,
                          const markov_t *chaine)
{
  unsigned int e;
  unsigned int r;
  unsigned int k;
  unsigned int destination;
  unsigned int poids;
  bool succes = ecrire_section(t, fichier, titre, markov_nb_etats(chaine));

  for (e = 0u; succes && e < markov_nb_etats(chaine); e++) {
    k = markov_nb_voisins(chaine, e);
    succes = ajouter_nombre(t, k);
    for (r = 0u; succes && r < k; r++) {
      destination = markov_voisin(chaine, e, r, &poids);
      succes = ajouter_champ(t, destination) && ajouter_champ(t, poids);
    }
    succes = succes && fin_de_ligne(t, fichier);
  }
  return succes;
}

/** Ecrit tout le modele dans le tampon et le fichier. */
static bool ecrire_modele(const modele_t *m, tampon_t *t, FILE *fichier)
{
  unsigned int n = hachage_nb_mots(m->mots);
  unsigned int b = hachage_paires_nb(m->bigrammes);
  unsigned int i;
  unsigned int r;
  unsigned int k;
  bool succes;

  succes = ajouter(t, ENTETE_MODELE) && fin_de_ligne(t, fichier)
           && ecrire_section(t, fichier, "ordre", m->ordre)
           && ecrire_section(t, fichier, "mots", n);
  /* mots : cle, variante choisie, casse, variantes */
  for (i = 0u; succes && i < n; i++) {
    k = lexique_nb_variantes(m->lexique, i);
    succes = ajouter(t, (i == REPERTOIRE_DEBUT_PHRASE)
                        ? "-" : hachage_mot(m->mots, i))
             && ajouter_choix(t, lexique_variante_mot(m->lexique, i))
             && ajouter_champ(t, (unsigned int)lexique_casse_mot(m->lexique,
                                                                 i))
             && ajouter_champ(t, k);
    for (r = 0u; succes && r < k; r++) {
      succes = ajouter(t, " ") && ajouter(t, lexique_variante(m->lexique, i, r));
    }
    succes = succes && fin_de_ligne(t, fichier);
  }
  /* bigrammes : paire et choix d'affichage */
  succes = succes && ecrire_section(t, fichier, "bigrammes", b);
  for (i = 0u; succes && i < b; i++) {
    succes = ajouter_nombre(t, hachage_paires_premier(m->bigrammes, i))
             && ajouter_champ(t, hachage_paires_second(m->bigrammes, i))
             && ajouter_choix(t, lexique_variante_bigramme(m->lexique, i))
             && ajouter_choix(t, lexique_casse_bigramme(m->lexique, i))
             && ajouter_champ(t, (unsigned int)lexique_liaison(m->lexique, i))
             && fin_de_ligne(t, fichier);
  }
  succes = succes && ecrire_chaine(t, fichier, "chaine", m->chaine)
           && ecrire_chaine(t, fichier, "ponctuation", m->ponctuation)
           && ajouter(t, "fin") && fin_de_ligne(t, fichier)
           && vider(t, fichier);
  return succes;
}

bool modele_sauvegarder(const modele_t *m, const char *chemin_fichier)
{
  tampon_t tampon = { NULL, 0u, 0u };
  FILE *fichier = NULL;
  bool succes;
  int erreur = 0;

  PRECONDITION(m != NULL);
  PRECONDITION(chemin_fichier != NULL);

  /* fichier vide (cree ou remplace) puis ecritures en mode ajout */
  succes = sauvegarde_creer_fichier("", chemin_fichier);
  if (succes) {
    fichier = sauvegarde_ouvrir_ajout(chemin_fichier);
    succes = (fichier != NULL);
  }
  if (succes) {
    succes = ecrire_modele(m, &tampon, fichier);
  }
  if (!succes) {
    erreur = errno;
  }
  if (fichier != NULL && !sauvegarde_fermer(fichier) && succes) {
    erreur = errno;
    succes = false;
  }
  free(tampon.texte);
  if (!succes) {
    if (fichier != NULL) {
      sauvegarde_supprimer(chemin_fichier);  /* pas de modele incomplet */
    }
    errno = erreur;
  }
  return succes;
}


/* ======================================================================== */
/*                               Chargement                                 */
/* ======================================================================== */

/** Lecture ligne par ligne d'un fichier de modele. */
typedef struct {
  FILE *fichier;
  tampon_t ligne;
  int erreur;      /* 0, EINVAL, ENOMEM ou EIO */
} lecteur_t;

/**
 * Lit la ligne suivante (sans son '\n') dans lecteur->ligne. Renvoie false
 * a la fin du fichier ou en cas d'erreur (lecteur->erreur est alors non
 * nul). Une ligne contenant un octet nul, ou qui commence ou finit par une
 * espace, est invalide.
 */
static bool lire_ligne(lecteur_t *lecteur)
{
  int octet;
  bool lue = false;
  bool fini = false;

  lecteur->ligne.longueur = 0u;
  while (lecteur->erreur == 0 && !fini) {
    octet = getc(lecteur->fichier);
    if (octet == EOF) {
      fini = true;
      if (ferror(lecteur->fichier)) {
        lecteur->erreur = EIO;
      }
    } else {
      lue = true;
      if (octet == '\n') {
        fini = true;
      } else if (octet == '\0') {
        lecteur->erreur = EINVAL;
      } else if (!reserver(&lecteur->ligne, 1u)) {
        lecteur->erreur = ENOMEM;
      } else {
        lecteur->ligne.texte[lecteur->ligne.longueur] = (char)octet;
        lecteur->ligne.longueur++;
      }
    }
  }
  if (lue && lecteur->erreur == 0 && !reserver(&lecteur->ligne, 0u)) {
    lecteur->erreur = ENOMEM;              /* ligne vide : tampon a creer */
  }
  if (lue && lecteur->erreur == 0) {
    lecteur->ligne.texte[lecteur->ligne.longueur] = '\0';
    if (lecteur->ligne.longueur > 0u
        && (lecteur->ligne.texte[0] == ' '
            || lecteur->ligne.texte[lecteur->ligne.longueur - 1u] == ' ')) {
      lecteur->erreur = EINVAL;
    }
  }
  return lue && lecteur->erreur == 0;
}

/**
 * Lit la ligne suivante, qui doit exister. Renvoie le curseur de lecture,
 * ou NULL (lecteur->erreur vaut alors EINVAL a la fin du fichier).
 */
static char *ligne_suivante(lecteur_t *lecteur)
{
  char *curseur = NULL;

  if (lire_ligne(lecteur)) {
    curseur = lecteur->ligne.texte;
  } else if (lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;              /* fichier tronque */
  }
  return curseur;
}

/** Lit le jeton suivant (non vide) de la ligne et avance le curseur. */
static bool lire_jeton(char **curseur, char **jeton)
{
  bool lu = (**curseur != '\0' && **curseur != ' ');

  if (lu) {
    *jeton = *curseur;
    while (**curseur != '\0' && **curseur != ' ') {
      (*curseur)++;
    }
    if (**curseur == ' ') {
      **curseur = '\0';
      (*curseur)++;
    }
  }
  return lu;
}

/** Lit un entier decimal non signe (au plus UINT_MAX). */
static bool lire_entier(char **curseur, unsigned int *valeur)
{
  char *jeton;
  const char *c;
  unsigned long long nombre = 0u;
  bool lu = lire_jeton(curseur, &jeton);

  for (c = jeton; lu && *c != '\0'; c++) {
    lu = (*c >= '0' && *c <= '9');
    nombre = (10u * nombre) + (unsigned long long)(*c - '0');
    lu = lu && nombre <= UINT_MAX;
  }
  if (lu) {
    *valeur = (unsigned int)nombre;
  }
  return lu;
}

/** Lit un entier, ou "-" (valeur LEXIQUE_AUCUNE). */
static bool lire_entier_ou_tiret(char **curseur, unsigned int *valeur)
{
  bool lu;

  if ((*curseur)[0] == '-' && ((*curseur)[1] == ' ' || (*curseur)[1] == '\0')) {
    *valeur = LEXIQUE_AUCUNE;
    lu = true;
    (*curseur)++;
    if (**curseur == ' ') {
      (*curseur)++;
    }
  } else {
    lu = lire_entier(curseur, valeur);
  }
  return lu;
}

/** Lit une ligne "<titre> <nombre>". */
static bool lire_section(lecteur_t *lecteur, const char *titre,
                         unsigned int *nombre)
{
  char *curseur = ligne_suivante(lecteur);
  char *jeton;
  bool lu = curseur != NULL && lire_jeton(&curseur, &jeton)
            && strcmp(jeton, titre) == 0 && lire_entier(&curseur, nombre)
            && *curseur == '\0';

  if (!lu && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
  return lu;
}

/** Choix d'affichage lus, appliques apres la finalisation du lexique. */
typedef struct {
  unsigned int *variante_mot;
  casse_t *casse_mot;
  unsigned int *variante_bigramme;
  unsigned int *casse_bigramme;
  liaison_t *liaison_bigramme;
} choix_t;

/** Lit la ligne du mot i (cle, choix, variantes). */
static void lire_mot(lecteur_t *lecteur, modele_t *m, choix_t *choix,
                     unsigned int i)
{
  char *curseur = ligne_suivante(lecteur);
  char *cle = NULL;
  char *variante;
  unsigned int variante_choisie = LEXIQUE_AUCUNE;
  unsigned int casse = 0u;
  unsigned int nb_variantes = 0u;
  unsigned int r;
  unsigned int identifiant;
  bool valide = curseur != NULL && lire_jeton(&curseur, &cle)
                && lire_entier_ou_tiret(&curseur, &variante_choisie)
                && lire_entier(&curseur, &casse) && casse < NB_CASSES
                && lire_entier(&curseur, &nb_variantes);

  /* le mot 0 est "- - c 0", les autres ont une cle {a..z} et au moins une
     variante, dont une choisie */
  if (valide && i == REPERTOIRE_DEBUT_PHRASE) {
    valide = strcmp(cle, "-") == 0 && variante_choisie == LEXIQUE_AUCUNE
             && nb_variantes == 0u;
    cle[0] = '\0';
  } else if (valide) {
    valide = hachage_mot_valide(cle) && nb_variantes > 0u
             && variante_choisie < nb_variantes;
  }
  if (valide) {
    identifiant = hachage_ajouter(m->mots, cle);
    if (identifiant == HACHAGE_ABSENT) {
      lecteur->erreur = ENOMEM;
    } else if (identifiant != i) {
      valide = false;                      /* mot en double */
    } else if (lexique_ajouter_mot(m->lexique) != i) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (r = 0u; valide && lecteur->erreur == 0 && r < nb_variantes; r++) {
    valide = lire_jeton(&curseur, &variante)
             && lexique_variante_valide(variante)
             && repertoire_graphie_valide(variante, cle);
    if (valide) {
      identifiant = lexique_ajouter_variante(m->lexique, i, variante);
      if (identifiant == LEXIQUE_ABSENT) {
        lecteur->erreur = ENOMEM;
      } else {
        valide = (identifiant == r);       /* variante en double */
      }
    }
  }
  if (valide && lecteur->erreur == 0) {
    valide = (*curseur == '\0');
    choix->variante_mot[i] = variante_choisie;
    choix->casse_mot[i] = (casse_t)casse;
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/** Lit la ligne du bigramme j (paire et choix d'affichage). */
static void lire_bigramme(lecteur_t *lecteur, modele_t *m, choix_t *choix,
                          unsigned int j)
{
  char *curseur = ligne_suivante(lecteur);
  unsigned int n = hachage_nb_mots(m->mots);
  unsigned int premier = 0u;
  unsigned int second = 0u;
  unsigned int variante = 0u;
  unsigned int casse = 0u;
  unsigned int liaison = 0u;
  unsigned int identifiant;
  bool valide = curseur != NULL && lire_entier(&curseur, &premier)
                && lire_entier(&curseur, &second)
                && lire_entier_ou_tiret(&curseur, &variante)
                && lire_entier_ou_tiret(&curseur, &casse)
                && lire_entier(&curseur, &liaison) && *curseur == '\0'
                && premier < n && second < n && liaison < NB_LIAISONS
                && (casse == LEXIQUE_AUCUNE || casse < NB_CASSES)
                /* la paire 0 est (0, 0) ; les autres decrivent un vrai mot */
                && ((j == 0u) ? (premier == 0u && second == 0u
                                 && variante == LEXIQUE_AUCUNE)
                              : second != REPERTOIRE_DEBUT_PHRASE)
                && (variante == LEXIQUE_AUCUNE
                    || variante < lexique_nb_variantes(m->lexique, second));

  if (valide) {
    identifiant = hachage_paires_ajouter(m->bigrammes, premier, second);
    if (identifiant == HACHAGE_ABSENT) {
      lecteur->erreur = ENOMEM;
    } else if (identifiant != j) {
      valide = false;                      /* paire en double */
    } else if (lexique_ajouter_bigramme(m->lexique, second) != j) {
      lecteur->erreur = ENOMEM;
    } else {
      choix->variante_bigramme[j] = variante;
      choix->casse_bigramme[j] = casse;
      choix->liaison_bigramme[j] = (liaison_t)liaison;
    }
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/** Lit une section de transitions dans une chaine (qui est creee). */
static markov_t *lire_chaine(lecteur_t *lecteur, const char *titre,
                             unsigned int nb_etats_attendu,
                             unsigned int graine)
{
  markov_t *chaine = NULL;
  char *curseur;
  unsigned int nb_etats = 0u;
  unsigned int e;
  unsigned int r;
  unsigned int k = 0u;
  unsigned int destination = 0u;
  unsigned int poids = 0u;
  unsigned int minimum;
  bool valide = lire_section(lecteur, titre, &nb_etats)
                && nb_etats == nb_etats_attendu;

  if (valide) {
    chaine = markov_creer(graine);
    if (chaine == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (e = 0u; valide && lecteur->erreur == 0 && e < nb_etats; e++) {
    if (markov_ajouter_etat(chaine) != e) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (e = 0u; valide && lecteur->erreur == 0 && e < nb_etats; e++) {
    curseur = ligne_suivante(lecteur);
    valide = curseur != NULL && lire_entier(&curseur, &k);
    minimum = 0u;
    for (r = 0u; valide && lecteur->erreur == 0 && r < k; r++) {
      /* etats d'arrivee strictement croissants : pas de doublon */
      valide = lire_entier(&curseur, &destination)
               && lire_entier(&curseur, &poids) && destination < nb_etats
               && destination >= minimum && poids > 0u;
      if (valide && !markov_augmenter_poids(chaine, e, destination, poids)) {
        if (errno == ENOMEM) {
          lecteur->erreur = ENOMEM;
        } else {
          valide = false;                  /* poids total trop grand */
        }
      }
      minimum = destination + 1u;
    }
    valide = valide && *curseur == '\0';
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur != 0) {
    markov_detruire(&chaine);
  } else {
    markov_finaliser(chaine);
  }
  return chaine;
}

/** Dernier mot d'un etat de la chaine. */
static unsigned int dernier_mot(const modele_t *m, unsigned int etat)
{
  return (m->ordre == 1u) ? etat : hachage_paires_second(m->bigrammes, etat);
}

/**
 * Verifie qu'un modele charge peut etre utilise sans risque : chaque etat
 * atteint a un voisin, chaque transition correspond a un bigramme connu et
 * chaque fin de phrase a une ponctuation.
 */
static bool verifier_coherence(const modele_t *m)
{
  unsigned int nb_etats = markov_nb_etats(m->chaine);
  unsigned int e;
  unsigned int r;
  unsigned int d;
  unsigned int poids;
  bool valide = true;

  for (e = 0u; valide && e < nb_etats; e++) {
    valide = (e == REPERTOIRE_DEBUT_PHRASE || markov_nb_voisins(m->chaine, e) > 0u);
    for (r = 0u; valide && r < markov_nb_voisins(m->chaine, e); r++) {
      d = markov_voisin(m->chaine, e, r, &poids);
      if (d == REPERTOIRE_DEBUT_PHRASE) {
        /* fin de phrase : pas de phrase vide, une ponctuation possible */
        valide = (e != REPERTOIRE_DEBUT_PHRASE)
                 && markov_nb_voisins(m->ponctuation,
                                      REPERTOIRE_NB_FINS + dernier_mot(m, e))
                    > 0u;
      } else if (m->ordre == 1u) {
        valide = (hachage_paires_identifiant(m->bigrammes, e, d)
                  != HACHAGE_ABSENT);
      } else {
        valide = (hachage_paires_premier(m->bigrammes, d)
                  == hachage_paires_second(m->bigrammes, e));
      }
    }
  }
  /* ponctuation : seuls les mots menent, et seulement a une ponctuation */
  for (e = 0u; valide && e < markov_nb_etats(m->ponctuation); e++) {
    for (r = 0u; valide && r < markov_nb_voisins(m->ponctuation, e); r++) {
      d = markov_voisin(m->ponctuation, e, r, &poids);
      valide = (e >= REPERTOIRE_NB_FINS && d < REPERTOIRE_NB_FINS);
    }
  }
  return valide;
}

/** Lit tout le fichier (le modele m a deja ses tables vides). */
static void lire_modele(lecteur_t *lecteur, modele_t *m, unsigned int graine)
{
  choix_t choix = { NULL, NULL, NULL, NULL, NULL };
  char *curseur;
  unsigned int n = 0u;
  unsigned int b = 0u;
  unsigned int i;

  /* en-tete, ordre et nombre de mots */
  curseur = ligne_suivante(lecteur);
  if (curseur != NULL && strcmp(curseur, ENTETE_MODELE) != 0) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0 && lire_section(lecteur, "ordre", &m->ordre)
      && m->ordre != 1u && m->ordre != 2u) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0 && lire_section(lecteur, "mots", &n) && n == 0u) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0) {
    choix.variante_mot = calloc(n, sizeof(unsigned int));
    choix.casse_mot = calloc(n, sizeof(casse_t));
    if (choix.variante_mot == NULL || choix.casse_mot == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (i = 0u; lecteur->erreur == 0 && i < n; i++) {
    lire_mot(lecteur, m, &choix, i);
  }
  /* bigrammes */
  if (lecteur->erreur == 0 && lire_section(lecteur, "bigrammes", &b)
      && b == 0u) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0) {
    choix.variante_bigramme = calloc(b, sizeof(unsigned int));
    choix.casse_bigramme = calloc(b, sizeof(unsigned int));
    choix.liaison_bigramme = calloc(b, sizeof(liaison_t));
    if (choix.variante_bigramme == NULL || choix.casse_bigramme == NULL
        || choix.liaison_bigramme == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (i = 0u; lecteur->erreur == 0 && i < b; i++) {
    lire_bigramme(lecteur, m, &choix, i);
  }
  /* chaines */
  if (lecteur->erreur == 0) {
    m->chaine = lire_chaine(lecteur, "chaine", (m->ordre == 1u) ? n : b,
                            graine);
  }
  if (lecteur->erreur == 0) {
    m->ponctuation = lire_chaine(lecteur, "ponctuation",
                                 REPERTOIRE_NB_FINS + n,
                                 graine ^ MELANGE_GRAINE_PONCTUATION);
  }
  /* "fin", puis plus rien */
  if (lecteur->erreur == 0) {
    curseur = ligne_suivante(lecteur);
    if (curseur != NULL && (strcmp(curseur, "fin") != 0 || lire_ligne(lecteur))) {
      lecteur->erreur = EINVAL;
    }
  }
  /* choix d'affichage, puis verification globale */
  if (lecteur->erreur == 0) {
    lexique_finaliser(m->lexique);
    for (i = 1u; i < n; i++) {
      lexique_fixer_mot(m->lexique, i, choix.variante_mot[i],
                        choix.casse_mot[i]);
    }
    for (i = 0u; i < b; i++) {
      lexique_fixer_bigramme(m->lexique, i, choix.variante_bigramme[i],
                             choix.casse_bigramme[i],
                             choix.liaison_bigramme[i]);
    }
    if (!verifier_coherence(m)) {
      lecteur->erreur = EINVAL;
    }
  }
  free(choix.variante_mot);
  free(choix.casse_mot);
  free(choix.variante_bigramme);
  free(choix.casse_bigramme);
  free(choix.liaison_bigramme);
}

modele_t *modele_charger(const char *chemin_fichier, unsigned int graine)
{
  lecteur_t lecteur = { NULL, { NULL, 0u, 0u }, 0 };
  modele_t *m;
  int erreur = 0;

  PRECONDITION(chemin_fichier != NULL);

  m = modele_vide(1u);
  if (m == NULL) {
    erreur = ENOMEM;
  } else {
    m->mots = hachage_creer(0u);
    m->bigrammes = hachage_paires_creer(0u);
    m->lexique = lexique_creer();
    if (m->mots == NULL || m->bigrammes == NULL || m->lexique == NULL) {
      erreur = ENOMEM;
    } else {
      lecteur.fichier = fopen(chemin_fichier, "rb");
      if (lecteur.fichier == NULL) {
        erreur = errno;
      } else {
        lire_modele(&lecteur, m, graine);
        erreur = lecteur.erreur;
        fclose(lecteur.fichier);
      }
    }
  }
  free(lecteur.ligne.texte);
  if (erreur != 0) {
    modele_detruire(&m);
    errno = erreur;
  }
  POSTCONDITION(m == NULL || modele_ordre(m) == 1u || modele_ordre(m) == 2u);
  return m;
}
