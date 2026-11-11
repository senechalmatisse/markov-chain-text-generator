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

/** Premiere ligne d'un fichier de modele (format version 2). */
#define ENTETE_MODELE "modele-markov 2"

/** Aucun mot de depart memorise (cf. etat_de_depart). */
#define AUCUN_DEPART UINT_MAX

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

/** Texte de chaque fin de phrase (etats 0 .. 4 de la ponctuation ; la
    derniere est la fin sans ponctuation). */
static const char *const TEXTE_PONCTUATION[REPERTOIRE_NB_FINS] = {
  ".", "!", "?", "...", ""
};

/** Marques de dialogue, en UTF-8 : tiret cadratin suivi d'une espace,
    guillemets francais ouvrant et fermant. */
#define TEXTE_TIRET "\xE2\x80\x94 "
#define TEXTE_GUILLEMET_OUVRANT "\xC2\xAB"
#define TEXTE_GUILLEMET_FERMANT "\xC2\xBB"

struct modele_s {
  unsigned int ordre;
  hachage_t *mots;
  hachage_paires_t *bigrammes;
  hachage_nuplets_t *etats;     /* k-uplets de mots (ordre >= 3), ou NULL */
  lexique_t *lexique;
  markov_t *chaine;
  markov_t *ponctuation;
  /* dernier resultat de etat_de_depart (evite de reparcourir les etats) */
  unsigned int depart_mot;      /* AUCUN_DEPART si rien n'est memorise */
  unsigned int depart_etat;
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
    m->etats = NULL;
    m->lexique = NULL;
    m->chaine = NULL;
    m->ponctuation = NULL;
    m->depart_mot = AUCUN_DEPART;
    m->depart_etat = REPERTOIRE_DEBUT_PHRASE;
  }
  return m;
}

void modele_detruire(modele_t **m)
{
  PRECONDITION(m != NULL);
  if (*m != NULL) {
    hachage_detruire(&(*m)->mots);
    hachage_paires_detruire(&(*m)->bigrammes);
    hachage_nuplets_detruire(&(*m)->etats);
    lexique_detruire(&(*m)->lexique);
    markov_detruire(&(*m)->chaine);
    markov_detruire(&(*m)->ponctuation);
    free(*m);
    *m = NULL;
  }
  POSTCONDITION(*m == NULL);
}

modele_t *modele_apprendre_corpus(const char *const *chemins,
                                  size_t nb_chemins, unsigned int graine,
                                  const modele_options_t *options,
                                  repertoire_bilan_t *bilan)
{
  modele_t *m;
  repertoire_options_t o;
  int erreur;

  PRECONDITION(chemins != NULL);
  PRECONDITION(nb_chemins >= 1u);
  PRECONDITION(options != NULL);
  PRECONDITION(options->ordre >= 1u && options->ordre <= MODELE_ORDRE_MAX);

  m = modele_vide(options->ordre);
  if (m != NULL) {
    m->mots = hachage_creer(0u);
    m->bigrammes = hachage_paires_creer(0u);
    m->lexique = lexique_creer();
    m->ponctuation = markov_creer(graine ^ MELANGE_GRAINE_PONCTUATION);
    if (options->ordre >= 3u) {
      m->etats = hachage_nuplets_creer(options->ordre, 0u);
    }
    if (m->mots == NULL || m->bigrammes == NULL || m->lexique == NULL
        || m->ponctuation == NULL
        || (options->ordre >= 3u && m->etats == NULL)) {
      modele_detruire(&m);
      errno = ENOMEM;
    }
  }
  if (m != NULL) {
    o.ordre = options->ordre;
    o.recursif = options->recursif;
    o.bigrammes = m->bigrammes;
    o.lexique = m->lexique;
    o.ponctuation = m->ponctuation;
    o.nuplets = m->etats;
    o.phrase_par_ligne = options->phrase_par_ligne;
    o.filtrer_langues = options->filtrer_langues;
    o.bilan = bilan;
    m->chaine = repertoire_apprendre_corpus(chemins, nb_chemins, graine, m->mots, &o);
    if (m->chaine == NULL) {
      erreur = errno;
      modele_detruire(&m);
      errno = erreur;
    }
  }
  POSTCONDITION(m == NULL || modele_ordre(m) == options->ordre);
  return m;
}

modele_t *modele_apprendre(const char *chemin_repertoire, unsigned int graine,
                           unsigned int ordre, bool recursif)
{
  modele_options_t options;

  PRECONDITION(chemin_repertoire != NULL);
  PRECONDITION(ordre >= 1u && ordre <= MODELE_ORDRE_MAX);

  options.ordre = ordre;
  options.recursif = recursif;
  options.phrase_par_ligne = false;
  options.filtrer_langues = false;
  return modele_apprendre_corpus(&chemin_repertoire, 1u, graine, &options, NULL);
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

const hachage_nuplets_t *modele_etats(const modele_t *m)
{
  PRECONDITION(m != NULL);
  return m->etats;
}

unsigned int modele_mot(const modele_t *m, const char *graphie)
{
  char *cle;
  unsigned int mot = HACHAGE_ABSENT;

  PRECONDITION(m != NULL);
  PRECONDITION(graphie != NULL);

  cle = repertoire_cle(graphie);            /* errno : EINVAL ou ENOMEM */
  if (cle != NULL) {
    mot = hachage_identifiant(m->mots, cle);
    free(cle);
    if (mot == HACHAGE_ABSENT) {
      errno = ENOENT;
    }
  }
  POSTCONDITION(mot == HACHAGE_ABSENT || (mot >= 1u && mot <= modele_nb_mots(m)));
  return mot;
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

/** Dernier mot d'un etat de la chaine. */
static unsigned int dernier_mot(const modele_t *m, unsigned int etat)
{
  unsigned int mot;

  if (m->ordre == 1u) {
    mot = etat;
  } else if (m->ordre == 2u) {
    mot = hachage_paires_second(m->bigrammes, etat);
  } else {
    mot = hachage_nuplets_element(m->etats, etat, m->ordre - 1u);
  }
  return mot;
}

/** Phrase en cours d'ecriture. */
typedef struct {
  tampon_t texte;
  unsigned int nb_mots;
  bool guillemet_ouvert;  /* un guillemet ouvrant attend son fermant */
  bool succes;            /* false des que la memoire a manque */
} ecriture_t;

/** Ajoute un texte a la phrase (sans effet apres un echec). */
static void ecrire(ecriture_t *e, const char *texte)
{
  e->succes = e->succes && ajouter(&e->texte, texte);
}

/**
 * Ajoute a la phrase le mot d'un bigramme, precede de son separateur : dans
 * l'ordre, guillemet fermant, liaison, tiret, guillemet ouvrant. Devant le
 * premier mot, seuls le tiret et le guillemet ouvrant sont ecrits. Un
 * guillemet n'est ferme que s'il est ouvert, et ouvert que s'il est ferme.
 */
static void ecrire_mot(const modele_t *m, ecriture_t *e, unsigned int bigramme)
{
  const lexique_t *l = m->lexique;
  unsigned int marques = lexique_marques(l, bigramme);
  bool debut = (e->nb_mots == 0u);
  size_t longueur;

  if (!debut) {
    if ((marques & MARQUE_FERME_GUILLEMET) != 0u && e->guillemet_ouvert) {
      ecrire(e, TEXTE_GUILLEMET_FERMANT);
      e->guillemet_ouvert = false;
    }
    ecrire(e, TEXTE_LIAISON[lexique_liaison(l, bigramme)]);
  }
  if ((marques & MARQUE_TIRET) != 0u) {
    ecrire(e, TEXTE_TIRET);
  }
  if ((marques & MARQUE_OUVRE_GUILLEMET) != 0u && !e->guillemet_ouvert) {
    ecrire(e, TEXTE_GUILLEMET_OUVRANT);
    e->guillemet_ouvert = true;
  }
  longueur = lexique_ecrire(l, bigramme, debut, NULL, 0u);
  e->succes = e->succes && reserver(&e->texte, longueur);
  if (e->succes) {
    lexique_ecrire(l, bigramme, debut, e->texte.texte + e->texte.longueur,
                   e->texte.capacite - e->texte.longueur);
    e->texte.longueur += longueur;
  }
  e->nb_mots++;
}

/** Ajoute a la phrase son premier mot, ecrit hors de tout contexte. */
static void ecrire_premier_mot_seul(const modele_t *m, ecriture_t *e, unsigned int mot)
{
  size_t longueur = lexique_ecrire_mot(m->lexique, mot, true, NULL, 0u);

  e->succes = e->succes && reserver(&e->texte, longueur);
  if (e->succes) {
    lexique_ecrire_mot(m->lexique, mot, true,
                       e->texte.texte + e->texte.longueur,
                       e->texte.capacite - e->texte.longueur);
    e->texte.longueur += longueur;
  }
  e->nb_mots++;
}

/**
 * Ecrit une phrase en parcourant la chaine a partir de l'etat donne (qui
 * n'est pas le debut de phrase) jusqu'au retour a l'etat 0. Si
 * premier_seul, le premier mot est ecrit hors contexte (son etat n'est pas
 * un debut de phrase). Renvoie la phrase, ou NULL si la memoire manque.
 */
static char *generer(modele_t *m, unsigned int etat, bool premier_seul)
{
  ecriture_t e = { { NULL, 0u, 0u }, 0u, false, true };
  unsigned int mot;
  unsigned int precedent = REPERTOIRE_DEBUT_PHRASE;
  unsigned int bigramme;
  unsigned int ponctuation = (unsigned int)PONCTUATION_POINT;

  while (etat != REPERTOIRE_DEBUT_PHRASE
         && e.nb_mots < MODELE_NB_MOTS_MAX_PHRASE) {
    mot = dernier_mot(m, etat);
    if (premier_seul && e.nb_mots == 0u) {
      ecrire_premier_mot_seul(m, &e, mot);
    } else {
      /* bigramme (mot precedent, mot) : c'est l'etat a l'ordre 2 ; sinon il
         existe car ces deux mots se suivent dans le corpus */
      bigramme = (m->ordre == 2u)
                 ? etat
                 : hachage_paires_identifiant(m->bigrammes, precedent, mot);
      INVARIANT(bigramme != HACHAGE_ABSENT);
      ecrire_mot(m, &e, bigramme);
    }
    precedent = mot;
    etat = markov_deplacer(m->chaine, etat);
  }
  /* ponctuation finale tiree selon le dernier mot (sauf phrase coupee),
     puis fermeture du guillemet reste ouvert */
  if (etat == REPERTOIRE_DEBUT_PHRASE) {
    ponctuation = markov_deplacer(m->ponctuation, REPERTOIRE_NB_FINS + precedent);
  }
  INVARIANT(ponctuation < REPERTOIRE_NB_FINS);
  ecrire(&e, TEXTE_PONCTUATION[ponctuation]);
  if (e.guillemet_ouvert) {
    ecrire(&e, TEXTE_GUILLEMET_FERMANT);
  }
  if (!e.succes) {
    free(e.texte.texte);
    e.texte.texte = NULL;
    errno = ENOMEM;
  }
  POSTCONDITION(e.texte.texte == NULL || e.texte.longueur >= 1u);
  return e.texte.texte;
}

char *modele_generer_phrase(modele_t *m)
{
  PRECONDITION(m != NULL);
  PRECONDITION(modele_peut_generer(m));

  return generer(m, markov_deplacer(m->chaine, REPERTOIRE_DEBUT_PHRASE), false);
}

/**
 * Etat "debut de phrase suivi du mot" : (0, mot) a l'ordre 2,
 * (0, ..., 0, mot) au-dela, le mot lui-meme a l'ordre 1 ; HACHAGE_ABSENT si
 * le mot ne commence aucune phrase du corpus.
 */
static unsigned int etat_debut_de_phrase(const modele_t *m, unsigned int mot)
{
  unsigned int elements[MODELE_ORDRE_MAX] = { 0u };
  unsigned int etat = hachage_paires_identifiant(m->bigrammes, REPERTOIRE_DEBUT_PHRASE, mot);

  if (etat != HACHAGE_ABSENT && m->ordre == 1u) {
    etat = mot;
  } else if (etat != HACHAGE_ABSENT && m->ordre >= 3u) {
    elements[m->ordre - 1u] = mot;
    etat = hachage_nuplets_identifiant(m->etats, elements);
  }
  return etat;
}

/**
 * Etat d'ou part une phrase qui commence par le mot donne, et indication
 * (premier_seul) que cet etat n'est pas un debut de phrase. Le resultat est
 * memorise : le parcours des etats n'est refait que si le mot change.
 */
static unsigned int etat_de_depart(modele_t *m, unsigned int mot, bool *premier_seul)
{
  unsigned int nb_etats = markov_nb_etats(m->chaine);
  unsigned int etat = etat_debut_de_phrase(m, mot);
  unsigned int e;
  unsigned int poids;
  unsigned int meilleur_poids = 0u;

  *premier_seul = (etat == HACHAGE_ABSENT);
  if (etat == HACHAGE_ABSENT && m->ordre == 1u) {
    etat = mot;
  } else if (etat == HACHAGE_ABSENT && m->depart_mot == mot) {
    etat = m->depart_etat;
  } else if (etat == HACHAGE_ABSENT) {
    /* contexte le plus frequent : etat de plus grand poids total dont le
       mot est le dernier (le premier cree, a poids egal) */
    for (e = 1u; e < nb_etats; e++) {
      if (dernier_mot(m, e) == mot) {
        poids = markov_poids_total(m->chaine, e);
        if (poids > meilleur_poids) {
          meilleur_poids = poids;
          etat = e;
        }
      }
    }
    m->depart_mot = mot;
    m->depart_etat = etat;
  }
  /* tout mot est le dernier mot d'un etat qui a un voisin (propriete des
     modeles appris, verifiee au chargement) */
  INVARIANT(etat != HACHAGE_ABSENT && etat != REPERTOIRE_DEBUT_PHRASE);
  return etat;
}

char *modele_generer_phrase_depuis(modele_t *m, unsigned int mot)
{
  unsigned int etat;
  bool premier_seul = false;

  PRECONDITION(m != NULL);
  PRECONDITION(mot >= 1u && mot <= modele_nb_mots(m));

  etat = etat_de_depart(m, mot, &premier_seul);
  return generer(m, etat, premier_seul);
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
static bool ecrire_section(tampon_t *t, FILE *fichier, const char *titre, unsigned int nombre)
{
  return ajouter(t, titre) && ajouter_champ(t, nombre) && fin_de_ligne(t, fichier);
}

/** Ecrit les transitions de chaque etat d'une chaine finalisee. */
static bool ecrire_chaine(tampon_t *t, FILE *fichier, const char *titre, const markov_t *chaine)
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
  unsigned int e = (m->etats != NULL) ? hachage_nuplets_nb(m->etats) : 0u;
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
    succes = ajouter(t, (i == REPERTOIRE_DEBUT_PHRASE) ? "-" : hachage_mot(m->mots, i))
             && ajouter_choix(t, lexique_variante_mot(m->lexique, i))
             && ajouter_champ(t, (unsigned int)lexique_casse_mot(m->lexique, i))
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
             && ajouter_champ(t, lexique_marques(m->lexique, i))
             && fin_de_ligne(t, fichier);
  }
  /* etats (ordre >= 3) : les k mots de chaque k-uplet */
  if (m->etats != NULL) {
    succes = succes && ecrire_section(t, fichier, "etats", e);
    for (i = 0u; succes && i < e; i++) {
      succes = ajouter_nombre(t, hachage_nuplets_element(m->etats, i, 0u));
      for (r = 1u; succes && r < m->ordre; r++) {
        succes = ajouter_champ(t, hachage_nuplets_element(m->etats, i, r));
      }
      succes = succes && fin_de_ligne(t, fichier);
    }
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
static bool lire_section(lecteur_t *lecteur, const char *titre, unsigned int *nombre)
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
  unsigned int *marques_bigramme;
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
static void lire_bigramme(lecteur_t *lecteur, modele_t *m, choix_t *choix, unsigned int j)
{
  char *curseur = ligne_suivante(lecteur);
  unsigned int n = hachage_nb_mots(m->mots);
  unsigned int premier = 0u;
  unsigned int second = 0u;
  unsigned int variante = 0u;
  unsigned int casse = 0u;
  unsigned int liaison = 0u;
  unsigned int marques = 0u;
  unsigned int identifiant;
  bool valide = curseur != NULL && lire_entier(&curseur, &premier)
                && lire_entier(&curseur, &second)
                && lire_entier_ou_tiret(&curseur, &variante)
                && lire_entier_ou_tiret(&curseur, &casse)
                && lire_entier(&curseur, &liaison)
                && lire_entier(&curseur, &marques) && *curseur == '\0'
                && premier < n && second < n && liaison < NB_LIAISONS
                && lexique_separateur_valide((liaison_t)liaison, marques)
                && (casse == LEXIQUE_AUCUNE || casse < NB_CASSES)
                /* la paire 0 est (0, 0) ; les autres decrivent un vrai mot */
                && ((j == 0u) ? (premier == 0u && second == 0u && variante == LEXIQUE_AUCUNE)
                              : second != REPERTOIRE_DEBUT_PHRASE)
                && (variante == LEXIQUE_AUCUNE || variante < lexique_nb_variantes(m->lexique, second));

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
      choix->marques_bigramme[j] = marques;
    }
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/**
 * Lit la ligne de l'etat j (ordre >= 3) : un k-uplet de mots. L'etat 0 est
 * (0, ..., 0) ; les autres se terminent par un vrai mot et leurs 0 (debut
 * de phrase) sont tous en tete.
 */
static void lire_etat(lecteur_t *lecteur, modele_t *m, unsigned int j)
{
  char *curseur = ligne_suivante(lecteur);
  unsigned int n = hachage_nb_mots(m->mots);
  unsigned int elements[MODELE_ORDRE_MAX] = { 0u };
  unsigned int identifiant;
  unsigned int r;
  bool valide = (curseur != NULL);

  for (r = 0u; valide && r < m->ordre; r++) {
    valide = lire_entier(&curseur, &elements[r]) && elements[r] < n
             && (r == 0u || elements[r] != REPERTOIRE_DEBUT_PHRASE
                 || elements[r - 1u] == REPERTOIRE_DEBUT_PHRASE);
  }
  valide = valide && *curseur == '\0' && ((elements[m->ordre - 1u] == REPERTOIRE_DEBUT_PHRASE) == (j == 0u));
  if (valide) {
    identifiant = hachage_nuplets_ajouter(m->etats, elements);
    if (identifiant == HACHAGE_ABSENT) {
      lecteur->erreur = ENOMEM;
    } else if (identifiant != j) {
      valide = false;                      /* etat en double */
    }
  }
  if (!valide && lecteur->erreur == 0) {
    lecteur->erreur = EINVAL;
  }
}

/** Lit une section de transitions dans une chaine (qui est creee). */
static markov_t *lire_chaine(lecteur_t *lecteur, const char *titre,
                             unsigned int nb_etats_attendu, unsigned int graine)
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
  bool valide = lire_section(lecteur, titre, &nb_etats) && nb_etats == nb_etats_attendu;

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

/**
 * Verifie qu'une transition e -> d (d != 0) de la chaine enchaine bien deux
 * etats : les mots de d sont ceux de e decales d'un rang, et le bigramme
 * (dernier mot de e, dernier mot de d) est connu du lexique.
 */
static bool transition_coherente(const modele_t *m, unsigned int e, unsigned int d)
{
  unsigned int r;
  bool valide;

  if (m->ordre == 1u) {
    valide = (hachage_paires_identifiant(m->bigrammes, e, d) != HACHAGE_ABSENT);
  } else if (m->ordre == 2u) {
    valide = (hachage_paires_premier(m->bigrammes, d) == hachage_paires_second(m->bigrammes, e));
  } else {
    valide = (hachage_paires_identifiant(m->bigrammes, dernier_mot(m, e), dernier_mot(m, d)) != HACHAGE_ABSENT);
    for (r = 0u; valide && r + 1u < m->ordre; r++) {
      valide = (hachage_nuplets_element(m->etats, d, r) == hachage_nuplets_element(m->etats, e, r + 1u));
    }
  }
  return valide;
}

/**
 * Verifie qu'un modele charge peut etre utilise sans risque : chaque etat
 * atteint a un voisin, chaque transition enchaine deux etats coherents,
 * chaque fin de phrase a une ponctuation et chaque mot est le dernier mot
 * d'un etat (on peut faire commencer une phrase par n'importe quel mot).
 * Renvoie false si le modele est incoherent ou si la memoire manque
 * (*erreur recoit EINVAL ou ENOMEM).
 */
static bool verifier_coherence(const modele_t *m, int *erreur)
{
  unsigned int nb_etats = markov_nb_etats(m->chaine);
  unsigned int nb_mots = hachage_nb_mots(m->mots);
  unsigned char *vus = calloc(nb_mots, sizeof(unsigned char));
  unsigned int e;
  unsigned int r;
  unsigned int d;
  unsigned int poids;
  bool valide = (vus != NULL);

  *erreur = valide ? EINVAL : ENOMEM;
  for (e = 0u; valide && e < nb_etats; e++) {
    vus[dernier_mot(m, e)] = 1u;
    valide = (e == REPERTOIRE_DEBUT_PHRASE || markov_nb_voisins(m->chaine, e) > 0u);
    for (r = 0u; valide && r < markov_nb_voisins(m->chaine, e); r++) {
      d = markov_voisin(m->chaine, e, r, &poids);
      if (d == REPERTOIRE_DEBUT_PHRASE) {
        /* fin de phrase : pas de phrase vide, une ponctuation possible */
        valide = (e != REPERTOIRE_DEBUT_PHRASE)
                 && markov_nb_voisins(m->ponctuation, REPERTOIRE_NB_FINS + dernier_mot(m, e)) > 0u;
      } else {
        valide = transition_coherente(m, e, d);
      }
    }
  }
  /* chaque mot termine au moins un etat */
  for (e = 0u; valide && e < nb_mots; e++) {
    valide = (vus[e] != 0u);
  }
  /* ponctuation : seuls les mots menent, et seulement a une ponctuation */
  for (e = 0u; valide && e < markov_nb_etats(m->ponctuation); e++) {
    for (r = 0u; valide && r < markov_nb_voisins(m->ponctuation, e); r++) {
      d = markov_voisin(m->ponctuation, e, r, &poids);
      valide = (e >= REPERTOIRE_NB_FINS && d < REPERTOIRE_NB_FINS);
    }
  }
  free(vus);
  return valide;
}

/** Lit tout le fichier (le modele m a deja ses tables vides). */
static void lire_modele(lecteur_t *lecteur, modele_t *m, unsigned int graine)
{
  choix_t choix = { NULL, NULL, NULL, NULL, NULL, NULL };
  char *curseur;
  unsigned int n = 0u;
  unsigned int b = 0u;
  unsigned int e = 0u;
  unsigned int nb_etats;
  unsigned int i;
  int erreur_coherence = 0;

  /* en-tete, ordre et nombre de mots */
  curseur = ligne_suivante(lecteur);
  if (curseur != NULL && strcmp(curseur, ENTETE_MODELE) != 0) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0 && lire_section(lecteur, "ordre", &m->ordre)
      && (m->ordre < 1u || m->ordre > MODELE_ORDRE_MAX)) {
    lecteur->erreur = EINVAL;
  }
  if (lecteur->erreur == 0 && m->ordre >= 3u) {
    m->etats = hachage_nuplets_creer(m->ordre, 0u);
    if (m->etats == NULL) {
      lecteur->erreur = ENOMEM;
    }
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
    choix.marques_bigramme = calloc(b, sizeof(unsigned int));
    if (choix.variante_bigramme == NULL || choix.casse_bigramme == NULL
        || choix.liaison_bigramme == NULL
        || choix.marques_bigramme == NULL) {
      lecteur->erreur = ENOMEM;
    }
  }
  for (i = 0u; lecteur->erreur == 0 && i < b; i++) {
    lire_bigramme(lecteur, m, &choix, i);
  }
  /* etats (ordre >= 3) */
  if (lecteur->erreur == 0 && m->ordre >= 3u
      && lire_section(lecteur, "etats", &e) && e == 0u) {
    lecteur->erreur = EINVAL;
  }
  for (i = 0u; lecteur->erreur == 0 && m->ordre >= 3u && i < e; i++) {
    lire_etat(lecteur, m, i);
  }
  /* chaines */
  if (lecteur->erreur == 0) {
    nb_etats = (m->ordre == 1u) ? n : ((m->ordre == 2u) ? b : e);
    m->chaine = lire_chaine(lecteur, "chaine", nb_etats, graine);
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
                             choix.liaison_bigramme[i],
                             choix.marques_bigramme[i]);
    }
    if (!verifier_coherence(m, &erreur_coherence)) {
      lecteur->erreur = erreur_coherence;
    }
  }
  free(choix.variante_mot);
  free(choix.casse_mot);
  free(choix.variante_bigramme);
  free(choix.casse_bigramme);
  free(choix.liaison_bigramme);
  free(choix.marques_bigramme);
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
  POSTCONDITION(m == NULL || (modele_ordre(m) >= 1u && modele_ordre(m) <= MODELE_ORDRE_MAX));
  return m;
}
